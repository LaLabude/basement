#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "BasementDsp.h"

class BasementAudioProcessor : public juce::AudioProcessor
{
public:
    BasementAudioProcessor();
    ~BasementAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Basement"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    basement::Dsp dsp;

    std::atomic<float>* modeParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* lowParam = nullptr;
    std::atomic<float>* punchParam = nullptr;
    std::atomic<float>* gritParam = nullptr;
    std::atomic<float>* needleParam = nullptr;
    std::atomic<float>* cityParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasementAudioProcessor)
};
