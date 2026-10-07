#pragma once

#include "Config.h"
#include "Common.h"
#include "Filters.h"
#include "Oscillator.h"

namespace labx19::dsp
{
    // CHOIR: a small group of singers on one note. Each singer is a band-limited saw darkened toward a glottal
    // pulse, with its own detune, vibrato (fading in after the attack, as singers do) and slow pitch drift.
    // Singers alternate left and right; each side runs through four formant band-passes whose centres glide
    // between the vowels U, O, A, E and I. BREATH adds aspiration noise through the same formants.
    //
    // Formants: alto values from the Csound manual's formant table (frequency, bandwidth, level).
    class Choir
    {
    public:
        static constexpr int maxSingers = labx19::maxSingers;
        static constexpr int numFormants = 4;

        struct Settings
        {
            int singers = 4;
            float detuneCents = 12.0f;
            float vibrato = 0.35f;   // 0..1 -> up to 40 cents
            float breath = 0.2f;
            float vowel = 0.5f;      // 0 U, 0.25 O, 0.5 A, 0.75 E, 1 I
            float size = 0.0f;       // -1 small (formants up), +1 large (formants down)
        };

        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed * 2246822519u + 3266489917u);
            reset();
        }

        void reset() noexcept
        {
            for (auto& f : formL) f.reset();
            for (auto& f : formR) f.reset();
            for (auto& s : singer)
            {
                s.lp = 0.0f;
                s.drift = 0.0f;
            }
            age = 0.0f;
        }

        // New note: fresh phases so stacked notes never phase-cancel, vibrato starts again from nothing.
        void noteStarted (bool resetFilters) noexcept
        {
            for (int i = 0; i < maxSingers; ++i)
            {
                auto& s = singer[(size_t) i];
                s.phase = rng.uni();
                s.vibPhase = rng.uni();
                s.vibRate = 4.6f + 1.2f * rng.uni();
                s.jitter = rng.bi();
                s.driftTarget = rng.bi();
                s.driftCountdown = 0.0f;
            }
            if (resetFilters)
                reset();
            age = 0.0f;
        }

        // Once per control block: the note's phase increment (cycles per sample) and the settings.
        void begin (float baseInc, const Settings& s, int n) noexcept
        {
            numSingers = std::clamp (s.singers, 1, maxSingers);
            breath = std::clamp (s.breath, 0.0f, 1.0f);

            // Vibrato fades in over 0.35 s after a 0.15 s delay.
            age += (float) n / sampleRate;
            const float vibFade = std::clamp ((age - 0.15f) / 0.35f, 0.0f, 1.0f);
            const float vibCents = 40.0f * s.vibrato * s.vibrato * vibFade;

            const float blockSeconds = (float) n / sampleRate;
            for (int i = 0; i < numSingers; ++i)
            {
                auto& sg = singer[(size_t) i];
                const float spreadPos = numSingers == 1 ? 0.0f : 2.0f * (float) i / (float) (numSingers - 1) - 1.0f;

                // Drift: a slow random walk of up to 6 cents per singer.
                sg.driftCountdown -= blockSeconds;
                if (sg.driftCountdown <= 0.0f)
                {
                    sg.driftTarget = rng.bi();
                    sg.driftCountdown = 0.4f + 0.8f * rng.uni();
                }
                sg.drift += (sg.driftTarget - sg.drift) * std::min (1.0f, blockSeconds * 1.5f);

                sg.vibPhase = wrap01 (sg.vibPhase + sg.vibRate * blockSeconds);
                const float cents = spreadPos * s.detuneCents + sg.jitter * 0.15f * s.detuneCents
                                  + sg.drift * 6.0f + vibCents * sin2pi (sg.vibPhase);
                sg.inc = std::min (baseInc * std::exp2 (cents * (1.0f / 1200.0f)), 0.45f);

                // Gentle glottal tilt: a one-pole low-pass at ten times the pitch, between 1.5 and 8 kHz, so the
                // upper formants still have harmonics to shape.
                const float hz = sg.inc * sampleRate;
                sg.lpCoef = onePoleCoefHz (std::clamp (10.0f * hz, 1500.0f, std::min (8000.0f, 0.45f * sampleRate)), sampleRate);
                sg.left = (i % 2) == 0;
            }
            // With an odd count the centre singer feeds both sides.
            centreSinger = (numSingers % 2 == 1) ? numSingers / 2 : -1;

            const float norm = 1.0f / std::sqrt ((float) numSingers);
            singerGain = norm;

            // Formants for the vowel, shifted by SIZE.
            const float shift = std::exp2 (-0.4f * std::clamp (s.size, -1.0f, 1.0f));
            const float v = std::clamp (s.vowel, 0.0f, 1.0f) * 4.0f;
            const int a = std::min ((int) v, 3);
            const float t = v - (float) a;
            float energy = 0.0f;
            for (int f = 0; f < numFormants; ++f)
            {
                const auto& va = vowels[a];
                const auto& vb = vowels[a + 1];
                const float freq = std::exp (std::log (va.freq[f]) + t * (std::log (vb.freq[f]) - std::log (va.freq[f]))) * shift;
                const float bw = (va.bw[f] + t * (vb.bw[f] - va.bw[f])) * 1.6f;
                const float levelDb = va.db[f] + t * (vb.db[f] - va.db[f]);
                const float fc = std::min (freq, sampleRate * 0.45f);
                const float k = std::clamp (bw / fc, 0.02f, 2.0f);
                const float g = SVF::gFor (fc, sampleRate);
                formL[(size_t) f].set (g, k);
                formR[(size_t) f].set (g, k);
                // The table gives each formant's peak in the output spectrum. The saw source falls 6 dB per octave,
                // so each formant is lifted by its distance above the first one (capped at +21 dB).
                if (f == 0)
                    firstFormant = fc;
                const float tilt = std::clamp (fc / firstFormant, 1.0f, 11.0f);
                formantGain[(size_t) f] = k * dbToGain (levelDb) * tilt;   // k * band-pass = unity peak
                energy += dbToGain (2.0f * levelDb) * bw;
            }

            // Equal loudness across vowels: with the tilt above, each formant passes energy in proportion to
            // level^2 * bandwidth / F1^2. Normalised to the vowel A (F1 800 Hz, sum 187.5).
            vowelNorm = (firstFormant / std::sqrt (std::max (energy, 1.0f))) / 58.4f;
        }

        // One stereo sample.
        void next (float& outL, float& outR) noexcept
        {
            float l = 0.0f, r = 0.0f;
            for (int i = 0; i < numSingers; ++i)
            {
                auto& sg = singer[(size_t) i];
                sg.phase += sg.inc;
                sg.phase -= std::floor (sg.phase);
                const float raw = osc::saw (sg.phase, sg.inc);
                sg.lp += (raw - sg.lp) * sg.lpCoef;
                const float v = sg.lp;
                if (i == centreSinger)
                {
                    l += v * 0.7071f;
                    r += v * 0.7071f;
                }
                else if (sg.left)
                    l += v;
                else
                    r += v;
            }
            l *= singerGain;
            r *= singerGain;

            if (breath > 0.0f)
            {
                const float b = breath * 0.35f;
                l += rng.bi() * b;
                r += rng.bi() * b;
            }

            float yl = 0.0f, yr = 0.0f;
            for (int f = 0; f < numFormants; ++f)
            {
                yl += formL[(size_t) f].bandpass (l) * formantGain[(size_t) f];
                yr += formR[(size_t) f].bandpass (r) * formantGain[(size_t) f];
            }
            outL = yl * makeup * vowelNorm;
            outR = yr * makeup * vowelNorm;
        }

    private:
        struct Vowel
        {
            float freq[numFormants];
            float bw[numFormants];
            float db[numFormants];
        };

        // Ordered dark to bright: U, O, A, E, I.
        static constexpr Vowel vowels[5] = {
            { { 325.0f, 700.0f, 2530.0f, 3500.0f }, { 50.0f, 60.0f, 170.0f, 180.0f }, { 0.0f, -12.0f, -30.0f, -40.0f } },   // U
            { { 450.0f, 800.0f, 2830.0f, 3500.0f }, { 70.0f, 80.0f, 100.0f, 130.0f }, { 0.0f, -9.0f, -16.0f, -28.0f } },    // O
            { { 800.0f, 1150.0f, 2800.0f, 3500.0f }, { 80.0f, 90.0f, 120.0f, 130.0f }, { 0.0f, -4.0f, -20.0f, -36.0f } },   // A
            { { 400.0f, 1600.0f, 2700.0f, 3300.0f }, { 60.0f, 80.0f, 120.0f, 150.0f }, { 0.0f, -24.0f, -30.0f, -35.0f } },  // E
            { { 350.0f, 1700.0f, 2700.0f, 3700.0f }, { 50.0f, 100.0f, 120.0f, 150.0f }, { 0.0f, -20.0f, -30.0f, -36.0f } }, // I
        };

        struct Singer
        {
            float phase = 0.0f, inc = 0.0f, lp = 0.0f, lpCoef = 0.1f;
            float vibPhase = 0.0f, vibRate = 5.0f, jitter = 0.0f;
            float drift = 0.0f, driftTarget = 0.0f, driftCountdown = 0.0f;
            bool left = true;
        };

        std::array<Singer, maxSingers> singer {};
        std::array<SVF, numFormants> formL {}, formR {};
        std::array<float, numFormants> formantGain {};
        Rng rng;
        float sampleRate = 48000.0f, breath = 0.0f, singerGain = 1.0f, age = 0.0f, firstFormant = 500.0f, vowelNorm = 1.0f;
        int numSingers = 4, centreSinger = -1;
        static constexpr float makeup = 7.0f;    // tuned so a full choir sits near the level of one saw
    };
}
