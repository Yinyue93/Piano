#include "PianoPhysical.h"
#include "PianoPhysicalKernels.h"
#include <algorithm>
#include <array>
#include <cmath>
#include "../../modules/simde/x86/sse2.h"
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int keys = 88, ports = 4, maxModes = 296, boardModes = 180;
using Vector = std::array<double, ports>;
using Matrix = std::array<Vector, ports>;

bool avx2Available()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7)
        return false;
    __cpuid(info, 1);
    if ((info[2] & ((1 << 27) | (1 << 28))) != ((1 << 27) | (1 << 28)) || (_xgetbv(0) & 6) != 6)
        return false;
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

double smooth(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}
double interpolate(int note, const std::array<double, 5> &values)
{
    const double x = std::clamp((note - 60.0) / 12.0, 0.0, 4.0);
    const int i = std::min(3, int(x));
    return values[size_t(i)] + (values[size_t(i + 1)] - values[size_t(i)]) * (x - i);
}
double geometric(int note, std::array<double, 5> values)
{
    for (auto &value : values)
        value = std::log(value);
    return std::exp(interpolate(note, values));
}
Vector location(double x)
{
    x = std::clamp(x, 0.0, 1.0) * (ports - 1);
    const int i = std::min(ports - 2, int(x));
    Vector w{};
    w[size_t(i)] = 1 - (x - i);
    w[size_t(i + 1)] = x - i;
    return w;
}
double dot(const Vector &a, const Vector &b)
{
    const auto x =
        simde_mm_add_pd(simde_mm_mul_pd(simde_mm_loadu_pd(a.data()), simde_mm_loadu_pd(b.data())),
                        simde_mm_mul_pd(simde_mm_loadu_pd(a.data() + 2), simde_mm_loadu_pd(b.data() + 2)));
    alignas(16) double pair[2];
    simde_mm_store_pd(pair, x);
    return pair[0] + pair[1];
}
double sum4(simde__m128 x)
{
    alignas(16) float values[4];
    simde_mm_store_ps(values, x);
    return double(values[0]) + values[1] + values[2] + values[3];
}
Vector multiply(const Matrix &a, const Vector &b)
{
    Vector y{};
    for (int i = 0; i < ports; ++i)
        y[size_t(i)] = dot(a[size_t(i)], b);
    return y;
}
Vector solve(Matrix a, Vector b)
{
    // Four regional bridge ports. Pivoting also covers deliberately extreme
    // parameter settings; ordinary passive systems are well conditioned here.
    for (int k = 0; k < ports; ++k)
    {
        int best = k;
        for (int i = k + 1; i < ports; ++i)
            if (std::abs(a[size_t(i)][size_t(k)]) > std::abs(a[size_t(best)][size_t(k)]))
                best = i;
        std::swap(a[size_t(k)], a[size_t(best)]);
        std::swap(b[size_t(k)], b[size_t(best)]);
        const double pivot = a[size_t(k)][size_t(k)];
        if (std::abs(pivot) < 1e-18)
            return {};
        for (int i = k + 1; i < ports; ++i)
        {
            const double r = a[size_t(i)][size_t(k)] / pivot;
            for (int j = k + 1; j < ports; ++j)
                a[size_t(i)][size_t(j)] -= r * a[size_t(k)][size_t(j)];
            b[size_t(i)] -= r * b[size_t(k)];
        }
    }
    Vector x{};
    for (int i = ports - 1; i >= 0; --i)
    {
        double y = b[size_t(i)];
        for (int j = i + 1; j < ports; ++j)
            y -= a[size_t(i)][size_t(j)] * x[size_t(j)];
        x[size_t(i)] = y / a[size_t(i)][size_t(i)];
    }
    return x;
}
} // namespace

struct PianoPhysical::Impl
{
    struct String
    {
        // Mass-normalized coordinates relative to the moving bridge. h is
        // the felt-contact shape; b is the reciprocal inertial coupling.
        alignas(32) std::array<float, maxModes> q{}, v{}, a{}, omega2{}, inv{}, h{}, b{}, sigma{},
            damperWeight{}, kq{}, hf{}, bf{};
        int count = 0, fullCount = 0, activeCount = 0, primaryCount = 0, note = 60;
        double pitch = 0, ghh = 0, ghb = 0, gbb = 0, ah = 0, ab = 0;
        double massAtBridge = 0, stiffnessAtBridge = 0, residualMass = 0, oldVelocity = 0, position = 0;
        double freeForce = 0, mobility = 0, hammerToBridge = 0;
        double mass = .006, stiffness = 0, exponent = 2.7, viscosity = .05;
        double compression = 0, hammerVelocity = 0, hammer = 0, contactDerivative = 0;
        double damper = 1, targetDamper = 1, level = 0;
        Vector weights{};
        bool key = false, touching = false, struck = false, fullBasis = false;
        unsigned age = 0;
    };
    struct BodyMode
    {
        double q = 0, v = 0, a = 0, omega2 = 0, inv = 0;
        Vector weights{};
        double leftAcceleration = 0, rightAcceleration = 0;
    };
    std::array<String, keys> strings;
    std::array<String, keys> strikeTemplates;
    std::array<Parameters, keys> templateParameters;
    std::array<BodyMode, boardModes> body;
    int bodyCount = 0;
    Matrix bodyMobility{};
    Matrix linearInverse{}, contactMobility{};
    Matrix parkedMass{}, parkedStiffness{};
    std::array<int, keys> enabled{};
    std::array<PianoPhysicalKernels::Modes, keys> kernelViews{};
    int enabledCount = 0;
    Parameters parameters;
    Configuration config;
    double rate = 48000, dt = 1.0 / 96000, pedal = 0, damperSmoothing = 0;
    int factor = 2, filterLength = 129, filterPosition = 0, controlPhase = 0;
    std::array<double, 257> filter{}, historyL{}, historyR{};
    bool running = false, couplingDirty = false, useAvx = false;
    unsigned quietCheck = 0;
    double previousBass = 0;

