#include "BuiltinPlugins.h"
#include "Biquad.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstring>

namespace ff::fx::builtin
{
namespace
{
namespace col
{
    const juce::Colour bg { 0xff141619 }, panel { 0xff1c1f24 }, well { 0xff0e1013 }, outline { 0xff323840 }, text { 0xffdde1e6 },
        dim { 0xff8b939d }, accent { 0xfff0a030 }, good { 0xff46c38a }, info { 0xff4c9bf5 }, bad { 0xffe5484d };
}

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using APVTS = juce::AudioProcessorValueTreeState;

std::unique_ptr<juce::AudioParameterFloat> param (const char* id, const char* name, float lo, float hi, float def,
                                                  const char* unit, float centre = 0.0f, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    const juce::String u (unit);
    auto toText = [u] (float v, int) {
        if (u == "Hz") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        if (u == "ms") return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (v, v < 10.0f ? 1 : 0) + " ms";
        if (u == "s") return juce::String (v, 2) + " s";
        if (u == "dB") return juce::String (v, 1) + " dB";
        if (u == "%") return juce::String (juce::roundToInt (v)) + " %";
        if (u == ":1") return juce::String (v, 1) + ":1";
        return juce::String (v, 2) + (u.isEmpty() ? juce::String() : " " + u);
    };
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                       juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (toText));
}

// =====================================================================================================  base

class Base : public juce::AudioPluginInstance
{
public:
    Base (const Info& i, Layout layout)
        : juce::AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo())
                                                      .withOutput ("Output", juce::AudioChannelSet::stereo())),
          info (i), state (*this, nullptr, "STATE", std::move (layout)) {}

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = describe (info); }
    const juce::String getName() const override { return info.name; }
    void releaseResources() override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto x = state.copyState().createXml()) copyXmlToBinary (*x, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size); x != nullptr && x->hasTagName (state.state.getType()))
            state.replaceState (juce::ValueTree::fromXml (*x));
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = b.getNumSamples();
        const int ch = juce::jmin (b.getNumChannels(), getTotalNumOutputChannels());
        if (n == 0 || ch == 0) return;
        float* L = b.getWritePointer (0);
        float* R = ch > 1 ? b.getWritePointer (1) : nullptr;
        for (int i = getTotalNumInputChannels(); i < ch; ++i) b.clear (i, 0, n);
        process (L, R, n);
    }

    /** R is nullptr when mono. */
    virtual void process (float* L, float* R, int n) noexcept = 0;
    /** Optional picture above the knobs (EQ curve, gain reduction...). */
    virtual std::unique_ptr<juce::Component> createDisplay() { return nullptr; }
    virtual int displayHeight() const { return 0; }

    float p (const char* id) const noexcept { return state.getRawParameterValue (id)->load (std::memory_order_relaxed); }
    double sampleRate() const noexcept { return juce::jmax (8000.0, getSampleRate()); }

    const Info& info;
    APVTS state;
};

// =====================================================================================================  editor

class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor (Base& b) : juce::AudioProcessorEditor (b), proc (b)
    {
        display = b.createDisplay();
        if (display != nullptr) addAndMakeVisible (*display);
        title.setText (b.info.name, juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, col::text);
        addAndMakeVisible (title);
        sub.setText (b.info.description, juce::dontSendNotification);
        sub.setFont (juce::Font (juce::FontOptions (12.0f)));
        sub.setColour (juce::Label::textColourId, col::dim);
        addAndMakeVisible (sub);

        for (auto* p : b.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr) continue;
            auto k = std::make_unique<Knob>();
            k->label.setText (rp->getName (40), juce::dontSendNotification);
            k->label.setJustificationType (juce::Justification::centred);
            k->label.setFont (juce::Font (juce::FontOptions (11.5f)));
            k->label.setColour (juce::Label::textColourId, col::dim);
            if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (rp))
            {
                k->combo.addItemList (cp->choices, 1);
                k->comboAttach = std::make_unique<juce::ComboBoxParameterAttachment> (*cp, k->combo);
                addAndMakeVisible (k->combo);
                addAndMakeVisible (k->label);
            }
            else if (auto* bp = dynamic_cast<juce::AudioParameterBool*> (rp))
            {
                k->toggle.setButtonText (rp->getName (40));
                k->toggle.setClickingTogglesState (true);
                k->toggle.setColour (juce::TextButton::buttonOnColourId, col::accent);
                k->buttonAttach = std::make_unique<juce::ButtonParameterAttachment> (*bp, k->toggle);
                addAndMakeVisible (k->toggle);
            }
            else
            {
                k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
                k->slider.setColour (juce::Slider::rotarySliderFillColourId, col::accent);
                k->attach = std::make_unique<juce::SliderParameterAttachment> (*rp, k->slider);
                k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
                addAndMakeVisible (k->slider);
                addAndMakeVisible (k->label);
            }
            knobs.push_back (std::move (k));
        }
        const int cols = juce::jmin (7, (int) knobs.size());
        const int rows = ((int) knobs.size() + cols - 1) / juce::jmax (1, cols);
        setSize (juce::jmax (520, cols * 88 + 24), 56 + b.displayHeight() + (b.displayHeight() > 0 ? 10 : 0) + rows * 104 + 12);
    }

    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        auto top = r.removeFromTop (40);
        title.setBounds (top.removeFromTop (22));
        sub.setBounds (top);
        if (display != nullptr)
        {
            display->setBounds (r.removeFromTop (proc.displayHeight()));
            r.removeFromTop (10);
        }
        const int cols = juce::jmin (7, (int) knobs.size());
        const int w = r.getWidth() / juce::jmax (1, cols);
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const int c = (int) i % cols, row = (int) i / cols;
            auto cell = juce::Rectangle<int> (r.getX() + c * w, r.getY() + row * 104, w, 100).reduced (4, 0);
            auto& k = *knobs[i];
            if (k.toggle.isVisible()) { k.toggle.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 28)); continue; }
            if (k.combo.isVisible())
            {
                k.label.setBounds (cell.removeFromTop (16));
                k.combo.setBounds (cell.withSizeKeepingCentre (cell.getWidth(), 26));
                continue;
            }
            k.label.setBounds (cell.removeFromTop (16));
            k.slider.setBounds (cell);
        }
    }

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        juce::TextButton toggle;
        juce::ComboBox combo;
        std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttach;
        std::unique_ptr<juce::SliderParameterAttachment> attach;
        std::unique_ptr<juce::ButtonParameterAttachment> buttonAttach;
    };
    Base& proc;
    juce::Label title, sub;
    std::unique_ptr<juce::Component> display;
    std::vector<std::unique_ptr<Knob>> knobs;
};

