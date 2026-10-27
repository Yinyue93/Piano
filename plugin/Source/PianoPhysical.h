#pragma once

#include <memory>

// A passive modal treble and soundboard. No recorded audio or impulse response
// is used here. The older waveguides supply the struck bass below middle C;
// their unstruck counterparts participate in this model's sympathetic bank.
// See docs/physical-grand.md for the reduced model's assumptions and limits.
class PianoPhysical
{
  public:
    static constexpr int firstStruckNote = 60;
    static constexpr int firstUndampedNote = 89; // F6, instrument-specific scale
    static constexpr int latencySamples = 32;

    struct Parameters
    {
        double young = 200e9, density = 7850.0;
        double length = 1.0, radius = 1.0, mass = 1.0;
        double stiffness = 1.0, exponent = 1.0, hysteresis = 1.0;
        double position = 0.125, decay = 0.25, loss = 5.85f, detune = 1.0;
        double bridge = 8000.0, horizontal = 60000.0, coupling = 400.0;
        bool operator==(const Parameters &) const = default;
    };

    // Diagnostic configurations are set before prepare(), never during a note.
    struct Configuration
    {
        int oversampling = 2;
        bool sympathetic = true, duplex = true, movingBridge = true;
        bool nativeVectorization = true;
        bool adaptiveModes = true;
    };

    struct Frame
    {
        float left = 0, right = 0;
        float bridgeForce = 0, hammerForce = 0;
    };

    PianoPhysical();
    ~PianoPhysical();
    PianoPhysical(const PianoPhysical &) = delete;
    PianoPhysical &operator=(const PianoPhysical &) = delete;

    void prepare(double sampleRate);
    void prepare(double sampleRate, Configuration);
    void reset();
    void setParameters(const Parameters &); // Applied to a note on its next strike.
    void strike(int note, float hammerVelocity);
    void setKey(int note, bool down);
    void setPedal(float position);
    void silence(int note);
    Frame process(float bassForce = 0, float bassPosition = 0.25f);
    bool isActive(int note) const;
    int activeNotes() const;
    int activeStringModes() const; // Diagnostic cost of the sympathetic/struck bank.
    double energy() const;         // Diagnostic mechanical energy, not output loudness.
    double frequency(int note) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
