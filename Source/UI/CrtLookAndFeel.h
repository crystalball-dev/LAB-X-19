#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace labx19::ui
{
    // CRT phosphor in pale dawn cyan, the third terminal after LAB X-3's green and LAB X-10's amber.
    // The warm accent (still called red in code, shared with the sibling widgets) marks DAWN and warnings.
    struct Palette
    {
        static inline const juce::Colour background { 0xff04080b };
        static inline const juce::Colour panel      { 0xff08121a };
        static inline const juce::Colour border     { 0xff17394a };
        static inline const juce::Colour faint      { 0xff0e2431 };
        static inline const juce::Colour dim        { 0xff4596ad };
        static inline const juce::Colour phosphor   { 0xff86e4ff };
        static inline const juce::Colour bright     { 0xffdaf8ff };
        static inline const juce::Colour red        { 0xffffb65e };
        static inline const juce::Colour knobTop    { 0xff10283a };
        static inline const juce::Colour knobBottom { 0xff04080b };
    };

    inline juce::Font monoFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Consolas", height, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    class CrtLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        CrtLookAndFeel()
        {
            using P = Palette;
            setColour (juce::ResizableWindow::backgroundColourId, P::background);
            setColour (juce::Label::textColourId, P::dim);
            setColour (juce::Slider::textBoxTextColourId, P::phosphor);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::Slider::textBoxHighlightColourId, P::dim.withAlpha (0.5f));
            setColour (juce::ComboBox::backgroundColourId, P::panel);
            setColour (juce::ComboBox::textColourId, P::phosphor);
            setColour (juce::ComboBox::outlineColourId, P::border);
            setColour (juce::ComboBox::arrowColourId, P::phosphor);
            setColour (juce::PopupMenu::backgroundColourId, P::background);
            setColour (juce::PopupMenu::textColourId, P::phosphor);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, P::dim);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
            setColour (juce::TextButton::buttonColourId, P::panel);
            setColour (juce::TextButton::buttonOnColourId, P::dim);
            setColour (juce::TextButton::textColourOffId, P::phosphor);
            setColour (juce::TextButton::textColourOnId, P::bright);
            setColour (juce::TextEditor::backgroundColourId, P::background);
            setColour (juce::TextEditor::textColourId, P::phosphor);
            setColour (juce::TextEditor::highlightColourId, P::dim.withAlpha (0.5f));
            setColour (juce::TextEditor::outlineColourId, P::border);
            setColour (juce::TextEditor::focusedOutlineColourId, P::dim);
            setColour (juce::CaretComponent::caretColourId, P::phosphor);
            setColour (juce::TooltipWindow::backgroundColourId, P::background);
            setColour (juce::TooltipWindow::textColourId, P::phosphor);
            setColour (juce::TooltipWindow::outlineColourId, P::border);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const bool accent = slider.getProperties().contains ("accent");
            const bool bipolar = slider.getProperties().contains ("bipolar");
            const auto colour = accent ? Palette::red : Palette::phosphor;

            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (3.0f);
            const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float angle = startAngle + pos * (endAngle - startAngle);
            const float lineW = std::max (2.0f, radius * 0.1f);
            const float arcR = radius - lineW;

            const juce::PathStrokeType stroke (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (Palette::faint);
            g.strokePath (track, stroke);

            const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            if (std::abs (angle - from) > 0.001f)
            {
                juce::Path value;
                value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
                g.setColour (colour.withAlpha (0.16f));
                g.strokePath (value, juce::PathStrokeType (lineW * 3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (colour);
                g.strokePath (value, stroke);
            }

            const float bodyR = arcR - lineW * 1.7f;
            if (bodyR > 2.0f)
            {
                juce::ColourGradient grad (Palette::knobTop, centre.x, centre.y - bodyR,
                                           Palette::knobBottom, centre.x, centre.y + bodyR, false);
                g.setGradientFill (grad);
                g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
                g.setColour (Palette::border);
                g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

                juce::Path pointer;
                pointer.addRoundedRectangle (-lineW * 0.4f, -bodyR + 1.0f, lineW * 0.8f, bodyR * 0.6f, lineW * 0.3f);
                pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
                g.setColour (colour);
                g.fillPath (pointer);
            }
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
            g.setColour (Palette::panel);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (box.hasKeyboardFocus (true) ? Palette::dim : Palette::border);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            juce::Path arrow;
            const float ax = (float) width - 10.0f, ay = (float) height * 0.5f;
            arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 3.0f);
            g.setColour (Palette::phosphor);
            g.fillPath (arrow);
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (3, 1, box.getWidth() - 15, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
            label.setMinimumHorizontalScale (0.6f);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override
        {
            const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
            const bool latched = button.getClickingTogglesState() && button.getToggleState();
            auto fill = down ? Palette::dim.withAlpha (0.45f) : (highlighted ? Palette::faint.brighter (0.2f) : Palette::panel);
            if (latched)
                fill = Palette::dim.withAlpha (highlighted ? 0.45f : 0.32f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (latched ? Palette::phosphor.withAlpha (0.7f) : (highlighted ? Palette::dim : Palette::border));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }

        juce::Font getLabelFont (juce::Label&) override                       { return monoFont (11.0f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override                 { return monoFont (11.5f); }
        juce::Font getPopupMenuFont() override                                { return monoFont (13.0f); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override        { return monoFont (10.5f, true); }

        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
            label->setFont (monoFont (10.5f));
            label->setMinimumHorizontalScale (0.7f);
            label->setColour (juce::Label::textColourId, slider.getProperties().contains ("accent") ? Palette::red : Palette::phosphor);
            label->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
            label->setColour (juce::TextEditor::backgroundColourId, Palette::background);
            label->setColour (juce::TextEditor::textColourId, Palette::phosphor);
            return label;
        }
    };
}
