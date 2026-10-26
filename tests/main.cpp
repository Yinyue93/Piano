#include <JuceHeader.h>
#include "qiano.h"
#include "filter.h"
#include "hammer.h"
#include "dwgs.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

// Current test tracking for crash reporting
static const char* g_currentTestName = "initialization";
static const char* g_currentTestPhase = "setup";

void signalHandler(int signal)
{
    std::cerr << "\n";
    std::cerr << "==========================================\n";
    std::cerr << "FATAL: Test crashed!\n";
    std::cerr << "==========================================\n";
    std::cerr << "Signal: ";

    switch (signal)
    {
        case SIGSEGV:
            std::cerr << "SIGSEGV (Segmentation fault)\n";
            std::cerr << "Cause: Invalid memory access\n";
            break;
        case SIGABRT:
            std::cerr << "SIGABRT (Abort)\n";
            std::cerr << "Cause: Assertion failure or abort() called\n";
            break;
        case SIGFPE:
            std::cerr << "SIGFPE (Floating point exception)\n";
            std::cerr << "Cause: Division by zero or invalid float operation\n";
            break;
        case SIGILL:
            std::cerr << "SIGILL (Illegal instruction)\n";
            std::cerr << "Cause: Corrupted code or invalid CPU instruction\n";
            break;
#ifndef _WIN32
        case SIGBUS:
            std::cerr << "SIGBUS (Bus error)\n";
            std::cerr << "Cause: Misaligned memory access\n";
            break;
#endif
        default:
            std::cerr << signal << " (Unknown signal)\n";
            break;
    }

    std::cerr << "\n";
    std::cerr << "Crash location:\n";
    std::cerr << "  Test: " << g_currentTestName << "\n";
    std::cerr << "  Phase: " << g_currentTestPhase << "\n";
    std::cerr << "\n";
    std::cerr << "Suggestions:\n";
    std::cerr << "  - Run with AddressSanitizer: ./test.sh Release ON\n";
    std::cerr << "  - Run in Debug mode: ./test.sh Debug\n";
    std::cerr << "  - Use a debugger to get a stack trace\n";
    std::cerr << "==========================================\n";

    // Re-raise to get proper exit code
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void installSignalHandlers()
{
    std::signal(SIGSEGV, signalHandler);
    std::signal(SIGABRT, signalHandler);
    std::signal(SIGFPE, signalHandler);
    std::signal(SIGILL, signalHandler);
#ifndef _WIN32
    std::signal(SIGBUS, signalHandler);
#endif
}

// Test configuration
constexpr float SAMPLE_RATE = 44100.0f;
constexpr int BLOCK_SIZE = 512;
constexpr float NOTE_DURATION_SECONDS = 0.5f;
constexpr float SILENCE_BETWEEN_NOTES = 0.1f;
constexpr float VELOCITY = 0.8f;
constexpr float TAIL_SECONDS = 2.0f;

// Comparison threshold - maximum allowed RMS difference
// Note: The Piano synth has some non-deterministic behavior in the reverb/soundboard,
// so we use a relatively high threshold to allow for minor variations while still
// catching major regressions (silence, crashes, significant audio changes).
constexpr float MAX_RMS_DIFFERENCE = 0.1f;

// Scale notes (C major scale from C4 to C5)
const int SCALE_NOTES[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
constexpr int NUM_SCALE_NOTES = sizeof(SCALE_NOTES) / sizeof(SCALE_NOTES[0]);

// Chord definitions (root notes and intervals)
struct ChordDefinition {
    const char* name;
    int rootNote;
    std::vector<int> intervals; // intervals from root
};

const std::vector<ChordDefinition> TEST_CHORDS = {
    { "C Major", 60, { 0, 4, 7 } },
    { "C Minor", 60, { 0, 3, 7 } },
    { "G Major", 55, { 0, 4, 7 } },
    { "F Major", 53, { 0, 4, 7 } },
    { "A Minor", 57, { 0, 3, 7 } },
    { "D Minor", 50, { 0, 3, 7 } },
    { "E Major", 52, { 0, 4, 7 } },
    { "C Major 7", 60, { 0, 4, 7, 11 } },
    { "G7", 55, { 0, 4, 7, 10 } },
    { "Dm7", 50, { 0, 3, 7, 10 } },
    { "F Major (spread)", 41, { 0, 7, 12, 16 } },
    { "C Power Chord", 48, { 0, 7, 12 } },
};

//==============================================================================
// Reference renders
//==============================================================================

// Set by --update-references. Without it a missing reference fails the test,
// so a wrong path or a deleted file can't turn a test run into a baseline run.
static bool g_updateReferences = false;

static bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ch++)
    {
        auto data = buffer.getReadPointer (ch);

        for (int i = 0; i < buffer.getNumSamples(); i++)
            if (! std::isfinite (data[i]))
                return false;
    }

    return true;
}

static bool saveWavFile (const juce::File& file, const juce::AudioBuffer<float>& buffer)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::FileOutputStream> outputStream (file.createOutputStream());

    if (outputStream == nullptr)
    {
        std::cerr << "Could not create output stream for: " << file.getFullPathName() << std::endl;
        return false;
    }

    // 32-bit float to preserve full dynamic range
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wavFormat.createWriterFor (outputStream.get(), SAMPLE_RATE,
                                   static_cast<unsigned int> (buffer.getNumChannels()), 32, {}, 0));

    if (writer == nullptr)
    {
        std::cerr << "Could not create WAV writer" << std::endl;
        return false;
    }

    outputStream.release(); // writer takes ownership

    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

static std::unique_ptr<juce::AudioBuffer<float>> loadWavFile (const juce::File& file)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        std::cerr << "Could not create reader for: " << file.getFullPathName() << std::endl;
        return nullptr;
    }

    // The tests render at SAMPLE_RATE: a reference made at another rate can't
    // be compared sample by sample
    if (reader->sampleRate != SAMPLE_RATE)
    {
        std::cerr << "Reference sample rate is " << reader->sampleRate << " Hz, the tests render at "
                  << SAMPLE_RATE << " Hz" << std::endl;
        return nullptr;
    }

    auto buffer = std::make_unique<juce::AudioBuffer<float>> (static_cast<int> (reader->numChannels),
                                                              static_cast<int> (reader->lengthInSamples));

    reader->read (buffer.get(), 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

    std::cout << "Loaded reference: " << reader->lengthInSamples << " samples, "
              << reader->numChannels << " channels" << std::endl;

    return buffer;
}

static bool compareAudio (const juce::AudioBuffer<float>& generated, const juce::AudioBuffer<float>& reference)
{
    std::cout << "\nComparing audio..." << std::endl;

    // NaN fails every comparison below, so it has to be ruled out first
    if (! allFinite (generated))
    {
        std::cerr << "Generated audio contains NaN or infinite samples" << std::endl;
        return false;
    }

    if (! allFinite (reference))
    {
        std::cerr << "Reference audio contains NaN or infinite samples" << std::endl;
        return false;
    }

    int genSamples = generated.getNumSamples();
    int refSamples = reference.getNumSamples();

    if (genSamples != refSamples)
    {
        std::cerr << "Sample count mismatch! Generated: " << genSamples
                  << ", Reference: " << refSamples << std::endl;
        return false;
    }

    int genChannels = generated.getNumChannels();
    int refChannels = reference.getNumChannels();

    if (genChannels != refChannels)
    {
        std::cerr << "Channel count mismatch! Generated: " << genChannels
                  << ", Reference: " << refChannels << std::endl;
        return false;
    }

    float genMaxLevel = generated.getMagnitude (0, genSamples);
    float refMaxLevel = reference.getMagnitude (0, refSamples);

    std::cout << "Generated max level: " << genMaxLevel << std::endl;
    std::cout << "Reference max level: " << refMaxLevel << std::endl;

    // Check that audio was produced (not silence)
    if (genMaxLevel < 0.01f)
    {
        std::cerr << "Generated audio is too quiet (possibly silence)" << std::endl;
        return false;
    }

    if (refMaxLevel < 0.01f)
    {
        std::cerr << "Reference audio is too quiet (possibly silence)" << std::endl;
        return false;
    }

    // Check that levels are in a similar range (within 50% of each other)
    float levelRatio = genMaxLevel / refMaxLevel;
    if (levelRatio < 0.5f || levelRatio > 2.0f)
    {
        std::cerr << "Audio levels differ significantly! Ratio: " << levelRatio << std::endl;
        return false;
    }

    // Calculate RMS difference for each channel
    double totalRmsDifference = 0.0;

    for (int channel = 0; channel < genChannels; ++channel)
    {
        const float* genData = generated.getReadPointer (channel);
        const float* refData = reference.getReadPointer (channel);

        double sumSquaredDiff = 0.0;
        float maxDiff = 0.0f;
        int maxDiffSample = 0;

        for (int i = 0; i < genSamples; ++i)
        {
            float diff = genData[i] - refData[i];
            sumSquaredDiff += static_cast<double> (diff * diff);

            if (std::abs (diff) > maxDiff)
            {
                maxDiff = std::abs (diff);
                maxDiffSample = i;
            }
        }

        double rms = std::sqrt (sumSquaredDiff / genSamples);
        totalRmsDifference += rms;

        std::cout << "Channel " << channel << ":" << std::endl;
        std::cout << "  RMS difference: " << rms << std::endl;
        std::cout << "  Max difference: " << maxDiff << " at sample " << maxDiffSample << std::endl;
    }

    double avgRmsDifference = totalRmsDifference / genChannels;
    std::cout << "\nAverage RMS difference: " << avgRmsDifference << std::endl;
    std::cout << "Threshold: " << MAX_RMS_DIFFERENCE << std::endl;

    if (avgRmsDifference <= MAX_RMS_DIFFERENCE)
    {
        std::cout << "\nTest PASSED" << std::endl;
        return true;
    }

    std::cerr << "\nTest FAILED - Audio output differs from reference!" << std::endl;
    return false;
}

// Compares a render with its reference, or rewrites the reference when run
// with --update-references
static bool checkAgainstReference (const juce::AudioBuffer<float>& generated, const juce::File& referenceFile)
{
    if (g_updateReferences)
    {
        if (! allFinite (generated))
        {
            std::cerr << "Generated audio contains NaN or infinite samples, not saving it" << std::endl;
            return false;
        }

        std::cout << "Updating reference: " << referenceFile.getFullPathName() << std::endl;

        if (! saveWavFile (referenceFile, generated))
        {
            std::cerr << "Failed to save reference file" << std::endl;
            return false;
        }

        std::cout << "Test PASSED (reference updated)" << std::endl;
        return true;
    }

    if (! referenceFile.existsAsFile())
    {
        std::cerr << "Reference file not found: " << referenceFile.getFullPathName() << std::endl;
        std::cerr << "Pass the reference folder (tests/reference) as the first argument, or run with "
                     "--update-references to create it" << std::endl;
        return false;
    }

    std::cout << "Loading reference file: " << referenceFile.getFullPathName() << std::endl;
    auto referenceAudio = loadWavFile (referenceFile);

    if (referenceAudio == nullptr)
    {
        std::cerr << "Failed to load reference file" << std::endl;
        return false;
    }

    return compareAudio (generated, *referenceAudio);
}

class PianoScaleTest
{
public:
    PianoScaleTest(const juce::String& referenceDir)
        : referencePath(referenceDir)
    {
    }

    bool run()
    {
        std::cout << "Piano Scale Test" << std::endl;
        std::cout << "=================" << std::endl;

        // Generate audio from the Piano class
        auto generatedAudio = generateScaleAudio();
        if (generatedAudio == nullptr)
        {
            std::cerr << "Failed to generate audio" << std::endl;
            return false;
        }

        std::cout << "Generated " << generatedAudio->getNumSamples() << " samples" << std::endl;

        return checkAgainstReference (*generatedAudio, juce::File (referencePath + "/piano_scale_reference.wav"));
    }

private:
    juce::String referencePath;

