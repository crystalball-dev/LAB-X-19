#pragma once

#include <vector>
#include "Common.h"
#include "Filters.h"

namespace labx19::dsp
{
    //==============================================================================
    // GRIT: the gritty undertone under clean chords. Only the band above CORNER is driven, and the result is
    // added in parallel, so the harmony and the low end stay clear. DUST is crackle and hiss that follows the
    // signal's envelope: silence stays silent.
    class Grit
    {
    public:
        enum Mode { tube = 0, tape, radio, crush };

        void prepare (float sr) noexcept
        {
            sampleRate = sr;
            followRelease = std::exp (-1.0f / (0.08f * sr));
            rng.seed (0x6A09E667u);
            reset();
        }

        void reset() noexcept
        {
            for (auto& c : ch)
                c = {};
            lastCorner = -1.0f;
            lastMode = -1;
        }

        void process (float* left, float* right, int n, int mode, float amount, float driveDb, float cornerHz, float dust) noexcept
        {
            amount = std::clamp (amount, 0.0f, 1.0f);
            dust = std::clamp (dust, 0.0f, 1.0f);
            if (amount < 1.0e-4f && dust < 1.0e-4f)
            {
                lastMode = -1;   // filters restart cleanly when GRIT comes back
                return;
            }

            if (mode != lastMode)
            {
                for (auto& c : ch)
                    c = {};
                lastMode = mode;
                lastCorner = -1.0f;
            }

            const float corner = std::max (cornerHz, mode == radio ? 400.0f : 60.0f);
            if (std::abs (corner - lastCorner) > 0.5f)
            {
                lastCorner = corner;
                const float g = SVF::gFor (corner, sampleRate);
                for (auto& c : ch)
                {
                    c.hp.set (g, 1.4142f);
                    c.hp2.set (g, 1.4142f);
                }
            }

            const float g = dbToGain (std::clamp (driveDb, 0.0f, 48.0f));
            const float comp = 0.5f / fastTanh (0.5f * std::max (g, 1.0f));
            const float postHz = mode == tape ? 6500.0f : (mode == radio ? 3000.0f : 9000.0f);
            const float postCoef = onePoleCoefHz (std::min (postHz, sampleRate * 0.45f), sampleRate);

            const float crushAmount = std::clamp (driveDb / 36.0f, 0.0f, 1.0f);
            const int holdSamples = 1 + (int) (crushAmount * 11.0f);
            const float levels = std::exp2 (12.0f - 8.0f * crushAmount);

            const float crackleChance = dust * dust * 40.0f / sampleRate;
            const float hissGain = dust * 0.04f;
            const float clickCoef = std::exp (-1.0f / (0.0015f * sampleRate));

            float* io[2] = { left, right };
            for (int c = 0; c < 2; ++c)
            {
                auto& s = ch[(size_t) c];
                float* x = io[c];
                for (int i = 0; i < n; ++i)
                {
                    const float in = x[i];

                    float lp, bp, hp;
                    s.hp.process (in, lp, bp, hp);

                    float wet = 0.0f;
                    if (amount > 1.0e-4f)
                    {
                        const float d = hp * g;
                        switch (mode)
                        {
                            case tube:  wet = (fastTanh (d + 0.25f) - fastTanh (0.25f)) * comp; break;
                            case tape:  wet = fastTanh (d) * comp; break;
                            case radio: wet = std::clamp (d, -1.0f, 1.0f) * comp; break;
                            default:
                            {
                                if (--s.holdCount <= 0)
                                {
                                    s.held = std::round (hp * levels) / levels;
                                    s.holdCount = holdSamples;
                                }
                                wet = s.held;
                                break;
                            }
                        }
                        // A second high-pass after the shaper: the first one's phase-shifted residue of the low band and
                        // any difference tones that land below the corner never reach the output, so the low end of a
                        // chord stays exactly as it was.
                        float lp2, bp2, hp2;
                        s.hp2.process (wet, lp2, bp2, hp2);
                        s.post1 += (hp2 - s.post1) * postCoef;
                        s.post2 += (s.post1 - s.post2) * postCoef;
                        wet = s.post2;
                    }

                    float dustOut = 0.0f;
                    if (dust > 1.0e-4f)
                    {
                        const float a = std::abs (in);
                        s.env = a > s.env ? a : s.env * followRelease;
                        if (rng.uni() < crackleChance)
                            s.click += rng.bi() * s.env * 0.8f;
                        s.click *= clickCoef;
                        s.hiss += (rng.bi() - s.hiss) * 0.35f;
                        dustOut = s.click + s.hiss * s.env * hissGain;
                    }

                    x[i] = in + wet * amount + dustOut;
                }
            }
        }

