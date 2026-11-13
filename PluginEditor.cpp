#include <BinaryData.h>
#include "PluginEditor.h"

namespace
{
constexpr int kW = 1120;
constexpr int kH = 648;

// colours sampled from the DMF LAB logo
const juce::Colour kBg       (0xff000000);
const juce::Colour kPanelLo  (0xff07040b);
const juce::Colour kPanel    (0xff0c0812);
const juce::Colour kPanelHi  (0xff181022);
const juce::Colour kPurple   (0xff472665);
const juce::Colour kPurpleDim(0xff2a1545);
const juce::Colour kPurpleGlow(0xff9155ea);
const juce::Colour kLime     (0xff7ef326);
const juce::Colour kLimeDark (0xff45a32b);
const juce::Colour kLimeLight(0xffe4ffc4);
const juce::Colour kText     (0xffeef9e4);
const juce::Colour kMuted    (0xff8f80a8);
const juce::Colour kGrid     (0xff1b1229);

const char* const kKnobIds[8]   = { "speed", "hold", "range", "human", "formant", "shift", "mix", "out" };
const char* const kKnobNames[8] = { "SPEED", "HOLD", "RANGE", "HUMAN", "FORMANT", "SHIFT", "MIX", "OUT" };
const char* const kNoteIds[12]  = { "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11" };

// pitch display geometry
constexpr int kPlotX = 252, kPlotY = 170, kPlotW = 512, kLanes = 24, kLaneH = 12;
constexpr int kBlackKeys = 0b010101001010;

juce::Font monoFont (float h, bool bold = true)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font titleFont (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), h, juce::Font::bold));
}

void panel (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (kPurple);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

void glowLine (juce::Graphics& g, float x1, float y1, float x2, float y2, juce::Colour c)
{
    g.setColour (c.withAlpha (0.16f));
    g.drawLine (x1, y1, x2, y2, 7.0f);
    g.setColour (c.withAlpha (0.45f));
    g.drawLine (x1, y1, x2, y2, 2.5f);
    g.setColour (c);
    g.drawLine (x1, y1, x2, y2, 1.0f);
}

void sectionLabel (juce::Graphics& g, int x, int y, const juce::String& text)
{
    g.setColour (kLime);
    g.fillRoundedRectangle ((float) x, (float) y + 5.0f, 6.0f, 6.0f, 2.0f);
    g.setColour (kText);
    g.setFont (monoFont (12.0f));
    g.drawText (text, x + 12, y, 200, 16, juce::Justification::centredLeft, false);
}

juce::String valueText (const char* id, float v)
{
    const juce::String name (id);
    if (name == "speed")  return juce::String (juce::roundToInt (v)) + " ms";
    if (name == "hold" || name == "human" || name == "mix")
        return juce::String (juce::roundToInt (v * 100.0f)) + " %";
    if (name == "range")  return "+/-" + juce::String (juce::roundToInt (v)) + " c";
    if (name == "formant" || name == "shift") return juce::String (v, 1) + " st";
    if (name == "out")    return juce::String (v, 1) + " dB";
    return {};
}

void levelMeter (juce::Graphics& g, float y, const char* label, float level)
{
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);

    g.setColour (kMuted);
    g.setFont (monoFont (11.0f));
    g.drawText (label, 226, (int) y - 7, 36, 14, juce::Justification::centredLeft, false);

    const juce::Rectangle<float> bar (268.0f, y - 4.0f, 412.0f, 8.0f);
    g.setColour (kPanelHi);
    g.fillRoundedRectangle (bar, 4.0f);
    if (frac > 0.0f)
    {
        g.setColour (kLime);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 4.0f);
    }

    g.setColour (kText);
    g.setFont (monoFont (11.0f, false));
    g.drawText (db <= -59.9f ? juce::String ("-inf") : juce::String (db, 1) + " dB",
                686, (int) y - 7, 78, 14, juce::Justification::centredRight, false);
}
} // namespace

