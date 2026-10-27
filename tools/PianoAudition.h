#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace PianoAudition
{
constexpr int rate = 48000, blockSize = 256;
struct Event
{
    int sample;
    juce::MidiMessage message;
};
struct Biquad
{
    double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double tick(double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};
// BS.1770 K weighting, 400 ms windows, absolute and relative gates (48 kHz).
inline double loudness(const juce::AudioBuffer<float> &audio)
{
    const int length = audio.getNumSamples();
    std::vector<double> power(size_t(length), 0), prefix(size_t(length) + 1, 0), blocks;
    for (int ch = 0; ch < 2; ++ch)
    {
        Biquad shelf{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241,
                     .73248077421585};
        Biquad hp{1, -2, 1, -1.99004745483398, .99007225036621};
        for (int i = 0; i < length; ++i)
        {
            const double y = hp.tick(shelf.tick(audio.getSample(ch, i)));
            power[size_t(i)] += y * y;
        }
    }
    for (int i = 0; i < length; ++i)
        prefix[size_t(i) + 1] = prefix[size_t(i)] + power[size_t(i)];
    double sum = 0;
    for (int i = 0; i + 19200 <= length; i += 4800)
    {
        const double e = (prefix[size_t(i) + 19200] - prefix[size_t(i)]) / 19200;
        if (e > 0 && -.691 + 10 * std::log10(e) > -70)
        {
            blocks.push_back(e);
            sum += e;
        }
    }
    if (blocks.empty())
        throw std::runtime_error("Silent audition");
    const double gate = sum / blocks.size() * .1;
    sum = 0;
    int count = 0;
    for (double e : blocks)
        if (e >= gate)
        {
            sum += e;
            ++count;
        }
    return -.691 + 10 * std::log10(sum / count);
}
inline void addNote(std::vector<Event> &events, double time, int note, int velocity, double duration)
{
    events.push_back(
        {int(std::lround(time * rate)), juce::MidiMessage::noteOn(1, note, juce::uint8(velocity))});
    events.push_back({int(std::lround((time + duration) * rate)), juce::MidiMessage::noteOff(1, note)});
}
inline std::vector<Event> passage(bool pedal)
{
    std::vector<Event> events;
    if (!pedal)
    {
        for (int i = 0; i < 4; ++i)
            addNote(events, .2 + 2 * i, 72 + 12 * i, 84, 1.6);
        const int bottoms[] = {72, 79, 84, 96};
        for (int i = 0; i < 4; ++i)
            for (int octave : {0, 12})
                addNote(events, 8.5 + 1.1 * i, bottoms[i] + octave, 78 + 4 * i, .85);
        const int melody[] = {84, 88, 91, 96, 95, 91, 96, 100, 103, 108, 103, 96};
        for (int i = 0; i < 12; ++i)
            addNote(events, 13 + .25 * i, melody[i], 68 + 3 * i, i == 11 ? 1 : .23);
    }
    else
    {
        // Same keys with pedal up, half down, then fully down; a short mixed
        // register phrase and repeated strikes finish the comparison.
        for (int section = 0; section < 3; ++section)
        {
            events.push_back(
                {int((.1 + 3 * section) * rate), juce::MidiMessage::controllerEvent(1, 64,
                                                                                    section == 0   ? 0
                                                                                    : section == 1 ? 64
                                                                                                   : 127)});
            for (int i = 0; i < 4; ++i)
                addNote(events, .25 + 3 * section + .22 * i, 72 + 4 * i, 78, .16);
            events.push_back({int((2.6 + 3 * section) * rate), juce::MidiMessage::controllerEvent(1, 64, 0)});
        }
        events.push_back({9 * rate, juce::MidiMessage::controllerEvent(1, 64, 96)});
        for (int n : {48, 55, 60, 64})
            addNote(events, 9.2, n, 66, 1.5);
        for (int i = 0; i < 8; ++i)
            addNote(events, 9.2 + .22 * i, 84 + (i % 2) * 7, 60 + 6 * i, .14);
        events.push_back({int(11.2 * rate), juce::MidiMessage::controllerEvent(1, 64, 0)});
    }
    return events;
}
inline juce::AudioBuffer<float> render(const char *preset, std::vector<Event> events, int length, int hostRate = rate, int hostBlockSize = blockSize)
{
    PianoAudioProcessor processor;
    processor.setCurrentProgram(preset);
    const auto* program = processor.getProgram(preset);
    if (program == nullptr || processor.getVoicing() != int(program->state.getProperty("pianoVoicing", -1)))
        throw std::runtime_error("Bundled preset missing");
    processor.setRateAndBufferSizeDetails(hostRate, hostBlockSize);
    processor.prepareToPlay(hostRate, hostBlockSize);
    const int latency = processor.getLatencySamples();
    juce::AudioBuffer<float> audio(2, length), block(2, hostBlockSize);
    audio.clear();
    juce::MidiBuffer midi;
    midi.ensureSize(65536);
    std::stable_sort(events.begin(), events.end(),
                     [](const auto &a, const auto &b) { return a.sample < b.sample; });
    size_t cursor = 0;
    for (int position = 0; position < length + latency; position += hostBlockSize)
    {
        const int count = std::min(hostBlockSize, length + latency - position);
        block.setSize(2, count, false, false, true);
        midi.clear();
        while (cursor < events.size() && events[cursor].sample < position + count)
        {
            midi.addEvent(events[cursor].message, events[cursor].sample - position);
            ++cursor;
        }
        processor.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < count; ++i)
            {
                const float value = block.getSample(ch, i);
                if (!std::isfinite(value))
                    throw std::runtime_error("Nonfinite audio");
                const int destination = position + i - latency;
                if (destination >= 0 && destination < length)
                    audio.setSample(ch, destination, value);
            }
    }
    return audio;
}
inline void writeWave(const juce::File &file, const juce::AudioBuffer<float> &audio)
{
    if (file.exists())
        throw std::runtime_error("Refusing to overwrite an audition");
    auto stream = file.createOutputStream();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), rate, 2, 24, {}, 0));
    if (!writer)
        throw std::runtime_error("Cannot create WAV");
    stream.release();
    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        throw std::runtime_error("WAV write failed");
}
} // namespace PianoAudition