juce::AudioProcessorEditor* Base::createEditor() { return new Editor (*this); }

/** Sets a parameter from the UI in its own units, as one gesture step. */
void setParam (APVTS& s, const char* id, float value)
{
    if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (value));
}
void beginGesture (APVTS& s, const char* id) { if (auto* p = s.getParameter (id)) p->beginChangeGesture(); }
void endGesture (APVTS& s, const char* id) { if (auto* p = s.getParameter (id)) p->endChangeGesture(); }

// =====================================================================================================  EQ

struct EqSettings
{
    float hp, lowF, lowG, m1F, m1G, m1Q, m2F, m2G, m2Q, highF, highG, lp, out;
    bool operator== (const EqSettings& o) const { return std::memcmp (this, &o, sizeof (*this)) == 0; }
};
constexpr int eqBands = 6;   // hp, low shelf, mid 1, mid 2, high shelf, lp
constexpr float eqHpOff = 10.0f, eqLpOff = 22000.0f;

/** Configures the six filters; returns which are doing something. */
std::array<bool, eqBands> configureEq (std::array<Biquad, eqBands>& f, double sr, const EqSettings& s)
{
    std::array<bool, eqBands> on {};
    on[0] = s.hp > eqHpOff + 0.5f;
    on[1] = std::abs (s.lowG) > 0.01f;
    on[2] = std::abs (s.m1G) > 0.01f;
    on[3] = std::abs (s.m2G) > 0.01f;
    on[4] = std::abs (s.highG) > 0.01f;
    on[5] = s.lp < eqLpOff - 1.0f && s.lp < sr * 0.45;
    f[0].set (Biquad::highPass, sr, s.hp, 0.707);
    f[1].set (Biquad::lowShelf, sr, s.lowF, 0.707, s.lowG);
    f[2].set (Biquad::peak, sr, s.m1F, s.m1Q, s.m1G);
    f[3].set (Biquad::peak, sr, s.m2F, s.m2Q, s.m2G);
    f[4].set (Biquad::highShelf, sr, s.highF, 0.707, s.highG);
    f[5].set (Biquad::lowPass, sr, s.lp, 0.707);
    return on;
}

class Eq final : public Base
{
public:
    explicit Eq (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (param ("hp", "Low cut", eqHpOff, 1000.0f, eqHpOff, "Hz", 80.0f));
        l.add (param ("lowF", "Low freq", 30.0f, 500.0f, 100.0f, "Hz", 120.0f));
        l.add (param ("lowG", "Low gain", -18.0f, 18.0f, 0.0f, "dB"));
        l.add (param ("m1F", "Mid 1 freq", 100.0f, 3000.0f, 500.0f, "Hz", 600.0f));
        l.add (param ("m1G", "Mid 1 gain", -18.0f, 18.0f, 0.0f, "dB"));
        l.add (param ("m1Q", "Mid 1 Q", 0.3f, 8.0f, 1.0f, "", 1.5f));
        l.add (param ("m2F", "Mid 2 freq", 500.0f, 12000.0f, 2500.0f, "Hz", 2500.0f));
        l.add (param ("m2G", "Mid 2 gain", -18.0f, 18.0f, 0.0f, "dB"));
        l.add (param ("m2Q", "Mid 2 Q", 0.3f, 8.0f, 1.0f, "", 1.5f));
        l.add (param ("highF", "High freq", 1500.0f, 18000.0f, 8000.0f, "Hz", 6000.0f));
        l.add (param ("highG", "High gain", -18.0f, 18.0f, 0.0f, "dB"));
        l.add (param ("lp", "High cut", 1000.0f, eqLpOff, eqLpOff, "Hz", 8000.0f));
        l.add (param ("out", "Output", -18.0f, 18.0f, 0.0f, "dB"));
        return l;
    }
    EqSettings settings() const
    {
        return { p ("hp"), p ("lowF"), p ("lowG"), p ("m1F"), p ("m1G"), p ("m1Q"), p ("m2F"), p ("m2G"), p ("m2Q"),
                 p ("highF"), p ("highG"), p ("lp"), p ("out") };
    }
    void prepareToPlay (double, int) override
    {
        for (auto& c : filters) for (auto& f : c) f.reset();
        last.hp = -1.0f;
        outGain.reset (sampleRate(), 0.03);
        outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("out")));
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const auto s = settings();
        if (! (s == last))
        {
            for (auto& c : filters)
            {
                std::array<Biquad, eqBands> fresh;
                active = configureEq (fresh, sampleRate(), s);
                for (int b = 0; b < eqBands; ++b) { fresh[(size_t) b].z1 = c[(size_t) b].z1; fresh[(size_t) b].z2 = c[(size_t) b].z2; }
                c = fresh;
            }
            last = s;
        }
        float* chans[2] = { L, R };
        for (int ch = 0; ch < (R != nullptr ? 2 : 1); ++ch)
            for (int b = 0; b < eqBands; ++b)
                if (active[(size_t) b]) filters[(size_t) ch][(size_t) b].process (chans[ch], n);
        outGain.setTargetValue (juce::Decibels::decibelsToGain (s.out));
        for (int i = 0; i < n; ++i)
        {
            const float g = outGain.getNextValue();
            L[i] *= g;
            if (R != nullptr) R[i] *= g;
        }
        if (analyserWanted.load (std::memory_order_relaxed))   // feed the spectrum display (only while it is open)
        {
            int s1, n1, s2, n2;
            scope.prepareToWrite (n, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) scopeBuf[(size_t) (s1 + i)] = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            for (int i = 0; i < n2; ++i) scopeBuf[(size_t) (s2 + i)] = R != nullptr ? 0.5f * (L[n1 + i] + R[n1 + i]) : L[n1 + i];
            scope.finishedWrite (n1 + n2);
        }
    }
    // spectrum feed (audio thread writes, display reads)
    static constexpr int scopeSize = 16384;
    juce::AbstractFifo scope { scopeSize };
    std::vector<float> scopeBuf = std::vector<float> ((size_t) scopeSize, 0.0f);
    std::atomic<bool> analyserWanted { false };
    std::unique_ptr<juce::Component> createDisplay() override;
    int displayHeight() const override { return 240; }
