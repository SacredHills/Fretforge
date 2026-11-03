#pragma once
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

namespace ff
{
/** Monophonic pitch detector using the McLeod Pitch Method (normalised square
    difference function computed via FFT autocorrelation).

    - Allocates only in prepare(); detect() is allocation-free.
    - Window length adapts to the lowest frequency requested so that a low B on a
      5-string bass (~31 Hz) or F#1 on an 8-string guitar can be tracked.
    - Returns a "clarity" (0..1) that is used as the confidence indicator. */
class PitchDetector
{
public:
    struct Result
    {
        double frequency = 0.0; // Hz, 0 if no pitch found
        double clarity = 0.0;   // 0..1, NSDF peak height (confidence)
        double rms = 0.0;       // linear RMS of the analysed window
    };

    void prepare (double sampleRate, double minFrequency = 27.0, double maxFrequency = 1600.0);

    /** Number of samples detect() expects (power of two). */
    int windowSize() const noexcept { return window; }
    double sampleRate() const noexcept { return sr; }

    /** Analyses exactly windowSize() samples. */
    Result detect (const float* samples) noexcept;

    /** Peak-picking threshold relative to the highest NSDF maximum (MPM "k"). */
    void setPeakThreshold (double k) noexcept { peakK = k; }

private:
    double sr = 48000.0;
    int window = 0;
    int tauMin = 2, tauMax = 0;
    double peakK = 0.93;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftData;  // 2 * fftSize floats (JUCE real-only layout)
    std::vector<float> frame;
    std::vector<double> nsdf;
};
} // namespace ff
