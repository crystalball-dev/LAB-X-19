#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "SpecimenCatalog.h"

namespace labx19
{
    enum class Kind { real, integer, choice };
    enum class Smooth { none, linear, log };

    struct ParamSpec
    {
        const char* id;
        const char* name;
        Kind kind;
        float minValue, maxValue, defaultValue, skewCentre;   // skewCentre 0 = linear range
        Smooth smooth;
        const char* unit;      // "%", "Hz", "s", "ms", "dB", "ct", "st", "x", "/s", "oct", ""
        const char* choices;   // '|' separated; "@specimen" builds the specimen list
    };

    // Every parameter is a float or a choice. A bool parameter failed pluginval's state restore in LAB X-3.
    namespace P
    {
        enum Index : int
        {
            oscAWave, oscAOctave, oscASemi, oscAFine, oscAShape, oscALevel,
            uniVoices, uniDetune, uniSpread,
            oscBWave, oscBOctave, oscBSemi, oscBFine, oscBShape, oscBLevel,
            subLevel, noiseLevel, noiseColour,
            choirLevel, choirVowel, choirSingers, choirDetune, choirVibrato, choirBreath, choirSize, choirOctave,
            specSource, specMode, specLevel, specPosition, specSize, specDensity, specSpray, specTrack, specTune,
            airSource, airLevel, airPosition, airSize, airTrack,
            filterModel, cutoff, resonance, filterDrive, filterEnv, keytrack, filterVel,
            env1A, env1D, env1S, env1R,
            env2A, env2D, env2S, env2R,
            ampVel,
            chordType, chordVoicing, strumTime, strumDir,
            arpMode, arpRate, arpOctaves, arpGate, arpSwing, arpLatch,
            lfoShape, lfoRate, lfoSync, lfoTarget, lfoDepth, lfoRetrig,
            gritMode, gritAmount, gritDrive, gritTone, gritDust,
            ensMix, ensDepth, ensRate,
            delayMix, delaySync, delayTime, delayFeedback, delayTone, delayMode,
            revMix, revSize, revDecay, revDamping, revShimmer, revPredelay,
            regAmount, regCeiling,
            dawn, volume, voiceMode, voices, glide, bendRange, width,
            count
        };
    }

    // Choice indices that the DSP code relies on.
    namespace Choice
    {
        enum OscAWave   { saw = 0, pulse, triangle, sine, table };
        enum OscBWave   { bSaw = 0, bPulse, bTriangle, bSine };
        enum SpecMode   { specGrain = 0, specStrike };
        enum Filter     { ladder = 0, svfLow, svfBand, svfHigh };
        enum Voicing    { close = 0, open, wide, drop2 };
        enum StrumDir   { strumDown = 0, strumUp, strumAlternate, strumRandom };
        enum ArpMode    { arpOff = 0, arpUp, arpDown, arpUpDown, arpRandom, arpOrder, arpChord };
        enum LfoShape   { lfoSine = 0, lfoTriangle, lfoSaw, lfoSquare, lfoSampleHold, lfoDrift };
        enum LfoTarget  { toCutoff = 0, toPitch, toShape, toVowel, toPosition, toLevel };
        enum GritMode   { tube = 0, tape, radio, crush };
        enum DelayMode  { stereo = 0, pingPong };
        enum VoiceMode  { poly = 0, mono, legato };
    }

    // CHORD shapes: semitone offsets from the key that is played. The first entry, Off, plays the key alone.
    struct ChordShape
    {
        const char* name;
        int count;
        int intervals[6];
    };

