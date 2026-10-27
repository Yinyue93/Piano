#include "PianoPhysicalKernels.h"

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#define PIANO_AVX __attribute__((target("avx2")))
#else
#define PIANO_AVX
#endif

namespace PianoPhysicalKernels
{
PIANO_AVX static double sum8(__m256 x)
{
    const auto combined = _mm_add_ps(_mm256_castps256_ps128(x), _mm256_extractf128_ps(x, 1));
    alignas(16) float values[4];
    _mm_store_ps(values, combined);
    return double(values[0]) + values[1] + values[2] + values[3];
}
PIANO_AVX void predictAvx(Modes *modes, int count)
{
    for (int i = 0; i < count; ++i)
    {
        auto &s = modes[i];
        auto h = _mm256_setzero_ps(), b = h;
        for (int j = 0; j < s.count; j += 8)
        {
            const auto v = _mm256_load_ps(s.v + j);
            const auto a = _mm256_mul_ps(
                _mm256_sub_ps(v, _mm256_mul_ps(_mm256_load_ps(s.kq + j), _mm256_load_ps(s.q + j))),
                _mm256_load_ps(s.inv + j));
            _mm256_store_ps(s.a + j, a);
            h = _mm256_add_ps(h, _mm256_mul_ps(_mm256_load_ps(s.h + j), a));
            b = _mm256_add_ps(b, _mm256_mul_ps(_mm256_load_ps(s.b + j), _mm256_sub_ps(v, a)));
        }
        *s.hammerResponse = sum8(h);
        *s.bridgeResponse = sum8(b);
    }
}
PIANO_AVX bool advanceAvx(Modes *modes, int count, float dt)
{
    const auto step = _mm256_set1_ps(dt);
    bool dropped = false;
    for (int i = 0; i < count; ++i)
    {
        auto &s = modes[i];
        auto power = _mm256_setzero_ps();
        const auto hammer = _mm256_set1_ps(s.hammer), bridge = _mm256_set1_ps(s.bridge);
        for (int j = 0; j < s.count; j += 8)
        {
            const auto average = _mm256_add_ps(
                _mm256_load_ps(s.a + j), _mm256_add_ps(_mm256_mul_ps(_mm256_load_ps(s.hf + j), hammer),
                                                       _mm256_mul_ps(_mm256_load_ps(s.bf + j), bridge)));
            const auto q = _mm256_add_ps(_mm256_load_ps(s.q + j), _mm256_mul_ps(step, average));
            const auto v = _mm256_sub_ps(_mm256_add_ps(average, average), _mm256_load_ps(s.v + j));
            _mm256_store_ps(s.q + j, q);
            _mm256_store_ps(s.v + j, v);
            power = _mm256_add_ps(
                power, _mm256_add_ps(_mm256_mul_ps(v, v),
                                     _mm256_mul_ps(_mm256_load_ps(s.omega2 + j), _mm256_mul_ps(q, q))));
        }
        const double energy = sum8(power);
        dropped |= *s.energy >= 1e-20 && energy < 1e-20;
        *s.energy = energy;
    }
    return dropped;
}
} // namespace PianoPhysicalKernels
#else
// These symbols are unreachable on other architectures; the portable kernel
// in PianoPhysical.cpp uses SIMDe's native SSE2 or NEON implementation.
void PianoPhysicalKernels::predictAvx(Modes *, int) {}
bool PianoPhysicalKernels::advanceAvx(Modes *, int, float)
{
    return false;
}
#endif
