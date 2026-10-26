
#ifndef PIANO_H
#define PIANO_H

#include <JuceHeader.h>
#include <memory>
#include <array>
#include <vector>

#define PIANO_MIN_NOTE 21
#define PIANO_MAX_NOTE 108

#undef FDN_REVERB
#define FDN_REVERB 1

const int NUM_NOTES = 128;    // 128 midi notes

#include "filter.h"
#include "dwgs.h"
#include "reverb.h"
#include "hammer.h"

enum {
    MaxDecimation = 16,
    MaxLongDelay = 16,
    TranBufferSize = 32,
    NumParams = 34
};

class Param
{
public:
    int tag;
    char name[64];
    char label[64];
};

class Value
{
public:
    operator float() const {
        return v;
    }
    float f = 0.0f;
    float v = 0.0f;
};

enum parameters {
    pYoungsModulus = 0,
    pStringDensity,
    pHammerMass,
    pStringTension,
    pStringLength,
    pStringRadius,
    pHammerCompliance,
    pHammerSpringConstant,
    pHammerHysteresis,
    pBridgeImpedance,
    pBridgeHorizontalImpedance,
    pVerticalHorizontalImpedance,
    pHammerPosition,
    pSoundboardSize,
    pStringDecay,
    pStringLopass,
    pDampedStringDecay,
    pDampedStringLopass,
    pSoundboardDecay,
    pSoundboardLopass,
    pLongitudinalGamma,
    pLongitudinalGammaQuadratic,
    pLongitudinalGammaDamped,
    pLongitudinalGammaQuadraticDamped,
    pLongitudinalMix,
    pLongitudinalTransverseMix,
    pVolume,
    pMaxVelocity,
    pStringDetuning,
    pBridgeMass,
    pBridgeSpring,
    pDwgs4,
    pDownsample,
    pLongModes
};

int getParameterIndex(const char *key);

class Piano;

class PianoNote
{
public:
    PianoNote (int note, int Fs, Piano *piano);
    ~PianoNote();
    float go();
    vec4 go4();
    float goUp();
    float goUpDelayed();
    float goDown();
    float goDownDelayed();
    // Returns false (and deactivates the note) if the strings can't be built
    bool triggerOn (float velocity, float* tune);
    void triggerOff();
    bool isActive();
    void deActivate();

    // Stops the note at once (Piano::reset()). Its strings still hold the old
    // sound, so they're cleared before the next strike: that keeps the cost
    // out of the reset, which can run on the audio thread.
    void cut();

    PianoNote* next = nullptr;
    PianoNote* prev = nullptr;
    int note;
    Piano* piano = nullptr;

    bool isDone();

    // isDone() compares the energy of the last second (a running sum: add the new
    // sample's power, take away the one leaving the window) with the note's peak.
    // It's double because float rounding left a residue above the 1e-8 threshold,
    // so silent notes never finished.
    double maxEnergy = 0.0;
    double energy = 0.0;
    Delay<65536> outputdelay;

    // Strings can settle with a constant offset (DC) on the bridge force. It's
    // inaudible but never decays, so it kept notes alive (and using CPU) forever.
    // Each note's output goes through a DC blocker before it's mixed or measured.
    float blockDC (float x);
    float dcR = 0.0f;
    float dcX1 = 0.0f;
    float dcY1 = 0.0f;
    static void fillFrequencyTable();
    static double freqTable[NUM_NOTES];

protected:
    // Zeroes everything the strings, filters and resamplers remember
    void clearState();
    bool needsClear = false;

    MSD2Filter bridge;

    bool bActive;
    int Fs;
    BiquadHP longHP;
    float alphasb;
    float Z2i;
    float nstringsi;
    float tranBridgeForce;
    float longBridgeForce;
    float longTranBridgeForce;
    float Z;
    float Zhv;
    float vTran;

    int downsample;
    float longForces[MaxDecimation + 1];
    float longForcesK[MaxDecimation + 1];

    alignas(32) float tranForces[TranBufferSize];
    alignas(32) float tranForcesH[TranBufferSize];
    alignas(32) float longTranForces[TranBufferSize];

    float T;
    float mu;
    float f;
    int nstrings;

    std::array<std::unique_ptr<dwgs>, 3> stringT;
    std::array<std::unique_ptr<dwgs>, 3> stringHT;
    std::unique_ptr<Hammer> hammer;

    alignas(32) float outUp[8];
    alignas(32) float outDown[8];
    int tUp;
    int tDown;
    int upSampleDelayNeeded;
    int downSampleDelayNeeded;

    bool bInit4;
    int tTran;
    int tLong;
    int tTranRead;
    int longDelay;
    int upsample;

    ResampleFIR downSampleFilter;
    ResampleFIR upSampleFilter;
};

class Piano
{
public:
    Piano();
    ~Piano();

    void addVoice (PianoNote *v);
    void removeVoice (PianoNote *v);
    void process (float *out, int samples);
    void process (float **out, int frameSamples, int offset);
    void process (float **out, int frameSamples, juce::MidiBuffer&);
    void init (float Fs, int blockSize);
    // Sizes every note's buffers for the current parameters. Call it after
    // init() and setParameter(), off the audio thread.
    void prepareNotes();
    void triggerOn (int note, float velocity, float *tune);
    void allNotesOff();
    // Silences everything now (All Sound Off, the host's reset): voices stop
    // without damping and the soundboard is cleared. Doesn't allocate.
    void reset();

    //vst crap
    void setParameter (int32_t index, float value);
    float getParameter (int32_t index);

    int USE_DWGS4 = 1;

protected:     
    friend class PianoNote;
    Value vals[NumParams];
    PianoNote* voiceList = nullptr;
    std::array<std::unique_ptr<PianoNote>, NUM_NOTES> noteArray;
    int blockSize = 0;
    float Fs = 0.0f;
    std::vector<float> input;
    bool soundboardDirty = true;

#ifdef FDN_REVERB
    std::unique_ptr<Reverb> soundboard;
#else
    std::unique_ptr<ConvolveReverb<revSize>> soundboard;
#endif
};

void qianoNote(int note, int N, float velocity, double *x, double *tune);
extern "C" void qianoNote(int note, int N, float velocity, float *x, float *tune);

#endif
