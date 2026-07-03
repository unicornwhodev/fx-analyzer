#include "PluginProcessor.h"
#include "FXComponents.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace
{
struct Runner
{
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const std::string& name)
    {
        ++checks;
        if (condition)
        {
            std::cout << "[PASS] " << name << '\n';
            return;
        }

        ++failures;
        std::cout << "[FAIL] " << name << '\n';
    }
};

using ProcessorPtr = std::unique_ptr<MusiqueAnalyzerProcessor>;

void setParameter(MusiqueAnalyzerProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    if (parameter == nullptr)
    {
        std::cerr << "Missing parameter: " << id << '\n';
        std::exit(2);
    }

    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameterValue(MusiqueAnalyzerProcessor& processor, const juce::String& id)
{
    if (auto* raw = processor.getAPVTS().getRawParameterValue(id))
        return raw->load();

    std::cerr << "Missing parameter: " << id << '\n';
    std::exit(2);
}

ProcessorPtr makeProcessor(int numInputs, int numOutputs, double sampleRate, int maximumBlockSize)
{
    auto processor = std::make_unique<MusiqueAnalyzerProcessor>();
    processor->setPlayConfigDetails(numInputs, numOutputs, sampleRate, maximumBlockSize);
    processor->prepareToPlay(sampleRate, maximumBlockSize);
    return processor;
}

juce::AudioBuffer<float> makeStereoSine(int samples,
                                        double sampleRate,
                                        double frequency,
                                        float amplitude = 0.2f,
                                        double rightPhase = 0.0)
{
    juce::AudioBuffer<float> buffer(2, samples);
    constexpr double pi = 3.14159265358979323846;
    for (int index = 0; index < samples; ++index)
    {
        const double t = (double) index / sampleRate;
        buffer.setSample(0, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t)));
        buffer.setSample(1, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t + rightPhase)));
    }
    return buffer;
}

juce::AudioBuffer<float> makeMonoSine(int samples, double sampleRate, double frequency, float amplitude = 0.2f)
{
    juce::AudioBuffer<float> buffer(1, samples);
    constexpr double pi = 3.14159265358979323846;
    for (int index = 0; index < samples; ++index)
    {
        const double t = (double) index / sampleRate;
        buffer.setSample(0, index, (float) (amplitude * std::sin(2.0 * pi * frequency * t)));
    }
    return buffer;
}

bool isFiniteBuffer(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

float differenceEnergy(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const int channels = juce::jmin(a.getNumChannels(), b.getNumChannels());
    const int samples = juce::jmin(a.getNumSamples(), b.getNumSamples());
    float sum = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            sum += std::abs(a.getSample(channel, sample) - b.getSample(channel, sample));
    return sum / (float) juce::jmax(1, channels * samples);
}

void process(MusiqueAnalyzerProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

void processRepeated(MusiqueAnalyzerProcessor& processor, const juce::AudioBuffer<float>& templateBuffer, int blocks)
{
    for (int block = 0; block < blocks; ++block)
    {
        auto buffer = templateBuffer;
        process(processor, buffer);
    }
}

template <typename Predicate>
bool waitForCondition(Predicate&& predicate, int attempts = 24, int sleepMs = 10)
{
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        if (predicate())
            return true;
        juce::Thread::sleep(sleepMs);
    }

    return predicate();
}

float maxSpectrumDb(MusiqueAnalyzerProcessor& processor)
{
    MusiqueAnalyzerProcessor::SpectrumArray primary {};
    processor.getSpectrumSnapshot(primary);
    float result = -120.0f;
    for (float value : primary)
        result = juce::jmax(result, value);
    return result;
}

int maxSpectrumIndex(MusiqueAnalyzerProcessor& processor)
{
    MusiqueAnalyzerProcessor::SpectrumArray primary {};
    processor.getSpectrumSnapshot(primary);
    int bestIndex = 0;
    float bestValue = primary[0];
    for (int index = 1; index < (int) primary.size(); ++index)
    {
        if (primary[(size_t) index] > bestValue)
        {
            bestValue = primary[(size_t) index];
            bestIndex = index;
        }
    }
    return bestIndex;
}

float spectrumIndexToFrequency(int index)
{
    const float norm = MusiqueAnalyzerProcessor::spectrumBinCount > 1
        ? (float) index / (float) (MusiqueAnalyzerProcessor::spectrumBinCount - 1)
        : 0.0f;
    return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, norm));
}