private:
    std::array<std::array<Biquad, eqBands>, 2> filters;
    std::array<bool, eqBands> active {};
    EqSettings last {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain;
};

/** Response curve with a draggable node per band (drag: frequency / gain; mouse wheel: Q; double-click: reset gain). */
class EqDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit EqDisplay (Eq& e) : eq (e)
    {
        eq.analyserWanted = true;
        startTimerHz (30);
    }
    ~EqDisplay() override { eq.analyserWanted = false; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 6.0f);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xFor (f);
            g.setColour (col::outline.withAlpha (0.6f));
            g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
            g.setColour (col::dim);
            g.drawText (f >= 1000.0f ? juce::String (juce::roundToInt (f / 1000.0f)) + "k" : juce::String (juce::roundToInt (f)),
                        juce::Rectangle<float> (x + 2, r.getBottom() - 14, 30, 12), juce::Justification::centredLeft);
        }
        for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = yFor (db);
            g.setColour (col::outline.withAlpha (db == 0.0f ? 1.0f : 0.5f));
            g.drawHorizontalLine (juce::roundToInt (y), r.getX(), r.getRight());
            g.setColour (col::dim);
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (r.getX() + 3, y - 12, 30, 12), juce::Justification::centredLeft);
        }
        // spectrum of what comes out (behind the curve)
        if (hasSpectrum)
        {
            juce::Path spec;
            const double sr0 = juce::jmax (8000.0, eq.getSampleRate());
            spec.startNewSubPath (0.0f, (float) getHeight());
            for (int px = 0; px <= getWidth(); px += 2)
            {
                const double f = freqFor ((float) px);
                const float bin = (float) (f / sr0 * fftSize);
                const int b0 = juce::jlimit (1, fftSize / 2 - 2, (int) bin);
                const float fr = juce::jlimit (0.0f, 1.0f, bin - (float) b0);
                const float db = spectrum[(size_t) b0] + fr * (spectrum[(size_t) b0 + 1] - spectrum[(size_t) b0]);
                const float y = (float) getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f));
                spec.lineTo ((float) px, y);
            }
            spec.lineTo ((float) getWidth(), (float) getHeight());
            spec.closeSubPath();
            g.setColour (col::info.withAlpha (0.22f));
            g.fillPath (spec);
            g.setColour (col::info.withAlpha (0.5f));
            g.strokePath (spec, juce::PathStrokeType (1.0f));
        }
        // curve
        const auto s = eq.settings();
        std::array<Biquad, eqBands> f;
        const double sr = juce::jmax (44100.0, eq.getSampleRate());
        const auto on = configureEq (f, sr, s);
        juce::Path curve;
        for (int px = 0; px <= getWidth(); px += 2)
        {
            const double freq = freqFor ((float) px);
            double mag = juce::Decibels::decibelsToGain ((double) s.out);
            for (int b = 0; b < eqBands; ++b) if (on[(size_t) b]) mag *= f[(size_t) b].magnitude (freq, sr);
            const float y = yFor ((float) juce::Decibels::gainToDecibels (mag, -60.0));
            if (px == 0) curve.startNewSubPath ((float) px, y); else curve.lineTo ((float) px, y);
        }
        juce::Path fill (curve);
        fill.lineTo ((float) getWidth(), yFor (0.0f));
        fill.lineTo (0.0f, yFor (0.0f));
        fill.closeSubPath();
        g.setColour (col::accent.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (col::accent);
        g.strokePath (curve, juce::PathStrokeType (2.0f));
        // nodes
        for (int b = 0; b < eqBands; ++b)
        {
            const auto c = nodePos (b, s);
            const bool hot = b == dragging || b == hover;
            g.setColour ((b == 0 || b == 5 ? col::info : col::good).withAlpha (hot ? 1.0f : 0.8f));
            g.fillEllipse (c.x - 6, c.y - 6, 12, 12);
            g.setColour (col::bg);
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (juce::String (b == 0 ? "L" : b == 5 ? "H" : juce::String (b)), juce::Rectangle<float> (c.x - 6, c.y - 6, 12, 12), juce::Justification::centred);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { const int h = nodeAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = nodeAt (e.position);
        if (dragging < 0) return;
        for (auto* id : ids (dragging)) if (id != nullptr) beginGesture (eq.state, id);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto id = ids (dragging);
        setParam (eq.state, id[0], freqFor (e.position.x));
        if (id[1] != nullptr) setParam (eq.state, id[1], juce::jlimit (-18.0f, 18.0f, dbFor (e.position.y)));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0) for (auto* id : ids (dragging)) if (id != nullptr) endGesture (eq.state, id);
        dragging = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = nodeAt (e.position);
        if (b < 0) return;
        const auto id = ids (b);
        if (b == 0) setParam (eq.state, id[0], eqHpOff);
        else if (b == 5) setParam (eq.state, id[0], eqLpOff);
        else setParam (eq.state, id[1], 0.0f);
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = nodeAt (e.position);
        const char* q = b == 2 ? "m1Q" : b == 3 ? "m2Q" : nullptr;
        if (q == nullptr) return;
        setParam (eq.state, q, juce::jlimit (0.3f, 8.0f, eq.p (q) * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f)));
    }

