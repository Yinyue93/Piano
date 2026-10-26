#pragma once

#include <JuceHeader.h>
#include "BoundedMidi.h"

#include <array>

//==============================================================================
/** MIDI filter implementing a sustain pedal (CC64). While the pedal is down,
    note offs are held back; they are emitted when the pedal is released.

    The held note offs are a fixed table (one slot per channel and note), so
    nothing here allocates. */
class SustainPedal
{
public:
    void reset()
    {
        pedalDown = false;
        clearHeld();
    }

    // Filters in into out, which is cleared first. out stays within
    // outCapacity bytes: see BoundedMidi.
    void processMidi (const juce::MidiBuffer& in, juce::MidiBuffer& out, int outCapacity)
    {
        out.clear();

        for (const auto meta : in)
        {
            auto pos = meta.samplePosition;

            // Only short messages are read as MidiMessages: longer ones (SysEx)
            // would allocate, and they don't concern the pedal
            if (meta.numBytes > 3)
            {
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
                continue;
            }

            auto m = meta.getMessage();
            auto channel = m.getChannel() - 1;

            if (m.isSustainPedalOn())
            {
                pedalDown = true;
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
            else if (m.isSustainPedalOff())
            {
                pedalDown = false;
                releaseHeld (out, outCapacity, pos);
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
            else if (pedalDown && m.isNoteOff() && channel >= 0)
            {
                hold (channel, m.getNoteNumber(), m.getVelocity());
            }
            else
            {
                if (pedalDown && m.isNoteOn() && channel >= 0)
                {
                    // Key is held again, so drop any pending note off for it
                    unhold (channel, m.getNoteNumber());
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    clearHeld();
                }

                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
        }
    }

private:
    void hold (int channel, int note, juce::uint8 velocity)
    {
        auto& slot = held[size_t (channel)][size_t (note)];

        if (slot == 0)
            numHeld++;

        slot = juce::uint8 (velocity + 1);
    }

    void unhold (int channel, int note)
    {
        auto& slot = held[size_t (channel)][size_t (note)];

        if (slot != 0)
        {
            slot = 0;
            numHeld--;
        }
    }

    void releaseHeld (juce::MidiBuffer& out, int capacity, int pos)
    {
        for (int channel = 0; channel < 16 && numHeld > 0; channel++)
        {
            for (int note = 0; note < 128; note++)
            {
                if (auto slot = held[size_t (channel)][size_t (note)])
                {
                    const juce::uint8 noteOff[] = { juce::uint8 (0x80 | channel), juce::uint8 (note), juce::uint8 (slot - 1) };
                    BoundedMidi::add (out, capacity, noteOff, 3, pos);
                }
            }
        }

        clearHeld();
    }

    void clearHeld()
    {
        if (numHeld > 0)
            for (auto& channel : held)
                channel.fill (0);

        numHeld = 0;
    }

    bool pedalDown = false;

    // The held note off's velocity + 1 for each channel and note, 0 if none
    std::array<std::array<juce::uint8, 128>, 16> held {};
    int numHeld = 0;
};
