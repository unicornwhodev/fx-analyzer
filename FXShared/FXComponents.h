#pragma once
#include <JuceHeader.h>
#include "FXTokens.h"
#include "FXLookAndFeel.h"

namespace fx
{
class MeterComponent : public juce::Component, private juce::Timer
{
public:
    MeterComponent() { startTimerHz(30); }
    void setLevel(float left, float right) { targetL = juce::jlimit(0.0f, 1.0f, left); targetR = juce::jlimit(0.0f, 1.0f, right); }
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        float mw = b.getWidth() * 0.42f;
        drawMeterBar(g, b.removeFromLeft(mw), smoothL);
        b.removeFromLeft(b.getWidth() * 0.16f);
        drawMeterBar(g, b, smoothR);
    }
private:
    float targetL = 0.0f, targetR = 0.0f, smoothL = 0.0f, smoothR = 0.0f;
    void timerCallback() override
    {
        constexpr float decay = 0.85f;
        smoothL = smoothL < targetL ? targetL : smoothL * decay;
        smoothR = smoothR < targetR ? targetR : smoothR * decay;
        targetL *= 0.92f; targetR *= 0.92f; repaint();
    }
    void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> r, float level)
    {
        g.setColour(col::meterBg); g.fillRoundedRectangle(r, 2.0f);
        float h = r.getHeight() * level; auto active = r.removeFromBottom(h);
        g.setColour(level < 0.6f ? col::meterLow : (level < 0.85f ? col::meterMid : col::meterHigh));
        g.fillRoundedRectangle(active, 2.0f);
        if (level > 0.98f) { g.setColour(col::meterClip); g.fillRect(r.getX(), r.getY(), r.getWidth(), 3.0f); }
    }
};

class LEDComponent : public juce::Component
{
public:
    void setOn(bool on) { isOn = on; repaint(); }
    void setAccent(juce::Colour c) { accentCol = c; }
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced(1.0f); auto c = isOn ? accentCol : col::ledOff;
        g.setColour(c); g.fillEllipse(b);
        if (isOn) { g.setColour(c.withAlpha(0.35f)); g.fillEllipse(b.expanded(3.0f)); }
    }
private:
    bool isOn = false; juce::Colour accentCol { col::meterHigh };
};

namespace preset
{
    inline juce::File findFxFolder(const juce::String& fxName)
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (int i = 0; i < 12; ++i)
        {
            if (dir.getChildFile("Presets").isDirectory()) return dir;
            auto legacy = dir.getChildFile("FX").getChildFile(fxName);
            if (legacy.getChildFile("Presets").isDirectory()) return legacy;
            auto direct = dir.getChildFile(fxName);
            if (direct.getChildFile("Presets").isDirectory()) return direct;
            auto parent = dir.getParentDirectory();
            if (parent == dir) break;
            dir = parent;
        }
        return {};
    }

    inline juce::File userPresetFolder(const juce::String& fxName)
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Musique FX").getChildFile(fxName).getChildFile("Presets").getChildFile("User");
    }

    inline juce::Array<juce::var> loadPresetsFromBank(const juce::File& file)
    {
        juce::Array<juce::var> out;
        if (!file.existsAsFile()) return out;
        auto json = juce::JSON::parse(file.loadFileAsString());
        if (auto* obj = json.getDynamicObject())
            if (auto* arr = obj->getProperty("presets").getArray())
                for (const auto& p : *arr) out.add(p);
        return out;
    }

    inline juce::Array<juce::var> loadAllPresets(const juce::String& fxName)
    {
        juce::Array<juce::var> out;
        auto fxDir = findFxFolder(fxName);
        if (fxDir.isDirectory())
            out.addArray(loadPresetsFromBank(fxDir.getChildFile("Presets").getChildFile("factory_bank.json")));
        auto userDir = userPresetFolder(fxName);
        if (userDir.isDirectory())
            for (auto& f : userDir.findChildFiles(juce::File::findFiles, false, "*.json"))
            {
                auto item = juce::JSON::parse(f.loadFileAsString());
                if (auto* obj = item.getDynamicObject())
                {
                    if (obj->getProperty("name").toString().isEmpty()) obj->setProperty("name", f.getFileNameWithoutExtension());
                    out.add(item);
                }
            }
        return out;
    }

    inline bool saveUserPreset(const juce::String& fxName, const juce::String& presetName,
                               const juce::StringArray& paramIds, juce::AudioProcessorValueTreeState& apvts)
    {
        auto userDir = userPresetFolder(fxName);
        if (!userDir.createDirectory()) return false;
        juce::DynamicObject::Ptr obj = new juce::DynamicObject();
        obj->setProperty("name", presetName);
        for (auto& id : paramIds)
            if (auto* raw = apvts.getRawParameterValue(id)) obj->setProperty(id, raw->load());
        auto safe = presetName.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
        if (safe.isEmpty()) safe = "Preset";
        auto ts = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        return userDir.getChildFile(ts + "_" + safe + ".json").replaceWithText(juce::JSON::toString(juce::var(obj)));
    }

    inline void applyToAPVTS(juce::AudioProcessorValueTreeState& apvts, const juce::var& preset)
    {
        auto* obj = preset.getDynamicObject(); if (!obj) return;
        for (int i = 0; i < obj->getProperties().size(); ++i)
        {
            auto key = obj->getProperties().getName(i); if (key == juce::Identifier("name")) continue;
            if (auto* param = apvts.getParameter(key.toString()))
            {
                float v = (float)obj->getProperty(key); param->setValueNotifyingHost(param->convertTo0to1(v));
            }
        }
    }
}

