#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout BasementAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });

    auto decibels = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });

    auto cityAttr = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.1f)  return juce::String ("New York");
            if (v < 0.4f)  return juce::String ("NY > Detroit");
            if (v < 0.6f)  return juce::String ("Detroit");
            if (v < 0.9f)  return juce::String ("Detroit > LA");
            return juce::String ("Los Angeles");
        })
        .withValueFromStringFunction ([] (const juce::String& t)
        {
            const auto c = t.trim().toLowerCase().substring (0, 1);
            if (c == "n") return 0.0f;
            if (c == "d") return 0.5f;
            if (c == "l") return 1.0f;
            return juce::jlimit (0.0f, 1.0f, t.getFloatValue() / 100.0f);
        });

    const juce::NormalisableRange<float> unit (0.0f, 1.0f, 0.001f);

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 1 }, "Mode",
                                                              juce::StringArray { "Sample", "Drums" }, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "drive", 1 }, "Drive", unit, 0.35f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "low", 1 }, "Low", unit, 0.50f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "punch", 1 }, "Punch", unit, 0.50f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "grit", 1 }, "Grit", unit, 0.25f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "needle", 1 }, "Needle", unit, 0.20f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "city", 1 }, "City", unit, 0.0f, cityAttr));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix", unit, 1.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, decibels));
    return layout;
}

BasementAudioProcessor::BasementAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    modeParam   = apvts.getRawParameterValue ("mode");
    driveParam  = apvts.getRawParameterValue ("drive");
    lowParam    = apvts.getRawParameterValue ("low");
    punchParam  = apvts.getRawParameterValue ("punch");
    gritParam   = apvts.getRawParameterValue ("grit");
    needleParam = apvts.getRawParameterValue ("needle");
    cityParam   = apvts.getRawParameterValue ("city");
    mixParam    = apvts.getRawParameterValue ("mix");
    outputParam = apvts.getRawParameterValue ("output");
}

void BasementAudioProcessor::prepareToPlay (double sampleRate, int)
{
    dsp.prepare (sampleRate);
}

bool BasementAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void BasementAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    basement::Params p;
    p.drums = modeParam->load() > 0.5f;
    p.drive = driveParam->load();
    p.low = lowParam->load();
    p.punch = punchParam->load();
    p.grit = gritParam->load();
    p.needle = needleParam->load();
    p.city = cityParam->load();
    p.mix = mixParam->load();
    p.outputDb = outputParam->load();

    const int numSamples = buffer.getNumSamples();
    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    dsp.process (buffer.getArrayOfWritePointers(), juce::jmin (2, buffer.getNumChannels()), numSamples, p);
}

juce::AudioProcessorEditor* BasementAudioProcessor::createEditor()
{
    return new BasementAudioProcessorEditor (*this);
}

void BasementAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BasementAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BasementAudioProcessor();
}