    std::unique_ptr<juce::AudioBuffer<float>> generateScaleAudio()
    {
        Piano piano;
        piano.init(SAMPLE_RATE, BLOCK_SIZE);

        // Set default parameters (mid-range values for consistent output)
        for (int i = 0; i < NumParams; i++)
        {
            piano.setParameter(i, 0.5f);
        }
        // Set volume to a reasonable level
        piano.setParameter(pVolume, 0.7f);
        // The references were rendered when Soundboard Size had no effect,
        // which is the same as its plugin default of 0
        piano.setParameter(pSoundboardSize, 0.0f);

        // Calculate total duration
        float totalDuration = (NOTE_DURATION_SECONDS + SILENCE_BETWEEN_NOTES) * NUM_SCALE_NOTES + TAIL_SECONDS;
        int totalSamples = static_cast<int>(totalDuration * SAMPLE_RATE);
        int numBlocks = (totalSamples + BLOCK_SIZE - 1) / BLOCK_SIZE;
        totalSamples = numBlocks * BLOCK_SIZE;

        // Allocate output buffer (stereo)
        auto outputBuffer = std::make_unique<juce::AudioBuffer<float>>(2, totalSamples);
        outputBuffer->clear();

        // Calculate timing
        int samplesPerNote = static_cast<int>((NOTE_DURATION_SECONDS + SILENCE_BETWEEN_NOTES) * SAMPLE_RATE);
        int noteOnDurationSamples = static_cast<int>(NOTE_DURATION_SECONDS * SAMPLE_RATE);

        // Process audio in blocks
        int currentSample = 0;
        int currentNoteIndex = 0;
        int noteStartSample = 0;
        bool noteIsOn = false;

        std::cout << "Generating scale audio..." << std::endl;

        while (currentSample < totalSamples)
        {
            int samplesToProcess = std::min(BLOCK_SIZE, totalSamples - currentSample);

            // Create MIDI buffer for this block
            juce::MidiBuffer midiBuffer;

            // Check if we need to trigger note on/off
            if (currentNoteIndex < NUM_SCALE_NOTES)
            {
                int noteAbsoluteStart = currentNoteIndex * samplesPerNote;
                int noteAbsoluteEnd = noteAbsoluteStart + noteOnDurationSamples;

                // Note on
                if (!noteIsOn && currentSample <= noteAbsoluteStart && currentSample + samplesToProcess > noteAbsoluteStart)
                {
                    int sampleOffset = noteAbsoluteStart - currentSample;
                    midiBuffer.addEvent(
                        juce::MidiMessage::noteOn(1, SCALE_NOTES[currentNoteIndex], VELOCITY),
                        sampleOffset
                    );
                    noteIsOn = true;
                    noteStartSample = noteAbsoluteStart;
                    std::cout << "  Note ON: " << SCALE_NOTES[currentNoteIndex] << " at sample " << noteAbsoluteStart << std::endl;
                }

                // Note off
                if (noteIsOn && currentSample <= noteAbsoluteEnd && currentSample + samplesToProcess > noteAbsoluteEnd)
                {
                    int sampleOffset = noteAbsoluteEnd - currentSample;
                    midiBuffer.addEvent(
                        juce::MidiMessage::noteOff(1, SCALE_NOTES[currentNoteIndex]),
                        sampleOffset
                    );
                    noteIsOn = false;
                    std::cout << "  Note OFF: " << SCALE_NOTES[currentNoteIndex] << " at sample " << noteAbsoluteEnd << std::endl;
                    currentNoteIndex++;
                }
            }

            // Get pointers for stereo output
            float* channels[2] = {
                outputBuffer->getWritePointer(0, currentSample),
                outputBuffer->getWritePointer(1, currentSample)
            };

            // Process the block
            piano.process(channels, samplesToProcess, midiBuffer);

            currentSample += samplesToProcess;
        }

        // Normalize to prevent clipping issues in comparison
        float maxLevel = outputBuffer->getMagnitude(0, totalSamples);
        if (maxLevel > 0.0f && maxLevel < 1.0f)
        {
            // Don't normalize - keep original levels for consistent comparison
        }

        std::cout << "Audio generation complete. Max level: " << maxLevel << std::endl;

        return outputBuffer;
    }
};

class PianoChordTest
{
public:
    PianoChordTest(const juce::String& referenceDir)
        : referencePath(referenceDir)
    {
    }

    bool run()
    {
        std::cout << "\nPiano Chord Test" << std::endl;
        std::cout << "=================" << std::endl;

        auto generatedAudio = generateChordAudio();
        if (generatedAudio == nullptr)
        {
            std::cerr << "Failed to generate chord audio" << std::endl;
            return false;
        }

        std::cout << "Generated " << generatedAudio->getNumSamples() << " samples" << std::endl;

        return checkAgainstReference (*generatedAudio, juce::File (referencePath + "/piano_chord_reference.wav"));
    }

private:
    juce::String referencePath;

    std::unique_ptr<juce::AudioBuffer<float>> generateChordAudio()
    {
        Piano piano;
        piano.init(SAMPLE_RATE, BLOCK_SIZE);

        for (int i = 0; i < NumParams; i++)
        {
            piano.setParameter(i, 0.5f);
        }
        piano.setParameter(pVolume, 0.7f);
        // The references were rendered when Soundboard Size had no effect,
        // which is the same as its plugin default of 0
        piano.setParameter(pSoundboardSize, 0.0f);

        constexpr float CHORD_DURATION = 1.0f;
        constexpr float CHORD_SILENCE = 0.3f;

        float totalDuration = (CHORD_DURATION + CHORD_SILENCE) * TEST_CHORDS.size() + TAIL_SECONDS;
        int totalSamples = static_cast<int>(totalDuration * SAMPLE_RATE);
        int numBlocks = (totalSamples + BLOCK_SIZE - 1) / BLOCK_SIZE;
        totalSamples = numBlocks * BLOCK_SIZE;

        auto outputBuffer = std::make_unique<juce::AudioBuffer<float>>(2, totalSamples);
        outputBuffer->clear();

        int samplesPerChord = static_cast<int>((CHORD_DURATION + CHORD_SILENCE) * SAMPLE_RATE);
        int chordOnDurationSamples = static_cast<int>(CHORD_DURATION * SAMPLE_RATE);

        int currentSample = 0;
        size_t currentChordIndex = 0;
        bool chordIsOn = false;

        std::cout << "Generating chord audio..." << std::endl;

        while (currentSample < totalSamples)
        {
            int samplesToProcess = std::min(BLOCK_SIZE, totalSamples - currentSample);
            juce::MidiBuffer midiBuffer;

            if (currentChordIndex < TEST_CHORDS.size())
            {
                int chordAbsoluteStart = static_cast<int>(currentChordIndex) * samplesPerChord;
                int chordAbsoluteEnd = chordAbsoluteStart + chordOnDurationSamples;

                // Chord on
                if (!chordIsOn && currentSample <= chordAbsoluteStart && currentSample + samplesToProcess > chordAbsoluteStart)
                {
                    int sampleOffset = chordAbsoluteStart - currentSample;
                    const auto& chord = TEST_CHORDS[currentChordIndex];
                    std::cout << "  Chord ON: " << chord.name << " at sample " << chordAbsoluteStart << std::endl;

                    for (int interval : chord.intervals)
                    {
                        int note = chord.rootNote + interval;
                        midiBuffer.addEvent(
                            juce::MidiMessage::noteOn(1, note, VELOCITY),
                            sampleOffset
                        );
                    }
                    chordIsOn = true;
                }

                // Chord off
                if (chordIsOn && currentSample <= chordAbsoluteEnd && currentSample + samplesToProcess > chordAbsoluteEnd)
                {
                    int sampleOffset = chordAbsoluteEnd - currentSample;
                    const auto& chord = TEST_CHORDS[currentChordIndex];
                    std::cout << "  Chord OFF: " << chord.name << " at sample " << chordAbsoluteEnd << std::endl;

                    for (int interval : chord.intervals)
                    {
                        int note = chord.rootNote + interval;
                        midiBuffer.addEvent(
                            juce::MidiMessage::noteOff(1, note),
                            sampleOffset
                        );
                    }
                    chordIsOn = false;
                    currentChordIndex++;
                }
            }

            float* channels[2] = {
                outputBuffer->getWritePointer(0, currentSample),
                outputBuffer->getWritePointer(1, currentSample)
            };

            piano.process(channels, samplesToProcess, midiBuffer);
            currentSample += samplesToProcess;
        }

        float maxLevel = outputBuffer->getMagnitude(0, totalSamples);
        std::cout << "Audio generation complete. Max level: " << maxLevel << std::endl;

        return outputBuffer;
    }
};

class PianoStressTest
{
public:
    PianoStressTest(const juce::String& referenceDir)
        : referencePath(referenceDir)
    {
    }

    bool run()
    {
        std::cout << "\nPiano Stress Test (4 minutes)" << std::endl;
        std::cout << "==============================" << std::endl;

        auto generatedAudio = generateStressTestAudio();
        if (generatedAudio == nullptr)
        {
            std::cerr << "Failed to generate stress test audio" << std::endl;
            return false;
        }

        std::cout << "Generated " << generatedAudio->getNumSamples() << " samples ("
                  << (generatedAudio->getNumSamples() / SAMPLE_RATE) << " seconds)" << std::endl;

        return checkAgainstReference (*generatedAudio, juce::File (referencePath + "/piano_stress_reference.wav"));
    }

private:
    juce::String referencePath;

    // Simple pseudo-random number generator for deterministic results
    uint32_t seed = 12345;
    uint32_t nextRandom()
    {
        seed = seed * 1103515245 + 12345;
        return (seed >> 16) & 0x7FFF;
    }
    float randomFloat() { return static_cast<float>(nextRandom()) / 32767.0f; }
    int randomInt(int min, int max) { return min + (nextRandom() % (max - min + 1)); }

    struct NoteEvent {
        int startSample;
        int endSample;
        int note;
        float velocity;
        bool noteOnSent = false;
        bool noteOffSent = false;
    };

    std::unique_ptr<juce::AudioBuffer<float>> generateStressTestAudio()
    {
        Piano piano;
        piano.init(SAMPLE_RATE, BLOCK_SIZE);

        for (int i = 0; i < NumParams; i++)
        {
            piano.setParameter(i, 0.5f);
        }
        piano.setParameter(pVolume, 0.6f); // Slightly lower volume for stress test
        // The references were rendered when Soundboard Size had no effect,
        // which is the same as its plugin default of 0
        piano.setParameter(pSoundboardSize, 0.0f);

        constexpr float TEST_DURATION_SECONDS = 240.0f; // 4 minutes
        int totalSamples = static_cast<int>(TEST_DURATION_SECONDS * SAMPLE_RATE);
        int numBlocks = (totalSamples + BLOCK_SIZE - 1) / BLOCK_SIZE;
        totalSamples = numBlocks * BLOCK_SIZE;

        auto outputBuffer = std::make_unique<juce::AudioBuffer<float>>(2, totalSamples);
        outputBuffer->clear();

        // Generate note events
        std::vector<NoteEvent> noteEvents;
        seed = 12345; // Reset seed for deterministic generation

        std::cout << "Generating note events..." << std::endl;

        // Phase 1 (0-60s): Sparse notes, varying lengths
        generateSparseNotes(noteEvents, 0, 60, 2.0f, 0.5f, 3.0f);

        // Phase 2 (60-120s): Moderate density, some overlapping notes
        generateModerateDensityNotes(noteEvents, 60, 120, 0.3f, 0.2f, 1.5f);

        // Phase 3 (120-180s): High density, many overlapping notes, chords
        generateHighDensityNotes(noteEvents, 120, 180, 0.1f, 0.1f, 1.0f);

        // Phase 4 (180-240s): Mix of all patterns
        generateMixedNotes(noteEvents, 180, 240);

        std::cout << "Generated " << noteEvents.size() << " note events" << std::endl;

        // Sort events by start time for efficient processing
        std::sort(noteEvents.begin(), noteEvents.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.startSample < b.startSample; });

