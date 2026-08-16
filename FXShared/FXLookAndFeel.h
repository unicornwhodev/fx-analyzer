#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "FXTokens.h"

namespace fx
{

class FXLookAndFeel : public juce::LookAndFeel_V4
{
public:
    explicit FXLookAndFeel(juce::Colour accentCol) : accentColour(accentCol)
    {
        setColour(juce::Slider::textBoxTextColourId, col::textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::rotarySliderFillColourId, accentCol);
        setColour(juce::Slider::rotarySliderOutlineColourId, col::border);
        setColour(juce::ComboBox::backgroundColourId, col::surfPrimary.brighter(0.05f));
        setColour(juce::ComboBox::textColourId, col::textPrimary);
        setColour(juce::ComboBox::outlineColourId, col::border);
        setColour(juce::ComboBox::arrowColourId, accentCol);
        setColour(juce::PopupMenu::backgroundColourId, col::surfSecondary);
        setColour(juce::PopupMenu::textColourId, col::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentCol.withAlpha(0.25f));
        setColour(juce::PopupMenu::highlightedTextColourId, col::textPrimary);
    }

    juce::Colour getAccent() const { return accentColour; }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider&) override
    {
        auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(6.0f);
        auto radius = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
        auto cx = r.getCentreX();
        auto cy = r.getCentreY();
        auto toAngle = startAngle + sliderPos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour(col::surfTertiary);
        g.strokePath(track, juce::PathStrokeType((float)dim::knobRing, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (sliderPos > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, toAngle, true);
            g.setColour(accentColour);
            g.strokePath(arc, juce::PathStrokeType((float)dim::knobRing, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float bodyR = radius * 0.68f;
        g.setColour(col::surfTertiary);
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(col::border);
        g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.25f), cx, cy - bodyR,
                                     juce::Colours::transparentBlack, cx, cy + bodyR, false);
        g.setGradientFill(shadow);
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        auto a = toAngle - juce::MathConstants<float>::halfPi;
        g.setColour(col::textPrimary);
        g.drawLine(cx + std::cos(a) * bodyR * 0.35f, cy + std::sin(a) * bodyR * 0.35f,
                   cx + std::cos(a) * bodyR * 0.85f, cy + std::sin(a) * bodyR * 0.85f, 2.0f);

        g.setColour(accentColour.withAlpha(0.6f));
        g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto b = juce::Rectangle<float>(0.0f, 0.0f, (float)w, (float)h);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(b, (float)dim::btnR);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(b.reduced(0.5f), (float)dim::btnR, 1.0f);

        juce::Path p;
        float cx = (float)w - 14.0f, cy = (float)h * 0.5f;
        p.addTriangle(cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(p);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn, const juce::Colour& bgCol, bool over, bool down) override
    {
        auto r = btn.getLocalBounds().toFloat().reduced(1.0f);
        auto fillCol = bgCol;
        if (btn.getToggleState()) fillCol = accentColour;
        else if (down) fillCol = fillCol.brighter(0.12f);
        else if (over) fillCol = fillCol.brighter(0.06f);

        g.setColour(fillCol);
        g.fillRoundedRectangle(r, (float)dim::btnR);
        g.setColour(col::border);
        g.drawRoundedRectangle(r, (float)dim::btnR, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        auto textCol = btn.getToggleState() ? col::bg : col::textPrimary;
        g.setColour(textCol);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(font::label)));
        g.drawText(btn.getButtonText(), btn.getLocalBounds(), juce::Justification::centred);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(label.getFont());
        g.drawText(label.getText(), label.getLocalBounds(), label.getJustificationType());
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
        auto trackY = bounds.getCentreY();
        const float trackH = 4.0f;

        g.setColour(col::surfTertiary);
        g.fillRoundedRectangle(bounds.getX(), trackY - trackH * 0.5f, bounds.getWidth(), trackH, 2.0f);

        float filledW = sliderPos - bounds.getX();
        if (filledW > 0.0f)
        {
            g.setColour(accentColour);
            g.fillRoundedRectangle(bounds.getX(), trackY - trackH * 0.5f, filledW, trackH, 2.0f);
        }

        g.setColour(col::textPrimary);
        g.fillEllipse(sliderPos - 6.0f, trackY - 6.0f, 12.0f, 12.0f);
        g.setColour(accentColour);
        g.fillEllipse(sliderPos - 4.0f, trackY - 4.0f, 8.0f, 8.0f);
    }

private:
    juce::Colour accentColour;
};

} // namespace fx
