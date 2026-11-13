#pragma once

#include <JuceHeader.h>
#include <memory>

#include "PluginProcessor.h"

//==============================================================================
class DMFTuneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
};

//==============================================================================
class DMFTuneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DMFTuneEditor (DMFTuneAudioProcessor&);
    ~DMFTuneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kNumKnobs = 8;

    void timerCallback() override;
    int paramIndex (const char* id) const;
    void setParam (const char* id, float plainValue);
    void applyKeyAndScale (int root, int scale);

    void paintDisplay (juce::Graphics&);
    void paintRightPanel (juce::Graphics&);
    void paintBottom (juce::Graphics&);

    DMFTuneAudioProcessor& proc;
    DMFTuneLookAndFeel look;   // declared before the components so it outlives them
    juce::Image logo;
    int displayBase = 48;      // MIDI note at the bottom lane of the pitch display

    juce::TextButton keyButton[12];
    juce::TextButton scaleButton[5];
    juce::TextButton modeButton[3];
    juce::TextButton noteButton[12];
    juce::TextButton bypassButton, prevButton, nextButton;
    juce::Slider knobSlider[kNumKnobs];

    // attachments are declared after the components they point to
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> noteAttach[12];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAttach[kNumKnobs];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DMFTuneEditor)
};