namespace ui
{
inline void markUnsupportedControl(juce::Button& button) { button.setEnabled(false); button.setTooltip("Unavailable in this build"); button.setAlpha(0.45f); }
}

namespace paint
{
inline void header(juce::Graphics& g, int w, juce::Colour accent) { g.setColour(col::surfPrimary); g.fillRect(0, 0, w, dim::headerH); g.setColour(accent); g.fillRect(0, dim::headerH - 2, w, 2); }
inline void presetBar(juce::Graphics& g, int w) { g.setColour(col::surfPrimary.brighter(0.04f)); g.fillRect(0, dim::headerH, w, dim::presetBarH); g.setColour(col::divider); g.drawHorizontalLine(dim::headerH + dim::presetBarH - 1, 0.0f, (float)w); }
inline void graphArea(juce::Graphics& g, int w) { const int top = dim::headerH + dim::presetBarH; g.setColour(col::graphBg); g.fillRect(0, top, w, dim::visualH); }
inline void graphGrid(juce::Graphics& g, int w, int cols = 32, int rows = 32)
{
    const int top = dim::headerH + dim::presetBarH, bot = top + dim::visualH; constexpr float pad = 16.0f;
    g.setColour(col::gridMinor);
    for (float x = pad; x < (float)w - pad; x += (float)cols) g.drawVerticalLine((int)x, (float)top + 8.0f, (float)bot - 8.0f);
    for (float y = (float)top + 8.0f; y < (float)bot - 8.0f; y += (float)rows) g.drawHorizontalLine((int)y, pad, (float)w - pad);
}
inline void controls(juce::Graphics& g, int w, int numKnobs = 6)
{
    const int top = dim::headerH + dim::presetBarH + dim::visualH;
    juce::ColourGradient grad(col::surfPrimary.brighter(0.03f), 0, (float)top, col::surfPrimary.darker(0.02f), 0, (float)(top + dim::controlsH), false);
    g.setGradientFill(grad); g.fillRect(0, top, w, dim::controlsH); g.setColour(col::divider); g.drawHorizontalLine(top, 0.0f, (float)w);
    const int kW = w / numKnobs, totalW = numKnobs * kW, startX = (w - totalW) / 2;
    juce::Rectangle<float> panel((float)startX + 10.0f, (float)top + 10.0f, (float)totalW - 20.0f, (float)dim::controlsH - 20.0f);
    g.setColour(col::surfSecondary.withAlpha(0.5f)); g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(col::border.withAlpha(0.5f)); g.drawRoundedRectangle(panel, 8.0f, 1.0f);
    g.setColour(col::divider.withAlpha(0.3f));
    for (int i = 1; i < numKnobs; ++i) { float x = (float)startX + i * kW; g.drawVerticalLine((int)x, (float)top + 20.0f, (float)(top + dim::controlsH - 20.0f)); }
}
inline void footer(juce::Graphics& g, int w) { const int top = dim::appH - dim::footerH; g.setColour(col::surfPrimary.darker(0.05f)); g.fillRect(0, top, w, dim::footerH); g.setColour(col::divider); g.drawHorizontalLine(top, 0.0f, (float)w); }
inline void footerLabel(juce::Graphics& g, const juce::String& text, int sliderX, int sliderW) { const int top = dim::appH - dim::footerH; g.setColour(col::textMuted); g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold"))); g.drawText(text, sliderX, top + 2, sliderW, 12, juce::Justification::centred); }
inline void outline(juce::Graphics& g, juce::Rectangle<int> bounds) { g.setColour(col::border); g.drawRect(bounds, 1); }
}
}