        // Process audio
        int currentSample = 0;
        int progress = 0;

        std::cout << "Processing audio..." << std::endl;

        while (currentSample < totalSamples)
        {
            int samplesToProcess = std::min(BLOCK_SIZE, totalSamples - currentSample);
            juce::MidiBuffer midiBuffer;

            // Process note events for this block
            for (auto& event : noteEvents)
            {
                // Note on
                if (!event.noteOnSent &&
                    event.startSample >= currentSample &&
                    event.startSample < currentSample + samplesToProcess)
                {
                    int offset = event.startSample - currentSample;
                    midiBuffer.addEvent(
                        juce::MidiMessage::noteOn(1, event.note, event.velocity),
                        offset
                    );
                    event.noteOnSent = true;
                }

                // Note off
                if (!event.noteOffSent &&
                    event.endSample >= currentSample &&
                    event.endSample < currentSample + samplesToProcess)
                {
                    int offset = event.endSample - currentSample;
                    midiBuffer.addEvent(
                        juce::MidiMessage::noteOff(1, event.note),
                        offset
                    );
                    event.noteOffSent = true;
                }
            }

            float* channels[2] = {
                outputBuffer->getWritePointer(0, currentSample),
                outputBuffer->getWritePointer(1, currentSample)
            };

            piano.process(channels, samplesToProcess, midiBuffer);
            currentSample += samplesToProcess;

            // Progress indicator
            int newProgress = (currentSample * 100) / totalSamples;
            if (newProgress >= progress + 10)
            {
                progress = newProgress;
                std::cout << "  " << progress << "% complete" << std::endl;
            }
        }

        float maxLevel = outputBuffer->getMagnitude(0, totalSamples);
        std::cout << "Audio generation complete. Max level: " << maxLevel << std::endl;

        return outputBuffer;
    }

    void generateSparseNotes(std::vector<NoteEvent>& events, float startSec, float endSec,
                              float avgGap, float minDuration, float maxDuration)
    {
        float currentTime = startSec;
        while (currentTime < endSec)
        {
            NoteEvent event;
            event.startSample = static_cast<int>(currentTime * SAMPLE_RATE);
            float duration = minDuration + randomFloat() * (maxDuration - minDuration);
            event.endSample = static_cast<int>((currentTime + duration) * SAMPLE_RATE);
            event.note = randomInt(36, 96); // Full piano range
            event.velocity = 0.4f + randomFloat() * 0.5f;
            events.push_back(event);

            currentTime += duration + avgGap * (0.5f + randomFloat());
        }
    }

    void generateModerateDensityNotes(std::vector<NoteEvent>& events, float startSec, float endSec,
                                       float avgGap, float minDuration, float maxDuration)
    {
        float currentTime = startSec;
        while (currentTime < endSec)
        {
            // Sometimes generate 2-3 notes at once
            int notesAtOnce = randomInt(1, 3);
            int baseNote = randomInt(48, 84);

            for (int i = 0; i < notesAtOnce; i++)
            {
                NoteEvent event;
                event.startSample = static_cast<int>(currentTime * SAMPLE_RATE);
                float duration = minDuration + randomFloat() * (maxDuration - minDuration);
                event.endSample = static_cast<int>((currentTime + duration) * SAMPLE_RATE);
                event.note = baseNote + randomInt(-12, 12);
                event.note = std::max(21, std::min(108, event.note)); // Clamp to piano range
                event.velocity = 0.3f + randomFloat() * 0.6f;
                events.push_back(event);
            }

            currentTime += avgGap * (0.5f + randomFloat());
        }
    }

    void generateHighDensityNotes(std::vector<NoteEvent>& events, float startSec, float endSec,
                                   float avgGap, float minDuration, float maxDuration)
    {
        float currentTime = startSec;
        while (currentTime < endSec)
        {
            // Generate chords and arpeggios
            int pattern = randomInt(0, 3);

            if (pattern == 0)
            {
                // Full chord
                int root = randomInt(48, 72);
                int chordType = randomInt(0, 3);
                std::vector<int> intervals;

                switch (chordType)
                {
                    case 0: intervals = { 0, 4, 7 }; break; // Major
                    case 1: intervals = { 0, 3, 7 }; break; // Minor
                    case 2: intervals = { 0, 4, 7, 11 }; break; // Major 7
                    case 3: intervals = { 0, 3, 7, 10 }; break; // Minor 7
                }

                float duration = minDuration + randomFloat() * (maxDuration - minDuration);
                for (int interval : intervals)
                {
                    NoteEvent event;
                    event.startSample = static_cast<int>(currentTime * SAMPLE_RATE);
                    event.endSample = static_cast<int>((currentTime + duration) * SAMPLE_RATE);
                    event.note = root + interval;
                    event.velocity = 0.4f + randomFloat() * 0.4f;
                    events.push_back(event);
                }
            }
            else if (pattern == 1)
            {
                // Arpeggio
                int root = randomInt(48, 72);
                std::vector<int> intervals = { 0, 4, 7, 12, 7, 4 };
                float noteTime = currentTime;
                float noteDuration = 0.1f + randomFloat() * 0.15f;

                for (int interval : intervals)
                {
                    NoteEvent event;
                    event.startSample = static_cast<int>(noteTime * SAMPLE_RATE);
                    event.endSample = static_cast<int>((noteTime + noteDuration * 2) * SAMPLE_RATE);
                    event.note = root + interval;
                    event.velocity = 0.5f + randomFloat() * 0.3f;
                    events.push_back(event);
                    noteTime += noteDuration;
                }
            }
            else
            {
                // Rapid succession
                int numNotes = randomInt(4, 8);
                float noteTime = currentTime;
                for (int i = 0; i < numNotes; i++)
                {
                    NoteEvent event;
                    event.startSample = static_cast<int>(noteTime * SAMPLE_RATE);
                    float duration = 0.05f + randomFloat() * 0.2f;
                    event.endSample = static_cast<int>((noteTime + duration) * SAMPLE_RATE);
                    event.note = randomInt(36, 96);
                    event.velocity = 0.3f + randomFloat() * 0.5f;
                    events.push_back(event);
                    noteTime += 0.05f + randomFloat() * 0.1f;
                }
            }

            currentTime += avgGap + randomFloat() * avgGap;
        }
    }

    void generateMixedNotes(std::vector<NoteEvent>& events, float startSec, float endSec)
    {
        // Alternate between different patterns
        float currentTime = startSec;
        int section = 0;

        while (currentTime < endSec)
        {
            float sectionEnd = std::min(currentTime + 10.0f, endSec);

            switch (section % 4)
            {
                case 0:
                    generateSparseNotes(events, currentTime, sectionEnd, 1.0f, 0.3f, 2.0f);
                    break;
                case 1:
                    generateModerateDensityNotes(events, currentTime, sectionEnd, 0.2f, 0.1f, 1.0f);
                    break;
                case 2:
                    generateHighDensityNotes(events, currentTime, sectionEnd, 0.15f, 0.1f, 0.8f);
                    break;
                case 3:
                    // Very dense - stress test
                    generateHighDensityNotes(events, currentTime, sectionEnd, 0.05f, 0.05f, 0.5f);
                    break;
            }

            currentTime = sectionEnd;
            section++;
        }
    }
};

// =============================================================================
// Unit Tests for Internal Classes
// =============================================================================

class FilterUnitTest
{
public:
    bool run()
    {
        std::cout << "\nFilter Unit Tests" << std::endl;
        std::cout << "=================" << std::endl;

        bool allPassed = true;

        allPassed &= testFilterCreation();
        allPassed &= testBiquadHP();
        allPassed &= testThiran();
        allPassed &= testLoss();
        allPassed &= testDelay();
        allPassed &= testDWGResonator();

        if (allPassed)
            std::cout << "All Filter unit tests PASSED" << std::endl;
        else
            std::cerr << "Some Filter unit tests FAILED" << std::endl;

        return allPassed;
    }

private:
    bool testFilterCreation()
    {
        std::cout << "  Testing Filter creation..." << std::endl;

        // Test basic filter allocation with different sizes
        Filter f1(2);
        Filter f2(4);
        Filter f3(16);

        // Verify that buffers were allocated (non-null)
        if (f1.b == nullptr || f1.a == nullptr || f1.x == nullptr || f1.y == nullptr)
        {
            std::cerr << "    FAILED: Filter buffers not allocated" << std::endl;
            return false;
        }

        std::cout << "    Filter creation: PASSED" << std::endl;
        return true;
    }

    bool testBiquadHP()
    {
        std::cout << "  Testing BiquadHP filter..." << std::endl;

        BiquadHP hp;

        // Create a high-pass filter at 1000 Hz (omega = 2*pi*f/Fs)
        float omega = 2.0f * float(PI) * 1000.0f / 44100.0f;
        float Q = 0.707f; // Butterworth Q

        hp.create(omega, Q);
        hp.init();

        // Test that filter coefficients are set
        if (hp.n != 2)
        {
            std::cerr << "    FAILED: BiquadHP order incorrect" << std::endl;
            return false;
        }

        // Test filtering with an impulse - verify filter produces output
        float impulseResponse[50];
        impulseResponse[0] = hp.filter(1.0f);
        for (int i = 1; i < 50; i++)
        {
            impulseResponse[i] = hp.filter(0.0f);
        }

        // Verify filter produces some output (not all zeros)
        float maxOutput = 0.0f;
        for (int i = 0; i < 50; i++)
        {
            maxOutput = std::max(maxOutput, std::abs(impulseResponse[i]));
        }

        if (maxOutput < 0.001f)
        {
            std::cerr << "    FAILED: BiquadHP not producing output" << std::endl;
            return false;
        }

        // Verify no NaN or Inf values
        for (int i = 0; i < 50; i++)
        {
            if (std::isnan(impulseResponse[i]) || std::isinf(impulseResponse[i]))
            {
                std::cerr << "    FAILED: BiquadHP produced NaN or Inf" << std::endl;
                return false;
            }
        }

        std::cout << "    BiquadHP filter: PASSED" << std::endl;
        return true;
    }

    bool testThiran()
    {
        std::cout << "  Testing Thiran allpass filter..." << std::endl;

        Thiran thiran(4);

        // Create a fractional delay of 3.5 samples with order 2
        thiran.create(3.5f, 2);

        // Verify filter order
        if (thiran.n != 2)
        {
            std::cerr << "    FAILED: Thiran order incorrect" << std::endl;
            return false;
        }

        // Test that it's an allpass filter - magnitude should be 1
        // Send an impulse and check that energy is preserved
        float impulseResponse[20];
        impulseResponse[0] = thiran.filter(1.0f);
        for (int i = 1; i < 20; i++)
        {
            impulseResponse[i] = thiran.filter(0.0f);
        }

        // Sum of squares should be approximately 1 (energy preservation)
        float energy = 0.0f;
        for (int i = 0; i < 20; i++)
        {
            energy += impulseResponse[i] * impulseResponse[i];
        }

        if (std::abs(energy - 1.0f) > 0.1f)
        {
            std::cerr << "    FAILED: Thiran not preserving energy (energy=" << energy << ")" << std::endl;
            return false;
        }

        std::cout << "    Thiran allpass filter: PASSED" << std::endl;
        return true;
    }

