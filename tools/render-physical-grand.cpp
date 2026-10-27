#include "PianoAudition.h"
using namespace PianoAudition;
namespace
{
void comparison(const juce::File &folder, bool pedal, std::ofstream &report)
{
    const int length = (pedal ? 14 : 18) * rate;
    auto a = render("Warm Body + Refined Treble", passage(pedal), length);
    auto b = render("Physical Grand", passage(pedal), length);
    for (auto *audio : {&a, &b})
        for (int ch = 0; ch < 2; ++ch)
            audio->applyGainRamp(ch, length - 480, 480, 1, 0);
    const double la = loudness(a), lb = loudness(b);
    const double target = std::min({-23.0, la - 6 - 20 * std::log10(a.getMagnitude(0, length)),
                                    lb - 6 - 20 * std::log10(b.getMagnitude(0, length))});
    int index = 0;
    for (auto *audio : {&a, &b})
    {
        const double original = index == 0 ? la : lb;
        audio->applyGain(float(std::pow(10.0, (target - original) / 20)));
        const juce::String name = (pedal ? (index == 0 ? "03-refined-pedal" : "04-physical-grand-pedal")
                                         : (index == 0 ? "01-refined-treble" : "02-physical-grand-treble"));
        const auto file = folder.getChildFile(name + ".wav");
        writeWave(file, *audio);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->lengthInSamples != length || reader->bitsPerSample != 24)
            throw std::runtime_error("WAV metadata mismatch");
        juce::AudioBuffer<float> check(2, length);
        reader->read(&check, 0, length, 0, true, true);
        const double measured = loudness(check), peak = 20 * std::log10(check.getMagnitude(0, length));
        if (std::abs(measured - target) > .05 || peak > -5.99)
            throw std::runtime_error("Export level mismatch");
        report << name << ": raw=" << original << " LUFS; fixed gain=" << target - original
               << " dB; exported=" << measured << " LUFS; peak=" << peak << " dBFS\n";
        std::cout << name << ": " << measured << " LUFS; " << peak << " dBFS\n";
        ++index;
    }
}
} // namespace

int main(int argc, char **argv)
try
{
    juce::ScopedJuceInitialiser_GUI gui;
    PianoAudioProcessor::launchCrashReporter = false;
    if (argc < 2)
    {
        std::cout << "PianoAudition output-folder | --levels output.csv | --benchmark\n";
        return 2;
    }
    if (std::string(argv[1]) == "--benchmark")
    {
        const int count = argc > 2 ? std::clamp(std::stoi(argv[2]), 64, 2048) : 128;
        PianoAudioProcessor processor;
        processor.setCurrentProgram("Physical Grand");
        processor.prepareToPlay(rate, count);
        juce::AudioBuffer<float> block(2, count);
        juce::MidiBuffer midi;
        midi.ensureSize(8192);
        std::vector<double> times;
        times.reserve(3000);
        const auto start = std::chrono::steady_clock::now();
        for (int position = 0; position < rate * 8; position += count)
        {
            midi.clear();
            if (position == 0)
            {
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
                for (int note : {36, 43, 48, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96, 100, 103})
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(90)), 0);
            }
            if (position == rate * 4)
                midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            const auto before = std::chrono::steady_clock::now();
            processor.processBlock(block, midi);
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
            if (!std::isfinite(block.getMagnitude(0, count)))
                throw std::runtime_error("Nonfinite benchmark");
        }
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::sort(times.begin(), times.end());
        std::cout << "16 mixed-register notes, pedal down, 48 kHz, " << count << " samples: 8 seconds in "
                  << elapsed << " seconds; median=" << times[times.size() / 2]
                  << " ms; p99=" << times[times.size() * 99 / 100] << " ms; max=" << times.back()
                  << " ms; block budget=" << count * 1000.0 / rate << " ms\n";
        return 0;
    }
    if (std::string(argv[1]) == "--levels" && argc >= 3)
    {
        std::ofstream report(argv[2]);
        report << "note,rms_db,peak_db\n";
        for (int note : {36, 48, 55, 57, 58, 59, 60, 61, 64, 72, 84, 96, 108})
        {
            const auto audio = render("Physical Grand",
                                      {{0, juce::MidiMessage::noteOn(1, note, juce::uint8(84))}}, rate / 2);
            double sum = 0;
            for (int i = 2400; i < 12000; ++i)
            {
                const double x = .5 * (audio.getSample(0, i) + audio.getSample(1, i));
                sum += x * x;
            }
            report << note << "," << 10 * std::log10(sum / 9600 + 1e-30) << ","
                   << 20 * std::log10(audio.getMagnitude(0, rate / 2) + 1e-30) << "\n";
        }
        return 0;
    }
    const juce::File folder(juce::String::fromUTF8(argv[1]));
    if (!folder.createDirectory())
        throw std::runtime_error("Cannot create output folder");
    if (folder.getChildFile("measurements.txt").exists())
        throw std::runtime_error("Use a new output folder");
    std::ofstream report(folder.getChildFile("measurements.txt").getFullPathName().toStdString());
    report << std::setprecision(9);
    report
        << "48 kHz, 24-bit stereo. Same MIDI and velocity within each pair. Plugin latency removed.\n"
        << "Fixed gain only, no compressor/limiter, at least 6 dB sample-peak headroom; 10 ms final fade.\n"
        << "Treble: C5/C6/C7/C8 at .2/2.2/4.2/6.2 s, velocity 84, held 1.6 s; octaves then melody.\n"
        << "Pedal: CC64 0/64/127 at .1/3.1/6.1 s, same short C5/E5/G#5/C6 figures; mixed register and "
           "repetitions at 9.2 s.\n"
        << "Refined uses binary sustain; Physical Grand uses continuous damper pressure.\n\n";
    comparison(folder, false, report);
    comparison(folder, true, report);
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << e.what() << "\n";
    return 1;
}