private:
    Eq& eq;
    int dragging = -1, hover = -1;
    EqSettings shown {};

    // ---- spectrum analyser ----
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> history = std::vector<float> ((size_t) fftSize, 0.0f), work = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> spectrum = std::vector<float> ((size_t) fftSize / 2, -120.0f);
    bool hasSpectrum = false;
    int quietTicks = 0;

    void pullSamples()
    {
        int s1, n1, s2, n2;
        const int ready = eq.scope.getNumReady();
        if (ready == 0) { if (++quietTicks > 15) hasSpectrum = false; return; }
        quietTicks = 0;
        eq.scope.prepareToRead (ready, s1, n1, s2, n2);
        auto push = [this] (const float* d, int n) {
            if (n >= fftSize) { std::copy (d + n - fftSize, d + n, history.begin()); return; }
            std::move (history.begin() + n, history.end(), history.begin());
            std::copy (d, d + n, history.end() - n);
        };
        push (eq.scopeBuf.data() + s1, n1);
        if (n2 > 0) push (eq.scopeBuf.data() + s2, n2);
        eq.scope.finishedRead (n1 + n2);

        std::copy (history.begin(), history.end(), work.begin());
        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (work.data());
        const float norm = 4.0f / (float) fftSize;   // a full-scale sine reads about 0 dB
        for (int b = 0; b < fftSize / 2; ++b)
        {
            const float db = juce::Decibels::gainToDecibels (work[(size_t) b] * norm, -120.0f);
            auto& v = spectrum[(size_t) b];
            v = db > v ? 0.6f * db + 0.4f * v : juce::jmax (db, v - 1.5f);   // quick rise, slow fall
        }
        hasSpectrum = true;
    }

    void timerCallback() override
    {
        pullSamples();
        shown = eq.settings();
        repaint();
    }

    static constexpr float fLo = 20.0f, fHi = 20000.0f, dbRange = 20.0f;
    float xFor (float f) const { return (float) getWidth() * std::log (f / fLo) / std::log (fHi / fLo); }
    float freqFor (float x) const { return juce::jlimit (fLo, fHi, fLo * std::pow (fHi / fLo, x / (float) juce::jmax (1, getWidth()))); }
    float yFor (float db) const { return (float) getHeight() * 0.5f - db / dbRange * (float) getHeight() * 0.5f; }
    float dbFor (float y) const { return ((float) getHeight() * 0.5f - y) / ((float) getHeight() * 0.5f) * dbRange; }

    std::array<const char*, 2> ids (int b) const
    {
        switch (b)
        {
            case 0: return { "hp", nullptr };
            case 1: return { "lowF", "lowG" };
            case 2: return { "m1F", "m1G" };
            case 3: return { "m2F", "m2G" };
            case 4: return { "highF", "highG" };
            default: return { "lp", nullptr };
        }
    }
    juce::Point<float> nodePos (int b, const EqSettings& s) const
    {
        switch (b)
        {
            case 0: return { xFor (juce::jmax (fLo, s.hp)), yFor (0.0f) };
            case 1: return { xFor (s.lowF), yFor (s.lowG) };
            case 2: return { xFor (s.m1F), yFor (s.m1G) };
            case 3: return { xFor (s.m2F), yFor (s.m2G) };
            case 4: return { xFor (s.highF), yFor (s.highG) };
            default: return { xFor (juce::jmin (fHi, s.lp)), yFor (0.0f) };
        }
    }
    int nodeAt (juce::Point<float> p) const
    {
        const auto s = eq.settings();
        int best = -1;
        float bestD = 14.0f;
        for (int b = 0; b < eqBands; ++b)
            if (const float d = nodePos (b, s).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
        return best;
    }
};

std::unique_ptr<juce::Component> Eq::createDisplay() { return std::make_unique<EqDisplay> (*this); }

// =====================================================================================================  compressor

class Compressor final : public Base
{
public:
    explicit Compressor (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (param ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, "dB"));
        l.add (param ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, ":1", 4.0f));
        l.add (param ("attack", "Attack", 0.1f, 100.0f, 10.0f, "ms", 10.0f));
        l.add (param ("release", "Release", 10.0f, 1000.0f, 120.0f, "ms", 150.0f));
        l.add (param ("knee", "Knee", 0.0f, 12.0f, 6.0f, "dB"));
        l.add (param ("makeup", "Makeup", 0.0f, 24.0f, 0.0f, "dB"));
        l.add (param ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%"));
        return l;
    }
    void prepareToPlay (double, int) override { env = 0.0f; grDb = 0.0f; }
    float gainComputer (float levelDb) const noexcept
    {
        const float t = p ("threshold"), ratio = p ("ratio"), knee = p ("knee");
        const float over = levelDb - t;
        if (knee > 0.0f && std::abs (over) <= knee * 0.5f)
            return (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return over > 0.0f ? over * (1.0f / ratio - 1.0f) : 0.0f;
    }
    void process (float* L, float* R, int n) noexcept override
    {
        const double sr = sampleRate();
        const float atk = std::exp (-1.0f / (float) (sr * p ("attack") * 0.001));
        const float rel = std::exp (-1.0f / (float) (sr * p ("release") * 0.001));
        const float makeup = juce::Decibels::decibelsToGain (p ("makeup")), mix = p ("mix") * 0.01f;
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = juce::jmax (std::abs (L[i]), R != nullptr ? std::abs (R[i]) : 0.0f);
            const float level = juce::Decibels::gainToDecibels (x, -120.0f);
            const float target = -gainComputer (level);   // reduction in dB, >= 0
            env = target > env ? atk * env + (1.0f - atk) * target : rel * env + (1.0f - rel) * target;
            const float g = juce::Decibels::decibelsToGain (-env) * makeup;
            const float gm = mix * g + (1.0f - mix);
            L[i] *= gm;
            if (R != nullptr) R[i] *= gm;
            maxGr = juce::jmax (maxGr, env);
        }
        grDb.store (juce::jmax (maxGr, grDb.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    }
    std::unique_ptr<juce::Component> createDisplay() override;
    int displayHeight() const override { return 150; }
    std::atomic<float> grDb { 0.0f };   // largest gain reduction since the display took it
private:
    float env = 0.0f;
};

/** Transfer curve and a gain-reduction meter. */
class CompDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit CompDisplay (Compressor& c) : comp (c) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 6.0f);
        auto curveArea = r.removeFromLeft (r.getHeight()).reduced (10.0f);
        g.setColour (col::outline);
        g.drawRect (curveArea);
        g.drawLine (curveArea.getX(), curveArea.getBottom(), curveArea.getRight(), curveArea.getY(), 1.0f);
        juce::Path p;
        for (int i = 0; i <= 60; ++i)
        {
            const float in = -60.0f + (float) i;
            const float out = in + comp.gainComputer (in);
            const float x = curveArea.getX() + (in + 60.0f) / 60.0f * curveArea.getWidth();
            const float y = curveArea.getBottom() - juce::jlimit (0.0f, 1.0f, (out + 60.0f) / 60.0f) * curveArea.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col::accent);
        g.strokePath (p, juce::PathStrokeType (2.0f));
        auto meter = r.reduced (16.0f, 18.0f);
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Gain reduction  " + juce::String (shownGr, 1) + " dB", meter.removeFromTop (18.0f), juce::Justification::centredLeft);
        meter.removeFromTop (6.0f);
        auto bar = meter.removeFromTop (16.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::bad.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, shownGr / 24.0f)), 3.0f);
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (int db : { 0, 6, 12, 18, 24 })
            g.drawText (juce::String (db), juce::Rectangle<float> (bar.getX() + bar.getWidth() * (float) db / 24.0f - 10.0f, bar.getBottom() + 2.0f, 20.0f, 12.0f),
                        juce::Justification::centred);
    }
