#pragma once

#include "Config.h"
#include "Choir.h"
#include "Common.h"
#include "Envelope.h"
#include "Filters.h"
#include "Granular.h"
#include "Oscillator.h"
#include "Specimen.h"

namespace labx19::dsp
{
    // Control-rate snapshot of everything a voice needs. Built by the processor every control block.
    struct VoiceParams
    {
        int   oscAWave = 0;
        float oscAPitch = 0.0f, oscAShape = 0.0f, oscALevel = 0.8f;   // pitch: octave, semitone and fine in semitones

        int   uniVoices = 1;
        float uniDetune = 18.0f, uniSpread = 0.7f;

        int   oscBWave = 0;
        float oscBPitch = 0.07f, oscBShape = 0.0f, oscBLevel = 0.0f;

        float subLevel = 0.0f, noiseLevel = 0.0f, noiseColour = 0.5f;

        float choirLevel = 0.0f, choirOctave = 0.0f;
        Choir::Settings choir;

        int   specMode = 1;
        float specLevel = 0.0f, specPosition = 0.0f, specSize = 0.18f, specDensity = 24.0f, specSpray = 0.2f;
        float specTrack = 1.0f, specTune = 0.0f;
        const SpecimenData* specimen = nullptr;

        float airLevel = 0.0f, airPosition = 0.4f, airSize = 0.26f, airTrack = 0.25f;
        const SpecimenData* air = nullptr;

        int   filterModel = 1;
        float cutoff = 6000.0f, resonance = 0.1f, driveDb = 0.0f, filterEnv = 0.0f, keytrack = 0.5f, filterVel = 0.3f;

        float a1 = 0.005f, d1 = 0.6f, s1 = 0.8f, r1 = 0.4f;
        float a2 = 0.005f, d2 = 0.5f, s2 = 0.3f, r2 = 0.5f;
        float ampVel = 0.5f;

        float glide = 0.0f, pitchBend = 0.0f;

        // Global modulation for this control block (LFO and DAWN)
        float modCutoffOct = 0.0f, modPitch = 0.0f, modShape = 0.0f, modVowel = 0.0f, modPosition = 0.0f;
    };

    // Per-note settings decided by the processor when the note starts.
    struct NoteStart
    {
        int note = 60;
        float velocity = 0.8f;
        int tag = 0;
        float startPitch = 60.0f;
        bool glideFromStart = false;
        uint64_t stamp = 0;
    };

    class Voice
    {
    public:
        static constexpr int maxUnison = labx19::maxUnison;

        void prepare (float newSampleRate, uint32_t seedValue) noexcept
        {
            sampleRate = newSampleRate;
            rng.seed (seedValue * 2654435761u + 12345u);
            grains.seed (seedValue * 69069u + 1u);
            grains.prepare (sampleRate);
            airGrains.seed (seedValue * 1664525u + 1013904223u);
            airGrains.prepare (sampleRate);
            strike.prepare (sampleRate);
            choir.prepare (sampleRate, seedValue);
            env1.setSampleRate (sampleRate);
            env2.setSampleRate (sampleRate);
            fadeStep = 1.0f / (0.005f * sampleRate);
            kill();
        }

        void noteOn (const NoteStart& s) noexcept
        {
            const bool wasActive = isActive();

            noteNumber = s.note;
            tag = s.tag;
            vel = std::clamp (s.velocity, 0.0f, 1.0f);
            keyDown = true;
            fading = false;
            fadeGain = 1.0f;
            targetPitch = (float) s.note;
            pitch = s.glideFromStart ? s.startPitch : (float) s.note;
            strikePending = true;

            if (! wasActive)
            {
                resetState();
                for (auto& ph : phaseA)
                    ph = rng.uni();
                phaseB = rng.uni();
                phaseSub = 0.0f;
                phaseTable = rng.uni();
            }

            choir.noteStarted (! wasActive);
            age = s.stamp;
            grains.noteStarted();
            airGrains.noteStarted();
            env1.noteOn();
            env2.noteOn();
        }

        // Legato slide: change the pitch target without retriggering anything.
        void legatoTo (int note, float velocity, int newTag) noexcept
        {
            noteNumber = note;
            tag = newTag;
            targetPitch = (float) note;
            vel = std::clamp (velocity, 0.0f, 1.0f);
            keyDown = true;
        }

        // Return to a held note after the newest one was released.
        void returnTo (int note, int newTag) noexcept
        {
            noteNumber = note;
            tag = newTag;
            targetPitch = (float) note;
        }

        void noteOff() noexcept { keyDown = false; env1.noteOff(); env2.noteOff(); }

        // Fades out over 5 ms and stops. Used for stolen voices.
        void fastRelease() noexcept
        {
            if (! isActive())
                return;
            fading = true;
            keyDown = false;
        }

