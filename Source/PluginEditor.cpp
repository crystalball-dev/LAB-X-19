#include "PluginEditor.h"
#include "FactoryPresets.h"

using namespace labx19;
using namespace labx19::ui;

namespace
{
    constexpr int headerHeight = 58;
    constexpr int footerHeight = 24;
    constexpr int margin = 10;
    constexpr int sideWidth = 236;
    constexpr int gap = 8;
    constexpr int panelPad = 8;
    constexpr int titleHeight = 18;
}

//==============================================================================
LabX19AudioProcessorEditor::SourceControls::SourceControls (LabX19AudioProcessorEditor& owner,
                                                            juce::AudioProcessorValueTreeState& state,
                                                            const juce::String& paramId, bool airLayer)
    : source (state, paramId, "SOURCE")
{
    addAndMakeVisible (source);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (libraryButton);
    addAndMakeVisible (status);

    loadButton.setTooltip ("Load your own audio file into this layer");
    libraryButton.setTooltip ("Choose your STALKER SPECIMENS library folder");
    loadButton.onClick = [&owner, airLayer] { owner.chooseUserFile (airLayer); };
    libraryButton.onClick = [&owner] { owner.chooseLibraryFolder(); };

    status.setFont (monoFont (9.5f));
    status.setJustificationType (juce::Justification::topLeft);
    status.setMinimumHorizontalScale (0.6f);
}

void LabX19AudioProcessorEditor::SourceControls::resized()
{
    auto b = getLocalBounds().reduced (2, 0);
    source.setBounds (b.removeFromTop (37));
    b.removeFromTop (4);
    auto buttons = b.removeFromTop (19);
    loadButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
    buttons.removeFromLeft (4);
    libraryButton.setBounds (buttons);
    b.removeFromTop (3);
    status.setBounds (b);
}

