#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include "Common.h"
#include "../Params.h"

namespace labx19::dsp
{
    // Turns the keys that are held into voice events: CHORD expands each key into a chord shape, STRUM spreads its
    // notes in time like a guitar, and ARP steps through them on the host's beat grid. Everything is scheduled on
    // an absolute sample clock; the processor cuts its control blocks at samplesUntilNext(), so every event lands on
    // its exact sample. No allocation: all state lives in fixed arrays.
    //
    // Voices are addressed by tag. A held key owns tags key * 8 + i (one per chord note); an arp step owns
    // tags from arpTagBase upwards.
    class NoteEngine
    {
    public:
        struct Sink
        {
            virtual ~Sink() = default;
            virtual void voiceOn (int note, float velocity, int tag) noexcept = 0;
            virtual void voiceOff (int tag) noexcept = 0;
            virtual void releaseAllVoices() noexcept = 0;
        };

        struct Settings
        {
            int chordType = 0, voicing = 0, strumDir = 0;
            float strumMs = 0.0f;
            int arpMode = 0;
            float arpBeats = 0.25f;
            int arpOctaves = 1;
            float arpGate = 0.6f, arpSwing = 0.0f;
            bool arpLatch = false;
            bool mono = false;
        };

        static constexpr int arpTagBase = 1024;
        static constexpr int maxKeys = 32;
        static constexpr int maxPattern = 128;
        static constexpr int maxPending = 256;

        void prepare (double sr, uint32_t seed) noexcept
        {
            sampleRate = sr;
            rng.seed (seed);
            reset();
        }

        // Forgets every key, event and arp step without telling the voices (they are flushed separately).
        void reset() noexcept
        {
            keyCount = 0;
            for (auto& e : pending)
                e.used = false;
            pendingCount = 0;
            for (auto& k : sounding)
                k = false;
            sustainDown = false;
            arpRunning = false;
            arpWasOn = false;
            stepCounter = 0;
            patternPos = 0;
            upDownDir = 1;
            alternateUp = false;
            patternDirty = true;
            patternSize = 0;
            beatClock = 0.0;
            lastSynced = false;
            now = 0;
        }

        // Per control block: the settings and the beat position at the current sample.
        void begin (const Settings& s, bool synced, double ppq, double bpmValue, Sink& sink) noexcept
        {
            const bool arpOn = s.arpMode != Choice::arpOff;
            if (arpOn != arpWasOn)
            {
                // Switching between played chords and the arp: everything sounding is released, held keys stay.
                sink.releaseAllVoices();
                for (auto& e : pending)
                    e.used = false;
                pendingCount = 0;
                for (auto& k : sounding)
                    k = false;
                arpRunning = false;
                arpWasOn = arpOn;
                patternDirty = true;
            }

            if (s.chordType != settings.chordType || s.voicing != settings.voicing || s.arpMode != settings.arpMode
                || s.arpOctaves != settings.arpOctaves || s.mono != settings.mono)
                patternDirty = true;

            settings = s;
            bpm = std::max (1.0, bpmValue);
            beatsPerSample = bpm / 60.0 / sampleRate;

            if (synced)
                beatNow = ppq;
            else
                beatNow = beatClock;

            if (arpRunning && synced != lastSynced)
                nextStepBeat = gridAtOrAfter (beatNow);   // the clock changed under us: continue from the next step
            else if (arpRunning && synced && (beatNow < nextStepBeat - 4.0 * stepLengthBeats() - 1.0e-6
                                              || beatNow > nextStepBeat + 2.0 * stepLengthBeats()))
                nextStepBeat = gridAtOrAfter (beatNow);   // host looped or jumped
            lastSynced = synced;
        }

        // Samples until the next scheduled event (at least 1 after fire() has run).
        int samplesUntilNext() const noexcept
        {
            int64_t best = std::numeric_limits<int>::max();
            for (const auto& e : pending)
                if (e.used)
                    best = std::min (best, e.time - now);
            if (arpRunning)
            {
                const double samples = (nextStepBeat - beatNow) / beatsPerSample;
                best = std::min (best, (int64_t) std::ceil (std::max (0.0, samples - 1.0e-6)));
            }
            return (int) std::clamp<int64_t> (best, 1, std::numeric_limits<int>::max());
        }