    double tunedFrequency(int note) const
    {
        // Smooth design curve guided by the recorded reference's treble
        // fundamentals. It is independent of unison detuning.
        const double cents = note <= 69  ? 0
                             : note < 72 ? .5 * (note - 69)
                                         : interpolate(note, {0, 1.5, 7.0, 18.0, 48.0});
        return 440 * std::exp2((note - 69 + cents / 100) / 12.0);
    }

    void configure(String &s)
    {
        const auto &p = parameters;
        const double f = s.pitch = tunedFrequency(s.note);
        const double radius =
            interpolate(s.note, {.51, .48, .453333, .426667, .40}) * .001 * std::clamp(p.radius, .6, 1.6);
        const double density = std::clamp(p.density, 4000.0, 16000.0);
        const double mu = pi * radius * radius * density;
        const double length = std::sqrt(900 / mu) / (2 * f) * std::clamp(p.length, .6, 1.6);
        const double tension = 4 * length * length * f * f * mu;
        const double B = pi * pi * pi * std::clamp(p.young, 80e9, 400e9) * std::pow(radius, 4) /
                         (4 * length * length * tension);
        const double modalMass = mu * length / 2;
        const int unisons = s.note < 31 ? 1 : s.note < 41 ? 2 : 3;
        const int simulatedUnisons = s.struck ? unisons : 1;
        const double basisMass = s.struck ? modalMass : modalMass * unisons;
        s.fullBasis = s.struck;
        const double upper = smooth((s.note - 96.0) / 12);
        const double strikePosition =
            std::clamp(p.position * (1 - .16 * smooth((s.note - 60.0) / 48)) * (1 - .2 * upper), .055, .22);
        const double width = .0012 / length;
        const double t60 = interpolate(s.note, {8, 4.2, 4.8, 2.2, 2.1});
        s.mass = interpolate(s.note, {.009, .0078, .0061, .0048, .0036}) *
                 interpolate(s.note, {1.1, 1.5, 1, .6, .6}) * (1 - .4 * upper) * std::clamp(p.mass, .35, 3.0);
        s.exponent = std::clamp(interpolate(s.note, {2.4, 2.55, 2.7, 2.9, 3.1}) * p.exponent, 1.5, 4.0);
        s.stiffness = 32 * (1 + upper) * interpolate(s.note, {.5, .25, 1, 4, 4}) *
                      std::clamp(p.stiffness, 0.0, 4.0) / std::pow(.0005, s.exponent);
        s.viscosity = .045 * std::clamp(p.hysteresis, .1, 5.0);
        // Displacement is the fixed-end modal sum plus x/L times bridge
        // displacement. Kinetic energy gives b and endpoint mass mu*L/3;
        // tension contributes endpoint stiffness T/L. This reciprocal inertial
        // coupling avoids treating silent strings as dashpots on the board.
        const double horizontal = std::clamp(p.coupling / 400.0, .1, 4.0) * .23 *
                                  std::sqrt(std::clamp(60000 / p.horizontal, .25, 4.0));
        s.position = strikePosition;
        s.massAtBridge = unisons * 2 * modalMass / 3 * (1 + horizontal * horizontal);
        s.stiffnessAtBridge = unisons * tension / length * (1 + horizontal * horizontal);
        s.weights = location(std::clamp((s.note - 40.0) / 68.0, 0.0, 1.0));
        // Bridge ribs distribute each point load over a much larger area than
        // the bare plate basis. This participation sets its driving mobility.
        const double participation = geometric(s.note, {.1, .16, .04, .024, .008});
        for (auto &weight : s.weights)
            weight *= participation * std::sqrt(std::clamp(8000 / p.bridge, .25, 4.0));
        // Keep the duplex resonators at the front so a ringing duplex does
        // not prevent inaudible high speaking-string modes from being parked.
        s.count = s.struck ? 8 : 0;
        for (int j = 0; j < s.count; ++j)
            s.omega2[size_t(j)] = s.h[size_t(j)] = s.b[size_t(j)] = s.sigma[size_t(j)] =
                s.damperWeight[size_t(j)] = s.q[size_t(j)] = s.v[size_t(j)] = 0;
        // A collective coordinate retains the four sympathetic partials in
        // both polarizations without resolving almost-identical unisons.
        // A strike expands this state into the full three-string basis.
        const int partialLimit = s.struck ? 48 : 4;
        const double detuning = std::clamp(p.detune, 0.0, 4.0);
        for (int k = 1; k <= partialLimit; ++k)
            for (int polarization = 0; polarization < 2; ++polarization)
                for (int string = 0; string < simulatedUnisons; ++string)
                {
                    const double offsets[3] = {-.45, .08, .37};
                    const double individual =
                        s.struck ? offsets[string] * (.8 + .2 * std::sin(s.note * 1.71 + string)) : 0;
                    const double cents = detuning * (individual + polarization * .12);
                    const double hz = f * k * std::sqrt((1 + B * k * k) / (1 + B)) * std::exp2(cents / 1200);
                    const double bandwidth = std::min(22000.0, .45 / dt);
                    if (hz > bandwidth || s.count >= maxModes)
                        break;
                    const double band = 1 - smooth((hz / bandwidth - .82) / .18);
                    const double omega = 2 / dt * std::tan(pi * hz * dt);
                    const int j = s.count++;
                    s.omega2[size_t(j)] = omega * omega;
                    const double spread = pi * k * width * .5;
                    const double contact = std::sin(pi * k * strikePosition) * (std::sin(spread) / spread);
                    s.h[size_t(j)] =
                        polarization == 0 ? band * contact / (simulatedUnisons * std::sqrt(basisMass)) : 0;
                    s.b[size_t(j)] = band * 2 * std::sqrt(basisMass) * (k % 2 ? 1 : -1) / (pi * k) *
                                     (polarization ? horizontal : 1.0);
                    const double intrinsic = 6.907755 / t60 * std::clamp(p.decay / .25, .2, 5.0);
                    const double partialLoss = interpolate(s.note, {.15, .8, .4, .12, .08});
                    s.sigma[size_t(j)] =
                        intrinsic * (polarization ? .65 : 1) *
                        (1 + partialLoss * std::pow(k - 1, 1.45) * std::clamp(p.loss / 5.85, .15, 5.0));
                    s.damperWeight[size_t(j)] = s.note >= firstUndampedNote ? 0 : (24 + 1.5 * k);
                }
        s.primaryCount = s.count - (s.struck ? 8 : 0);
        if (config.duplex && s.note >= 60)
            for (int k = 0; k < 2; ++k)
            {
                const double hz = f * (k == 0 ? 2.03 : 3.97);
                if (hz > std::min(21000.0, .43 / dt))
                    continue;
                const double duplexMass = modalMass * .01;
                s.massAtBridge += 2 * duplexMass / 3;
                s.stiffnessAtBridge += 8 * hz * hz * duplexMass;
                if (!s.struck)
                    continue;
                const int j = k;
                const double w = 2 / dt * std::tan(pi * hz * dt);
                s.omega2[size_t(j)] = w * w;
                s.h[size_t(j)] = 0;
                s.b[size_t(j)] = 2 * std::sqrt(duplexMass) / pi;
                s.sigma[size_t(j)] = 18 + f * .002;
                s.damperWeight[size_t(j)] = 0;
            }
        while (s.count % 8)
        {
            const auto k = size_t(s.count++);
            s.omega2[k] = s.h[k] = s.b[k] = s.sigma[k] = s.damperWeight[k] = s.q[k] = s.v[k] = 0;
        }
        s.fullCount = s.activeCount = s.count;
        updateDamping(s);
    }