    bool testLoss()
    {
        std::cout << "  Testing Loss filter..." << std::endl;

        Loss loss;

        // Create a loss filter for middle C (262 Hz)
        float f0 = 262.0f;
        float c1 = 0.5f;
        float c3 = 10.0f;

        loss.create(f0, c1, c3, 1.0f);

        // Loss filter should attenuate signal over time
        float output = 0.0f;
        for (int i = 0; i < 100; i++)
        {
            output = loss.filter(1.0f);
        }

        // Output should be less than input (loss)
        if (output >= 1.0f)
        {
            std::cerr << "    FAILED: Loss filter not attenuating signal" << std::endl;
            return false;
        }

        std::cout << "    Loss filter: PASSED" << std::endl;
        return true;
    }

    bool testDelay()
    {
        std::cout << "  Testing Delay line..." << std::endl;

        Delay<1024> delay;
        delay.setDelay(10);
        delay.clear();

        // Test that impulse is delayed correctly
        float output = delay.goDelay(1.0f); // Input impulse

        // First 9 outputs should be 0
        for (int i = 0; i < 9; i++)
        {
            output = delay.goDelay(0.0f);
            if (output != 0.0f)
            {
                std::cerr << "    FAILED: Delay not working correctly at sample " << i << std::endl;
                return false;
            }
        }

        // 10th output should be the impulse
        output = delay.goDelay(0.0f);
        if (std::abs(output - 1.0f) > 0.0001f)
        {
            std::cerr << "    FAILED: Impulse not appearing at correct delay (got " << output << ")" << std::endl;
            return false;
        }

        std::cout << "    Delay line: PASSED" << std::endl;
        return true;
    }

    bool testDWGResonator()
    {
        std::cout << "  Testing DWGResonator..." << std::endl;

        DWGResonator resonator;

        // Create resonator at 440 Hz with some damping
        float omega = 2.0f * float(PI) * 440.0f / 44100.0f;
        float gamma = 0.001f;

        resonator.create(omega, gamma);

        // Excite with impulse and verify it produces oscillation
        float output = resonator.go(1.0f);

        float maxOutput = std::abs(output);
        int zeroCrossings = 0;
        float prevOutput = output;

        for (int i = 0; i < 1000; i++)
        {
            output = resonator.go(0.0f);

            if (! std::isfinite(output))
            {
                std::cerr << "    FAILED: DWGResonator produced NaN or Inf" << std::endl;
                return false;
            }

            maxOutput = std::max(maxOutput, std::abs(output));

            // Count zero crossings to verify oscillation
            if ((prevOutput >= 0 && output < 0) || (prevOutput < 0 && output >= 0))
                zeroCrossings++;

            prevOutput = output;
        }

        // Should have oscillations (multiple zero crossings)
        if (zeroCrossings < 10)
        {
            std::cerr << "    FAILED: DWGResonator not oscillating (zeroCrossings=" << zeroCrossings << ")" << std::endl;
            return false;
        }

        // Output should decay but still be present
        if (maxOutput < 0.001f)
        {
            std::cerr << "    FAILED: DWGResonator output too quiet" << std::endl;
            return false;
        }

        std::cout << "    DWGResonator: PASSED" << std::endl;
        return true;
    }
};

class HammerUnitTest
{
public:
    bool run()
    {
        std::cout << "\nHammer Unit Tests" << std::endl;
        std::cout << "=================" << std::endl;

        bool allPassed = true;

        allPassed &= testHammerCreation();
        allPassed &= testHammerStrike();
        allPassed &= testHammerEscape();
        allPassed &= testHammerForce();

        if (allPassed)
            std::cout << "All Hammer unit tests PASSED" << std::endl;
        else
            std::cerr << "Some Hammer unit tests FAILED" << std::endl;

        return allPassed;
    }

private:
    bool testHammerCreation()
    {
        std::cout << "  Testing Hammer creation..." << std::endl;

        Hammer hammer;

        // Set up hammer parameters
        float Fs = 44100.0f;
        float m = 0.005f;   // mass
        float K = 1e9f;     // stiffness
        float p = 2.5f;     // exponent
        float Z = 1.0f;     // impedance
        float alpha = 0.1f;
        int escapeDelay = 10;

        hammer.set(Fs, m, K, p, Z, alpha, escapeDelay);

        std::cout << "    Hammer creation: PASSED" << std::endl;
        return true;
    }

    bool testHammerStrike()
    {
        std::cout << "  Testing Hammer strike..." << std::endl;

        Hammer hammer;

        float Fs = 44100.0f;
        float m = 0.005f;
        float K = 1e9f;
        float p = 2.5f;
        float Z = 1.0f;
        float alpha = 0.1f;
        int escapeDelay = 10;

        hammer.set(Fs, m, K, p, Z, alpha, escapeDelay);

        // Strike with initial velocity
        float velocity = 2.0f;
        hammer.strike(velocity);

        // After strike, hammer should not be escaped yet
        if (hammer.isEscaped())
        {
            std::cerr << "    FAILED: Hammer escaped immediately after strike" << std::endl;
            return false;
        }

        // Position should start at 0
        if (std::abs(hammer.getX()) > 0.0001f)
        {
            std::cerr << "    FAILED: Hammer position not 0 after strike" << std::endl;
            return false;
        }

        std::cout << "    Hammer strike: PASSED" << std::endl;
        return true;
    }

    bool testHammerEscape()
    {
        std::cout << "  Testing Hammer escape..." << std::endl;

        Hammer hammer;

        float Fs = 44100.0f;
        float m = 0.005f;
        float K = 1e9f;
        float p = 2.5f;
        float Z = 1.0f;
        float alpha = 0.1f;
        int escapeDelay = 10;

        hammer.set(Fs, m, K, p, Z, alpha, escapeDelay);
        hammer.strike(2.0f);

        // Run hammer simulation for a while
        bool escaped = false;
        for (int i = 0; i < 10000; i++)
        {
            hammer.load(0.0f, 0.0f);
            if (hammer.isEscaped())
            {
                escaped = true;
                break;
            }
        }

        // Hammer should eventually escape
        if (!escaped)
        {
            std::cerr << "    FAILED: Hammer never escaped" << std::endl;
            return false;
        }

        std::cout << "    Hammer escape: PASSED" << std::endl;
        return true;
    }

    bool testHammerForce()
    {
        std::cout << "  Testing Hammer force generation..." << std::endl;

        Hammer hammer;

        float Fs = 44100.0f;
        float m = 0.005f;
        float K = 1e9f;
        float p = 2.5f;
        float Z = 1.0f;
        float alpha = 0.1f;
        int escapeDelay = 100;

        hammer.set(Fs, m, K, p, Z, alpha, escapeDelay);
        hammer.strike(2.0f);

        // Simulate with string feedback and check force is generated
        float maxForce = 0.0f;
        for (int i = 0; i < 100; i++)
        {
            float force = hammer.load(0.0f, 0.0f);

            if (! std::isfinite(force))
            {
                std::cerr << "    FAILED: Hammer force is NaN or infinite" << std::endl;
                return false;
            }

            maxForce = std::max(maxForce, force);
        }

        // Hammer should generate some force when in contact with string
        if (maxForce < 0.001f)
        {
            std::cerr << "    FAILED: Hammer not generating force (maxForce=" << maxForce << ")" << std::endl;
            return false;
        }

        std::cout << "    Hammer force generation: PASSED" << std::endl;
        return true;
    }
};

class MSD2FilterUnitTest
{
public:
    bool run()
    {
        std::cout << "\nMSD2Filter Unit Tests" << std::endl;
        std::cout << "=====================" << std::endl;

        bool allPassed = true;

        allPassed &= testMSD2Creation();
        allPassed &= testMSD2Filtering();
        allPassed &= testMSD2StaysPassive();

        if (allPassed)
            std::cout << "All MSD2Filter unit tests PASSED" << std::endl;
        else
            std::cerr << "Some MSD2Filter unit tests FAILED" << std::endl;

        return allPassed;
    }

private:
    bool testMSD2Creation()
    {
        std::cout << "  Testing MSD2Filter creation..." << std::endl;

        MSD2Filter filter;

        // Create with typical parameters
        float Fs = 44100.0f;
        float m1 = 0.01f, k1 = 1000.0f, R1 = 0.1f;
        float m2 = 0.02f, k2 = 2000.0f, R2 = 0.2f;
        float R12 = 0.05f, k12 = 500.0f;
        float Zn = 1.0f, Z = 1.0f;

        filter.create(Fs, m1, k1, R1, m2, k2, R2, R12, k12, Zn, Z);

        std::cout << "    MSD2Filter creation: PASSED" << std::endl;
        return true;
    }

    bool testMSD2Filtering()
    {
        std::cout << "  Testing MSD2Filter filtering..." << std::endl;

        MSD2Filter filter;

        float Fs = 44100.0f;
        float m1 = 0.01f, k1 = 1000.0f, R1 = 0.1f;
        float m2 = 0.02f, k2 = 2000.0f, R2 = 0.2f;
        float R12 = 0.05f, k12 = 500.0f;
        float Zn = 1.0f, Z = 1.0f;

        filter.create(Fs, m1, k1, R1, m2, k2, R2, R12, k12, Zn, Z);

        // Test filtering with 2-channel input
        float in[2] = { 1.0f, 0.5f };
        float out[2] = { 0.0f, 0.0f };

        filter.filter(in, out);

        // Outputs should be modified by the filter
        // Just verify no NaN or Inf
        if (std::isnan(out[0]) || std::isnan(out[1]) ||
            std::isinf(out[0]) || std::isinf(out[1]))
        {
            std::cerr << "    FAILED: MSD2Filter produced NaN or Inf" << std::endl;
            return false;
        }

        std::cout << "    MSD2Filter filtering: PASSED" << std::endl;
        return true;
    }

    // Middle C at the plugin defaults: Zb = 8000, ZbH = 60000, Zn = 3 Z. The
    // Vertical Horizontal Impedance range reaches the coupling where the
    // determinant is zero (about 21932) and goes far past it. The bridge must
    // stay passive: it can't return more than 2 Z / Zn of what comes in.
    bool testMSD2StaysPassive()
    {
        std::cout << "  Testing MSD2Filter coupling limit..." << std::endl;

        const float Z = 4.9f, Zn = 3.0f * Z, R1 = 8000.0f, R2 = 60000.0f;
        const float limit = 2.0f * Z / Zn;

        for (float R12 : { 400.0f, 21931.7f, -21931.7f, 161371.5f })
        {
            MSD2Filter filter;
            filter.create (44100.0f, 10.0f, 1e5f, R1, 10.0f, 1e5f, R2, R12, 0.0f, Zn, Z);

            for (auto in : { std::array<float, 2> { 1.0f, 0.0f }, { 0.0f, 1.0f },
                             { 0.7071f, 0.7071f }, { 0.7071f, -0.7071f } })
            {
                float out[2] = {};
                filter.filter (in.data(), out);

                float gain = std::sqrt (out[0] * out[0] + out[1] * out[1]);

                if (! std::isfinite (gain) || gain > limit * 1.001f)
                {
                    std::cerr << "    FAILED: R12 = " << R12 << " gives gain " << gain
                              << " (limit " << limit << ")" << std::endl;
                    return false;
                }
            }
        }

        std::cout << "    MSD2Filter coupling limit: PASSED" << std::endl;
        return true;
    }
};

