#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/CrtLookAndFeel.h"
#include "UI/Widgets.h"

class LabX19AudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LabX19AudioProcessorEditor (LabX19AudioProcessor&);
    ~LabX19AudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Pulls meters, scope and status from the processor. Public so the render harness can call it.
    void refreshFromProcessor();

    static constexpr int editorWidth = 1320;
    static constexpr int editorHeight = 860;

private:
    void timerCallback() override { refreshFromProcessor(); }

    struct PanelItem
    {
        juce::Component* component;
        float units;
    };

    struct Panel
    {
        juce::String title;
        std::vector<PanelItem> items;
        juce::Rectangle<int> bounds;
        bool accent = false;
    };

    // Source selection, file buttons and loader status for one layer (SPECIMEN or AIR).
    class SourceControls final : public juce::Component
    {
    public:
        SourceControls (LabX19AudioProcessorEditor& owner, juce::AudioProcessorValueTreeState& state,
                        const juce::String& paramId, bool airLayer);
        void resized() override;

        labx19::ui::LabeledChoice source;
        juce::TextButton loadButton { "LOAD FILE" }, libraryButton { "LIBRARY" };
        juce::Label status;
    };

    labx19::ui::LabeledKnob* knob (const char* id, const char* caption, bool accent = false);
    labx19::ui::ChoiceStack* choices (const char* idA, const char* captionA, const char* idB = "", const char* captionB = "");
    void layoutRow (std::vector<Panel>& row, juce::Rectangle<int> area, float unitWidth);
    void stepPreset (int delta);
    void chooseLibraryFolder();
    void chooseUserFile (bool airLayer);

    LabX19AudioProcessor& audioProcessor;
    labx19::ui::CrtLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<labx19::ui::LabeledKnob>> knobs;
    std::vector<std::unique_ptr<labx19::ui::ChoiceStack>> stacks;
    std::vector<std::vector<Panel>> rows;
    std::vector<Panel> sidePanels;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    std::unique_ptr<SourceControls> specimenControls, airControls;
    labx19::ui::HorizonView horizon;
    labx19::ui::CompanyView company;
    labx19::ui::ScopeView scopeView;
    labx19::ui::LabeledKnob* dawnKnob = nullptr;
    juce::Label systemLabel;
    labx19::ui::ScanlineOverlay overlay;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<float> scopeBuffer;
    int lastProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LabX19AudioProcessorEditor)
};
