#pragma once

namespace labx19
{
    inline constexpr const char* pluginName    = "LAB X-19";
    inline constexpr const char* vendorName    = "OPERATION FAIRWAY, LLC";
    inline constexpr const char* versionString = "0.1.1";
    inline constexpr const char* projectUrl    = "github.com/crystalball-dev/LAB-X-19";

    inline constexpr int maxVoices    = 16;   // one voice per sounding note; chords take one voice per chord note
    inline constexpr int maxUnison    = 7;    // BONDS copies of OSC A
    inline constexpr int maxSingers   = 8;    // CHOIR singers per voice
    inline constexpr int maxChord     = 6;    // notes one key can expand to
    inline constexpr int controlBlock = 16;   // samples per control-rate step
}