class ResampleFIRUnitTest
{
public:
    bool run()
    {
        std::cout << "\nResampleFIR Unit Tests" << std::endl;
        std::cout << "======================" << std::endl;

        bool allPassed = true;

        allPassed &= testResampleFIRCreation();
        allPassed &= testResampleFIRFiltering();

        if (allPassed)
            std::cout << "All ResampleFIR unit tests PASSED" << std::endl;
        else
            std::cerr << "Some ResampleFIR unit tests FAILED" << std::endl;

        return allPassed;
    }

private:
    bool testResampleFIRCreation()
    {
        std::cout << "  Testing ResampleFIR creation..." << std::endl;

        ResampleFIR fir;

        if (fir.isCreated())
        {
            std::cerr << "    FAILED: ResampleFIR should not be created initially" << std::endl;
            return false;
        }

        fir.create(2); // 2x resampling

        if (!fir.isCreated())
        {
            std::cerr << "    FAILED: ResampleFIR should be created after create()" << std::endl;
            return false;
        }

        std::cout << "    ResampleFIR creation: PASSED" << std::endl;
        return true;
    }

    bool testResampleFIRFiltering()
    {
        std::cout << "  Testing ResampleFIR filtering..." << std::endl;

        ResampleFIR fir;
        fir.create(2);

        // Create input vector (8 samples as required by filter8)
        alignas(32) float input[8] = { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -0.5f, 0.0f, 0.5f };
        vec8 vin = simde_mm256_load_ps(input);

        vec8 output = fir.filter8(vin);

        // Extract output values
        alignas(32) float outValues[8];
        simde_mm256_store_ps(outValues, output);

        // Verify no NaN or Inf in output
        for (int i = 0; i < 8; i++)
        {
            if (std::isnan(outValues[i]) || std::isinf(outValues[i]))
            {
                std::cerr << "    FAILED: ResampleFIR produced NaN or Inf at index " << i << std::endl;
                return false;
            }
        }

        std::cout << "    ResampleFIR filtering: PASSED" << std::endl;
        return true;
    }
};

class DWGSUnitTest
{
public:
    bool run()
    {
        std::cout << "\nDWGS (Digital Waveguide String) Unit Tests" << std::endl;
        std::cout << "===========================================" << std::endl;

        bool allPassed = true;

        allPassed &= testDWGSCreation();
        allPassed &= testDWGSSetup();
        allPassed &= testDWGSStringSimulation();
        allPassed &= testDWGSDamper();
        allPassed &= testDWGSUpsampleCalculation();

        if (allPassed)
            std::cout << "All DWGS unit tests PASSED" << std::endl;
        else
            std::cerr << "Some DWGS unit tests FAILED" << std::endl;

        return allPassed;
    }

private:
    bool testDWGSCreation()
    {
        std::cout << "  Testing DWGS creation..." << std::endl;

        dwgs string;

        // Verify initial state
        if (string.wave0 != nullptr || string.wave1 != nullptr ||
            string.wave != nullptr || string.Fl != nullptr)
        {
            std::cerr << "    FAILED: DWGS buffers should be null initially" << std::endl;
            return false;
        }

        std::cout << "    DWGS creation: PASSED" << std::endl;
        return true;
    }

    bool testDWGSSetup()
    {
        std::cout << "  Testing DWGS setup..." << std::endl;

        dwgs string;

        // Setup parameters for a very low note (A1, 55 Hz)
        // At 44100 Hz, this gives deltot = 44100/55 = 801.8 samples per period
        // With inpos = 0.12, del1 = (0.12 * 801.8) - 1 = 95.2 which is > 2
        float Fs = 44100.0f;
        int longmodes = 8;      // Higher value to keep nLongModes < 128
        int downsample = 1;
        float f = 55.0f;        // very low frequency
        float c1 = 0.5f;        // loss parameter
        float c3 = 10.0f;       // loss parameter
        float B = 0.00001f;     // very small inharmonicity for low notes
        float L = 2.0f;         // string length (longer for low notes)
        float longFreq1 = 200.0f; // Higher value to keep nLongModes reasonable
        float gammaL = 0.001f;
        float gammaL2 = 0.0001f;
        float inpos = 0.12f;    // hammer position
        float Z = 1.0f;         // impedance

        // For very low frequencies, upsample=1 should work
        int upsample = 1;

        string.set(Fs, longmodes, downsample, upsample, f, c1, c3, B, L, longFreq1, gammaL, gammaL2, inpos, Z);

        // Verify buffers are allocated
        if (string.wave0 == nullptr || string.wave1 == nullptr ||
            string.wave == nullptr || string.Fl == nullptr)
        {
            std::cerr << "    FAILED: DWGS buffers not allocated after set()" << std::endl;
            return false;
        }

        // Verify frequency is stored
        if (std::abs(string.f - f) > 0.001f)
        {
            std::cerr << "    FAILED: DWGS frequency not set correctly" << std::endl;
            return false;
        }

        // Verify delay values are reasonable
        if (string.del0 <= 0 || string.del1 <= 0 || string.del2 <= 0 || string.del3 <= 0)
        {
            std::cerr << "    FAILED: DWGS delay values not set correctly" << std::endl;
            return false;
        }

        std::cout << "    DWGS setup: PASSED" << std::endl;
        return true;
    }

    bool testDWGSStringSimulation()
    {
        std::cout << "  Testing DWGS string simulation..." << std::endl;

        dwgs string;

        // Setup for a low note (A1, 55 Hz)
        float Fs = 44100.0f;
        int longmodes = 8;
        int downsample = 1;
        float f = 55.0f;
        float c1 = 0.5f;
        float c3 = 10.0f;
        float B = 0.00001f;
        float L = 2.0f;
        float longFreq1 = 200.0f;
        float gammaL = 0.001f;
        float gammaL2 = 0.0001f;
        float inpos = 0.12f;
        float Z = 1.0f;

        int upsample = 1;

        string.set(Fs, longmodes, downsample, upsample, f, c1, c3, B, L, longFreq1, gammaL, gammaL2, inpos, Z);

        // Initialize string state
        string.init_string1();

        // Run string simulation for several samples
        float maxOutput = 0.0f;
        bool hasNaN = false;

        for (int i = 0; i < 1000; i++)
        {
            float output = string.go_string();
            float sbOutput = string.go_soundboard(0.1f, 0.0f);

            if (std::isnan(output) || std::isnan(sbOutput) ||
                std::isinf(output) || std::isinf(sbOutput))
            {
                hasNaN = true;
                break;
            }

            maxOutput = std::max(maxOutput, std::abs(output));
            maxOutput = std::max(maxOutput, std::abs(sbOutput));
        }

        if (hasNaN)
        {
            std::cerr << "    FAILED: DWGS simulation produced NaN or Inf" << std::endl;
            return false;
        }

        std::cout << "    DWGS string simulation: PASSED" << std::endl;
        return true;
    }

    bool testDWGSDamper()
    {
        std::cout << "  Testing DWGS damper..." << std::endl;

        dwgs string;

        // Setup with a very low note
        float Fs = 44100.0f;
        int longmodes = 8;
        int downsample = 1;
        float f = 55.0f;
        float c1 = 0.5f;
        float c3 = 10.0f;
        float B = 0.00001f;
        float L = 2.0f;
        float longFreq1 = 200.0f;
        float gammaL = 0.001f;
        float gammaL2 = 0.0001f;
        float inpos = 0.12f;
        float Z = 1.0f;

        int upsample = 1;

        string.set(Fs, longmodes, downsample, upsample, f, c1, c3, B, L, longFreq1, gammaL, gammaL2, inpos, Z);

        // Apply damper with increased damping
        float newC1 = 2.0f;
        float newC3 = 50.0f;
        int nDamper = 64;

        string.damper(newC1, newC3, gammaL, gammaL2, nDamper);

        // Verify damper state is set
        if (string.nDamper != nDamper)
        {
            std::cerr << "    FAILED: DWGS damper count not set correctly" << std::endl;
            return false;
        }

        std::cout << "    DWGS damper: PASSED" << std::endl;
        return true;
    }

    bool testDWGSUpsampleCalculation()
    {
        std::cout << "  Testing DWGS upsample calculation..." << std::endl;

        dwgs string;

        // Test getMinUpsample for various frequencies
        float Fs = 44100.0f;
        int downsample = 1;
        float inpos = 0.12f;
        float B = 0.0001f;

        // Low frequency should need less upsampling
        int upsampleLow = string.getMinUpsample(downsample, Fs, 100.0f, inpos, B);

        // High frequency might need more upsampling
        int upsampleHigh = string.getMinUpsample(downsample, Fs, 2000.0f, inpos, B);

        // Upsample values should be powers of 2 and >= 1
        if (upsampleLow < 1 || upsampleHigh < 1)
        {
            std::cerr << "    FAILED: DWGS upsample values should be >= 1" << std::endl;
            return false;
        }

        // Check they're powers of 2
        auto isPowerOf2 = [](int n) { return n > 0 && (n & (n - 1)) == 0; };

        if (!isPowerOf2(upsampleLow) || !isPowerOf2(upsampleHigh))
        {
            std::cerr << "    FAILED: DWGS upsample values should be powers of 2" << std::endl;
            return false;
        }

        std::cout << "    DWGS upsample calculation: PASSED" << std::endl;
        return true;
    }
};

//==============================================================================
// Edge case tests: settings and sample rates that used to overflow or abort()
//==============================================================================

class PianoEdgeCaseTest
{
public:
    bool run()
    {
        std::cout << "\nPiano Edge Case Tests" << std::endl;
        std::cout << "=====================" << std::endl;

        bool allPassed = true;

        allPassed &= testUpsampleIsCapped();
        allPassed &= testExtremeSettings();

        if (allPassed)
            std::cout << "All edge case tests PASSED" << std::endl;
        else
            std::cerr << "Some edge case tests FAILED" << std::endl;

        return allPassed;
    }

private:
    // Same defaults as PianoAudioProcessor's parameters
    static constexpr float pluginDefaults[NumParams] = {
        0.5f, 0.5f, 0.5f, 0.5f, 0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f, 0.0f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        0.5f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f,
        0.5f, 1.0f, 0.0f, 0.0f
    };

    bool testUpsampleIsCapped()
    {
        std::cout << "  Testing upsample cap..." << std::endl;

        // Top note with the hammer right at the string end: without the cap this
        // asked for 16x and overran the dispersion filter's coefficient buffers
        for (float Fs : { 22050.0f, 32000.0f, 44100.0f })
        {
            for (int downsample : { 1, 2 })
            {
                dwgs string;
                int upsample = string.getMinUpsample (downsample, Fs, 4400.0f, 0.04f, 1e-3f);

                if (upsample > MaxFilterUpsample)
                {
                    std::cerr << "    FAILED: upsample " << upsample << " > " << MaxFilterUpsample
                              << " (Fs " << Fs << ", downsample " << downsample << ")" << std::endl;
                    return false;
                }
            }
        }

        std::cout << "    Upsample cap: PASSED" << std::endl;
        return true;
    }

    bool testExtremeSettings()
    {
        std::cout << "  Testing extreme settings across sample rates..." << std::endl;

        struct Config
        {
            const char* name;
            std::vector<float> values;
        };

        std::vector<float> defaults (pluginDefaults, pluginDefaults + NumParams);

        auto edge = defaults;
        edge[pDwgs4] = 1.0f;
        edge[pDownsample] = 1.0f;
        edge[pHammerPosition] = 0.0f;

        std::vector<Config> configs = {
            { "plugin defaults",                      defaults },
            { "downsample + hammer at string end",    edge },
            { "all minimum",                          std::vector<float> (NumParams, 0.0f) },
            { "all maximum",                          std::vector<float> (NumParams, 1.0f) },
        };

        bool allPassed = true;

        for (float Fs : { 22050.0f, 44100.0f, 96000.0f, 192000.0f })
        {
            for (auto& config : configs)
            {
                g_currentTestPhase = config.name;

                int nonFinite = render (Fs, config.values);

                if (nonFinite > 0)
                {
                    std::cerr << "    FAILED: " << config.name << " @ " << Fs << " Hz produced "
                              << nonFinite << " non-finite samples" << std::endl;
                    allPassed = false;
                }
            }
        }

        if (allPassed)
            std::cout << "    Extreme settings: PASSED" << std::endl;

        return allPassed;
    }

