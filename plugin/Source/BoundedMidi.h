#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Adding to a juce::MidiBuffer allocates once its storage is full. The audio
    thread's buffers get their storage in prepareToPlay() (ensureSize() with
    their capacity in bytes), and add() keeps them within it, so MIDI never
    allocates there.

    When a buffer fills up, the events that stop sound win: anything else is
    dropped once the buffer is three quarters full, while note offs, sustain
    off and All Notes / Sound Off can use the rest. If even those don't fit,
    one All Notes Off goes in their place, so no note is left ringing. Only a
    flood of thousands of events in one block gets that far.
*/
namespace BoundedMidi
{
    // Bytes a MidiBuffer stores per event: timestamp, size, then the data
    inline int eventSize (int numBytes)
    {
        return numBytes + int (sizeof (juce::int32) + sizeof (juce::uint16));
    }

    inline bool isRelease (const juce::uint8* data, int numBytes)
    {
        if (numBytes < 3)
            return false;

        auto status = data[0] & 0xf0;

        if (status == 0x80 || (status == 0x90 && data[2] == 0))
            return true;

        // Sustain pedal up, All Sound Off, All Notes Off
        return status == 0xb0 && ((data[1] == 64 && data[2] < 64) || data[1] == 120 || data[1] == 123);
    }

    // Returns false if the event was dropped. capacity is the buffer's
    // ensureSize() in bytes.
    inline bool add (juce::MidiBuffer& buffer, int capacity, const juce::uint8* data, int numBytes, int samplePosition)
    {
        const auto used     = buffer.data.size();
        const auto size     = eventSize (numBytes);
        const auto reserve  = eventSize (3);   // room for the All Notes Off

        if (! isRelease (data, numBytes))
        {
            if (used + size > capacity * 3 / 4)
                return false;

            buffer.addEvent (data, numBytes, samplePosition);
            return true;
        }

        if (used + size <= capacity - reserve)
        {
            buffer.addEvent (data, numBytes, samplePosition);
            return true;
        }

        // Every add leaves the reserve free, so this succeeds once per buffer
        if (used + reserve <= capacity)
        {
            const juce::uint8 allNotesOff[] = { juce::uint8 (0xb0 | (data[0] & 0x0f)), 123, 0 };
            buffer.addEvent (allNotesOff, 3, samplePosition);
        }

        return false;
    }
}
