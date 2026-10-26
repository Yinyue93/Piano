#pragma once

#include <JuceHeader.h>
#include "BoundedMidi.h"

#include <array>

//==============================================================================
/** MIDI filter implementing a sustain pedal (CC64) for each MIDI channel.
    While a channel's pedal is down, its note offs are held back; they are
    emitted when that pedal is released, or by Reset All Controllers (CC121),
    which lifts it. All Notes Off (CC123) releases the keys but respects the
    pedal; only All Sound Off (CC120) bypasses it and clears the held notes.

    The held note offs are a fixed table (one slot per channel and note), so
    nothing here allocates. */
class SustainPedal
{
public:
    void reset()
    {
        pedalDown.fill (false);

        for (int channel = 0; channel < 16; channel++)
        {
            clearHeld (channel);
            keysDown[size_t (channel)].fill (false);
        }
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
            else if (m.isNoteOff())
            {
                keysDown[size_t (channel)][size_t (m.getNoteNumber())] = false;
                if (down)
                    hold (channel, m.getNoteNumber(), m.getVelocity());
                else
                    BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
            }
            else if (m.isAllNotesOff())
            {
                // Equivalent to releasing every key on this channel. Notes
                // already held by the pedal must keep their pending note off.
                auto& keys = keysDown[size_t (channel)];
                if (down)
                {
                    for (int note = 0; note < 128; note++)
                        if (keys[size_t (note)])
                            hold (channel, note, 0);
                }
                else
                {
                    BoundedMidi::add (out, outCapacity, meta.data, meta.numBytes, pos);
                }
                keys.fill (false);
            }
            else
            {
                if (m.isNoteOn())
                {
                    keysDown[size_t (channel)][size_t (m.getNoteNumber())] = true;
                    // Key is held again, so drop any pending note off for it
                    unhold (channel, m.getNoteNumber());
                }
                else if (m.isAllSoundOff())
                {
                    clearHeld (channel);
                    keysDown[size_t (channel)].fill (false);
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
    std::array<std::array<bool, 128>, 16> keysDown {};

    // The held note off's velocity + 1 for each channel and note, 0 if none
    std::array<std::array<juce::uint8, 128>, 16> held {};
    std::array<int, 16> numHeld {};
};