//==============================================================================
void DMFTuneLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                               bool isHighlighted, bool isDown)
{
    const auto area = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const int kind = (int) b.getProperties().getWithDefault ("kind", 0);

    if (kind == 2) // bypass
    {
        g.setColour (on ? kPurple : kPanelHi);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (on ? kLime : kPurpleGlow);
        g.drawRoundedRectangle (area, 8.0f, 1.5f);
        return;
    }

    if (kind == 3) // preset arrows
    {
        g.setColour (isDown ? kPurple : kPanelHi);
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (kPurple);
        g.drawRoundedRectangle (area, 8.0f, 1.0f);
        return;
    }

    if (on)
    {
        g.setColour (kLime.withAlpha (0.20f));
        g.fillRoundedRectangle (area.expanded (3.0f), 9.0f);
        g.setColour (kLime);
        g.fillRoundedRectangle (area, 6.0f);
    }
    else
    {
        g.setColour (isDown ? kPurpleDim : (isHighlighted ? kPanelHi : kPanel));
        g.fillRoundedRectangle (area, 6.0f);
        g.setColour (isHighlighted ? kPurpleGlow : kPurple);
        g.drawRoundedRectangle (area, 6.0f, 1.0f);
    }
}

void DMFTuneLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    const int kind = (int) b.getProperties().getWithDefault ("kind", 0);

    juce::Colour c = kText;
    if (kind == 2)
        c = on ? kLimeLight : kText;
    else if (on)
        c = kBg;
    else if (kind == 1)
        c = kMuted;

    g.setColour (c);
    g.setFont (monoFont (kind == 3 ? 16.0f : 12.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

void DMFTuneLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                           float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (7.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const float r = d * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float arcR = r - 4.0f;

    // body with a soft shadow
    g.setColour (juce::Colours::black);
    g.fillEllipse (c.x - r, c.y - r + 3.0f, d, d);
    g.setColour (kPanelHi);
    g.fillEllipse (c.x - r, c.y - r, d, d);

    // track
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (kPurpleDim);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc: glow then core
    juce::Path value;
    value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
    g.setColour (kLime.withAlpha (0.22f));
    g.strokePath (value, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (kLime);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -(r - 12.0f), 3.0f, r * 0.45f, 1.5f);
    g.setColour (kLimeLight);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));

    // centre cap
    g.setColour (kPanel);
    g.fillEllipse (c.x - r * 0.42f, c.y - r * 0.42f, r * 0.84f, r * 0.84f);
}

//==============================================================================
DMFTuneEditor::DMFTuneEditor (DMFTuneAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);

    // KEY
    for (int i = 0; i < 12; ++i)
    {
        keyButton[i].setButtonText (DMFTuneAudioProcessor::noteName (i));
        keyButton[i].onClick = [this, i] { applyKeyAndScale (i, paramIndex ("scale")); };
        addAndMakeVisible (keyButton[i]);
    }

    // SCALE
    const char* scaleNames[5] = { "CHROMATIC", "MAJOR", "MINOR", "MINOR PENTA", "BLUES" };
    for (int i = 0; i < 5; ++i)
    {
        scaleButton[i].setButtonText (scaleNames[i]);
        scaleButton[i].onClick = [this, i] { applyKeyAndScale (paramIndex ("key"), i); };
        addAndMakeVisible (scaleButton[i]);
    }

    // MODE
    const char* modeNames[3] = { "CLEAN", "HARD", "SNAP" };
    for (int i = 0; i < 3; ++i)
    {
        modeButton[i].setButtonText (modeNames[i]);
        modeButton[i].onClick = [this, i] { setParam ("mode", (float) i); };
        addAndMakeVisible (modeButton[i]);
    }

    // NOTE toggles, bound straight to the parameters
    for (int i = 0; i < 12; ++i)
    {
        noteButton[i].setButtonText (DMFTuneAudioProcessor::noteName (i));
        noteButton[i].getProperties().set ("kind", 1);
        noteButton[i].setClickingTogglesState (true);
        addAndMakeVisible (noteButton[i]);
        noteAttach[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.apvts, kNoteIds[i], noteButton[i]);
    }

    // BYPASS and preset arrows
    bypassButton.setButtonText ("BYPASS");
    bypassButton.getProperties().set ("kind", 2);
    bypassButton.setClickingTogglesState (true);
    addAndMakeVisible (bypassButton);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "bypass", bypassButton);

    prevButton.setButtonText ("<");
    nextButton.setButtonText (">");
    prevButton.getProperties().set ("kind", 3);
    nextButton.getProperties().set ("kind", 3);
    prevButton.onClick = [this]
    {
        const int n = proc.getNumPrograms();
        proc.setCurrentProgram ((proc.getCurrentProgram() + n - 1) % n);
    };
    nextButton.onClick = [this]
    {
        proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms());
    };
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);

    // KNOBS
    for (int i = 0; i < kNumKnobs; ++i)
    {
        knobSlider[i].setSliderStyle (juce::Slider::RotaryVerticalDrag);
        knobSlider[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knobSlider[i].setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                           juce::MathConstants<float>::pi * 2.75f, true);
        knobSlider[i].setMouseDragSensitivity (220);
        addAndMakeVisible (knobSlider[i]);
        knobAttach[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.apvts, kKnobIds[i], knobSlider[i]);
    }

    startTimerHz (30);
    setSize (kW, kH); // last: resized() runs here and needs every component above
}

