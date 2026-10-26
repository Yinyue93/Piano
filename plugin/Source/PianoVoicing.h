#pragma once
#include "reverb.h"
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

// Storage is prepared off the audio thread. The legacy IR has no native-rate
// metadata; 48 kHz is the reference used for the approved audition renders.
class PianoVoicing
{
public:
    enum Mode { classic = 0, warmBody = 1, warmDetail = 2 };
    void prepare (double sampleRate)
    {
        const double ratio = 48000.0 / sampleRate;
        std::vector<float> response (size_t (std::ceil (revSize / ratio)));
        for (size_t i = 0; i < response.size(); ++i)
        {
            const double position = double (i) * ratio;
            const auto index = std::min (size_t (position), size_t (revSize - 1));
            const double fraction = position - double (index);
            const float a = ConvolveReverb<revSize>::res[index];
            const float b = index + 1 < revSize ? ConvolveReverb<revSize>::res[index + 1] : 0.0f;
            response[i] = float ((a + fraction * (b - a)) * ratio);
        }
        if (! convolution.init (64, 1024, response.data(), response.size()))
            throw std::runtime_error ("Unable to prepare piano body response");
        filters[0].shelf (sampleRate, 600.0, 5.0);
        filters[1].shelf (sampleRate, 2500.0, 7.0);
        filters[2].bell (sampleRate, 950.0, -4.0, 2.0);
        clear();
    }
    void clear()
    {
        convolution.clearHistory();
        for (auto& filter : filters) filter.clear();
    }
    void process (const float* input, float* output, int samples, int mode)
    {
        convolution.process (input, output, size_t (samples));
        // Fixed compensation calibrated with the audition passage, not AGC.
        constexpr double bodyGain = 0.0046595;
        constexpr double detailMakeup = 1.29465;
        for (int i = 0; i < samples; ++i)
        {
            double value = output[i];
            if (mode == warmDetail)
            {
                for (auto& filter : filters) value = filter.process (value);
                value *= detailMakeup;
            }
            output[i] = float (value * bodyGain);
        }
    }
private:
    struct Filter
    {
        std::array<double, 3> b {};
        double a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
        void clear() { z1 = z2 = 0.0; }
        double process (double x)
        {
            const double y = b[0] * x + z1;
            z1 = b[1] * x - a1 * y + z2;
            z2 = b[2] * x - a2 * y;
            return y;
        }
        void set (double b0, double b1, double b2, double a0, double aa1, double aa2)
        {
            b = { b0 / a0, b1 / a0, b2 / a0 };
            a1 = aa1 / a0; a2 = aa2 / a0;
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
            const double alpha = std::sin (w) / (2.0*q), c = std::cos (w);
            set (1+alpha*A, -2*c, 1-alpha*A, 1+alpha/A, -2*c, 1-alpha/A);
        }
    };
    fftconvolver::TwoStageFFTConvolver convolution;
    std::array<Filter, 3> filters;
};
