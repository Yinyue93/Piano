#pragma once

#include <JuceHeader.h>
#include "qiano.h"
#include "SustainPedal.h"

//==============================================================================
class PianoAudioProcessor : public gin::Processor
{
public:
    //==============================================================================
	PianoAudioProcessor();
    ~PianoAudioProcessor() override;

    void stateUpdated() override;
    void updateState() override;

    //==============================================================================
    void reset() override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    juce::MidiKeyboardState keyState;
    SustainPedal sustainPedal;

    std::unique_ptr<Piano> piano;

    juce::Array<const gin::Parameter*> params;

    // The constructor launches the shared CrashReporter app if it's installed.
    // Tests turn this off.
    static inline bool launchCrashReporter = true;

	constexpr static int interalBlockSize = 32;

	// The engine renders fixed blocks of interalBlockSize. Incoming MIDI waits in
	// pendingMidi (timestamps relative to the first unrendered sample) until a
	// whole block is available. These buffers are preallocated in prepareToPlay
	// so they don't allocate on the audio thread.
	constexpr static int midiBufferBytes = 8192;
	juce::MidiBuffer		pendingMidi, pendingMidiSpare, workMidi;
	int						pendingSamples = 0;

	gin::AudioMidiFifo		fifoOut {2, interalBlockSize};
	juce::AudioSampleBuffer workBuffer {2, interalBlockSize};

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoAudioProcessor)
};
