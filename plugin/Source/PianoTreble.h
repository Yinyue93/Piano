#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Per-note correction selected in treble audition 4. State belongs to the
// voice, so separate instances and simultaneous notes cannot affect each other.
class PianoTreble
{
public:
    void prepare (int note, double sampleRate)
    {
        const double x = std::clamp ((note - 60.0) / 24.0, 0.0, 1.0);
        const double weight = x * x * (3.0 - 2.0 * x);
        active = note > 60;
        floor = 0.45 + 0.40 * weight;
        filters[0].shelf (sampleRate, 600.0, -2.5 * weight);
        filters[1].shelf (sampleRate, 2500.0, -4.0 * weight);
        filters[2].bell (sampleRate, 950.0, 2.0 * weight, 2.0);
        clear();
    }
    void clear() { for (auto& f : filters) f.z1 = f.z2 = 0.0; }
    double envelopeFloor() const { return floor; }
    float process (float input)
    {
        if (! active) return input;
        double x = input;
        for (auto& f : filters) x = f.process (x);
        return float (x);
    }
private:
    struct Filter
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double b0_, double b1_, double b2_, double a0, double a1_, double a2_)
        {
            b0 = b0_ / a0; b1 = b1_ / a0; b2 = b2_ / a0;
            a1 = a1_ / a0; a2 = a2_ / a0;
        }
        void shelf (double rate, double hz, double db)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = 6.283185307179586 * std::min (hz, rate * 0.4) / rate;
            const double c = std::cos (w), beta = std::sin (w) * std::sqrt (2.0 * A);
            set (A*((A+1)+(A-1)*c+beta), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-beta),
                 (A+1)-(A-1)*c+beta, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-beta);
        }
        void bell (double rate, double hz, double db, double q)
        {
            const double A = std::pow (10.0, db / 40.0);
            const double w = 6.283185307179586 * std::min (hz, rate * 0.4) / rate;
            const double c = std::cos (w), alpha = std::sin (w) / (2.0 * q);
            set (1+alpha*A, -2*c, 1-alpha*A, 1+alpha/A, -2*c, 1-alpha/A);
        }
        double process (double x)
        {
            const double y = b0*x + z1;
            z1 = b1*x - a1*y + z2;
            z2 = b2*x - a2*y;
            return y;
        }
    };
    std::array<Filter, 3> filters;
    double floor = 0.45;
    bool active = false;
};
