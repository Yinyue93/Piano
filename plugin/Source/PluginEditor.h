#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
// Shows the transport position (the host's, or in the standalone app the MIDI
// file player's), so a sound can be found again in a song while debugging.
// It's where the playhead was at the start of the block being rendered: the
// audio heard lags it by the plugin's latency and the audio device's buffer.
class PlaybackTimeDisplay : public juce::Component,
                            private juce::Timer
{
public:
    PlaybackTimeDisplay (PianoAudioProcessor&);

    void resized() override;

    // "1:23.456", "1:23.456 stopped", or "No transport"
    static juce::String format (double seconds, bool playing);

private:
    void timerCallback() override;

    PianoAudioProcessor& proc;
    juce::Label name, value;
};

//==============================================================================
class PianoAudioProcessorEditor : public gin::ProcessorEditor
{
public:
    PianoAudioProcessorEditor (PianoAudioProcessor&);
    ~PianoAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<int> getBaseBounds() const;
    void applyScale();

    PianoAudioProcessor& proc;

    juce::MidiKeyboardComponent keyboard { proc.keyState, juce::MidiKeyboardComponent::horizontalKeyboard };
    PlaybackTimeDisplay playbackTime { proc };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoAudioProcessorEditor)
};