    void updateDamping(String &s)
    {
        s.ghh = s.ghb = s.gbb = 0;
        for (int j = 0; j < s.count; ++j)
        {
            const auto k = size_t(j);
            s.inv[k] =
                1 / (1 + dt * (s.sigma[k] + s.damper * s.damperWeight[k]) + dt * dt * .25 * s.omega2[k]);
            const double g = dt * .5 * s.inv[k];
            s.kq[k] = float(dt * .5 * s.omega2[k]);
            s.hf[k] = float(g * s.h[k]);
            s.bf[k] = s.inv[k] * s.b[k];
            s.ghh += g * s.h[k] * s.h[k];
            s.ghb += s.inv[k] * s.h[k] * s.b[k];
            s.gbb += s.inv[k] * s.b[k] * s.b[k];
        }
        s.residualMass = std::max(0.0, s.massAtBridge - s.gbb);
        s.mobility = 2 / dt * s.residualMass + dt * .5 * s.stiffnessAtBridge;
        s.hammerToBridge = s.position - s.ghb;
    }

    void prepareCoupling()
    {
        Matrix load{};
        parkedMass = {};
        parkedStiffness = {};
        enabledCount = 0;
        for (int i = 0; i < keys; ++i)
        {
            auto &s = strings[size_t(i)];
            if (s.fullBasis != s.struck)
            {
                // A played note is reduced only after its energy is inaudible.
                s.q.fill(0);
                s.v.fill(0);
                configure(s);
            }
            const bool clamped = !s.key && s.note < firstUndampedNote && pedal < .001 && s.level < 1e-20;
            const int count = s.struck ? s.activeCount : (config.sympathetic && !clamped) ? s.fullCount : 0;
            if (s.count != count)
            {
                s.count = count;
                if (count == 0)
                {
                    s.q.fill(0);
                    s.v.fill(0);
                    s.level = 0;
                }
                updateDamping(s);
            }
            if (count > 0)
            {
                enabled[size_t(enabledCount)] = i;
                kernelViews[size_t(enabledCount++)] = {s.q.data(),  s.v.data(),      s.a.data(), s.inv.data(),
                                                       s.kq.data(), s.h.data(),      s.b.data(), s.hf.data(),
                                                       s.bf.data(), s.omega2.data(), s.count,    &s.ah,
                                                       &s.ab,       &s.level};
            }
            for (int p = 0; p < ports; ++p)
                for (int q = 0; q < ports; ++q)
                {
                    load[size_t(p)][size_t(q)] += s.mobility * s.weights[size_t(p)] * s.weights[size_t(q)];
                    if (count == 0)
                    {
                        parkedMass[size_t(p)][size_t(q)] +=
                            s.massAtBridge * s.weights[size_t(p)] * s.weights[size_t(q)];
                        parkedStiffness[size_t(p)][size_t(q)] +=
                            s.stiffnessAtBridge * s.weights[size_t(p)] * s.weights[size_t(q)];
                    }
                }
        }
        Matrix system{};
        for (int p = 0; p < ports; ++p)
            for (int q = 0; q < ports; ++q)
            {
                system[size_t(p)][size_t(q)] = p == q ? 1 : 0;
                for (int r = 0; r < ports; ++r)
                    system[size_t(p)][size_t(q)] +=
                        bodyMobility[size_t(p)][size_t(r)] * load[size_t(r)][size_t(q)];
            }
        for (int q = 0; q < ports; ++q)
        {
            Vector unit{};
            unit[size_t(q)] = 1;
            const auto column = solve(system, unit);
            for (int p = 0; p < ports; ++p)
                linearInverse[size_t(p)][size_t(q)] = column[size_t(p)];
        }
        contactMobility = {};
        for (int p = 0; p < ports; ++p)
            for (int q = 0; q < ports; ++q)
                for (int r = 0; r < ports; ++r)
                    contactMobility[size_t(p)][size_t(q)] +=
                        linearInverse[size_t(p)][size_t(r)] * bodyMobility[size_t(r)][size_t(q)];
    }