//==============================================================================
LabX19AudioProcessorEditor::LabX19AudioProcessorEditor (LabX19AudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    auto& state = audioProcessor.apvts;

    specimenControls = std::make_unique<SourceControls> (*this, state, "specimen_source", false);
    airControls = std::make_unique<SourceControls> (*this, state, "air_source", true);
    addAndMakeVisible (*specimenControls);
    addAndMakeVisible (*airControls);

    // Row 1: the oscillators
    rows.push_back ({
        { utf8 ("OSC A  //  ГЕНЕРАТОР"), { { choices ("osc_a_wave", "WAVE"), 1.5f }, { knob ("osc_a_octave", "OCT"), 1.0f },
                                           { knob ("osc_a_semi", "SEMI"), 1.0f }, { knob ("osc_a_fine", "FINE"), 1.0f },
                                           { knob ("osc_a_shape", "SHAPE"), 1.0f }, { knob ("osc_a_level", "LEVEL"), 1.0f } } },
        { utf8 ("BONDS  //  УЗЫ"), { { knob ("uni_voices", "VOICES"), 1.0f }, { knob ("uni_detune", "DETUNE"), 1.0f },
                                     { knob ("uni_spread", "SPREAD"), 1.0f } } },
        { "OSC B", { { choices ("osc_b_wave", "WAVE"), 1.5f }, { knob ("osc_b_octave", "OCT"), 1.0f },
                     { knob ("osc_b_semi", "SEMI"), 1.0f }, { knob ("osc_b_fine", "FINE"), 1.0f },
                     { knob ("osc_b_shape", "SHAPE"), 1.0f }, { knob ("osc_b_level", "LEVEL"), 1.0f } } },
        { "SUB  +  NOISE", { { knob ("sub_level", "SUB"), 1.0f }, { knob ("noise_level", "NOISE"), 1.0f },
                             { knob ("noise_colour", "COLOUR"), 1.0f } } },
    });

    // Row 2: the voices
    rows.push_back ({
        { utf8 ("CHOIR  //  ХОР"), { { knob ("choir_level", "LEVEL"), 1.0f }, { knob ("choir_vowel", "VOWEL"), 1.0f },
                                     { knob ("choir_singers", "SINGERS"), 1.0f }, { knob ("choir_detune", "DETUNE"), 1.0f },
                                     { knob ("choir_vibrato", "VIBRATO"), 1.0f }, { knob ("choir_breath", "BREATH"), 1.0f },
                                     { knob ("choir_size", "SIZE"), 1.0f }, { knob ("choir_octave", "OCT"), 1.0f } } },
        { utf8 ("AIR  //  ВОЗДУХ"), { { airControls.get(), 2.6f }, { knob ("air_level", "LEVEL"), 1.0f },
                                      { knob ("air_position", "POSITION"), 1.0f }, { knob ("air_size", "GRAIN"), 1.0f },
                                      { knob ("air_track", "KEYTRACK"), 1.0f } } },
    });

    // Row 3: the zone's own sounds and the filter
    rows.push_back ({
        { utf8 ("SPECIMEN  //  ОБРАЗЕЦ"), { { specimenControls.get(), 2.6f }, { choices ("specimen_mode", "MODE"), 1.3f },
                                            { knob ("specimen_level", "LEVEL"), 1.0f }, { knob ("specimen_position", "POSITION"), 1.0f },
                                            { knob ("specimen_size", "GRAIN"), 1.0f }, { knob ("specimen_density", "DENSITY"), 1.0f },
                                            { knob ("specimen_spray", "SPRAY"), 1.0f }, { knob ("specimen_track", "KEYTRACK"), 1.0f },
                                            { knob ("specimen_tune", "TUNE"), 1.0f } } },
        { "FILTER", { { choices ("filter_model", "MODEL"), 1.5f }, { knob ("filter_cutoff", "CUTOFF"), 1.0f },
                      { knob ("filter_res", "RESO"), 1.0f }, { knob ("filter_drive", "DRIVE"), 1.0f },
                      { knob ("filter_env", "ENV"), 1.0f }, { knob ("filter_keytrack", "KEYTRACK"), 1.0f },
                      { knob ("filter_vel", "VELOCITY"), 1.0f } } },
    });

    // Row 4: envelopes and the note engine
    rows.push_back ({
        { "AMP ENVELOPE",    { { knob ("env1_attack", "A"), 1.0f }, { knob ("env1_decay", "D"), 1.0f },
                               { knob ("env1_sustain", "S"), 1.0f }, { knob ("env1_release", "R"), 1.0f },
                               { knob ("amp_vel", "VEL"), 1.0f } } },
        { "FILTER ENVELOPE", { { knob ("env2_attack", "A"), 1.0f }, { knob ("env2_decay", "D"), 1.0f },
                               { knob ("env2_sustain", "S"), 1.0f }, { knob ("env2_release", "R"), 1.0f } } },
        { utf8 ("CHORD  //  АККОРД"), { { choices ("chord_type", "SHAPE", "chord_voicing", "VOICING"), 1.5f },
                                        { knob ("strum_time", "STRUM"), 1.0f }, { choices ("strum_dir", "DIRECTION"), 1.4f } } },
        { "ARP", { { choices ("arp_mode", "MODE", "arp_rate", "RATE"), 1.5f }, { knob ("arp_octaves", "OCT"), 1.0f },
                   { knob ("arp_gate", "GATE"), 1.0f }, { knob ("arp_swing", "SWING"), 1.0f },
                   { choices ("arp_latch", "LATCH"), 1.2f } } },
    });

    // Row 5: modulation, grit, ensemble and delay
    rows.push_back ({
        { "LFO", { { choices ("lfo_shape", "SHAPE", "lfo_sync", "SYNC"), 1.5f },
                   { choices ("lfo_target", "TARGET", "lfo_retrig", "RETRIG"), 1.5f },
                   { knob ("lfo_rate", "RATE"), 1.0f }, { knob ("lfo_depth", "DEPTH"), 1.0f } } },
        { utf8 ("GRIT  //  ПЫЛЬ"), { { choices ("grit_mode", "MODE"), 1.4f }, { knob ("grit_amount", "AMOUNT"), 1.0f },
                                     { knob ("grit_drive", "DRIVE"), 1.0f }, { knob ("grit_tone", "CORNER"), 1.0f },
                                     { knob ("grit_dust", "DUST"), 1.0f } } },
        { "ENSEMBLE", { { knob ("ens_mix", "MIX"), 1.0f }, { knob ("ens_depth", "DEPTH"), 1.0f },
                        { knob ("ens_rate", "RATE"), 1.0f } } },
        { "DELAY", { { choices ("delay_sync", "SYNC", "delay_mode", "MODE"), 1.5f }, { knob ("delay_time", "TIME"), 1.0f },
                     { knob ("delay_feedback", "FEEDBACK"), 1.0f }, { knob ("delay_tone", "TONE"), 1.0f },
                     { knob ("delay_mix", "MIX"), 1.0f } } },
    });

    // Row 6: the space, the regulator and the master section
    rows.push_back ({
        { utf8 ("REVERB  //  ОАЗИС"), { { knob ("rev_mix", "MIX"), 1.0f }, { knob ("rev_size", "SIZE"), 1.0f },
                                        { knob ("rev_decay", "DECAY"), 1.0f }, { knob ("rev_damping", "DAMPING"), 1.0f },
                                        { knob ("rev_shimmer", "SHIMMER"), 1.0f }, { knob ("rev_predelay", "PRE-DELAY"), 1.0f } } },
        { utf8 ("REGULATOR  //  РЕГУЛЯТОР"), { { knob ("reg_amount", "AMOUNT"), 1.0f }, { knob ("reg_ceiling", "CEILING"), 1.0f } } },
        { "MASTER", { { choices ("voice_mode", "MODE"), 1.4f }, { knob ("voices", "VOICES"), 1.0f },
                      { knob ("glide", "GLIDE"), 1.0f }, { knob ("bend_range", "BEND"), 1.0f },
                      { knob ("width", "WIDTH"), 1.0f }, { knob ("master_volume", "VOLUME"), 1.0f } } },
    });

    dawnKnob = knob ("dawn", "DAWN", true);

    sidePanels.push_back ({ utf8 ("HORIZON  //  ГОРИЗОНТ"), { { &horizon, 1.0f } } });
    sidePanels.push_back ({ utf8 ("DAWN  //  РАССВЕТ"), { { dawnKnob, 1.0f } }, {}, true });
    sidePanels.push_back ({ utf8 ("COMPANY  //  ОТРЯД"), { { &company, 1.0f } } });
    sidePanels.push_back ({ "MONITOR", { { &scopeView, 1.0f } } });
    sidePanels.push_back ({ "SYSTEM", { { &systemLabel, 1.0f } } });

    addAndMakeVisible (horizon);
    addAndMakeVisible (company);
    addAndMakeVisible (scopeView);
    systemLabel.setFont (monoFont (11.0f));
    systemLabel.setJustificationType (juce::Justification::topLeft);
    systemLabel.setColour (juce::Label::textColourId, Palette::phosphor);
    addAndMakeVisible (systemLabel);

    // Preset browser
    const auto& presets = factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        presetBox.addItem (juce::String (i + 1).paddedLeft ('0', 2) + "  " + presets[(size_t) i].name, i + 1);
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != audioProcessor.getCurrentProgram())
            audioProcessor.setCurrentProgram (index);
        lastProgram = audioProcessor.getCurrentProgram();
    };
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    addAndMakeVisible (overlay);

    scopeBuffer.assign ((size_t) LabX19AudioProcessor::scopeSize, 0.0f);

    // Controls were built before they had a parent, so they captured the default theme's colours.
    // Re-broadcasting makes every slider rebuild its text box through the CRT look-and-feel.
    sendLookAndFeelChange();

    setSize (editorWidth, editorHeight);
    refreshFromProcessor();
    startTimerHz (30);
}

