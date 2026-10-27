#pragma once

#include "BoundedMidi.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Refined Treble 2 was voiced at 48 kHz. The legacy short-delay string
// filters move its upper partials at other rates, across narrow body
// resonances. Keep that instrument at its reference rate and convert only
// its stereo output. At 48 kHz the processor bypasses this class entirely.
class PianoReferenceRate
{
public:
    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 64;
    static constexpr int halfLength = 32;
    static constexpr int midiCapacity = 32768;

    static int latencyFor (double hostRate)
    {
        return int (std::ceil ((blockSize + halfLength) * hostRate / sampleRate));
    }

    void prepare (double hostRate)
    {
        ratio = sampleRate / hostRate;
        // The total delay is an integer number of host frames, including
        // the existing 64-frame synthesis buffer.
        filterDelay = latencyFor (hostRate) * ratio - blockSize;
        const double cutoff = 0.94 * std::min (1.0, hostRate / sampleRate);
        kernels.resize (phases + 1);
        for (int phase = 0; phase <= phases; ++phase)
        {
            const double fraction = double (phase) / phases;
            double sum = 0.0;
            for (int tap = 0; tap < taps; ++tap)
            {
                const double distance = tap - (halfLength - 1) - fraction;
                const double x = juce::MathConstants<double>::pi * cutoff * distance;
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (x) / x;
                const double angle = juce::MathConstants<double>::pi * distance / halfLength;
                const double window = 0.42 + 0.5 * std::cos (angle) + 0.08 * std::cos (2 * angle);
                const double coefficient = cutoff * sinc * window;
                kernels[size_t (phase)][size_t (tap)] = float (coefficient);
                sum += coefficient;
            }
            for (auto& coefficient : kernels[size_t (phase)])
                coefficient = float (coefficient / sum);
        }
        queued.ensureSize (midiCapacity);
        carry.ensureSize (midiCapacity);
        pending.ensureSize (midiCapacity);
        reset();
    }

    void reset()
    {
        hostFrames = nextEngineFrame = 0;
        pendingSamples = 0;
        queued.clear();
        carry.clear();
        pending.clear();
        rendered.clear();
        for (auto& channel : history) channel.fill (0.0f);
    }

    // renderBlock receives one complete, timestamped block of reference-rate
    // MIDI. All buffers and FIR coefficients are prepared before callbacks.
    template <typename RenderBlock>
    void process (juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi, RenderBlock&& renderBlock)
    {
        const auto firstEngineFrame = nextEngineFrame;
        for (const auto meta : midi)
        {
            // Never move a new event into a reference frame already rendered
            // by the preceding host block (important when upconverting).
            const auto frame = int64_t (std::ceil ((double (hostFrames) + meta.samplePosition) * ratio - 1e-8));
            const int offset = int (std::max (int64_t (0), frame - firstEngineFrame));
            BoundedMidi::add (queued, midiCapacity, meta.data, meta.numBytes, offset);
        }

        auto event = queued.cbegin();
        for (int i = 0; i < output.getNumSamples(); ++i)
        {
            const double position = double (hostFrames + i) * ratio;
            const auto latest = int64_t (std::floor (position + 1e-8));
            while (nextEngineFrame <= latest)
            {
                for (; event != queued.cend()
                       && (*event).samplePosition <= nextEngineFrame - firstEngineFrame; ++event)
                {
                    const auto meta = *event;
                    BoundedMidi::add (pending, midiCapacity, meta.data, meta.numBytes, pendingSamples);
                }

                // Play the previous synthesis block while collecting the
                // next one: exactly the existing 64-frame engine latency.
                for (int channel = 0; channel < 2; ++channel)
                    history[size_t (channel)][size_t (nextEngineFrame) & historyMask]
                        = rendered.getSample (channel, pendingSamples);
                ++nextEngineFrame;
                if (++pendingSamples == blockSize)
                {
                    rendered.clear();
                    renderBlock (rendered, pending);
                    pending.clear();
                    pendingSamples = 0;
                }
            }

            const double centre = position - filterDelay;
            const auto whole = int64_t (std::floor (centre));
            const double fractionalPhase = (centre - double (whole)) * phases;
            const int phase = std::clamp (int (fractionalPhase), 0, phases - 1);
            const float mix = float (fractionalPhase - phase);
            const auto& a = kernels[size_t (phase)];
            const auto& b = kernels[size_t (phase + 1)];
            std::array<double, 2> value {};
            for (int tap = 0; tap < taps; ++tap)
            {
                const auto frame = whole + tap - (halfLength - 1);
                if (frame < 0 || frame >= nextEngineFrame) continue;
                const float coefficient = a[size_t (tap)] + mix * (b[size_t (tap)] - a[size_t (tap)]);
                for (int channel = 0; channel < 2; ++channel)
                    value[size_t (channel)] += coefficient * double (history[size_t (channel)][size_t (frame) & historyMask]);
            }
            for (int channel = 0; channel < std::min (2, output.getNumChannels()); ++channel)
                output.setSample (channel, i, float (value[size_t (channel)]));
        }
        hostFrames += output.getNumSamples();

        // Only events on the next fractional-rate boundary normally remain.
        // Empty host blocks may also queue MIDI without advancing time.
        carry.clear();
        for (; event != queued.cend(); ++event)
        {
            const auto meta = *event;
            const int offset = int (meta.samplePosition - (nextEngineFrame - firstEngineFrame));
            BoundedMidi::add (carry, midiCapacity, meta.data, meta.numBytes, std::max (0, offset));
        }
        queued.swapWith (carry);
    }

private:
    static constexpr int taps = 2 * halfLength, phases = 1024;
    static constexpr size_t historyMask = 127;
    std::vector<std::array<float, taps>> kernels;
    std::array<std::array<float, historyMask + 1>, 2> history {};
    juce::AudioBuffer<float> rendered { 2, blockSize };
    juce::MidiBuffer queued, carry, pending;
    int64_t hostFrames = 0, nextEngineFrame = 0;
    int pendingSamples = 0;
    double ratio = 1.0, filterDelay = halfLength;
};