    void prepareBody()
    {
        bodyMobility = {};
        // Low frequencies move the whole orthotropic plate. At higher
        // frequencies the ribs localize motion: four smaller participating
        // regions cover the bridge. All modes remain in the same passive
        // mobility matrix. Dimensions are a designed scale, not a measured FEM.
        constexpr double h = .009, rho = 430, ex = 11e9, ey = .8e9;
        const double dx = ex * h * h * h / 12, dy = ey * h * h * h / 12, dxy = .7 * std::sqrt(dx * dy);
        int index = 0;
        const auto add = [&](int m, int n, double a, double b, int region)
        {
            const double kx = (m + .7) / a, ky = (n + .7) / b;
            const double hz =
                pi * .5 *
                std::sqrt((dx * std::pow(kx, 4) + 2 * dxy * kx * kx * ky * ky + dy * std::pow(ky, 4)) /
                          (rho * h));
            if (hz > std::min(22000.0, .45 / dt))
                return;
            const double participation =
                std::sqrt(region < 0 ? 1 - smooth((hz - 850) / 400) : smooth((hz - 650) / 400));
            if (participation == 0)
                return;
            auto &mode = body[size_t(index++)];
            mode = {};
            const double w = 2 / dt * std::tan(pi * hz * dt), mass = rho * h * a * b / 4;
            mode.omega2 = w * w;
            mode.inv = 1 / (1 + dt * (9 + hz * (region < 0 ? .2 : .75)) + dt * dt * .25 * w * w);
            for (int p = 0; p < ports; ++p)
            {
                const double x = region < 0 ? .22 + .18 * p : .43, y = region < 0 ? .64 - .055 * p : .47;
                const double local = region < 0 || region == p ? 1 : (std::abs(region - p) == 1 ? .35 : 0);
                mode.weights[size_t(p)] =
                    local * participation * std::sin(pi * m * x) * std::sin(pi * n * y) / std::sqrt(mass);
            }
            for (int p = 0; p < ports; ++p)
                for (int q = 0; q < ports; ++q)
                    bodyMobility[size_t(p)][size_t(q)] +=
                        dt * .5 * mode.inv * mode.weights[size_t(p)] * mode.weights[size_t(q)];
            // Four coherent radiating patches are collocated with the bridge
            // ports. Observe their acceleration with coincident angled
            // cardioids. A common time-domain acceleration is essential here:
            // assigning propagation phase at each mode's natural frequency
            // gives false cancellations when strings force it off resonance.
            for (int port = 0; port < ports; ++port)
            {
                const double offset = (.22 + .18 * port) * 1.45 - .725;
                const double y = (.64 - .055 * port) * 1.55 - 1.05;
                const double r = std::sqrt(offset * offset + y * y + .50 * .50);
                const double scale = 1.2 * .035 / (2 * pi * r) * mode.weights[size_t(port)];
                mode.leftAcceleration += scale * .5 * (1 + (-.6 * offset + .4) / r);
                mode.rightAcceleration += scale * .5 * (1 + (.6 * offset + .4) / r);
            }
        };
        for (int m = 1; m <= 8; ++m)
            for (int n = 1; n <= 6; ++n)
                add(m, n, 1.45, 1.55, -1);
        for (int region = 0; region < ports; ++region)
            for (int m = 1; m <= 11; ++m)
                for (int n : {1, 2, 4})
                    add(m, n, .48 + .015 * region, .15 + .004 * region, region);
        bodyCount = index;
        while (index < boardModes)
            body[size_t(index++)] = {};
    }

    void restoreHarmonics()
    {
        for (auto &s : strings)
            if (s.struck && s.activeCount < s.fullCount)
            {
                s.activeCount = s.fullCount;
                couplingDirty = true;
            }
    }

    void parkQuietHarmonics()
    {
        if (!config.adaptiveModes)
            return;
        for (auto &s : strings)
        {
            if (!s.struck || s.touching)
                continue;
            // Retain at least four partials in all three unisons and both
            // polarizations. Remove a high group only below an absolute
            // mechanical-energy floor, and restore all groups on new keys.
            while (s.activeCount > 32)
            {
                double energy = 0;
                for (int j = s.activeCount - 8; j < s.activeCount; ++j)
                    energy += double(s.v[size_t(j)]) * s.v[size_t(j)] +
                              double(s.omega2[size_t(j)]) * s.q[size_t(j)] * s.q[size_t(j)];
                if (energy >= 1e-18)
                    break;
                s.activeCount -= 8;
                std::fill_n(s.q.data() + s.activeCount, 8, 0.0f);
                std::fill_n(s.v.data() + s.activeCount, 8, 0.0f);
                couplingDirty = true;
            }
        }
    }

