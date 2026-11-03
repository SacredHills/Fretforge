#include "PitchDetector.h"
#include <cmath>

namespace ff
{
void PitchDetector::prepare (double sampleRate, double minFrequency, double maxFrequency)
{
    sr = sampleRate;
    tauMax = (int) std::ceil (sr / juce::jmax (1.0, minFrequency));
    tauMin = juce::jmax (2, (int) std::floor (sr / juce::jmax (minFrequency * 2.0, maxFrequency)));

    window = juce::nextPowerOfTwo (tauMax * 2);
    const int fftSize = window * 2; // zero padding for linear (not circular) correlation
    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) fftSize)));

    fftData.assign ((size_t) fftSize * 2, 0.0f);
    frame.assign ((size_t) window, 0.0f);
    nsdf.assign ((size_t) tauMax + 2, 0.0);
}

PitchDetector::Result PitchDetector::detect (const float* samples) noexcept
{
    Result result;
    if (fft == nullptr)
        return result;

    // Remove DC and measure level.
    double mean = 0.0;
    for (int i = 0; i < window; ++i)
        mean += samples[i];
    mean /= window;

    double sumSq = 0.0;
    for (int i = 0; i < window; ++i)
    {
        const float v = (float) (samples[i] - mean);
        frame[(size_t) i] = v;
        sumSq += (double) v * v;
    }
    result.rms = std::sqrt (sumSq / window);
    if (sumSq < 1.0e-12)
        return result;

    // Autocorrelation r(tau) via |FFT|^2.
    const int fftSize = fft->getSize();
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (frame.begin(), frame.end(), fftData.begin());
    fft->performRealOnlyForwardTransform (fftData.data(), true);
    for (int k = 0; k < fftSize; ++k)
    {
        const float re = fftData[(size_t) (2 * k)];
        const float im = fftData[(size_t) (2 * k + 1)];
        fftData[(size_t) (2 * k)] = re * re + im * im;
        fftData[(size_t) (2 * k + 1)] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (fftData.data());

    // Normalise so r(0) == sumSq regardless of the FFT implementation's scaling.
    const double r0 = fftData[0];
    if (r0 <= 0.0)
        return result;
    const double scale = sumSq / r0;

    // NSDF: n(tau) = 2 r(tau) / m(tau), m(tau) = sum x[j]^2 + x[j+tau]^2
    double m = 2.0 * sumSq;
    nsdf[0] = 1.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        const double a = frame[(size_t) (tau - 1)];
        const double b = frame[(size_t) (window - tau)];
        m -= a * a + b * b;
        const double r = fftData[(size_t) tau] * scale;
        nsdf[(size_t) tau] = m > 1.0e-12 ? 2.0 * r / m : 0.0;
    }

    // Key maxima: highest point between each positive-going and negative-going zero crossing.
    constexpr int maxPeaks = 64;
    int peakTau[maxPeaks];
    double peakVal[maxPeaks];
    int numPeaks = 0;
    double highest = 0.0;

    int tau = 1;
    while (tau < tauMax && nsdf[(size_t) tau] > 0.0) // skip the lobe around zero lag
        ++tau;

    while (tau < tauMax && numPeaks < maxPeaks)
    {
        while (tau < tauMax && nsdf[(size_t) tau] <= 0.0)
            ++tau;
        int bestTau = -1;
        double best = 0.0;
        while (tau < tauMax && nsdf[(size_t) tau] > 0.0)
        {
            if (nsdf[(size_t) tau] > best && tau >= tauMin)
            {
                best = nsdf[(size_t) tau];
                bestTau = tau;
            }
            ++tau;
        }
        if (bestTau > 0)
        {
            peakTau[numPeaks] = bestTau;
            peakVal[numPeaks] = best;
            highest = juce::jmax (highest, best);
            ++numPeaks;
        }
    }

    if (numPeaks == 0 || highest <= 0.0)
        return result;

    const double threshold = peakK * highest;
    int chosen = 0;
    for (int i = 0; i < numPeaks; ++i)
        if (peakVal[i] >= threshold) { chosen = i; break; }

    // Parabolic interpolation for sub-sample lag precision.
    const int t = peakTau[chosen];
    double refinedTau = t;
    double refinedVal = peakVal[chosen];
    if (t > 1 && t < tauMax)
    {
        const double y0 = nsdf[(size_t) (t - 1)], y1 = nsdf[(size_t) t], y2 = nsdf[(size_t) (t + 1)];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
        {
            const double delta = 0.5 * (y0 - y2) / denom;
            if (std::abs (delta) < 1.0)
            {
                refinedTau = t + delta;
                refinedVal = y1 - 0.25 * (y0 - y2) * delta;
            }
        }
    }

    result.frequency = sr / refinedTau;
    result.clarity = juce::jlimit (0.0, 1.0, refinedVal);
    return result;
}
} // namespace ff
