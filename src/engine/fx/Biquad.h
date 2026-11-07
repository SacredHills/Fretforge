#pragma once
#include <cmath>
#include <complex>
#include <juce_core/juce_core.h>

namespace ff::fx
{
/** RBJ-cookbook biquad. Coefficients are computed in place (no allocation), so it can be retuned on the audio thread. */
struct Biquad
{
    enum Type { lowPass, highPass, peak, lowShelf, highShelf, notch, bandPass };

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    void set (Type type, double sr, double freq, double q, double gainDb = 0.0) noexcept
    {
        freq = juce::jlimit (1.0, sr * 0.49, freq);
        q = juce::jmax (0.05, q);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (type)
        {
            case lowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case highPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case lowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s;       A1 = -2 * ((A - 1) + (A + 1) * cw);    A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case highShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s;       A1 = 2 * ((A - 1) - (A + 1) * cw);      A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case notch:    B0 = 1; B1 = -2 * cw; B2 = 1; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case bandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;   // 0 dB peak
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    /** First-order (6 dB/oct) low or high pass (bilinear transform). */
    void setFirstOrder (bool highPassFilter, double sr, double freq) noexcept
    {
        freq = juce::jlimit (1.0, sr * 0.49, freq);
        const double K = std::tan (juce::MathConstants<double>::pi * freq / sr);
        const double n = 1.0 / (1.0 + K);
        b0 = (float) (highPassFilter ? n : K * n);
        b1 = (float) (highPassFilter ? -n : K * n);
        b2 = 0.0f;
        a1 = (float) ((K - 1.0) * n);
        a2 = 0.0f;
    }
    /** Pass-through. */
    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    /** |H| at `freq` (for drawing responses). */
    double magnitude (double freq, double sr) const noexcept
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const std::complex<double> z1c = std::polar (1.0, -w), z2c = std::polar (1.0, -2.0 * w);
        const auto num = (double) b0 + (double) b1 * z1c + (double) b2 * z2c;
        const auto den = 1.0 + (double) a1 * z1c + (double) a2 * z2c;
        return std::abs (num / den);
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void process (float* d, int n) noexcept
    {
        for (int i = 0; i < n; ++i) d[i] = process (d[i]);
        if (std::abs (z1) < 1.0e-20f) z1 = 0;   // flush denormals left by silence
        if (std::abs (z2) < 1.0e-20f) z2 = 0;
    }
};
} // namespace ff::fx