    // Discrete-gradient felt force: elastic work equals the change in stored
    // contact energy. This avoids the sample-rate-dependent numerical loss of
    // an end-point force, which is particularly audible on the shortest strings.
    std::array<double, 2> felt(const String &s, double x) const
    {
        const double previous = std::max(0.0, s.compression), next = std::max(0.0, x);
        const double delta = x - s.compression;
        double elastic = 0, gradient = 0;
        if (std::abs(delta) < 1e-9)
        {
            const double mid = std::max(0.0, (x + s.compression) * .5);
            elastic = s.stiffness * std::pow(mid, s.exponent);
            gradient = mid > 0 ? .5 * s.exponent * elastic / mid : 0;
        }
        else
        {
            const double potential = s.stiffness / (s.exponent + 1) *
                                     (std::pow(next, s.exponent + 1) - std::pow(previous, s.exponent + 1));
            elastic = potential / delta;
            gradient = (s.stiffness * std::pow(next, s.exponent) - elastic) / delta;
        }
        const double viscosity = std::max(0.0, 1 + s.viscosity * delta / dt);
        return {elastic * viscosity,
                viscosity > 0 ? std::max(0.0, gradient * viscosity + elastic * s.viscosity / dt) : 0};
    }

    double contact(String &s, double boardVelocity)
    {
        s.contactDerivative = 0;
        if (!s.touching || s.stiffness == 0)
            return 0;
        const double free = s.ah + s.hammerToBridge * boardVelocity;
        const double mobility = dt / (2 * s.mass) + s.ghh;
        const double freeX = s.compression + dt * (s.hammerVelocity - free);
        if (freeX <= 0 && s.compression <= 0)
            return 0;
        const double compliance = dt * mobility;
        double lo = std::min(0.0, freeX) - .002, hi = freeX;
        for (int i = 0; i < 8 && lo + compliance * felt(s, lo)[0] > freeX; ++i)
            lo = 2 * lo - .002;
        double x = std::clamp(s.compression, lo, hi);
        for (int iteration = 0; iteration < 18; ++iteration)
        {
            const auto law = felt(s, x);
            const double force = law[0], derivative = law[1];
            const double residual = x + compliance * force - freeX;
            if (std::abs(residual) < 1e-12)
                break;
            if (residual > 0)
                hi = x;
            else
                lo = x;
            const double next = x - residual / (1 + compliance * derivative);
            x = next > lo && next < hi ? next : (lo + hi) * .5;
        }
        const auto law = felt(s, x);
        const double derivative = law[1];
        s.contactDerivative = -derivative * dt * s.hammerToBridge / (1 + compliance * derivative);
        return law[0];
    }

