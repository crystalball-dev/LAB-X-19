// LabX19Render: drives the LAB X-19 processor offline.
//
//   LabX19Render --out <file.wav> [--preset <n|name>] [--notes 36,43] [--events "36@0-2:127,43@0.5-2"]
//                [--seconds 6] [--hold 4] [--velocity 0.8] [--sr 48000] [--block 512] [--bpm 128]
//                [--specimens <dir>] [--userfile <file>] [--set id=value ...] [--cc "1:127@0.5,64:0@2"]
//                [--program-at "4@2.5,7@5"] [--tail-from <seconds>] [--no-trim] [--mute-voices]
//                [--png <file.png>] [--roundtrip] [--list] [--list-params] [--list-specimens]
//
//   LabX19Render --fuzz --seconds 300 [--seed 1] [--vary-blocks] [--out <file.wav>]
//       Random session: notes, presets, parameter moves, mod wheel, bend, sustain. Every 30 s
//       the last 10 s are silent (all notes off, no events). Reports any level that rises
//       during those silent stretches, non-finite samples and per-block processing time.
//
// --bpm attaches a playing transport at that tempo, starting at beat 0.
// Output is trimmed by the plugin's reported latency (unless --no-trim), so a note sent at
// t seconds is heard at t seconds in the file.
// Prints key=value statistics for the analysis scripts.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

namespace
{
    struct NoteEvent
    {
        int note;
        double start, end;
        float velocity;
    };

    struct ProgramChange
    {
        int index;
        double time;
    };