private:
    Compressor& comp;
    float shownGr = 0.0f;
    void timerCallback() override
    {
        const float gr = comp.grDb.exchange (0.0f);
        shownGr = gr > shownGr ? gr : juce::jmax (gr, shownGr - 1.0f);
        repaint();
    }
};

std::unique_ptr<juce::Component> Compressor::createDisplay() { return std::make_unique<CompDisplay> (*this); }

// =====================================================================================================  limiter

/** Look-ahead brick-wall limiter: the gain each sample needs is held over the look-ahead window and smoothed with
    a box filter of the same length, so the gain is already down when a peak leaves the delay; a final clip at the
    ceiling catches anything left (there is nothing left in practice). */
class Limiter final : public Base
{
public:
    explicit Limiter (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (param ("gain", "Input gain", 0.0f, 24.0f, 0.0f, "dB"));
        l.add (param ("ceiling", "Ceiling", -12.0f, 0.0f, -1.0f, "dB"));
        l.add (param ("release", "Release", 1.0f, 1000.0f, 80.0f, "ms", 100.0f));
        return l;
    }
    void prepareToPlay (double, int) override
    {
        look = juce::jmax (1, (int) std::round (sampleRate() * 0.0015));
        setLatencySamples (look);
        for (auto& d : delay) d.assign ((size_t) look + 1, 0.0f);
        held.assign ((size_t) look + 1, 1.0f);
        dqVal.assign ((size_t) look + 2, 1.0f);
        dqIdx.assign ((size_t) look + 2, 0);
        dqHead = dqTail = 0;
        box.assign ((size_t) look, 1.0f);
        boxSum = (double) look;
        pos = 0; t = 0; env = 1.0f;
        inGain.reset (sampleRate(), 0.05);
        inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("gain")));
    }
    void process (float* L, float* R, int n) noexcept override
    {
        if (delay[0].empty()) return;
        const float ceiling = juce::Decibels::decibelsToGain (p ("ceiling"));
        const float rel = 1.0f - std::exp (-1.0f / (float) (sampleRate() * p ("release") * 0.001));
        inGain.setTargetValue (juce::Decibels::decibelsToGain (p ("gain")));
        const int len = look + 1, qcap = (int) dqVal.size();
        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g0 = inGain.getNextValue();
            const float xl = L[i] * g0, xr = R != nullptr ? R[i] * g0 : xl;
            const float pk = juce::jmax (std::abs (xl), std::abs (xr));
            const float need = pk > ceiling ? ceiling / pk : 1.0f;
            // sliding minimum of `need` over the last look + 1 samples (monotonic queue)
            while (dqHead != dqTail && dqVal[(size_t) ((dqTail - 1 + qcap) % qcap)] >= need) dqTail = (dqTail - 1 + qcap) % qcap;
            dqVal[(size_t) dqTail] = need;
            dqIdx[(size_t) dqTail] = t;
            dqTail = (dqTail + 1) % qcap;
            while (dqIdx[(size_t) dqHead] <= t - len) dqHead = (dqHead + 1) % qcap;
            const float hold = dqVal[(size_t) dqHead];
            // box filter over `look` samples
            const int bi = (int) (t % look);
            boxSum += (double) hold - (double) box[(size_t) bi];
            box[(size_t) bi] = hold;
            const float target = (float) (boxSum / look);
            env = target < env ? target : env + rel * (target - env);
            // delay the audio by `look`
            const float dl = delay[0][(size_t) pos], dr = delay[1][(size_t) pos];
            delay[0][(size_t) pos] = xl;
            delay[1][(size_t) pos] = xr;
            pos = (pos + 1) % look;
            L[i] = juce::jlimit (-ceiling, ceiling, dl * env);
            if (R != nullptr) R[i] = juce::jlimit (-ceiling, ceiling, dr * env);
            maxGr = juce::jmax (maxGr, -juce::Decibels::gainToDecibels (env, -60.0f));
            ++t;
        }
        grDb.store (juce::jmax (maxGr, grDb.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    }
    std::unique_ptr<juce::Component> createDisplay() override;
    int displayHeight() const override { return 70; }
    std::atomic<float> grDb { 0.0f };
private:
    int look = 1, pos = 0, dqHead = 0, dqTail = 0;
    juce::int64 t = 0;
    std::array<std::vector<float>, 2> delay;
    std::vector<float> held, dqVal, box;
    std::vector<juce::int64> dqIdx;
    double boxSum = 0.0;
    float env = 1.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain;
};

class GrMeter final : public juce::Component, private juce::Timer
{
public:
    explicit GrMeter (std::atomic<float>& g) : gr (g) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::well);
        g.fillRoundedRectangle (r, 6.0f);
        auto m = r.reduced (16.0f, 10.0f);
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Gain reduction  " + juce::String (shown, 1) + " dB", m.removeFromTop (18.0f), juce::Justification::centredLeft);
        m.removeFromTop (6.0f);
        auto bar = m.removeFromTop (14.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::bad.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, shown / 12.0f)), 3.0f);
    }
private:
    std::atomic<float>& gr;
    float shown = 0.0f;
    void timerCallback() override
    {
        const float v = gr.exchange (0.0f);
        shown = v > shown ? v : juce::jmax (v, shown - 0.5f);
        repaint();
    }
};

std::unique_ptr<juce::Component> Limiter::createDisplay() { return std::make_unique<GrMeter> (grDb); }

// =====================================================================================================  delay

