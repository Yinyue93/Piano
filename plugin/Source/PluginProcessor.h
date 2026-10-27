#pragma once

#include <JuceHeader.h>
#include "qiano.h"
#include "SustainPedal.h"
#include "PianoReferenceRate.h"

#include <atomic>

//==============================================================================
class PianoAudioProcessor : public gin::Processor
{
public:
    //==============================================================================
	PianoAudioProcessor();
    ~PianoAudioProcessor() override;

    void stateUpdated() override;
    void updateState() override;
    void loadAllPrograms() override;

    // Preset-specific voicing; old state without this property uses Classic.
    void setVoicing (int mode);
    int getVoicing() const { return requestedVoicing.load (std::memory_order_relaxed); }

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

    // The transport position at the start of the last block, for the
    // editor's time display: the host's, or in the standalone app the MIDI
    // file player's. Negative when there's no transport.
    std::atomic<double> playheadSeconds { -1.0 };
    std::atomic<bool> playheadPlaying { false };

    std::unique_ptr<Piano> piano;

    juce::Array<const gin::Parameter*> params;

    // The constructor launches the shared CrashReporter app if it's installed.
    // Tests turn this off.
    static inline bool launchCrashReporter = true;

	// A 64-sample span lets each voice reuse its filter/string data in cache.
	// The FIFO reports this latency to the host (1.33 ms at 48 kHz).
	constexpr static int interalBlockSize = 64;

	// The host's MIDI is copied to inputMidi and goes through the sustain pedal
	// into sustainedMidi. The engine renders fixed blocks of interalBlockSize:
	// MIDI waits in pendingMidi (timestamps relative to the engine block) until
	// the block is complete. These buffers get their storage in prepareToPlay
	// and stay within it (see BoundedMidi), so they don't allocate on the audio
	// thread.
	constexpr static int midiBufferBytes = 32768;
	juce::MidiBuffer		inputMidi, sustainedMidi, pendingMidi;
	int						pendingSamples = 0;

	// Holds the latency (one engine block) plus the block being read out. The
	// host block is rendered a step at a time, so this doesn't depend on the
	// host's block size.
	gin::AudioMidiFifo		fifoOut {2, 2 * interalBlockSize};
	juce::AudioSampleBuffer workBuffer {2, interalBlockSize};

private:
    void updatePlayhead();
    void updateEngineLatency();
    // Prepared alongside the host-rate engine, so changing voicing does not
    // allocate a new instrument on the audio thread.
    std::unique_ptr<Piano> alternateRatePiano;
    PianoReferenceRate referenceRate;
    bool referenceRateActive = false;
    double outputSampleRate = PianoReferenceRate::sampleRate;
    std::atomic<int> requestedVoicing { PianoVoicing::classic };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoAudioProcessor)
};
