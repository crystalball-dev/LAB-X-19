#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include "SpecimenLibrary.h"
#include "Config.h"

namespace labx19
{
    namespace
    {
        constexpr double maxSeconds = 60.0;     // longest stretch of a file that is decoded
        constexpr float targetRms = 0.25f;      // -12 dBFS after normalisation (grain copy)
        constexpr float strikeRms = 0.3f;       // loudest 400 ms of the Strike copy
        constexpr float peakCeiling = 2.0f;     // sources may exceed full scale; the voice stages tame them
        constexpr float frameRms = 0.35f;       // wavetable frames are levelled to this RMS
        constexpr float periodicConfidence = 0.3f;

        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = pluginName;
            o.filenameSuffix = ".settings";
            o.folderName = vendorName;
            o.osxLibrarySubFolder = "Application Support";
            o.commonToAllUsers = false;
            return o;
        }

        // Some game files decode with a burst of garbage: Shadow of Chornobyl's psy_blackout_l.ogg starts with
        // 238 samples reaching +71 dBFS. Non-finite samples and anything above +24 dBFS are silenced together
        // with 10 ms either side, so one bad burst can neither dominate the normalisation nor reach the output.
        int silenceDecodeBursts (float* d, int length, double sampleRate)
        {
            const int pad = std::max (1, (int) (sampleRate * 0.010));
            int bursts = 0, clearedUntil = -1;
            for (int i = 0; i < length; ++i)
            {
                if (std::isfinite (d[i]) && std::abs (d[i]) <= 16.0f)
                    continue;
                ++bursts;
                const int from = std::max (clearedUntil + 1, i - pad);
                const int to = std::min (length - 1, i + pad);
                for (int k = from; k <= to; ++k)
                    d[k] = 0.0f;
                clearedUntil = std::max (clearedUntil, to);
            }
            return bursts;
        }

        // Many recordings swell from silence to a single loud moment. Grains can start anywhere,
        // so the slow loudness contour is flattened (a quarter of the original swing is kept)
        // while everything faster than ~50 ms, the texture itself, is left untouched.
        void flattenDynamics (float* d, int length, double sampleRate)
        {
            const int frame = std::max (64, (int) (sampleRate * 0.05));
            const int numFrames = (length + frame - 1) / frame;
            if (numFrames < 3)
                return;

            std::vector<float> env ((size_t) numFrames);
            for (int f = 0; f < numFrames; ++f)
            {
                const int start = f * frame;
                const int end = std::min (length, start + frame);
                double sum = 0.0;
                for (int i = start; i < end; ++i)
                    sum += (double) d[i] * d[i];
                env[(size_t) f] = (float) std::sqrt (sum / (double) std::max (1, end - start));
            }

            std::vector<float> smooth (env.size());
            for (int f = 0; f < numFrames; ++f)
            {
                const float prev = env[(size_t) std::max (0, f - 1)];
                const float next = env[(size_t) std::min (numFrames - 1, f + 1)];
                smooth[(size_t) f] = 0.25f * prev + 0.5f * env[(size_t) f] + 0.25f * next;
            }

            auto sorted = smooth;
            std::sort (sorted.begin(), sorted.end());
            const float reference = std::max (1.0e-5f, sorted[(size_t) (0.8f * (float) (numFrames - 1))]);

            std::vector<float> gains (smooth.size());
            for (size_t f = 0; f < smooth.size(); ++f)
                gains[f] = std::clamp (std::pow (reference / std::max (smooth[f], 1.0e-6f), 0.75f), 0.25f, 16.0f);

            for (int i = 0; i < length; ++i)
            {
                const float pos = ((float) i + 0.5f) / (float) frame - 0.5f;
                const int f0 = std::clamp ((int) std::floor (pos), 0, numFrames - 1);
                const int f1 = std::min (f0 + 1, numFrames - 1);
                const float t = std::clamp (pos - (float) f0, 0.0f, 1.0f);
                d[i] *= gains[(size_t) f0] + (gains[(size_t) f1] - gains[(size_t) f0]) * t;
            }
        }

        // Period of the loudest partial between 20 Hz and 1 kHz. A Hann-windowed, zero-padded FFT finds the
        // partial; normalised autocorrelation within 15 % of its period refines the lag to a fraction of
        // a sample. The loudest hum becomes the table's fundamental, so a table played at E2 sounds at E2.
        // confidence is the correlation at the chosen lag.
        struct PeriodDetector
        {
            static constexpr int order = 14;
            static constexpr int size = 1 << order;