class Delay final : public Base
{
public:
    explicit Delay (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (param ("time", "Time", 1.0f, 2000.0f, 380.0f, "ms", 400.0f));
        l.add (param ("feedback", "Feedback", 0.0f, 95.0f, 35.0f, "%"));
        l.add (param ("mix", "Mix", 0.0f, 100.0f, 30.0f, "%"));
        l.add (param ("lowCut", "Low cut", 20.0f, 1000.0f, 80.0f, "Hz", 150.0f));
        l.add (param ("highCut", "High cut", 1000.0f, 20000.0f, 6000.0f, "Hz", 5000.0f));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "pingpong", 1 }, "Ping-pong", false));
        return l;
    }
    void prepareToPlay (double, int) override
    {
        const int len = (int) (sampleRate() * 2.1) + 4;
        for (auto& b : buf) b.assign ((size_t) len, 0.0f);
        write = 0;
        time.reset (sampleRate(), 0.25);
        time.setCurrentAndTargetValue (p ("time"));
        for (auto& f : lo) f.reset();
        for (auto& f : hi) f.reset();
    }
    double getTailLengthSeconds() const override { return 6.0; }
    void process (float* L, float* R, int n) noexcept override
    {
        if (buf[0].empty()) return;
        const double sr = sampleRate();
        const int len = (int) buf[0].size();
        const float fb = p ("feedback") * 0.01f, mix = p ("mix") * 0.01f;
        const bool ping = p ("pingpong") >= 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            lo[(size_t) c].set (Biquad::highPass, sr, p ("lowCut"), 0.707);
            hi[(size_t) c].set (Biquad::lowPass, sr, p ("highCut"), 0.707);
        }
        time.setTargetValue (p ("time"));
        for (int i = 0; i < n; ++i)
        {
            const double d = juce::jlimit (1.0, (double) len - 3.0, (double) time.getNextValue() * 0.001 * sr);
            double rp = (double) write - d;
            if (rp < 0) rp += len;
            const int i0 = (int) rp;
            const float fr = (float) (rp - i0);
            const int i1 = (i0 + 1) % len;
            const float dl = buf[0][(size_t) i0] + fr * (buf[0][(size_t) i1] - buf[0][(size_t) i0]);
            const float dr = buf[1][(size_t) i0] + fr * (buf[1][(size_t) i1] - buf[1][(size_t) i0]);
            const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
            const float wl = hi[0].process (lo[0].process (dl)) * fb;
            const float wr = hi[1].process (lo[1].process (dr)) * fb;
            if (ping)
            {
                buf[0][(size_t) write] = 0.5f * (inL + inR) + wr;   // left repeats feed the right line and back
                buf[1][(size_t) write] = wl;
            }
            else
            {
                buf[0][(size_t) write] = inL + wl;
                buf[1][(size_t) write] = inR + wr;
            }
            write = (write + 1) % len;
            L[i] = inL * (1.0f - mix) + dl * mix;
            if (R != nullptr) R[i] = inR * (1.0f - mix) + dr * mix;
            else L[i] = inL * (1.0f - mix) + 0.5f * (dl + dr) * mix;
        }
    }
private:
    std::array<std::vector<float>, 2> buf;
    int write = 0;
    juce::SmoothedValue<float> time;
    std::array<Biquad, 2> lo, hi;
};

// =====================================================================================================  chorus

/** Classic stereo chorus: one modulated delay per side, LFOs 90 degrees apart, smooth (Hermite) interpolation. */
class Chorus final : public Base
{
public:
    explicit Chorus (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (param ("rate", "Rate", 0.05f, 5.0f, 0.6f, "Hz", 1.0f));
        l.add (param ("depth", "Depth", 0.0f, 100.0f, 40.0f, "%"));
        l.add (param ("delay", "Delay", 3.0f, 25.0f, 8.0f, "ms", 10.0f));
        l.add (param ("feedback", "Feedback", 0.0f, 70.0f, 0.0f, "%"));
        l.add (param ("width", "Width", 0.0f, 100.0f, 100.0f, "%"));
        l.add (param ("mix", "Mix", 0.0f, 100.0f, 50.0f, "%"));
        return l;
    }
    void prepareToPlay (double, int) override
    {
        const int len = (int) (sampleRate() * 0.06) + 8;
        for (auto& b : buf) b.assign ((size_t) len, 0.0f);
        write = 0;
        phase = 0.0;
        fbL = fbR = 0.0f;
        for (auto* v : { &delayMs, &depthMs }) v->reset (sampleRate(), 0.1);
        delayMs.setCurrentAndTargetValue (p ("delay"));
        depthMs.setCurrentAndTargetValue (depthFor (p ("delay"), p ("depth")));
    }
    void process (float* L, float* R, int n) noexcept override
    {
        if (buf[0].empty()) return;
        const double sr = sampleRate();
        const int len = (int) buf[0].size();
        const float fb = p ("feedback") * 0.01f, mix = p ("mix") * 0.01f, width = p ("width") * 0.01f;
        const double inc = juce::MathConstants<double>::twoPi * p ("rate") / sr;
        delayMs.setTargetValue (p ("delay"));
        depthMs.setTargetValue (depthFor (p ("delay"), p ("depth")));
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
            const float base = delayMs.getNextValue(), depth = depthMs.getNextValue();
            const float modL = (float) std::sin (phase), modR = (float) std::sin (phase + juce::MathConstants<double>::halfPi * width * 2.0);
            phase += inc;
            if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
            buf[0][(size_t) write] = inL + fbL * fb;
            buf[1][(size_t) write] = inR + fbR * fb;
            const float wl = read (buf[0], len, (base + depth * modL) * 0.001 * sr);
            const float wr = read (buf[1], len, (base + depth * modR) * 0.001 * sr);
            fbL = wl; fbR = wr;
            write = (write + 1) % len;
            L[i] = inL * (1.0f - 0.5f * mix) + wl * mix * 0.7f;
            if (R != nullptr) R[i] = inR * (1.0f - 0.5f * mix) + wr * mix * 0.7f;
            else L[i] = inL * (1.0f - 0.5f * mix) + 0.35f * (wl + wr) * mix;
        }
    }
