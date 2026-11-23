#include "PluginEditor.h"

namespace
{
    const juce::Colour bg      { 0xff141311 };
    const juce::Colour panel   { 0xff1d1b18 };
    const juce::Colour accent  { 0xffd9a441 };
    const juce::Colour textDim { 0xff8a8478 };
    const juce::Colour textMain{ 0xffe8e2d4 };
}

BasementAudioProcessorEditor::BasementAudioProcessorEditor (BasementAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (drive, "DRIVE", "drive");
    setupKnob (low, "LOW", "low");
    setupKnob (punch, "PUNCH", "punch");
    setupKnob (grit, "GRIT", "grit");
    setupKnob (needle, "NEEDLE", "needle");
    setupKnob (city, "CITY", "city");
    setupKnob (mix, "MIX", "mix");
    setupKnob (output, "OUTPUT", "output");

    modeBox.addItemList ({ "Sample", "Drums" }, 1);
    modeBox.setColour (juce::ComboBox::backgroundColourId, panel);
    modeBox.setColour (juce::ComboBox::textColourId, accent);
    modeBox.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.5f));
    modeBox.setColour (juce::ComboBox::arrowColourId, accent);
    addAndMakeVisible (modeBox);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "mode", modeBox);

    setSize (800, 300);
}

void BasementAudioProcessorEditor::setupKnob (Knob& k, const juce::String& title, const juce::String& paramId)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 92, 20);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.2f));
    k.slider.setColour (juce::Slider::thumbColourId, textMain);
    k.slider.setColour (juce::Slider::textBoxTextColourId, textMain);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);

    k.label.setText (title, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void BasementAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);

    auto area = getLocalBounds().reduced (20);
    g.setColour (accent);
    g.setFont (30.0f);
    g.drawText ("BASEMENT", area.removeFromTop (40).withTrimmedRight (140), juce::Justification::centredLeft);

    g.setColour (textDim);
    g.setFont (13.0f);
    g.drawText ("boom bap tone tool", 20, 52, 300, 18, juce::Justification::centredLeft);

    g.setColour (panel.brighter (0.15f));
    g.drawLine (20.0f, 78.0f, (float) getWidth() - 20.0f, 78.0f, 1.0f);
}

void BasementAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    auto header = area.removeFromTop (50);
    modeBox.setBounds (header.removeFromRight (120).withSizeKeepingCentre (120, 28));

    area.removeFromTop (36);

    Knob* knobs[] = { &drive, &low, &punch, &grit, &needle, &city, &mix, &output };
    const int w = area.getWidth() / 8;
    for (auto* k : knobs)
    {
        auto col = area.removeFromLeft (w);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col);
    }
}
