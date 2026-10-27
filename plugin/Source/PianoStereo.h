#pragma once

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>
#include <vector>

// A fixed player-perspective image plus quiet, band-limited early reflections.
// Only the side signal changes: (left + right) / 2 retains the original body.
class PianoStereo
{
public:
    void prepare (double sampleRate)
    {
        nearTap = std::max (1, int (std::round (sampleRate * 0.0037)));
        farTap = std::max (nearTap + 1, int (std::round (sampleRate * 0.0083)));
        history.assign (size_t (farTap + 1), 0.0f);
        lowCoefficient = float (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 180.0 / sampleRate));
        highCoefficient = float (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 5500.0 / sampleRate));
        width.reset (sampleRate, 0.03);
        clear();
    }

    void clear()
    {
        std::fill (history.begin(), history.end(), 0.0f);
        position = 0;
        low = high = 0.0f;
        width.setCurrentAndTargetValue (width.getTargetValue());
    }

    void setWidth (float value) { width.setTargetValue (std::clamp (value, 0.0f, 1.0f)); }

    static float notePosition (int note)
    {
        // Middle C stays central; even the outer keys remain in both mics.
        return 0.55f * std::clamp ((note - 60) / 48.0f, -1.0f, 1.0f);
    }

    void process (float mid, float bodySide, float& left, float& right)
    {
        low += lowCoefficient * (mid - low);
        high += highCoefficient * ((mid - low) - high);
        history[size_t (position)] = high;
        const auto read = [this] (int delay)
        {
            // Both taps are shorter than the ring, so at most one wrap is needed.
            int index = position - delay;
            if (index < 0) index += int (history.size());
            return history[size_t (index)];
        };
        const float side = width.getNextValue() * (bodySide + 0.12f * (read (nearTap) - read (farTap)));
        left = mid - side;
        right = mid + side;
        if (++position == int (history.size())) position = 0;
    }

private:
    std::vector<float> history;
    juce::SmoothedValue<float> width { 0.0f };
    int position = 0, nearTap = 1, farTap = 2;
    float low = 0.0f, high = 0.0f;
    float lowCoefficient = 0.0f, highCoefficient = 0.0f;
};
