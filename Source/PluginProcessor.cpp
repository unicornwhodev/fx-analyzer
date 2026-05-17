#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>

MusiqueAnalyzerProcessor::MusiqueAnalyzerProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MusiqueAnalyzerFFT"),
      parameters(*this, nullptr, "MusiqueAnalyzer", createParameterLayout())
{
    smoothedSpectrum.fill(-96.0f);
    for (auto& value : spectrumData)
        value.store(-96.0f);
    startThread(juce::Thread::Priority::low);
}

MusiqueAnalyzerProcessor::~MusiqueAnalyzerProcessor()
{
    signalThreadShouldExit();
    notify();
    stopThread(1000);
}

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueAnalyzerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    
    // Analyzer specific parameters
    p.push_back(std::make_unique<juce::AudioParameterFloat>("smoothing", "Smoothing", 0.0f, 1.0f, 0.5f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tilt", "Tilt", 0.0f, 6.0f, 3.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("range", "Range", 24.0f, 120.0f, 60.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("offset", "Offset", -60.0f, 60.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("resolution", "Resolution", 1.0f, 4.0f, 2.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("speed", "Speed", 0.1f, 10.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("analysis_output", "Analysis Output", true));
    
    // Standard footer parameters
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -24.0f, 12.0f, 0.0f));
    
    // Standard header parameters
    p.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));

    return { p.begin(), p.end() };
}

void MusiqueAnalyzerProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    preparedSampleRate = sampleRate;
    analysisBuffer.fill(0.0f);
    fftData.fill(0.0f);
    smoothedSpectrum.fill(-96.0f);
    for (auto& value : spectrumData)
        value.store(-96.0f, std::memory_order_relaxed);
    analysisWritePos = 0;
    samplesUntilNextFFT = fftHopSize;
    analysisFrameFifo.reset();
}

bool MusiqueAnalyzerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MusiqueAnalyzerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);

    const bool bypass = parameters.getRawParameterValue("bypass")->load() > 0.5f;
    const float outGainDb = parameters.getRawParameterValue("output")->load();
    const float outGain = std::pow(10.0f, outGainDb / 20.0f);
    const float mix = parameters.getRawParameterValue("mix")->load() / 100.0f;
    const bool frozen = parameters.getRawParameterValue("freeze")->load() > 0.5f;
    const bool analyzeOutput = parameters.getRawParameterValue("analysis_output")->load() > 0.5f;

    if (! frozen && ! analyzeOutput)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float left = buffer.getSample(0, sample);
            const float right = buffer.getSample(buffer.getNumChannels() > 1 ? 1 : 0, sample);
            pushNextSampleIntoAnalysisBuffer(0.5f * (left + right));
        }
    }

    if (! bypass && (outGain != 1.0f || mix != 1.0f))
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                float dry = channelData[sample];
                float wet = dry * outGain;
                channelData[sample] = dry + mix * (wet - dry);
            }
        }
    }

    if (bypass && outGain != 1.0f)
        buffer.applyGain(outGain);

    if (! frozen && analyzeOutput)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float left = buffer.getSample(0, sample);
            const float right = buffer.getSample(buffer.getNumChannels() > 1 ? 1 : 0, sample);
            pushNextSampleIntoAnalysisBuffer(0.5f * (left + right));
        }
    }

    visualState.captureOutput(buffer);
}

void MusiqueAnalyzerProcessor::pushNextSampleIntoAnalysisBuffer(float sample) noexcept
{
    analysisBuffer[(size_t) analysisWritePos] = sample;
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

    auto& frame = analysisFrames[(size_t) start1];
    for (int sample = 0; sample < fftSize; ++sample)
        frame[(size_t) sample] = analysisBuffer[(size_t) ((analysisWritePos + sample) % fftSize)];

    analysisFrameFifo.finishedWrite(1);
    notify();
}

void MusiqueAnalyzerProcessor::updateSpectrum(const float* frameData) noexcept
{
    if (preparedSampleRate <= 0.0)
        return;

    for (int sample = 0; sample < fftSize; ++sample)
        fftData[(size_t) sample] = frameData[(size_t) sample];
    std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);

    fftWindow.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

    const float smoothing = parameters.getRawParameterValue("smoothing")->load();
    const float tilt = parameters.getRawParameterValue("tilt")->load();
    const float resolution = parameters.getRawParameterValue("resolution")->load();
    const float speed = parameters.getRawParameterValue("speed")->load();

    const float smoothingFactor = juce::jlimit(0.10f, 0.98f,
        juce::jmap(smoothing, 0.0f, 1.0f, 0.15f, 0.94f)
            * juce::jmap(speed, 0.1f, 10.0f, 1.10f, 0.72f));
    const float averagingWidth = juce::jmap(resolution, 1.0f, 4.0f, 0.040f, 0.010f);
    const float nyquist = (float) (preparedSampleRate * 0.5);
    const int fftLimit = fftSize / 2;
    const float tiltCentered = tilt - 3.0f;
    constexpr float magnitudeNormalisation = 2.0f / (float) fftSize;

    for (size_t index = 0; index < spectrumBinCount; ++index)
    {
        const float norm = (float) index / (float) (spectrumBinCount - 1);
        const float minNorm = juce::jlimit(0.0f, 1.0f, norm - averagingWidth);
        const float maxNorm = juce::jlimit(0.0f, 1.0f, norm + averagingWidth);
        const float freqA = 20.0f * std::pow(1000.0f, minNorm);
        const float freqB = 20.0f * std::pow(1000.0f, maxNorm);
        const int startBin = juce::jlimit(1, fftLimit - 1, (int) std::floor((freqA / nyquist) * (float) fftLimit));
        const int endBin = juce::jlimit(startBin, fftLimit - 1, (int) std::ceil((freqB / nyquist) * (float) fftLimit));

        float magnitude = 0.0f;
        for (int bin = startBin; bin <= endBin; ++bin)
            magnitude += fftData[(size_t) bin];

        magnitude /= (float) juce::jmax(1, endBin - startBin + 1);

        const float centerFreq = std::sqrt(freqA * freqB);
        const float octaveFrom1k = std::log2(juce::jmax(20.0f, centerFreq) / 1000.0f);
        float dB = juce::Decibels::gainToDecibels(magnitude * magnitudeNormalisation, -120.0f);
        dB += tiltCentered * octaveFrom1k * 0.78f;
        dB = juce::jlimit(-120.0f, 24.0f, dB);

        smoothedSpectrum[index] = smoothedSpectrum[index] * smoothingFactor + dB * (1.0f - smoothingFactor);
        spectrumData[index].store(smoothedSpectrum[index], std::memory_order_relaxed);
    }
}

void MusiqueAnalyzerProcessor::run()
{
    while (! threadShouldExit())
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        analysisFrameFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 <= 0)
        {
            wait(40);
            continue;
        }

        updateSpectrum(analysisFrames[(size_t) start1].data());
        analysisFrameFifo.finishedRead(1);
    }
}

void MusiqueAnalyzerProcessor::getSpectrumSnapshot(std::array<float, spectrumBinCount>& destination) const noexcept
{
    for (size_t index = 0; index < spectrumBinCount; ++index)
        destination[index] = spectrumData[index].load(std::memory_order_relaxed);
}

juce::AudioProcessorEditor* MusiqueAnalyzerProcessor::createEditor()
{
    return new MusiqueAnalyzerEditor(*this);
}

void MusiqueAnalyzerProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MusiqueAnalyzerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueAnalyzerProcessor();
}
