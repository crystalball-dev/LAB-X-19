#include "FactoryPresets.h"
#include "Params.h"

namespace labx19
{
    namespace
    {
        // Choice indices, spelled out for readability.
        constexpr float saw = 0, pulse = 1, triangle = 2, sine = 3, table = 4;          // OSC A / OSC B
        constexpr float grain = 0, strike = 1;                                            // SPECIMEN
        constexpr float ladder = 0, svfLow = 1;                                           // FILTER
        constexpr float major = 1, sus2 = 3, sus4 = 4, add9 = 5, maj7 = 6, min7 = 7, maj9 = 9, min9 = 10,
                        sixNine = 11, fifths = 13, lydian = 15;                           // CHORD
        constexpr float open = 1, wide = 2, drop2 = 3;                                    // voicing
        constexpr float down = 0, alternate = 2;                                          // strum
        constexpr float arpUp = 1, arpUpDown = 3, arpRandom = 4, arpChord = 6;
        constexpr float r8 = 1, r16 = 3, r16t = 4, r8d = 6;                               // arp rate
        constexpr float lfoSine = 0, lfoTriangle = 1, lfoDrift = 5;
        constexpr float bars4 = 13, bars2 = 12;                                           // lfo sync
        constexpr float toCutoff = 0, toVowel = 3, toPosition = 4;
        constexpr float tube = 0, tape = 1, radio = 2;                                    // GRIT
        constexpr float d8 = 2, d8d = 3, d4 = 1, d4d = 6;                                 // delay sync
        constexpr float stereo = 0, pingPong = 1;
        constexpr float legato = 2;

        float spec (const char* name) { return (float) specimenChoiceForName (name); }

        std::vector<Preset> build()
        {
            std::vector<Preset> p;

            // 1. The init patch: a warm saw pair, open filter, a little room. Somewhere to start from.
            p.push_back ({ "Road North", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.8f }, { "uni_voices", 2.0f }, { "uni_detune", 10.0f },
                { "filter_model", svfLow }, { "filter_cutoff", 4500.0f }, { "filter_res", 0.1f },
                { "env1_attack", 0.01f }, { "env1_decay", 0.8f }, { "env1_sustain", 0.8f }, { "env1_release", 0.6f },
                { "ens_mix", 0.2f }, { "rev_mix", 0.22f }, { "master_volume", -6.0f } } });

            // 2. Many minds as one: the CHOIR alone, singers drifting, a murmuring crowd underneath.
            p.push_back ({ "C-Consciousness", {
                { "osc_a_level", 0.0f },
                { "choir_level", 0.8f }, { "choir_vowel", 0.35f }, { "choir_singers", 6.0f }, { "choir_detune", 16.0f },
                { "choir_vibrato", 0.4f }, { "choir_breath", 0.25f },
                { "air_source", spec ("Gathering") }, { "air_level", 0.3f }, { "air_position", 0.3f },
                { "filter_cutoff", 7000.0f }, { "filter_keytrack", 0.3f },
                { "env1_attack", 0.8f }, { "env1_decay", 1.0f }, { "env1_sustain", 1.0f }, { "env1_release", 2.5f },
                { "lfo_shape", lfoDrift }, { "lfo_rate", 0.15f }, { "lfo_target", toVowel }, { "lfo_depth", 0.35f },
                { "ens_mix", 0.3f }, { "rev_mix", 0.4f }, { "rev_size", 0.85f }, { "rev_decay", 0.75f }, { "rev_shimmer", 0.2f },
                { "rev_predelay", 40.0f }, { "master_volume", -6.8f } } });

            // 3. The Oasis theme as a granular cloud over a soft triangle bed, shimmering upward.
            p.push_back ({ "Oasis Bloom", {
                { "osc_a_wave", triangle }, { "osc_a_level", 0.35f }, { "uni_voices", 3.0f }, { "uni_detune", 12.0f },
                { "specimen_source", spec ("Oasis") }, { "specimen_mode", grain }, { "specimen_level", 0.7f },
                { "specimen_position", 0.3f }, { "specimen_size", 420.0f }, { "specimen_density", 28.0f },
                { "specimen_spray", 0.4f }, { "specimen_track", 1.0f },
                { "choir_level", 0.25f }, { "choir_vowel", 0.85f }, { "choir_singers", 4.0f },
                { "filter_cutoff", 5000.0f },
                { "env1_attack", 1.2f }, { "env1_decay", 1.0f }, { "env1_sustain", 1.0f }, { "env1_release", 3.0f },
                { "lfo_shape", lfoDrift }, { "lfo_rate", 0.1f }, { "lfo_target", toPosition }, { "lfo_depth", 0.25f },
                { "rev_mix", 0.45f }, { "rev_size", 0.9f }, { "rev_decay", 0.8f }, { "rev_shimmer", 0.5f },
                { "master_volume", -4.7f } } });