private:
    std::array<std::vector<float>, 2> buf;
    int write = 0;
    double phase = 0.0;
    float fbL = 0.0f, fbR = 0.0f;
    juce::SmoothedValue<float> delayMs, depthMs;

    static float depthFor (float delay, float depthPercent) { return juce::jmin (delay - 1.0f, 6.0f) * depthPercent * 0.01f; }
    float read (const std::vector<float>& b, int len, double delaySamples) const noexcept
    {
        double rp = (double) write - delaySamples;
        while (rp < 0) rp += len;
        const int i1 = (int) rp;
        const float f = (float) (rp - i1);
        auto at = [&] (int k) { return b[(size_t) ((k % len + len) % len)]; };
        const float xm1 = at (i1 - 1), x0 = at (i1), x1 = at (i1 + 1), x2 = at (i1 + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

// =====================================================================================================  reverb

/** Feedback delay network: pre-delay, input diffusion, 8 damped, gently modulated lines mixed by a Householder
    matrix. The type sets the space: line lengths, diffusion, brightness, modulation, and for Spring a chirping
    dispersion chain. */
class Reverb final : public Base
{
public:
    static constexpr const char* typeNames[] = { "Room", "Hall", "Plate", "Chamber", "Spring", "Ambience", "Cathedral" };
    struct Type { float size, decay, diffusion, damping, mod, spread; bool spring; };
    static constexpr Type types[] = {
        { 0.55f, 0.8f, 0.60f, 0.10f, 1.0f, 1.0f, false },   // room
        { 1.25f, 1.2f, 0.70f, 0.05f, 6.0f, 1.0f, false },   // hall
        { 0.85f, 1.0f, 0.78f, -0.25f, 3.0f, 1.0f, false },  // plate: dense and bright
        { 0.80f, 1.0f, 0.65f, 0.15f, 2.0f, 0.8f, false },   // chamber
        { 0.40f, 1.0f, 0.50f, 0.20f, 0.5f, 0.6f, true },    // spring
        { 0.35f, 0.35f, 0.55f, 0.10f, 1.0f, 1.0f, false },  // ambience: short and close
        { 1.90f, 1.6f, 0.72f, 0.20f, 8.0f, 1.0f, false },   // cathedral
    };
    static constexpr int numTypes = (int) std::size (typeNames);

    explicit Reverb (const Info& i) : Base (i, layout()) {}
    static Layout layout()
    {
        Layout l;
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "type", 1 }, "Type",
                                                              juce::StringArray (typeNames, numTypes), 1));
        l.add (param ("size", "Size", 0.0f, 100.0f, 50.0f, "%"));
        l.add (param ("decay", "Decay", 0.2f, 12.0f, 2.2f, "s", 2.0f));
        l.add (param ("damping", "Damping", 0.0f, 100.0f, 40.0f, "%"));
        l.add (param ("predelay", "Pre-delay", 0.0f, 200.0f, 15.0f, "ms", 40.0f));
        l.add (param ("lowCut", "Low cut", 20.0f, 800.0f, 120.0f, "Hz", 150.0f));
        l.add (param ("width", "Width", 0.0f, 100.0f, 100.0f, "%"));
        l.add (param ("mix", "Mix", 0.0f, 100.0f, 25.0f, "%"));
        return l;
    }
    double getTailLengthSeconds() const override { return 18.0; }
    void prepareToPlay (double, int) override
    {
        const double sr = sampleRate();
        for (auto& l : lines) l.assign ((size_t) (sr * 0.45) + 16, 0.0f);
        for (size_t k = 0; k < diff.size(); ++k) diff[k].assign ((size_t) (sr * diffMs[k] * 0.001) + 2, 0.0f);
        pre.assign ((size_t) (sr * 0.21) + 2, 0.0f);
        std::fill (pos.begin(), pos.end(), 0);
        std::fill (dpos.begin(), dpos.end(), 0);
        std::fill (lp.begin(), lp.end(), 0.0f);
        for (auto& c : chirp) c = {};
        prePos = 0;
        lfo = 0.0;
        hp.reset();
        springLp.reset();
        lastType = -1;
    }
    void process (float* L, float* R, int n) noexcept override
    {
        if (lines[0].empty()) return;
        const double sr = sampleRate();
        const int typeIndex = juce::jlimit (0, numTypes - 1, juce::roundToInt (p ("type")));
        const auto& ty = types[typeIndex];
        if (typeIndex != lastType)   // a different space: start the tail fresh
        {
            for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
            std::fill (lp.begin(), lp.end(), 0.0f);
            lastType = typeIndex;
        }
        const float size = ty.size * (0.5f + 1.5f * p ("size") * 0.01f);
        const float decay = juce::jmax (0.1f, p ("decay") * ty.decay), mix = p ("mix") * 0.01f, width = p ("width") * 0.01f * ty.spread;
        const float damp = juce::jlimit (0.0f, 0.97f, 0.05f + 0.9f * p ("damping") * 0.01f + ty.damping);
        const float dg = ty.diffusion;
        const int preLen = (int) pre.size();
        const int preD = juce::jlimit (0, preLen - 1, (int) (p ("predelay") * 0.001 * sr));
        hp.set (Biquad::highPass, sr, ty.spring ? juce::jmax (300.0f, p ("lowCut")) : p ("lowCut"), 0.707);
        springLp.set (Biquad::lowPass, sr, 4200.0, 0.8);
        const double modDepth = ty.mod * sr / 48000.0;
        const double lfoInc = juce::MathConstants<double>::twoPi * 0.7 / sr;
        std::array<double, numLines> len {};
        std::array<float, numLines> g {};
        for (int k = 0; k < numLines; ++k)
        {
            const int maxLen = (int) lines[(size_t) k].size() - 4 - (int) modDepth;
            len[(size_t) k] = juce::jlimit (8.0, (double) maxLen, lineMs[(size_t) k] * size * 0.001 * sr);
            g[(size_t) k] = std::pow (10.0f, -3.0f * (float) len[(size_t) k] / (decay * (float) sr));
        }
        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R != nullptr ? R[i] : L[i];
            pre[(size_t) prePos] = 0.5f * (inL + inR);
            int rp = prePos - preD;
            if (rp < 0) rp += preLen;
            float x = hp.process (pre[(size_t) rp]);
            prePos = (prePos + 1) % preLen;
            for (size_t k = 0; k < diff.size(); ++k)   // input diffusion
            {
                auto& d = diff[k];
                const float dv = d[(size_t) dpos[k]];
                const float y = -dg * x + dv;
                d[(size_t) dpos[k]] = x + dg * y;
                dpos[k] = (dpos[k] + 1) % (int) d.size();
                x = y;
            }
            lfo += lfoInc;
            if (lfo > juce::MathConstants<double>::twoPi) lfo -= juce::MathConstants<double>::twoPi;
            std::array<float, numLines> out {};
            float sum = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                auto& line = lines[(size_t) k];
                const int size_ = (int) line.size();
                const double m = modDepth * std::sin (lfo + k * 0.785398);   // each line drifts a little: no metallic ring
                double r = pos[(size_t) k] - (len[(size_t) k] + m);
                while (r < 0) r += size_;
                const int r0 = (int) r;
                const float f = (float) (r - r0);
                const float v = line[(size_t) r0] + f * (line[(size_t) ((r0 + 1) % size_)] - line[(size_t) r0]);
                auto& st = lp[(size_t) k];
                st += (1.0f - damp) * (v - st);
                out[(size_t) k] = st;
                sum += st;
            }
            sum *= 2.0f / (float) numLines;
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < numLines; ++k)
            {
                auto& line = lines[(size_t) k];
                line[(size_t) pos[(size_t) k]] = (out[(size_t) k] - sum) * g[(size_t) k] + x * ((k & 1) ? -0.35f : 0.35f);
                pos[(size_t) k] = (pos[(size_t) k] + 1) % (int) line.size();
                if (k & 1) wr += out[(size_t) k]; else wl += out[(size_t) k];
            }
            wl *= 0.5f; wr *= 0.5f;
            if (ty.spring)   // dispersion: a chain of first-order all-passes makes the springy "drip"
            {
                for (auto& c : chirp)
                {
                    const float yl = -0.62f * wl + c.xl + 0.62f * c.yl;
                    c.xl = wl; c.yl = yl; wl = yl;
                    const float yr = -0.62f * wr + c.xr + 0.62f * c.yr;
                    c.xr = wr; c.yr = yr; wr = yr;
                }
                wl = springLp.process (wl);
                wr = 0.8f * wr + 0.2f * wl;
            }
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
            wl = mid + side;
            wr = mid - side;
            L[i] = inL * (1.0f - mix) + wl * mix;
            if (R != nullptr) R[i] = inR * (1.0f - mix) + wr * mix;
            else L[i] = inL * (1.0f - mix) + mid * mix;
        }
        for (auto& st : lp) if (std::abs (st) < 1.0e-20f) st = 0.0f;
    }