    // A host transport that is always playing at a fixed tempo.
    struct TestPlayHead final : public juce::AudioPlayHead
    {
        double bpm = 120.0, sampleRate = 48000.0;
        juce::int64 position = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setTimeInSamples (position);
            info.setPpqPosition ((double) position / sampleRate * bpm / 60.0);
            info.setIsPlaying (true);
            return info;
        }
    };

    juce::String option (const juce::StringArray& args, const juce::String& key, const juce::String& fallback = {})
    {
        const int i = args.indexOf (key);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : fallback;
    }

    juce::StringArray allOptions (const juce::StringArray& args, const juce::String& key)
    {
        juce::StringArray out;
        for (int i = 0; i + 1 < args.size(); ++i)
            if (args[i] == key)
                out.add (args[i + 1]);
        return out;
    }

    int presetIndex (const juce::String& text)
    {
        const auto& presets = labx19::factoryPresets();
        if (text.containsOnly ("0123456789"))
            return juce::jlimit (0, (int) presets.size() - 1, text.getIntValue());
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].name.equalsIgnoreCase (text))
                return i;
        return -1;
    }

    bool setParam (LabX19AudioProcessor& proc, const juce::String& assignment)
    {
        const auto id = assignment.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = assignment.fromFirstOccurrenceOf ("=", false, false).trim().getFloatValue();
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            return true;
        }
        std::cerr << "unknown parameter: " << id << std::endl;
        return false;
    }

    std::vector<float> paramSnapshot (LabX19AudioProcessor& proc)
    {
        std::vector<float> v;
        for (const auto& s : labx19::paramSpecs())
            v.push_back (proc.apvts.getRawParameterValue (s.id)->load());
        return v;
    }

    double ticksToMs (juce::int64 ticks)
    {
        return juce::Time::highResolutionTicksToSeconds (ticks) * 1000.0;
    }

    struct BlockTimer
    {
        std::vector<double> ms;
        std::vector<double> budget;

        void add (double elapsedMs, double budgetMs) { ms.push_back (elapsedMs); budget.push_back (budgetMs); }

        juce::String summary() const
        {
            if (ms.empty())
                return {};
            auto sorted = ms;
            std::sort (sorted.begin(), sorted.end());
            const double p99 = sorted[(size_t) (0.999 * (double) (sorted.size() - 1))];
            int overruns = 0;
            for (size_t i = 0; i < ms.size(); ++i)
                if (ms[i] > 0.5 * budget[i])
                    ++overruns;
            return " block_max_ms=" + juce::String (sorted.back(), 3) + " block_p999_ms=" + juce::String (p99, 3)
                 + " half_budget_overruns=" + juce::String (overruns);
        }
    };

    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
        auto options = juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                       .withNumChannels (buffer.getNumChannels())
                                                       .withBitsPerSample (32)
                                                       .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        if (auto writer = wav.createWriterFor (stream, options))
            return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return false;
    }

    //==============================================================================
    int runFuzz (LabX19AudioProcessor& proc, double sampleRate, int baseBlock, double seconds, int seed,
                 bool varyBlocks, const juce::File& outFile)
    {
        juce::Random rnd (seed);
        const auto& specs = labx19::paramSpecs();
        const int total = (int) (seconds * sampleRate);
        const double cycle = 30.0, activeSpan = 20.0;

        juce::AudioBuffer<float> block (2, 4096);
        juce::AudioBuffer<float> full;
        if (outFile != juce::File())
        {
            full.setSize (2, total);
            full.clear();
        }

        std::vector<int> held;
        int events = 0, presetChanges = 0, paramChanges = 0;
        long nonFinite = 0;
        float peak = 0.0f;
        BlockTimer timer;

        // Silence analysis: 100 ms RMS windows during each idle stretch.
        const int window = (int) (0.1 * sampleRate);
        double windowSum = 0.0;
        int windowCount = 0, windowIndex = 0, idleStretches = 0;
        bool inIdle = false, voicesWereActive = false;
        float minSoFar = 1000.0f;
        float worstGrowthFx = -1000.0f, worstGrowthVoices = -1000.0f;
        double worstFxTime = 0.0;
        int growthFx = 0, growthVoices = 0;
        double nextEvent = 0.0;

        int pos = 0;
        while (pos < total)
        {
            const int n = varyBlocks ? std::min (total - pos, 16 + rnd.nextInt (1009)) : std::min (baseBlock, total - pos);
            const double t0 = (double) pos / sampleRate;
            const bool idleNow = std::fmod (t0, cycle) >= activeSpan;
            juce::MidiBuffer midi;

            if (idleNow && ! inIdle)
            {
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                held.clear();
                inIdle = true;
                ++idleStretches;
                windowSum = 0.0;
                windowCount = 0;
                windowIndex = 0;
                minSoFar = 1000.0f;
            }
            else if (! idleNow && inIdle)
            {
                inIdle = false;
            }

            if (idleNow)
            {
                nextEvent = (std::floor (t0 / cycle) + 1.0) * cycle;
            }
            else
            {
                const double blockEnd = t0 + (double) n / sampleRate;
                while (nextEvent < blockEnd)
                {
                    const int offset = juce::jlimit (0, n - 1, (int) ((nextEvent - t0) * sampleRate));
                    const float r = rnd.nextFloat();
                    if (r < 0.40f && held.size() < 10)
                    {
                        const int note = 24 + rnd.nextInt (60);
                        midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (30 + rnd.nextInt (98))), offset);
                        held.push_back (note);
                    }
                    else if (r < 0.70f && ! held.empty())
                    {
                        const int k = rnd.nextInt ((int) held.size());
                        midi.addEvent (juce::MidiMessage::noteOff (1, held[(size_t) k]), offset);
                        held.erase (held.begin() + k);
                    }
                    else if (r < 0.75f)
                    {
                        proc.setCurrentProgram (rnd.nextInt (proc.getNumPrograms()));
                        ++presetChanges;
                    }
                    else if (r < 0.87f)
                    {
                        const auto& s = specs[(size_t) rnd.nextInt ((int) specs.size())];
                        if (auto* p = proc.apvts.getParameter (s.id))
                            p->setValueNotifyingHost (rnd.nextFloat());
                        ++paramChanges;
                    }
                    else if (r < 0.92f)
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, 1, rnd.nextInt (128)), offset);
                    else if (r < 0.96f)
                        midi.addEvent (juce::MidiMessage::pitchWheel (1, rnd.nextInt (16384)), offset);
                    else if (r < 0.99f)
                        midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, rnd.nextBool() ? 127 : 0), offset);
                    else
                    {
                        midi.addEvent (juce::MidiMessage::allNotesOff (1), offset);
                        held.clear();
                    }
                    ++events;
                    nextEvent += 0.05 + rnd.nextDouble() * 0.55;
                }
            }

            juce::AudioBuffer<float> view (block.getArrayOfWritePointers(), 2, 0, n);
            const auto start = juce::Time::getHighResolutionTicks();
            proc.processBlock (view, midi);
            timer.add (ticksToMs (juce::Time::getHighResolutionTicks() - start), 1000.0 * (double) n / sampleRate);

            const bool voicesActive = proc.meters.activeVoices.load() > 0;
            const float* l = view.getReadPointer (0);
            const float* r = view.getReadPointer (1);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                {
                    ++nonFinite;
                    continue;
                }
                peak = std::max (peak, std::max (std::abs (l[i]), std::abs (r[i])));

                if (inIdle)
                {
                    windowSum += (double) (l[i] * l[i] + r[i] * r[i]);
                    if (++windowCount >= window)
                    {
                        const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (windowSum / (2.0 * windowCount)), -240.0f);
                        if (windowIndex >= 5 && rmsDb > -80.0f)
                        {
                            const float growth = rmsDb - minSoFar;
                            if (voicesWereActive)
                            {
                                worstGrowthVoices = std::max (worstGrowthVoices, growth);
                                if (growth > 6.0f)
                                    ++growthVoices;
                            }
                            else
                            {
                                if (growth > worstGrowthFx)
                                {
                                    worstGrowthFx = growth;
                                    worstFxTime = (double) (pos + i) / sampleRate;
                                }
                                if (growth > 6.0f)
                                    ++growthFx;
                            }
                        }
                        minSoFar = std::min (minSoFar, rmsDb);
                        voicesWereActive = voicesActive;
                        windowSum = 0.0;
                        windowCount = 0;
                        ++windowIndex;
                    }
                }
            }

            if (full.getNumSamples() > 0)
                for (int ch = 0; ch < 2; ++ch)
                    full.copyFrom (ch, pos, view, ch, 0, n);

            pos += n;
        }

        std::cout << "fuzz seed=" << seed << " seconds=" << seconds << " events=" << events
                  << " preset_changes=" << presetChanges << " param_changes=" << paramChanges
                  << " idle_stretches=" << idleStretches
                  << " growth_fx_only=" << growthFx << " worst_growth_fx_db=" << juce::String (worstGrowthFx, 1)
                  << " at_s=" << juce::String (worstFxTime, 2)
                  << " growth_with_voices=" << growthVoices << " worst_growth_voices_db=" << juce::String (worstGrowthVoices, 1)
                  << " non_finite=" << nonFinite
                  << " peak_db=" << juce::String (juce::Decibels::gainToDecibels (peak, -120.0f), 2)
                  << " faults=" << proc.meters.faults.load()
                  << " faults_logged=" << proc.flushFaultLog()
                  << timer.summary() << std::endl;
        const char* stages[] = { "voices", "grit", "ensemble", "delay", "reverb" };
        for (int k = 0; k < 5; ++k)
            std::cout << "stage_" << stages[k] << "_peak_db=" << juce::String (juce::Decibels::gainToDecibels (proc.stagePeak[(size_t) k], -120.0f), 1)
                      << " stage_" << stages[k] << "_hot_s=" << juce::String (proc.stageHotAt[(size_t) k] < 0 ? -1.0 : (double) proc.stageHotAt[(size_t) k] / sampleRate, 2) << " ";
        std::cout << std::endl;

        if (full.getNumSamples() > 0 && ! writeWav (outFile, full, sampleRate))
            std::cerr << "could not write " << outFile.getFullPathName() << std::endl;

        return (nonFinite == 0 && growthFx == 0) ? 0 : 1;
    }
}

