#include "PluginProcessor.h"
#if ! MUSIQUE_ANALYZER_DSP_TESTS
#include "PluginEditor.h"
#endif

#include <algorithm>
#include <cmath>

namespace
{
float getRawValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float fallback = 0.0f)
{
    if (auto* raw = apvts.getRawParameterValue(id))
        return raw->load();
    return fallback;
}

int getChoiceValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int fallback = 0)
{
    return (int) std::round(getRawValue(apvts, id, (float) fallback));
}

void setParameterValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
{
    if (auto* parameter = apvts.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float clamp01(float value) noexcept
{
    return juce::jlimit(0.0f, 1.0f, value);
}

float dbToGain(float dB) noexcept
{
    return juce::Decibels::decibelsToGain(dB);
}

float gainToDb(float gain) noexcept
{
    return juce::Decibels::gainToDecibels(gain, -120.0f);
}

float spectrumNormalisedToFrequency(float norm) noexcept
{
    return 20.0f * std::pow(1000.0f, clamp01(norm));
}

float frequencyToSpectrumNorm(float frequency) noexcept
{
    return std::log10(juce::jmax(20.0f, frequency) / 20.0f) / std::log10(1000.0f);
}

struct AudioCallbackGuard
{
    explicit AudioCallbackGuard(std::atomic<int>& counterIn) : counter(counterIn)
    {
        counter.fetch_add(1, std::memory_order_acq_rel);
    }

    ~AudioCallbackGuard()
    {
        counter.fetch_sub(1, std::memory_order_acq_rel);
    }

    std::atomic<int>& counter;
};
}

MusiqueAnalyzerProcessor::MusiqueAnalyzerProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MusiqueAnalyzerWorker"),
      parameters(*this, nullptr, "MusiqueAnalyzer", createParameterLayout())
{
    resetAnalysisTransport();
    postExternalStateChange();
    startThread(juce::Thread::Priority::low);
}

MusiqueAnalyzerProcessor::~MusiqueAnalyzerProcessor()
{
    analysisSuspended.store(true, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread(1000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueAnalyzerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "view", "View",
        juce::StringArray { "Spectrum", "Spectrogram", "Oscilloscope", "Loudness", "Stereo" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "source", "Source", juce::StringArray { "Input", "Output" }, 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "channel_mode", "Channel Mode", juce::StringArray { "Sum", "Stereo", "Mid", "Side" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>("hold", "Hold", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("smoothing", "Smoothing", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("tilt", "Tilt", 0.0f, 6.0f, 3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("range", "Range", 24.0f, 120.0f, 60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("offset", "Offset", -60.0f, 60.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("resolution", "Resolution", 1.0f, 4.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("speed", "Speed", 0.1f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("analysis_output", "Analysis Output", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("spec_contrast", "Spec Contrast", 0.0f, 100.0f, 55.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("spec_floor", "Spec Floor", -120.0f, -24.0f, -84.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "spec_scroll", "Spec Scroll", juce::NormalisableRange<float>(1.0f, 4.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "spec_palette", "Spec Palette", juce::StringArray { "Amber", "Ice", "Mono" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("scope_window", "Scope Window", 20.0f, 1000.0f, 220.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("scope_zoom", "Scope Zoom", 0.5f, 8.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "scope_trigger", "Scope Trigger", juce::StringArray { "Auto", "Rise", "Free" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("scope_persist", "Scope Persist", 0.0f, 100.0f, 35.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("loud_history", "Loud History", 2.0f, 20.0f, 8.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("loud_hold", "Loud Hold", 0.0f, 100.0f, 40.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "loud_scale", "Loud Scale", juce::StringArray { "Tight", "EBU", "Wide" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("loud_gate", "Loud Gate", -80.0f, -10.0f, -70.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_zoom", "Stereo Zoom", 0.5f, 4.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_decay", "Stereo Decay", 0.0f, 100.0f, 35.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_focus", "Stereo Focus", 0.0f, 100.0f, 50.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_hold", "Stereo Hold", 0.0f, 100.0f, 40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_balance", "Stereo Balance", -100.0f, 100.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -24.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));

    return { params.begin(), params.end() };
}

juce::StringArray MusiqueAnalyzerProcessor::getAllParameterIds()
{
    return {
        "view", "source", "channel_mode", "hold",
        "smoothing", "tilt", "range", "offset", "resolution", "speed", "analysis_output",
        "spec_contrast", "spec_floor", "spec_scroll", "spec_palette",
        "scope_window", "scope_zoom", "scope_trigger", "scope_persist",
        "loud_history", "loud_hold", "loud_scale", "loud_gate",
        "stereo_zoom", "stereo_decay", "stereo_focus", "stereo_hold", "stereo_balance",
        "mix", "output", "bypass", "freeze"
    };
}

void MusiqueAnalyzerProcessor::normalisePresetObject(juce::var& preset)
{
    auto* object = preset.getDynamicObject();
    if (object == nullptr)
        return;

    auto ensure = [&](const char* key, const juce::var& value)
    {
        if (!object->hasProperty(key))
            object->setProperty(key, value);
    };

    const bool legacyOutput = object->hasProperty("analysis_output")
        ? (bool) object->getProperty("analysis_output")
        : true;

    ensure("view", 0);
    ensure("source", legacyOutput ? 1 : 0);
    ensure("channel_mode", 0);
    ensure("hold", false);
    ensure("smoothing", 0.5f);
    ensure("tilt", 3.0f);
    ensure("range", 60.0f);
    ensure("offset", 0.0f);
    ensure("resolution", 2.0f);
    ensure("speed", 1.0f);
    ensure("analysis_output", legacyOutput);
    ensure("spec_contrast", 55.0f);
    ensure("spec_floor", -84.0f);
    ensure("spec_scroll", 1.0f);
    ensure("spec_palette", 0);
    ensure("scope_window", 220.0f);
    ensure("scope_zoom", 1.0f);
    ensure("scope_trigger", 0);
    ensure("scope_persist", 35.0f);
    ensure("loud_history", 8.0f);
    ensure("loud_hold", 40.0f);
    ensure("loud_scale", 0);
    ensure("loud_gate", -70.0f);
    ensure("stereo_zoom", 1.0f);
    ensure("stereo_decay", 35.0f);
    ensure("stereo_focus", 50.0f);
    ensure("stereo_hold", 40.0f);
    ensure("stereo_balance", 0.0f);
    ensure("mix", 100.0f);
    ensure("output", 0.0f);
    ensure("bypass", false);
    ensure("freeze", false);
}

void MusiqueAnalyzerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analysisSuspended.store(true, std::memory_order_release);
    while (audioCallbacksInFlight.load(std::memory_order_acquire) > 0)
        juce::Thread::sleep(1);

    signalThreadShouldExit();
    notify();
    stopThread(1000);

    preparedSampleRate = sampleRate;
    maximumBlockSize = juce::jmax(1, samplesPerBlock);

    if (loudHighPass.coefficients == nullptr)
        loudHighPass.coefficients = new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    if (loudShelf.coefficients == nullptr)
        loudShelf.coefficients = new juce::dsp::IIR::Coefficients<float>(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

    *loudHighPass.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 38.0f, 0.5f);
    *loudShelf.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate, 1681.974f, 0.707f, dbToGain(4.0f));
    loudHighPass.reset();
    loudShelf.reset();

    resetAnalysisTransport();
    startThread(juce::Thread::Priority::low);
    analysisSuspended.store(false, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::releaseResources()
{
    analysisSuspended.store(true, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread(1000);
}

bool MusiqueAnalyzerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    const bool monoLayout = input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::mono();
    const bool stereoLayout = input == juce::AudioChannelSet::stereo() && output == juce::AudioChannelSet::stereo();
    return monoLayout || stereoLayout;
}

void MusiqueAnalyzerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    AudioCallbackGuard callbackGuard(audioCallbacksInFlight);
    visualState.captureInput(buffer);

    const auto params = buildParameterSnapshot();
    if (params.freeze || (params.hold && (params.view == spectrumView || params.view == spectrogramView)))
        dropQueuedAnalysisFrames.store(true, std::memory_order_release);

    currentViewAtomic.store(params.view, std::memory_order_relaxed);
    currentSourceAtomic.store(params.source, std::memory_order_relaxed);
    currentChannelModeAtomic.store(params.channelMode, std::memory_order_relaxed);
    freezeStateAtomic.store(params.freeze, std::memory_order_relaxed);
    holdStateAtomic.store(params.hold, std::memory_order_relaxed);

    if (params.source == sourceInput)
        captureAnalysisSource(buffer, params);

    if (!params.bypass)
    {
        const float outputGain = dbToGain(params.outputDb);
        const float mix = clamp01(params.mix / 100.0f);
        if (outputGain != 1.0f || mix != 1.0f)
        {
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto* channelData = buffer.getWritePointer(channel);
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                {
                    const float dry = channelData[sample];
                    const float wet = dry * outputGain;
                    channelData[sample] = dry + mix * (wet - dry);
                }
            }
        }
    }

    visualState.captureOutput(buffer);

    if (params.source == sourceOutput)
        captureAnalysisSource(buffer, params);
}

void MusiqueAnalyzerProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    AudioCallbackGuard callbackGuard(audioCallbacksInFlight);
    visualState.captureInput(buffer);
    visualState.captureOutput(buffer);
    captureAnalysisSource(buffer, buildParameterSnapshot());
}

juce::AudioProcessorEditor* MusiqueAnalyzerProcessor::createEditor()
{
#if MUSIQUE_ANALYZER_DSP_TESTS
    return nullptr;
#else
    return new MusiqueAnalyzerEditor(*this);
#endif
}

juce::AudioProcessorParameter* MusiqueAnalyzerProcessor::getBypassParameter() const
{
    return const_cast<juce::AudioProcessorValueTreeState&>(parameters).getParameter("bypass");
}

void MusiqueAnalyzerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    normaliseStateTree(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void MusiqueAnalyzerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState == nullptr || !xmlState->hasTagName(parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml(*xmlState);
    normaliseStateTree(state);
    parameters.replaceState(state);
    postExternalStateChange();
}

void MusiqueAnalyzerProcessor::getSpectrumSnapshot(SpectrumArray& primary,
                                                   SpectrumArray* secondary,
                                                   SpectrumArray* peaks) const noexcept
{
    for (size_t index = 0; index < spectrumBinCount; ++index)
    {
        primary[index] = primarySpectrumData[index].load(std::memory_order_relaxed);
        if (secondary != nullptr)
            (*secondary)[index] = secondarySpectrumData[index].load(std::memory_order_relaxed);
        if (peaks != nullptr)
            (*peaks)[index] = primaryPeakData[index].load(std::memory_order_relaxed);
    }
}

void MusiqueAnalyzerProcessor::getSpectrogramSnapshot(SpectrogramArray& destination, int& cursor) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto generation = spectrogramGeneration.load(std::memory_order_acquire);
        if ((generation & 1u) != 0u)
            continue;

        destination = spectrogramMatrix;
        const int localCursor = spectrogramCursor.load(std::memory_order_relaxed);
        if (generation == spectrogramGeneration.load(std::memory_order_acquire))
        {
            cursor = localCursor;
            return;
        }
    }

    destination.fill(-120.0f);
    cursor = spectrogramCursor.load(std::memory_order_relaxed);
}

void MusiqueAnalyzerProcessor::getScopeSnapshot(ScopeArray& left, ScopeArray& right) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto generation = scopeGeneration.load(std::memory_order_acquire);
        if ((generation & 1u) != 0u)
            continue;

        left = scopeSnapshotLeft;
        right = scopeSnapshotRight;
        if (generation == scopeGeneration.load(std::memory_order_acquire))
            return;
    }

    left.fill(0.0f);
    right.fill(0.0f);
}

AnalyzerMonitorSnapshot MusiqueAnalyzerProcessor::getMonitorSnapshot() const noexcept
{
    return {
        {
            momentaryLufs.load(std::memory_order_relaxed),
            shortTermLufs.load(std::memory_order_relaxed),
            integratedLufs.load(std::memory_order_relaxed),
            truePeakDb.load(std::memory_order_relaxed)
        },
        stereoFieldState.getSnapshot(),
        currentViewAtomic.load(std::memory_order_relaxed),
        currentSourceAtomic.load(std::memory_order_relaxed),
        currentChannelModeAtomic.load(std::memory_order_relaxed),
        spectrogramCursor.load(std::memory_order_relaxed),
        freezeStateAtomic.load(std::memory_order_relaxed),
        holdStateAtomic.load(std::memory_order_relaxed)
    };
}

void MusiqueAnalyzerProcessor::postExternalStateChange()
{
    analysisSuspended.store(true, std::memory_order_release);
    while (audioCallbacksInFlight.load(std::memory_order_acquire) > 0)
        juce::Thread::sleep(1);

    const auto params = buildParameterSnapshot();
    syncLegacyStateFromSource(params);
    resetAnalysisTransport();
    currentViewAtomic.store(params.view, std::memory_order_relaxed);
    currentSourceAtomic.store(params.source, std::memory_order_relaxed);
    currentChannelModeAtomic.store(params.channelMode, std::memory_order_relaxed);
    freezeStateAtomic.store(params.freeze, std::memory_order_relaxed);
    holdStateAtomic.store(params.hold, std::memory_order_relaxed);
    dropQueuedAnalysisFrames.store(false, std::memory_order_release);
    analysisSuspended.store(false, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::clearSpectrogramHistory()
{
    const auto generation = spectrogramGeneration.fetch_add(1u, std::memory_order_acq_rel);
    juce::ignoreUnused(generation);
    spectrogramMatrix.fill(-120.0f);
    spectrogramCursor.store(0, std::memory_order_relaxed);
    spectrogramGeneration.fetch_add(1u, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::resetLoudnessHistory()
{
    momentaryEnergyState = 0.0;
    shortTermEnergyState = 0.0;
    integratedEnergySum = 0.0;
    integratedSampleCount = 0.0;
    loudnessPrevSample = 0.0f;
    momentaryLufs.store(-120.0f, std::memory_order_relaxed);
    shortTermLufs.store(-120.0f, std::memory_order_relaxed);
    integratedLufs.store(-120.0f, std::memory_order_relaxed);
    truePeakDb.store(-120.0f, std::memory_order_relaxed);
}

void MusiqueAnalyzerProcessor::ensureStateParamValue(juce::ValueTree& state, const char* paramId, const juce::var& value)
{
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        auto child = state.getChild(childIndex);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == paramId)
        {
            if (!child.hasProperty("value"))
                child.setProperty("value", value, nullptr);
            return;
        }
    }

    juce::ValueTree child("PARAM");
    child.setProperty("id", paramId, nullptr);
    child.setProperty("value", value, nullptr);
    state.appendChild(child, nullptr);
}

juce::var MusiqueAnalyzerProcessor::readStateParamValue(const juce::ValueTree& state, const char* paramId, const juce::var& fallback)
{
    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        auto child = state.getChild(childIndex);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == paramId)
            return child.getProperty("value", fallback);
    }

    return fallback;
}

void MusiqueAnalyzerProcessor::normaliseStateTree(juce::ValueTree& state)
{
    const bool legacyOutput = (bool) readStateParamValue(state, "analysis_output", true);
    ensureStateParamValue(state, "view", spectrumView);
    ensureStateParamValue(state, "source", legacyOutput ? sourceOutput : sourceInput);
    ensureStateParamValue(state, "channel_mode", sumMode);
    ensureStateParamValue(state, "hold", false);
    ensureStateParamValue(state, "smoothing", 0.5f);
    ensureStateParamValue(state, "tilt", 3.0f);
    ensureStateParamValue(state, "range", 60.0f);
    ensureStateParamValue(state, "offset", 0.0f);
    ensureStateParamValue(state, "resolution", 2.0f);
    ensureStateParamValue(state, "speed", 1.0f);
    ensureStateParamValue(state, "analysis_output", legacyOutput);
    ensureStateParamValue(state, "spec_contrast", 55.0f);
    ensureStateParamValue(state, "spec_floor", -84.0f);
    ensureStateParamValue(state, "spec_scroll", 1.0f);
    ensureStateParamValue(state, "spec_palette", 0);
    ensureStateParamValue(state, "scope_window", 220.0f);
    ensureStateParamValue(state, "scope_zoom", 1.0f);
    ensureStateParamValue(state, "scope_trigger", 0);
    ensureStateParamValue(state, "scope_persist", 35.0f);
    ensureStateParamValue(state, "loud_history", 8.0f);
    ensureStateParamValue(state, "loud_hold", 40.0f);
    ensureStateParamValue(state, "loud_scale", 0);
    ensureStateParamValue(state, "loud_gate", -70.0f);
    ensureStateParamValue(state, "stereo_zoom", 1.0f);
    ensureStateParamValue(state, "stereo_decay", 35.0f);
    ensureStateParamValue(state, "stereo_focus", 50.0f);
    ensureStateParamValue(state, "stereo_hold", 40.0f);
    ensureStateParamValue(state, "stereo_balance", 0.0f);
    ensureStateParamValue(state, "mix", 100.0f);
    ensureStateParamValue(state, "output", 0.0f);
    ensureStateParamValue(state, "bypass", false);
    ensureStateParamValue(state, "freeze", false);
}

MusiqueAnalyzerProcessor::AnalyzerParameters MusiqueAnalyzerProcessor::buildParameterSnapshot() const
{
    AnalyzerParameters snapshot;
    snapshot.view = juce::jlimit(0, numViews - 1, getChoiceValue(parameters, "view", spectrumView));
    snapshot.source = juce::jlimit(0, 1, getChoiceValue(parameters, "source", sourceOutput));
    snapshot.channelMode = juce::jlimit(0, numChannelModes - 1, getChoiceValue(parameters, "channel_mode", sumMode));
    snapshot.hold = getRawValue(parameters, "hold") > 0.5f;
    snapshot.smoothing = getRawValue(parameters, "smoothing", 0.5f);
    snapshot.tilt = getRawValue(parameters, "tilt", 3.0f);
    snapshot.range = getRawValue(parameters, "range", 60.0f);
    snapshot.offset = getRawValue(parameters, "offset", 0.0f);
    snapshot.resolution = getRawValue(parameters, "resolution", 2.0f);
    snapshot.speed = getRawValue(parameters, "speed", 1.0f);
    snapshot.legacyAnalysisOutput = getRawValue(parameters, "analysis_output", 1.0f) > 0.5f;
    snapshot.mix = getRawValue(parameters, "mix", 100.0f);
    snapshot.outputDb = getRawValue(parameters, "output", 0.0f);
    snapshot.bypass = getRawValue(parameters, "bypass") > 0.5f;
    snapshot.freeze = getRawValue(parameters, "freeze") > 0.5f;
    snapshot.specContrast = getRawValue(parameters, "spec_contrast", 55.0f);
    snapshot.specFloor = getRawValue(parameters, "spec_floor", -84.0f);
    snapshot.specScroll = getRawValue(parameters, "spec_scroll", 1.0f);
    snapshot.specPalette = getChoiceValue(parameters, "spec_palette", 0);
    snapshot.scopeWindowMs = getRawValue(parameters, "scope_window", 220.0f);
    snapshot.scopeZoom = getRawValue(parameters, "scope_zoom", 1.0f);
    snapshot.scopeTrigger = getChoiceValue(parameters, "scope_trigger", 0);
    snapshot.scopePersist = getRawValue(parameters, "scope_persist", 35.0f);
    snapshot.loudHistory = getRawValue(parameters, "loud_history", 8.0f);
    snapshot.loudHold = getRawValue(parameters, "loud_hold", 40.0f);
    snapshot.loudScale = getChoiceValue(parameters, "loud_scale", 0);
    snapshot.loudGate = getRawValue(parameters, "loud_gate", -70.0f);
    snapshot.stereoZoom = getRawValue(parameters, "stereo_zoom", 1.0f);
    snapshot.stereoDecay = getRawValue(parameters, "stereo_decay", 35.0f);
    snapshot.stereoFocus = getRawValue(parameters, "stereo_focus", 50.0f);
    snapshot.stereoHold = getRawValue(parameters, "stereo_hold", 40.0f);
    snapshot.stereoBalance = getRawValue(parameters, "stereo_balance", 0.0f);
    return snapshot;
}

void MusiqueAnalyzerProcessor::resetAnalysisTransport()
{
    analysisBufferLeft.fill(0.0f);
    analysisBufferRight.fill(0.0f);
    for (auto& frame : analysisFramesLeft)
        frame.fill(0.0f);
    for (auto& frame : analysisFramesRight)
        frame.fill(0.0f);
    fftScratchPrimary.fill(0.0f);
    fftScratchSecondary.fill(0.0f);
    smoothedPrimary.fill(-120.0f);
    smoothedSecondary.fill(-120.0f);
    peakHoldPrimary.fill(-120.0f);
    peakHoldSecondary.fill(-120.0f);
    for (size_t index = 0; index < spectrumBinCount; ++index)
    {
        primarySpectrumData[index].store(-120.0f, std::memory_order_relaxed);
        secondarySpectrumData[index].store(-120.0f, std::memory_order_relaxed);
        primaryPeakData[index].store(-120.0f, std::memory_order_relaxed);
        secondaryPeakData[index].store(-120.0f, std::memory_order_relaxed);
    }

    clearSpectrogramHistory();
    scopeRingLeft.fill(0.0f);
    scopeRingRight.fill(0.0f);
    scopeSnapshotLeft.fill(0.0f);
    scopeSnapshotRight.fill(0.0f);
    scopeWritePos = 0;
    scopeGeneration.store(0u, std::memory_order_relaxed);

    resetLoudnessHistory();
    juce::AudioBuffer<float> emptyStereo(2, 1);
    emptyStereo.clear();
    stereoFieldState.capture(emptyStereo);
    currentViewAtomic.store(spectrumView, std::memory_order_relaxed);
    currentSourceAtomic.store(sourceOutput, std::memory_order_relaxed);
    currentChannelModeAtomic.store(sumMode, std::memory_order_relaxed);
    freezeStateAtomic.store(false, std::memory_order_relaxed);
    holdStateAtomic.store(false, std::memory_order_relaxed);

    analysisWritePos = 0;
    samplesUntilNextFFT = fftHopSize;
    analysisFrameFifo.reset();
    dropQueuedAnalysisFrames.store(false, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::pushNextAnalysisSample(float left, float right) noexcept
{
    analysisBufferLeft[(size_t) analysisWritePos] = left;
    analysisBufferRight[(size_t) analysisWritePos] = right;
    analysisWritePos = (analysisWritePos + 1) % fftSize;

    if (--samplesUntilNextFFT <= 0)
    {
        samplesUntilNextFFT += fftHopSize;
        queueAnalysisFrame();
    }
}

void MusiqueAnalyzerProcessor::queueAnalysisFrame() noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    analysisFrameFifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 <= 0)
        return;

    auto& leftFrame = analysisFramesLeft[(size_t) start1];
    auto& rightFrame = analysisFramesRight[(size_t) start1];
    for (int sample = 0; sample < fftSize; ++sample)
    {
        const size_t index = (size_t) ((analysisWritePos + sample) % fftSize);
        leftFrame[(size_t) sample] = analysisBufferLeft[index];
        rightFrame[(size_t) sample] = analysisBufferRight[index];
    }

    analysisFrameFifo.finishedWrite(1);
    notify();
}

void MusiqueAnalyzerProcessor::processAnalysisFrame(const float* leftFrame, const float* rightFrame) noexcept
{
    if (preparedSampleRate <= 0.0)
        return;

    const auto params = buildParameterSnapshot();
    if (params.freeze || (params.hold && (params.view == spectrumView || params.view == spectrogramView)))
        return;

    std::array<float, fftSize> primaryFrame {};
    std::array<float, fftSize> secondaryFrame {};
    for (int sample = 0; sample < fftSize; ++sample)
    {
        const float left = leftFrame[(size_t) sample];
        const float right = rightFrame[(size_t) sample];
        const float mid = 0.5f * (left + right);
        const float side = 0.5f * (left - right);

        switch (params.channelMode)
        {
            case stereoOverlayMode:
                primaryFrame[(size_t) sample] = left;
                secondaryFrame[(size_t) sample] = right;
                break;
            case midMode:
                primaryFrame[(size_t) sample] = mid;
                secondaryFrame[(size_t) sample] = side;
                break;
            case sideMode:
                primaryFrame[(size_t) sample] = side;
                secondaryFrame[(size_t) sample] = mid;
                break;
            case sumMode:
            default:
                primaryFrame[(size_t) sample] = mid;
                secondaryFrame[(size_t) sample] = 0.0f;
                break;
        }
    }

    const float smoothingFactor = juce::jlimit(0.08f, 0.985f,
        juce::jmap(params.smoothing, 0.0f, 1.0f, 0.12f, 0.94f) * juce::jmap(params.speed, 0.1f, 10.0f, 1.05f, 0.68f));
    const float averagingWidth = juce::jmap(params.resolution, 1.0f, 4.0f, 0.045f, 0.010f);
    const float nyquist = (float) (preparedSampleRate * 0.5);
    const int fftLimit = fftSize / 2;
    const float tiltCentered = params.tilt - 3.0f;
    constexpr float magnitudeNormalisation = 2.0f / (float) fftSize;

    auto computeSpectrum = [&](const std::array<float, fftSize>& input,
                               std::array<float, 2 * fftSize>& scratch,
                               SpectrumArray& smoothed,
                               std::array<std::atomic<float>, spectrumBinCount>& destination)
    {
        for (int sample = 0; sample < fftSize; ++sample)
            scratch[(size_t) sample] = input[(size_t) sample];
        std::fill(scratch.begin() + fftSize, scratch.end(), 0.0f);

        fftWindow.multiplyWithWindowingTable(scratch.data(), fftSize);
        forwardFFT.performFrequencyOnlyForwardTransform(scratch.data());

        for (size_t index = 0; index < spectrumBinCount; ++index)
        {
            const float norm = (float) index / (float) (spectrumBinCount - 1);
            const float minNorm = juce::jlimit(0.0f, 1.0f, norm - averagingWidth);
            const float maxNorm = juce::jlimit(0.0f, 1.0f, norm + averagingWidth);
            const float freqA = spectrumNormalisedToFrequency(minNorm);
            const float freqB = spectrumNormalisedToFrequency(maxNorm);
            const int startBin = juce::jlimit(1, fftLimit - 1, (int) std::floor((freqA / nyquist) * (float) fftLimit));
            const int endBin = juce::jlimit(startBin, fftLimit - 1, (int) std::ceil((freqB / nyquist) * (float) fftLimit));

            float magnitude = 0.0f;
            for (int bin = startBin; bin <= endBin; ++bin)
                magnitude += scratch[(size_t) bin];

            magnitude /= (float) juce::jmax(1, endBin - startBin + 1);
            const float centerFreq = std::sqrt(freqA * freqB);
            const float octaveFrom1k = std::log2(juce::jmax(20.0f, centerFreq) / 1000.0f);
            float dB = gainToDb(magnitude * magnitudeNormalisation);
            dB += tiltCentered * octaveFrom1k * 0.78f;
            dB = juce::jlimit(-120.0f, 24.0f, dB);

            smoothed[index] = smoothed[index] * smoothingFactor + dB * (1.0f - smoothingFactor);
            destination[index].store(smoothed[index], std::memory_order_relaxed);
        }
    };

    computeSpectrum(primaryFrame, fftScratchPrimary, smoothedPrimary, primarySpectrumData);
    computeSpectrum(secondaryFrame, fftScratchSecondary, smoothedSecondary, secondarySpectrumData);

    const float peakDecay = juce::jmap(params.speed, 0.1f, 10.0f, 0.10f, 1.25f);
    for (size_t index = 0; index < spectrumBinCount; ++index)
    {
        peakHoldPrimary[index] = params.hold
            ? juce::jmax(peakHoldPrimary[index], smoothedPrimary[index])
            : juce::jmax(smoothedPrimary[index], peakHoldPrimary[index] - peakDecay);
        peakHoldSecondary[index] = params.hold
            ? juce::jmax(peakHoldSecondary[index], smoothedSecondary[index])
            : juce::jmax(smoothedSecondary[index], peakHoldSecondary[index] - peakDecay);

        primaryPeakData[index].store(peakHoldPrimary[index], std::memory_order_relaxed);
        secondaryPeakData[index].store(peakHoldSecondary[index], std::memory_order_relaxed);
    }

    const int advanceColumns = juce::jlimit(1, 4, (int) std::round(params.specScroll));
    spectrogramGeneration.fetch_add(1u, std::memory_order_acq_rel);
    int cursor = spectrogramCursor.load(std::memory_order_relaxed);
    for (int step = 0; step < advanceColumns; ++step)
    {
        cursor = (cursor + 1) % (int) spectrogramColumnCount;
        for (size_t row = 0; row < spectrumBinCount; ++row)
            spectrogramMatrix[row * spectrogramColumnCount + (size_t) cursor] = smoothedPrimary[row];
    }
    spectrogramCursor.store(cursor, std::memory_order_relaxed);
    spectrogramGeneration.fetch_add(1u, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::captureAnalysisSource(const juce::AudioBuffer<float>& buffer, const AnalyzerParameters& params) noexcept
{
    if (analysisSuspended.load(std::memory_order_acquire) || params.freeze || buffer.getNumSamples() <= 0)
        return;

    const bool holdSpectrumFrames = params.hold && (params.view == spectrumView || params.view == spectrogramView);
    const bool holdScopeFrame = params.hold && params.view == oscilloscopeView;
    const bool holdLoudnessFrame = params.hold && params.view == loudnessView;
    const bool holdStereoFrame = params.hold && params.view == stereoView;

    if (!holdStereoFrame)
        stereoFieldState.capture(buffer);

    if (!holdLoudnessFrame)
        updateLoudness(buffer, params);

    const int rightChannel = buffer.getNumChannels() > 1 ? 1 : 0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float left = buffer.getSample(0, sample);
        const float right = buffer.getSample(rightChannel, sample);
        if (!holdScopeFrame)
        {
            scopeRingLeft[(size_t) scopeWritePos] = left;
            scopeRingRight[(size_t) scopeWritePos] = right;
            scopeWritePos = (scopeWritePos + 1) % scopeRingSize;
        }

        if (!holdSpectrumFrames)
            pushNextAnalysisSample(left, right);
    }

    if (!holdScopeFrame)
        updateScopeSnapshot(params);
}

void MusiqueAnalyzerProcessor::updateScopeSnapshot(const AnalyzerParameters& params) noexcept
{
    if (preparedSampleRate <= 0.0)
        return;

    const int desiredWindowSamples = juce::jlimit((int) scopeSnapshotSize,
                                                  scopeRingSize - 1,
                                                  (int) std::round((params.scopeWindowMs * 0.001f) * (float) preparedSampleRate));
    int startOffset = scopeWritePos - desiredWindowSamples;
    if (startOffset < 0)
        startOffset += scopeRingSize;

    if (params.scopeTrigger != 2)
    {
        const int searchSamples = juce::jmax(1, desiredWindowSamples - (int) scopeSnapshotSize);
        for (int sample = 1; sample < searchSamples; ++sample)
        {
            const int prevIndex = (startOffset + sample - 1) % scopeRingSize;
            const int currIndex = (startOffset + sample) % scopeRingSize;
            const float prevMid = 0.5f * (scopeRingLeft[(size_t) prevIndex] + scopeRingRight[(size_t) prevIndex]);
            const float currMid = 0.5f * (scopeRingLeft[(size_t) currIndex] + scopeRingRight[(size_t) currIndex]);
            const bool rising = prevMid <= 0.0f && currMid > 0.0f;
            const bool autoNearZero = params.scopeTrigger == 0 && std::abs(currMid) < 0.08f;
            if (rising || autoNearZero)
            {
                startOffset = currIndex;
                break;
            }
        }
    }

    const float zoom = juce::jlimit(0.25f, 8.0f, params.scopeZoom);
    scopeGeneration.fetch_add(1u, std::memory_order_acq_rel);
    for (size_t index = 0; index < scopeSnapshotSize; ++index)
    {
        const float norm = scopeSnapshotSize > 1 ? (float) index / (float) (scopeSnapshotSize - 1) : 0.0f;
        const int sampleOffset = juce::jlimit(0,
                                              desiredWindowSamples - 1,
                                              (int) std::round(norm * (float) (desiredWindowSamples - 1)));
        const int ringIndex = (startOffset + sampleOffset) % scopeRingSize;
        scopeSnapshotLeft[index] = juce::jlimit(-1.5f, 1.5f, scopeRingLeft[(size_t) ringIndex] * zoom);
        scopeSnapshotRight[index] = juce::jlimit(-1.5f, 1.5f, scopeRingRight[(size_t) ringIndex] * zoom);
    }
    scopeGeneration.fetch_add(1u, std::memory_order_release);
}

void MusiqueAnalyzerProcessor::updateLoudness(const juce::AudioBuffer<float>& buffer, const AnalyzerParameters& params) noexcept
{
    const int rightChannel = buffer.getNumChannels() > 1 ? 1 : 0;
    double weightedEnergy = 0.0;
    float blockTruePeak = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float mono = 0.5f * (buffer.getSample(0, sample) + buffer.getSample(rightChannel, sample));
        const float weighted = loudShelf.processSample(loudHighPass.processSample(mono));
        weightedEnergy += (double) weighted * (double) weighted;
        const float interp = 0.5f * (mono + loudnessPrevSample);
        blockTruePeak = juce::jmax(blockTruePeak, std::abs(mono), std::abs(interp));
        loudnessPrevSample = mono;
    }

    const double blockEnergy = weightedEnergy / (double) juce::jmax(1, buffer.getNumSamples());
    const float tauMomentary = 0.40f;
    const float tauShortTerm = 3.0f;
    const float blockSeconds = (float) buffer.getNumSamples() / (float) juce::jmax(1.0, preparedSampleRate);
    const float alphaM = std::exp(-blockSeconds / tauMomentary);
    const float alphaS = std::exp(-blockSeconds / tauShortTerm);

    momentaryEnergyState = momentaryEnergyState * alphaM + blockEnergy * (1.0 - alphaM);
    shortTermEnergyState = shortTermEnergyState * alphaS + blockEnergy * (1.0 - alphaS);

    const float blockLufs = gainToDb(std::sqrt((float) juce::jmax(1.0e-12, blockEnergy))) - 0.691f;
    if (blockLufs > params.loudGate)
    {
        integratedEnergySum += blockEnergy * (double) buffer.getNumSamples();
        integratedSampleCount += (double) buffer.getNumSamples();
    }

    const float momentary = gainToDb(std::sqrt((float) juce::jmax(1.0e-12, momentaryEnergyState))) - 0.691f;
    const float shortTerm = gainToDb(std::sqrt((float) juce::jmax(1.0e-12, shortTermEnergyState))) - 0.691f;
    const float integrated = integratedSampleCount > 0.0
        ? gainToDb(std::sqrt((float) juce::jmax(1.0e-12, integratedEnergySum / integratedSampleCount))) - 0.691f
        : -120.0f;

    momentaryLufs.store(momentary, std::memory_order_relaxed);
    shortTermLufs.store(shortTerm, std::memory_order_relaxed);
    integratedLufs.store(integrated, std::memory_order_relaxed);
    truePeakDb.store(gainToDb(blockTruePeak), std::memory_order_relaxed);
}

void MusiqueAnalyzerProcessor::syncLegacyStateFromSource(const AnalyzerParameters& params)
{
    const float legacyValue = params.source == sourceOutput ? 1.0f : 0.0f;
    if (std::abs(getRawValue(parameters, "analysis_output", legacyValue) - legacyValue) > 0.001f)
        setParameterValue(parameters, "analysis_output", legacyValue);
}

void MusiqueAnalyzerProcessor::run()
{
    while (! threadShouldExit())
    {
        if (dropQueuedAnalysisFrames.load(std::memory_order_acquire))
        {
            for (;;)
            {
                int dropStart1 = 0, dropSize1 = 0, dropStart2 = 0, dropSize2 = 0;
                analysisFrameFifo.prepareToRead(analysisQueueSize, dropStart1, dropSize1, dropStart2, dropSize2);
                const int readyToDrop = dropSize1 + dropSize2;
                if (readyToDrop <= 0)
                    break;

                analysisFrameFifo.finishedRead(readyToDrop);
            }

            dropQueuedAnalysisFrames.store(false, std::memory_order_release);
        }

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        analysisFrameFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0)
        {
            wait(25);
            continue;
        }

        processAnalysisFrame(analysisFramesLeft[(size_t) start1].data(),
                             analysisFramesRight[(size_t) start1].data());
        analysisFrameFifo.finishedRead(1);
    }
}

#if ! MUSIQUE_ANALYZER_DSP_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueAnalyzerProcessor();
}
#endif
