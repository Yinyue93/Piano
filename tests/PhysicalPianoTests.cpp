#include "PianoPhysical.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
double renderEnergy(PianoPhysical &piano, int samples)
{
    double energy = 0;
    for (int i = 0; i < samples; ++i)
    {
        const auto f = piano.process();
        require(std::isfinite(f.left) && std::isfinite(f.right), "Nonfinite physical output");
        energy += double(f.left) * f.left + double(f.right) * f.right;
    }
    return energy;
}
void writeWave(const std::string &path, const std::vector<float> &data, int rate)
{
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "Cannot create render");
    auto u32 = [&](uint32_t n) { file.write(reinterpret_cast<char *>(&n), 4); };
    auto u16 = [&](uint16_t n) { file.write(reinterpret_cast<char *>(&n), 2); };
    file.write("RIFF", 4);
    u32(uint32_t(data.size() * 4 + 36));
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(3);
    u16(4);
    u32(rate);
    u32(rate * 16);
    u16(16);
    u16(32);
    file.write("data", 4);
    u32(uint32_t(data.size() * 4));
    file.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size() * 4));
}
} // namespace

int main(int argc, char **argv)
try
{
    if (argc >= 3 && std::string(argv[1]) == "--survey")
    {
        const int rate = argc > 3 ? std::stoi(argv[3]) : 48000;
        const float velocity = argc > 4 ? std::stof(argv[4]) : 4.375f;
        PianoPhysical piano;
        piano.prepare(rate);
        std::ofstream report(argv[2]);
        require(bool(report), "Cannot create survey");
        report << "note,hammer_velocity,frequency,rms_db,peak_db,decay_db,bridge_db,hammer_peak\n";
        for (int note = 60; note <= 108; ++note)
        {
            piano.reset();
            piano.setKey(note, true);
            piano.strike(note, velocity);
            double power = 0, peak = 0, early = 0, late = 0, bridge = 0, hammer = 0;
            for (int i = 0; i < int(rate * 1.4); ++i)
            {
                const auto f = piano.process();
                const double mono = .5 * (f.left + f.right);
                require(std::isfinite(mono), "Nonfinite survey");
                peak = std::max({peak, std::abs(double(f.left)), std::abs(double(f.right))});
                hammer = std::max(hammer, double(f.hammerForce));
                if (i >= int(rate * .03) && i < int(rate * .37))
                {
                    power += mono * mono;
                    bridge += double(f.bridgeForce) * f.bridgeForce;
                }
                if (i >= int(rate * .05) && i < int(rate * .20))
                    early += mono * mono;
                if (i >= int(rate * .8))
                    late += mono * mono;
            }
            report << note << "," << velocity << "," << piano.frequency(note) << ","
                   << 10 * std::log10(power / (rate * .34) + 1e-30) << "," << 20 * std::log10(peak + 1e-30)
                   << "," << 10 * std::log10(4 * early / (late + 1e-30)) << ","
                   << 10 * std::log10(bridge / (rate * .34) + 1e-30) << "," << hammer << "\n";
        }
        std::cout << "Wrote 49-key survey to " << argv[2] << "\n";
        return 0;
    }
    if (argc >= 3 && std::string(argv[1]) == "--render")
    {
        const int rate = argc > 3 ? std::stoi(argv[3]) : 48000;
        const int note = argc > 4 ? std::stoi(argv[4]) : 108;
        const float velocity = argc > 5 ? std::stof(argv[5]) : 4.375;
        PianoPhysical piano;
        PianoPhysical::Configuration config;
        if (argc > 6)
            config.oversampling = std::stoi(argv[6]);
        piano.prepare(rate, config);
        PianoPhysical::Parameters parameters;
        if (argc > 7)
            parameters.mass = std::stod(argv[7]);
        if (argc > 8)
            parameters.stiffness = std::stod(argv[8]);
        if (argc > 9)
            parameters.position = std::stod(argv[9]);
        piano.setParameters(parameters);
        piano.setKey(note, true);
        piano.strike(note, velocity);
        std::vector<float> data(size_t(rate) * 3 * 4);
        const auto start = std::chrono::steady_clock::now();
        double peak = 0, force = 0;
        for (int i = 0; i < rate * 3; ++i)
        {
            if (i == int(rate * 1.6))
                piano.setKey(note, false);
            const auto f = piano.process();
            data[size_t(i) * 4] = f.left;
            data[size_t(i) * 4 + 1] = f.right;
            data[size_t(i) * 4 + 2] = f.bridgeForce;
            data[size_t(i) * 4 + 3] = f.hammerForce;
            require(std::isfinite(f.left), "Nonfinite render");
            peak = std::max(peak, std::abs(double(f.left)));
            force = std::max(force, double(f.hammerForce));
        }
        writeWave(argv[2], data, rate);
        std::cout << "note=" << note << " rate=" << rate << " f=" << piano.frequency(note) << " peak=" << peak
                  << " hammerPeak=" << force << " energy=" << piano.energy() << " seconds="
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << "\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--benchmark")
    {
        PianoPhysical piano;
        piano.prepare(48000);
        piano.setPedal(1);
        for (int note : {60, 64, 67, 72, 76, 79, 84, 88, 91, 96, 100, 103})
        {
            piano.setKey(note, true);
            piano.strike(note, 5);
        }
        const auto start = std::chrono::steady_clock::now();
        renderEnergy(piano, 48000 * 3);
        std::cout << "3 seconds, pedal down, 12 notes: "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << " seconds\n";
        return 0;
    }
    for (double rate : {44100.0, 48000.0, 96000.0})
    {
        PianoPhysical piano;
        piano.prepare(rate);
        require(renderEnergy(piano, 64) == 0, "Reset model must be exactly silent");
        for (int note : {60, 72, 84, 96, 108})
        {
            piano.reset();
            piano.setKey(note, true);
            piano.strike(note, 8);
            const double initial = piano.energy();
            const double signal = renderEnergy(piano, int(rate * .08));
            require(signal > 1e-15, "Missing struck note");
            require(piano.energy() <= initial * 1.01, "Unforced passive model gained energy during contact");
            double previous = piano.energy();
            for (int block = 0; block < 16; ++block)
            {
                renderEnergy(piano, 64);
                const double next = piano.energy();
                require(next <= previous * 1.00001 + 1e-15, "Passive tail gained energy");
                previous = next;
            }
            std::cout << "PASS passive note " << note << " @ " << rate << " signal=" << signal << "\n";
        }
        piano.reset();
        piano.setPedal(1);
        piano.setKey(72, true);
        piano.strike(72, 5);
        renderEnergy(piano, int(rate * .25));
        require(piano.isActive(84), "Unstruck octave must receive sympathetic energy");
        piano.reset();
        require(renderEnergy(piano, 256) == 0, "Reset left a body or decimator tail");
    }
    PianoPhysical held, released;
    held.prepare(48000);
    released.prepare(48000);
    held.setKey(96, true);
    released.setKey(96, true);
    held.strike(96, 4);
    released.strike(96, 4);
    for (int i = 0; i < 12000; ++i)
    {
        if (i == 1000)
            released.setKey(96, false);
        const auto a = held.process(), b = released.process();
        require(a.left == b.left && a.right == b.right, "Undamped top register responds to key release");
    }
    std::cout << "PASS undamped register and reset\n";
    double previousPedalEnergy = 0;
    for (float pedal : {0.0f, .5f, 1.0f})
    {
        PianoPhysical piano;
        piano.prepare(48000);
        piano.setPedal(pedal);
        piano.setKey(72, true);
        piano.strike(72, 4.375f);
        renderEnergy(piano, 4800);
        piano.setKey(72, false);
        renderEnergy(piano, 14400);
        const double tail = renderEnergy(piano, 14400);
        require(tail > previousPedalEnergy * 2,
                "Half-pedal positions do not produce progressively longer decay");
        previousPedalEnergy = tail;
    }
    std::cout << "PASS continuous pedal decay\n";
    for (int note : {72, 96, 108})
    {
        double previous = 0;
        for (float velocity : {.8f, 4.375f, 10.0f})
        {
            PianoPhysical piano;
            piano.prepare(48000);
            piano.setKey(note, true);
            piano.strike(note, velocity);
            const double signal = renderEnergy(piano, 18000);
            require(signal > previous * 1.5, "Hammer dynamics are not increasing from soft to loud");
            previous = signal;
        }
        double reference = 0;
        for (int rate : {44100, 48000, 96000})
            for (int factor : {2, 4})
            {
                PianoPhysical piano;
                PianoPhysical::Configuration config;
                config.oversampling = factor;
                piano.prepare(rate, config);
                piano.setKey(note, true);
                piano.strike(note, 4.375f);
                renderEnergy(piano, int(rate * .03));
                const double signal = renderEnergy(piano, int(rate * .34)) / (rate * .34);
                if (reference == 0)
                    reference = signal;
                require(std::abs(10 * std::log10(signal / reference)) < .8,
                        "Treble level changes excessively with sample rate or oversampling");
            }
    }
    std::cout << "PASS hammer dynamics and 2x/4x sample-rate agreement\n";
    PianoPhysical ringing, fresh;
    ringing.prepare(48000);
    fresh.prepare(48000);
    ringing.setKey(84, true);
    ringing.strike(84, 5);
    renderEnergy(ringing, 7200);
    ringing.strike(84, 3);
    fresh.setKey(84, true);
    fresh.strike(84, 3);
    double difference = 0;
    for (int i = 0; i < 4800; ++i)
        difference += std::abs(double(ringing.process().left) - fresh.process().left);
    require(difference > 1e-3, "Restrike discarded the vibrating string state");
    std::cout << "PASS phase-preserving repeated strikes\n";
    for (bool large : {false, true})
    {
        PianoPhysical piano;
        piano.prepare(48000);
        PianoPhysical::Parameters p;
        p.young = large ? 400e9 : 80e9;
        p.density = large ? 16000 : 4000;
        p.mass = large ? 3 : .35;
        p.radius = large ? 1.6 : .6;
        p.length = large ? .6 : 1.6;
        p.stiffness = large ? 4 : .05;
        p.exponent = large ? 2 : .5;
        p.bridge = large ? 32 : 200000;
        p.horizontal = large ? 240 : 2000000;
        p.coupling = large ? 10000 : 1;
        piano.setParameters(p);
        piano.setPedal(.7f);
        for (int note : {60, 72, 84, 96, 108})
        {
            piano.setKey(note, true);
            piano.strike(note, 15);
        }
        require(std::isfinite(renderEnergy(piano, 24000)), "Nonfinite result at extreme physical parameters");
    }
    std::cout << "PASS bounded physical parameters\n";
    PianoPhysical chord;
    chord.prepare(48000);
    chord.setPedal(1);
    for (int note : {60, 64, 67, 72, 76, 79, 84, 88, 91, 96, 100, 103})
    {
        chord.setKey(note, true);
        chord.strike(note, 8);
    }
    const double initial = chord.energy();
    renderEnergy(chord, 4800);
    require(chord.energy() <= initial * 1.01, "Shared bridge gained energy during a chord attack");
    double previous = chord.energy();
    for (int block = 0; block < 32; ++block)
    {
        renderEnergy(chord, 128);
        const double next = chord.energy();
        require(next <= previous * 1.00001 + 1e-15, "Coupled chord tail gained energy");
        previous = next;
    }
    std::cout << "PASS passive shared-bridge chord\n";
    PianoPhysical native, portable;
    PianoPhysical::Configuration portableConfig;
    portableConfig.nativeVectorization = false;
    native.prepare(48000);
    portable.prepare(48000, portableConfig);
    for (auto *piano : {&native, &portable})
    {
        piano->setPedal(.7f);
        for (int note : {60, 72, 84, 96, 108})
        {
            piano->setKey(note, true);
            piano->strike(note, 5);
        }
    }
    double error = 0, referencePower = 0;
    for (int i = 0; i < 12000; ++i)
    {
        const auto a = native.process(), b = portable.process();
        const double delta = double(a.left) - b.left;
        error += delta * delta;
        referencePower += double(b.left) * b.left;
    }
    require(error < referencePower * 1e-6, "Native and portable SIMD paths diverge");
    std::cout << "PASS native/portable SIMD agreement\n";
    PianoPhysical adaptive, full;
    PianoPhysical::Configuration fullConfig;
    fullConfig.adaptiveModes = false;
    adaptive.prepare(48000);
    full.prepare(48000, fullConfig);
    for (auto *piano : {&adaptive, &full})
    {
        piano->setPedal(1);
        for (int note : {60, 72, 84, 96, 108})
        {
            piano->setKey(note, true);
            piano->strike(note, 5);
        }
    }
    error = referencePower = 0;
    for (int i = 0; i < 144000; ++i)
    {
        if (i == 96000)
        {
            adaptive.strike(72, 3);
            full.strike(72, 3);
        }
        const auto a = adaptive.process(), b = full.process();
        const double delta = double(a.left) - b.left;
        error += delta * delta;
        referencePower += double(b.left) * b.left;
    }
    require(error < referencePower * 1e-6, "Parking inaudible modes changes the rendered sound");
    require(adaptive.activeStringModes() < full.activeStringModes(), "Quiet modal groups were never parked");
    std::cout << "PASS adaptive/full modal agreement: " << 10 * std::log10(error / referencePower)
              << " dB relative error; modes " << adaptive.activeStringModes() << "/"
              << full.activeStringModes() << "\n";
    return 0;
}
catch (const std::exception &e)
{
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
}