DMFTuneEditor::~DMFTuneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
int DMFTuneEditor::paramIndex (const char* id) const
{
    return juce::roundToInt (proc.apvts.getRawParameterValue (id)->load());
}

void DMFTuneEditor::setParam (const char* id, float plain)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void DMFTuneEditor::applyKeyAndScale (int root, int scale)
{
    static const int scales[5][12] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 0, 2, 4, 5, 7, 9, 11, -1, -1, -1, -1, -1 },
        { 0, 2, 3, 5, 7, 8, 10, -1, -1, -1, -1, -1 },
        { 0, 3, 5, 7, 10, -1, -1, -1, -1, -1, -1, -1 },
        { 0, 3, 5, 6, 7, 10, -1, -1, -1, -1, -1, -1 },
    };
    if (! juce::isPositiveAndBelow (scale, 5))
        return;

    bool on[12] = {};
    for (int k = 0; k < 12; ++k)
        if (scales[scale][k] >= 0)
            on[(root + scales[scale][k]) % 12] = true;

    setParam ("key", (float) root);
    setParam ("scale", (float) scale);
    for (int i = 0; i < 12; ++i)
        setParam (kNoteIds[i], on[i] ? 1.0f : 0.0f);
}

void DMFTuneEditor::timerCallback()
{
    const int key = paramIndex ("key");
    const int scale = paramIndex ("scale");
    const int mode = paramIndex ("mode");

    for (int i = 0; i < 12; ++i)
        keyButton[i].setToggleState (i == key, juce::dontSendNotification);
    for (int i = 0; i < 5; ++i)
        scaleButton[i].setToggleState (i == scale, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        modeButton[i].setToggleState (i == mode, juce::dontSendNotification);

    repaint();
}

//==============================================================================
void DMFTuneEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c0a33), 0.0f, 0.0f,
                                             kBg, bounds.getWidth(), bounds.getHeight(), false));
    g.fillRect (bounds);

    // header
    g.setColour (kPanelLo);
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, bounds.getWidth(), 120.0f));
    glowLine (g, 0.0f, 120.0f, bounds.getWidth(), 120.0f, kLime);

    if (logo.isValid())
        g.drawImageWithin (logo, 12, 6, 108, 108, juce::RectanglePlacement::centred, false);

    g.setFont (titleFont (46.0f));
    g.setColour (kLime.withAlpha (0.30f));
    g.drawText ("DMF TUNE", 131, 13, 420, 54, juce::Justification::centredLeft, false);
    g.setColour (kLime);
    g.drawText ("DMF TUNE", 133, 15, 420, 54, juce::Justification::centredLeft, false);

    g.setColour (kMuted);
    g.setFont (monoFont (11.0f, false));
    g.drawText ("AUTO PITCH CORRECTION   //   DMF LAB", 135, 72, 460, 16, juce::Justification::centredLeft, false);

    // preset name
    g.setColour (kPanel);
    g.fillRoundedRectangle (684.0f, 40.0f, 216.0f, 36.0f, 8.0f);
    g.setColour (kPurple);
    g.drawRoundedRectangle (684.5f, 40.5f, 215.0f, 35.0f, 8.0f, 1.0f);
    g.setColour (kText);
    g.setFont (monoFont (13.0f));
    g.drawText (proc.getProgramName (proc.getCurrentProgram()), 684, 40, 216, 36,
                juce::Justification::centred, false);

    // main panels
    panel (g, juce::Rectangle<float> (20.0f, 136.0f, 176.0f, 384.0f));
    panel (g, juce::Rectangle<float> (212.0f, 136.0f, 564.0f, 384.0f));
    panel (g, juce::Rectangle<float> (792.0f, 136.0f, 308.0f, 384.0f));
    panel (g, juce::Rectangle<float> (20.0f, 532.0f, 1080.0f, 80.0f));

    sectionLabel (g, 32, 146, "KEY");
    sectionLabel (g, 32, 330, "SCALE");
    sectionLabel (g, 226, 146, "PITCH");

    paintDisplay (g);
    paintRightPanel (g);
    paintBottom (g);

    // footer
    g.setColour (kMuted);
    g.setFont (monoFont (10.0f, false));
    g.drawText ("DMF TUNE  v1.0", 22, 620, 300, 16, juce::Justification::centredLeft, false);
    g.drawText ("DMF LAB", kW - 322, 620, 300, 16, juce::Justification::centredRight, false);

    // outer frame
    g.setColour (kPurple.withAlpha (0.8f));
    g.drawRoundedRectangle (bounds.reduced (6.0f), 14.0f, 1.5f);
}

