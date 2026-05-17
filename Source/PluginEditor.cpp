#include "PluginEditor.h"
#include "BinaryData.h"

MusiqueAnalyzerEditor::MusiqueAnalyzerEditor(MusiqueAnalyzerProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    // Header
    titleLabel.setText("ANALYZER", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    auto setupHdrBtn = [&](juce::TextButton& b, bool toggle = false) {
        b.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
        b.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
        if (toggle) b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    setupHdrBtn(bypassBtn, true);
    setupHdrBtn(freezeBtn, true);
    setupHdrBtn(sourceBtn, true);
    setupHdrBtn(settingsBtn);
    fx::ui::markUnsupportedControl(settingsBtn);
    sourceBtn.setTooltip("Choose whether the spectrum follows the plugin input or processed output");
    freezeBtn.setTooltip("Freeze spectrum updates while meters continue following live audio");

    // Preset bar
    setupHdrBtn(prevBtn); setupHdrBtn(nextBtn); setupHdrBtn(saveBtn); setupHdrBtn(abBtn);
    addAndMakeVisible(presetBox);

    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-analyzer"));
    if (presets->isEmpty()) { presetBox.addItem("Init", 1); presetBox.setSelectedId(1); }
    else
    {
        int id = 1;
        for (auto& pv : *presets)
            if (auto* o = pv.getDynamicObject())
                presetBox.addItem(o->getProperty("name").toString(), id++);
        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
        fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(0));
    }
    presetBox.onChange = [this] {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i < presets->size()) fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(i));
    };
    prevBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i > 0) presetBox.setSelectedItemIndex(i - 1); };
    nextBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i < presetBox.getNumItems() - 1) presetBox.setSelectedItemIndex(i + 1); };
    saveBtn.onClick = [this] {
        auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        juce::StringArray ids {"smoothing","tilt","range","offset","resolution","speed","analysis_output","mix","output","bypass","freeze"};
        if (fx::preset::saveUserPreset("fx-analyzer", name, ids, proc.getAPVTS()))
        {
            *presets = fx::preset::loadAllPresets("fx-analyzer");
            presetBox.clear();
            int id = 1;
            for (auto& pv : *presets)
                if (auto* o = pv.getDynamicObject()) presetBox.addItem(o->getProperty("name").toString(), id++);
            presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    // Knobs
    const char* labels[6] = {"SMOOTHING", "TILT", "RANGE", "OFFSET", "DETAIL", "RESPONSE"};
    for (int i = 0; i < 6; ++i)
    {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setText(labels[i], juce::dontSendNotification);
        knobLabels[i].setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::label).withStyle("Bold")));
        knobLabels[i].setJustificationType(juce::Justification::centred);
        knobLabels[i].setColour(juce::Label::textColourId, fx::col::textMuted);
        addAndMakeVisible(knobLabels[i]);
    }

    // Footer
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(mixSlider);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(outputSlider);
    activeLED.setAccent(fx::accent::filter);
    addAndMakeVisible(activeLED);
    versionLabel.setText("Musique Analyzer v1.0", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    // Attachments
    smoothAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "smoothing",  knobs[0]);
    tiltAtt   = std::make_unique<SliderAttach>(proc.getAPVTS(), "tilt",       knobs[1]);
    rangeAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "range",      knobs[2]);
    offsetAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "offset",     knobs[3]);
    resAtt    = std::make_unique<SliderAttach>(proc.getAPVTS(), "resolution", knobs[4]);
    speedAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "speed",      knobs[5]);
    mixAtt    = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix",        mixSlider);
    outAtt    = std::make_unique<SliderAttach>(proc.getAPVTS(), "output",     outputSlider);
    
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    freezeAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "freeze", freezeBtn);
    sourceAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "analysis_output", sourceBtn);

    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueAnalyzerEditor::~MusiqueAnalyzerEditor() { setLookAndFeel(nullptr); }

