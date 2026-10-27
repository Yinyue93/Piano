#pragma once

namespace PianoPhysicalKernels
{
struct Modes
{
    float *q, *v, *a;
    const float *inv, *kq, *h, *b, *hf, *bf, *omega2;
    int count;
    double *hammerResponse, *bridgeResponse, *energy;
    float hammer = 0, bridge = 0;
};

// Only called after CPU and OS vector-state support have been checked. Keeping
// this translation unit separate preserves the portable SSE2/NEON path.
void predictAvx(Modes *, int);
bool advanceAvx(Modes *, int, float step);
} // namespace PianoPhysicalKernels
