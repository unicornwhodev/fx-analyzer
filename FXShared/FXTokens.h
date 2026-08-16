#pragma once
#include <JuceHeader.h>

namespace fx
{
namespace dim
{
constexpr int appW = 1024, appH = 600;
constexpr int headerH = 60, presetBarH = 50, visualH = 300, controlsH = 150, footerH = 40;
constexpr int baseUnit = 8, sectionPad = 16, knobSpacing = 24, labelSpacing = 6;
constexpr int knobDiam = 68, knobRing = 6;
constexpr int btnH = 28, btnR = 6, btnPadX = 12;
constexpr int meterW = 10;
constexpr int ledSize = 10;
}
namespace col
{
static const juce::Colour bg { 0xff111214 };
static const juce::Colour surfPrimary { 0xff1A1C20 };
static const juce::Colour surfSecondary { 0xff22252B };
static const juce::Colour surfTertiary { 0xff2A2D32 };
static const juce::Colour border { 0xff2F333A };
static const juce::Colour divider { 0xff2A2E35 };
static const juce::Colour textPrimary { 0xffF2F2F2 };
static const juce::Colour textSecondary { 0xffB8BDC6 };
static const juce::Colour textMuted { 0xff7A808A };
static const juce::Colour disabled { 0xff4A4F58 };
static const juce::Colour graphBg { 0xff16181C };
static const juce::Colour gridMajor { 0xff2C3037 };
static const juce::Colour gridMinor { 0xff22262C };
static const juce::Colour meterBg { 0xff1C1F24 };
static const juce::Colour meterLow { 0xff3AA8FF };
static const juce::Colour meterMid { 0xffFFD84A };
static const juce::Colour meterHigh { 0xffFF4A4A };
static const juce::Colour meterClip { 0xffFF0000 };
static const juce::Colour ledOff { 0xff3A3F47 };
}
namespace accent
{
static const juce::Colour delay { 0xffFF8C32 };
static const juce::Colour reverb { 0xff3AA8FF };
static const juce::Colour filter { 0xffFFD84A };
static const juce::Colour eq { 0xff4AD6FF };
static const juce::Colour compressor { 0xffFF4A4A };
static const juce::Colour distortion { 0xffC9372C };
static const juce::Colour pitch { 0xff9B6CFF };
static const juce::Colour creative { 0xff56D6C4 };
}
namespace font
{
constexpr float header = 20.0f;
constexpr float preset = 14.0f;
constexpr float label = 12.0f;
constexpr float value = 11.0f;
constexpr float footer = 10.0f;
}
namespace anim
{
constexpr int knobResponseMs = 60;
constexpr int bypassFadeMs = 80;
constexpr int meterDecayMs = 120;
constexpr int fftRefreshHz = 30;
}
}
