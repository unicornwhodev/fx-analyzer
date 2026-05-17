#pragma once
#include <JuceHeader.h>
#include "FXAudioVisualState.h"

class MusiqueAnalyzerProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    static constexpr size_t spectrumBinCount = 96;

    MusiqueAnalyzerProcessor();
    ~MusiqueAnalyzerProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Musique Analyzer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    void getSpectrumSnapshot(std::array<float, spectrumBinCount>& destination) const noexcept;

private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int fftHopSize = fftSize / 4;
    static constexpr int analysisQueueSize = 4;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushNextSampleIntoAnalysisBuffer(float sample) noexcept;
    void queueAnalysisFrame() noexcept;
    void updateSpectrum(const float* frameData) noexcept;
    void run() override;

    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    double preparedSampleRate = 44100.0;
    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { fftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::array<float, fftSize> analysisBuffer {};
    std::array<float, 2 * fftSize> fftData {};
    std::array<std::array<float, fftSize>, analysisQueueSize> analysisFrames {};
    juce::AbstractFifo analysisFrameFifo { analysisQueueSize };
    std::array<float, spectrumBinCount> smoothedSpectrum {};
    std::array<std::atomic<float>, spectrumBinCount> spectrumData {};
    int analysisWritePos = 0;
    int samplesUntilNextFFT = fftHopSize;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueAnalyzerProcessor)
};