    // Plays the lowest, middle and highest notes and returns the number of NaN/inf samples
    static int render (float Fs, const std::vector<float>& values)
    {
        constexpr int blockSize = 32;

        Piano piano;
        piano.init (Fs, blockSize);

        for (int i = 0; i < NumParams; i++)
            piano.setParameter (i, values[size_t (i)]);

        std::vector<float> left (blockSize), right (blockSize);
        float* channels[2] = { left.data(), right.data() };

        int numBlocks = int (0.25f * Fs) / blockSize;
        int nonFinite = 0;

        for (int b = 0; b < numBlocks; b++)
        {
            juce::MidiBuffer midi;

            for (int note : { PIANO_MIN_NOTE, 60, PIANO_MAX_NOTE })
            {
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                else if (b == numBlocks / 2)
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            }

            piano.process (channels, blockSize, midi);

            for (auto s : left)
                if (! std::isfinite (s))
                    nonFinite++;
        }

        return nonFinite;
    }
};

//==============================================================================
// MIDI handling tests
//==============================================================================

class PianoMidiTest
{
public:
    bool run()
    {
        std::cout << "\nPiano MIDI Tests" << std::endl;
        std::cout << "================" << std::endl;

        bool allPassed = true;

        allPassed &= testAllOffDampsNotes ("All Notes Off", juce::MidiMessage::allNotesOff (1));
        allPassed &= testAllOffDampsNotes ("All Sound Off", juce::MidiMessage::allSoundOff (1));

        if (allPassed)
            std::cout << "All MIDI tests PASSED" << std::endl;
        else
            std::cerr << "Some MIDI tests FAILED" << std::endl;

        return allPassed;
    }

private:
    // A held note (no note off) must be damped by the controller message.
    // These used to be ignored, so notes held by the sustain pedal rang on
    // after a panic: SustainPedal drops its pending note offs on these messages.
    bool testAllOffDampsNotes (const char* name, const juce::MidiMessage& allOff)
    {
        std::cout << "  Testing " << name << "..." << std::endl;

        float ringing = renderTailRms (nullptr);
        float damped  = renderTailRms (&allOff);

        std::cout << "    Tail RMS ringing: " << ringing << ", after " << name << ": " << damped << std::endl;

        // Written so that NaN fails: every comparison with NaN is false
        if (! (ringing > 0.0f && damped <= 0.1f * ringing))
        {
            std::cerr << "    FAILED: " << name << " didn't damp the held note" << std::endl;
            return false;
        }

        std::cout << "    " << name << ": PASSED" << std::endl;
        return true;
    }