    inline constexpr ChordShape chordShapes[] = {
        { "Off",     1, { 0 } },
        { "Major",   3, { 0, 4, 7 } },
        { "Minor",   3, { 0, 3, 7 } },
        { "Sus2",    3, { 0, 2, 7 } },
        { "Sus4",    3, { 0, 5, 7 } },
        { "Add9",    4, { 0, 4, 7, 14 } },
        { "Maj7",    4, { 0, 4, 7, 11 } },
        { "Min7",    4, { 0, 3, 7, 10 } },
        { "Dom7",    4, { 0, 4, 7, 10 } },
        { "Maj9",    5, { 0, 4, 7, 11, 14 } },
        { "Min9",    5, { 0, 3, 7, 10, 14 } },
        { "6/9",     5, { 0, 4, 7, 9, 14 } },
        { "Min11",   6, { 0, 3, 7, 10, 14, 17 } },
        { "Fifths",  3, { 0, 7, 14 } },
        { "Octaves", 2, { 0, 12 } },
        { "Lydian",  5, { 0, 4, 7, 11, 18 } },
    };
    inline constexpr int numChordShapes = (int) std::size (chordShapes);

    // Beats (quarter notes) per step for each arp_rate choice.
    inline constexpr float arpRateBeats[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 0.75f, 0.375f };

    // Beats (quarter notes) per LFO cycle for each lfo_sync choice; index 0 is free-running.
    inline constexpr float lfoSyncBeats[] = { 0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                              2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f, 1.5f, 0.75f, 8.0f, 16.0f };

    // Beats per echo for each delay_sync choice; index 0 uses delay_time in milliseconds.
    inline constexpr float delaySyncBeats[] = { 0.0f, 1.0f, 0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.5f, 2.0f };

    inline juce::String chordChoices()
    {
        juce::StringArray names;
        for (const auto& c : chordShapes)
            names.add (c.name);
        return names.joinIntoString ("|");
    }