    Frame tick(double bass, const Vector &bassWeights)
    {
        if (++controlPhase == 16)
        {
            controlPhase = 0;
            bool changed = couplingDirty;
            couplingDirty = false;
            const double lift = 1 - pedal, pedalDamper = lift * lift * lift;
            for (auto &s : strings)
            {
                s.targetDamper = s.key || s.note >= firstUndampedNote ? 0 : pedalDamper;
                const double next = s.damper + damperSmoothing * (s.targetDamper - s.damper);
                if (std::abs(next - s.damper) > 1e-8)
                {
                    s.damper = next;
                    updateDamping(s);
                    changed = true;
                }
            }
            if (changed)
                prepareCoupling();
        }
        Vector bodyFree{}, bodyVelocity{}, bodyDisplacement{};
        auto free0 = simde_mm_setzero_pd(), free1 = free0, velocity0 = free0, velocity1 = free0,
             position0 = free0, position1 = free0;
        for (int index = 0; index < bodyCount; ++index)
        {
            auto &m = body[size_t(index)];
            m.a = (m.v - dt * .5 * m.omega2 * m.q) * m.inv;
            const auto w0 = simde_mm_loadu_pd(m.weights.data()), w1 = simde_mm_loadu_pd(m.weights.data() + 2);
            free0 = simde_mm_add_pd(free0, simde_mm_mul_pd(w0, simde_mm_set1_pd(m.a)));
            free1 = simde_mm_add_pd(free1, simde_mm_mul_pd(w1, simde_mm_set1_pd(m.a)));
            velocity0 = simde_mm_add_pd(velocity0, simde_mm_mul_pd(w0, simde_mm_set1_pd(m.v)));
            velocity1 = simde_mm_add_pd(velocity1, simde_mm_mul_pd(w1, simde_mm_set1_pd(m.v)));
            position0 = simde_mm_add_pd(position0, simde_mm_mul_pd(w0, simde_mm_set1_pd(m.q)));
            position1 = simde_mm_add_pd(position1, simde_mm_mul_pd(w1, simde_mm_set1_pd(m.q)));
        }
        simde_mm_storeu_pd(bodyFree.data(), free0);
        simde_mm_storeu_pd(bodyFree.data() + 2, free1);
        simde_mm_storeu_pd(bodyVelocity.data(), velocity0);
        simde_mm_storeu_pd(bodyVelocity.data() + 2, velocity1);
        simde_mm_storeu_pd(bodyDisplacement.data(), position0);
        simde_mm_storeu_pd(bodyDisplacement.data() + 2, position1);
        if (!config.movingBridge)
        {
            bodyVelocity = {};
            bodyDisplacement = {};
        }
        const auto bassVelocity = multiply(bodyMobility, bassWeights);
        for (int p = 0; p < ports; ++p)
            bodyFree[size_t(p)] += bass * bassVelocity[size_t(p)];
        Vector freeForce = multiply(parkedMass, bodyVelocity);
        const auto staticForce = multiply(parkedStiffness, bodyDisplacement);
        for (int p = 0; p < ports; ++p)
            freeForce[size_t(p)] = 2 / dt * freeForce[size_t(p)] - staticForce[size_t(p)];
        const Vector parkedFree = freeForce;
        bool contactActive = false;
        if (useAvx)
            PianoPhysicalKernels::predictAvx(kernelViews.data(), enabledCount);
        for (int index = 0; index < enabledCount; ++index)
        {
            auto &s = strings[size_t(enabled[size_t(index)])];
            if (!useAvx)
            {
                auto ah = simde_mm_setzero_ps(), ab = simde_mm_setzero_ps();
                for (int j = 0; j < s.count; j += 4)
                {
                    const auto a =
                        simde_mm_mul_ps(simde_mm_sub_ps(simde_mm_load_ps(s.v.data() + j),
                                                        simde_mm_mul_ps(simde_mm_load_ps(s.kq.data() + j),
                                                                        simde_mm_load_ps(s.q.data() + j))),
                                        simde_mm_load_ps(s.inv.data() + j));
                    simde_mm_store_ps(s.a.data() + j, a);
                    ah = simde_mm_add_ps(ah, simde_mm_mul_ps(simde_mm_load_ps(s.h.data() + j), a));
                    ab = simde_mm_add_ps(
                        ab, simde_mm_mul_ps(simde_mm_load_ps(s.b.data() + j),
                                            simde_mm_sub_ps(simde_mm_load_ps(s.v.data() + j), a)));
                }
                s.ah = sum4(ah);
                s.ab = sum4(ab);
            }
            s.oldVelocity = dot(s.weights, bodyVelocity);
            s.ah += s.ghb * s.oldVelocity;
            s.freeForce = 2 / dt * (s.ab + s.residualMass * s.oldVelocity) -
                          s.stiffnessAtBridge * dot(s.weights, bodyDisplacement);
            for (int p = 0; p < ports; ++p)
                freeForce[size_t(p)] += s.weights[size_t(p)] * s.freeForce;
            contactActive |= s.touching;
        }
        const auto linearLoad = multiply(bodyMobility, freeForce);
        for (int p = 0; p < ports; ++p)
            bodyFree[size_t(p)] += linearLoad[size_t(p)];
        const auto linearVelocity = multiply(linearInverse, bodyFree);
        Vector velocity = linearVelocity, totalForce{};
        if (!config.movingBridge)
            velocity = {};
        // Eliminate each string's linear bridge port and nonlinear hammer
        // locally, then solve only four shared soundboard velocities.
        for (int iteration = 0; contactActive && iteration < 5; ++iteration)
        {
            Matrix admittance{};
            totalForce = {};
            for (int index = 0; index < enabledCount; ++index)
            {
                auto &s = strings[size_t(enabled[size_t(index)])];
                if (!s.touching)
                    continue;
                const double y = dot(s.weights, velocity);
                s.hammer = contact(s, y);
                const double force = s.hammerToBridge * s.hammer;
                const double derivative = s.hammerToBridge * s.contactDerivative;
                for (int p = 0; p < ports; ++p)
                {
                    totalForce[size_t(p)] += s.weights[size_t(p)] * force;
                    for (int q = 0; q < ports; ++q)
                        admittance[size_t(p)][size_t(q)] +=
                            derivative * s.weights[size_t(p)] * s.weights[size_t(q)];
                }
            }
            if (!config.movingBridge)
                break;
            auto residual = multiply(contactMobility, totalForce);
            double error = 0;
            for (int p = 0; p < ports; ++p)
            {
                residual[size_t(p)] = velocity[size_t(p)] - linearVelocity[size_t(p)] - residual[size_t(p)];
                error += std::abs(residual[size_t(p)]);
            }
            if (error < 1e-12)
                break;
            Matrix jacobian{};
            for (int p = 0; p < ports; ++p)
                for (int q = 0; q < ports; ++q)
                {
                    jacobian[size_t(p)][size_t(q)] = p == q ? 1 : 0;
                    for (int r = 0; r < ports; ++r)
                        jacobian[size_t(p)][size_t(q)] -=
                            contactMobility[size_t(p)][size_t(r)] * admittance[size_t(r)][size_t(q)];
                }
            const auto correction = solve(jacobian, residual);
            for (int p = 0; p < ports; ++p)
                velocity[size_t(p)] -= correction[size_t(p)];
        }
        totalForce = parkedFree;
        const auto parkedInertia = multiply(parkedMass, velocity);
        const auto parkedElasticity = multiply(parkedStiffness, velocity);
        for (int p = 0; p < ports; ++p)
            totalForce[size_t(p)] -=
                2 / dt * parkedInertia[size_t(p)] + dt * .5 * parkedElasticity[size_t(p)];
        Frame result;
        for (int index = 0; index < enabledCount; ++index)
        {
            auto &s = strings[size_t(enabled[size_t(index)])];
            const double y = dot(s.weights, velocity);
            s.hammer = contact(s, y);
            const double force = s.freeForce - s.mobility * y + s.hammerToBridge * s.hammer;
            const double vh = s.ah + s.ghh * s.hammer + s.hammerToBridge * y;
            if (s.touching)
            {
                s.compression += dt * (s.hammerVelocity - dt * s.hammer / (2 * s.mass) - vh);
                s.hammerVelocity -= dt * s.hammer / s.mass;
                if (s.compression < -.0004 && s.hammerVelocity < 0)
                    s.touching = false;
                if (++s.age > unsigned(.08 / dt))
                    s.touching = false;
            }
            if (useAvx)
            {
                kernelViews[size_t(index)].hammer = float(s.hammer);
                kernelViews[size_t(index)].bridge = float(s.oldVelocity - y);
            }
            else
            {
                auto power = simde_mm_setzero_ps();
                const auto hammer = simde_mm_set1_ps(float(s.hammer)),
                           bridge = simde_mm_set1_ps(float(s.oldVelocity - y));
                const auto step = simde_mm_set1_ps(float(dt));
                for (int j = 0; j < s.count; j += 4)
                {
                    const auto average = simde_mm_add_ps(
                        simde_mm_load_ps(s.a.data() + j),
                        simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(s.hf.data() + j), hammer),
                                        simde_mm_mul_ps(simde_mm_load_ps(s.bf.data() + j), bridge)));
                    const auto q =
                        simde_mm_add_ps(simde_mm_load_ps(s.q.data() + j), simde_mm_mul_ps(step, average));
                    const auto v =
                        simde_mm_sub_ps(simde_mm_add_ps(average, average), simde_mm_load_ps(s.v.data() + j));
                    simde_mm_store_ps(s.q.data() + j, q);
                    simde_mm_store_ps(s.v.data() + j, v);
                    power = simde_mm_add_ps(
                        power, simde_mm_add_ps(simde_mm_mul_ps(v, v),
                                               simde_mm_mul_ps(simde_mm_load_ps(s.omega2.data() + j),
                                                               simde_mm_mul_ps(q, q))));
                }
                const double level = sum4(power);
                if (s.level >= 1e-20 && level < 1e-20)
                    couplingDirty = true;
                s.level = level;
            }
            for (int p = 0; p < ports; ++p)
                totalForce[size_t(p)] += s.weights[size_t(p)] * force;
            result.bridgeForce += float(force);
            result.hammerForce += float(s.hammer);
        }
        if (useAvx)
            couplingDirty |= PianoPhysicalKernels::advanceAvx(kernelViews.data(), enabledCount, float(dt));
        for (int index = 0; index < enabledCount; ++index)
        {
            auto &s = strings[size_t(enabled[size_t(index)])];
            if (s.struck && !s.touching && s.level < 1e-20)
            {
                s.struck = false;
                couplingDirty = true;
            }
        }
        for (int index = 0; index < bodyCount; ++index)
        {
            auto &m = body[size_t(index)];
            const double force = bass * dot(m.weights, bassWeights) + dot(m.weights, totalForce);
            const double average = m.a + dt * .5 * m.inv * force;
            const double acceleration = 2 / dt * (average - m.v);
            m.q += dt * average;
            m.v = 2 * average - m.v;
            result.left += float(m.leftAcceleration * acceleration);
            result.right += float(m.rightAcceleration * acceleration);
        }
        return result;
    }
};