// DelayLine reads compared with a double-precision reference, including delays that put the read position a
// fraction of a float's resolution below zero, where 0.1.1 read past the end of the buffer. The slot past the end
// holds a 1e6 sentinel, so a stray read shows up as a huge error (the 0.1.1 read fails this test).
static int selfTestDelayLine()
{
    labx19::dsp::DelayLine line;
    const int size = 1928;   // the ENSEMBLE line at 48 kHz
    line.prepare (size);
    juce::Random rnd (19);
    std::vector<double> history ((size_t) size, 0.0);
    double worst = 0.0;
    long checks = 0;

    for (int step = 0; step < 200000; ++step)
    {
        const float x = (float) rnd.nextDouble() * 2.0f - 1.0f;
        line.push (x);
        history[(size_t) (step % size)] = x;
        const int written = step + 1;   // samples pushed so far; the newest is 1 sample back

        float delays[4] = { 1.0f + (float) rnd.nextDouble() * (float) (size - 4),
                            (float) (line.write) + 1.0e-5f,          // write - delay just below zero
                            (float) (line.write) + 3.0e-5f,
                            (float) (size - 2) };
        for (float d : delays)
        {
            const double dd = std::clamp ((double) d, 1.0, (double) (size - 2));
            if (dd >= written)
                continue;
            const int whole = (int) dd;
            const double frac = dd - whole;
            const double a = history[(size_t) ((written - whole) % size)];
            const double b = history[(size_t) ((written - whole - 1 + size) % size)];
            const double ref = a + frac * (b - a);
            worst = std::max (worst, std::abs ((double) line.read (d) - ref));
            ++checks;
        }
    }
    std::cout << "delayline_checks=" << checks << " delayline_worst_error=" << worst << std::endl;
    return worst < 1.0e-3 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.contains ("--selftest-delay"))
        return selfTestDelayLine();

    if (args.contains ("--list"))
    {
        const auto& presets = labx19::factoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            std::cout << i << "\t" << presets[(size_t) i].name << std::endl;
        return 0;
    }

    if (args.contains ("--list-specimens"))
    {
        for (int i = 0; i < labx19::specimenCatalogSize; ++i)
        {
            const auto& e = labx19::specimenCatalog[i];
            std::cout << (i + 1) << "\t" << e.name << "\t" << e.rootNote << "\t" << e.relativePath << std::endl;
        }
        return 0;
    }

    if (args.contains ("--list-params"))
    {
        const auto& specs = labx19::paramSpecs();
        int count = 0;
        for (const auto& s : specs)
            if (s.id != nullptr)
            {
                std::cout << s.id << "\t" << s.name << std::endl;
                ++count;
            }
        std::cout << "param_count=" << count << " table_size=" << (int) specs.size() << std::endl;
        return count == (int) specs.size() ? 0 : 1;
    }

    const double sampleRate = option (args, "--sr", "48000").getDoubleValue();
    const int blockSize = option (args, "--block", "512").getIntValue();
    const double seconds = option (args, "--seconds", "6").getDoubleValue();
    const double hold = option (args, "--hold", "4").getDoubleValue();
    const float velocity = option (args, "--velocity", "0.8").getFloatValue();
    const juce::File outFile (option (args, "--out"));
    const int prepareBlock = args.contains ("--vary-blocks") ? 4096 : blockSize;

    auto proc = std::make_unique<LabX19AudioProcessor>();
    if (args.contains ("--fault-log"))
        proc->setFaultLogFile (juce::File (option (args, "--fault-log")));

    TestPlayHead playHead;
    const bool useTransport = args.contains ("--bpm");
    if (useTransport)
    {
        playHead.bpm = option (args, "--bpm").getDoubleValue();
        playHead.sampleRate = sampleRate;
        proc->setPlayHead (&playHead);
    }

    if (args.contains ("--specimens"))
        proc->setLibraryRoot (juce::File (option (args, "--specimens")), false);
    if (args.contains ("--userfile"))
        proc->getSpecimenLibrary().setUserFile (juce::File (option (args, "--userfile")));

    if (args.contains ("--preset"))
    {
        const int index = presetIndex (option (args, "--preset"));
        if (index < 0)
        {
            std::cerr << "unknown preset" << std::endl;
            return 2;
        }
        proc->setCurrentProgram (index);
    }

    for (const auto& assignment : allOptions (args, "--set"))
        if (! setParam (*proc, assignment))
            return 2;

    proc->setPlayConfigDetails (0, 2, sampleRate, prepareBlock);
    proc->prepareToPlay (sampleRate, prepareBlock);

    // One empty block issues the specimen request; then wait for the loader and start clean.
    {
        juce::AudioBuffer<float> warm (2, blockSize);
        juce::MidiBuffer none;
        proc->processBlock (warm, none);
        proc->getSpecimenLibrary().waitUntilSettled (15000);
        proc->getAirLibrary().waitUntilSettled (15000);
        proc->prepareToPlay (sampleRate, prepareBlock);
    }

    if (args.contains ("--fuzz"))
    {
        const int result = runFuzz (*proc, sampleRate, blockSize, seconds, option (args, "--seed", "1").getIntValue(),
                                    args.contains ("--vary-blocks"), outFile);
        proc->releaseResources();
        return result;
    }

    // Note events: "note@start-end" with an optional ":velocity" in MIDI units (1..127)
    std::vector<NoteEvent> events;
    if (args.contains ("--events"))
    {
        for (const auto& token : juce::StringArray::fromTokens (option (args, "--events"), ",", ""))
        {
            const int note = token.upToFirstOccurrenceOf ("@", false, false).getIntValue();
            auto times = token.fromFirstOccurrenceOf ("@", false, false);
            float vel = velocity;
            if (times.contains (":"))
            {
                vel = juce::jlimit (1.0f, 127.0f, times.fromFirstOccurrenceOf (":", false, false).getFloatValue()) / 127.0f;
                times = times.upToFirstOccurrenceOf (":", false, false);
            }
            events.push_back ({ note, times.upToFirstOccurrenceOf ("-", false, false).getDoubleValue(),
                                times.fromFirstOccurrenceOf ("-", false, false).getDoubleValue(), vel });
        }
    }
    else if (! args.contains ("--mute-voices") && ! args.contains ("--no-notes"))
    {
        for (const auto& token : juce::StringArray::fromTokens (option (args, "--notes", "48"), ",", ""))
            events.push_back ({ token.getIntValue(), 0.0, hold, velocity });
    }

    std::vector<ProgramChange> programChanges;
    for (const auto& token : juce::StringArray::fromTokens (option (args, "--program-at"), ",", ""))
        programChanges.push_back ({ token.upToFirstOccurrenceOf ("@", false, false).getIntValue(),
                                    token.fromFirstOccurrenceOf ("@", false, false).getDoubleValue() });

    proc->muteVoices = args.contains ("--mute-voices");
    if (args.contains ("--inject"))
    {
        // "--inject <stage>@<seconds>": stage 0-4 = voices, grit, ensemble, delay, reverb sums; 6 = one voice
        const auto spec = option (args, "--inject");
        proc->injectStage = spec.upToFirstOccurrenceOf ("@", false, false).getIntValue();
        proc->injectAtSample = (juce::int64) (spec.fromFirstOccurrenceOf ("@", false, false).getDoubleValue() * sampleRate);
    }

    // Controller events: "cc:value@seconds"
    struct CcEvent { int cc, value; double time; };
    std::vector<CcEvent> ccs;
    for (const auto& token : juce::StringArray::fromTokens (option (args, "--cc"), ",", ""))
    {
        const auto head = token.upToFirstOccurrenceOf ("@", false, false);
        ccs.push_back ({ head.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                         head.fromFirstOccurrenceOf (":", false, false).getIntValue(),
                         token.fromFirstOccurrenceOf ("@", false, false).getDoubleValue() });
    }

    const int latency = args.contains ("--no-trim") ? 0 : proc->getLatencySamples();
    const int total = (int) (seconds * sampleRate);
    const int rendered = total + latency;
    juce::AudioBuffer<float> raw (2, rendered);
    raw.clear();
    BlockTimer timer;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < rendered; pos += blockSize)
    {
        const int n = std::min (blockSize, rendered - pos);

        for (const auto& pc : programChanges)
        {
            const int at = (int) (pc.time * sampleRate);
            if (at >= pos && at < pos + n)
                proc->setCurrentProgram (pc.index);
        }

        juce::AudioBuffer<float> view (raw.getArrayOfWritePointers(), 2, pos, n);
        juce::MidiBuffer midi;
        for (const auto& e : events)
        {
            const int on = (int) (e.start * sampleRate);
            const int off = (int) (e.end * sampleRate);
            if (on >= pos && on < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, e.note, e.velocity), on - pos);
            if (off >= pos && off < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, e.note), off - pos);
        }
        for (const auto& c : ccs)
        {
            const int at = (int) (c.time * sampleRate);
            if (at >= pos && at < pos + n)
                midi.addEvent (juce::MidiMessage::controllerEvent (1, c.cc, c.value), at - pos);
        }
        playHead.position = pos;
        const auto start = juce::Time::getHighResolutionTicks();
        proc->processBlock (view, midi);
        timer.add (ticksToMs (juce::Time::getHighResolutionTicks() - start), 1000.0 * (double) n / sampleRate);
    }
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;

    juce::AudioBuffer<float> output (2, total);
    for (int ch = 0; ch < 2; ++ch)
        output.copyFrom (ch, 0, raw, ch, latency, total);

    // Statistics
    int nonFinite = 0;
    float peak = 0.0f, maxJump = 0.0f;
    double sumSquares = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = output.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
        {
            if (! std::isfinite (d[i]))
            {
                ++nonFinite;
                continue;
            }
            peak = std::max (peak, std::abs (d[i]));
            sumSquares += (double) d[i] * d[i];
            if (i > 0)
                maxJump = std::max (maxJump, std::abs (d[i] - d[i - 1]));
        }
    }
    const double rms = std::sqrt (sumSquares / std::max (1, total * 2));
    const double realtimeFactor = ((double) rendered / sampleRate * 1000.0) / std::max (0.001, elapsedMs);
    auto& lib = proc->getSpecimenLibrary();

    std::cout << "preset=\"" << proc->getProgramName (proc->getCurrentProgram()) << "\""
              << " specimen=\"" << lib.getStatusText() << "\""
              << " air=\"" << proc->getAirLibrary().getStatusText() << "\""
              << " table_hz=" << juce::String (lib.getTableHz(), 2)
              << " table_frames=" << lib.getPeriodicFrames()
              << " latency=" << proc->getLatencySamples()
              << " seconds=" << seconds
              << " peak_db=" << juce::String (juce::Decibels::gainToDecibels (peak, -120.0f), 2)
              << " rms_db=" << juce::String (juce::Decibels::gainToDecibels ((float) rms, -120.0f), 2)
              << " max_jump=" << juce::String (maxJump, 4)
              << " non_finite=" << nonFinite
              << " faults=" << proc->meters.faults.load()
              << " faults_logged=" << proc->flushFaultLog()
              << " render_ms=" << juce::String (elapsedMs, 1)
              << " realtime_x=" << juce::String (realtimeFactor, 1)
              << timer.summary();

    if (args.contains ("--tail-from"))
    {
        // Level contour after the given time: does anything rise once playing has stopped?
        const int from = juce::jlimit (0, total, (int) (option (args, "--tail-from").getDoubleValue() * sampleRate));
        const int window = (int) (0.1 * sampleRate);
        float tailPeak = 0.0f, minSoFar = 1000.0f, worstRise = -1000.0f;
        int clipped = 0, index = 0;
        for (int w = from; w + window <= total; w += window, ++index)
        {
            double sum = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = output.getReadPointer (ch);
                for (int i = w; i < w + window; ++i)
                {
                    sum += (double) d[i] * d[i];
                    tailPeak = std::max (tailPeak, std::abs (d[i]));
                    if (std::abs (d[i]) > 0.8f)
                        ++clipped;
                }
            }
            const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (2.0 * window)), -240.0f);
            if (index >= 5 && rmsDb > -80.0f)
                worstRise = std::max (worstRise, rmsDb - minSoFar);
            minSoFar = std::min (minSoFar, rmsDb);
        }
        std::cout << " tail_peak_db=" << juce::String (juce::Decibels::gainToDecibels (tailPeak, -120.0f), 2)
                  << " tail_rise_db=" << juce::String (worstRise, 1)
                  << " tail_clipped=" << clipped;
    }
    std::cout << std::endl;

    if (outFile != juce::File() && ! writeWav (outFile, output, sampleRate))
    {
        std::cerr << "could not write " << outFile.getFullPathName() << std::endl;
        return 3;
    }

    if (args.contains ("--roundtrip"))
    {
        juce::MemoryBlock state;
        proc->getStateInformation (state);
        const auto before = paramSnapshot (*proc);

        auto other = std::make_unique<LabX19AudioProcessor>();
        other->setCurrentProgram ((proc->getCurrentProgram() + 5) % other->getNumPrograms());
        other->setStateInformation (state.getData(), (int) state.getSize());
        const auto after = paramSnapshot (*other);

        int mismatches = 0;
        for (size_t i = 0; i < before.size(); ++i)
            if (std::abs (before[i] - after[i]) > 1.0e-5f)
            {
                ++mismatches;
                std::cout << "roundtrip mismatch: " << labx19::paramSpecs()[i].id << " " << before[i] << " vs " << after[i] << std::endl;
            }
        std::cout << "roundtrip=" << (mismatches == 0 ? "PASS" : "FAIL") << " state_bytes=" << (int) state.getSize() << std::endl;
        if (mismatches != 0)
            return 4;
    }

    if (args.contains ("--png"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditor());
        if (auto* labEditor = dynamic_cast<LabX19AudioProcessorEditor*> (editor.get()))
            for (int i = 0; i < 12; ++i)
                labEditor->refreshFromProcessor();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const juce::File pngFile (option (args, "--png"));
        pngFile.deleteFile();
        juce::FileOutputStream stream (pngFile);
        juce::PNGImageFormat png;
        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::cerr << "could not write " << pngFile.getFullPathName() << std::endl;
            return 5;
        }
        std::cout << "png=" << pngFile.getFullPathName() << " size=" << image.getWidth() << "x" << image.getHeight() << std::endl;
        editor.reset();
    }

    proc->setPlayHead (nullptr);
    proc->releaseResources();
    return nonFinite == 0 ? 0 : 1;
}