LabX19AudioProcessorEditor::~LabX19AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

LabeledKnob* LabX19AudioProcessorEditor::knob (const char* id, const char* caption, bool accent)
{
    knobs.push_back (std::make_unique<LabeledKnob> (audioProcessor.apvts, id, caption, accent));
    addAndMakeVisible (*knobs.back());
    return knobs.back().get();
}

ChoiceStack* LabX19AudioProcessorEditor::choices (const char* idA, const char* captionA, const char* idB, const char* captionB)
{
    stacks.push_back (std::make_unique<ChoiceStack> (audioProcessor.apvts, idA, captionA, idB, captionB));
    addAndMakeVisible (*stacks.back());
    return stacks.back().get();
}

//==============================================================================
void LabX19AudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll (Palette::background);

    // Faint grid behind everything
    g.setColour (Palette::faint.withAlpha (0.35f));
    for (int x = 0; x < bounds.getWidth(); x += 24)
        g.drawVerticalLine (x, 0.0f, (float) bounds.getHeight());
    for (int y = 0; y < bounds.getHeight(); y += 24)
        g.drawHorizontalLine (y, 0.0f, (float) bounds.getWidth());

    // Header
    auto header = bounds.withHeight (headerHeight).reduced (margin + 6, 6);
    g.setFont (monoFont (30.0f, true));
    g.setColour (Palette::phosphor.withAlpha (0.18f));
    g.drawText ("LAB X-19", header.translated (0, 1).expanded (1, 0), juce::Justification::topLeft);
    g.setColour (Palette::phosphor);
    g.drawText ("LAB X-19", header, juce::Justification::topLeft);
    g.setFont (monoFont (11.0f));
    g.setColour (Palette::dim);
    g.drawText (utf8 ("ЛАБОРАТОРИЯ X19  //  CHORDS, CHOIRS AND THE ROAD AHEAD"), header.withTrimmedTop (32), juce::Justification::topLeft);
    g.drawText (juce::String (vendorName) + "  //  v" + versionString, header.withTrimmedTop (32), juce::Justification::topRight);
    g.setColour (Palette::border);
    g.drawHorizontalLine (headerHeight - 2, (float) margin, (float) (bounds.getWidth() - margin));

    // Panels
    auto drawPanel = [&g] (const Panel& panel)
    {
        const auto r = panel.bounds.toFloat();
        g.setColour (Palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (panel.accent ? Palette::red.withAlpha (0.45f) : Palette::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        auto title = panel.bounds.withHeight (titleHeight).reduced (panelPad, 0);
        g.setFont (monoFont (10.5f, true));
        g.setColour (panel.accent ? Palette::red : Palette::phosphor.withAlpha (0.85f));
        g.drawText (panel.title, title.withTrimmedTop (3), juce::Justification::centredLeft);
        g.setColour (panel.accent ? Palette::red.withAlpha (0.3f) : Palette::faint);
        g.drawHorizontalLine (panel.bounds.getY() + titleHeight, r.getX() + 4.0f, r.getRight() - 4.0f);
    };

    for (const auto& row : rows)
        for (const auto& panel : row)
            drawPanel (panel);
    for (const auto& panel : sidePanels)
        drawPanel (panel);

    // Footer
    auto footer = bounds.withTop (bounds.getHeight() - footerHeight).reduced (margin + 6, 4);
    g.setFont (monoFont (10.0f));
    g.setColour (Palette::dim);
    g.drawText (juce::String ("OPERATION FAIRWAY  //  AGPL-3.0  //  ") + projectUrl, footer, juce::Justification::centredLeft);
    g.drawText ("SOUND SOURCES READ FROM YOUR OWN DISK  //  NO GAME AUDIO INCLUDED", footer, juce::Justification::centredRight);
}

void LabX19AudioProcessorEditor::layoutRow (std::vector<Panel>& row, juce::Rectangle<int> area, float unitWidth)
{
    float naturalTotal = 0.0f;
    for (const auto& panel : row)
    {
        float units = 0.0f;
        for (const auto& item : panel.items)
            units += item.units;
        naturalTotal += units * unitWidth + (float) (2 * panelPad);
    }

    const float spare = std::max (0.0f, (float) area.getWidth() - naturalTotal - (float) (gap * ((int) row.size() - 1)));
    const float extraPerPanel = spare / (float) row.size();

    float x = (float) area.getX();
    for (auto& panel : row)
    {
        float units = 0.0f;
        for (const auto& item : panel.items)
            units += item.units;

        const float width = units * unitWidth + (float) (2 * panelPad) + extraPerPanel;
        panel.bounds = juce::Rectangle<int> ((int) std::round (x), area.getY(), (int) std::round (width), area.getHeight());

        auto content = panel.bounds.withTrimmedTop (titleHeight + 4).reduced (panelPad, 0).withTrimmedBottom (4);
        const float contentNatural = units * unitWidth;
        float cx = (float) content.getX() + ((float) content.getWidth() - contentNatural) * 0.5f;
        for (const auto& item : panel.items)
        {
            const float w = item.units * unitWidth;
            item.component->setBounds (juce::Rectangle<int> ((int) std::round (cx), content.getY(), (int) std::round (w), content.getHeight()));
            cx += w;
        }

        x += width + (float) gap;
    }
}

void LabX19AudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    overlay.setBounds (bounds);

    // Header controls
    auto header = bounds.withHeight (headerHeight).reduced (margin, 14);
    auto presetArea = header.withSizeKeepingCentre (340, 26).translated (60, -4);
    prevButton.setBounds (presetArea.removeFromLeft (26));
    nextButton.setBounds (presetArea.removeFromRight (26));
    presetBox.setBounds (presetArea.reduced (4, 0));

    auto body = bounds.withTrimmedTop (headerHeight + 4).withTrimmedBottom (footerHeight).reduced (margin, 0);
    auto side = body.removeFromRight (sideWidth);
    body.removeFromRight (gap);

    // One unit width for the whole grid: the widest row decides, so every knob is the same size.
    const int rowCount = (int) rows.size();
    const int rowHeight = (body.getHeight() - gap * (rowCount - 1)) / rowCount;
    float unitWidth = 64.0f;
    for (const auto& row : rows)
    {
        float units = 0.0f;
        for (const auto& panel : row)
            for (const auto& item : panel.items)
                units += item.units;
        const float overhead = (float) (row.size() * 2 * panelPad + (row.size() - 1) * gap);
        unitWidth = std::min (unitWidth, ((float) body.getWidth() - overhead) / units);
    }

    for (int r = 0; r < rowCount; ++r)
        layoutRow (rows[(size_t) r], body.withTrimmedTop (r * (rowHeight + gap)).withHeight (rowHeight), unitWidth);

    // Side column: HORIZON, DAWN, COMPANY, scope, SYSTEM (takes the rest)
    const int heights[] = { 150, 128, 112, 110, 0 };
    auto column = side;
    for (size_t i = 0; i < sidePanels.size(); ++i)
    {
        auto& panel = sidePanels[i];
        panel.bounds = heights[i] > 0 ? column.removeFromTop (heights[i]) : column;
        column.removeFromTop (gap);
        auto content = panel.bounds.withTrimmedTop (titleHeight + 4).reduced (panelPad, 4);
        for (auto& item : panel.items)
            item.component->setBounds (content);
    }

    if (dawnKnob != nullptr)
        dawnKnob->setBounds (sidePanels[1].bounds.withTrimmedTop (titleHeight + 2).reduced (62, 4));

    overlay.toFront (false);
}

