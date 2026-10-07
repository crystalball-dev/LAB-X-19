#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CrtLookAndFeel.h"

namespace labx19::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Rotary control with a caption above and its value below.
    class LabeledKnob final : public juce::Component
    {
    public:
        LabeledKnob (APVTS& state, const juce::String& paramID, const juce::String& caption, bool accent = false)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 14);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            if (accent)
                slider.getProperties().set ("accent", true);

            if (auto* param = state.getParameter (paramID))
            {
                const auto range = param->getNormalisableRange();
                if (range.start < 0.0f && range.end > 0.0f)
                    slider.getProperties().set ("bipolar", true);
                slider.setTooltip (param->getName (64));
            }

            label.setText (caption, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, accent ? Palette::red : Palette::dim);
            label.setInterceptsMouseClicks (false, false);
            label.setMinimumHorizontalScale (0.6f);

            addAndMakeVisible (label);
            addAndMakeVisible (slider);

            attachment = std::make_unique<APVTS::SliderAttachment> (state, paramID, slider);

            if (auto* param = state.getParameter (paramID))
                slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        }

        void resized() override
        {
            auto b = getLocalBounds();
            label.setBounds (b.removeFromTop (13));
            slider.setBounds (b);
        }

        juce::Slider slider;
        juce::Label label;

    private:
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    // Choice parameter as a combo box with a caption above.
    class LabeledChoice final : public juce::Component
    {
    public:
        LabeledChoice (APVTS& state, const juce::String& paramID, const juce::String& caption)
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
                box.addItemList (choice->choices, 1);
            if (auto* param = state.getParameter (paramID))
                box.setTooltip (param->getName (64));

            label.setText (caption, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centredLeft);
            label.setInterceptsMouseClicks (false, false);

            addAndMakeVisible (label);
            addAndMakeVisible (box);
            attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramID, box);
        }

        void resized() override
        {
            auto b = getLocalBounds();
            label.setBounds (b.removeFromTop (13));
            b.removeFromTop (2);
            box.setBounds (b.removeFromTop (22));
        }

        static constexpr int naturalHeight = 37;

        juce::ComboBox box;
        juce::Label label;

    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };

    // Up to two choices stacked in one column, so a panel with several switches stays narrow.
    class ChoiceStack final : public juce::Component
    {
    public:
        ChoiceStack (APVTS& state, const juce::String& idA, const juce::String& captionA,
                     const juce::String& idB = {}, const juce::String& captionB = {})
            : top (state, idA, captionA)
        {
            addAndMakeVisible (top);
            if (idB.isNotEmpty())
            {
                bottom = std::make_unique<LabeledChoice> (state, idB, captionB);
                addAndMakeVisible (*bottom);
            }
        }

        void resized() override
        {
            auto b = getLocalBounds().reduced (3, 0);
            const int h = LabeledChoice::naturalHeight;
            if (bottom == nullptr)
            {
                top.setBounds (b.withHeight (h).withY (b.getY() + 8));
                return;
            }
            const int spare = std::max (0, b.getHeight() - 2 * h);
            top.setBounds (b.removeFromTop (h + spare / 3).withHeight (h).translated (0, spare / 6));
            bottom->setBounds (b.withHeight (h).translated (0, spare / 6));
        }

    private:
        LabeledChoice top;
        std::unique_ptr<LabeledChoice> bottom;
    };

    // HORIZON: the sun comes up with DAWN; its glow follows the output level.
    class HorizonView final : public juce::Component
    {
    public:
        void setValues (float dawnValue, float levelDb)
        {
            dawn += (std::clamp (dawnValue, 0.0f, 1.0f) - dawn) * 0.25f;
            const float l = std::clamp ((levelDb + 48.0f) / 48.0f, 0.0f, 1.0f);
            level += (l - level) * (l > level ? 0.4f : 0.08f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (Palette::background);
            g.fillRect (b);

            const float horizonY = b.getY() + b.getHeight() * 0.72f;
            const float radius = b.getHeight() * 0.42f;
            const float sunY = horizonY + radius * 0.9f - dawn * radius * 1.35f;
            const auto centre = juce::Point<float> (b.getCentreX(), sunY);

            g.saveState();
            g.reduceClipRegion (b.withBottom (horizonY).toNearestInt());

            // Glow and rays
            const float glow = 0.12f + 0.45f * level;
            juce::ColourGradient halo (Palette::red.withAlpha (glow), centre.x, centre.y,
                                       Palette::red.withAlpha (0.0f), centre.x + radius * 2.4f, centre.y, true);
            g.setGradientFill (halo);
            g.fillEllipse (juce::Rectangle<float> (radius * 4.8f, radius * 4.8f).withCentre (centre));

            g.setColour (Palette::red.withAlpha (0.25f + 0.5f * level));
            for (int i = 0; i < 9; ++i)
            {
                const float a = juce::MathConstants<float>::pi * (-0.5f + (float) i / 8.0f) - juce::MathConstants<float>::halfPi;
                const auto p1 = centre.getPointOnCircumference (radius * 1.25f, a + juce::MathConstants<float>::halfPi);
                const auto p2 = centre.getPointOnCircumference (radius * (1.6f + 0.5f * level), a + juce::MathConstants<float>::halfPi);
                g.drawLine ({ p1, p2 }, 1.4f);
            }

            g.setColour (Palette::red.interpolatedWith (Palette::bright, 0.3f * level));
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 2.0f);
            g.setColour (Palette::red.withAlpha (0.18f + 0.3f * dawn));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
            g.restoreState();

            // Horizon and the ground lines below it
            g.setColour (Palette::phosphor);
            g.drawHorizontalLine ((int) horizonY, b.getX(), b.getRight());
            g.setColour (Palette::faint.brighter (0.4f));
            for (int i = 1; i <= 3; ++i)
            {
                const float y = horizonY + (b.getBottom() - horizonY) * (float) (i * i) / 10.0f;
                g.drawHorizontalLine ((int) y, b.getX(), b.getRight());
            }

            g.setFont (monoFont (10.5f, true));
            g.setColour (dawn > 0.05f ? Palette::red : Palette::dim);
            g.drawText (utf8 ("РАССВЕТ ") + juce::String (juce::roundToInt (dawn * 100.0f)) + "%",
                        b.reduced (4.0f, 2.0f), juce::Justification::topRight);

            g.setColour (Palette::border);
            g.drawRect (b, 1.0f);
        }

    private:
        float dawn = 0.0f, level = 0.0f;
    };

    // COMPANY: what the note engine is doing: the chord shape, keys held and the arp's step.
    class CompanyView final : public juce::Component
    {
    public:
        void setValues (const juce::String& chordName, const juce::String& arpName, int keys, int step)
        {
            chord = chordName;
            arp = arpName;
            heldKeys = keys;
            arpStep = step;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (2.0f);
            g.setFont (monoFont (11.0f));
            auto line = [&g, &b] (const juce::String& caption, const juce::String& text, juce::Colour colour)
            {
                auto r = b.removeFromTop (16.0f);
                g.setColour (Palette::dim);
                g.drawText (caption, r.removeFromLeft (52.0f), juce::Justification::centredLeft);
                g.setColour (colour);
                g.drawText (text, r, juce::Justification::centredLeft);
            };
            line ("CHORD", chord, chord == "Off" ? Palette::dim : Palette::phosphor);
            line ("ARP", arp, arp == "Off" ? Palette::dim : Palette::phosphor);
            line ("KEYS", juce::String (heldKeys), heldKeys > 0 ? Palette::bright : Palette::dim);

            b.removeFromTop (4.0f);
            const int dots = 8;
            const float w = b.getWidth() / (float) dots;
            for (int i = 0; i < dots; ++i)
            {
                const auto r = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, 10.0f).reduced (3.0f, 1.0f);
                const bool lit = arpStep >= 0 && (arpStep % dots) == i;
                g.setColour (lit ? Palette::red : Palette::faint);
                g.fillRoundedRectangle (r, 2.0f);
            }
        }

    private:
        juce::String chord { "Off" }, arp { "Off" };
        int heldKeys = 0, arpStep = -1;
    };

    // Phosphor oscilloscope with a rising-edge trigger and a short persistence trail.
    class ScopeView final : public juce::Component
    {
    public:
        static constexpr int points = 512;

        void setSamples (const float* source, int numSource)
        {
            int start = 0;
            const int searchEnd = std::max (0, numSource - points * 2);
            for (int i = 1; i < searchEnd; ++i)
                if (source[i - 1] <= 0.0f && source[i] > 0.0f)
                {
                    start = i;
                    break;
                }

            history[2] = history[1];
            history[1] = history[0];
            for (int i = 0; i < points; ++i)
            {
                const int idx = start + i * 2;
                history[0][(size_t) i] = idx < numSource ? source[idx] : 0.0f;
            }
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setColour (Palette::background);
            g.fillRect (b);

            g.setColour (Palette::faint);
            for (int i = 1; i < 8; ++i)
                g.drawVerticalLine ((int) (b.getX() + b.getWidth() * (float) i / 8.0f), b.getY(), b.getBottom());
            for (int i = 1; i < 4; ++i)
                g.drawHorizontalLine ((int) (b.getY() + b.getHeight() * (float) i / 4.0f), b.getX(), b.getRight());

            const float alphas[3] = { 1.0f, 0.35f, 0.15f };
            for (int h = 2; h >= 0; --h)
            {
                juce::Path trace;
                for (int i = 0; i < points; ++i)
                {
                    const float x = b.getX() + b.getWidth() * (float) i / (float) (points - 1);
                    const float y = b.getCentreY() - std::clamp (history[(size_t) h][(size_t) i] * 1.6f, -1.0f, 1.0f) * b.getHeight() * 0.45f;
                    if (i == 0)
                        trace.startNewSubPath (x, y);
                    else
                        trace.lineTo (x, y);
                }
                if (h == 0)
                {
                    g.setColour (Palette::phosphor.withAlpha (0.18f));
                    g.strokePath (trace, juce::PathStrokeType (4.0f));
                }
                g.setColour (Palette::phosphor.withAlpha (alphas[h]));
                g.strokePath (trace, juce::PathStrokeType (1.3f));
            }

            g.setColour (Palette::border);
            g.drawRect (b, 1.0f);
        }

    private:
        std::array<std::array<float, points>, 3> history {};
    };

    // Scanlines and a vignette drawn over everything. Never takes mouse input.
    class ScanlineOverlay final : public juce::Component
    {
    public:
        ScanlineOverlay()
        {
            setInterceptsMouseClicks (false, false);
            pattern = juce::Image (juce::Image::ARGB, 4, 3, true);
            for (int x = 0; x < 4; ++x)
                pattern.setPixelAt (x, 2, juce::Colour (0x2a000000));
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            g.setFillType (juce::FillType (pattern, juce::AffineTransform()));
            g.fillRect (b);

            juce::ColourGradient vignette (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                           juce::Colour (0x66000000), b.getX(), b.getY(), true);
            g.setGradientFill (vignette);
            g.fillRect (b);
        }

    private:
        juce::Image pattern;
    };
}
