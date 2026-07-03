#include "PluginEditor.h"
#include "BinaryData.h"

#include <cmath>

namespace
{
const std::array<MusiqueAnalyzerEditor::ViewUiConfig, MusiqueAnalyzerProcessor::numViews> kViewConfigs {{
    {
        "SPECTRUM",
        { "smoothing", "tilt", "range", "offset", "resolution", "speed" },
        { "SMOOTH", "TILT", "RANGE", "OFFSET", "DETAIL", "SPEED" }
    },
    {
        "SPECTROGRAM",
        { "range", "offset", "spec_contrast", "spec_floor", "spec_scroll", "resolution" },
        { "RANGE", "OFFSET", "CONTRAST", "FLOOR", "SCROLL", "DETAIL" }
    },
    {
        "OSCILLOSCOPE",
        { "scope_window", "scope_zoom", "scope_trigger", "scope_persist", "offset", "speed" },
        { "WINDOW", "ZOOM", "TRIGGER", "PERSIST", "OFFSET", "SPEED" }
    },
    {
        "LOUDNESS",
        { "range", "offset", "loud_history", "loud_hold", "loud_gate", "loud_scale" },
        { "RANGE", "OFFSET", "HISTORY", "HOLD", "GATE", "SCALE" }
    },
    {
        "STEREO",
        { "stereo_zoom", "stereo_decay", "stereo_focus", "stereo_hold", "stereo_balance", "speed" },
        { "ZOOM", "DECAY", "FOCUS", "HOLD", "BALANCE", "SPEED" }
    }
}};

float getParamValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float fallback = 0.0f)
{
    if (auto* param = apvts.getRawParameterValue(id))
        return param->load();
    return fallback;
}

int getChoiceValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int fallback = 0)
{
    return (int) std::round(getParamValue(apvts, id, (float) fallback));
}

void setParameter(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

void setupButton(juce::TextButton& button, bool toggle = false)
{
    button.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
    button.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
    if (toggle)
        button.setClickingTogglesState(true);
}

void setupKnob(juce::Slider& slider, juce::Label& label, const char* text)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::label).withStyle("Bold")));
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, fx::col::textMuted);
}

juce::String sourceName(int index)
{
    return index == MusiqueAnalyzerProcessor::sourceInput ? "INPUT" : "OUTPUT";
}

juce::String channelModeName(int index)
{
    switch (index)
    {
        case MusiqueAnalyzerProcessor::stereoOverlayMode: return "STEREO";
        case MusiqueAnalyzerProcessor::midMode:           return "MID";
        case MusiqueAnalyzerProcessor::sideMode:          return "SIDE";
        default:                                          return "SUM";
    }
}

juce::String triggerName(int index)
{
    switch (index)
    {
        case 1:  return "RISE";
        case 2:  return "FREE";
        default: return "AUTO";
    }
}

juce::String paletteName(int index)
{
    switch (index)
    {
        case 1:  return "ICE";
        case 2:  return "MONO";
        default: return "AMBER";
    }
}

juce::String loudScaleName(int index)
{
    switch (index)
    {
        case 1:  return "EBU";
        case 2:  return "WIDE";
        default: return "TIGHT";
    }
}