//==============================================================================
void LabX19AudioProcessorEditor::refreshFromProcessor()
{
    const auto& m = audioProcessor.meters;
    horizon.setValues (m.dawn.load(), m.rmsDb.load());

    auto& state = audioProcessor.apvts;
    auto choiceText = [&state] (const char* id)
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            return c->getCurrentChoiceName();
        return juce::String();
    };
    company.setValues (choiceText ("chord_type") + (choiceText ("chord_type") == "Off" ? "" : "  " + choiceText ("chord_voicing")),
                       choiceText ("arp_mode") + (choiceText ("arp_mode") == "Off" ? "" : "  " + choiceText ("arp_rate")),
                       m.heldKeys.load(), m.arpStep.load());

    audioProcessor.copyScope (scopeBuffer.data(), (int) scopeBuffer.size());
    scopeView.setSamples (scopeBuffer.data(), (int) scopeBuffer.size());

    auto showStatus = [] (SourceControls& controls, SpecimenLibrary& lib)
    {
        const auto status = lib.getStatus();
        controls.status.setText (lib.getStatusText(), juce::dontSendNotification);
        controls.status.setColour (juce::Label::textColourId,
            status == SpecimenLibrary::Status::ready ? Palette::phosphor
            : (status == SpecimenLibrary::Status::loading || status == SpecimenLibrary::Status::idle ? Palette::dim : Palette::red));
    };
    showStatus (*specimenControls, audioProcessor.getSpecimenLibrary());
    showStatus (*airControls, audioProcessor.getAirLibrary());

    const int program = audioProcessor.getCurrentProgram();
    if (program != lastProgram)
    {
        presetBox.setSelectedId (program + 1, juce::dontSendNotification);
        lastProgram = program;
    }

    const int mode = (int) std::lround (state.getRawParameterValue ("voice_mode")->load());
    const int limit = mode == Choice::poly ? (int) std::lround (state.getRawParameterValue ("voices")->load()) : 1;
    const char* modeNames[] = { "POLY", "MONO", "LEGATO" };
    const bool library = audioProcessor.getSpecimenLibrary().getLibraryRoot().isDirectory();

    juce::String text;
    text << "VOICES    " << m.activeVoices.load() << " / " << limit << "  " << modeNames[juce::jlimit (0, 2, mode)] << "\n"
         << "MIDI IN   " << juce::String (m.midiPerSecond.load(), 1) << " /s\n"
         << "BPM       " << juce::String (m.bpm.load(), 1) << "  " << (m.synced.load() ? "SYNC" : "FREE") << "\n"
         << "LATENCY   " << m.latency.load() << " smp\n"
         << "PEAK      " << juce::String (m.peakDb.load(), 1) << " dBFS\n"
         << "REGULATE  " << juce::String (m.gainReductionDb.load(), 1) << " dB\n"
         << "FAULTS    " << m.faults.load()
         << (m.lastFaultStage.load() >= 0 ? juce::String ("  ") + LabX19AudioProcessor::stageName (m.lastFaultStage.load()) : juce::String())
         << "\n"
         << "LIBRARY   " << (library ? "SPECIMENS" : "NOT FOUND") << "\n"
         << "CHOICE    PENDING";
    systemLabel.setText (text, juce::dontSendNotification);
}

