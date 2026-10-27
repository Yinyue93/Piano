#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>

class PianoModelAudit
{
  public:
    static void write(const juce::File &file, const juce::AudioBuffer<float> &audio, double rate)
    {
        if (file.existsAsFile())
            throw std::runtime_error("Output already exists");
        auto stream = file.createOutputStream();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.get(), rate, unsigned(audio.getNumChannels()), 32, {}, 0));
        if (!writer)
            throw std::runtime_error("WAV writer failed");
        stream.release();
        writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    }

    static void run(const juce::File &folder)
    {
        folder.createDirectory();
        std::ofstream report(folder.getChildFile("baseline.csv").getFullPathName().toStdString());
        report << "note,internal_rate,hammer_peak,contact_ms,raw_rms,final_rms\n";
        for (int note : {60, 72, 84, 96, 108})
        {
            PianoAudioProcessor proc;
            proc.setCurrentProgram("Warm Body + Refined Treble");
            proc.prepareToPlay(48000, 64);
            auto &piano = *proc.piano;
            piano.noteOn(1, note, 84);
            auto &voice = *piano.noteArray[size_t(note)];
            const double internal = 48000.0 * voice.upsample / voice.downsample;
            double hammerPeak = 0;
            int contact = 0;
            for (int i = 0; i < int(internal * .05); ++i)
            {
                voice.go();
                hammerPeak = std::max(hammerPeak, double(voice.hammer->F));
                if (voice.hammer->F > 1e-5)
                    ++contact;
            }
            proc.prepareToPlay(48000, 64);
            proc.piano->noteOn(1, note, 84);
            auto &rawVoice = *proc.piano->noteArray[size_t(note)];
            juce::AudioBuffer<float> raw(1, 96000), final(2, 96000), block(2, 64);
            double rawEnergy = 0, finalEnergy = 0;
            for (int i = 0; i < raw.getNumSamples(); ++i)
            {
                const float x = rawVoice.goUpDelayed();
                raw.setSample(0, i, x);
                if (i >= 2400 && i < 12000)
                    rawEnergy += double(x) * x;
            }
            proc.prepareToPlay(48000, 64);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < 96000; pos += 64)
            {
                midi.clear();
                if (pos == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(84)), 0);
                proc.processBlock(block, midi);
                for (int c = 0; c < 2; ++c)
                    final.copyFrom(c, pos, block, c, 0, 64);
                for (int i = 0; i < 64; ++i)
                    if (pos + i >= 2400 && pos + i < 12000)
                        finalEnergy += .5 * (double(block.getSample(0, i)) * block.getSample(0, i) +
                                             double(block.getSample(1, i)) * block.getSample(1, i));
            }
            // Diagnostic bridge forces can exceed PCM range; use one fixed
            // attenuation per stage, recorded here rather than normalizing notes.
            raw.applyGain(.001f);
            write(folder.getChildFile("note-" + juce::String(note) + "-string.wav"), raw, 48000);
            write(folder.getChildFile("note-" + juce::String(note) + "-final.wav"), final, 48000);
            report << std::setprecision(12) << note << "," << internal << "," << hammerPeak << ","
                   << 1000 * contact / internal << "," << std::sqrt(rawEnergy / 9600) << ","
                   << std::sqrt(finalEnergy / 9600) << "\n";
            std::cout << "Audited " << note << std::endl;
        }
    }
};
int main(int argc, char **argv)
try
{
    juce::ScopedJuceInitialiser_GUI gui;
    PianoAudioProcessor::launchCrashReporter = false;
    if (argc != 2)
        return 2;
    PianoModelAudit::run(juce::File(juce::String(argv[1])));
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << e.what() << std::endl;
    return 1;
}