        void kill() noexcept
        {
            env1.kill();
            env2.kill();
            strike.stop();
            keyDown = false;
            fading = false;
            fadeGain = 1.0f;
            tag = -1;
        }

        bool isActive() const noexcept       { return env1.isActive(); }
        bool isFading() const noexcept       { return fading; }
        bool isKeyDown() const noexcept      { return keyDown; }
        bool isReleasing() const noexcept    { return env1.isReleasing() || fading; }
        int getNote() const noexcept         { return noteNumber; }
        int getTag() const noexcept          { return tag; }
        float getPitch() const noexcept      { return pitch; }
        float getLevel() const noexcept      { return env1.getValue() * (fading ? fadeGain : 1.0f); }
        uint64_t getAge() const noexcept     { return age; }

        void render (float* outL, float* outR, int n, const VoiceParams& p) noexcept
        {
            env1.setParameters (p.a1, p.d1, p.s1, p.r1);
            env2.setParameters (p.a2, p.d2, p.s2, p.r2);

            // Portamento: exponential, about 95 % of the way after one glide time.
            if (p.glide > 0.0005f)
                pitch += (targetPitch - pitch) * (1.0f - std::exp (-(float) n * 3.0f / (p.glide * sampleRate)));
            else
                pitch = targetPitch;

            const float base = pitch + p.pitchBend + p.modPitch;
            const float maxInc = 0.45f;
            const float incAEnd = std::min (midiToHz (base + p.oscAPitch) / sampleRate, maxInc);
            const float incBEnd = std::min (midiToHz (base + p.oscBPitch) / sampleRate, maxInc);
            const float incSubEnd = std::min (midiToHz (base - 12.0f) / sampleRate, maxInc);

            if (freshStart)
            {
                incA = incAEnd;
                incB = incBEnd;
                incSub = incSubEnd;
            }

            const float invN = 1.0f / (float) n;
            const float dIncA = (incAEnd - incA) * invN;
            const float dIncB = (incBEnd - incB) * invN;
            const float dIncSub = (incSubEnd - incSub) * invN;

            // BONDS unison: symmetric detune, neighbours panned to alternate sides.
            const int numUnison = std::clamp (p.uniVoices, 1, maxUnison);
            std::array<float, maxUnison> ratio {}, gainL {}, gainR {};
            float maxRatio = 1.0f;
            for (int i = 0; i < numUnison; ++i)
            {
                const float off = numUnison == 1 ? 0.0f : 2.0f * (float) i / (float) (numUnison - 1) - 1.0f;
                ratio[(size_t) i] = std::exp2 (off * p.uniDetune * (1.0f / 1200.0f));
                maxRatio = std::max (maxRatio, ratio[(size_t) i]);
                float pan = 0.0f;
                if (numUnison > 1 && std::abs (off) > 1.0e-4f)
                    pan = ((i % 2) == 0 ? -1.0f : 1.0f) * p.uniSpread * (0.4f + 0.6f * std::abs (off));
                panGains (pan, gainL[(size_t) i], gainR[(size_t) i]);
            }
            const float unisonNorm = 1.0f / std::sqrt ((float) numUnison);

            const float shapeA = std::clamp (p.oscAShape + p.modShape, 0.0f, 1.0f);
            const float shapeB = std::clamp (p.oscBShape + p.modShape, 0.0f, 1.0f);
            const WavetableData* table = (p.specimen != nullptr && p.specimen->table.valid) ? &p.specimen->table : nullptr;
            const int mipA = WavetableData::mipFor (incAEnd * sampleRate * maxRatio);
            const bool useA = p.oscALevel > 1.0e-4f;
            const bool useB = p.oscBLevel > 1.0e-4f;
            const bool useSub = p.subLevel > 1.0e-4f;

            // NOISE: white through a one-pole low-pass whose corner COLOUR sweeps from 400 Hz to 16 kHz.
            const bool useNoise = p.noiseLevel > 1.0e-4f;
            const float noiseHz = std::min (400.0f * std::exp2 (p.noiseColour * 5.3f), sampleRate * 0.45f);
            const float noiseCoef = onePoleCoefHz (noiseHz, sampleRate);
            const float noiseGain = std::clamp (std::sqrt (16000.0f / noiseHz), 1.0f, 6.0f) * 0.5f;

            // CHOIR
            const bool useChoir = p.choirLevel > 1.0e-4f;
            if (useChoir)
            {
                auto cs = p.choir;
                cs.vowel = std::clamp (cs.vowel + p.modVowel, 0.0f, 1.0f);
                choir.begin (std::min (midiToHz (base + 12.0f * p.choirOctave) / sampleRate, maxInc), cs, n);
            }

            // SPECIMEN and AIR: playback ratio from the root note, key tracking and tune.
            const bool useSpec = p.specLevel > 1.0e-4f && p.specimen != nullptr;
            const bool strikeMode = p.specMode == 1;
            float specRate = 1.0f;
            GrainCloud::Settings gs;
            if (p.specimen != nullptr)
            {
                specRate = std::exp2 ((p.specTrack * (base - p.specimen->rootNote) + p.specTune) * (1.0f / 12.0f));
                gs.positionNorm = std::clamp (p.specPosition + p.modPosition, 0.0f, 1.0f);
                gs.spray = p.specSpray;
                gs.sizeSeconds = p.specSize;
                gs.density = p.specDensity;
                gs.rate = specRate;
                gs.width = 0.8f;
            }
            if (strikePending)
            {
                strikePending = false;
                if (strikeMode && p.specimen != nullptr)
                    strike.start (p.specimen, p.specPosition);
                else
                    strike.stop();
            }

            const bool useAir = p.airLevel > 1.0e-4f && p.air != nullptr;
            GrainCloud::Settings as;
            if (useAir)
            {
                as.positionNorm = std::clamp (p.airPosition + 0.5f * p.modPosition, 0.0f, 1.0f);
                as.spray = 0.35f;
                as.sizeSeconds = p.airSize;
                as.density = std::clamp (6.0f / std::max (p.airSize, 0.02f), 4.0f, 60.0f);   // about six grains overlap
                as.rate = std::exp2 (p.airTrack * (base - p.air->rootNote) * (1.0f / 12.0f));
                as.width = 1.0f;
            }

            // Filter envelope runs for the whole block; the cutoff follows it at control rate.
            float e2 = env2.getValue();
            for (int i = 0; i < n; ++i)
                e2 = env2.next();

            const float cutoffOct = e2 * p.filterEnv * 6.0f
                                  + p.filterVel * (vel - 0.7f) * 4.0f
                                  + p.keytrack * (base - 60.0f) * (1.0f / 12.0f)
                                  + p.modCutoffOct;
            const float cutoffHz = std::clamp (p.cutoff * std::exp2 (cutoffOct), 20.0f, sampleRate * 0.45f);

            const int model = p.filterModel;
            const float gEnd = std::tan (pi * cutoffHz / sampleRate);
            if (gPrev < 0.0f || model != lastModel)
                gPrev = gEnd;
            lastModel = model;
            const float dG = (gEnd - gPrev) * invN;
            float g = gPrev;

            const float res = std::clamp (p.resonance, 0.0f, 1.0f);
            float k = 0.0f, makeup = 1.0f;
            if (model == 0) { k = TransistorLadder::kFor (res); makeup = TransistorLadder::makeupFor (k); }
            else            { k = 2.0f - 1.97f * res;           makeup = model == 2 ? k * 1.4f : 1.0f; }
            if (model == 0) { ladder[0].setG (g); ladder[1].setG (g); }
            const bool sweeping = std::abs (dG) > 1.0e-9f;

            // Drive into the filter: linear at 0 dB, fully shaped from 3 dB up.
            const float driveGain = dbToGain (p.driveDb);
            const float driveAmount = std::clamp (p.driveDb / 3.0f, 0.0f, 1.0f);
            const float driveComp = 0.5f / fastTanh (0.5f * driveGain);

            const float velGainTarget = 1.0f - p.ampVel * (1.0f - vel);
            if (freshStart)
                velGain = velGainTarget;

            constexpr float mixScale = 0.5f;
            const float specGain = p.specLevel * (strikeMode ? 1.6f : 2.0f);
            const float airGain = p.airLevel * 2.0f;
            const float choirGain = p.choirLevel;

            for (int i = 0; i < n; ++i)
            {
                incA += dIncA;
                incB += dIncB;
                incSub += dIncSub;

                float xL = 0.0f, xR = 0.0f, mono = 0.0f;

                // OSC A through BONDS
                if (useA)
                {
                    float aL = 0.0f, aR = 0.0f;
                    for (int u = 0; u < numUnison; ++u)
                    {
                        float& ph = phaseA[(size_t) u];
                        const float inc = std::min (incA * ratio[(size_t) u], 0.5f);
                        ph += inc;
                        ph -= std::floor (ph);

                        float v;
                        switch (p.oscAWave)
                        {
                            case 0:  v = osc::sawMorph (ph, inc, shapeA); break;
                            case 1:  v = osc::pulse (ph, inc, 0.5f - 0.45f * shapeA); break;
                            case 2:  v = osc::fold (osc::triangle (ph, inc), shapeA); break;
                            case 3:  v = osc::fold (sin2pi (ph), shapeA); break;
                            default: v = table != nullptr ? readTable (*table, shapeA, ph, mipA) : sin2pi (ph); break;
                        }
                        aL += v * gainL[(size_t) u];
                        aR += v * gainR[(size_t) u];
                    }
                    xL += aL * unisonNorm * p.oscALevel;
                    xR += aR * unisonNorm * p.oscALevel;
                }

                // OSC B
                if (useB)
                {
                    phaseB += incB;
                    phaseB -= std::floor (phaseB);
                    float b;
                    switch (p.oscBWave)
                    {
                        case 0:  b = osc::sawMorph (phaseB, incB, shapeB); break;
                        case 1:  b = osc::pulse (phaseB, incB, 0.5f - 0.45f * shapeB); break;
                        case 2:  b = osc::fold (osc::triangle (phaseB, incB), shapeB); break;
                        default: b = osc::fold (sin2pi (phaseB), shapeB); break;
                    }
                    mono += b * p.oscBLevel;
                }

                // SUB
                if (useSub)
                {
                    phaseSub += incSub;
                    phaseSub -= std::floor (phaseSub);
                    mono += sin2pi (phaseSub) * p.subLevel;
                }

                // NOISE
                if (useNoise)
                {
                    noiseLp += (rng.bi() - noiseLp) * noiseCoef;
                    mono += noiseLp * noiseGain * p.noiseLevel;
                }

                xL += mono;
                xR += mono;

                // CHOIR
                if (useChoir)
                {
                    float cl, cr;
                    choir.next (cl, cr);
                    xL += cl * choirGain;
                    xR += cr * choirGain;
                }

                // SPECIMEN
                if (useSpec)
                {
                    if (strikeMode)
                    {
                        const float v = strike.process (p.specimen, specRate) * specGain;
                        xL += v;
                        xR += v;
                    }
                    else
                    {
                        float gl, gr;
                        grains.process (p.specimen, gs, gl, gr);
                        xL += gl * specGain;
                        xR += gr * specGain;
                    }
                }

                // AIR
                if (useAir)
                {
                    float gl, gr;
                    airGrains.process (p.air, as, gl, gr);
                    xL += gl * airGain;
                    xR += gr * airGain;
                }

                xL *= mixScale;
                xR *= mixScale;

                if (driveAmount > 0.0f)
                {
                    xL += (fastTanh (xL * driveGain) * driveComp - xL) * driveAmount;
                    xR += (fastTanh (xR * driveGain) * driveComp - xR) * driveAmount;
                }

                // FILTER
                if (sweeping)
                {
                    g += dG;
                    if (model == 0) { ladder[0].setG (g); ladder[1].setG (g); }
                }

                float yL, yR;
                if (model == 0)
                {
                    yL = ladder[0].process (xL, k) * makeup;
                    yR = ladder[1].process (xR, k) * makeup;
                }
                else
                {
                    float lp, bp, hp;
                    svf[0].set (g, k);
                    svf[0].process (xL, lp, bp, hp);
                    yL = (model == 1 ? lp : (model == 2 ? bp : hp)) * makeup;
                    svf[1].set (g, k);
                    svf[1].process (xR, lp, bp, hp);
                    yR = (model == 1 ? lp : (model == 2 ? bp : hp)) * makeup;
                }

                // Amplitude
                velGain += (velGainTarget - velGain) * 0.002f;
                float amp = env1.next() * velGain;
                if (fading)
                {
                    amp *= fadeGain;
                    fadeGain = std::max (0.0f, fadeGain - fadeStep);
                }

                outL[i] += yL * amp;
                outR[i] += yR * amp;
            }

            gPrev = gEnd;
            incA = incAEnd;
            incB = incBEnd;
            incSub = incSubEnd;
            freshStart = false;

            if (fading && fadeGain <= 0.0f)
                kill();
        }

    private:
        void resetState() noexcept
        {
            for (auto& f : ladder) f.reset();
            for (auto& f : svf)    f.reset();
            noiseLp = 0.0f;
            gPrev = -1.0f;
            freshStart = true;
            grains.clear();
            airGrains.clear();
            strike.stop();
        }

        Envelope env1, env2;
        GrainCloud grains, airGrains;
        StrikePlayer strike;
        Choir choir;
        std::array<TransistorLadder, 2> ladder {};
        std::array<SVF, 2> svf {};
        Rng rng;

        float sampleRate = 48000.0f;
        int noteNumber = 60, tag = -1;
        float vel = 1.0f, pitch = 60.0f, targetPitch = 60.0f;
        bool keyDown = false, freshStart = true, strikePending = false;
        bool fading = false;
        float fadeGain = 1.0f, fadeStep = 0.004f;
        uint64_t age = 0;

        std::array<float, maxUnison> phaseA {};
        float phaseB = 0.0f, phaseSub = 0.0f, phaseTable = 0.0f;
        float incA = 0.0f, incB = 0.0f, incSub = 0.0f;
        float noiseLp = 0.0f;
        float velGain = 1.0f;
        float gPrev = -1.0f;
        int lastModel = -1;
    };
}