PianoPhysical::PianoPhysical() : impl(std::make_unique<Impl>()) {}
PianoPhysical::~PianoPhysical() = default;
void PianoPhysical::prepare(double rate)
{
    prepare(rate, Configuration{});
}
void PianoPhysical::prepare(double rate, Configuration config)
{
    auto &m = *impl;
    m.rate = std::isfinite(rate) ? std::clamp(rate, 8000.0, 384000.0) : 48000.0;
    m.config = config;
    m.useAvx = config.nativeVectorization && avx2Available();
    m.factor = config.oversampling >= 4 ? 4 : 2;
    m.dt = 1 / (m.rate * m.factor);
    m.damperSmoothing = 1 - std::exp(-16 * m.dt / .008);
    for (int i = 0; i < keys; ++i)
    {
        m.strings[size_t(i)].note = i + 21;
        m.configure(m.strings[size_t(i)]);
        auto &model = m.strikeTemplates[size_t(i)];
        model.note = i + 21;
        model.struck = true;
        model.damper = 0;
        m.configure(model);
        m.templateParameters[size_t(i)] = m.parameters;
    }
    m.prepareBody();
    m.filterLength = 64 * m.factor + 1;
    double sum = 0;
    const double cutoff = .45 / m.factor;
    for (int i = 0; i < m.filterLength; ++i)
    {
        const int x = i - (m.filterLength - 1) / 2;
        const double sinc = x == 0 ? 2 * cutoff : std::sin(2 * pi * cutoff * x) / (pi * x);
        const double a = 2 * pi * i / (m.filterLength - 1);
        const double window = .42 - .5 * std::cos(a) + .08 * std::cos(2 * a);
        sum += m.filter[size_t(i)] = sinc * window;
    }
    for (int i = 0; i < m.filterLength; ++i)
        m.filter[size_t(i)] /= sum;
    reset();
}
void PianoPhysical::reset()
{
    auto &m = *impl;
    for (auto &s : m.strings)
    {
        s.q.fill(0);
        s.v.fill(0);
        s.a.fill(0);
        s.oldVelocity = s.compression = s.hammerVelocity = s.hammer = s.level = 0;
        s.key = s.touching = s.struck = false;
        s.age = 0;
        s.damper = s.targetDamper = s.note >= firstUndampedNote ? 0 : 1;
        if (s.count)
            m.updateDamping(s);
    }
    for (auto &b : m.body)
        b.q = b.v = b.a = 0;
    m.historyL.fill(0);
    m.historyR.fill(0);
    m.filterPosition = 0;
    m.pedal = 0;
    m.previousBass = 0;
    m.running = false;
    m.controlPhase = 0;
    m.quietCheck = 0;
    m.couplingDirty = false;
    m.prepareCoupling();
}
void PianoPhysical::setParameters(const Parameters &p)
{
    impl->parameters = p;
}
void PianoPhysical::strike(int note, float velocity)
{
    if (note < firstStruckNote || note > 108 || !std::isfinite(velocity) || velocity <= 0)
        return;
    auto &m = *impl;
    auto &s = m.strings[size_t(note - 21)];
    m.restoreHarmonics();
    // Default full string coefficients are prepared off the audio thread.
    // Only changed physical parameters require a rebuild on the next strike.
    const bool expand = !s.fullBasis;
    const auto oldQ = s.q, oldV = s.v;
    const bool key = s.key;
    const double level = s.level;
    auto &model = m.strikeTemplates[size_t(note - 21)];
    if (m.templateParameters[size_t(note - 21)] != m.parameters)
    {
        m.configure(model);
        m.templateParameters[size_t(note - 21)] = m.parameters;
    }
    s = model;
    s.key = key;
    s.level = level;
    s.touching = s.struck = true;
    s.age = 0;
    if (expand)
        for (int j = 0; j < std::min(s.primaryCount, 24); ++j)
        {
            s.q[size_t(j + 8)] = oldQ[size_t(j / 3)] / std::sqrt(3.0f);
            s.v[size_t(j + 8)] = oldV[size_t(j / 3)] / std::sqrt(3.0f);
        }
    else
    {
        s.q = oldQ;
        s.v = oldV;
    }
    s.compression = 0;
    s.hammerVelocity = std::clamp(double(velocity), 0.0, 15.0);
    s.damper = s.targetDamper = 0;
    m.prepareCoupling();
    m.running = true;
}
void PianoPhysical::setKey(int note, bool down)
{
    if (note >= 21 && note <= 108)
    {
        if (down)
            impl->restoreHarmonics();
        auto &key = impl->strings[size_t(note - 21)].key;
        if (key != down)
        {
            key = down;
            impl->couplingDirty = true;
        }
    }
}
void PianoPhysical::setPedal(float position)
{
    const double value = std::isfinite(position) ? std::clamp(double(position), 0.0, 1.0) : 0.0;
    if (value != impl->pedal)
    {
        impl->pedal = value;
        impl->couplingDirty = true;
    }
}
void PianoPhysical::silence(int note)
{
    if (note < 21 || note > 108)
        return;
    auto &s = impl->strings[size_t(note - 21)];
    s.q.fill(0);
    s.v.fill(0);
    s.a.fill(0);
    s.oldVelocity = s.compression = s.hammerVelocity = s.hammer = s.level = 0;
    s.touching = s.struck = s.key = false;
    impl->couplingDirty = true;
}
PianoPhysical::Frame PianoPhysical::process(float bassForce, float bassPosition)
{
    auto &m = *impl;
    if (bassForce != 0)
        m.running = true;
    if (!m.running)
        return {};
    Frame result;
    const auto weights = location(bassPosition);
    for (int i = 0; i < m.factor; ++i)
    {
        const double bass = m.previousBass + (bassForce - m.previousBass) * (i + 1) / m.factor;
        const auto frame = m.tick(bass, weights);
        m.historyL[size_t(m.filterPosition)] = frame.left;
        m.historyR[size_t(m.filterPosition)] = frame.right;
        if (++m.filterPosition == m.filterLength)
            m.filterPosition = 0;
        result.bridgeForce += frame.bridgeForce / m.factor;
        result.hammerForce += frame.hammerForce / m.factor;
    }
    m.previousBass = bassForce;
    double left = 0, right = 0;
    int read = m.filterPosition;
    for (int k = 0; k < m.filterLength; ++k)
    {
        if (--read < 0)
            read = m.filterLength - 1;
        left += m.filter[size_t(k)] * m.historyL[size_t(read)];
        right += m.filter[size_t(k)] * m.historyR[size_t(read)];
    }
    result.left = float(left);
    result.right = float(right);
    if (++m.quietCheck == 1024)
    {
        m.quietCheck = 0;
        m.parkQuietHarmonics();
        const bool contact =
            std::any_of(m.strings.begin(), m.strings.end(), [](const auto &s) { return s.touching; });
        if (bassForce == 0 && !contact && energy() < 1e-22)
        {
            // Preserve lifted keys and pedal position while parking an
            // inaudible tail. A later strike or bass input wakes the model.
            for (auto &s : m.strings)
            {
                s.q.fill(0);
                s.v.fill(0);
                s.level = 0;
                s.struck = false;
            }
            for (auto &b : m.body)
                b.q = b.v = 0;
            m.historyL.fill(0);
            m.historyR.fill(0);
            m.running = false;
        }
    }
    return result;
}
bool PianoPhysical::isActive(int note) const
{
    if (note < 21 || note > 108)
        return false;
    const auto &s = impl->strings[size_t(note - 21)];
    return s.touching || s.level > 1e-16;
}
int PianoPhysical::activeNotes() const
{
    int n = 0;
    for (int note = firstStruckNote; note <= 108; ++note)
        if (isActive(note))
            ++n;
    return n;
}
int PianoPhysical::activeStringModes() const
{
    int count = 0;
    for (const auto &s : impl->strings)
        count += s.count;
    return count;
}