            // 4. The hologram at the heart of the plant: a wide saw wall that rises an octave as it fades.
            p.push_back ({ "Wish Granter", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.7f }, { "uni_voices", 7.0f }, { "uni_detune", 22.0f }, { "uni_spread", 0.9f },
                { "choir_level", 0.3f }, { "choir_vowel", 1.0f }, { "choir_singers", 4.0f },
                { "filter_model", svfLow }, { "filter_cutoff", 1400.0f }, { "filter_env", 0.35f }, { "filter_res", 0.15f },
                { "env2_attack", 2.0f }, { "env2_decay", 3.0f }, { "env2_sustain", 0.5f }, { "env2_release", 3.0f },
                { "env1_attack", 1.0f }, { "env1_sustain", 1.0f }, { "env1_release", 3.5f },
                { "grit_dust", 0.15f },
                { "rev_mix", 0.5f }, { "rev_size", 0.95f }, { "rev_decay", 0.85f }, { "rev_shimmer", 0.7f },
                { "dawn", 0.2f }, { "master_volume", -8.9f } } });

            // 5. The bells of Zalissya, struck on every key, with a sine body underneath.
            p.push_back ({ "Zalissya Bells", {
                { "osc_a_wave", sine }, { "osc_a_level", 0.35f }, { "osc_a_octave", 1.0f },
                { "osc_b_wave", sine }, { "osc_b_level", 0.15f }, { "osc_b_octave", 2.0f }, { "osc_b_semi", 7.0f }, { "osc_b_fine", 0.0f },
                { "specimen_source", spec ("Zalissya Bells") }, { "specimen_mode", strike }, { "specimen_level", 0.7f },
                { "specimen_track", 1.0f },
                { "filter_cutoff", 12000.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 3.0f }, { "env1_sustain", 0.0f }, { "env1_release", 2.0f },
                { "delay_mix", 0.2f }, { "delay_sync", d4d }, { "delay_feedback", 0.4f }, { "delay_mode", pingPong },
                { "rev_mix", 0.35f }, { "rev_shimmer", 0.2f }, { "master_volume", -0.2f } } });

            // 6. Around the fire: one key sings a whole open chord, strummed.
            p.push_back ({ "Campfire Choir", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.2f },
                { "choir_level", 0.75f }, { "choir_vowel", 0.5f }, { "choir_singers", 5.0f }, { "choir_vibrato", 0.45f },
                { "air_source", spec ("Gathering II") }, { "air_level", 0.25f },
                { "chord_type", maj9 }, { "chord_voicing", open }, { "strum_time", 35.0f }, { "strum_dir", down },
                { "filter_cutoff", 5500.0f },
                { "env1_attack", 0.35f }, { "env1_sustain", 1.0f }, { "env1_release", 2.0f },
                { "ens_mix", 0.25f }, { "rev_mix", 0.38f }, { "rev_size", 0.8f }, { "rev_decay", 0.7f },
                { "master_volume", -16.3f } } });

            // 7. Bonds deeper than family: saws and voices locked together through the ensemble.
            p.push_back ({ "Brothers in Arms", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.6f }, { "uni_voices", 5.0f }, { "uni_detune", 14.0f },
                { "osc_b_wave", pulse }, { "osc_b_level", 0.25f }, { "osc_b_octave", -1.0f }, { "osc_b_shape", 0.3f },
                { "choir_level", 0.45f }, { "choir_vowel", 0.3f }, { "choir_singers", 6.0f },
                { "chord_type", add9 }, { "chord_voicing", drop2 },
                { "filter_model", ladder }, { "filter_cutoff", 2400.0f }, { "filter_res", 0.15f },
                { "env1_attack", 0.15f }, { "env1_sustain", 0.9f }, { "env1_release", 1.2f },
                { "ens_mix", 0.6f }, { "ens_depth", 0.6f }, { "rev_mix", 0.3f },
                { "master_volume", -8.1f } } });

            // 8. The second signal, decoded step by step: an up-down arp over a minor ninth.
            p.push_back ({ "Signal B", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.6f }, { "uni_voices", 2.0f }, { "uni_detune", 8.0f },
                { "osc_b_wave", pulse }, { "osc_b_level", 0.35f }, { "osc_b_octave", 1.0f },
                { "chord_type", min9 }, { "arp_mode", arpUpDown }, { "arp_rate", r16 }, { "arp_octaves", 2.0f },
                { "arp_gate", 0.45f },
                { "filter_model", ladder }, { "filter_cutoff", 900.0f }, { "filter_res", 0.35f }, { "filter_env", 0.45f },
                { "env2_attack", 0.001f }, { "env2_decay", 0.18f }, { "env2_sustain", 0.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 0.3f }, { "env1_sustain", 0.4f }, { "env1_release", 0.2f },
                { "delay_mix", 0.3f }, { "delay_sync", d8d }, { "delay_feedback", 0.45f }, { "delay_mode", pingPong },
                { "rev_mix", 0.25f }, { "master_volume", 0.1f } } });

            // 9. Sweeping a detector through an anomaly field: random steps, chimes and door bells.
            p.push_back ({ "Artifact Hunt", {
                { "osc_a_wave", sine }, { "osc_a_level", 0.5f }, { "osc_a_octave", 1.0f },
                { "specimen_source", spec ("Door Bell") }, { "specimen_mode", strike }, { "specimen_level", 0.5f },
                { "chord_type", lydian }, { "arp_mode", arpRandom }, { "arp_rate", r16t }, { "arp_octaves", 2.0f },
                { "arp_gate", 0.3f },
                { "filter_cutoff", 9000.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 0.5f }, { "env1_sustain", 0.0f }, { "env1_release", 0.5f },
                { "delay_mix", 0.35f }, { "delay_sync", d8 }, { "delay_feedback", 0.5f }, { "delay_mode", pingPong },
                { "rev_mix", 0.35f }, { "rev_shimmer", 0.3f }, { "master_volume", -1.1f } } });

            // 10. After the emission the sky clears: whole chords struck on the beat, strummed and bright.
            p.push_back ({ "Emission Over", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.55f }, { "uni_voices", 3.0f }, { "uni_detune", 12.0f },
                { "choir_level", 0.35f }, { "choir_vowel", 0.7f },
                { "chord_type", sus2 }, { "chord_voicing", wide }, { "strum_time", 18.0f }, { "strum_dir", alternate },
                { "arp_mode", arpChord }, { "arp_rate", r8 }, { "arp_octaves", 2.0f }, { "arp_gate", 0.55f },
                { "filter_model", svfLow }, { "filter_cutoff", 1800.0f }, { "filter_env", 0.4f },
                { "env2_attack", 0.001f }, { "env2_decay", 0.3f }, { "env2_sustain", 0.2f },
                { "env1_attack", 0.002f }, { "env1_decay", 0.4f }, { "env1_sustain", 0.5f }, { "env1_release", 0.3f },
                { "grit_mode", tape }, { "grit_amount", 0.25f },
                { "delay_mix", 0.18f }, { "delay_sync", d8d }, { "rev_mix", 0.3f }, { "master_volume", -3.8f } } });

            // 11. Stabs for the bar on the Skadovsk: minor sevenths, short and gritty.
            p.push_back ({ "Skadovsk Stab", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.75f }, { "uni_voices", 5.0f }, { "uni_detune", 18.0f },
                { "chord_type", min7 }, { "chord_voicing", open },
                { "filter_model", ladder }, { "filter_cutoff", 700.0f }, { "filter_res", 0.2f }, { "filter_env", 0.55f },
                { "env2_attack", 0.001f }, { "env2_decay", 0.22f }, { "env2_sustain", 0.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 0.35f }, { "env1_sustain", 0.0f }, { "env1_release", 0.25f },
                { "grit_mode", tape }, { "grit_amount", 0.35f }, { "grit_drive", 18.0f },
                { "rev_mix", 0.2f }, { "master_volume", -0.8f } } });

            // 12. Freedom's anthem: open suspended stabs, brass-like, driven through the tube.
            p.push_back ({ "Freedom Stab", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.7f }, { "uni_voices", 3.0f }, { "uni_detune", 9.0f },
                { "osc_b_wave", pulse }, { "osc_b_level", 0.35f }, { "osc_b_shape", 0.2f },
                { "chord_type", sus4 }, { "chord_voicing", open },
                { "filter_model", svfLow }, { "filter_cutoff", 1100.0f }, { "filter_env", 0.45f },
                { "env2_attack", 0.02f }, { "env2_decay", 0.35f }, { "env2_sustain", 0.25f },
                { "env1_attack", 0.008f }, { "env1_decay", 0.5f }, { "env1_sustain", 0.3f }, { "env1_release", 0.35f },
                { "grit_mode", tube }, { "grit_amount", 0.5f }, { "grit_drive", 20.0f }, { "grit_tone", 900.0f },
                { "rev_mix", 0.22f }, { "master_volume", -3.6f } } });

            // 13. A guitar at the fire in Rostok: each key strums the campfire A chord at its own pitch.
            p.push_back ({ "Campfire at Rostok", {
                { "osc_a_wave", triangle }, { "osc_a_level", 0.15f }, { "osc_a_octave", -1.0f },
                { "specimen_source", spec ("Campfire A") }, { "specimen_mode", strike }, { "specimen_level", 0.85f },
                { "filter_cutoff", 9000.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 3.5f }, { "env1_sustain", 0.0f }, { "env1_release", 1.2f },
                { "env2_attack", 0.001f },
                { "rev_mix", 0.28f }, { "rev_size", 0.5f }, { "master_volume", -3.7f } } });

            // 14. The old piano in a dead house, one more song: piano strike, a sine body and the dust of years.
            p.push_back ({ "Old Piano Hour", {
                { "osc_a_wave", sine }, { "osc_a_level", 0.2f },
                { "specimen_source", spec ("Old Piano") }, { "specimen_mode", strike }, { "specimen_level", 0.85f },
                { "filter_cutoff", 7000.0f },
                { "env1_attack", 0.001f }, { "env1_decay", 5.0f }, { "env1_sustain", 0.0f }, { "env1_release", 1.0f },
                { "grit_mode", radio }, { "grit_amount", 0.12f }, { "grit_dust", 0.35f },
                { "rev_mix", 0.3f }, { "rev_size", 0.45f }, { "master_volume", -4.9f } } });

            // 15. A harmonica on the long road home, stretched into a pad.
            p.push_back ({ "Long Road Harmonica", {
                { "osc_a_wave", pulse }, { "osc_a_level", 0.25f }, { "osc_a_shape", 0.4f },
                { "specimen_source", spec ("Harmonica") }, { "specimen_mode", grain }, { "specimen_level", 0.8f },
                { "specimen_position", 0.2f }, { "specimen_size", 300.0f }, { "specimen_density", 30.0f },
                { "specimen_spray", 0.25f },
                { "filter_cutoff", 4000.0f },
                { "env1_attack", 0.4f }, { "env1_sustain", 1.0f }, { "env1_release", 1.5f },
                { "lfo_shape", lfoTriangle }, { "lfo_sync", bars4 }, { "lfo_target", toPosition }, { "lfo_depth", 0.3f },
                { "ens_mix", 0.3f }, { "rev_mix", 0.35f }, { "master_volume", -3.6f } } });

            // 16. Seven together: the heavy supersaw, an octave down partner, grit above 800 Hz.
            p.push_back ({ "Bonds of Seven", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.8f }, { "uni_voices", 7.0f }, { "uni_detune", 35.0f }, { "uni_spread", 1.0f },
                { "osc_b_wave", saw }, { "osc_b_level", 0.45f }, { "osc_b_octave", -1.0f }, { "osc_b_fine", -9.0f },
                { "sub_level", 0.25f },
                { "filter_model", ladder }, { "filter_cutoff", 5000.0f }, { "filter_res", 0.1f },
                { "env1_attack", 0.004f }, { "env1_sustain", 1.0f }, { "env1_release", 0.4f },
                { "grit_mode", tube }, { "grit_amount", 0.4f }, { "grit_drive", 18.0f }, { "grit_tone", 800.0f },
                { "ens_mix", 0.25f }, { "rev_mix", 0.18f }, { "reg_amount", 0.3f }, { "master_volume", -10.9f } } });

            // 17. The choice at the end: a legato saw lead that glides, wide and driven.
            p.push_back ({ "Strelok's Choice", {
                { "voice_mode", legato }, { "glide", 0.09f },
                { "osc_a_wave", saw }, { "osc_a_level", 0.8f }, { "uni_voices", 7.0f }, { "uni_detune", 24.0f },
                { "osc_b_wave", pulse }, { "osc_b_level", 0.3f }, { "osc_b_octave", 1.0f }, { "osc_b_shape", 0.5f },
                { "sub_level", 0.4f },
                { "filter_model", ladder }, { "filter_cutoff", 2600.0f }, { "filter_res", 0.25f }, { "filter_env", 0.25f },
                { "env1_attack", 0.01f }, { "env1_sustain", 1.0f }, { "env1_release", 0.35f },
                { "lfo_shape", lfoSine }, { "lfo_rate", 5.5f }, { "lfo_target", 1.0f }, { "lfo_depth", 0.012f },
                { "grit_mode", tube }, { "grit_amount", 0.45f }, { "grit_drive", 22.0f },
                { "delay_mix", 0.22f }, { "delay_sync", d8d }, { "delay_feedback", 0.35f },
                { "rev_mix", 0.25f }, { "master_volume", -6.4f } } });

            // 18. The sun on the cooling towers: clean major sevenths with grit and dust underneath.
            p.push_back ({ "Dawn Over Pripyat", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.6f }, { "uni_voices", 3.0f }, { "uni_detune", 11.0f },
                { "osc_b_wave", pulse }, { "osc_b_level", 0.3f }, { "osc_b_shape", 0.35f },
                { "chord_type", maj7 }, { "chord_voicing", open },
                { "filter_model", svfLow }, { "filter_cutoff", 3000.0f },
                { "env1_attack", 0.05f }, { "env1_sustain", 0.9f }, { "env1_release", 1.0f },
                { "grit_mode", tube }, { "grit_amount", 0.5f }, { "grit_drive", 16.0f }, { "grit_tone", 1500.0f },
                { "grit_dust", 0.25f },
                { "ens_mix", 0.35f }, { "rev_mix", 0.3f }, { "dawn", 0.2f }, { "master_volume", -8.8f } } });

            // 19. The Clear Sky theme and a six-nine chord, wide open: the possibilities are endless.
            p.push_back ({ "Endless Possibilities", {
                { "osc_a_wave", saw }, { "osc_a_level", 0.35f }, { "uni_voices", 4.0f }, { "uni_detune", 15.0f },
                { "choir_level", 0.35f }, { "choir_vowel", 0.6f }, { "choir_singers", 6.0f },
                { "specimen_source", spec ("Clear Sky") }, { "specimen_mode", grain }, { "specimen_level", 0.45f },
                { "specimen_position", 0.45f }, { "specimen_size", 500.0f }, { "specimen_density", 20.0f }, { "specimen_spray", 0.5f },
                { "chord_type", sixNine }, { "chord_voicing", wide },
                { "filter_cutoff", 4200.0f },
                { "env1_attack", 1.5f }, { "env1_sustain", 1.0f }, { "env1_release", 3.0f },
                { "lfo_shape", lfoDrift }, { "lfo_sync", bars2 }, { "lfo_target", toCutoff }, { "lfo_depth", 0.2f },
                { "ens_mix", 0.3f }, { "rev_mix", 0.45f }, { "rev_size", 0.9f }, { "rev_decay", 0.8f }, { "rev_shimmer", 0.45f },
                { "master_volume", -10.1f } } });

            juce::ignoreUnused (table, fifths, major, d4, stereo);
            return p;
        }
    }

    const std::vector<Preset>& factoryPresets()
    {
        static const std::vector<Preset> presets = build();
        return presets;
    }
}