    inline const std::array<ParamSpec, P::count>& paramSpecs()
    {
        static const juce::String chordList = chordChoices();

        static const std::array<ParamSpec, P::count> specs {{
            { "osc_a_wave",      "Osc A Wave",            Kind::choice,  0.0f,   4.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Saw|Pulse|Triangle|Sine|Table" },
            { "osc_a_octave",    "Osc A Octave",          Kind::integer, -3.0f,  2.0f,     0.0f,    0.0f,   Smooth::none,   "oct", "" },
            { "osc_a_semi",      "Osc A Semitone",        Kind::integer, -12.0f, 12.0f,    0.0f,    0.0f,   Smooth::none,   "st",  "" },
            { "osc_a_fine",      "Osc A Fine",            Kind::real,    -100.0f, 100.0f,  0.0f,    0.0f,   Smooth::linear, "ct",  "" },
            { "osc_a_shape",     "Osc A Shape",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "osc_a_level",     "Osc A Level",           Kind::real,    0.0f,   1.0f,     0.8f,    0.0f,   Smooth::linear, "%",   "" },

            { "uni_voices",      "Bonds Voices",          Kind::integer, 1.0f,   7.0f,     1.0f,    0.0f,   Smooth::none,   "",    "" },
            { "uni_detune",      "Bonds Detune",          Kind::real,    0.0f,   100.0f,   18.0f,   0.0f,   Smooth::linear, "ct",  "" },
            { "uni_spread",      "Bonds Spread",          Kind::real,    0.0f,   1.0f,     0.7f,    0.0f,   Smooth::linear, "%",   "" },

            { "osc_b_wave",      "Osc B Wave",            Kind::choice,  0.0f,   3.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Saw|Pulse|Triangle|Sine" },
            { "osc_b_octave",    "Osc B Octave",          Kind::integer, -3.0f,  2.0f,     0.0f,    0.0f,   Smooth::none,   "oct", "" },
            { "osc_b_semi",      "Osc B Semitone",        Kind::integer, -12.0f, 12.0f,    0.0f,    0.0f,   Smooth::none,   "st",  "" },
            { "osc_b_fine",      "Osc B Fine",            Kind::real,    -100.0f, 100.0f,  7.0f,    0.0f,   Smooth::linear, "ct",  "" },
            { "osc_b_shape",     "Osc B Shape",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "osc_b_level",     "Osc B Level",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },

            { "sub_level",       "Sub Level",             Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "noise_level",     "Noise Level",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "noise_colour",    "Noise Colour",          Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,   Smooth::linear, "%",   "" },

            { "choir_level",     "Choir Level",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "choir_vowel",     "Choir Vowel",           Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,   Smooth::linear, "%",   "" },
            { "choir_singers",   "Choir Singers",         Kind::integer, 1.0f,   8.0f,     4.0f,    0.0f,   Smooth::none,   "",    "" },
            { "choir_detune",    "Choir Detune",          Kind::real,    0.0f,   50.0f,    12.0f,   0.0f,   Smooth::linear, "ct",  "" },
            { "choir_vibrato",   "Choir Vibrato",         Kind::real,    0.0f,   1.0f,     0.35f,   0.0f,   Smooth::linear, "%",   "" },
            { "choir_breath",    "Choir Breath",          Kind::real,    0.0f,   1.0f,     0.2f,    0.0f,   Smooth::linear, "%",   "" },
            { "choir_size",      "Choir Size",            Kind::real,    -1.0f,  1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "choir_octave",    "Choir Octave",          Kind::integer, -2.0f,  1.0f,     0.0f,    0.0f,   Smooth::none,   "oct", "" },

            { "specimen_source", "Specimen Source",       Kind::choice,  0.0f,   (float) specimenCatalogSize, 1.0f, 0.0f, Smooth::none, "", "@specimen" },
            { "specimen_mode",   "Specimen Mode",         Kind::choice,  0.0f,   1.0f,     1.0f,    0.0f,   Smooth::none,   "",    "Grain|Strike" },
            { "specimen_level",  "Specimen Level",        Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "specimen_position","Specimen Position",    Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "specimen_size",   "Specimen Grain Size",   Kind::real,    10.0f,  1000.0f,  180.0f,  150.0f, Smooth::none,   "ms",  "" },
            { "specimen_density","Specimen Density",      Kind::real,    1.0f,   120.0f,   24.0f,   20.0f,  Smooth::none,   "/s",  "" },
            { "specimen_spray",  "Specimen Spray",        Kind::real,    0.0f,   1.0f,     0.2f,    0.0f,   Smooth::linear, "%",   "" },
            { "specimen_track",  "Specimen Keytrack",     Kind::real,    0.0f,   1.0f,     1.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "specimen_tune",   "Specimen Tune",         Kind::real,    -24.0f, 24.0f,    0.0f,    0.0f,   Smooth::linear, "st",  "" },

            { "air_source",      "Air Source",            Kind::choice,  0.0f,   (float) specimenCatalogSize, 26.0f, 0.0f, Smooth::none, "", "@specimen" },
            { "air_level",       "Air Level",             Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "air_position",    "Air Position",          Kind::real,    0.0f,   1.0f,     0.4f,    0.0f,   Smooth::linear, "%",   "" },
            { "air_size",        "Air Grain Size",        Kind::real,    20.0f,  1000.0f,  260.0f,  200.0f, Smooth::none,   "ms",  "" },
            { "air_track",       "Air Keytrack",          Kind::real,    0.0f,   1.0f,     0.25f,   0.0f,   Smooth::linear, "%",   "" },

            { "filter_model",    "Filter Model",          Kind::choice,  0.0f,   3.0f,     1.0f,    0.0f,   Smooth::none,   "",    "Ladder|SVF LP|SVF BP|SVF HP" },
            { "filter_cutoff",   "Filter Cutoff",         Kind::real,    20.0f,  20000.0f, 6000.0f, 1000.0f, Smooth::log,   "Hz",  "" },
            { "filter_res",      "Filter Resonance",      Kind::real,    0.0f,   1.0f,     0.1f,    0.0f,   Smooth::linear, "%",   "" },
            { "filter_drive",    "Filter Drive",          Kind::real,    0.0f,   24.0f,    0.0f,    0.0f,   Smooth::linear, "dB",  "" },
            { "filter_env",      "Filter Env Amount",     Kind::real,    -1.0f,  1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "filter_keytrack", "Filter Keytrack",       Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,   Smooth::linear, "%",   "" },
            { "filter_vel",      "Filter Velocity",       Kind::real,    0.0f,   1.0f,     0.3f,    0.0f,   Smooth::linear, "%",   "" },

            { "env1_attack",     "Amp Attack",            Kind::real,    0.001f, 10.0f,    0.005f,  0.5f,   Smooth::none,   "s",   "" },
            { "env1_decay",      "Amp Decay",             Kind::real,    0.001f, 10.0f,    0.6f,    0.8f,   Smooth::none,   "s",   "" },
            { "env1_sustain",    "Amp Sustain",           Kind::real,    0.0f,   1.0f,     0.8f,    0.0f,   Smooth::none,   "%",   "" },
            { "env1_release",    "Amp Release",           Kind::real,    0.001f, 20.0f,    0.4f,    1.5f,   Smooth::none,   "s",   "" },

            { "env2_attack",     "Filter Attack",         Kind::real,    0.001f, 10.0f,    0.005f,  0.5f,   Smooth::none,   "s",   "" },
            { "env2_decay",      "Filter Decay",          Kind::real,    0.001f, 10.0f,    0.5f,    0.8f,   Smooth::none,   "s",   "" },
            { "env2_sustain",    "Filter Sustain",        Kind::real,    0.0f,   1.0f,     0.3f,    0.0f,   Smooth::none,   "%",   "" },
            { "env2_release",    "Filter Release",        Kind::real,    0.001f, 20.0f,    0.5f,    1.5f,   Smooth::none,   "s",   "" },

            { "amp_vel",         "Velocity Amount",       Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,   Smooth::none,   "%",   "" },

            { "chord_type",      "Chord",                 Kind::choice,  0.0f,   (float) (numChordShapes - 1), 0.0f, 0.0f, Smooth::none, "", chordList.toRawUTF8() },
            { "chord_voicing",   "Chord Voicing",         Kind::choice,  0.0f,   3.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Close|Open|Wide|Drop 2" },
            { "strum_time",      "Strum",                 Kind::real,    0.0f,   250.0f,   0.0f,    40.0f,  Smooth::none,   "ms",  "" },
            { "strum_dir",       "Strum Direction",       Kind::choice,  0.0f,   3.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Down|Up|Alternate|Random" },

            { "arp_mode",        "Arp Mode",              Kind::choice,  0.0f,   6.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Off|Up|Down|Up-Down|Random|Order|Chord" },
            { "arp_rate",        "Arp Rate",              Kind::choice,  0.0f,   7.0f,     3.0f,    0.0f,   Smooth::none,   "",    "1/4|1/8|1/8 T|1/16|1/16 T|1/32|1/8 D|1/16 D" },
            { "arp_octaves",     "Arp Octaves",           Kind::integer, 1.0f,   4.0f,     1.0f,    0.0f,   Smooth::none,   "",    "" },
            { "arp_gate",        "Arp Gate",              Kind::real,    0.05f,  1.0f,     0.6f,    0.0f,   Smooth::none,   "%",   "" },
            { "arp_swing",       "Arp Swing",             Kind::real,    0.0f,   0.6f,     0.0f,    0.0f,   Smooth::none,   "%",   "" },
            { "arp_latch",       "Arp Latch",             Kind::choice,  0.0f,   1.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Off|On" },

            { "lfo_shape",       "LFO Shape",             Kind::choice,  0.0f,   5.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Sine|Triangle|Saw|Square|S&H|Drift" },
            { "lfo_rate",        "LFO Rate",              Kind::real,    0.02f,  30.0f,    0.5f,    2.0f,   Smooth::none,   "Hz",  "" },
            { "lfo_sync",        "LFO Sync",              Kind::choice,  0.0f,   13.0f,    0.0f,    0.0f,   Smooth::none,   "",    "Free|1/1|1/2|1/4|1/8|1/16|1/32|1/4 T|1/8 T|1/16 T|1/4 D|1/8 D|2 bars|4 bars" },
            { "lfo_target",      "LFO Target",            Kind::choice,  0.0f,   5.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Cutoff|Pitch|Shape|Vowel|Position|Level" },
            { "lfo_depth",       "LFO Depth",             Kind::real,    -1.0f,  1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "lfo_retrig",      "LFO Retrigger",         Kind::choice,  0.0f,   1.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Free|Note" },

            { "grit_mode",       "Grit Mode",             Kind::choice,  0.0f,   3.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Tube|Tape|Radio|Crush" },
            { "grit_amount",     "Grit Amount",           Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "grit_drive",      "Grit Drive",            Kind::real,    0.0f,   36.0f,    12.0f,   0.0f,   Smooth::linear, "dB",  "" },
            { "grit_tone",       "Grit Corner",           Kind::real,    100.0f, 5000.0f,  600.0f,  700.0f, Smooth::log,    "Hz",  "" },
            { "grit_dust",       "Grit Dust",             Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },

            { "ens_mix",         "Ensemble Mix",          Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "ens_depth",       "Ensemble Depth",        Kind::real,    0.0f,   1.0f,     0.5f,    0.0f,   Smooth::linear, "%",   "" },
            { "ens_rate",        "Ensemble Rate",         Kind::real,    0.05f,  5.0f,     0.6f,    0.6f,   Smooth::linear, "Hz",  "" },

            { "delay_mix",       "Delay Mix",             Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "delay_sync",      "Delay Sync",            Kind::choice,  0.0f,   7.0f,     3.0f,    0.0f,   Smooth::none,   "",    "Free|1/4|1/8|1/8 D|1/8 T|1/16|1/4 D|1/2" },
            { "delay_time",      "Delay Time",            Kind::real,    10.0f,  2000.0f,  375.0f,  400.0f, Smooth::none,   "ms",  "" },
            { "delay_feedback",  "Delay Feedback",        Kind::real,    0.0f,   0.95f,    0.35f,   0.0f,   Smooth::linear, "%",   "" },
            { "delay_tone",      "Delay Tone",            Kind::real,    500.0f, 16000.0f, 5000.0f, 3000.0f, Smooth::log,   "Hz",  "" },
            { "delay_mode",      "Delay Mode",            Kind::choice,  0.0f,   1.0f,     1.0f,    0.0f,   Smooth::none,   "",    "Stereo|Ping-Pong" },

            { "rev_mix",         "Reverb Mix",            Kind::real,    0.0f,   1.0f,     0.2f,    0.0f,   Smooth::linear, "%",   "" },
            { "rev_size",        "Reverb Size",           Kind::real,    0.0f,   1.0f,     0.6f,    0.0f,   Smooth::none,   "%",   "" },
            { "rev_decay",       "Reverb Decay",          Kind::real,    0.0f,   1.0f,     0.55f,   0.0f,   Smooth::none,   "%",   "" },
            { "rev_damping",     "Reverb Damping",        Kind::real,    0.0f,   1.0f,     0.4f,    0.0f,   Smooth::none,   "%",   "" },
            { "rev_shimmer",     "Reverb Shimmer",        Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "rev_predelay",    "Reverb Pre-delay",      Kind::real,    0.0f,   250.0f,   20.0f,   60.0f,  Smooth::none,   "ms",  "" },

            { "reg_amount",      "Regulator Amount",      Kind::real,    0.0f,   1.0f,     0.15f,   0.0f,   Smooth::linear, "%",   "" },
            { "reg_ceiling",     "Regulator Ceiling",     Kind::real,    -12.0f, 0.0f,     -0.3f,   0.0f,   Smooth::linear, "dB",  "" },

            { "dawn",            "DAWN",                  Kind::real,    0.0f,   1.0f,     0.0f,    0.0f,   Smooth::linear, "%",   "" },
            { "master_volume",   "Master Volume",         Kind::real,    -48.0f, 6.0f,     -6.0f,   0.0f,   Smooth::linear, "dB",  "" },
            { "voice_mode",      "Voice Mode",            Kind::choice,  0.0f,   2.0f,     0.0f,    0.0f,   Smooth::none,   "",    "Poly|Mono|Legato" },
            { "voices",          "Voices",                Kind::integer, 1.0f,   16.0f,    16.0f,   0.0f,   Smooth::none,   "",    "" },
            { "glide",           "Glide",                 Kind::real,    0.0f,   2.0f,     0.0f,    0.3f,   Smooth::none,   "s",   "" },
            { "bend_range",      "Bend Range",            Kind::integer, 1.0f,   24.0f,    2.0f,    0.0f,   Smooth::none,   "st",  "" },
            { "width",           "Width",                 Kind::real,    0.0f,   1.5f,     1.0f,    0.0f,   Smooth::linear, "%",   "" },
        }};
        return specs;
    }

    inline int paramIndexForId (const juce::String& id)
    {
        const auto& specs = paramSpecs();
        for (int i = 0; i < P::count; ++i)
            if (id == specs[(size_t) i].id)
                return i;
        return -1;
    }

    inline juce::StringArray choicesFor (const ParamSpec& s)
    {
        if (juce::String (s.choices) == "@specimen")
        {
            juce::StringArray names { "User File" };
            for (const auto& e : specimenCatalog)
                names.add (e.name);
            return names;
        }
        return juce::StringArray::fromTokens (s.choices, "|", "");
    }

    inline int specimenChoiceForName (const juce::String& name)
    {
        for (int i = 0; i < specimenCatalogSize; ++i)
            if (name == specimenCatalog[i].name)
                return i + 1;
        jassertfalse;
        return 1;
    }

    inline juce::NormalisableRange<float> rangeFor (const ParamSpec& s)
    {
        juce::NormalisableRange<float> range (s.minValue, s.maxValue);
        if (s.kind != Kind::real)
            range.interval = 1.0f;
        else if (s.skewCentre > 0.0f)
            range.setSkewForCentre (s.skewCentre);
        return range;
    }

    inline juce::String formatValue (const ParamSpec& s, float v)
    {
        const juce::String unit (s.unit);
        if (unit == "%")   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        if (unit == "Hz")  return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                               : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        if (unit == "s")   return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
        if (unit == "ms")  return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
        if (unit == "dB")  return juce::String (v, 1) + " dB";
        if (unit == "ct")  return juce::String (juce::roundToInt (v)) + " ct";
        if (unit == "st")  return s.kind == Kind::real ? juce::String (v, 1) + " st"
                                                       : (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st";
        if (unit == "x")   return juce::String (v, v < 10.0f ? 3 : 2) + "x";
        if (unit == "/s")  return juce::String (v, v < 10.0f ? 1 : 0) + "/s";
        if (unit == "oct") return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v));
        return juce::String (juce::roundToInt (v));
    }

    inline float parseValue (const ParamSpec& s, const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        float v = t.retainCharacters ("0123456789.-+").getFloatValue();
        const juce::String unit (s.unit);
        if (unit == "%")                           v *= 0.01f;
        else if (unit == "Hz" && t.contains ("k")) v *= 1000.0f;
        else if (unit == "s" && t.contains ("ms")) v *= 0.001f;
        return juce::jlimit (s.minValue, s.maxValue, v);
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& s : paramSpecs())
        {
            jassert (s.id != nullptr);
            const juce::ParameterID pid { s.id, 1 };
            const auto* spec = &s;

            switch (s.kind)
            {
                case Kind::choice:
                    layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, choicesFor (s), (int) s.defaultValue));
                    break;

                case Kind::integer:
                    layout.add (std::make_unique<juce::AudioParameterInt> (
                        pid, s.name, (int) s.minValue, (int) s.maxValue, (int) s.defaultValue,
                        juce::AudioParameterIntAttributes()
                            .withStringFromValueFunction ([spec] (int v, int) { return formatValue (*spec, (float) v); })));
                    break;

                case Kind::real:
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        pid, s.name, rangeFor (s), s.defaultValue,
                        juce::AudioParameterFloatAttributes()
                            .withStringFromValueFunction ([spec] (float v, int) { return formatValue (*spec, v); })
                            .withValueFromStringFunction ([spec] (const juce::String& text) { return parseValue (*spec, text); })));
                    break;
            }
        }

        return layout;
    }
}