    // Strikes and holds a chord, optionally sends allOff after 0.25 s, and
    // returns the RMS of 0.75 s - 1.25 s
    static float renderTailRms (const juce::MidiMessage* allOff)
    {
        Piano piano;
        piano.init (SAMPLE_RATE, BLOCK_SIZE);

        for (int i = 0; i < NumParams; i++)
            piano.setParameter (i, 0.5f);

        std::vector<float> left (BLOCK_SIZE), right (BLOCK_SIZE);
        float* channels[2] = { left.data(), right.data() };

        int numBlocks   = int (1.25f * SAMPLE_RATE) / BLOCK_SIZE;
        int offBlock    = int (0.25f * SAMPLE_RATE) / BLOCK_SIZE;
        int tailStart   = int (0.75f * SAMPLE_RATE) / BLOCK_SIZE;

        double sum = 0.0;
        int count = 0;

        for (int b = 0; b < numBlocks; b++)
        {
            juce::MidiBuffer midi;

            if (b == 0)
                for (int note : { 48, 60, 64, 67 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            if (allOff != nullptr && b == offBlock)
                midi.addEvent (*allOff, 0);

            piano.process (channels, BLOCK_SIZE, midi);

            if (b >= tailStart)
            {
                for (auto s : left)
                    sum += double (s) * s;

                count += BLOCK_SIZE;
            }
        }

        return float (std::sqrt (sum / std::max (count, 1)));
    }
};

class PianoParameterTest
{
public:
    bool run()
    {
        std::cout << "\nPiano Parameter Tests" << std::endl;
        std::cout << "=====================" << std::endl;

        bool allPassed = true;

        allPassed &= testChangesSound ("Soundboard Size", pSoundboardSize, 0.0f, 1.0f);
        allPassed &= testChangesSound ("Long Modes", pLongModes, 0.0f, 1.0f);
        allPassed &= testPrepareNotesKeepsSound();
        allPassed &= testSimdMatchesScalar ("default mixes", {});
        allPassed &= testSimdMatchesScalar ("Longitudinal Transverse Mix off", { { pLongitudinalTransverseMix, 0.0f } });
        allPassed &= testSimdMatchesScalar ("Longitudinal Mix off", { { pLongitudinalMix, 0.0f } });
        allPassed &= testSimdMatchesScalar ("unequal mixes", { { pLongitudinalTransverseMix, 0.6f }, { pLongitudinalMix, 0.4f } });
        allPassed &= testRendersSanely ("Vertical Horizontal Impedance at the old singularity", { { pVerticalHorizontalImpedance, 0.8337f } });
        allPassed &= testRendersSanely ("Vertical Horizontal Impedance at maximum", { { pVerticalHorizontalImpedance, 1.0f } });

        if (allPassed)
            std::cout << "All parameter tests PASSED" << std::endl;
        else
            std::cerr << "Some parameter tests FAILED" << std::endl;

        return allPassed;
    }

private:
    // These parameters used to be ignored by the engine. Both settings must
    // render a sane chord, and the two renders must differ.
    bool testChangesSound (const char* name, int param, float a, float b)
    {
        std::cout << "  Testing " << name << "..." << std::endl;

        auto renderA = renderChord ({ { param, a } }, false);
        auto renderB = renderChord ({ { param, b } }, false);

        double diff = 0.0, ref = 0.0;
        float peak = 0.0f;
        bool finite = true;

        for (size_t i = 0; i < renderA.size(); i++)
        {
            finite = finite && std::isfinite (renderA[i]) && std::isfinite (renderB[i]);
            diff += double (renderA[i] - renderB[i]) * (renderA[i] - renderB[i]);
            ref  += double (renderA[i]) * renderA[i];
            peak = std::max ({ peak, std::abs (renderA[i]), std::abs (renderB[i]) });
        }

        double relDiff = std::sqrt (diff / std::max (ref, 1e-20));
        std::cout << "    Relative difference: " << relDiff << ", peak: " << peak << std::endl;

        if (! finite || peak > 4.0f || ref <= 0.0)
        {
            std::cerr << "    FAILED: " << name << " produced invalid output" << std::endl;
            return false;
        }

        if (relDiff < 0.01)
        {
            std::cerr << "    FAILED: " << name << " doesn't change the sound" << std::endl;
            return false;
        }

        std::cout << "    " << name << ": PASSED" << std::endl;
        return true;
    }

    // prepareNotes() runs every note's setup ahead of time. It must not leave
    // anything behind that changes what the notes sound like.
    bool testPrepareNotesKeepsSound()
    {
        std::cout << "  Testing prepareNotes()..." << std::endl;

        auto plain    = renderChord ({ { pVolume, 0.7f } }, false);
        auto prepared = renderChord ({ { pVolume, 0.7f } }, true);

        float maxDiff = 0.0f;
        bool finite = true;

        for (size_t i = 0; i < plain.size(); i++)
        {
            finite = finite && std::isfinite (plain[i]) && std::isfinite (prepared[i]);
            maxDiff = std::max (maxDiff, std::abs (plain[i] - prepared[i]));
        }

        std::cout << "    Max difference: " << maxDiff << std::endl;

        if (! finite || maxDiff > 1.0e-6f)
        {
            std::cerr << "    FAILED: prepareNotes() changed the output" << std::endl;
            return false;
        }

        std::cout << "    prepareNotes(): PASSED" << std::endl;
        return true;
    }

    // The SIMD path (Dwgs4) takes over from the scalar one once the hammer has
    // left the string. It used to apply Longitudinal Mix to both longitudinal
    // forces, ignoring Longitudinal Transverse Mix. The two paths aren't bit
    // identical (about 0.6% apart at the defaults), but the mix controls must
    // act the same way in both. With the old bug unequal mixes were 4-20% apart.
    bool testSimdMatchesScalar (const char* name, std::vector<std::pair<int, float>> changes)
    {
        std::cout << "  Testing SIMD against scalar, " << name << "..." << std::endl;

        auto scalarChanges = changes;
        scalarChanges.push_back ({ pDwgs4, 0.0f });
        changes.push_back ({ pDwgs4, 1.0f });

        auto scalar = renderChord (scalarChanges, false);
        auto simd   = renderChord (changes, false);

        double diff = 0.0, ref = 0.0;
        bool finite = true;

        for (size_t i = 0; i < scalar.size(); i++)
        {
            finite = finite && std::isfinite (scalar[i]) && std::isfinite (simd[i]);
            diff += double (scalar[i] - simd[i]) * (scalar[i] - simd[i]);
            ref  += double (scalar[i]) * scalar[i];
        }

        double relDiff = std::sqrt (diff / std::max (ref, 1e-20));
        std::cout << "    Relative difference: " << relDiff << std::endl;

        if (! finite || ref <= 0.0 || relDiff > 0.02)
        {
            std::cerr << "    FAILED: SIMD and scalar disagree with " << name << std::endl;
            return false;
        }

        std::cout << "    SIMD against scalar, " << name << ": PASSED" << std::endl;
        return true;
    }

    // Settings in the parameter range must render finite, bounded audio
    bool testRendersSanely (const char* name, std::vector<std::pair<int, float>> changes)
    {
        std::cout << "  Testing " << name << "..." << std::endl;

        for (float dwgs4 : { 0.0f, 1.0f })
        {
            auto c = changes;
            c.push_back ({ pDwgs4, dwgs4 });
            auto render = renderChord (c, false);

            float peak = 0.0f;
            double sum = 0.0;
            bool finite = true;

            for (auto x : render)
            {
                finite = finite && std::isfinite (x);
                peak = std::max (peak, std::abs (x));
                sum += double (x) * x;
            }

            std::cout << "    Dwgs4 " << dwgs4 << " peak: " << peak << std::endl;

            if (! finite || peak > 4.0f || sum <= 0.0)
            {
                std::cerr << "    FAILED: " << name << " produced invalid output" << std::endl;
                return false;
            }
        }

        std::cout << "    " << name << ": PASSED" << std::endl;
        return true;
    }

    // Renders 1 s of a held chord with some parameters changed from 0.5
    static std::vector<float> renderChord (const std::vector<std::pair<int, float>>& changes, bool prepare)
    {
        Piano piano;
        piano.init (SAMPLE_RATE, BLOCK_SIZE);

        for (int i = 0; i < NumParams; i++)
            piano.setParameter (i, 0.5f);

        for (auto [param, value] : changes)
            piano.setParameter (param, value);

        if (prepare)
            piano.prepareNotes();

        std::vector<float> left (BLOCK_SIZE), right (BLOCK_SIZE), out;
        float* channels[2] = { left.data(), right.data() };

        int numBlocks = int (SAMPLE_RATE) / BLOCK_SIZE;

        for (int b = 0; b < numBlocks; b++)
        {
            juce::MidiBuffer midi;

            if (b == 0)
                for (int note : { 36, 48, 60, 64, 67, 76 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);

            piano.process (channels, BLOCK_SIZE, midi);
            out.insert (out.end(), left.begin(), left.end());
        }

        return out;
    }
};

//==============================================================================
// Processor tests: the host-facing code (PluginProcessor and SustainPedal),
// driven through processBlock() as a host would
//==============================================================================

class PianoProcessorTest
{
public:
    bool run()
    {
        std::cout << "\nPiano Processor Tests" << std::endl;
        std::cout << "=====================" << std::endl;

        bool allPassed = true;

        allPassed &= testParameterMapping();
        allPassed &= testLatencyAndBlockSizes();
        allPassed &= testSustainPedal();

        if (allPassed)
            std::cout << "All processor tests PASSED" << std::endl;
        else
            std::cerr << "Some processor tests FAILED" << std::endl;

        return allPassed;
    }

private:
    struct Event
    {
        int sample;
        juce::MidiMessage message;
    };

    static std::unique_ptr<PianoAudioProcessor> createProcessor (int maxBlockSize)
    {
        PianoAudioProcessor::launchCrashReporter = false;

        auto proc = std::make_unique<PianoAudioProcessor>();
        proc->setRateAndBufferSizeDetails (SAMPLE_RATE, maxBlockSize);
        proc->prepareToPlay (SAMPLE_RATE, maxBlockSize);
        return proc;
    }

    // Renders the left channel through processBlock(), cycling through blockSizes
    static std::vector<float> renderProcessor (PianoAudioProcessor& proc, const std::vector<Event>& events,
                                               const std::vector<int>& blockSizes, int numSamples)
    {
        std::vector<float> out;
        out.reserve (size_t (numSamples));

        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        int pos = 0;
        for (size_t b = 0; pos < numSamples; b++)
        {
            int n = std::min (blockSizes[b % blockSizes.size()], numSamples - pos);
            buffer.setSize (2, n, false, false, true);

            midi.clear();
            for (auto& e : events)
                if (e.sample >= pos && e.sample < pos + n)
                    midi.addEvent (e.message, e.sample - pos);

            proc.processBlock (buffer, midi);

            auto data = buffer.getReadPointer (0);
            out.insert (out.end(), data, data + n);
            pos += n;
        }

        return out;
    }

    static float rms (const std::vector<float>& x, float startSeconds, float endSeconds)
    {
        auto start = size_t (startSeconds * SAMPLE_RATE);
        auto end   = std::min (x.size(), size_t (endSeconds * SAMPLE_RATE));

        double sum = 0.0;
        for (auto i = start; i < end; i++)
            sum += double (x[i]) * x[i];

        return float (std::sqrt (sum / double (std::max (end - start, size_t (1)))));
    }

    // Hosts address parameters by ID and position, and the engine by its enum.
    // Hammer Position and the impedances used to drive the wrong engine slots.
    bool testParameterMapping()
    {
        std::cout << "  Testing host parameter mapping..." << std::endl;

        // Host order: changing it, or an ID, breaks saved sessions and automation
        static const std::pair<const char*, int> hostParams[] = {
            { "YoungsModulus", pYoungsModulus },
            { "StringDensity", pStringDensity },
            { "HammerMass", pHammerMass },
            { "StringTension", pStringTension },
            { "StringLength", pStringLength },
            { "StringRadius", pStringRadius },
            { "HammerCompliance", pHammerCompliance },
            { "HammerSpringConstant", pHammerSpringConstant },
            { "HammerHysteresis", pHammerHysteresis },
            { "HammerPosition", pHammerPosition },
            { "BridgeImpedance", pBridgeImpedance },
            { "BridgeHorizontalImpedance", pBridgeHorizontalImpedance },
            { "VerticalHorizontalImpedance", pVerticalHorizontalImpedance },
            { "SoundboardSize", pSoundboardSize },
            { "StringDecay", pStringDecay },
            { "StringLopass", pStringLopass },
            { "DampedStringDecay", pDampedStringDecay },
            { "DampedStringLopass", pDampedStringLopass },
            { "SoundboardDecay", pSoundboardDecay },
            { "SoundboardLopass", pSoundboardLopass },
            { "LongitudinalGamma", pLongitudinalGamma },
            { "LongitudinalGammaQuadratic", pLongitudinalGammaQuadratic },
            { "LongitudinalGammaDamped", pLongitudinalGammaDamped },
            { "LongitudinalGammaQuadDamped", pLongitudinalGammaQuadraticDamped },
            { "LongitudinalMix", pLongitudinalMix },
            { "LongitudinalTransverseMix", pLongitudinalTransverseMix },
            { "Volume", pVolume },
            { "MaxVelocity", pMaxVelocity },
            { "StringDetuning", pStringDetuning },
            { "BridgeMass", pBridgeMass },
            { "BridgeSpring", pBridgeSpring },
            { "Dwgs4", pDwgs4 },
            { "Downsample", pDownsample },
            { "LongModes", pLongModes },
        };

        constexpr int blockSize = 64;
        auto proc = createProcessor (blockSize);
        auto params = proc->getPluginParameters();

        if (params.size() != NumParams || int (std::size (hostParams)) != NumParams)
        {
            std::cerr << "    FAILED: " << params.size() << " host parameters, expected " << NumParams << std::endl;
            return false;
        }

        std::vector<float> defaults;
        for (int i = 0; i < NumParams; i++)
            defaults.push_back (proc->params[i]->getValue());

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int h = 0; h < NumParams; h++)
        {
            auto [uid, index] = hostParams[h];

            if (params[h]->getUid() != uid)
            {
                std::cerr << "    FAILED: host parameter " << h << " is " << params[h]->getUid()
                          << ", expected " << uid << std::endl;
                return false;
            }

            // Move just this one well away from its default
            float value = defaults[size_t (index)] < 0.5f ? 1.0f : 0.0f;
            params[h]->setValue (value);
            proc->processBlock (buffer, midi);

            for (int i = 0; i < NumParams; i++)
            {
                float expected = (i == index) ? value : defaults[size_t (i)];

                if (proc->piano->getParameter (i) != expected)
                {
                    std::cerr << "    FAILED: setting " << uid << " left engine parameter " << i << " at "
                              << proc->piano->getParameter (i) << ", expected " << expected << std::endl;
                    return false;
                }
            }

            params[h]->setValue (defaults[size_t (index)]);
        }

        proc->processBlock (buffer, midi);

        std::cout << "    Host parameter mapping: PASSED" << std::endl;
        return true;
    }

    // The engine renders 32-sample blocks behind a FIFO, which the processor
    // reports as latency. Whatever the host's block sizes, the output must be
    // the engine's own output delayed by exactly that latency.
    bool testLatencyAndBlockSizes()
    {
        std::cout << "  Testing latency and host block sizes..." << std::endl;

        constexpr int latency = PianoAudioProcessor::interalBlockSize;
        constexpr int numSamples = latency * 700;

        const std::vector<Event> events = {
            { 5,     juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) },
            { 1000,  juce::MidiMessage::noteOn (1, 64, (juce::uint8) 80) },
            { 5003,  juce::MidiMessage::noteOn (1, 67, (juce::uint8) 110) },
            { 9000,  juce::MidiMessage::noteOff (1, 60) },
            { 16001, juce::MidiMessage::allNotesOff (1) },
        };

        // The engine on its own, with the processor's default parameters and
        // the same 32-sample blocks
        std::vector<float> engine;
        {
            auto defaults = createProcessor (latency);

            Piano piano;
            piano.init (SAMPLE_RATE, latency);

            for (int i = 0; i < NumParams; i++)
                piano.setParameter (i, defaults->params[i]->getValue());

            piano.prepareNotes();

            std::vector<float> left (latency), right (latency);
            float* channels[2] = { left.data(), right.data() };

            for (int pos = 0; pos < numSamples; pos += latency)
            {
                juce::MidiBuffer midi;
                for (auto& e : events)
                    if (e.sample >= pos && e.sample < pos + latency)
                        midi.addEvent (e.message, e.sample - pos);

                piano.process (channels, latency, midi);
                engine.insert (engine.end(), left.begin(), left.end());
            }
        }

        if (! (rms (engine, 0.0f, 0.5f) > 1.0e-4f))
        {
            std::cerr << "    FAILED: the engine render is silent or not finite" << std::endl;
            return false;
        }

        const std::vector<std::vector<int>> partitions = {
            { 32 }, { 512 }, { 1 }, { 7 }, { 100, 3, 64, 17 },
        };

        for (auto& blockSizes : partitions)
        {
            juce::String name;
            for (auto n : blockSizes)
                name << (name.isEmpty() ? "" : ", ") << n;

            auto proc = createProcessor (*std::max_element (blockSizes.begin(), blockSizes.end()));

            if (proc->getLatencySamples() != latency)
            {
                std::cerr << "    FAILED: reported latency is " << proc->getLatencySamples()
                          << ", expected " << latency << std::endl;
                return false;
            }

            auto out = renderProcessor (*proc, events, blockSizes, numSamples);

            float maxDiff = 0.0f;
            bool finite = true;

            for (size_t i = 0; i < out.size(); i++)
            {
                float expected = i < size_t (latency) ? 0.0f : engine[i - size_t (latency)];
                finite = finite && std::isfinite (out[i]);
                maxDiff = std::max (maxDiff, std::abs (out[i] - expected));
            }

            std::cout << "    Blocks of " << name << ": max difference " << maxDiff << std::endl;

            if (! finite || maxDiff != 0.0f)
            {
                std::cerr << "    FAILED: blocks of " << name << " don't give the engine's output "
                          << latency << " samples late" << std::endl;
                return false;
            }
        }

        std::cout << "    Latency and host block sizes: PASSED" << std::endl;
        return true;
    }

    // CC64 holds note offs back until the pedal comes up. A key pressed again
    // while the pedal is down cancels its held note off, and All Notes Off
    // drops the held ones.
    bool testSustainPedal()
    {
        std::cout << "  Testing sustain pedal..." << std::endl;

        auto at = [] (float seconds) { return int (seconds * SAMPLE_RATE); };

        auto noteOn    = juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100);
        auto noteOff   = juce::MidiMessage::noteOff (1, 60);
        auto pedalDown = juce::MidiMessage::controllerEvent (1, 64, 127);
        auto pedalUp   = juce::MidiMessage::controllerEvent (1, 64, 0);

        // RMS of 0.7 s - 1.2 s: half a second after the last thing that can damp the note
        auto tail = [&] (std::vector<Event> events)
        {
            auto proc = createProcessor (BLOCK_SIZE);
            auto out = renderProcessor (*proc, events, { BLOCK_SIZE }, at (1.2f));
            return rms (out, 0.7f, 1.2f);
        };

        float released = tail ({ { 0, noteOn }, { at (0.1f), noteOff } });
        float held     = tail ({ { 0, noteOn }, { 100, pedalDown }, { at (0.1f), noteOff } });
        float pedalOff = tail ({ { 0, noteOn }, { 100, pedalDown }, { at (0.1f), noteOff }, { at (0.2f), pedalUp } });
        float rekeyed  = tail ({ { 0, noteOn }, { 100, pedalDown }, { at (0.1f), noteOff },
                                 { at (0.15f), noteOn }, { at (0.2f), pedalUp } });
        float allOff   = tail ({ { 0, noteOn }, { 100, pedalDown }, { at (0.1f), noteOff },
                                 { at (0.2f), juce::MidiMessage::allNotesOff (1) } });

        std::cout << "    Tail RMS: released " << released << ", held by pedal " << held
                  << ", pedal up " << pedalOff << ", key pressed again " << rekeyed
                  << ", All Notes Off " << allOff << std::endl;

        // Written so that NaN fails: every comparison with NaN is false
        struct Check { const char* what; bool ok; };

        const Check checks[] = {
            { "the pedal didn't hold the note",                     held > 10.0f * released },
            { "pedal up didn't release the note",                   pedalOff <= 0.1f * held },
            { "pressing the key again didn't cancel its note off",  rekeyed > 10.0f * pedalOff },
            { "All Notes Off didn't drop the held note off",        allOff <= 0.1f * held },
        };

        for (auto& c : checks)
        {
            if (! c.ok)
            {
                std::cerr << "    FAILED: " << c.what << std::endl;
                return false;
            }
        }

        std::cout << "    Sustain pedal: PASSED" << std::endl;
        return true;
    }
};