        // Fires everything due at the current sample: note-offs first, then note-ons, then a due arp step.
        void fire (Sink& sink) noexcept
        {
            if (arpWasOn)
                runArp (sink);

            if (pendingCount == 0)
                return;

            for (int pass = 0; pass < 2; ++pass)
                for (auto& e : pending)
                    if (e.used && e.time <= now && e.on == (pass == 1))
                    {
                        e.used = false;
                        --pendingCount;
                        if (e.on)
                            sink.voiceOn (e.note, e.velocity, e.tag);
                        else
                            sink.voiceOff (e.tag);
                    }
        }

        void advance (int n) noexcept
        {
            now += n;
            beatClock += (double) n * beatsPerSample;
            beatNow += (double) n * beatsPerSample;
        }

        //==============================================================================
        void keyDown (int note, float velocity, Sink& sink) noexcept
        {
            note = std::clamp (note, 0, 127);
            if (arpWasOn)
            {
                arpKeyDown (note, velocity);
                return;
            }

            if (sounding[(size_t) note])
                releaseKey (note, sink);   // the same key again without a release: restart it

            int notes[maxChordNotes];
            const int count = chordNotes (note, notes);
            int order[maxChordNotes];
            strumOrder (count, order);

            const double strumSamples = (double) settings.strumMs * 0.001 * sampleRate;
            for (int j = 0; j < count; ++j)
            {
                const int i = order[j];
                schedule (now + (int64_t) std::llround (strumSamples * j), true, notes[i], velocity, note * 8 + i);
            }
            sounding[(size_t) note] = true;
            heldPhysically (note, velocity, true);
        }

        void keyUp (int note, Sink& sink) noexcept
        {
            note = std::clamp (note, 0, 127);
            if (arpWasOn)
            {
                arpKeyUp (note);
                return;
            }

            heldPhysically (note, 0.0f, false);
            if (sustainDown)
                return;   // released when the pedal comes up
            releaseKey (note, sink);
        }

        void setSustain (bool down, Sink& sink) noexcept
        {
            sustainDown = down;
            if (down)
                return;

            if (arpWasOn)
            {
                removeReleasedKeys();
                return;
            }

            for (int note = 0; note < 128; ++note)
                if (sounding[(size_t) note] && ! isPhysicallyDown (note))
                    releaseKey (note, sink);
        }

        void allNotesOff (Sink& sink) noexcept
        {
            for (int note = 0; note < 128; ++note)
                if (sounding[(size_t) note])
                    releaseKey (note, sink);
            keyCount = 0;
            sustainDown = false;
            patternDirty = true;
            if (arpRunning)
                stopArp (sink);
            for (auto& e : pending)
                e.used = false;
            pendingCount = 0;
        }

        // For the editor: the keys and notes in play.
        int getHeldKeyCount() const noexcept { return keyCount; }
        int getArpStep() const noexcept      { return arpRunning ? (int) (stepCounter % 1000) : -1; }

        // Chord notes for one key, after the voicing. Returns the count (1..maxChordNotes).
        int chordNotes (int key, int* out) const noexcept
        {
            const int type = (settings.mono && settings.arpMode == Choice::arpOff)
                           ? 0 : std::clamp (settings.chordType, 0, numChordShapes - 1);
            const auto& shape = chordShapes[type];
            int count = shape.count;
            for (int i = 0; i < count; ++i)
                out[i] = key + shape.intervals[i];

            if (count >= 2)
            {
                switch (settings.voicing)
                {
                    case Choice::open:
                        for (int i = 1; i < count; i += 2)
                            out[i] += 12;
                        break;
                    case Choice::wide:
                        out[0] -= 12;
                        for (int i = 2; i < count; i += 2)
                            out[i] += 12;
                        break;
                    case Choice::drop2:
                        if (count >= 3)
                        {
                            // The second-highest note drops an octave.
                            int hi = 0, second = -1;
                            for (int i = 1; i < count; ++i)
                                if (out[i] > out[hi]) hi = i;
                            for (int i = 0; i < count; ++i)
                                if (i != hi && (second < 0 || out[i] > out[second])) second = i;
                            if (second >= 0)
                                out[second] -= 12;
                        }
                        break;
                    default:
                        break;
                }
            }

            // Keep notes in range and drop duplicates.
            int unique = 0;
            for (int i = 0; i < count; ++i)
            {
                int n = out[i];
                while (n < 0) n += 12;
                while (n > 127) n -= 12;
                bool seen = false;
                for (int j = 0; j < unique; ++j)
                    if (out[j] == n)
                        seen = true;
                if (! seen)
                    out[unique++] = n;
            }
            return std::max (1, unique);
        }

