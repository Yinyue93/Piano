#include "PianoAudition.h"
#include <map>

using namespace PianoAudition;
namespace
{
constexpr const char* preset = "Warm Body + Refined Treble 2";

double measureNote (PianoAudioProcessor& processor, int fs, int note, int velocity)
{
    processor.reset();
    const int latency = processor.getLatencySamples();
    const int begin = latency + int (.05 * fs), end = latency + int (.25 * fs);
    juce::AudioBuffer<float> block (2, 257);
    juce::MidiBuffer midi;
    double energy = 0;
    for (int position = 0; position < end; position += 257)
    {
        midi.clear();
        if (position == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, juce::uint8 (velocity)), 0);
        processor.processBlock (block, midi);
        for (int i = 0; i < 257; ++i)
            for (int channel = 0; channel < 2; ++channel)
            {
                const double sample = block.getSample (channel, i);
                if (! std::isfinite (sample)) throw std::runtime_error ("Nonfinite note render");
                if (position + i >= begin && position + i < end) energy += sample * sample;
            }
    }
    return energy / (2 * (end - begin));
}

void writeAtRate (const juce::File& file, const juce::AudioBuffer<float>& audio, int fs)
{
    if (file.exists()) throw std::runtime_error ("Refusing to overwrite a render");
    auto stream = file.createOutputStream();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (stream.get(), fs, 2, 24, {}, 0));
    if (! writer) throw std::runtime_error ("Cannot create WAV");
    stream.release();
    if (! writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) throw std::runtime_error ("Cannot write WAV");
}

juce::AudioBuffer<float> readWave (const juce::File& file, int fs)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader (format.createReaderFor (file.createInputStream().release(), true));
    if (! reader || reader->sampleRate != fs || reader->numChannels != 2
        || reader->bitsPerSample != 24 || reader->lengthInSamples != fs * 5)
        throw std::runtime_error ("WAV format mismatch");
    juce::AudioBuffer<float> audio (2, fs * 5);
    reader->read (&audio, 0, fs * 5, 0, true, true);
    return audio;
}

void excerpt (const juce::File& folder, int fs, const juce::File& approved, std::ostream& report)
{
    juce::FileInputStream stream (juce::File::getCurrentWorkingDirectory().getChildFile ("midi/chop2511.mid"));
    juce::MidiFile midi;
    if (! midi.readFrom (stream)) throw std::runtime_error ("Run from the repository with midi/chop2511.mid present");
    midi.convertTimestampTicksToSeconds();
    std::vector<Event> events;
    for (int track = 0; track < midi.getNumTracks(); ++track)
        for (int i = 0; i < midi.getTrack (track)->getNumEvents(); ++i)
        {
            const auto message = midi.getTrack (track)->getEventPointer (i)->message;
            if (! message.isMetaEvent() && message.getTimeStamp() < 20)
                events.push_back ({int (std::lround (message.getTimeStamp() * fs)), message});
        }
    const auto entire = render (preset, events, 20 * fs, fs, 257);
    juce::AudioBuffer<float> clip (2, 5 * fs);
    for (int channel = 0; channel < 2; ++channel)
    {
        clip.copyFrom (channel, 0, entire, channel, 15 * fs, 5 * fs);
        clip.applyGainRamp (channel, 0, int (.002 * fs), 0, 1);
        clip.applyGainRamp (channel, 5 * fs - int (.005 * fs), int (.005 * fs), 1, 0);
    }
    clip.applyGain (.25f);
    const auto file = folder.getChildFile ("chop2511-15s-20s-" + juce::String (fs) + ".wav");
    writeAtRate (file, clip, fs);
    const auto check = readWave (file, fs);
    const double peak = check.getMagnitude (0, check.getNumSamples());
    if (! std::isfinite (peak) || peak >= .9) throw std::runtime_error ("Insufficient clip headroom");
    report << file.getFileName() << ": peak=" << 20 * std::log10 (peak) << " dBFS; 24-bit stereo, 5 seconds\n";
    if (fs == 48000 && approved.existsAsFile())
    {
        const auto reference = readWave (approved, fs);
        int different = 0;
        double error = 0, energy = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < fs * 5; ++i)
            {
                const double x = reference.getSample (channel, i), delta = x - check.getSample (channel, i);
                different += delta != 0;
                error += delta * delta;
                energy += x * x;
            }
        report << "Approved 48 kHz comparison: " << different << " differing PCM samples; relative error="
               << 10 * std::log10 (error / std::max (1e-30, energy) + 1e-30) << " dB\n";
        if (error > 1e-12 * energy) throw std::runtime_error ("Approved audition changed");
    }
}
}

int main (int argc, char** argv)
try
{
    juce::ScopedJuceInitialiser_GUI gui;
    PianoAudioProcessor::launchCrashReporter = false;
    if (argc < 2 || argc > 3)
    {
        std::cout << "BalancedTrebleAudition new-output-folder [approved-48k-wav]\n";
        return 2;
    }
    const juce::File folder (juce::String::fromUTF8 (argv[1]));
    if (folder.exists() || ! folder.createDirectory()) throw std::runtime_error ("Use a new output directory");
    const juce::File approved = argc == 3 ? juce::File (juce::String::fromUTF8 (argv[2])) : juce::File();
    std::ofstream report (folder.getChildFile ("verification.txt").getFullPathName().toStdString());
    std::ofstream csv (folder.getChildFile ("levels.csv").getFullPathName().toStdString());
    csv << "rate,note,velocity,early_dbfs,delta_from_48k_db\n";
    std::map<std::pair<int, int>, double> reference;
    double worst = 0;
    for (int fs : { 48000, 44100, 88200, 96000, 192000 })
    {
        PianoAudioProcessor processor;
        processor.setCurrentProgram (preset);
        processor.setRateAndBufferSizeDetails (fs, 257);
        processor.prepareToPlay (fs, 257);
        report << fs << " Hz: reported latency " << processor.getLatencySamples() << " samples\n";
        for (int note = 73; note <= 108; ++note)
            for (int velocity : { 32, 56, 84, 112, 127 })
            {
                const double energy = measureNote (processor, fs, note, velocity);
                const auto key = std::make_pair (note, velocity);
                if (fs == 48000) reference[key] = energy;
                const double delta = 10 * std::log10 (energy / reference[key]);
                worst = std::max (worst, std::abs (delta));
                csv << fs << ',' << note << ',' << velocity << ',' << 10 * std::log10 (energy) << ',' << delta << '\n';
                if (! std::isfinite (delta) || std::abs (delta) > .15) throw std::runtime_error ("Sample-rate level disagreement");
            }
        std::cout << "Verified notes at " << fs << " Hz\n";
        if (fs == 44100 || fs == 48000 || fs == 96000) excerpt (folder, fs, approved, report);
    }
    report << "900 isolated-note renders: maximum difference from 48 kHz = " << worst << " dB.\n"
           << "Same MIDI, preset and fixed -12.0412 dB export gain; no compression or limiting.\n";
    std::cout << "Maximum sample-rate level difference: " << worst << " dB\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
