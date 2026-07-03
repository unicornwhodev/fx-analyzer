#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueAnalyzerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    struct ViewUiConfig
    {
        const char* title;
        std::array<const char*, 6> paramIds;
        std::array<const char*, 6> labels;
    };

    explicit MusiqueAnalyzerEditor(MusiqueAnalyzerProcessor&);
    ~MusiqueAnalyzerEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;
    using ComboAttach = APVTS::ComboBoxAttachment;

    void timerCallback() override;
    void loadPresets();
    void refreshPresetBox();
    void normalisePreset(juce::var&) const;
    int getCurrentViewIndex() const;
    int getCurrentSourceIndex() const;
    int getCurrentChannelModeIndex() const;
    void rebuildViewUi(bool force = false);
    void bindViewKnobs(int viewIndex);
    void configureKnobDisplay(juce::Slider& slider, const juce::String& paramId);
    void updateContextButtons();
    void cycleChannelMode();
    void syncLegacySourceParameter();
    void storeCurrentABSlot();
    void recallABSlot(bool slotA);
    void paintVisualization(juce::Graphics&, juce::Rectangle<int>);
    void paintSpectrum(juce::Graphics&, juce::Rectangle<float>);
    void paintSpectrogram(juce::Graphics&, juce::Rectangle<float>);
    void paintOscilloscope(juce::Graphics&, juce::Rectangle<float>);
    void paintLoudness(juce::Graphics&, juce::Rectangle<float>);
    void paintStereo(juce::Graphics&, juce::Rectangle<float>);

    MusiqueAnalyzerProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::filter };

    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn { "Bypass" };
    juce::TextButton freezeBtn { "Freeze" };
    juce::TextButton actionBtn { "Hold" };
    juce::TextButton channelModeBtn { "SUM" };
    juce::TextButton statusBtn { "Status" };

    juce::TextButton prevBtn { "<" };
    juce::TextButton nextBtn { ">" };
    juce::TextButton saveBtn { "Save" };
    juce::TextButton abBtn { "A/B" };
    juce::ComboBox presetBox;
    juce::ComboBox viewBox;
    juce::ComboBox sourceBox;

    std::array<juce::Slider, 6> knobs;
    std::array<juce::Label, 6> knobLabels;

    fx::MeterComponent inMeter, outMeter;
    juce::Slider mixSlider;
    juce::Slider outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent activeLED;

    std::unique_ptr<SliderAttach> mixAtt;
    std::unique_ptr<SliderAttach> outAtt;
    std::unique_ptr<ComboAttach> viewAtt;
    std::unique_ptr<ComboAttach> sourceAtt;
    std::unique_ptr<ButtonAttach> bypassAtt;
    std::unique_ptr<ButtonAttach> freezeAtt;
    std::array<std::unique_ptr<SliderAttach>, 6> viewKnobAtts;

    std::shared_ptr<juce::Array<juce::var>> presets;
    juce::ValueTree abStateA, abStateB;
    bool showingA = true;
    bool syncingSource = false;
    int displayedView = -1;

    MusiqueAnalyzerProcessor::SpectrumArray spectrumPrimary {};
    MusiqueAnalyzerProcessor::SpectrumArray spectrumSecondary {};
    MusiqueAnalyzerProcessor::SpectrumArray spectrumPeaks {};
    MusiqueAnalyzerProcessor::SpectrogramArray spectrogramValues {};
    MusiqueAnalyzerProcessor::ScopeArray scopeLeft {};
    MusiqueAnalyzerProcessor::ScopeArray scopeRight {};
    AnalyzerMonitorSnapshot monitorSnapshot {};
    int spectrogramCursor = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueAnalyzerEditor)
};