    private:
        static constexpr int maxChordNotes = 6;

        struct Pending
        {
            int64_t time = 0;
            int note = 0, tag = 0;
            float velocity = 0.0f;
            bool on = false, used = false;
        };

        struct Key
        {
            int note = 0;
            float velocity = 0.0f;
            bool down = false;   // physically held (false: latched or held by the pedal)
        };

        void schedule (int64_t time, bool on, int note, float velocity, int tag) noexcept
        {
            for (auto& e : pending)
                if (! e.used)
                {
                    e = { time, note, tag, velocity, on, true };
                    ++pendingCount;
                    return;
                }
            // Queue full: drop the event. Only reachable with absurd strum and arp settings.
        }

        void cancelPendingOn (int tagFrom, int tagTo) noexcept
        {
            for (auto& e : pending)
                if (e.used && e.on && e.tag >= tagFrom && e.tag < tagTo)
                {
                    e.used = false;
                    --pendingCount;
                }
        }

        void releaseKey (int note, Sink& sink) noexcept
        {
            cancelPendingOn (note * 8, note * 8 + 8);
            for (int i = 0; i < 8; ++i)
                sink.voiceOff (note * 8 + i);
            sounding[(size_t) note] = false;
        }

        // Order in which chord notes start: index into the chord, lowest note first for Down.
        void strumOrder (int count, int* order) noexcept
        {
            for (int i = 0; i < count; ++i)
                order[i] = i;
            int dir = settings.strumDir;
            if (dir == Choice::strumAlternate)
            {
                dir = alternateUp ? Choice::strumUp : Choice::strumDown;
                alternateUp = ! alternateUp;
            }
            if (dir == Choice::strumRandom)
            {
                for (int i = count - 1; i > 0; --i)
                    std::swap (order[i], order[(int) (rng.uni() * (float) (i + 1)) % (i + 1)]);
                return;
            }
            // Chord notes are not always ascending after voicing: sort by pitch.
            int notes[maxChordNotes];
            chordNotesForOrder (notes, count);
            for (int i = 1; i < count; ++i)
                for (int j = i; j > 0 && notes[order[j]] < notes[order[j - 1]]; --j)
                    std::swap (order[j], order[j - 1]);
            if (dir == Choice::strumUp)
                for (int i = 0; i < count / 2; ++i)
                    std::swap (order[i], order[count - 1 - i]);
        }

        void chordNotesForOrder (int* notes, int count) const noexcept
        {
            int tmp[maxChordNotes];
            const int n = chordNotes (60, tmp);
            for (int i = 0; i < count; ++i)
                notes[i] = i < n ? tmp[i] : 0;
        }

        // Physical key list (shared by both modes; the arp uses it as its note source).
        void heldPhysically (int note, float velocity, bool down) noexcept
        {
            for (int i = 0; i < keyCount; ++i)
                if (keys[(size_t) i].note == note)
                {
                    if (down)
                    {
                        keys[(size_t) i].down = true;
                        keys[(size_t) i].velocity = velocity;
                    }
                    else if (arpWasOn && (settings.arpLatch || sustainDown))
                        keys[(size_t) i].down = false;
                    else
                        removeKeyAt (i);
                    patternDirty = true;
                    return;
                }
            if (down && keyCount < maxKeys)
            {
                keys[(size_t) keyCount++] = { note, velocity, true };
                patternDirty = true;
            }
        }

        bool isPhysicallyDown (int note) const noexcept
        {
            for (int i = 0; i < keyCount; ++i)
                if (keys[(size_t) i].note == note)
                    return keys[(size_t) i].down;
            return false;
        }

