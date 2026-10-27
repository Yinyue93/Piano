#include "PianoAudition.h"

using namespace PianoAudition;

namespace
{
constexpr const char* baseline = "Warm Body + Refined Treble";
constexpr const char* selected = "Warm Body + Refined Treble 2";

std::vector<Event> dynamics()
{
    std::vector<Event> events;
    double time = .2;
    for (int velocity : {40, 84, 112})
        for (int note : {84, 96, 108})
        {
            addNote(events, time, note, velocity, .75);
            time += 1.4;
        }
    for (int i = 0; i < 5; ++i)
        addNote(events, 13.1 + i * .3, 96, 76 + i * 5, .12);
    for (int note : {48, 60, 67})
        addNote(events, 15.1, note, 70, 1.2);
    for (int i = 0; i < 8; ++i)
        addNote(events, 15.1 + i * .22, 84 + (i % 4) * 4, 80, .18);
    return events;
}

void pair(const juce::File& folder, bool varied, std::ostream& report)
{
    constexpr int length = 18 * rate;
    const auto events = varied ? dynamics() : passage(false);
    auto a = render(baseline, events, length), b = render(selected, events, length);
    double target = -23;
    for (auto* audio : {&a, &b})
    {
        for (int ch = 0; ch < 2; ++ch)
            audio->applyGainRamp(ch, length - 480, 480, 1, 0);
        target = std::min(target, loudness(*audio) - 6 - 20 * std::log10(audio->getMagnitude(0, length)));
    }
    int index = 0;
    for (auto* audio : {&a, &b})
    {
        double gain = 0;
        for (int iteration = 0; iteration < 10; ++iteration)
        {
            const double adjustment = target - loudness(*audio);
            if (std::abs(adjustment) < .005)
                break;
            audio->applyGain(float(std::pow(10, adjustment / 20)));
            gain += adjustment;
        }
        const char* name = varied ? (index == 0 ? "03-baseline-dynamics" : "04-refined-treble-2-dynamics")
                                  : (index == 0 ? "01-baseline" : "02-refined-treble-2");
        const auto file = folder.getChildFile(juce::String(name) + ".wav");
        writeWave(file, *audio);
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->sampleRate != rate || reader->numChannels != 2 || reader->bitsPerSample != 24 ||
            reader->lengthInSamples != length)
            throw std::runtime_error("Export format mismatch");
        juce::AudioBuffer<float> check(2, length);
        reader->read(&check, 0, length, 0, true, true);
        const double lu = loudness(check), peak = 20 * std::log10(check.getMagnitude(0, length));
        if (std::abs(lu - target) > .05 || peak > -5.99)
            throw std::runtime_error("Export loudness/headroom mismatch");
        report << name << ": " << lu << " LUFS; fixed gain=" << gain << " dB; peak=" << peak << " dBFS\n";
        std::cout << "Verified " << name << ": " << lu << " LUFS\n";
        ++index;
    }
}

void benchmark(bool reverse = false)
{
    constexpr int size = 128;
    for (const auto* preset : {reverse ? selected : baseline, reverse ? baseline : selected})
    {
        PianoAudioProcessor processor;
        processor.setCurrentProgram(preset);
        processor.prepareToPlay(rate, size);
        juce::AudioBuffer<float> buffer(2, size);
        juce::MidiBuffer midi;
        midi.ensureSize(4096);
        std::vector<double> times;
        times.reserve(rate * 8 / size);
        long long voiceBlocks = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int position = 0; position < rate * 8; position += size)
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
            processor.processBlock(buffer, midi);
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count());
            voiceBlocks += processor.piano->getNumVoices();
            if (!std::isfinite(buffer.getMagnitude(0, size)))
                throw std::runtime_error("Nonfinite benchmark");
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::sort(times.begin(), times.end());
        std::cout << preset << ": 8 seconds in " << elapsed << " seconds; median=" << times[times.size() / 2]
                  << " ms; p99=" << times[times.size() * 99 / 100] << " ms; max=" << times.back()
                  << " ms; block deadline=" << size * 1000.0 / rate
                  << " ms; average active voices=" << double(voiceBlocks) / times.size() << "\n";
    }
}
} // namespace

int main(int argc, char** argv)
try
{
    juce::ScopedJuceInitialiser_GUI gui;
    PianoAudioProcessor::launchCrashReporter = false;
    if (argc != 2)
    {
        std::cout << "RefinedTrebleAudition output-folder | --benchmark | --benchmark-reverse | --release-check\n";
        return 2;
    }
    if (std::string(argv[1]) == "--benchmark" || std::string(argv[1]) == "--benchmark-reverse")
    {
        benchmark(std::string(argv[1]) == "--benchmark-reverse");
        return 0;
    }
    if (std::string(argv[1]) == "--release-check")
    {
        for (int fs : {44100, 48000, 96000})
            for (int note : {88, 89, 96, 108})
            {
                std::vector<Event> held {{13, juce::MidiMessage::noteOn(1, note, juce::uint8(84))}};
                auto released = held;
                released.push_back({1001, juce::MidiMessage::noteOff(1, note)});
                const auto a = render(selected, held, 24000, fs, 64);
                const auto b = render(selected, released, 24000, fs, 257);
                double error = 0, energy = 0, peak = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 24000; ++i)
                    {
                        const double x = a.getSample(ch, i), d = x - b.getSample(ch, i);
                        error += d * d;
                        energy += x * x;
                        peak = std::max(peak, std::abs(d));
                    }
                std::cout << "rate=" << fs << " note=" << note << " relative_error="
                          << 10 * std::log10(error / std::max(energy, 1e-30) + 1e-30)
                          << " dB; peak_difference=" << peak << '\n';
            }
        return 0;
    }
    const juce::File folder(juce::String::fromUTF8(argv[1]));
    if (folder.exists() || !folder.createDirectory())
        throw std::runtime_error("Use a new output directory");
    std::ofstream report(folder.getChildFile("measurements.txt").getFullPathName().toStdString());
    report << std::setprecision(10)
           << "Production processor, 48 kHz, 24-bit stereo; identical MIDI within each pair.\n"
           << "One fixed gain per clip, matched integrated loudness, at least 6 dB sample-peak headroom.\n"
           << "64-sample latency removed; 10 ms final fade; no compressor or limiter.\n"
           << "Treble: C5/C6/C7/C8 at 0.2/2.2/4.2/6.2 s, velocity 84, held 1.6 s; octaves and melody follow.\n"
           << "Upper notes overlap naturally after release in Refined Treble 2; those windows are not isolated-note spectra.\n"
           << "Dynamics: C6/C7/C8 at velocities 40/84/112, then repeated C7 and a mixed-register phrase.\n\n";
    pair(folder, false, report);
    pair(folder, true, report);
    return 0;
}
catch (const std::exception& e)
{
    std::cerr << e.what() << '\n';
    return 1;
}
