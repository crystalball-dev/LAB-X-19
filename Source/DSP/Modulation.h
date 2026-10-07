#pragma once

#include "Common.h"
#include "Programmer.h"

namespace labx19::dsp
{
    // Shared transport view for one control block.
    struct Transport
    {
        double bpm = 128.0;        // host tempo, or the last one seen
        bool synced = false;       // host is playing and reports a musical position
        double ppq = 0.0;          // quarter notes at the start of this control block, valid when synced
    };

    // One global LFO. Free-running in Hz, or locked to the host's beat position when the host plays;
    // when it does not, synced rates free-run at the last known tempo. Drift is LAB X-3's PROGRAMMER walk.
    class Lfo
    {
    public:
        enum Shape { sine = 0, triangle, saw, square, sampleHold, drift };

        void prepare (float sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed * 747796405u + 2891336453u);
            walk.seed (seed * 1103515245u + 12345u);
            walk.reset (sr);
            reset();
        }

        void reset() noexcept
        {
            phase = 0.0;
            lastCycle = -1;
            held = rng.bi();
            value = 0.0f;
        }

        void retrigger() noexcept
        {
            phase = 0.0;
            held = rng.bi();
        }

        // Advances by n samples; returns the bipolar value for this block.
        float advance (int n, int shape, float rateHz, float syncBeats, const Transport& t, bool retrigMode) noexcept
        {
            const bool synced = syncBeats > 0.0f;
            const double cyclesPerSecond = synced ? (t.bpm / 60.0) / (double) syncBeats : (double) rateHz;
            bool wrapped = false;

            if (synced && t.synced && ! retrigMode)
            {
                const double cycles = t.ppq / (double) syncBeats;
                const auto index = (long long) std::floor (cycles);
                wrapped = index != lastCycle;
                lastCycle = index;
                phase = cycles - std::floor (cycles);
            }
            else
            {
                phase += cyclesPerSecond * (double) n / (double) sampleRate;
                if (phase >= 1.0)
                {
                    phase -= std::floor (phase);
                    wrapped = true;
                }
                lastCycle = -1;
            }

            if (wrapped)
                held = rng.bi();

            const float p = (float) phase;
            switch (shape)
            {
                case sine:       value = sin2pi (p); break;
                case triangle:   value = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); break;
                case saw:        value = 1.0f - 2.0f * p; break;
                case square:     value = p < 0.5f ? 1.0f : -1.0f; break;
                case sampleHold: value = held; break;
                default:         value = walk.advance (n, (float) cyclesPerSecond); break;
            }
            return value;
        }

        float getValue() const noexcept { return value; }
        double getPhase() const noexcept { return phase; }

    private:
        Rng rng;
        Programmer walk;
        float sampleRate = 48000.0f, value = 0.0f, held = 0.0f;
        double phase = 0.0;
        long long lastCycle = -1;
    };
}
