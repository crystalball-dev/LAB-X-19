#pragma once

#include <vector>
#include "Common.h"

namespace labx19::dsp
{
    // Single-cycle frames cut from a recording, each stored as band-limited copies (mip levels),
    // one per octave of playback pitch.
    struct WavetableData
    {
        static constexpr int frameSize = 2048;
        static constexpr int numFrames = 16;
        static constexpr int numMips = 11;
        static constexpr int stride = frameSize + 1;   // one guard sample for interpolation
        static constexpr float lowestHz = 8.1758f;     // MIDI note 0
        static constexpr double topPartialHz = 19000.0;

        std::vector<float> samples;   // [frame][mip][stride]
        float nativeHz = 0.0f;        // median detected fundamental; 0 when the source is noise-like
        int periodicFrames = 0;
        bool valid = false;

        const float* frame (int f, int mip) const noexcept
        {
            return samples.data() + ((size_t) f * (size_t) numMips + (size_t) mip) * (size_t) stride;
        }

        float* frame (int f, int mip) noexcept
        {
            return samples.data() + ((size_t) f * (size_t) numMips + (size_t) mip) * (size_t) stride;
        }

        // Mip level L serves fundamentals from lowestHz * 2^L to twice that.
        static int mipFor (float hz) noexcept
        {
            const int m = (int) std::floor (std::log2 (std::max (hz, lowestHz) / lowestHz));
            return std::clamp (m, 0, numMips - 1);
        }

        // Highest harmonic kept at mip level L, so nothing above topPartialHz is ever played.
        static int harmonicsFor (int mip) noexcept
        {
            const double base = (double) lowestHz * std::exp2 ((double) mip);
            return std::clamp ((int) std::floor (topPartialHz / (2.0 * base)), 1, frameSize / 2 - 1);
        }

        bool isPeriodic() const noexcept { return valid && periodicFrames * 2 >= numFrames; }
    };

    // Reads the table at frame position pos (0..1 across all frames) and phase t (cycles, 0..1).
    inline float readTable (const WavetableData& w, float pos, float t, int mip) noexcept
    {
        constexpr int size = WavetableData::frameSize;
        const float x = std::clamp (pos, 0.0f, 1.0f) * (float) (WavetableData::numFrames - 1);
        const int f0 = std::min ((int) x, WavetableData::numFrames - 2);
        const float ff = x - (float) f0;

        const float idx = (t - std::floor (t)) * (float) size;
        int i = (int) idx;
        if (i >= size) i = size - 1;
        const float fr = idx - (float) i;

        const float* a = w.frame (f0, mip);
        const float* b = w.frame (f0 + 1, mip);
        const float va = a[i] + fr * (a[i + 1] - a[i]);
        const float vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }

    // Decoded mono copies of one source file plus the wavetable cut from it. storage keeps the recording's own
    // dynamics (Strike plays it like an instrument sample); flatStorage has its slow loudness contour flattened
    // (grains can start anywhere). Guard zeros on both ends let the Hermite reader run without bounds checks.
    struct SpecimenData
    {
        static constexpr int guard = 4;
        std::vector<float> storage, flatStorage;
        int length = 0;
        double sampleRate = 44100.0;
        uint64_t id = 0;   // unique per load, so a reused memory address is never mistaken for the same source
        float rootNote = 60.0f;   // MIDI note at which the file plays at its recorded pitch
        int onset = 0;            // first sample of the attack, where Strike starts at position 0
        WavetableData table;

        const float* data() const noexcept { return storage.data() + guard; }
        const float* flat() const noexcept { return flatStorage.data() + guard; }
    };
}
