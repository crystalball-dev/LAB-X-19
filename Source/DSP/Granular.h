#pragma once

#include "Common.h"
#include "Specimen.h"

namespace labx19::dsp
{
    // Per-voice stereo granular cloud reading the flattened copy of a SpecimenData buffer (after LAB X-3).
    // Each grain gets its own place in the stereo field, so a held chord becomes a wide, breathing pad.
    class GrainCloud
    {
    public:
        static constexpr int maxGrains = 24;

        struct Settings
        {
            float positionNorm = 0.3f;   // 0..1, already modulated
            float spray = 0.2f;          // 0..1: position scatter, pitch scatter and reversed grains
            float sizeSeconds = 0.12f;
            float density = 24.0f;       // grains per second
            float rate = 1.0f;           // playback ratio from key tracking
            float width = 0.8f;          // 0..1 stereo spread of the grains
        };

        void seed (uint32_t s) noexcept       { rng.seed (s); }
        void prepare (float sr) noexcept      { hostRate = sr; clear(); }
        void clear() noexcept                 { for (auto& g : grains) g.active = false; spawnPhase = 1.0f; }

        // New notes spawn a grain immediately. Grains already sounding fade out on their own window.
        void noteStarted() noexcept           { spawnPhase = 1.0f; }

        void process (const SpecimenData* sp, const Settings& s, float& outL, float& outR) noexcept
        {
            outL = outR = 0.0f;
            const uint64_t id = sp != nullptr ? sp->id : 0;
            if (id != lastId)
            {
                for (auto& g : grains)
                    g.active = false;
                lastId = id;
            }

            if (sp == nullptr || sp->length < 64)
                return;

            spawnPhase += s.density / hostRate;
            if (spawnPhase >= 1.0f)
            {
                spawnPhase -= 1.0f + 0.35f * rng.uni();   // irregular spacing
                spawn (*sp, s);
            }

            const float* d = sp->flat();
            const double maxPos = (double) sp->length - 2.0;

            for (auto& g : grains)
            {
                if (! g.active)
                    continue;

                const float w = sin2pi ((float) g.age * g.invLength * 0.5f);   // sin (pi * x)
                const float v = hermite (d, g.pos) * (w * w);
                outL += v * g.gainL;
                outR += v * g.gainR;
                g.pos += g.inc;

                if (++g.age >= g.length || g.pos < 1.0 || g.pos >= maxPos)
                    g.active = false;
            }
        }

    private:
        struct Grain
        {
            double pos = 0.0, inc = 1.0;
            float gainL = 0.0f, gainR = 0.0f, invLength = 0.0f;
            int length = 0, age = 0;
            bool active = false;
        };

        void spawn (const SpecimenData& sp, const Settings& s) noexcept
        {
            Grain* slot = nullptr;
            for (auto& g : grains)
            {
                if (! g.active)
                {
                    slot = &g;
                    break;
                }
            }
            if (slot == nullptr)
                return;

            const int len = std::max (32, (int) (s.sizeSeconds * hostRate));
            const float scatter = rng.bi() * s.spray * 0.12f;   // semitones
            double inc = (double) (s.rate * semisToRatio (scatter)) * sp.sampleRate / (double) hostRate;
            const bool reverse = rng.uni() < std::max (0.0f, s.spray - 0.5f) * 0.5f;
            const double span = inc * (double) len;
            const double usable = std::max (2.0, (double) sp.length - 4.0 - span);

            double start = (double) s.positionNorm * usable
                         + (double) (rng.bi() * s.spray) * 0.5 * (double) sp.length;
            start = std::clamp (start, 2.0, usable);

            if (reverse)
            {
                start = std::min (start + span, (double) sp.length - 3.0);
                inc = -inc;
            }

            slot->pos = start;
            slot->inc = inc;
            slot->length = len;
            slot->invLength = 1.0f / (float) len;
            slot->age = 0;

            const float overlap = std::clamp (s.density * s.sizeSeconds, 1.0f, (float) maxGrains);
            const float amp = (0.75f + 0.25f * rng.uni()) / std::sqrt (overlap);
            float l = 1.0f, r = 1.0f;
            panGains (rng.bi() * std::clamp (s.width, 0.0f, 1.0f), l, r);
            slot->gainL = amp * l;
            slot->gainR = amp * r;
            slot->active = true;
        }

        std::array<Grain, maxGrains> grains {};
        uint64_t lastId = 0;
        Rng rng;
        float hostRate = 48000.0f, spawnPhase = 1.0f;
    };

    // STRIKE: plays the recording once from the onset, pitched from its root note, like a sampler key.
    class StrikePlayer
    {
    public:
        void prepare (float sr) noexcept { hostRate = sr; active = false; }
        void stop() noexcept             { active = false; }

        void start (const SpecimenData* sp, float positionNorm) noexcept
        {
            active = sp != nullptr && sp->length > 64;
            if (! active)
                return;
            lastId = sp->id;
            const double from = (double) sp->onset;
            const double span = std::max (0.0, (double) sp->length - 64.0 - from);
            pos = from + (double) std::clamp (positionNorm, 0.0f, 1.0f) * span;
            // Starting inside the file needs a short fade; the onset itself keeps its attack.
            fadeIn = positionNorm > 0.001f ? 0.0f : 1.0f;
            fadeInStep = 1.0f / (0.002f * hostRate);
        }

        // rate: playback ratio relative to the recorded pitch.
        float process (const SpecimenData* sp, float rate) noexcept
        {
            if (! active)
                return 0.0f;
            if (sp == nullptr || sp->id != lastId)
            {
                active = false;   // the source changed under a ringing note
                return 0.0f;
            }

            const double end = (double) sp->length - 3.0;
            const double fadeStart = end - 0.005 * sp->sampleRate;
            float v = hermite (sp->data(), pos);
            if (pos > fadeStart)
                v *= (float) std::max (0.0, (end - pos) / (end - fadeStart));
            if (fadeIn < 1.0f)
            {
                v *= fadeIn;
                fadeIn = std::min (1.0f, fadeIn + fadeInStep);
            }

            pos += (double) rate * sp->sampleRate / (double) hostRate;
            if (pos >= end)
                active = false;
            return v;
        }

    private:
        double pos = 0.0;
        float hostRate = 48000.0f, fadeIn = 1.0f, fadeInStep = 0.01f;
        uint64_t lastId = 0;
        bool active = false;
    };
}
