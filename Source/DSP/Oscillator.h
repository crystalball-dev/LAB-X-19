#pragma once

#include "Common.h"

namespace labx19::dsp::osc
{
    // Band-limited saw, falling edge at t = 0.
    inline float saw (float t, float dt) noexcept
    {
        return 2.0f * t - 1.0f - polyBlep (t, dt);
    }

    // Saw that morphs into a square: subtracting a half-cycle-shifted saw cancels the even harmonics.
    inline float sawMorph (float t, float dt, float shape) noexcept
    {
        const float s = saw (t, dt);
        if (shape <= 1.0e-4f)
            return s;
        return s - shape * saw (wrap01 (t + 0.5f), dt);
    }

    // Band-limited pulse with width pw (0..1), DC removed.
    inline float pulse (float t, float dt, float pw) noexcept
    {
        float s = t < pw ? 1.0f : -1.0f;
        s += polyBlep (t, dt);
        s -= polyBlep (wrap01 (t - pw + 1.0f), dt);
        return s - (2.0f * pw - 1.0f);
    }

    // Band-limited triangle: -1 at t = 0, +1 at t = 0.5, corners rounded with polyBLAMP.
    inline float triangle (float t, float dt) noexcept
    {
        float y = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
        y += 4.0f * dt * (polyBlamp (t, dt) - polyBlamp (wrap01 (t + 0.5f), dt));
        return y;
    }

    // Triangle shifted so that it starts at zero and rises, like a sine.
    inline float triangleFromZero (float t, float dt) noexcept
    {
        return triangle (wrap01 (t + 0.25f), dt);
    }

    // Sine fold of a signal in [-1, 1]: shape 0 leaves it alone, shape 1 folds it over itself.
    inline float fold (float s, float shape) noexcept
    {
        if (shape <= 1.0e-4f)
            return s;
        const float folded = sin2pi (0.25f * (1.0f + 4.0f * shape) * s);
        return s + (folded - s) * shape;
    }

    // HUM: sine with a second harmonic at -9 dB and a third at -18 dB, like the lab's transformers.
    inline float hum (float t) noexcept
    {
        return (sin2pi (t) + 0.355f * sin2pi (2.0f * t) + 0.126f * sin2pi (3.0f * t)) * 0.8f;
    }
}
