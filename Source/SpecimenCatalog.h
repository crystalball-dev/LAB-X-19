#pragma once

#include <iterator>

namespace labx19
{
    // Sounds the SPECIMEN and AIR layers read from the STALKER SPECIMENS library, a folder of sounds extracted
    // from the user's own copies of the games (<drive>:\_AUDIO\STALKER SPECIMENS\<game>\sounds\...).
    // Only relative paths live here. This repository contains no game audio.
    //
    // Append new entries at the end only. Choice indices are saved inside host projects.
    //
    // rootNote: the source's pitch class placed in octave 3 (C3 = 48). Playing the root key plays the file at its
    // recorded pitch in Strike and Grain modes. Measured with an averaged FFT chroma of the mono decode.
    struct SpecimenEntry
    {
        const char* name;
        const char* relativePath;
        float rootNote;
    };

    #define LABX19_GP "heart_of_chornobyl/sounds/Events/Playing_Guitar/"
    #define LABX19_S2 "heart_of_chornobyl/sounds/Events/"

    inline constexpr SpecimenEntry specimenCatalog[] =
    {
        // STALKER 2 campfire guitar: one down-strum per chord, recorded in A major and C major.
        { "Campfire A",        LABX19_GP "Chords/A_major/A/SFX_GP_Chords_A_Down_01.flac",                    57.0f },   // C# E A
        { "Campfire D",        LABX19_GP "Chords/A_major/D/SFX_GP_Chords_D_Down_01.flac",                    50.0f },   // A D F#
        { "Campfire E",        LABX19_GP "Chords/A_major/E/SFX_GP_Chords_E_Down_01.flac",                    52.0f },   // E B F#
        { "Campfire Bm",       LABX19_GP "Chords/A_major/Bm/SFX_GP_Chords_Bm_Down_01.flac",                  59.0f },   // F# B D
        { "Campfire F#m",      LABX19_GP "Chords/A_major/F_m/SFX_GP_Chords_F_dies_minor_Down_01.flac",       54.0f },   // F# C# A
        { "Campfire C#m",      LABX19_GP "Chords/A_major/C_m/SFX_GP_Chords_C_dies_minor_Down_01.flac",       49.0f },   // C# G# E
        { "Campfire C",        LABX19_GP "Chords/C_major/C/SFX_GP_Chords_C_Down_01.flac",                    48.0f },   // C G E
        { "Campfire G",        LABX19_GP "Chords/C_major/G/SFX_GP_Chords_G_Down_01.flac",                    55.0f },   // G D B
        { "Campfire Am",       LABX19_GP "Chords/C_major/Am/SFX_GP_Chords_Am_Down_01.flac",                  57.0f },   // E A C
        { "Campfire Em",       LABX19_GP "Chords/C_major/Em/SFX_GP_Chords_Em_Down_01.flac",                  52.0f },   // G E B
        { "Campfire Dm",       LABX19_GP "Chords/C_major/Dm/SFX_GP_Chords_Dm_Down_01.flac",                  50.0f },   // A D F
        { "Campfire F",        LABX19_GP "Chords/C_major/F/SFX_GP_Chords_F_Down_01.flac",                    53.0f },   // C F A
        { "Strum Glissando",   LABX19_GP "Gliss/A/SFX_GP_Chords_A_Gliss_01.flac",                            57.0f },   // A chord swept, very quiet
        { "Fingerstyle Night", LABX19_S2 "Contextual_Actions/CA_Guitar/SFX_CA_Guitar_Fingerstyle_Music_01.flac", 53.0f }, // 73 s, F
        { "Campfire Song",     LABX19_S2 "Contextual_Actions/CA_Guitar/SFX_CA_Guitar_Strum_Music_01.flac",   50.0f },   // 60 s, D
        { "Rostok Guitar",     "shadow_of_chornobyl/sounds/music/guitar_1.ogg",                              54.0f },   // 29 s, F#
        { "Harmonica",         "call_of_prypiat/sounds/localization/rus/characters_voice/human_01/stalker/music/harmonica_1.ogg", 57.0f }, // 34 s, A
        { "Old Piano",         LABX19_S2 "DLC-1/Quests/MQ/MQ06/OldPiano/SFX_MQ06_OldPiano_A.flac",           57.0f },   // one A, 8 s ring

        // Bells, chimes and tones
        { "Zalissya Bells",    LABX19_S2 "Ambient_Actors/Zalissya/SFX_Zalissya_Arc_Bells_01.flac",           56.0f },   // G#, 3.4 kHz shimmer
        { "Zalissya Bells II", LABX19_S2 "Ambient_Actors/Zalissya/SFX_Zalissya_Arc_Bells_02.flac",           49.0f },   // C#
        { "Zalissya Arc",      LABX19_S2 "Ambient_Actors/Zalissya/SFX_Zalissya_Arc_Drone.flac",              55.0f },   // G B D drone
        { "Door Bell",         LABX19_S2 "DLC-1/InteractableObjects/IO_DoorBell/SFX_DoorBell_Bell_01.flac",  50.0f },   // D
        { "Elevator Bell",     LABX19_S2 "Quests/MQ/E06/E06_MQ01/Elevator/SFX_E06_MQ01_Elevator_Bell.flac",  48.0f },   // pure C
        { "Wind Chimes",       "shadow_of_chornobyl/sounds/ambient/random/rnd_chimes.ogg",                   49.0f },   // C#
        { "X-16 Hum",          "shadow_of_chornobyl/sounds/ambient/x16/x16_hum_2.ogg",                       55.0f },   // pure G

        // Voices and gatherings: texture for the AIR layer
        { "Gathering",         LABX19_S2 "Arena/SFX_Arena_Crowd_Hum_Loop_01.flac",                           60.0f },   // crowd murmur
        { "Gathering II",      LABX19_S2 "Arena/SFX_Arena_Crowd_Hum_Loop_02.flac",                           60.0f },
        { "Object 68",         LABX19_S2 "DLC-1/Narrative_Scenes/NS_032_MQ03/SFX_NS_032_MQ03_Object68_Vocalization.flac", 48.0f },
        { "Monolith Lamp",     LABX19_S2 "Interactive_Objects/IO_DallinRoom/SFX_IO_DalinRoom_MonolithLamp_Voice_01.flac", 58.0f },

        // The games' own score, as granular raw material
        { "Wasteland Score",   "shadow_of_chornobyl/sounds/music/amb01_l.ogg",                               58.0f },   // A# E F
        { "Night Score",       "shadow_of_chornobyl/sounds/music/amb10_l.ogg",                               53.0f },
        { "Oasis",             LABX19_S2 "Music/Quests/MQ/E11_MQ01/MUS_E11_MQ01_Oasis.flac",                 53.0f },   // F
        { "Clear Sky",         LABX19_S2 "Music/Cutscenes/MUS_E05_MQ03_ClearSky.flac",                       48.0f },   // B C
        { "Zone Alive",        LABX19_S2 "Music/Cutscenes/MUS_E06_MQ04_Zone_Alive/MUS_E06_MQ04_Zone_Alive_First_Part.flac", 48.0f }, // C
    };

    #undef LABX19_GP
    #undef LABX19_S2

    inline constexpr int specimenCatalogSize = (int) std::size (specimenCatalog);

    // Choice index 0 is the user's own file. Catalog entry i is choice i + 1.
    inline constexpr int specimenUserChoice = 0;

    // Default location of the STALKER SPECIMENS library on any drive, and the game folders it may hold.
    inline constexpr const char* specimenLibrarySubPath = "_AUDIO/STALKER SPECIMENS";
    inline constexpr const char* specimenLibraryGames[] = { "shadow_of_chornobyl", "clear_sky", "call_of_prypiat",
                                                            "heart_of_chornobyl" };
}