void DMFTuneEditor::paintDisplay (juce::Graphics& g)
{
    auto& eng = proc.engine;

    const uint16_t mask = [this]
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i)
            if (proc.apvts.getRawParameterValue (kNoteIds[i])->load() > 0.5f)
                m = (uint16_t) (m | (1u << i));
        return m;
    }();

    // legend and readout
    g.setFont (monoFont (11.0f, false));
    g.setColour (kMuted);
    g.drawText ("RAW", 352, 146, 40, 16, juce::Justification::centredLeft, false);
    g.drawText ("TUNED", 432, 146, 60, 16, juce::Justification::centredLeft, false);
    {
        juce::Path p, dashed;
        p.startNewSubPath (326.0f, 154.0f);
        p.lineTo (344.0f, 154.0f);
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, p, dash, 2);
        g.setColour (kPurpleGlow);
        g.fillPath (dashed);
        g.setColour (kLime);
        g.fillRoundedRectangle (410.0f, 151.0f, 18.0f, 4.0f, 2.0f);
    }

    // follow the singer's register
    const int head = eng.traceHead();
    {
        float sum = 0.0f;
        int cnt = 0;
        for (int k = 1; k <= 80; ++k)
        {
            const auto tf = eng.traceAt (head - k);
            if (tf.inMidi > 0.0f) { sum += tf.inMidi; ++cnt; }
        }
        if (cnt > 10)
        {
            const float mean = sum / (float) cnt;
            if (mean < (float) displayBase + 4.0f || mean > (float) displayBase + 20.0f)
                displayBase = juce::jlimit (24, 84, 12 * (int) std::floor ((mean - 6.0f) / 12.0f));
        }
    }

    // lanes
    for (int i = 0; i < kLanes; ++i)
    {
        const int p = kLanes - 1 - i;
        const int midi = displayBase + p;
        const int pc = midi % 12;
        const bool on = ((mask >> pc) & 1) != 0;
        const bool black = ((kBlackKeys >> pc) & 1) != 0;
        const int y = kPlotY + i * kLaneH;

        g.setColour (on ? kLime.withAlpha (0.12f) : (black ? juce::Colour (0xff08050c) : juce::Colour (0xff120c1a)));
        g.fillRect (kPlotX, y, kPlotW, kLaneH);
        g.setColour (kGrid);
        g.fillRect (kPlotX, y + kLaneH - 1, kPlotW, 1);

        const juce::String label = pc == 0 ? juce::String ("C") + juce::String (midi / 12 - 1)
                                           : juce::String (DMFTuneAudioProcessor::noteName (pc));
        g.setColour (on ? kLime : kMuted);
        g.setFont (monoFont (9.5f));
        g.drawText (label, kPlotX - 36, y - 1, 32, kLaneH + 2, juce::Justification::centredRight, false);
    }

    // time grid
    g.setColour (kGrid);
    for (int k = 1; k <= 3; ++k)
        g.fillRect (kPlotX + k * 128, kPlotY, 1, kLanes * kLaneH);

    const float winSeconds = 8.0f;
    const int nFrames = juce::jmin (eng.kTraceSize - 2, (int) (winSeconds / eng.hopSeconds()));
    auto yOf = [](float midi, int base)
    {
        const float y = (float) kPlotY + ((float) kLanes - (midi - (float) base)) * (float) kLaneH - (float) kLaneH * 0.5f;
        return juce::jlimit ((float) kPlotY, (float) (kPlotY + kLanes * kLaneH), y);
    };

    juce::Path raw, tuned;
    bool rawOpen = false, tunedOpen = false;
    for (int k = 0; k < nFrames; ++k)
    {
        const int idx = head - nFrames + k;
        const float x = (float) kPlotX + 6.0f + (float) k / (float) (nFrames - 1) * (float) (kPlotW - 6);
        const auto tf = idx >= 0 ? eng.traceAt (idx) : dmftune::TraceFrame();
        if (tf.inMidi > 0.0f)
        {
            if (rawOpen)   raw.lineTo (x, yOf (tf.inMidi, displayBase));
            else         { raw.startNewSubPath (x, yOf (tf.inMidi, displayBase)); rawOpen = true; }
            if (tunedOpen) tuned.lineTo (x, yOf (tf.outMidi, displayBase));
            else         { tuned.startNewSubPath (x, yOf (tf.outMidi, displayBase)); tunedOpen = true; }
        }
        else
        {
            rawOpen = false;
            tunedOpen = false;
        }
    }

    if (! raw.isEmpty())
    {
        juce::Path dashed;
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, raw, dash, 2);
        g.setColour (kPurpleGlow.withAlpha (0.75f));
        g.fillPath (dashed);

        const auto rounded = juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (kLime.withAlpha (0.20f));
        g.strokePath (tuned, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (kLime);
        g.strokePath (tuned, rounded);
    }

    // NOW line
    g.setColour (kPurpleGlow);
    g.fillRect (kPlotX + kPlotW - 2, kPlotY, 2, kLanes * kLaneH);

    // target readout
    {
        juce::String txt = "--";
        for (int k = 1; k <= 12; ++k)
        {
            const auto tf = eng.traceAt (head - k);
            if (tf.inMidi > 0.0f && tf.targetMidi > 0.0f)
            {
                const int tm = juce::roundToInt (tf.targetMidi);
                const int cents = juce::roundToInt ((tf.inMidi - tf.targetMidi) * 100.0f);
                txt = juce::String (DMFTuneAudioProcessor::noteName (tm)) + juce::String (tm / 12 - 1)
                      + "   " + juce::String (cents > 0 ? "+" : "") + juce::String (cents) + " c";
                break;
            }
        }
        g.setColour (kLime);
        g.setFont (monoFont (13.0f));
        g.drawText (txt, 560, 145, 204, 18, juce::Justification::centredRight, false);
    }

    // time axis
    g.setFont (monoFont (10.0f, false));
    const char* ticks[] = { "-8s", "-6s", "-4s", "-2s" };
    g.setColour (kMuted);
    for (int k = 0; k < 4; ++k)
        g.drawText (ticks[k], kPlotX + k * 128 + 4, kPlotY + kLanes * kLaneH + 4, 40, 14,
                    juce::Justification::centredLeft, false);
    g.setColour (kLime);
    g.drawText ("NOW", kPlotX + kPlotW - 40, kPlotY + kLanes * kLaneH + 4, 36, 14,
                juce::Justification::centredRight, false);
}

