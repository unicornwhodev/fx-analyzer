#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "FXAudioVisualState.h"

struct LoudnessSnapshot
{
    float momentaryLufs = -120.0f;
    float shortTermLufs = -120.0f;
    float integratedLufs = -120.0f;
    float truePeakDb = -120.0f;
};

struct AnalyzerMonitorSnapshot
{
    LoudnessSnapshot loudness;
    fx::StereoFieldSnapshot stereoField;
    int view = 0;
    int source = 0;
    int channelMode = 0;
    int spectrogramCursor = 0;
    bool frozen = false;
    bool hold = false;
};

class MusiqueAnalyzerProcessor : public juce::AudioProcessor, private juce::Thread
{
public:
    enum ViewIndex
    {
        spectrumView = 0,
        spectrogramView,
        oscilloscopeView,
        loudnessView,
        stereoView,
        numViews
    };

    enum SourceIndex
    {
        sourceInput = 0,
        sourceOutput
    };

    enum ChannelModeIndex
    {
        sumMode = 0,
        stereoOverlayMode,
        midMode,
        sideMode,
        numChannelModes
    };

    static constexpr size_t spectrumBinCount = 96;
    static constexpr size_t spectrogramColumnCount = 96;
    static constexpr size_t scopeSnapshotSize = 512;

    using SpectrumArray = std::array<float, spectrumBinCount>;
    using SpectrogramArray = std::array<float, spectrumBinCount * spectrogramColumnCount>;
    using ScopeArray = std::array<float, scopeSnapshotSize>;

    MusiqueAnalyzerProcessor();
    ~MusiqueAnalyzerProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray getAllParameterIds();
    static void normalisePresetObject(juce::var& preset);

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override
    {
#if MUSIQUE_ANALYZER_DSP_TESTS
        return "Musique Analyzer";
#else
        return JucePlugin_Name;
#endif
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }

    void getSpectrumSnapshot(SpectrumArray& primary,
                             SpectrumArray* secondary = nullptr,
                             SpectrumArray* peaks = nullptr) const noexcept;
    void getSpectrogramSnapshot(SpectrogramArray& destination, int& cursor) const noexcept;
    void getScopeSnapshot(ScopeArray& left, ScopeArray& right) const noexcept;
    AnalyzerMonitorSnapshot getMonitorSnapshot() const noexcept;

    void postExternalStateChange();
    void clearSpectrogramHistory();
    void resetLoudnessHistory();

private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int fftHopSize = fftSize / 4;
    static constexpr int analysisQueueSize = 8;
    static constexpr int scopeRingSize = 65536;

    struct AnalyzerParameters
    {
        int view = spectrumView;
        int source = sourceOutput;
        int channelMode = sumMode;
        bool bypass = false;
        bool freeze = false;
        bool hold = false;
        bool legacyAnalysisOutput = true;
        float smoothing = 0.5f;
        float tilt = 3.0f;
        float range = 60.0f;
        float offset = 0.0f;
        float resolution = 2.0f;
        float speed = 1.0f;
        float mix = 100.0f;
        float outputDb = 0.0f;
        float specContrast = 50.0f;
        float specFloor = -84.0f;
        float specScroll = 1.0f;
        int specPalette = 0;
        float scopeWindowMs = 220.0f;
        float scopeZoom = 1.0f;
        int scopeTrigger = 0;
        float scopePersist = 30.0f;
        float loudHistory = 8.0f;
        float loudHold = 40.0f;
        int loudScale = 0;
        float loudGate = -70.0f;
        float stereoZoom = 1.0f;
        float stereoDecay = 35.0f;
        float stereoFocus = 50.0f;
        float stereoHold = 40.0f;
        float stereoBalance = 0.0f;
    };

    static void ensureStateParamValue(juce::ValueTree& state, const char* paramId, const juce::var& value);
    static juce::var readStateParamValue(const juce::ValueTree& state, const char* paramId, const juce::var& fallback);
    static void normaliseStateTree(juce::ValueTree& state);

    AnalyzerParameters buildParameterSnapshot() const;
    void resetAnalysisTransport();
    void pushNextAnalysisSample(float left, float right) noexcept;
    void queueAnalysisFrame() noexcept;
    void processAnalysisFrame(const float* leftFrame, const float* rightFrame) noexcept;
    void captureAnalysisSource(const juce::AudioBuffer<float>& buffer, const AnalyzerParameters& params) noexcept;
    void updateScopeSnapshot(const AnalyzerParameters& params) noexcept;
    void updateLoudness(const juce::AudioBuffer<float>& buffer, const AnalyzerParameters& params) noexcept;
    void syncLegacyStateFromSource(const AnalyzerParameters& params);
    void run() override;

    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    fx::StereoFieldState stereoFieldState;

    double preparedSampleRate = 44100.0;
    int maximumBlockSize = 0;

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> fftWindow { fftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::array<float, fftSize> analysisBufferLeft {};
    std::array<float, fftSize> analysisBufferRight {};
    std::array<std::array<float, fftSize>, analysisQueueSize> analysisFramesLeft {};
    std::array<std::array<float, fftSize>, analysisQueueSize> analysisFramesRight {};
    juce::AbstractFifo analysisFrameFifo { analysisQueueSize };
    std::array<float, 2 * fftSize> fftScratchPrimary {};
    std::array<float, 2 * fftSize> fftScratchSecondary {};
    SpectrumArray smoothedPrimary {};
    SpectrumArray smoothedSecondary {};
    SpectrumArray peakHoldPrimary {};
    SpectrumArray peakHoldSecondary {};
    std::array<std::atomic<float>, spectrumBinCount> primarySpectrumData {};
    std::array<std::atomic<float>, spectrumBinCount> secondarySpectrumData {};
    std::array<std::atomic<float>, spectrumBinCount> primaryPeakData {};
    std::array<std::atomic<float>, spectrumBinCount> secondaryPeakData {};

    SpectrogramArray spectrogramMatrix {};
    std::atomic<uint32_t> spectrogramGeneration { 0 };
    std::atomic<int> spectrogramCursor { 0 };

    std::array<float, scopeRingSize> scopeRingLeft {};
    std::array<float, scopeRingSize> scopeRingRight {};
    ScopeArray scopeSnapshotLeft {};
    ScopeArray scopeSnapshotRight {};
    std::atomic<uint32_t> scopeGeneration { 0 };

    std::atomic<float> momentaryLufs { -120.0f };
    std::atomic<float> shortTermLufs { -120.0f };
    std::atomic<float> integratedLufs { -120.0f };
    std::atomic<float> truePeakDb { -120.0f };

    std::atomic<int> currentViewAtomic { spectrumView };
    std::atomic<int> currentSourceAtomic { sourceOutput };
    std::atomic<int> currentChannelModeAtomic { sumMode };
    std::atomic<bool> freezeStateAtomic { false };
    std::atomic<bool> holdStateAtomic { false };

    juce::dsp::IIR::Filter<float> loudHighPass;
    juce::dsp::IIR::Filter<float> loudShelf;
    float loudnessPrevSample = 0.0f;
    double momentaryEnergyState = 0.0;
    double shortTermEnergyState = 0.0;
    double integratedEnergySum = 0.0;
    double integratedSampleCount = 0.0;

    int analysisWritePos = 0;
    int samplesUntilNextFFT = fftHopSize;
    int scopeWritePos = 0;

    std::atomic<bool> analysisSuspended { false };
    std::atomic<bool> dropQueuedAnalysisFrames { false };
    std::atomic<int> audioCallbacksInFlight { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueAnalyzerProcessor)
};