double PianoPhysical::energy() const
{
    double e = 0;
    Vector velocity{}, displacement{};
    for (const auto &b : impl->body)
        for (int p = 0; p < ports; ++p)
        {
            velocity[size_t(p)] += b.weights[size_t(p)] * b.v;
            displacement[size_t(p)] += b.weights[size_t(p)] * b.q;
        }
    for (const auto &s : impl->strings)
    {
        const double u = dot(s.weights, velocity), y = dot(s.weights, displacement);
        e += .5 * (s.massAtBridge * u * u + s.stiffnessAtBridge * y * y);
        for (int j = 0; j < s.count; ++j)
            e += .5 * (s.v[size_t(j)] * s.v[size_t(j)] +
                       s.omega2[size_t(j)] * s.q[size_t(j)] * s.q[size_t(j)]) +
                 u * s.b[size_t(j)] * s.v[size_t(j)];
        if (s.touching)
            e += .5 * s.mass * s.hammerVelocity * s.hammerVelocity +
                 s.stiffness / (s.exponent + 1) * std::pow(std::max(0.0, s.compression), s.exponent + 1);
    }
    for (const auto &b : impl->body)
        e += .5 * (b.v * b.v + b.omega2 * b.q * b.q);
    return e;
}
double PianoPhysical::frequency(int note) const
{
    return impl->tunedFrequency(note);
}