void LabX19AudioProcessorEditor::stepPreset (int delta)
{
    const int count = audioProcessor.getNumPrograms();
    const int next = (audioProcessor.getCurrentProgram() + delta + count) % count;
    audioProcessor.setCurrentProgram (next);
    presetBox.setSelectedId (next + 1, juce::dontSendNotification);
    lastProgram = next;
}

void LabX19AudioProcessorEditor::chooseLibraryFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Select your STALKER SPECIMENS library folder",
                                                   audioProcessor.getSpecimenLibrary().getLibraryRoot(), "*");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir.isDirectory())
                                  audioProcessor.setLibraryRoot (dir, true);
                          });
}

void LabX19AudioProcessorEditor::chooseUserFile (bool airLayer)
{
    auto& lib = airLayer ? audioProcessor.getAirLibrary() : audioProcessor.getSpecimenLibrary();
    const auto start = lib.getUserFile().existsAsFile() ? lib.getUserFile() : lib.getLibraryRoot();
    chooser = std::make_unique<juce::FileChooser> ("Load a sound", start, "*.wav;*.ogg;*.flac;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, airLayer] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              auto& target = airLayer ? audioProcessor.getAirLibrary() : audioProcessor.getSpecimenLibrary();
                              target.setUserFile (file);
                              if (auto* param = audioProcessor.apvts.getParameter (airLayer ? "air_source" : "specimen_source"))
                                  param->setValueNotifyingHost (param->convertTo0to1 ((float) specimenUserChoice));
                          });
}