juce::ValueTree copyStateTree(MusiqueAnalyzerProcessor& processor)
{
    juce::MemoryBlock stateData;
    processor.getStateInformation(stateData);
    auto xml = juce::AudioProcessor::getXmlFromBinary(stateData.getData(), (int) stateData.getSize());
    if (xml == nullptr)
    {
        std::cerr << "Failed to decode state XML\n";
        std::exit(2);
    }

    return juce::ValueTree::fromXml(*xml);
}

void loadStateTree(MusiqueAnalyzerProcessor& processor, const juce::ValueTree& state)
{
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        std::cerr << "Failed to encode state XML\n";
        std::exit(2);
    }

    juce::MemoryBlock stateData;
    juce::AudioProcessor::copyXmlToBinary(*xml, stateData);
    processor.setStateInformation(stateData.getData(), (int) stateData.getSize());
}

void removeParameterFromState(juce::ValueTree& state, const juce::String& id)
{
    for (int index = state.getNumChildren() - 1; index >= 0; --index)
    {
        auto child = state.getChild(index);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
            state.removeChild(index, nullptr);
    }
}

juce::File findFactoryBankForTests()
{
    auto dir = juce::File::getCurrentWorkingDirectory();
    for (int depth = 0; depth < 8; ++depth)
    {
        const std::array<juce::File, 3> candidates {
            dir.getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("fx-analyzer").getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("FX").getChildFile("fx-analyzer").getChildFile("Presets").getChildFile("factory_bank.json")
        };

        for (const auto& candidate : candidates)
            if (candidate.existsAsFile())
                return candidate;

        auto parent = dir.getParentDirectory();
        if (parent == dir)
            break;
        dir = parent;
    }

    return {};
}

juce::Array<juce::var> loadFactoryPresetsForTests(Runner& runner)
{
    const auto file = findFactoryBankForTests();
    runner.expect(file.existsAsFile(), "factory preset bank is discoverable");
    if (!file.existsAsFile())
        return {};

    auto presets = fx::preset::loadPresetsFromBank(file);
    for (auto& preset : presets)
        MusiqueAnalyzerProcessor::normalisePresetObject(preset);
    return presets;
}
}