        void removeKeyAt (int i) noexcept
        {
            for (int j = i; j < keyCount - 1; ++j)
                keys[(size_t) j] = keys[(size_t) j + 1];
            --keyCount;
        }

        void removeReleasedKeys() noexcept
        {
            if (settings.arpLatch)
                return;
            for (int i = keyCount - 1; i >= 0; --i)
                if (! keys[(size_t) i].down)
                    removeKeyAt (i);
            patternDirty = true;
        }

        //==============================================================================
        // ARP
        void arpKeyDown (int note, float velocity) noexcept
        {
            // A fresh press after a latched phrase replaces the phrase.
            bool anyDown = false;
            for (int i = 0; i < keyCount; ++i)
                anyDown = anyDown || keys[(size_t) i].down;
            if (! anyDown)
                keyCount = 0;
            heldPhysically (note, velocity, true);
        }

        void arpKeyUp (int note) noexcept
        {
            heldPhysically (note, 0.0f, false);
        }

        double stepLengthBeats() const noexcept { return std::max (1.0e-3, (double) settings.arpBeats); }

        // Step start positions: even steps on the grid, odd steps pushed late by the swing.
        double stepStartBeat (int64_t index) const noexcept
        {
            const double sb = stepLengthBeats();
            const double swingOffset = (index & 1) ? 0.5 * (double) settings.arpSwing * sb : 0.0;
            return (double) index * sb + swingOffset;
        }

        double gridAtOrAfter (double beat) const noexcept
        {
            const double sb = stepLengthBeats();
            gridIndex = (int64_t) std::ceil (beat / sb - 1.0e-6);
            return stepStartBeat (gridIndex);
        }

        void stopArp (Sink& sink) noexcept
        {
            for (int i = 0; i < lastStepCount; ++i)
                sink.voiceOff (lastStepTags[(size_t) i]);
            lastStepCount = 0;
            arpRunning = false;
        }

        void runArp (Sink& sink) noexcept
        {
            if (keyCount == 0)
            {
                if (arpRunning)
                    stopArp (sink);
                return;
            }

            if (! arpRunning)
            {
                arpRunning = true;
                patternPos = 0;
                upDownDir = 1;
                if (lastSynced)
                    nextStepBeat = gridAtOrAfter (beatNow);
                else
                {
                    beatClock = 0.0;
                    beatNow = 0.0;
                    gridIndex = 0;
                    nextStepBeat = 0.0;
                }
            }

            if (beatNow + 1.0e-7 < nextStepBeat)
                return;

            if (patternDirty)
                buildPattern();

            const bool fullGate = settings.arpGate >= 0.999f;

            // End the previous step's notes. At full gate they end one sample after the new ones start, so a
            // Legato voice slides from step to step instead of retriggering.
            for (int i = 0; i < lastStepCount; ++i)
            {
                if (fullGate)
                    schedule (now + 1, false, 0, 0.0f, lastStepTags[(size_t) i]);
                else
                    sink.voiceOff (lastStepTags[(size_t) i]);
            }
            lastStepCount = 0;

            const double thisStep = stepStartBeat (gridIndex);
            const double nextStep = stepStartBeat (gridIndex + 1);
            const double lengthSamples = std::max (1.0, (nextStep - thisStep) / beatsPerSample);
            const int64_t gateSamples = std::max<int64_t> (1, (int64_t) std::llround (lengthSamples * (double) settings.arpGate));
            const int base = arpTagBase + (int) (stepCounter % 8192) * 8;

            float velocity = 0.0f;
            for (int i = 0; i < keyCount; ++i)
                velocity = std::max (velocity, keys[(size_t) i].velocity);

            if (settings.arpMode == Choice::arpChord)
            {
                const int shift = 12 * (int) (stepCounter % (uint64_t) std::max (1, settings.arpOctaves));
                int order[maxPattern];
                for (int i = 0; i < chordSize; ++i)
                    order[i] = i;
                const double strumSamples = (double) settings.strumMs * 0.001 * sampleRate;
                for (int j = 0; j < chordSize && j < 8; ++j)
                {
                    const int note = std::min (127, chordSet[(size_t) order[j]] + shift);
                    const int tag = base + j;
                    schedule (now + (int64_t) std::llround (strumSamples * j), true, note, velocity, tag);
                    if (! fullGate)
                        schedule (now + gateSamples, false, note, 0.0f, tag);
                    lastStepTags[(size_t) lastStepCount++] = tag;
                }
            }
            else if (patternSize > 0)
            {
                int index = 0;
                switch (settings.arpMode)
                {
                    case Choice::arpRandom:
                    {
                        index = (int) (rng.uni() * (float) patternSize) % patternSize;
                        if (patternSize > 1 && index == lastRandom)
                            index = (index + 1 + (int) (rng.uni() * (float) (patternSize - 1))) % patternSize;
                        lastRandom = index;
                        break;
                    }
                    case Choice::arpUpDown:
                    {
                        if (patternPos >= patternSize) patternPos = patternSize - 1;
                        index = patternPos;
                        if (patternSize > 1)
                        {
                            if (patternPos + upDownDir >= patternSize || patternPos + upDownDir < 0)
                                upDownDir = -upDownDir;
                            patternPos += upDownDir;
                        }
                        break;
                    }
                    default:
                        index = patternPos % patternSize;
                        patternPos = (patternPos + 1) % patternSize;
                        break;
                }

                const int note = pattern[(size_t) index];
                const int tag = base;
                schedule (now, true, note, velocity, tag);
                if (! fullGate)
                    schedule (now + gateSamples, false, note, 0.0f, tag);
                lastStepTags[(size_t) lastStepCount++] = tag;
            }

            ++stepCounter;
            ++gridIndex;
            nextStepBeat = stepStartBeat (gridIndex);
        }