void DMFTuneEditor::paintRightPanel (juce::Graphics& g)
{
    sectionLabel (g, 806, 146, "MODE");

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int col = i % 4, row = i / 4;
        const int cx = 838 + col * 76;
        const int top = 234 + row * 110;
        const float v = proc.apvts.getRawParameterValue (kKnobIds[i])->load();

        g.setColour (kText);
        g.setFont (monoFont (11.0f));
        g.drawText (kKnobNames[i], cx - 36, top + 64, 72, 14, juce::Justification::centred, false);
        g.setColour (kMuted);
        g.setFont (monoFont (10.0f, false));
        g.drawText (valueText (kKnobIds[i], v), cx - 36, top + 80, 72, 14, juce::Justification::centred, false);
    }

    // correction meter
    const float corr = juce::jlimit (-50.0f, 50.0f, proc.engine.correctionCents());
    sectionLabel (g, 806, 452, "CORRECTION");
    g.setColour (kLime);
    g.setFont (monoFont (12.0f));
    g.drawText (juce::String (corr > 0.5f ? "+" : "") + juce::String (juce::roundToInt (corr)) + " c",
                980, 452, 100, 16, juce::Justification::centredRight, false);

    const juce::Rectangle<float> track (806.0f, 478.0f, 276.0f, 10.0f);
    g.setColour (kPanelHi);
    g.fillRoundedRectangle (track, 5.0f);
    const float mid = track.getCentreX();
    const float half = track.getWidth() * 0.5f;
    const float wBar = std::abs (corr) / 50.0f * half;
    if (wBar > 0.0f)
    {
        g.setColour (kLime);
        g.fillRoundedRectangle (juce::Rectangle<float> (corr >= 0.0f ? mid : mid - wBar, track.getY(),
                                                        wBar, track.getHeight()), 4.0f);
    }
    g.setColour (kText);
    g.fillRect (mid - 0.5f, track.getY() - 3.0f, 1.0f, track.getHeight() + 6.0f);

    g.setColour (kMuted);
    g.setFont (monoFont (10.0f, false));
    g.drawText ("-50", 806, 494, 60, 14, juce::Justification::centredLeft, false);
    g.drawText ("+50", 1022, 494, 60, 14, juce::Justification::centredRight, false);
}

