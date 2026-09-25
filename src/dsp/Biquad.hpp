// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Math.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace titv::dsp {

struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    // Design frequencies are kept below Nyquist: at low sample rates a 12 kHz shelf
    // would otherwise give unstable coefficients. Above the limit the filter acts at
    // the limit instead.
    static double omega(double freq, double sampleRate) noexcept
    {
        const double f = std::clamp(freq, 1.0, 0.45 * sampleRate);
        return 2.0 * kPi * f / sampleRate;
    }

    // Second-order high-pass (RBJ cookbook). q = 1/sqrt(2) gives a Butterworth response.
    static BiquadCoeffs highPass(double freq, double q, double sampleRate) noexcept
    {
        const double w0 = omega(freq, sampleRate);
        const double cosw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = (1.0 + cosw) / 2.0 / a0;
        c.b1 = -(1.0 + cosw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cosw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    static BiquadCoeffs lowPass(double freq, double q, double sampleRate) noexcept
    {
        const double w0 = omega(freq, sampleRate);
        const double cosw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = (1.0 - cosw) / 2.0 / a0;
        c.b1 = (1.0 - cosw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cosw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    // Band-pass with 0 dB gain at the centre frequency.
    static BiquadCoeffs bandPass(double freq, double q, double sampleRate) noexcept
    {
        const double w0 = omega(freq, sampleRate);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        BiquadCoeffs c;
        c.b0 = alpha / a0;
        c.b1 = 0.0;
        c.b2 = -alpha / a0;
        c.a1 = -2.0 * std::cos(w0) / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    static BiquadCoeffs peaking(double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = omega(freq, sampleRate);
        const double cosw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        BiquadCoeffs c;
        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = -2.0 * cosw / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = c.b1;
        c.a2 = (1.0 - alpha / A) / a0;
        return c;
    }

    static BiquadCoeffs lowShelf(double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = omega(freq, sampleRate);
        const double cosw = std::cos(w0);
        const double k = 2.0 * std::sqrt(A) * std::sin(w0) / (2.0 * q);
        const double a0 = (A + 1.0) + (A - 1.0) * cosw + k;
        BiquadCoeffs c;
        c.b0 = A * ((A + 1.0) - (A - 1.0) * cosw + k) / a0;
        c.b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0;
        c.b2 = A * ((A + 1.0) - (A - 1.0) * cosw - k) / a0;
        c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0;
        c.a2 = ((A + 1.0) + (A - 1.0) * cosw - k) / a0;
        return c;
    }

    static BiquadCoeffs highShelf(double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = omega(freq, sampleRate);
        const double cosw = std::cos(w0);
        const double k = 2.0 * std::sqrt(A) * std::sin(w0) / (2.0 * q);
        const double a0 = (A + 1.0) - (A - 1.0) * cosw + k;
        BiquadCoeffs c;
        c.b0 = A * ((A + 1.0) + (A - 1.0) * cosw + k) / a0;
        c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0;
        c.b2 = A * ((A + 1.0) + (A - 1.0) * cosw - k) / a0;
        c.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0;
        c.a2 = ((A + 1.0) - (A - 1.0) * cosw - k) / a0;
        return c;
    }

    // Magnitude of the response at freq, for measurement and tests.
    double magnitudeAt(double freq, double sampleRate) const noexcept
    {
        const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * freq / sampleRate);
        const std::complex<double> z2 = z1 * z1;
        return std::abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }
};

// Transposed direct form II with double-precision state: low cutoffs at 192 kHz
// put the poles close to the unit circle, where float state becomes noisy.
class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { c_ = c; }
    void reset() noexcept { s1_ = s2_ = 0.0; }

    float process(float x) noexcept
    {
        const double in = x;
        const double y = c_.b0 * in + s1_;
        s1_ = c_.b1 * in - c_.a1 * y + s2_;
        s2_ = c_.b2 * in - c_.a2 * y;
        return static_cast<float>(y);
    }

private:
    BiquadCoeffs c_;
    double s1_ = 0.0, s2_ = 0.0;
};

} // namespace titv::dsp