void MusiqueAnalyzerEditor::timerCallback()
{
    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);
    proc.getSpectrumSnapshot(spectrumValues);

    phase += 0.05f;
    if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
    
    const bool isBypassed = proc.getAPVTS().getRawParameterValue("bypass")->load() > 0.5f;
    const bool isFrozen = proc.getAPVTS().getRawParameterValue("freeze")->load() > 0.5f;
    const bool analyzeOutput = proc.getAPVTS().getRawParameterValue("analysis_output")->load() > 0.5f;
    const float trackedLevel = analyzeOutput
        ? juce::jmax(outputLevels.left, outputLevels.right)
        : juce::jmax(inputLevels.left, inputLevels.right);
    activeLED.setOn(!isFrozen && trackedLevel > 0.02f);
    sourceBtn.setButtonText(analyzeOutput ? "SOURCE OUT" : "SOURCE IN");
    sourceBtn.setColour(juce::TextButton::buttonColourId,
        analyzeOutput ? fx::accent::filter.withAlpha(0.18f) : fx::col::surfSecondary);
    sourceBtn.setColour(juce::TextButton::textColourOffId,
        analyzeOutput ? fx::accent::filter.brighter(0.2f) : fx::col::textPrimary);
    freezeBtn.setColour(juce::TextButton::buttonColourId,
        isFrozen ? fx::col::meterMid.withAlpha(0.18f) : fx::col::surfSecondary);
    freezeBtn.setColour(juce::TextButton::textColourOffId,
        isFrozen ? fx::col::meterMid.brighter(0.15f) : fx::col::textPrimary);
    
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueAnalyzerEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float cx = (float)area.getX();
    const float cy = (float)area.getY();
    const float pad = 24.0f;

    // Get parameters
    float rangeVal = 60.0f, offsetVal = 0.0f, resolutionVal = 2.0f, tiltVal = 3.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("range")) rangeVal = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("offset")) offsetVal = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("resolution")) resolutionVal = p->load();
    if (auto* p = proc.getAPVTS().getRawParameterValue("tilt")) tiltVal = p->load();
    
    const bool isFrozen = proc.getAPVTS().getRawParameterValue("freeze")->load() > 0.5f;
    const bool analyzeOutput = proc.getAPVTS().getRawParameterValue("analysis_output")->load() > 0.5f;
    const float tiltCentered = tiltVal - 3.0f;
    
    // Draw dB grid (horizontal lines)
    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    
    float maxDb = offsetVal;
    float minDb = offsetVal - rangeVal;
    
    for (int db = (int)minDb; db <= (int)maxDb; db += 12)
    {
        float norm = 1.0f - (float)(db - minDb) / rangeVal;
        float yPos = cy + pad + norm * (h - 2.0f * pad);
        
        if (yPos >= cy + pad && yPos <= cy + h - pad)
        {
            g.drawText(juce::String(db), (int)cx + 2, (int)(yPos - 5), 26, 10, juce::Justification::centredRight);
            g.setColour(fx::col::gridMinor);
            g.drawHorizontalLine((int)yPos, cx + pad + 4, cx + w - pad);
            g.setColour(fx::col::textMuted);
        }
    }

    // Draw Frequency grid (vertical lines)
    const float freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const char* freqLabels[] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
    for (int i = 0; i < 10; ++i)
    {
        float norm = std::log10(freqs[i] / 20.0f) / std::log10(20000.0f / 20.0f);
        float xPos = cx + pad + norm * (w - 2.0f * pad);
        g.setColour(fx::col::gridMinor);
        g.drawVerticalLine((int)xPos, cy + pad, cy + h - pad);
        g.setColour(fx::col::textMuted);
        g.drawText(freqLabels[i], (int)(xPos - 14), (int)(cy + h - pad + 4), 28, 12, juce::Justification::centred);
    }

    // Draw real analyzer spectrum
    juce::Path spectrumPath;
    juce::Path glowPath;
    bool started = false;
    const float lineThickness = juce::jmap(resolutionVal, 1.0f, 4.0f, 2.8f, 1.3f);

    for (size_t s = 0; s < spectrumValues.size(); ++s)
    {
        const float norm = (float)s / (float)(spectrumValues.size() - 1);
        float xPos = cx + pad + norm * (w - 2.0f * pad);

        const float displayDb = spectrumValues[s] + offsetVal;
        const float clampedNorm = juce::jlimit(0.0f, 1.0f, 1.0f - ((displayDb - minDb) / juce::jmax(1.0f, rangeVal)));
        const float yPos = cy + pad + clampedNorm * (h - 2.0f * pad);

        if (!started) { spectrumPath.startNewSubPath(xPos, yPos); started = true; }
        else spectrumPath.lineTo(xPos, yPos);
    }

    // Fill under curve
    {
        juce::Path fillPath(spectrumPath);
        fillPath.lineTo(cx + w - pad, cy + h - pad);
        fillPath.lineTo(cx + pad, cy + h - pad);
        fillPath.closeSubPath();
        
        juce::ColourGradient grad(fx::accent::filter.withAlpha(0.5f), cx, cy + pad,
                                  fx::accent::filter.withAlpha(0.0f), cx, cy + h - pad, false);
        g.setGradientFill(grad);
        g.fillPath(fillPath);
    }

    glowPath = spectrumPath;
    g.setColour(fx::accent::filter.withAlpha(0.16f));
    g.strokePath(glowPath, juce::PathStrokeType(lineThickness + 4.0f));

    g.setColour(fx::accent::filter.withAlpha(0.95f));
    g.strokePath(spectrumPath, juce::PathStrokeType(lineThickness));

    auto drawBadge = [&](juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(0.18f));
        g.fillRoundedRectangle(rect, 8.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(rect, 8.0f, 1.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
    };

    drawBadge({ cx + w - 286.0f, cy + 14.0f, 92.0f, 22.0f }, analyzeOutput ? "SOURCE OUT" : "SOURCE IN", fx::col::textSecondary);
    drawBadge({ cx + w - 186.0f, cy + 14.0f, 82.0f, 22.0f }, isFrozen ? "SPEC HOLD" : "LIVE SPEC", isFrozen ? fx::col::meterMid : fx::accent::filter);
    drawBadge({ cx + w - 96.0f, cy + 14.0f, 74.0f, 22.0f }, "FFT 2K", fx::col::textSecondary);

    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    const juce::String calText = "top " + juce::String(offsetVal, 0) + " dB  range " + juce::String(rangeVal, 0) + " dB  tilt "
        + (tiltCentered >= 0.0f ? "+" : "") + juce::String(tiltCentered, 1);
    g.drawText(calText, (int) cx + 28, (int) (cy + 16), 220, 18, juce::Justification::centredLeft);
}

void MusiqueAnalyzerEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::filter);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12, 10, 40, 40), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int fy = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float)getWidth() - 52.0f, (float)fy + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "MIX", 80, 120);
    fx::paint::footerLabel(g, "OUT", 210, 120);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueAnalyzerEditor::resized()
{
    // Header
    titleLabel.setBounds(56, 10, 160, 40);
    bypassBtn.setBounds(getWidth() - 378, 16, 64, fx::dim::btnH);
    freezeBtn.setBounds(getWidth() - 308, 16, 64, fx::dim::btnH);
    sourceBtn.setBounds(getWidth() - 238, 16, 96, fx::dim::btnH);
    settingsBtn.setBounds(getWidth() - 118, 16, 42, fx::dim::btnH);

    // Preset bar
    const int py = fx::dim::headerH + 11;
    prevBtn.setBounds(260, py, 30, fx::dim::btnH);
    presetBox.setBounds(294, py, 250, fx::dim::btnH);
    nextBtn.setBounds(548, py, 30, fx::dim::btnH);
    saveBtn.setBounds(590, py, 56, fx::dim::btnH);
    abBtn.setBounds(652, py, 48, fx::dim::btnH);

    // Knobs
    const int ctrlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int numKnobs = 6;
    const int kW = getWidth() / numKnobs;
    const int kY = ctrlTop + 14;
    for (int i = 0; i < numKnobs; ++i)
    {
        int x = i * kW;
        knobs[i].setBounds(x + (kW - 92) / 2, kY, 92, 90);
        knobLabels[i].setBounds(x + (kW - 120) / 2, kY + 92, 120, 16);
    }

    // Footer
    const int fy = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, fy + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, fy + 6, 20, fx::dim::footerH - 12);
    mixSlider.setBounds(80, fy + 8, 120, 24);
    outputSlider.setBounds(210, fy + 8, 120, 24);
    activeLED.setBounds(350, fy + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 220, fy + 8, 160, 24);
}