private:
    static constexpr int numLines = 8;
    static constexpr std::array<float, numLines> lineMs { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.7f, 73.1f };
    static constexpr std::array<float, 4> diffMs { 4.7f, 3.6f, 12.7f, 9.3f };
    std::array<std::vector<float>, numLines> lines;
    std::array<int, numLines> pos {};
    std::array<float, numLines> lp {};
    std::array<std::vector<float>, 4> diff;
    std::array<int, 4> dpos {};
    std::vector<float> pre;
    int prePos = 0, lastType = -1;
    double lfo = 0.0;
    struct ChirpStage { float xl = 0, yl = 0, xr = 0, yr = 0; };
    std::array<ChirpStage, 16> chirp {};
    Biquad hp, springLp;
};
} // namespace

// =====================================================================================================  registry

const std::vector<Info>& all()
{
    static const std::vector<Info> list {
        { "fretforge:eq", "EQ", "EQ", "Six-band EQ: low / high cut, shelves and two bells. Drag the points, mouse wheel for Q." },
        { "fretforge:comp", "Compressor", "Dynamics", "Smooth compressor with soft knee and parallel mix." },
        { "fretforge:delay", "Delay", "Delay", "Stereo or ping-pong delay with filtered repeats." },
        { "fretforge:chorus", "Chorus", "Modulation", "Stereo chorus." },
        { "fretforge:reverb", "Reverb", "Reverb", "Room to hall reverb. On an FX track, set Mix to 100 %." },
        { "fretforge:limiter", "Limiter", "Dynamics", "Look-ahead brick-wall limiter: nothing gets past the ceiling. Put it last on the master." },
    };
    return list;
}

const Info* find (const juce::String& id)
{
    for (auto& i : all())
        if (id == i.id) return &i;
    return nullptr;
}

juce::PluginDescription describe (const Info& i)
{
    juce::PluginDescription d;
    d.name = i.name;
    d.descriptiveName = i.description;
    d.pluginFormatName = formatName;
    d.category = i.category;
    d.manufacturerName = "FretForge";
    d.version = "1.0";
    d.fileOrIdentifier = i.id;
    d.uniqueId = d.deprecatedUid = (int) juce::String (i.id).hashCode();
    d.isInstrument = false;
    d.numInputChannels = d.numOutputChannels = 2;
    return d;
}

juce::String descriptionXml (const juce::String& id)
{
    const auto* i = find (id);
    if (i == nullptr) return {};
    return describe (*i).createXml()->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

std::unique_ptr<juce::AudioPluginInstance> create (const juce::String& id)
{
    const auto* i = find (id);
    if (i == nullptr) return nullptr;
    const juce::String s (i->id);
    if (s == "fretforge:eq") return std::make_unique<Eq> (*i);
    if (s == "fretforge:comp") return std::make_unique<Compressor> (*i);
    if (s == "fretforge:delay") return std::make_unique<Delay> (*i);
    if (s == "fretforge:chorus") return std::make_unique<Chorus> (*i);
    if (s == "fretforge:reverb") return std::make_unique<Reverb> (*i);
    if (s == "fretforge:limiter") return std::make_unique<Limiter> (*i);
    return nullptr;
}

juce::String stateWith (const juce::String& id, const std::map<juce::String, float>& values)
{
    auto p = create (id);
    auto* b = dynamic_cast<Base*> (p.get());
    if (b == nullptr) return {};
    for (auto& [k, v] : values)
        if (auto* prm = b->state.getParameter (k)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    juce::MemoryBlock mb;
    b->getStateInformation (mb);
    return mb.toBase64Encoding();
}

void Format::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& id)
{
    if (const auto* i = find (id)) results.add (new juce::PluginDescription (describe (*i)));
}

juce::String Format::getNameOfPluginFromIdentifier (const juce::String& id)
{
    const auto* i = find (id);
    return i != nullptr ? juce::String (i->name) : id;
}

juce::StringArray Format::searchPathsForPlugins (const juce::FileSearchPath&, bool, bool)
{
    juce::StringArray ids;
    for (auto& i : all()) ids.add (i.id);
    return ids;
}

void Format::createPluginInstance (const juce::PluginDescription& d, double sr, int bs, PluginCreationCallback cb)
{
    auto p = create (d.fileOrIdentifier);
    if (p == nullptr) { cb (nullptr, "Unknown built-in plugin \"" + d.name + "\"."); return; }
    p->setRateAndBufferSizeDetails (sr, bs);
    cb (std::move (p), {});
}
} // namespace ff::fx::builtin