int main()
{
    Runner runner;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "analysis_output", 0.0f);
        setParameter(*processor, "smoothing", 0.77f);

        auto state = copyStateTree(*processor);
        removeParameterFromState(state, "view");
        removeParameterFromState(state, "source");
        loadStateTree(*processor, state);

        runner.expect((int) std::round(getParameterValue(*processor, "view")) == 0, "legacy state defaults to spectrum view");
        runner.expect((int) std::round(getParameterValue(*processor, "source")) == 0, "legacy state derives source from analysis_output");
        runner.expect(std::abs(getParameterValue(*processor, "smoothing") - 0.77f) < 0.001f, "legacy state preserves historical fields");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);

        juce::DynamicObject::Ptr object = new juce::DynamicObject();
        object->setProperty("name", "Legacy");
        object->setProperty("analysis_output", true);
        object->setProperty("range", 48.0f);
        object->setProperty("mix", 91.0f);
        juce::var preset(object.get());
        MusiqueAnalyzerProcessor::normalisePresetObject(preset);
        fx::preset::applyToAPVTS(processor->getAPVTS(), preset);
        processor->postExternalStateChange();

        runner.expect((int) std::round(getParameterValue(*processor, "view")) == 0, "legacy preset opens on spectrum");
        runner.expect((int) std::round(getParameterValue(*processor, "source")) == 1, "legacy preset keeps output source intent");
        runner.expect(std::abs(getParameterValue(*processor, "mix") - 91.0f) < 0.001f, "legacy preset preserves mix");
    }

    {
        auto source = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*source, "view", 4.0f);
        setParameter(*source, "source", 0.0f);
        setParameter(*source, "channel_mode", 3.0f);
        setParameter(*source, "hold", 1.0f);
        auto state = copyStateTree(*source);

        auto target = makeProcessor(2, 2, sampleRate, blockSize);
        loadStateTree(*target, state);

        runner.expect((int) std::round(getParameterValue(*target, "view")) == 4, "round-trip state preserves view");
        runner.expect((int) std::round(getParameterValue(*target, "source")) == 0, "round-trip state preserves source");
        runner.expect((int) std::round(getParameterValue(*target, "channel_mode")) == 3, "round-trip state preserves channel mode");
        runner.expect(getParameterValue(*target, "hold") > 0.5f, "round-trip state preserves hold");
    }

    {
        auto processor = makeProcessor(1, 1, sampleRate, blockSize);

        juce::AudioProcessor::BusesLayout monoLayout;
        monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
        monoLayout.outputBuses.add(juce::AudioChannelSet::mono());
        juce::AudioProcessor::BusesLayout stereoLayout;
        stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
        stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());

        runner.expect(processor->isBusesLayoutSupported(monoLayout), "mono->mono layout is supported");
        runner.expect(processor->isBusesLayoutSupported(stereoLayout), "stereo->stereo layout is supported");

        auto monoBuffer = makeMonoSine(blockSize, sampleRate, 330.0);
        process(*processor, monoBuffer);
        runner.expect(isFiniteBuffer(monoBuffer), "mono processing remains finite");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "mix", 35.0f);
        setParameter(*processor, "output", 12.0f);
        setParameter(*processor, "bypass", 1.0f);

        auto buffer = makeStereoSine(blockSize, sampleRate, 440.0, 0.18f, 0.25);
        auto expected = buffer;
        process(*processor, buffer);
        runner.expect(differenceEnergy(buffer, expected) < 1.0e-6f, "bypass returns the dry signal unchanged");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 0.0f);
        setParameter(*processor, "source", 0.0f);
        setParameter(*processor, "output", 0.0f);
        processor->postExternalStateChange();

        const auto sine = makeStereoSine(blockSize, sampleRate, 1000.0, 0.22f);
        processRepeated(*processor, sine, 20);
        runner.expect(waitForCondition([&]() { return maxSpectrumDb(*processor) > -40.0f; }), "spectrum worker produces a measurable peak");
        const float peakFrequency = spectrumIndexToFrequency(maxSpectrumIndex(*processor));
        runner.expect(peakFrequency > 700.0f && peakFrequency < 1400.0f, "1 kHz sine lands on a coherent spectrum bin");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 0.0f);
        setParameter(*processor, "hold", 0.0f);
        processor->postExternalStateChange();

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 1000.0, 0.22f), 20);
        runner.expect(waitForCondition([&]() { return maxSpectrumDb(*processor) > -40.0f; }), "spectrum is active before hold");
        const int heldIndex = maxSpectrumIndex(*processor);

        setParameter(*processor, "hold", 1.0f);
        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 220.0, 0.22f), 20);
        juce::Thread::sleep(40);
        const int heldAfter = maxSpectrumIndex(*processor);
        runner.expect(std::abs(heldAfter - heldIndex) <= 1, "hold freezes the active spectrum display");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 1.0f);
        setParameter(*processor, "freeze", 0.0f);
        processor->postExternalStateChange();

        const int initialCursor = processor->getMonitorSnapshot().spectrogramCursor;
        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 880.0, 0.22f), 20);
        runner.expect(waitForCondition([&]() { return processor->getMonitorSnapshot().spectrogramCursor != initialCursor; }), "spectrogram advances while live");

        setParameter(*processor, "freeze", 1.0f);
        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 220.0, 0.22f), 20);
        juce::Thread::sleep(60);
        const int frozenCursorA = processor->getMonitorSnapshot().spectrogramCursor;
        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 330.0, 0.22f), 20);
        juce::Thread::sleep(60);
        const int frozenCursorB = processor->getMonitorSnapshot().spectrogramCursor;
        runner.expect(frozenCursorA == frozenCursorB, "freeze stops spectrogram scrolling");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 0.0f);
        setParameter(*processor, "source", 0.0f);
        setParameter(*processor, "analysis_output", 0.0f);
        setParameter(*processor, "output", 12.0f);
        processor->postExternalStateChange();

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 440.0, 0.18f), 20);
        runner.expect(waitForCondition([&]() { return maxSpectrumDb(*processor) > -50.0f; }), "input-source spectrum captured");
        const float inputPeakDb = maxSpectrumDb(*processor);

        setParameter(*processor, "source", 1.0f);
        setParameter(*processor, "analysis_output", 1.0f);
        processor->postExternalStateChange();
        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 440.0, 0.18f), 20);
        runner.expect(waitForCondition([&]() { return maxSpectrumDb(*processor) > inputPeakDb + 6.0f; }), "output source capture reflects post-gain level");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 2.0f);
        setParameter(*processor, "scope_window", 120.0f);
        setParameter(*processor, "scope_zoom", 1.6f);
        processor->postExternalStateChange();

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 330.0, 0.22f, 0.25), 12);
        runner.expect(waitForCondition([&]()
        {
            MusiqueAnalyzerProcessor::ScopeArray left {};
            MusiqueAnalyzerProcessor::ScopeArray right {};
            processor->getScopeSnapshot(left, right);
            float magnitude = 0.0f;
            for (size_t index = 0; index < left.size(); ++index)
                magnitude = juce::jmax(magnitude, std::abs(left[index]), std::abs(right[index]));
            return magnitude > 0.05f;
        }), "oscilloscope snapshot becomes active");

        MusiqueAnalyzerProcessor::ScopeArray left {};
        MusiqueAnalyzerProcessor::ScopeArray right {};
        processor->getScopeSnapshot(left, right);
        bool finite = true;
        for (size_t index = 0; index < left.size(); ++index)
            finite = finite && std::isfinite(left[index]) && std::isfinite(right[index]);
        runner.expect(finite, "oscilloscope snapshot remains finite");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 3.0f);
        processor->postExternalStateChange();

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 220.0, 0.25f), 40);
        runner.expect(waitForCondition([&]()
        {
            const auto snapshot = processor->getMonitorSnapshot().loudness;
            return snapshot.momentaryLufs > -60.0f && snapshot.truePeakDb > -40.0f;
        }), "loudness monitor returns plausible bounded values");

        const auto snapshot = processor->getMonitorSnapshot().loudness;
        runner.expect(snapshot.integratedLufs <= snapshot.truePeakDb + 20.0f, "integrated loudness stays numerically plausible");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        setParameter(*processor, "view", 4.0f);
        processor->postExternalStateChange();

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 220.0, 0.2f), 12);
        runner.expect(waitForCondition([&]() { return processor->getMonitorSnapshot().stereoField.correlation > 0.95f; }), "duplicated mono reports near-perfect correlation");

        processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 220.0, 0.2f, juce::MathConstants<double>::halfPi), 12);
        runner.expect(waitForCondition([&]() { return processor->getMonitorSnapshot().stereoField.correlation < 0.85f; }), "wider stereo content lowers correlation");
    }

    {
        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            setParameter(*processor, "view", (float) (iteration % MusiqueAnalyzerProcessor::numViews));
            auto state = copyStateTree(*processor);
            loadStateTree(*processor, state);
            processor->prepareToPlay(sampleRate, blockSize);
            processRepeated(*processor, makeStereoSine(blockSize, sampleRate, 330.0 + iteration * 110.0, 0.18f), 8);
        }

        runner.expect(waitForCondition([&]() { return maxSpectrumDb(*processor) > -90.0f; }), "prepare/state resets keep the analyzer alive");
    }

    {
        auto presets = loadFactoryPresetsForTests(runner);
        runner.expect(presets.size() == 14, "factory bank exposes 14 presets");

        auto processor = makeProcessor(2, 2, sampleRate, blockSize);
        for (int index = 0; index < presets.size(); ++index)
        {
            auto preset = presets.getReference(index);
            fx::preset::applyToAPVTS(processor->getAPVTS(), preset);
            processor->postExternalStateChange();
            auto buffer = makeStereoSine(blockSize, sampleRate, 330.0, 0.16f, 0.15);
            process(*processor, buffer);
            runner.expect(isFiniteBuffer(buffer), "factory preset " + std::to_string(index + 1) + " processes without NaN");
        }
    }

    std::cout << "Checks: " << runner.checks << ", failures: " << runner.failures << '\n';
    return runner.failures == 0 ? 0 : 1;
}
