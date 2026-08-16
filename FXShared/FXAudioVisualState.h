#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

namespace fx
{
struct StereoLevels { float left = 0.0f; float right = 0.0f; };

class AudioVisualState
{
public:
    void captureInput(const juce::AudioBuffer<float>& buffer) noexcept { capture(buffer, inputLeft, inputRight); }
    void captureOutput(const juce::AudioBuffer<float>& buffer) noexcept { capture(buffer, outputLeft, outputRight); }
    StereoLevels getInputLevels() const noexcept { return { inputLeft.load(std::memory_order_relaxed), inputRight.load(std::memory_order_relaxed) }; }
    StereoLevels getOutputLevels() const noexcept { return { outputLeft.load(std::memory_order_relaxed), outputRight.load(std::memory_order_relaxed) }; }
private:
    static float computeLevel(const juce::AudioBuffer<float>& buffer, int channel) noexcept
    {
        if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0) return 0.0f;
        const float peak = buffer.getMagnitude(channel, 0, buffer.getNumSamples());
        const float rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
        const float amplitude = juce::jmax(peak * 0.72f, rms * 1.35f);
        const float dB = juce::Decibels::gainToDecibels(amplitude, -60.0f);
        return juce::jlimit(0.0f, 1.0f, juce::jmap(dB, -60.0f, 0.0f, 0.0f, 1.0f));
    }
    static void capture(const juce::AudioBuffer<float>& buffer, std::atomic<float>& leftStore, std::atomic<float>& rightStore) noexcept
    {
        const int rightChannel = buffer.getNumChannels() > 1 ? 1 : 0;
        leftStore.store(computeLevel(buffer, 0), std::memory_order_relaxed);
        rightStore.store(computeLevel(buffer, rightChannel), std::memory_order_relaxed);
    }
    std::atomic<float> inputLeft { 0.0f }, inputRight { 0.0f }, outputLeft { 0.0f }, outputRight { 0.0f };
};

struct StereoFieldSnapshot { float correlation = 1.0f; float width = 0.0f; float balance = 0.0f; };
class StereoFieldState
{
public:
    void capture(const juce::AudioBuffer<float>& buffer) noexcept
    {
        if (buffer.getNumChannels() < 2 || buffer.getNumSamples() <= 0)
        {
            correlation.store(1.0f, std::memory_order_relaxed);
            width.store(0.0f, std::memory_order_relaxed);
            balance.store(0.0f, std::memory_order_relaxed);
            return;
        }
        double leftEnergy = 0.0, rightEnergy = 0.0, crossEnergy = 0.0, midEnergy = 0.0, sideEnergy = 0.0;
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const float left = buffer.getSample(0, sample), right = buffer.getSample(1, sample);
            const float mid = 0.5f * (left + right), side = 0.5f * (left - right);
            leftEnergy += (double)left * left; rightEnergy += (double)right * right; crossEnergy += (double)left * right;
            midEnergy += (double)mid * mid; sideEnergy += (double)side * side;
        }
        const double denom = std::sqrt(leftEnergy * rightEnergy);
        correlation.store(denom > 1.0e-9 ? (float)juce::jlimit(-1.0, 1.0, crossEnergy / denom) : 1.0f, std::memory_order_relaxed);
        width.store((float)juce::jlimit(0.0, 1.5, sideEnergy / juce::jmax(1.0e-9, midEnergy)), std::memory_order_relaxed);
        balance.store((float)juce::jlimit(-1.0, 1.0, (rightEnergy - leftEnergy) / juce::jmax(1.0e-9, leftEnergy + rightEnergy)), std::memory_order_relaxed);
    }
    StereoFieldSnapshot getSnapshot() const noexcept
    {
        return { correlation.load(std::memory_order_relaxed), width.load(std::memory_order_relaxed), balance.load(std::memory_order_relaxed) };
    }
private:
    std::atomic<float> correlation { 1.0f }, width { 0.0f }, balance { 0.0f };
};
}