        void buildPattern() noexcept
        {
            patternDirty = false;
            patternSize = 0;
            chordSize = 0;

            // All chord notes of all keys, in the order the keys were pressed.
            int collected[maxPattern];
            int count = 0;
            for (int k = 0; k < keyCount; ++k)
            {
                int notes[maxChordNotes];
                const int n = chordNotes (keys[(size_t) k].note, notes);
                for (int i = 0; i < n && count < maxPattern; ++i)
                {
                    bool seen = false;
                    for (int j = 0; j < count; ++j)
                        seen = seen || collected[j] == notes[i];
                    if (! seen)
                        collected[count++] = notes[i];
                }
            }

            // Chord mode plays the whole set at once (low to high, so STRUM sweeps down the strings).
            for (int i = 0; i < count && chordSize < 8; ++i)
                chordSet[(size_t) chordSize++] = collected[i];
            std::sort (chordSet.begin(), chordSet.begin() + chordSize);

            const bool ordered = settings.arpMode == Choice::arpOrder;
            if (! ordered)
                std::sort (collected, collected + count);

            const int octaves = std::clamp (settings.arpOctaves, 1, 4);
            for (int o = 0; o < octaves; ++o)
                for (int i = 0; i < count && patternSize < maxPattern; ++i)
                {
                    const int n = collected[i] + 12 * o;
                    if (n <= 127)
                        pattern[(size_t) patternSize++] = n;
                }

            if (settings.arpMode == Choice::arpDown)
                std::reverse (pattern.begin(), pattern.begin() + patternSize);

            if (patternPos >= patternSize)
                patternPos = 0;
        }

        Settings settings;
        double sampleRate = 48000.0, bpm = 120.0, beatsPerSample = 120.0 / 60.0 / 48000.0;
        int64_t now = 0;
        Rng rng;

        std::array<Pending, maxPending> pending {};
        int pendingCount = 0;
        std::array<bool, 128> sounding {};
        std::array<Key, maxKeys> keys {};
        int keyCount = 0;
        bool sustainDown = false, alternateUp = false;

        // Arp state
        bool arpRunning = false, arpWasOn = false, patternDirty = true, lastSynced = false;
        double beatClock = 0.0, beatNow = 0.0, nextStepBeat = 0.0;
        mutable int64_t gridIndex = 0;
        uint64_t stepCounter = 0;
        std::array<int, maxPattern> pattern {};
        std::array<int, 8> chordSet {};
        int patternSize = 0, chordSize = 0, patternPos = 0, upDownDir = 1, lastRandom = -1;
        std::array<int, 8> lastStepTags {};
        int lastStepCount = 0;
    };
}