// Benchmark configuration
constexpr float BENCHMARK_SAMPLE_RATE = 44100.0f;
constexpr int BENCHMARK_BLOCK_SIZE = 512;
constexpr float BENCHMARK_DURATION_SECONDS = 30.0f;  // 30 seconds of audio
constexpr int BENCHMARK_ITERATIONS = 3;  // Run multiple times for consistency

struct BenchmarkResult {
    std::string name;
    double audioSeconds;
    double clockSeconds;
    double realtimeRatio;  // audioSeconds / clockSeconds (higher = faster)
};

BenchmarkResult runSingleBenchmark(const std::string& name, int numVoices)
{
    Piano piano;
    piano.init(BENCHMARK_SAMPLE_RATE, BENCHMARK_BLOCK_SIZE);

    // Set default parameters
    for (int i = 0; i < NumParams; i++)
        piano.setParameter(i, 0.5f);
    piano.setParameter(pVolume, 0.7f);

    int totalSamples = static_cast<int>(BENCHMARK_DURATION_SECONDS * BENCHMARK_SAMPLE_RATE);
    int numBlocks = (totalSamples + BENCHMARK_BLOCK_SIZE - 1) / BENCHMARK_BLOCK_SIZE;

    // Allocate output buffer (stereo)
    juce::AudioBuffer<float> outputBuffer(2, BENCHMARK_BLOCK_SIZE);
    float* channels[2] = { outputBuffer.getWritePointer(0), outputBuffer.getWritePointer(1) };

    // Trigger notes one at a time with blocks in between (more realistic)
    juce::MidiBuffer midiBuffer;
    for (int v = 0; v < numVoices; v++)
    {
        midiBuffer.clear();
        midiBuffer.addEvent(juce::MidiMessage::noteOn(1, 48 + v * 4, 0.8f), 0);
        piano.process(channels, BENCHMARK_BLOCK_SIZE, midiBuffer);
    }

    // Clear MIDI for benchmark blocks
    midiBuffer.clear();

    // Benchmark
    auto startTime = std::chrono::high_resolution_clock::now();

    for (int block = 0; block < numBlocks; block++)
    {
        piano.process(channels, BENCHMARK_BLOCK_SIZE, midiBuffer);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double clockSeconds = std::chrono::duration<double>(endTime - startTime).count();
    double audioSeconds = BENCHMARK_DURATION_SECONDS;

    return { name, audioSeconds, clockSeconds, audioSeconds / clockSeconds };
}

int runBenchmarks(const std::string& outputPath)
{
    std::cout << "========================================" << std::endl;
    std::cout << "Piano Performance Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Sample rate: " << BENCHMARK_SAMPLE_RATE << " Hz" << std::endl;
    std::cout << "Block size: " << BENCHMARK_BLOCK_SIZE << " samples" << std::endl;
    std::cout << "Duration: " << BENCHMARK_DURATION_SECONDS << " seconds" << std::endl;
    std::cout << "Iterations: " << BENCHMARK_ITERATIONS << std::endl;
    std::cout << std::endl;

    // Initialize JUCE - needed for MidiBuffer and other JUCE types
    // On headless CI, this should still work as we're not creating windows
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::vector<BenchmarkResult> results;

    // Test configurations: different polyphony levels
    const int voiceCounts[] = { 1, 4, 8, 16 };

    for (int voices : voiceCounts)
    {
        std::string testName = std::to_string(voices) + " voice" + (voices > 1 ? "s" : "");
        std::cout << "Benchmarking " << testName << "..." << std::endl;

        double totalClockSeconds = 0;
        double totalAudioSeconds = 0;

        for (int iter = 0; iter < BENCHMARK_ITERATIONS; iter++)
        {
            auto result = runSingleBenchmark(testName, voices);
            totalClockSeconds += result.clockSeconds;
            totalAudioSeconds += result.audioSeconds;
        }

        double avgClockSeconds = totalClockSeconds / BENCHMARK_ITERATIONS;
        double avgAudioSeconds = totalAudioSeconds / BENCHMARK_ITERATIONS;
        double realtimeRatio = avgAudioSeconds / avgClockSeconds;

        results.push_back({ testName, avgAudioSeconds, avgClockSeconds, realtimeRatio });

        std::cout << "  Clock time: " << std::fixed << std::setprecision(3) << avgClockSeconds << "s" << std::endl;
        std::cout << "  Audio time: " << avgAudioSeconds << "s" << std::endl;
        std::cout << "  Realtime ratio: " << std::setprecision(2) << realtimeRatio << "x" << std::endl;
        std::cout << std::endl;
    }

    // Print summary
    std::cout << "========================================" << std::endl;
    std::cout << "BENCHMARK SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::left << std::setw(12) << "Test"
              << std::right << std::setw(12) << "Clock(s)"
              << std::setw(12) << "Audio(s)"
              << std::setw(12) << "Ratio" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    for (const auto& r : results)
    {
        std::cout << std::left << std::setw(12) << r.name
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(12) << r.clockSeconds
                  << std::setw(12) << r.audioSeconds
                  << std::setprecision(2) << std::setw(11) << r.realtimeRatio << "x" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    // Output JSON for github-action-benchmark
    if (!outputPath.empty())
    {
        std::ofstream jsonFile(outputPath);
        if (jsonFile.is_open())
        {
            jsonFile << "[\n";
            for (size_t i = 0; i < results.size(); i++)
            {
                const auto& r = results[i];
                jsonFile << "  {\n";
                jsonFile << "    \"name\": \"" << r.name << "\",\n";
                jsonFile << "    \"unit\": \"x realtime\",\n";
                jsonFile << "    \"value\": " << std::fixed << std::setprecision(4) << r.realtimeRatio << "\n";
                jsonFile << "  }" << (i < results.size() - 1 ? "," : "") << "\n";
            }
            jsonFile << "]\n";
            jsonFile.close();
            std::cout << "\nResults written to: " << outputPath << std::endl;
        }
        else
        {
            std::cerr << "Failed to write results to: " << outputPath << std::endl;
            return 1;
        }
    }

    return 0;
}

static void printUsage()
{
    std::cout << "Usage: PianoTests [reference-dir] [--group <name>]... [--update-references]\n"
                 "       PianoTests --benchmark [output.json]\n"
                 "       PianoTests --list\n\n"
                 "  reference-dir         folder with the reference renders, tests/reference in the\n"
                 "                        repository (default: reference/ in the current folder)\n"
                 "  --group <name>        run only this test group, can be given more than once\n"
                 "  --update-references   write the reference renders instead of comparing with them\n"
                 "  --list                print the test group names" << std::endl;
}

int main(int argc, char* argv[])
{
    // Check for benchmark mode
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--benchmark") == 0)
        {
            std::string outputPath;
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                outputPath = argv[i + 1];
            }
            return runBenchmarks(outputPath);
        }
    }

    juce::String referenceDir;
    juce::StringArray selectedGroups;
    bool listGroups = false;

    for (int i = 1; i < argc; i++)
    {
        juce::String arg (argv[i]);

        if (arg == "--update-references")
        {
            g_updateReferences = true;
        }
        else if (arg == "--group" && i + 1 < argc)
        {
            selectedGroups.add (argv[++i]);
        }
        else if (arg == "--list")
        {
            listGroups = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
        else if (! arg.startsWith ("-") && referenceDir.isEmpty())
        {
            referenceDir = arg;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage();
            return 2;
        }
    }

    if (referenceDir.isEmpty())
        referenceDir = juce::File::getCurrentWorkingDirectory().getFullPathName() + "/reference";

    struct TestGroup
    {
        const char* name;
        std::function<bool()> run;
    };

    const std::vector<TestGroup> groups = {
        // Unit tests for internal classes
        { "FilterUnitTest",      [] { return FilterUnitTest().run(); } },
        { "HammerUnitTest",      [] { return HammerUnitTest().run(); } },
        { "MSD2FilterUnitTest",  [] { return MSD2FilterUnitTest().run(); } },
        { "ResampleFIRUnitTest", [] { return ResampleFIRUnitTest().run(); } },
        { "DWGSUnitTest",        [] { return DWGSUnitTest().run(); } },
        // Engine behaviour
        { "PianoEdgeCaseTest",   [] { return PianoEdgeCaseTest().run(); } },
        { "PianoMidiTest",       [] { return PianoMidiTest().run(); } },
        { "PianoParameterTest",  [] { return PianoParameterTest().run(); } },
        // The plugin's processor, as a host drives it
        { "PianoProcessorTest",  [] { return PianoProcessorTest().run(); } },
        // Renders compared with the references
        { "PianoScaleTest",      [&] { return PianoScaleTest (referenceDir).run(); } },
        { "PianoChordTest",      [&] { return PianoChordTest (referenceDir).run(); } },
        { "PianoStressTest",     [&] { return PianoStressTest (referenceDir).run(); } },
    };

    if (listGroups)
    {
        for (auto& g : groups)
            std::cout << g.name << std::endl;

        return 0;
    }

    for (auto& name : selectedGroups)
    {
        if (std::none_of (groups.begin(), groups.end(), [&] (const TestGroup& g) { return name == g.name; }))
        {
            std::cerr << "Unknown test group: " << name << " (--list prints them)" << std::endl;
            return 2;
        }
    }

    // Install signal handlers for crash detection
    installSignalHandlers();

    std::cout << "========================================" << std::endl;
    std::cout << "Piano Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize JUCE
    g_currentTestPhase = "JUCE initialization";
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Reference directory: " << referenceDir << std::endl;

    if (g_updateReferences)
        std::cout << "Updating the reference renders instead of comparing with them" << std::endl;

    std::cout << std::endl;

    int testsRun = 0;
    int testsPassed = 0;
    int testsFailed = 0;

    std::vector<const TestGroup*> toRun;
    for (auto& g : groups)
        if (selectedGroups.isEmpty() || selectedGroups.contains (g.name))
            toRun.push_back (&g);

    for (auto g : toRun)
    {
        g_currentTestName = g->name;
        g_currentTestPhase = "running";
        std::cout << "\n[" << (testsRun + 1) << "/" << toRun.size() << "] Running " << g->name << "..." << std::endl;

        testsRun++;

        if (g->run())
            testsPassed++;
        else
            testsFailed++;
    }

    // Summary
    g_currentTestName = "summary";
    g_currentTestPhase = "complete";

    std::cout << "\n========================================" << std::endl;
    std::cout << "TEST SUMMARY" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Total:   " << testsRun << std::endl;
    std::cout << "Passed:  " << testsPassed << std::endl;
    std::cout << "Failed:  " << testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    if (testsFailed == 0)
    {
        std::cout << "RESULT: ALL TESTS PASSED" << std::endl;
        return 0;
    }
    else
    {
        std::cerr << "RESULT: SOME TESTS FAILED" << std::endl;
        return 1;
    }
}
