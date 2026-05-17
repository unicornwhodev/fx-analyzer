#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueAnalyzerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MusiqueAnalyzerEditor(MusiqueAnalyzerProcessor&);
    ~MusiqueAnalyzerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;

    void timerCallback() override;
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);

    MusiqueAnalyzerProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::filter };

    // Header
    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn{"Bypass"}, freezeBtn{"Freeze"}, sourceBtn{"SOURCE OUT"}, settingsBtn{juce::CharPointer_UTF8("\xe2\x9a\x99")};

    // Preset bar
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, saveBtn{"Save"}, abBtn{"A/B"};
    juce::ComboBox presetBox;

    // 6 knobs: Smoothing, Tilt, Range, Offset, Resolution, Speed
    juce::Slider knobs[6];
    juce::Label knobLabels[6];

    // Footer
    fx::MeterComponent inMeter, outMeter;
    juce::Slider mixSlider, outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent activeLED;

    // Visualization state
    float phase = 0.0f;
    std::array<float, MusiqueAnalyzerProcessor::spectrumBinCount> spectrumValues {};

    // Attachments
    std::unique_ptr<SliderAttach> smoothAtt, tiltAtt, rangeAtt, offsetAtt, resAtt, speedAtt, mixAtt, outAtt;
    std::unique_ptr<ButtonAttach> bypassAtt, freezeAtt, sourceAtt;

    std::shared_ptr<juce::Array<juce::var>> presets;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueAnalyzerEditor)
};
