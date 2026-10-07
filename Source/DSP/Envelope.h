#pragma once

#include "Common.h"

namespace labx19::dsp
{
    // Analog-style exponential ADSR. Retriggering continues from the current level, so it never clicks.
    class Envelope
    {
    public:
        enum class Stage { idle, attack, decay, sustain, release };

        void setSampleRate (float sr) noexcept { sampleRate = sr; }

        void setParameters (float attackS, float decayS, float sustainLevel, float releaseS) noexcept
        {
            attackCoef  = coefFor (attackS / 1.466f);   // reaches 1.0 in ~attackS while aiming at 1.3
            decayCoef   = coefFor (decayS / 4.6f);      // ~99 % of the way in decayS
            releaseCoef = coefFor (releaseS / 4.6f);
            sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
        }

        void noteOn() noexcept   { stage = Stage::attack; }
        void noteOff() noexcept  { if (stage != Stage::idle) stage = Stage::release; }
        void kill() noexcept     { stage = Stage::idle; value = 0.0f; }

        bool isActive() const noexcept    { return stage != Stage::idle; }
        bool isReleasing() const noexcept { return stage == Stage::release; }
        float getValue() const noexcept   { return value; }

        float next() noexcept
        {
            switch (stage)
            {
                case Stage::attack:
                    value += (1.3f - value) * attackCoef;
                    if (value >= 1.0f)
                    {
                        value = 1.0f;
                        stage = Stage::decay;
                    }
                    break;

                case Stage::decay:
                    value += (sustain - value) * decayCoef;
                    if (std::abs (value - sustain) < 1.0e-4f)
                    {
                        value = sustain;
                        stage = Stage::sustain;
                    }
                    break;

                case Stage::sustain:
                    value += (sustain - value) * 0.002f;   // follows sustain changes without zipper noise
                    break;

                case Stage::release:
                    value -= value * releaseCoef;
                    if (value < 1.0e-5f)
                    {
                        value = 0.0f;
                        stage = Stage::idle;
                    }
                    break;

                case Stage::idle:
                    break;
            }
            return value;
        }

    private:
        float coefFor (float tau) const noexcept { return onePoleCoef (std::max (tau, 2.0e-5f), sampleRate); }

        Stage stage = Stage::idle;
        float value = 0.0f, sustain = 1.0f;
        float attackCoef = 1.0f, decayCoef = 1.0f, releaseCoef = 1.0f;
        float sampleRate = 48000.0f;
    };
}