void drawBadge(juce::Graphics& g, juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
{
    g.setColour(colour.withAlpha(0.16f));
    g.fillRoundedRectangle(rect, 8.0f);
    g.setColour(colour.withAlpha(0.58f));
    g.drawRoundedRectangle(rect, 8.0f, 1.0f);
    g.setColour(fx::col::textPrimary);
    g.setFont(juce::Font(juce::FontOptions {}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
}

float mapDbToY(float valueDb, float topDb, float rangeDb, juce::Rectangle<float> area)
{
    const float bottomDb = topDb - rangeDb;
    const float norm = juce::jlimit(0.0f, 1.0f, 1.0f - ((valueDb - bottomDb) / juce::jmax(1.0f, rangeDb)));
    return area.getY() + norm * area.getHeight();
}

float mapLogFrequencyToX(float frequencyHz, juce::Rectangle<float> area)
{
    const float norm = std::log10(juce::jlimit(20.0f, 20000.0f, frequencyHz) / 20.0f) / std::log10(1000.0f);
    return area.getX() + juce::jlimit(0.0f, 1.0f, norm) * area.getWidth();
}

juce::Colour spectrogramColour(float normalised, int palette)
{
    normalised = juce::jlimit(0.0f, 1.0f, normalised);
    switch (palette)
    {
        case 1:
            return juce::Colour::fromHSV(0.58f - normalised * 0.12f,
                                         0.62f + normalised * 0.28f,
                                         0.10f + normalised * 0.90f,
                                         1.0f);
        case 2:
            return juce::Colour::fromFloatRGBA(normalised, normalised, normalised, 1.0f);
        default:
            return juce::Colour::fromHSV(0.11f - normalised * 0.07f,
                                         0.88f,
                                         0.06f + normalised * 0.94f,
                                         1.0f);
    }
}
}

MusiqueAnalyzerEditor::MusiqueAnalyzerEditor(MusiqueAnalyzerProcessor& processor)
    : AudioProcessorEditor(&processor), proc(processor)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    titleLabel.setText("ANALYZER", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    for (auto* button : { &bypassBtn, &freezeBtn, &actionBtn, &channelModeBtn, &statusBtn, &prevBtn, &nextBtn, &saveBtn, &abBtn })
    {
        setupButton(*button, button == &bypassBtn || button == &freezeBtn);
        addAndMakeVisible(*button);
    }

    statusBtn.setInterceptsMouseClicks(false, false);
    bypassBtn.setTooltip("Native host bypass. The dry signal stays unchanged.");
    freezeBtn.setTooltip("Freeze all analyzer views without touching the audio path.");
    actionBtn.setTooltip("Contextual hold or reset action for the active view.");
    channelModeBtn.setTooltip("Cycle SUM, STEREO, MID and SIDE for the views that support it.");
    statusBtn.setTooltip("Live view status driven by the real analyzer state.");

    addAndMakeVisible(presetBox);
    addAndMakeVisible(viewBox);
    addAndMakeVisible(sourceBox);
    viewBox.addItemList(juce::StringArray { "Spectrum", "Spectrogram", "Oscilloscope", "Loudness", "Stereo" }, 1);
    sourceBox.addItemList(juce::StringArray { "Input", "Output" }, 1);

    for (int index = 0; index < 6; ++index)
    {
        setupKnob(knobs[(size_t) index], knobLabels[(size_t) index], "");
        addAndMakeVisible(knobs[(size_t) index]);
        addAndMakeVisible(knobLabels[(size_t) index]);
    }

    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(mixSlider);
    addAndMakeVisible(outputSlider);

    activeLED.setAccent(fx::accent::filter);
    addAndMakeVisible(activeLED);

    versionLabel.setText("Musique Analyzer v1.1", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions {}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    mixAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix", mixSlider);
    outAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "output", outputSlider);
    viewAtt = std::make_unique<ComboAttach>(proc.getAPVTS(), "view", viewBox);
    sourceAtt = std::make_unique<ComboAttach>(proc.getAPVTS(), "source", sourceBox);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    freezeAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "freeze", freezeBtn);

    loadPresets();
    monitorSnapshot = proc.getMonitorSnapshot();
    abStateA = proc.getAPVTS().copyState();
    abStateB = abStateA.createCopy();
    showingA = true;
    abBtn.setButtonText("A");

    presetBox.onChange = [this]
    {
        if (presets == nullptr)
            return;

        const int presetIndex = presetBox.getSelectedItemIndex();
        if (presetIndex <= 0)
            return;

        const int arrayIndex = presetIndex - 1;
        if (arrayIndex >= presets->size())
            return;

        auto preset = presets->getReference(arrayIndex);
        normalisePreset(preset);
        fx::preset::applyToAPVTS(proc.getAPVTS(), preset);
        syncLegacySourceParameter();
        proc.postExternalStateChange();
        abStateA = proc.getAPVTS().copyState();
        abStateB = abStateA.createCopy();
        showingA = true;
        abBtn.setButtonText("A");
        rebuildViewUi(true);
    };

    prevBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index > 0)
            presetBox.setSelectedItemIndex(index - 1);
    };

    nextBtn.onClick = [this]
    {
        const int index = presetBox.getSelectedItemIndex();
        if (index < presetBox.getNumItems() - 1)
            presetBox.setSelectedItemIndex(index + 1);
    };

    saveBtn.onClick = [this]
    {
        const auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        if (fx::preset::saveUserPreset("fx-analyzer", name, MusiqueAnalyzerProcessor::getAllParameterIds(), proc.getAPVTS()))
        {
            loadPresets();
            if (presetBox.getNumItems() > 1)
                presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    abBtn.onClick = [this]
    {
        storeCurrentABSlot();
        recallABSlot(!showingA);
    };

    viewBox.onChange = [this] { rebuildViewUi(true); };
    sourceBox.onChange = [this]
    {
        syncLegacySourceParameter();
        rebuildViewUi(true);
    };
    channelModeBtn.onClick = [this] { cycleChannelMode(); };
    actionBtn.onClick = [this]
    {
        const int viewIndex = getCurrentViewIndex();
        if (viewIndex == MusiqueAnalyzerProcessor::loudnessView)
        {
            proc.resetLoudnessHistory();
            return;
        }

        const bool hold = getParamValue(proc.getAPVTS(), "hold") > 0.5f;
        setParameter(proc.getAPVTS(), "hold", hold ? 0.0f : 1.0f);
    };

    rebuildViewUi(true);
    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueAnalyzerEditor::~MusiqueAnalyzerEditor()
{
    setLookAndFeel(nullptr);
}

void MusiqueAnalyzerEditor::loadPresets()
{
    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-analyzer"));
    for (auto& preset : *presets)
        normalisePreset(preset);
    refreshPresetBox();
}

void MusiqueAnalyzerEditor::refreshPresetBox()
{
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem("Current State", 1);

    if (presets != nullptr)
    {
        int itemId = 2;
        for (auto& preset : *presets)
        {
            if (auto* object = preset.getDynamicObject())
                presetBox.addItem(object->getProperty("name").toString(), itemId++);
        }
    }

    presetBox.setSelectedId(1, juce::dontSendNotification);
}

void MusiqueAnalyzerEditor::normalisePreset(juce::var& preset) const
{
    MusiqueAnalyzerProcessor::normalisePresetObject(preset);
}

int MusiqueAnalyzerEditor::getCurrentViewIndex() const
{
    return juce::jlimit(0, MusiqueAnalyzerProcessor::numViews - 1, getChoiceValue(proc.getAPVTS(), "view", 0));
}

int MusiqueAnalyzerEditor::getCurrentSourceIndex() const
{
    return juce::jlimit(0, 1, getChoiceValue(proc.getAPVTS(), "source", 1));
}

int MusiqueAnalyzerEditor::getCurrentChannelModeIndex() const
{
    return juce::jlimit(0, MusiqueAnalyzerProcessor::numChannelModes - 1, getChoiceValue(proc.getAPVTS(), "channel_mode", 0));
}

void MusiqueAnalyzerEditor::rebuildViewUi(bool force)
{
    const int viewIndex = getCurrentViewIndex();
    if (!force && viewIndex == displayedView)
    {
        updateContextButtons();
        return;
    }

    displayedView = viewIndex;
    const auto& config = kViewConfigs[(size_t) viewIndex];
    for (int index = 0; index < 6; ++index)
        knobLabels[(size_t) index].setText(config.labels[(size_t) index], juce::dontSendNotification);

    bindViewKnobs(viewIndex);
    updateContextButtons();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueAnalyzerEditor::bindViewKnobs(int viewIndex)
{
    const auto& config = kViewConfigs[(size_t) viewIndex];
    for (int index = 0; index < 6; ++index)
    {
        configureKnobDisplay(knobs[(size_t) index], config.paramIds[(size_t) index]);
        viewKnobAtts[(size_t) index] = std::make_unique<SliderAttach>(proc.getAPVTS(), config.paramIds[(size_t) index], knobs[(size_t) index]);
    }
}

void MusiqueAnalyzerEditor::configureKnobDisplay(juce::Slider& slider, const juce::String& paramId)
{
    auto percent = [](double value) { return juce::String((int) std::round(value)) + "%"; };
    auto signedDb = [](double value)
    {
        return juce::String(value >= 0.0 ? "+" : "") + juce::String(value, 1) + " dB";
    };

    if (paramId == "smoothing")
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
    }
    else if (paramId == "tilt")
    {
        slider.textFromValueFunction = [](double value)
        {
            return juce::String((value - 3.0) >= 0.0 ? "+" : "") + juce::String(value - 3.0, 1);
        };
    }
    else if (paramId == "range")
    {
        slider.textFromValueFunction = [](double value) { return juce::String((int) std::round(value)) + " dB"; };
    }
    else if (paramId == "offset" || paramId == "spec_floor" || paramId == "loud_gate")
    {
        slider.textFromValueFunction = signedDb;
    }
    else if (paramId == "resolution")
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + "x"; };
    }
    else if (paramId == "speed")
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + "x"; };
    }
    else if (paramId == "spec_contrast" || paramId == "scope_persist" || paramId == "loud_hold"
             || paramId == "stereo_decay" || paramId == "stereo_focus" || paramId == "stereo_hold")
    {
        slider.textFromValueFunction = percent;
    }
    else if (paramId == "spec_scroll")
    {
        slider.textFromValueFunction = [](double value) { return juce::String((int) std::round(value)) + "x"; };
    }
    else if (paramId == "scope_window")
    {
        slider.textFromValueFunction = [](double value) { return juce::String((int) std::round(value)) + " ms"; };
    }
    else if (paramId == "scope_zoom" || paramId == "stereo_zoom")
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + "x"; };
    }
    else if (paramId == "scope_trigger")
    {
        slider.textFromValueFunction = [](double value) { return triggerName((int) std::round(value)); };
    }
    else if (paramId == "loud_history")
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + " s"; };
    }
    else if (paramId == "loud_scale")
    {
        slider.textFromValueFunction = [](double value) { return loudScaleName((int) std::round(value)); };
    }
    else if (paramId == "stereo_balance")
    {
        slider.textFromValueFunction = [](double value)
        {
            return juce::String(value >= 0.0 ? "+" : "") + juce::String((int) std::round(value));
        };
    }
    else
    {
        slider.textFromValueFunction = [](double value) { return juce::String(value, 1); };
    }
}

