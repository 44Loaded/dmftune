#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct Preset
{
    const char* name;
    float speed, hold, range, human;
    int mode;
};

const Preset kPresets[] = {
    { "HARD SNAP",   18.0f, 0.40f, 100.0f, 0.25f, 1 },
    { "ROBOT",        0.0f, 0.70f, 200.0f, 0.00f, 2 },
    { "SOFT NATURE", 120.0f, 0.30f,  80.0f, 0.60f, 0 },
    { "SUBTLE FIX",  220.0f, 0.50f,  50.0f, 0.80f, 0 },
    { "WIDE GLIDE",   60.0f, 0.20f, 150.0f, 0.35f, 1 },
};
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

const char* const kNoteIds[12] = { "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11" };
} // namespace

const char* DMFTuneAudioProcessor::noteName (int pc)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[((pc % 12) + 12) % 12];
}

juce::AudioProcessorValueTreeState::ParameterLayout DMFTuneAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed",
                                                       NormalisableRange<float> (0.0f, 400.0f, 0.0f, 0.5f), 18.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "hold", 1 }, "Hold",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.4f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "range", 1 }, "Range",
                                                       NormalisableRange<float> (10.0f, 200.0f), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "human", 1 }, "Human",
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.25f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant",
                                                       NormalisableRange<float> (-6.0f, 6.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "shift", 1 }, "Shift",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 1.0f), 1.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Out",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                        StringArray { "CLEAN", "HARD", "SNAP" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key",
                                                        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale",
                                                        StringArray { "Chromatic", "Major", "Minor", "Minor Penta", "Blues" }, 2));

    // A minor by default: A B C D E F G
    const bool defaultOn[12] = { true, false, true, false, true, true, false, true, false, true, false, true };
    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { kNoteIds[i], 1 },
                                                          String ("Note ") + noteName (i), defaultOn[i]));
    return layout;
}

DMFTuneAudioProcessor::DMFTuneAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pSpeed = apvts.getRawParameterValue ("speed");
    pHold = apvts.getRawParameterValue ("hold");
    pRange = apvts.getRawParameterValue ("range");
    pHuman = apvts.getRawParameterValue ("human");
    pFormant = apvts.getRawParameterValue ("formant");
    pShift = apvts.getRawParameterValue ("shift");
    pMix = apvts.getRawParameterValue ("mix");
    pOut = apvts.getRawParameterValue ("out");
    pMode = apvts.getRawParameterValue ("mode");
    pBypass = apvts.getRawParameterValue ("bypass");
    for (int i = 0; i < 12; ++i)
        pNote[(size_t) i] = apvts.getRawParameterValue (kNoteIds[i]);
}

dmftune::Params DMFTuneAudioProcessor::readParams() const
{
    dmftune::Params p;
    p.speedMs = pSpeed->load();
    p.hold = pHold->load();
    p.rangeCents = pRange->load();
    p.human = pHuman->load();
    p.formantSt = pFormant->load();
    p.shiftSt = pShift->load();
    p.mix = pMix->load();
    p.outDb = pOut->load();
    p.mode = juce::jlimit (0, 2, (int) std::lround (pMode->load()));
    p.bypass = pBypass->load() > 0.5f;
    uint16_t mask = 0;
    for (int i = 0; i < 12; ++i)
        if (pNote[(size_t) i]->load() > 0.5f)
            mask = (uint16_t) (mask | (1u << i));
    p.noteMask = mask;
    return p;
}

void DMFTuneAudioProcessor::prepareToPlay (double sampleRate, int)
{
    preparedChannels = juce::jlimit (1, 2, getTotalNumInputChannels());
    engine.setParams (readParams());
    engine.prepare (sampleRate, preparedChannels);
    engine.setParams (readParams());
    setLatencySamples (engine.latencySamples());
    prepared.store (true);
}

bool DMFTuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void DMFTuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();

    // The engine buffers only exist after prepareToPlay(), so never run before that.
    if (! prepared.load() || numCh <= 0 || numSamples <= 0)
    {
        buffer.clear();
        return;
    }

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    float inPeak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        inPeak = juce::jmax (inPeak, buffer.getMagnitude (ch, 0, numSamples));

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), numCh, numSamples);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numCh; ++ch)
        outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, numSamples));

    const float decay = std::pow (0.9995f, (float) numSamples);
    inLevel.store (juce::jmax (inPeak, inLevel.load() * decay));
    outLevel.store (juce::jmax (outPeak, outLevel.load() * decay));
}

juce::AudioProcessorEditor* DMFTuneAudioProcessor::createEditor()
{
    return new DMFTuneEditor (*this);
}

int DMFTuneAudioProcessor::getNumPrograms() { return kNumPresets; }

const juce::String DMFTuneAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, kNumPresets) ? kPresets[index].name : "";
}

void DMFTuneAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, kNumPresets))
        return;

    currentProgram = index;
    const auto& p = kPresets[index];

    auto set = [this] (const char* id, float plain)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
    };
    set ("speed", p.speed);
    set ("hold", p.hold);
    set ("range", p.range);
    set ("human", p.human);
    set ("mode", (float) p.mode);
    set ("formant", 0.0f);
    set ("shift", 0.0f);
}

void DMFTuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DMFTuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, kNumPresets - 1, (int) tree.getProperty ("program", 0));
            apvts.replaceState (tree);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DMFTuneAudioProcessor();
}