            juce::dsp::FFT fft { order };
            std::vector<float> buffer = std::vector<float> ((size_t) size * 2, 0.0f);

            double detect (const float* x, int w, double sr, float& confidence)
            {
                confidence = 0.0f;
                w = std::min (w, size);

                std::fill (buffer.begin(), buffer.end(), 0.0f);
                for (int i = 0; i < w; ++i)
                    buffer[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (dsp::twoPi * (float) i / (float) (w - 1)));
                fft.performFrequencyOnlyForwardTransform (buffer.data(), true);

                const int lo = std::max (2, (int) std::ceil (20.0 * size / sr));
                const int hi = std::min (size / 2 - 2, (int) (1000.0 * size / sr));
                if (hi <= lo + 2)
                    return 0.0;

                int k = lo;
                for (int i = lo; i <= hi; ++i)
                    if (buffer[(size_t) i] > buffer[(size_t) k])
                        k = i;
                if (buffer[(size_t) k] <= 1.0e-9f)
                    return 0.0;

                const double a = std::log ((double) buffer[(size_t) k - 1] + 1.0e-12);
                const double b = std::log ((double) buffer[(size_t) k] + 1.0e-12);
                const double c = std::log ((double) buffer[(size_t) k + 1] + 1.0e-12);
                const double d = a - 2.0 * b + c;
                const double bin = (double) k + (std::abs (d) > 1.0e-12 ? std::clamp (0.5 * (a - c) / d, -0.5, 0.5) : 0.0);
                const double period0 = (double) size / bin;   // samples per cycle of the loudest partial

                const int minLag = std::max (2, (int) std::floor (period0 * 0.85));
                const int maxLag = std::min ((int) std::ceil (period0 * 1.15), (int) (w * 0.6));
                if (maxLag <= minLag + 2)
                    return 0.0;

                std::vector<double> sq ((size_t) w + 1, 0.0);
                for (int i = 0; i < w; ++i)
                    sq[(size_t) i + 1] = sq[(size_t) i] + (double) x[i] * (double) x[i];

                auto corr = [&] (int lag)
                {
                    const int len = w - lag;
                    double acc = 0.0;
                    for (int i = 0; i < len; ++i)
                        acc += (double) x[i] * (double) x[i + lag];
                    return acc / std::sqrt (std::max (sq[(size_t) len] * (sq[(size_t) w] - sq[(size_t) lag]), 1.0e-20));
                };

                int best = minLag;
                double bestR = -2.0;
                for (int lag = minLag; lag <= maxLag; ++lag)
                {
                    const double r = corr (lag);
                    if (r > bestR)
                    {
                        bestR = r;
                        best = lag;
                    }
                }

                const double ra = corr (best - 1), rb = bestR, rc = corr (best + 1);
                const double dd = ra - 2.0 * rb + rc;
                const double shift = std::abs (dd) > 1.0e-9 ? std::clamp (0.5 * (ra - rc) / dd, -0.5, 0.5) : 0.0;
                confidence = (float) rb;
                return (double) best + shift;
            }
        };
    }

    //==============================================================================
    dsp::WavetableData SpecimenLibrary::buildTable (const float* d, int length, double sr)
    {
        using WT = dsp::WavetableData;
        constexpr int N = WT::frameSize;
        constexpr int frames = WT::numFrames;

        WT table;
        if (d == nullptr || length < 512 || sr <= 0.0)
            return table;

        table.samples.assign ((size_t) frames * (size_t) WT::numMips * (size_t) WT::stride, 0.0f);

        const int window = std::min (4096, length);
        std::vector<std::vector<float>> cycles ((size_t) frames, std::vector<float> ((size_t) N, 0.0f));
        std::vector<float> detected;
        PeriodDetector detector;

        for (int f = 0; f < frames; ++f)
        {
            auto& cyc = cycles[(size_t) f];
            const int centre = (int) (((double) f + 0.5) / (double) frames * (double) length);
            const int start = std::clamp (centre - window / 2, 0, length - window);

            float confidence = 0.0f;
            const double period = detector.detect (d + start, window, sr, confidence);
            bool periodic = confidence >= periodicConfidence && period > 2.0;

            if (periodic)
            {
                // One period from the rising zero crossing nearest the centre, cross-faded with the period
                // before it so the cycle repeats without a seam.
                const double lo = period + 3.0;
                const double hi = (double) length - period - 4.0;
                if (hi <= lo)
                {
                    periodic = false;
                }
                else
                {
                    const int c = std::clamp (centre, (int) lo + 1, (int) hi - 1);
                    double t0 = (double) c;
                    bool found = false;
                    for (int off = 0; off <= (int) period && ! found; ++off)
                        for (int sign : { 1, -1 })
                        {
                            const int i = c + sign * off;
                            if ((double) (i - 1) < lo || (double) i > hi)
                                continue;
                            if (d[i - 1] <= 0.0f && d[i] > 0.0f)
                            {
                                t0 = (double) (i - 1) + (double) d[i - 1] / (double) (d[i - 1] - d[i]);
                                found = true;
                                break;
                            }
                        }

                    for (int k = 0; k < N; ++k)
                    {
                        const double s = (double) k / (double) N;
                        const float a = dsp::hermite (d, t0 + s * period);
                        const float b = dsp::hermite (d, t0 - period + s * period);
                        cyc[(size_t) k] = a * (float) (1.0 - s) + b * (float) s;
                    }
                    detected.push_back ((float) (sr / period));
                    ++table.periodicFrames;
                }
            }

            if (! periodic)
            {
                // Noise-like material: a 2048-sample loop, cross-faded with the block before it.
                if (length >= 2 * N + 2)
                {
                    const int s0 = std::clamp (centre - N / 2, N, length - N - 1);
                    for (int k = 0; k < N; ++k)
                    {
                        const float s = (float) k / (float) N;
                        cyc[(size_t) k] = d[s0 + k] * (1.0f - s) + d[s0 - N + k] * s;
                    }
                }
                else
                {
                    for (int k = 0; k < N; ++k)
                    {
                        const float w = 0.5f - 0.5f * std::cos (dsp::twoPi * (float) k / (float) N);
                        cyc[(size_t) k] = d[std::min (k * length / N, length - 1)] * w;
                    }
                }
            }

            // Remove DC, level to a common RMS, keep peaks at or below full scale.
            double mean = 0.0;
            for (float v : cyc)
                mean += v;
            mean /= (double) N;
            double energy = 0.0;
            float peak = 0.0f;
            for (auto& v : cyc)
            {
                v -= (float) mean;
                energy += (double) v * v;
                peak = std::max (peak, std::abs (v));
            }
            const float rms = (float) std::sqrt (energy / (double) N);
            if (rms > 1.0e-7f)
            {
                float gain = frameRms / rms;
                if (peak * gain > 1.0f)
                    gain = 1.0f / peak;
                for (auto& v : cyc)
                    v *= gain;
            }
        }

        // Band-limited copies: one FFT per frame, then every mip keeps only the harmonics that stay
        // below 19 kHz across its octave.
        juce::dsp::FFT fft (11);
        std::vector<float> spectrum ((size_t) 2 * N), work ((size_t) 2 * N);
        for (int f = 0; f < frames; ++f)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            std::copy (cycles[(size_t) f].begin(), cycles[(size_t) f].end(), spectrum.begin());
            fft.performRealOnlyForwardTransform (spectrum.data(), true);
            spectrum[0] = spectrum[1] = 0.0f;   // no DC

            for (int mip = 0; mip < WT::numMips; ++mip)
            {
                const int keep = WT::harmonicsFor (mip);
                work = spectrum;
                for (int k = keep + 1; k <= N / 2; ++k)
                    work[(size_t) (2 * k)] = work[(size_t) (2 * k + 1)] = 0.0f;
                fft.performRealOnlyInverseTransform (work.data());

                float* dst = table.frame (f, mip);
                std::copy (work.begin(), work.begin() + N, dst);
                dst[N] = dst[0];
            }
        }

        if (! detected.empty())
        {
            auto sorted = detected;
            std::sort (sorted.begin(), sorted.end());
            table.nativeHz = sorted[sorted.size() / 2];
        }
        table.valid = true;
        return table;
    }

    //==============================================================================
    SpecimenLibrary::SpecimenLibrary (const juce::String& threadName)
        : juce::Thread (threadName)
    {
        formats.registerBasicFormats();

        settings = std::make_unique<juce::PropertiesFile> (settingsOptions());
        juce::File saved (settings->getValue ("specimenLibraryRoot"));
        root = looksLikeLibrary (saved) ? saved : autodetectLibrary();

        startThread (juce::Thread::Priority::low);
    }

    SpecimenLibrary::~SpecimenLibrary()
    {
        stopThread (5000);
        active.store (nullptr);
        activeOwner.reset();
        retired.clear();
    }

    bool SpecimenLibrary::looksLikeLibrary (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return false;
        for (auto game : specimenLibraryGames)
            if (folder.getChildFile (game).getChildFile ("sounds").isDirectory())
                return true;
        return false;
    }

    juce::File SpecimenLibrary::autodetectLibrary()
    {
        for (auto drive : { "C", "D", "E", "F", "G", "H" })
        {
            const juce::File candidate (juce::String (drive) + ":\\" + juce::String (specimenLibrarySubPath).replace ("/", "\\"));
            if (looksLikeLibrary (candidate))
                return candidate;
        }
        return {};
    }

    void SpecimenLibrary::requestChoice (int choice) noexcept
    {
        requested.store (choice);
    }

    const dsp::SpecimenData* SpecimenLibrary::acquireForBlock() noexcept
    {
        // Sequentially consistent on purpose: pairs with publish() so a retired buffer can
        // only be freed after the block that might still read it has finished.
        epoch.fetch_add (1);
        return active.load();
    }

    void SpecimenLibrary::setLibraryRoot (const juce::File& folder, bool persist)
    {
        {
            const juce::ScopedLock sl (lock);
            if (folder == root)
                return;
            root = folder;
        }
        if (persist && settings != nullptr)
        {
            settings->setValue ("specimenLibraryRoot", folder.getFullPathName());
            settings->saveIfNeeded();
        }
        generation.fetch_add (1);
        notify();
    }

    juce::File SpecimenLibrary::getLibraryRoot() const
    {
        const juce::ScopedLock sl (lock);
        return root;
    }

    void SpecimenLibrary::setUserFile (const juce::File& file)
    {
        {
            const juce::ScopedLock sl (lock);
            if (file == userFile)
                return;
            userFile = file;
        }
        generation.fetch_add (1);
        notify();
    }

    juce::File SpecimenLibrary::getUserFile() const
    {
        const juce::ScopedLock sl (lock);
        return userFile;
    }

    juce::String SpecimenLibrary::getStatusText() const
    {
        const juce::ScopedLock sl (lock);
        return statusText;
    }

    bool SpecimenLibrary::isSettled() const noexcept
    {
        return handledChoice.load() == requested.load()
            && handledGeneration.load() == generation.load()
            && status.load() != Status::loading;
    }

    bool SpecimenLibrary::waitUntilSettled (int timeoutMs) const
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (isSettled())
                return true;
            juce::Thread::sleep (10);
        }
        return false;
    }

    void SpecimenLibrary::setStatus (Status s, const juce::String& text)
    {
        {
            const juce::ScopedLock sl (lock);
            statusText = text;
        }
        status.store (s);
    }

    void SpecimenLibrary::run()
    {
        while (! threadShouldExit())
        {
            reclaim();

            const int choice = requested.load();
            const int gen = generation.load();

            if (choice >= 0 && (choice != handledChoice.load() || gen != handledGeneration.load()))
            {
                load (choice);
                handledGeneration.store (gen);
                handledChoice.store (choice);
                continue;
            }

            wait (40);
        }
    }

    void SpecimenLibrary::load (int choice)
    {
        juce::File file;
        juce::String name;
        float rootNote = -1.0f;
        {
            const juce::ScopedLock sl (lock);
            if (choice == specimenUserChoice)
            {
                file = userFile;
                name = userFile.getFileNameWithoutExtension();
            }
            else if (juce::isPositiveAndNotGreaterThan (choice, specimenCatalogSize))
            {
                const auto& entry = specimenCatalog[choice - 1];
                name = entry.name;
                rootNote = entry.rootNote;
                if (root.isDirectory())
                    file = root.getChildFile (entry.relativePath);
            }
        }

        auto fail = [this] (Status s, const juce::String& text)
        {
            publish (nullptr);
            tableHz.store (0.0f);
            periodicFrames.store (0);
            setStatus (s, text);
        };

        if (choice != specimenUserChoice && file == juce::File())
            return fail (Status::missingLibrary, "SPECIMEN LIBRARY NOT FOUND / SET FOLDER");

        if (choice == specimenUserChoice && file == juce::File())
            return fail (Status::missingFile, "NO USER FILE / LOAD ONE");

        if (! file.existsAsFile())
            return fail (Status::missingFile, "FILE NOT FOUND: " + file.getFileName());

        setStatus (Status::loading, "LOADING " + name.toUpperCase());

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
            return fail (Status::failed, "UNREADABLE: " + file.getFileName());

        const auto maxSamples = (juce::int64) (reader->sampleRate * maxSeconds);
        const int length = (int) std::min (reader->lengthInSamples, maxSamples);
        const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
        const double sr = reader->sampleRate;

        juce::AudioBuffer<float> decoded (channels, length);
        decoded.clear();
        reader->read (decoded.getArrayOfWritePointers(), channels, 0, length);

        if (threadShouldExit())
            return;

        auto data = std::make_unique<dsp::SpecimenData>();
        data->storage.assign ((size_t) length + 2 * dsp::SpecimenData::guard, 0.0f);
        data->length = length;
        data->sampleRate = sr;
        data->id = nextId++;

        float* d = data->storage.data() + dsp::SpecimenData::guard;
        for (int i = 0; i < length; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < channels; ++ch)
                s += decoded.getSample (ch, i);
            d[i] = s / (float) channels;
        }

        silenceDecodeBursts (d, length, sr);

        // Strike copy: the recording's own dynamics, levelled on its loudest 400 ms.
        {
            const int window = std::max (64, (int) (0.4 * sr));
            double best = 0.0, sum = 0.0;
            float peak = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                sum += (double) d[i] * d[i];
                if (i >= window)
                    sum -= (double) d[i - window] * d[i - window];
                best = std::max (best, sum);
                peak = std::max (peak, std::abs (d[i]));
            }
            const float loudest = (float) std::sqrt (std::max (0.0, best) / (double) std::min (window, length));
            float gain = 1.0f;
            if (loudest > 1.0e-6f && peak > 0.0f)
            {
                gain = std::min (strikeRms / loudest, peakCeiling / peak);
                for (int i = 0; i < length; ++i)
                    d[i] *= gain;
            }

            // Onset: the attack starts 2 ms before the signal first reaches 5 % of its peak.
            const float threshold = 0.05f * peak * gain;
            int onset = 0;
            while (onset < length && std::abs (d[onset]) < threshold)
                ++onset;
            data->onset = std::clamp (onset - (int) (0.002 * sr), 0, std::max (0, length - 1));
        }

        // Grain copy: slow contour flattened, levelled to a common RMS.
        data->flatStorage = data->storage;
        float* f = data->flatStorage.data() + dsp::SpecimenData::guard;
        flattenDynamics (f, length, sr);
        {
            double sumSquares = 0.0;
            float peak = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                sumSquares += (double) f[i] * (double) f[i];
                peak = std::max (peak, std::abs (f[i]));
            }
            const float rms = (float) std::sqrt (sumSquares / (double) length);
            if (rms > 1.0e-6f && peak > 0.0f)
            {
                const float gain = std::min (targetRms / rms, peakCeiling / peak);
                for (int i = 0; i < length; ++i)
                    f[i] *= gain;
            }
        }

        data->table = buildTable (f, length, sr);
        const auto& table = data->table;
        const bool periodic = table.isPeriodic();
        const float hz = periodic ? table.nativeHz : 0.0f;
        const int frames = table.periodicFrames;

        // A user file has no catalogue root: use the pitch the table cutter found, else middle C.
        data->rootNote = rootNote >= 0.0f ? rootNote : (periodic ? dsp::hzToMidi (hz) : 60.0f);

        publish (std::move (data));
        tableHz.store (hz);
        periodicFrames.store (frames);
        setStatus (Status::ready, name.toUpperCase() + " / " + juce::String ((double) length / sr, 1) + " S / "
                                  + (periodic ? juce::String (juce::roundToInt (hz)) + " HZ" : juce::String ("NOISY")));
    }

    void SpecimenLibrary::publish (std::unique_ptr<dsp::SpecimenData> next)
    {
        active.exchange (next.get());
        const auto e = epoch.load();
        if (activeOwner != nullptr)
            retired.emplace_back (std::move (activeOwner), e);
        activeOwner = std::move (next);
    }

    void SpecimenLibrary::reclaim()
    {
        const auto now = epoch.load();
        retired.erase (std::remove_if (retired.begin(), retired.end(),
                                       [now] (const auto& r) { return now > r.second + 1; }),
                       retired.end());
    }
}