    private:
        struct Channel
        {
            SVF hp, hp2;
            float post1 = 0.0f, post2 = 0.0f, held = 0.0f, env = 0.0f, click = 0.0f, hiss = 0.0f;
            int holdCount = 0;
        };

        std::array<Channel, 2> ch {};
        Rng rng;
        float sampleRate = 48000.0f, followRelease = 0.999f, lastCorner = -1.0f;
        int lastMode = -1;
    };

    //==============================================================================
    // Fractional delay line with linear interpolation.
    struct DelayLine
    {
        std::vector<float> buffer;
        int write = 0, size = 0;

        void prepare (int samples)
        {
            size = std::max (4, samples);
            buffer.assign ((size_t) size, 0.0f);
            write = 0;
        }

        void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); write = 0; }

        void push (float x) noexcept
        {
            buffer[(size_t) write] = x;
            if (++write >= size)
                write = 0;
        }

        // delay in samples, measured back from the most recent push (1 = the latest sample)
        // The whole and fractional parts of the delay are split before any wrap-around, so the indices are pure
        // integer arithmetic. (0.1.1 and earlier wrapped a float position: write - delay a hair below zero, plus
        // size, rounded to exactly size and read one sample past the end of the buffer. That garbage was the
        // ENSEMBLE blowout logged in FL.)
        float read (float delay) const noexcept
        {
            delay = std::clamp (delay, 1.0f, (float) (size - 2));
            const int whole = (int) delay;
            const float frac = delay - (float) whole;
            int a = write - whole;   // whole samples back (write - 1 is the newest)
            if (a < 0)
                a += size;
            int b = a - 1;           // one further back
            if (b < 0)
                b += size;
            return buffer[(size_t) a] + frac * (buffer[(size_t) b] - buffer[(size_t) a]);
        }
    };

    //==============================================================================
    // ENSEMBLE: a three-tap, string-machine style chorus. Each side reads three delay taps swept by phase-shifted
    // slow sines plus a faster shimmer, so a single saw becomes a section.
    class Ensemble
    {
    public:
        void prepare (float sr)
        {
            sampleRate = sr;
            for (auto& l : lines)
                l.prepare ((int) (0.04f * sr) + 8);
            reset();
        }

        void reset() noexcept
        {
            for (auto& l : lines)
                l.reset();
            phaseSlow = 0.0f;
            phaseFast = 0.0f;
        }

        void process (float* left, float* right, int n, float mix, float depth, float rateHz) noexcept
        {
            mix = std::clamp (mix, 0.0f, 1.0f);
            if (mix < 1.0e-4f)
            {
                // Keep the lines filled so turning it up never plays stale audio.
                for (int i = 0; i < n; ++i)
                {
                    lines[0].push (left[i]);
                    lines[1].push (right[i]);
                }
                return;
            }

            const float slowInc = std::clamp (rateHz, 0.01f, 10.0f) / sampleRate;
            const float fastInc = 6.3f / sampleRate;
            const float baseDelay = 0.008f * sampleRate;
            const float sweep = 0.006f * sampleRate * std::clamp (depth, 0.0f, 1.0f);
            const float shimmer = 0.00012f * sampleRate * std::clamp (depth, 0.0f, 1.0f);
            const float dryGain = 1.0f - 0.45f * mix;
            const float wetGain = 0.75f * mix;

            for (int i = 0; i < n; ++i)
            {
                lines[0].push (left[i]);
                lines[1].push (right[i]);
                phaseSlow = wrap01 (phaseSlow + slowInc);
                phaseFast = wrap01 (phaseFast + fastInc);

                float wl = 0.0f, wr = 0.0f;
                for (int t = 0; t < 3; ++t)
                {
                    const float off = (float) t / 3.0f;
                    const float fast = sin2pi (phaseFast + off) * shimmer;
                    const float dl = baseDelay + sweep * (0.5f + 0.5f * sin2pi (phaseSlow + off)) + fast;
                    const float dr = baseDelay + sweep * (0.5f + 0.5f * sin2pi (phaseSlow + off + 0.5f)) - fast;
                    wl += lines[0].read (dl);
                    wr += lines[1].read (dr);
                }
                left[i]  = left[i]  * dryGain + wl * (wetGain / 3.0f) * 1.7f;
                right[i] = right[i] * dryGain + wr * (wetGain / 3.0f) * 1.7f;
            }
        }

    private:
        std::array<DelayLine, 2> lines {};
        float sampleRate = 48000.0f, phaseSlow = 0.0f, phaseFast = 0.0f;
    };

    //==============================================================================
    // DELAY: stereo or ping-pong echoes with a darkening, soft-limited feedback path.
    class EchoDelay
    {
    public:
        void prepare (float sr)
        {
            sampleRate = sr;
            for (auto& l : lines)
                l.prepare ((int) (2.6f * sr) + 8);
            hpCoef = onePoleCoefHz (120.0f, sr);
            reset();
        }

        void reset() noexcept
        {
            for (auto& l : lines)
                l.reset();
            lpL = lpR = hpL = hpR = 0.0f;
            currentDelay = -1.0f;
        }

        void process (float* left, float* right, int n, float mix, float delaySeconds, float feedback, float toneHz,
                      bool pingPong) noexcept
        {
            mix = std::clamp (mix, 0.0f, 1.0f);
            const float target = std::clamp (delaySeconds * sampleRate, 16.0f, (float) lines[0].size - 4.0f);
            if (currentDelay < 0.0f)
                currentDelay = target;
            feedback = std::clamp (feedback, 0.0f, 0.95f);
            const float lpCoef = onePoleCoefHz (std::min (toneHz, sampleRate * 0.45f), sampleRate);

            if (mix < 1.0e-4f && feedback < 1.0e-4f)
            {
                for (int i = 0; i < n; ++i)
                {
                    lines[0].push (0.0f);
                    lines[1].push (0.0f);
                }
                currentDelay = target;
                return;
            }

            for (int i = 0; i < n; ++i)
            {
                // Glide toward a new time like a tape head, about 50 ms.
                currentDelay += (target - currentDelay) * 0.0004f;

                const float dl = lines[0].read (currentDelay);
                const float dr = lines[1].read (currentDelay);

                lpL += (dl - lpL) * lpCoef;
                lpR += (dr - lpR) * lpCoef;
                hpL += (lpL - hpL) * hpCoef;
                hpR += (lpR - hpR) * hpCoef;
                const float fl = fastTanh ((lpL - hpL) * feedback * 1.1f) / 1.1f;
                const float fr = fastTanh ((lpR - hpR) * feedback * 1.1f) / 1.1f;

                float inL, inR;
                if (pingPong)
                {
                    inL = 0.5f * (left[i] + right[i]) + fr;
                    inR = fl;
                }
                else
                {
                    inL = left[i] + fl;
                    inR = right[i] + fr;
                }
                lines[0].push (inL);
                lines[1].push (inR);

                left[i]  = left[i]  * (1.0f - 0.3f * mix) + dl * mix;
                right[i] = right[i] * (1.0f - 0.3f * mix) + dr * mix;
            }
        }

    private:
        std::array<DelayLine, 2> lines {};
        float sampleRate = 48000.0f, currentDelay = -1.0f, hpCoef = 0.01f;
        float lpL = 0.0f, lpR = 0.0f, hpL = 0.0f, hpR = 0.0f;
    };

    //==============================================================================
    // REVERB: LAB X-3's NOOSPHERE (8-line FDN, Hadamard mixing, damping, slow modulation) with a pre-delay and
    // SHIMMER: the tail is pitched up an octave and fed back into the network, so it rises as it fades. Each pass
    // climbs an octave into the damping and the shimmer's own 6 kHz low-pass, so the loop always dies away.
    class ShimmerReverb
    {
    public:
        static constexpr int numLines = 8;

        void prepare (float sr)
        {
            sampleRate = sr;
            const int maxLen = (int) std::ceil (sr * 0.205f) + 16;
            for (auto& line : lines)
                line.prepare (maxLen);

            const float g = SVF::gFor (80.0f, sr);
            hpL.set (g, 1.4142f);
            hpR.set (g, 1.4142f);

            preL.prepare ((int) (0.26f * sr) + 8);
            preR.prepare ((int) (0.26f * sr) + 8);
            shift.prepare (shiftSize);
            shiftWindow = std::min ((float) shiftSize - 8.0f, 0.06f * sr);
            shimHpCoef = onePoleCoefHz (300.0f, sr);
            shimLpCoef = onePoleCoefHz (6000.0f, sr);

            scale = scaleTarget;
            mix = mixTarget;
            reset();
        }

        void reset() noexcept
        {
            for (auto& line : lines)
            {
                line.reset();
                lineLp[(size_t) (&line - lines.data())] = 0.0f;
            }
            for (auto& v : lineLp)
                v = 0.0f;
            hpL.reset();
            hpR.reset();
            preL.reset();
            preR.reset();
            shift.reset();
            shiftPhase = 0.0f;
            shimHp = shimLp = shimOut = 0.0f;
            lfoPhase1 = 0.0f;
            lfoPhase2 = 0.37f;
            quietCounter = 0;
            asleep = false;
        }

        // Clears the tail and jumps straight to the current settings (preset changes).
        void flush() noexcept
        {
            reset();
            scale = scaleTarget;
            mix = mixTarget;
        }

        // size, decay, damping, mix, shimmer: 0..1; predelay in seconds. Call at control rate.
        void setParameters (float size, float decay, float damping, float newMix, float newShimmer, float predelaySeconds) noexcept
        {
            scaleTarget = 0.3f + 2.2f * std::clamp (size, 0.0f, 1.0f);
            rt60 = 0.3f * std::exp2 (std::clamp (decay, 0.0f, 1.0f) * 6.0f);
            dampCoef = onePoleCoefHz (9000.0f * std::exp2 (-std::clamp (damping, 0.0f, 1.0f) * 2.6f), sampleRate);
            mixTarget = std::clamp (newMix, 0.0f, 1.0f);
            shimmer = std::clamp (newShimmer, 0.0f, 1.0f);
            predelay = std::clamp (predelaySeconds * sampleRate, 1.0f, 0.25f * sampleRate);
        }

        float getRt60() const noexcept { return rt60; }

        void process (float* left, float* right, int n) noexcept
        {
            if (mixTarget < 1.0e-4f && mix < 1.0e-4f)
            {
                // Fully dry for a second: clear the network and stop processing until it is needed again.
                if (asleep)
                    return;
                quietCounter += n;
                if (quietCounter > (int) sampleRate)
                {
                    reset();
                    asleep = true;
                    return;
                }
            }
            else
            {
                quietCounter = 0;
                asleep = false;
            }

            float gains[numLines];
            float gainSum = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                const float delaySamples = baseMs[k] * 0.001f * sampleRate * scaleTarget;
                gains[k] = std::pow (10.0f, -3.0f * delaySamples / (rt60 * sampleRate));
                gainSum += gains[k];
            }
            const float gMean = gainSum / (float) numLines;
            const float energyRatio = std::max (1.0f - gMean * gMean, 0.0004f) / 0.19f;
            const float inputGain = 0.35f * std::pow (energyRatio, 0.2f);

            const float lfoInc1 = 0.13f / sampleRate, lfoInc2 = 0.21f / sampleRate;
            const float modDepth = 0.0006f * sampleRate;
            const float shimGain = 0.55f * shimmer;
            const float shiftInc = 1.0f / shiftWindow;

            for (int i = 0; i < n; ++i)
            {
                scale += (scaleTarget - scale) * 0.0005f;
                mix += (mixTarget - mix) * 0.002f;
                lfoPhase1 = wrap01 (lfoPhase1 + lfoInc1);
                lfoPhase2 = wrap01 (lfoPhase2 + lfoInc2);
                const float m1 = sin2pi (lfoPhase1) * modDepth;
                const float m2 = sin2pi (lfoPhase2) * modDepth;

                float o[numLines];
                for (int k = 0; k < numLines; ++k)
                {
                    float d = baseMs[k] * 0.001f * sampleRate * scale;
                    if (k == 0 || k == 5) d += m1;
                    if (k == 2 || k == 7) d += m2;
                    o[k] = lines[(size_t) k].read (d);
                }

                const float wetL = o[0] - o[2] + o[4] - o[6];
                const float wetR = o[1] - o[3] + o[5] - o[7];

                float f[numLines];
                for (int k = 0; k < numLines; ++k)
                {
                    lineLp[(size_t) k] += (o[k] - lineLp[(size_t) k]) * dampCoef;
                    f[k] = lineLp[(size_t) k] * gains[k];
                }
                hadamard (f);

                // SHIMMER: an octave up via two crossfaded read heads sliding through a short buffer.
                float shimmerIn = 0.0f;
                if (shimGain > 1.0e-4f)
                {
                    shift.push (0.5f * (wetL + wetR));
                    shiftPhase = wrap01 (shiftPhase + shiftInc);
                    const float p2 = wrap01 (shiftPhase + 0.5f);
                    const float d1 = 2.0f + shiftWindow * (1.0f - shiftPhase);
                    const float d2 = 2.0f + shiftWindow * (1.0f - p2);
                    const float w1 = sin2pi (0.5f * shiftPhase);
                    const float w2 = sin2pi (0.5f * p2);
                    const float pitched = shift.read (d1) * w1 * w1 + shift.read (d2) * w2 * w2;
                    shimLp += (pitched - shimLp) * shimLpCoef;
                    shimHp += (shimLp - shimHp) * shimHpCoef;
                    shimOut = fastTanh ((shimLp - shimHp) * shimGain);
                    shimmerIn = shimOut;
                }

                // Pre-delay, then the network's input (sub-bass kept out).
                preL.push (left[i]);
                preR.push (right[i]);
                float lp, bp, hpOutL, hpOutR;
                hpL.process (preL.read (predelay), lp, bp, hpOutL);
                hpR.process (preR.read (predelay), lp, bp, hpOutR);
                const float inL = (hpOutL + shimmerIn) * inputGain;
                const float inR = (hpOutR + shimmerIn) * inputGain;
                for (int k = 0; k < numLines; ++k)
                    lines[(size_t) k].push ((k & 1) == 0 ? f[k] + inL : f[k] + inR);

                const float dryGain = sin2pi (mix * 0.25f + 0.25f);   // cos (mix * pi / 2)
                const float wetGain = sin2pi (mix * 0.25f) * 0.5f;    // sin (mix * pi / 2)
                left[i]  = left[i]  * dryGain + wetL * wetGain;
                right[i] = right[i] * dryGain + wetR * wetGain;
            }
        }

    private:
        static void hadamard (float* x) noexcept
        {
            for (int h = 1; h < numLines; h <<= 1)
                for (int i = 0; i < numLines; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = x[j], b = x[j + h];
                        x[j] = a + b;
                        x[j + h] = a - b;
                    }
            const float norm = 0.35355339f;   // 1 / sqrt (8)
            for (int i = 0; i < numLines; ++i)
                x[i] *= norm;
        }

        static constexpr float baseMs[numLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
        static constexpr int shiftSize = 8192;

        std::array<DelayLine, numLines> lines {};
        std::array<float, numLines> lineLp {};
        DelayLine preL, preR, shift;
        SVF hpL, hpR;
        int quietCounter = 0;
        bool asleep = false;
        float sampleRate = 48000.0f;
        float scale = 1.4f, scaleTarget = 1.4f, rt60 = 2.4f, dampCoef = 0.5f;
        float mix = 0.2f, mixTarget = 0.2f, shimmer = 0.0f, predelay = 1.0f;
        float shiftWindow = 2880.0f, shiftPhase = 0.0f, shimHpCoef = 0.04f, shimLpCoef = 0.5f;
        float shimHp = 0.0f, shimLp = 0.0f, shimOut = 0.0f;
        float lfoPhase1 = 0.0f, lfoPhase2 = 0.37f;
    };
}
