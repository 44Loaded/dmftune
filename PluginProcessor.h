#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

#include "DMFTuneDSP.h"

class DMFTuneAudioProcessor : public juce::AudioProcessor
{
public:
    DMFTuneAudioProcessor();
    ~DMFTuneAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DMF TUNE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    dmftune::Engine engine;
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };

    static const char* noteName (int pitchClass);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dmftune::Params readParams() const;

    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pRange = nullptr;
    std::atomic<float>* pHuman = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pShift = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::array<std::atomic<float>*, 12> pNote {};

    int currentProgram = 0;
    int preparedChannels = 2;
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DMFTuneAudioProcessor)
};
