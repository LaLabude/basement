#pragma once

#include "PluginProcessor.h"

class BasementAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit BasementAudioProcessorEditor (BasementAudioProcessor&);
    ~BasementAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob& k, const juce::String& title, const juce::String& paramId);

    BasementAudioProcessor& processor;

    Knob drive, low, punch, grit, needle, city, mix, output;
    juce::ComboBox modeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BasementAudioProcessorEditor)
};