void MusiqueAnalyzerEditor::updateContextButtons()
{
    const auto& apvts = proc.getAPVTS();
    const int viewIndex = getCurrentViewIndex();
    const bool hold = getParamValue(apvts, "hold") > 0.5f;
    const bool channelModeRelevant = viewIndex == MusiqueAnalyzerProcessor::spectrumView
        || viewIndex == MusiqueAnalyzerProcessor::spectrogramView
        || viewIndex == MusiqueAnalyzerProcessor::oscilloscopeView;

    channelModeBtn.setEnabled(channelModeRelevant);
    channelModeBtn.setAlpha(channelModeRelevant ? 1.0f : 0.55f);
    channelModeBtn.setButtonText(channelModeRelevant ? channelModeName(getCurrentChannelModeIndex()) : "AUTO");
    channelModeBtn.setColour(juce::TextButton::buttonColourId,
        channelModeRelevant ? fx::accent::filter.withAlpha(0.16f) : fx::col::surfSecondary);
    channelModeBtn.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);

    if (viewIndex == MusiqueAnalyzerProcessor::loudnessView)
    {
        actionBtn.setButtonText("RESET");
        actionBtn.setColour(juce::TextButton::buttonColourId, fx::accent::filter.withAlpha(0.18f));
        actionBtn.setTooltip("Reset momentary, short-term, integrated and true-peak history.");
    }
    else
    {
        actionBtn.setButtonText(hold ? "HOLD ON" : "HOLD OFF");
        actionBtn.setColour(juce::TextButton::buttonColourId,
            hold ? fx::accent::filter.withAlpha(0.24f) : fx::col::surfSecondary);
        actionBtn.setTooltip("Hold the active analyzer view while audio continues to pass through.");
    }

    juce::String statusText;
    switch (viewIndex)
    {
        case MusiqueAnalyzerProcessor::spectrumView:
            statusText = sourceName(getCurrentSourceIndex()) + "  " + channelModeName(getCurrentChannelModeIndex());
            break;
        case MusiqueAnalyzerProcessor::spectrogramView:
            statusText = paletteName(getChoiceValue(apvts, "spec_palette")) + "  "
                + juce::String((int) std::round(getParamValue(apvts, "spec_scroll", 1.0f))) + "X";
            break;
        case MusiqueAnalyzerProcessor::oscilloscopeView:
            statusText = triggerName(getChoiceValue(apvts, "scope_trigger")) + "  "
                + juce::String((int) std::round(getParamValue(apvts, "scope_window", 220.0f))) + "MS";
            break;
        case MusiqueAnalyzerProcessor::loudnessView:
            statusText = "M " + juce::String(monitorSnapshot.loudness.momentaryLufs, 1)
                + "  TP " + juce::String(monitorSnapshot.loudness.truePeakDb, 1);
            break;
        case MusiqueAnalyzerProcessor::stereoView:
            statusText = "CORR " + juce::String(monitorSnapshot.stereoField.correlation, 2)
                + "  W " + juce::String(monitorSnapshot.stereoField.width * 100.0f, 0) + "%";
            break;
        default:
            statusText = "LIVE";
            break;
    }

    if (monitorSnapshot.frozen)
        statusText += "  FROZEN";
    else if (monitorSnapshot.hold && viewIndex != MusiqueAnalyzerProcessor::loudnessView)
        statusText += "  HOLD";

    statusBtn.setButtonText(statusText);
    statusBtn.setColour(juce::TextButton::buttonColourId, fx::accent::filter.withAlpha(0.14f));
    statusBtn.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
}

