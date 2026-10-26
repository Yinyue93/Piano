#pragma once

#include <JuceHeader.h>
#include "BoundedMidi.h"

#include <array>

//==============================================================================
/** MIDI filter implementing a sustain pedal (CC64) for each MIDI channel.
    While a channel's pedal is down, its note offs are held back; they are
    emitted when that pedal is released, or by Reset All Controllers (CC121),
    which lifts it.

    The held note offs are a fixed table (one slot per channel and note), so
    nothing here allocates. */
class SustainPedal
{
public:
    void reset()
    {
        pedalDown.fill (false);

        for (int channel = 0; channel < 16; channel++)
            clearHeld (channel);
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

            // System messages have no channel
            if (channel < 0)
            {
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
                continue;
            }

            auto& down = pedalDown[size_t (channel)];

            if (m.isSustainPedalOn())
            {
                down = true;
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
            else if (m.isSustainPedalOff() || m.isControllerOfType (121))
            {
                down = false;
                releaseHeld (out, outCapacity, channel, pos);
                BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
            else if (down && m.isNoteOff())
            {
                hold (channel, m.getNoteNumber(), m.getVelocity());
            }
            else
            {
                if (down && m.isNoteOn())
                {
                    // Key is held again, so drop any pending note off for it
                    unhold (channel, m.getNoteNumber());
                }
                else if (m.isAllNotesOff() || m.isAllSoundOff())
                {
                    clearHeld (channel);
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
            numHeld[size_t (channel)]++;

        slot = juce::uint8 (velocity + 1);
    }

    void unhold (int channel, int note)
    {
        auto& slot = held[size_t (channel)][size_t (note)];

        if (slot != 0)
        {
            slot = 0;
            numHeld[size_t (channel)]--;
        }
    }

    void releaseHeld (juce::MidiBuffer& out, int capacity, int channel, int pos)
    {
        if (numHeld[size_t (channel)] == 0)
            return;

        for (int note = 0; note < 128; note++)
        {
            if (auto slot = held[size_t (channel)][size_t (note)])
            {
                const juce::uint8 noteOff[] = { juce::uint8 (0x80 | channel), juce::uint8 (note), juce::uint8 (slot - 1) };
                BoundedMidi::add (out, capacity, noteOff, 3, pos);
            }
        }

        clearHeld (channel);
    }

    void clearHeld (int channel)
    {
        if (numHeld[size_t (channel)] > 0)
            held[size_t (channel)].fill (0);

        numHeld[size_t (channel)] = 0;
    }

    std::array<bool, 16> pedalDown {};

    // The held note off's velocity + 1 for each channel and note, 0 if none
    std::array<std::array<juce::uint8, 128>, 16> held {};
    std::array<int, 16> numHeld {};
};
