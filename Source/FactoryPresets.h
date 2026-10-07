#pragma once

#include <utility>
#include <vector>
#include <juce_core/juce_core.h>

namespace labx19
{
    struct Preset
    {
        juce::String name;
        std::vector<std::pair<juce::String, float>> values;   // parameter id, real value; others use defaults
    };

    const std::vector<Preset>& factoryPresets();
}