void MusiqueAnalyzerEditor::cycleChannelMode()
{
    const int viewIndex = getCurrentViewIndex();
    if (viewIndex != MusiqueAnalyzerProcessor::spectrumView
        && viewIndex != MusiqueAnalyzerProcessor::spectrogramView
        && viewIndex != MusiqueAnalyzerProcessor::oscilloscopeView)
        return;

    const int nextMode = (getCurrentChannelModeIndex() + 1) % MusiqueAnalyzerProcessor::numChannelModes;
    setParameter(proc.getAPVTS(), "channel_mode", (float) nextMode);
    rebuildViewUi(true);
}

void MusiqueAnalyzerEditor::syncLegacySourceParameter()
{
    if (syncingSource)
        return;

    const juce::ScopedValueSetter<bool> guard(syncingSource, true);
    setParameter(proc.getAPVTS(), "analysis_output",
                 getCurrentSourceIndex() == MusiqueAnalyzerProcessor::sourceOutput ? 1.0f : 0.0f);
}

void MusiqueAnalyzerEditor::storeCurrentABSlot()
{
    const auto currentState = proc.getAPVTS().copyState();
    if (!abStateA.isValid())
    {
        abStateA = currentState;
        abStateB = currentState.createCopy();
        showingA = true;
        return;
    }

    if (showingA)
        abStateA = currentState;
    else
        abStateB = currentState;
}

void MusiqueAnalyzerEditor::recallABSlot(bool slotA)
{
    const auto state = slotA ? abStateA : abStateB;
    if (!state.isValid())
        return;

    proc.getAPVTS().replaceState(state.createCopy());
    syncLegacySourceParameter();
    proc.postExternalStateChange();
    showingA = slotA;
    abBtn.setButtonText(showingA ? "A" : "B");
    rebuildViewUi(true);
}

void MusiqueAnalyzerEditor::timerCallback()
{
    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);

    proc.getSpectrumSnapshot(spectrumPrimary, &spectrumSecondary, &spectrumPeaks);
    proc.getSpectrogramSnapshot(spectrogramValues, spectrogramCursor);
    proc.getScopeSnapshot(scopeLeft, scopeRight);
    monitorSnapshot = proc.getMonitorSnapshot();

    const float trackedLevel = getCurrentSourceIndex() == MusiqueAnalyzerProcessor::sourceInput
        ? juce::jmax(inputLevels.left, inputLevels.right)
        : juce::jmax(outputLevels.left, outputLevels.right);
    activeLED.setOn(!monitorSnapshot.frozen && trackedLevel > 0.02f);

    rebuildViewUi();
    updateContextButtons();
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueAnalyzerEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto area = bounds.toFloat().reduced(18.0f, 18.0f);
    switch (getCurrentViewIndex())
    {
        case MusiqueAnalyzerProcessor::spectrogramView:
            paintSpectrogram(g, area);
            break;
        case MusiqueAnalyzerProcessor::oscilloscopeView:
            paintOscilloscope(g, area);
            break;
        case MusiqueAnalyzerProcessor::loudnessView:
            paintLoudness(g, area);
            break;
        case MusiqueAnalyzerProcessor::stereoView:
            paintStereo(g, area);
            break;
        case MusiqueAnalyzerProcessor::spectrumView:
        default:
            paintSpectrum(g, area);
            break;
    }
}