void DMFTuneEditor::paintBottom (juce::Graphics& g)
{
    sectionLabel (g, 32, 540, "ACTIVE NOTES");

    levelMeter (g, 488.0f, "IN", proc.inLevel.load());
    levelMeter (g, 508.0f, "OUT", proc.outLevel.load());
}

void DMFTuneEditor::resized()
{
    for (int i = 0; i < 12; ++i)
        keyButton[i].setBounds (27 + (i % 3) * 56, 170 + (i / 3) * 36, 50, 30);

    for (int i = 0; i < 5; ++i)
        scaleButton[i].setBounds (27, 354 + i * 34, 162, 28);

    for (int i = 0; i < 3; ++i)
        modeButton[i].setBounds (800 + i * 100, 170, 92, 32);

    for (int i = 0; i < kNumKnobs; ++i)
    {
        const int col = i % 4, row = i / 4;
        const int cx = 838 + col * 76;
        const int top = 234 + row * 110;
        knobSlider[i].setBounds (cx - 30, top, 60, 60);
    }

    for (int i = 0; i < 12; ++i)
        noteButton[i].setBounds (20 + i * 90, 562, 84, 44);

    prevButton.setBounds (640, 40, 36, 36);
    nextButton.setBounds (906, 40, 36, 36);
    bypassButton.setBounds (kW - 160, 40, 136, 36);
}