void MusiqueAnalyzerEditor::paintSpectrum(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& apvts = proc.getAPVTS();
    const float rangeDb = getParamValue(apvts, "range", 60.0f);
    const float topDb = getParamValue(apvts, "offset", 0.0f);
    const int channelMode = getCurrentChannelModeIndex();
    const bool overlay = channelMode != MusiqueAnalyzerProcessor::sumMode;

    const auto graph = area.reduced(18.0f, 26.0f);
    g.setColour(fx::col::gridMinor);
    for (int dB = 0; dB <= (int) rangeDb; dB += 12)
    {
        const float lineValue = topDb - (float) dB;
        const float y = mapDbToY(lineValue, topDb, rangeDb, graph);
        g.drawHorizontalLine((int) y, graph.getX(), graph.getRight());
        g.setColour(fx::col::textMuted);
        g.setFont(juce::Font(juce::FontOptions {}.withHeight(9.0f)));
        g.drawText(juce::String((int) std::round(lineValue)), (int) area.getX(), (int) y - 6, 30, 12, juce::Justification::centredRight);
        g.setColour(fx::col::gridMinor);
    }

    const std::array<float, 10> frequencies { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    const std::array<const char*, 10> labels { "20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k" };
    for (size_t index = 0; index < frequencies.size(); ++index)
    {
        const float x = mapLogFrequencyToX(frequencies[index], graph);
        g.setColour(fx::col::gridMinor);
        g.drawVerticalLine((int) x, graph.getY(), graph.getBottom());
        g.setColour(fx::col::textMuted);
        g.drawText(labels[index], (int) x - 14, (int) graph.getBottom() + 6, 28, 12, juce::Justification::centred);
    }

    auto buildPath = [&](const auto& values, juce::Path& path)
    {
        for (size_t index = 0; index < values.size(); ++index)
        {
            const float norm = values.size() > 1 ? (float) index / (float) (values.size() - 1) : 0.0f;
            const float x = graph.getX() + norm * graph.getWidth();
            const float y = mapDbToY(values[index], topDb, rangeDb, graph);
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
    };

    juce::Path primaryPath;
    buildPath(spectrumPrimary, primaryPath);

    juce::Path fillPath(primaryPath);
    fillPath.lineTo(graph.getRight(), graph.getBottom());
    fillPath.lineTo(graph.getX(), graph.getBottom());
    fillPath.closeSubPath();
    juce::ColourGradient fillGradient(fx::accent::filter.withAlpha(0.34f), graph.getX(), graph.getY(),
                                      fx::accent::filter.withAlpha(0.0f), graph.getX(), graph.getBottom(), false);
    g.setGradientFill(fillGradient);
    g.fillPath(fillPath);

    if (overlay)
    {
        juce::Path secondaryPath;
        buildPath(spectrumSecondary, secondaryPath);
        g.setColour(fx::col::textSecondary.withAlpha(0.62f));
        g.strokePath(secondaryPath, juce::PathStrokeType(1.4f));
    }

    juce::Path peaksPath;
    buildPath(spectrumPeaks, peaksPath);
    g.setColour(fx::accent::filter.withAlpha(0.24f));
    g.strokePath(peaksPath, juce::PathStrokeType(5.0f));
    g.setColour(fx::accent::filter.withAlpha(0.92f));
    g.strokePath(primaryPath, juce::PathStrokeType(2.2f));

    drawBadge(g, { area.getRight() - 346.0f, area.getY() + 8.0f, 114.0f, 22.0f }, sourceName(getCurrentSourceIndex()), fx::col::textSecondary);
    drawBadge(g, { area.getRight() - 224.0f, area.getY() + 8.0f, 96.0f, 22.0f }, channelModeName(channelMode), fx::accent::filter);
    drawBadge(g, { area.getRight() - 120.0f, area.getY() + 8.0f, 96.0f, 22.0f },
              monitorSnapshot.hold ? "PEAK HOLD" : "LIVE", monitorSnapshot.hold ? fx::col::meterMid : fx::col::textSecondary);
}

void MusiqueAnalyzerEditor::paintSpectrogram(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& apvts = proc.getAPVTS();
    const float rangeDb = getParamValue(apvts, "range", 60.0f);
    const float topDb = getParamValue(apvts, "offset", 0.0f);
    const float floorDb = getParamValue(apvts, "spec_floor", -84.0f);
    const float contrast = getParamValue(apvts, "spec_contrast", 55.0f);
    const int palette = getChoiceValue(apvts, "spec_palette", 0);
    const auto graph = area.reduced(18.0f, 24.0f);

    const float minDb = juce::jmin(floorDb, topDb - rangeDb);
    const float maxDb = juce::jmax(topDb, minDb + 12.0f);
    const float gamma = juce::jmap(contrast / 100.0f, 1.9f, 0.55f);
    const float columnWidth = graph.getWidth() / (float) MusiqueAnalyzerProcessor::spectrogramColumnCount;
    const float rowHeight = graph.getHeight() / (float) MusiqueAnalyzerProcessor::spectrumBinCount;

    for (size_t row = 0; row < MusiqueAnalyzerProcessor::spectrumBinCount; ++row)
    {
        const float y = graph.getBottom() - (float) (row + 1) * rowHeight;
        for (size_t column = 0; column < MusiqueAnalyzerProcessor::spectrogramColumnCount; ++column)
        {
            const int sourceColumn = (spectrogramCursor + 1 + (int) column) % (int) MusiqueAnalyzerProcessor::spectrogramColumnCount;
            const float valueDb = spectrogramValues[row * MusiqueAnalyzerProcessor::spectrogramColumnCount + (size_t) sourceColumn];
            const float normalised = juce::jlimit(0.0f, 1.0f, (valueDb - minDb) / juce::jmax(1.0f, maxDb - minDb));
            const float shaped = std::pow(normalised, gamma);
            g.setColour(spectrogramColour(shaped, palette));
            g.fillRect(graph.getX() + (float) column * columnWidth, y, columnWidth + 0.75f, rowHeight + 0.75f);
        }
    }

    g.setColour(fx::col::border.withAlpha(0.55f));
    g.drawRect(graph, 1.0f);
    drawBadge(g, { area.getRight() - 338.0f, area.getY() + 8.0f, 108.0f, 22.0f }, paletteName(palette), fx::accent::filter);
    drawBadge(g, { area.getRight() - 222.0f, area.getY() + 8.0f, 94.0f, 22.0f }, sourceName(getCurrentSourceIndex()), fx::col::textSecondary);
    drawBadge(g, { area.getRight() - 120.0f, area.getY() + 8.0f, 96.0f, 22.0f },
              monitorSnapshot.frozen ? "FROZEN" : (monitorSnapshot.hold ? "HOLD" : "SCROLL"), monitorSnapshot.frozen ? fx::col::meterMid : fx::col::textSecondary);
}

void MusiqueAnalyzerEditor::paintOscilloscope(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& apvts = proc.getAPVTS();
    const float offset = getParamValue(apvts, "offset", 0.0f);
    const float persist = getParamValue(apvts, "scope_persist", 35.0f);
    const int channelMode = getCurrentChannelModeIndex();
    const auto graph = area.reduced(18.0f, 24.0f);
    const float centerY = graph.getCentreY() - (offset / 120.0f) * graph.getHeight() * 0.5f;

    g.setColour(fx::col::gridMinor);
    for (int line = 0; line < 5; ++line)
    {
        const float y = graph.getY() + (float) line * graph.getHeight() / 4.0f;
        g.drawHorizontalLine((int) y, graph.getX(), graph.getRight());
    }
    for (int column = 0; column < 9; ++column)
    {
        const float x = graph.getX() + (float) column * graph.getWidth() / 8.0f;
        g.drawVerticalLine((int) x, graph.getY(), graph.getBottom());
    }
    g.setColour(fx::col::gridMajor);
    g.drawHorizontalLine((int) centerY, graph.getX(), graph.getRight());

    auto buildTrace = [&](auto sampleFn, juce::Path& path)
    {
        for (size_t index = 0; index < scopeLeft.size(); ++index)
        {
            const float norm = scopeLeft.size() > 1 ? (float) index / (float) (scopeLeft.size() - 1) : 0.0f;
            const float x = graph.getX() + norm * graph.getWidth();
            const float sample = juce::jlimit(-1.2f, 1.2f, sampleFn(index));
            const float y = centerY - sample * graph.getHeight() * 0.36f;
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
    };

    auto midSample = [&](size_t index) { return 0.5f * (scopeLeft[index] + scopeRight[index]); };
    auto sideSample = [&](size_t index) { return 0.5f * (scopeLeft[index] - scopeRight[index]); };

    juce::Path primaryPath;
    juce::Path secondaryPath;
    switch (channelMode)
    {
        case MusiqueAnalyzerProcessor::stereoOverlayMode:
            buildTrace([&](size_t index) { return scopeLeft[index]; }, primaryPath);
            buildTrace([&](size_t index) { return scopeRight[index]; }, secondaryPath);
            break;
        case MusiqueAnalyzerProcessor::midMode:
            buildTrace(midSample, primaryPath);
            buildTrace(sideSample, secondaryPath);
            break;
        case MusiqueAnalyzerProcessor::sideMode:
            buildTrace(sideSample, primaryPath);
            buildTrace(midSample, secondaryPath);
            break;
        case MusiqueAnalyzerProcessor::sumMode:
        default:
            buildTrace(midSample, primaryPath);
            break;
    }

    if (!secondaryPath.isEmpty())
    {
        g.setColour(fx::col::textSecondary.withAlpha(juce::jmap(persist, 0.18f, 0.58f)));
        g.strokePath(secondaryPath, juce::PathStrokeType(1.4f));
    }
    g.setColour(fx::accent::filter.withAlpha(0.88f));
    g.strokePath(primaryPath, juce::PathStrokeType(2.0f));

    drawBadge(g, { area.getRight() - 338.0f, area.getY() + 8.0f, 108.0f, 22.0f }, triggerName(getChoiceValue(apvts, "scope_trigger", 0)), fx::accent::filter);
    drawBadge(g, { area.getRight() - 222.0f, area.getY() + 8.0f, 94.0f, 22.0f }, channelModeName(channelMode), fx::col::textSecondary);
    drawBadge(g, { area.getRight() - 120.0f, area.getY() + 8.0f, 96.0f, 22.0f },
              monitorSnapshot.hold ? "HOLD" : "LIVE", monitorSnapshot.hold ? fx::col::meterMid : fx::col::textSecondary);
}

void MusiqueAnalyzerEditor::paintLoudness(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& apvts = proc.getAPVTS();
    const int scale = getChoiceValue(apvts, "loud_scale", 0);
    const float history = getParamValue(apvts, "loud_history", 8.0f);
    const float holdValue = getParamValue(apvts, "loud_hold", 40.0f);
    const auto graph = area.reduced(24.0f, 26.0f);

    float minScale = -36.0f;
    float maxScale = 3.0f;
    if (scale == 1)
        minScale = -54.0f;
    else if (scale == 2)
        minScale = -72.0f;

    const std::array<float, 4> values {
        monitorSnapshot.loudness.momentaryLufs,
        monitorSnapshot.loudness.shortTermLufs,
        monitorSnapshot.loudness.integratedLufs,
        monitorSnapshot.loudness.truePeakDb
    };
    const std::array<const char*, 4> labels { "M", "S", "I", "TP" };
    const float barWidth = 84.0f;
    const float startX = graph.getX() + 80.0f;
    const float barTop = graph.getY() + 18.0f;
    const float barHeight = graph.getHeight() - 56.0f;
    const float barBottom = barTop + barHeight;

    for (int step = 0; step <= 6; ++step)
    {
        const float db = minScale + (maxScale - minScale) * ((float) step / 6.0f);
        const float y = barBottom - ((db - minScale) / (maxScale - minScale)) * barHeight;
        g.setColour(fx::col::gridMinor);
        g.drawHorizontalLine((int) y, graph.getX() + 24.0f, graph.getRight());
        g.setColour(fx::col::textMuted);
        g.drawText(juce::String((int) std::round(db)), (int) graph.getX(), (int) y - 6, 24, 12, juce::Justification::centredRight);
    }

    for (size_t index = 0; index < values.size(); ++index)
    {
        const float x = startX + (float) index * 156.0f;
        const float norm = juce::jlimit(0.0f, 1.0f, (values[index] - minScale) / (maxScale - minScale));
        const float filled = norm * barHeight;
        g.setColour(fx::col::meterBg);
        g.fillRoundedRectangle(x, barTop, barWidth, barHeight, 8.0f);
        g.setColour(index == 3 ? fx::col::meterMid.withAlpha(0.82f) : fx::accent::filter.withAlpha(0.82f));
        g.fillRoundedRectangle(x, barBottom - filled, barWidth, filled, 8.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions {}.withHeight(12.0f).withStyle("Bold")));
        g.drawText(labels[index], (int) x, (int) (barBottom + 10.0f), (int) barWidth, 16, juce::Justification::centred);
        g.drawText(juce::String(values[index], 1), (int) x, (int) (barTop - 18.0f), (int) barWidth, 14, juce::Justification::centred);
    }

    drawBadge(g, { area.getRight() - 340.0f, area.getY() + 8.0f, 108.0f, 22.0f }, loudScaleName(scale), fx::accent::filter);
    drawBadge(g, { area.getRight() - 224.0f, area.getY() + 8.0f, 96.0f, 22.0f }, juce::String(history, 1) + " S", fx::col::textSecondary);
    drawBadge(g, { area.getRight() - 120.0f, area.getY() + 8.0f, 96.0f, 22.0f }, "HOLD " + juce::String((int) std::round(holdValue)), fx::col::textSecondary);
}

void MusiqueAnalyzerEditor::paintStereo(juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& apvts = proc.getAPVTS();
    const float zoom = getParamValue(apvts, "stereo_zoom", 1.0f);
    const float focus = getParamValue(apvts, "stereo_focus", 50.0f) / 100.0f;
    const float decay = getParamValue(apvts, "stereo_decay", 35.0f) / 100.0f;
    const float balanceBias = getParamValue(apvts, "stereo_balance", 0.0f) / 100.0f;
    auto graph = area.reduced(18.0f, 24.0f);

    const auto scopeArea = graph.removeFromLeft(graph.getWidth() * 0.62f).reduced(12.0f);
    const auto statsArea = graph.reduced(16.0f);
    const auto centre = scopeArea.getCentre();
    const float radius = juce::jmin(scopeArea.getWidth(), scopeArea.getHeight()) * 0.48f;
    g.setColour(fx::col::gridMinor);
    g.drawEllipse(scopeArea, 1.0f);
    g.drawLine(centre.x, scopeArea.getY(), centre.x, scopeArea.getBottom(), 1.0f);
    g.drawLine(scopeArea.getX(), centre.y, scopeArea.getRight(), centre.y, 1.0f);
    g.drawLine(scopeArea.getX(), scopeArea.getBottom(), scopeArea.getRight(), scopeArea.getY(), 0.75f);
    g.drawLine(scopeArea.getX(), scopeArea.getY(), scopeArea.getRight(), scopeArea.getBottom(), 0.75f);

    juce::Path cloud;
    for (size_t index = 0; index < scopeLeft.size(); ++index)
    {
        const float leftSample = scopeLeft[index];
        const float rightSample = scopeRight[index];
        const float mid = 0.5f * (leftSample + rightSample);
        const float side = 0.5f * (leftSample - rightSample);
        const float x = centre.x + (side * (0.55f + focus * 0.95f) + balanceBias * 0.35f) * radius * zoom;
        const float y = centre.y - mid * radius * zoom;
        if (index == 0)
            cloud.startNewSubPath(x, y);
        else
            cloud.lineTo(x, y);
    }

    g.setColour(fx::accent::filter.withAlpha(0.18f + decay * 0.18f));
    g.strokePath(cloud, juce::PathStrokeType(5.0f));
    g.setColour(fx::accent::filter.withAlpha(0.86f));
    g.strokePath(cloud, juce::PathStrokeType(1.8f));

    auto drawStatBar = [&](float x, float y, float width, const juce::String& label, float normalised, juce::String valueText)
    {
        g.setColour(fx::col::textMuted);
        g.setFont(juce::Font(juce::FontOptions {}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(label, (int) x, (int) y, 72, 14, juce::Justification::left);
        g.setColour(fx::col::meterBg);
        g.fillRoundedRectangle(x + 74.0f, y + 4.0f, width, 8.0f, 4.0f);
        g.setColour(fx::accent::filter.withAlpha(0.82f));
        g.fillRoundedRectangle(x + 74.0f, y + 4.0f, width * juce::jlimit(0.0f, 1.0f, normalised), 8.0f, 4.0f);
        g.setColour(fx::col::textPrimary);
        g.drawText(valueText, (int) (x + 74.0f + width + 10.0f), (int) y, 64, 14, juce::Justification::left);
    };

    const float statX = statsArea.getX();
    const float statY = statsArea.getY() + 34.0f;
    drawStatBar(statX, statY, 112.0f, "CORR", (monitorSnapshot.stereoField.correlation + 1.0f) * 0.5f,
                juce::String(monitorSnapshot.stereoField.correlation, 2));
    drawStatBar(statX, statY + 34.0f, 112.0f, "WIDTH", juce::jlimit(0.0f, 1.0f, monitorSnapshot.stereoField.width / 1.5f),
                juce::String(monitorSnapshot.stereoField.width * 100.0f, 0) + "%");
    drawStatBar(statX, statY + 68.0f, 112.0f, "BAL", (monitorSnapshot.stereoField.balance + 1.0f) * 0.5f,
                juce::String(monitorSnapshot.stereoField.balance, 2));

    drawBadge(g, { area.getRight() - 330.0f, area.getY() + 8.0f, 104.0f, 22.0f }, "ZOOM " + juce::String(zoom, 1) + "X", fx::accent::filter);
    drawBadge(g, { area.getRight() - 218.0f, area.getY() + 8.0f, 90.0f, 22.0f }, "FOCUS " + juce::String((int) std::round(focus * 100.0f)), fx::col::textSecondary);
    drawBadge(g, { area.getRight() - 120.0f, area.getY() + 8.0f, 96.0f, 22.0f },
              monitorSnapshot.hold ? "HOLD" : "LIVE", monitorSnapshot.hold ? fx::col::meterMid : fx::col::textSecondary);
}

void MusiqueAnalyzerEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::filter);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12.0f, 10.0f, 40.0f, 40.0f), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth(), 32, 28);
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int footerY = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float) getWidth() - 52.0f, (float) footerY + 4.0f, 32.0f, 32.0f),
                    juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "MIX", 80, 120);
    fx::paint::footerLabel(g, "OUT", 220, 120);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueAnalyzerEditor::resized()
{
    titleLabel.setBounds(56, 10, 180, 40);
    bypassBtn.setBounds(getWidth() - 466, 16, 70, fx::dim::btnH);
    freezeBtn.setBounds(getWidth() - 388, 16, 70, fx::dim::btnH);
    actionBtn.setBounds(getWidth() - 310, 16, 84, fx::dim::btnH);
    channelModeBtn.setBounds(getWidth() - 218, 16, 88, fx::dim::btnH);
    statusBtn.setBounds(getWidth() - 122, 16, 104, fx::dim::btnH);

    const int presetY = fx::dim::headerH + 11;
    prevBtn.setBounds(128, presetY, 30, fx::dim::btnH);
    presetBox.setBounds(162, presetY, 228, fx::dim::btnH);
    nextBtn.setBounds(394, presetY, 30, fx::dim::btnH);
    viewBox.setBounds(438, presetY, 144, fx::dim::btnH);
    sourceBox.setBounds(590, presetY, 110, fx::dim::btnH);
    saveBtn.setBounds(712, presetY, 56, fx::dim::btnH);
    abBtn.setBounds(776, presetY, 48, fx::dim::btnH);

    const int controlsTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int knobWidth = getWidth() / 6;
    const int knobY = controlsTop + 14;
    for (int index = 0; index < 6; ++index)
    {
        const int x = index * knobWidth;
        knobs[(size_t) index].setBounds(x + (knobWidth - 92) / 2, knobY, 92, 90);
        knobLabels[(size_t) index].setBounds(x + (knobWidth - 120) / 2, knobY + 92, 120, 16);
    }

    const int footerY = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, footerY + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, footerY + 6, 20, fx::dim::footerH - 12);
    mixSlider.setBounds(80, footerY + 8, 120, 24);
    outputSlider.setBounds(220, footerY + 8, 120, 24);
    activeLED.setBounds(366, footerY + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 232, footerY + 8, 170, 24);
}
