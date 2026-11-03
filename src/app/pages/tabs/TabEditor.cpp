#include "TabEditor.h"
#include "core/MusicTheory.h"

namespace ff
{
namespace tids = tab::ids;

TabEditor::TabEditor (ProjectDocument& d, EngineHost& h) : doc (d), host (h)
{
    setWantsKeyboardFocus (true);
}

void TabEditor::setScore (juce::ValueTree s)
{
    score = s;
    trackIndex = juce::jlimit (0, juce::jmax (0, tab::tracks (score).getNumChildren() - 1), trackIndex);
    cursor = {};
    selActive = false;
    layoutChanged();
}

void TabEditor::setTrackIndex (int i)
{
    trackIndex = juce::jlimit (0, juce::jmax (0, tab::tracks (score).getNumChildren() - 1), i);
    cursorString = juce::jlimit (0, juce::jmax (0, strings() - 1), cursorString);
    layoutChanged();
    changed();
}

juce::ValueTree TabEditor::track() const { return tab::tracks (score).getChild (trackIndex); }
juce::ValueTree TabEditor::barNode (int b) const { return tab::bar (track(), b); }
int TabEditor::barCount() const { return track().isValid() ? track().getNumChildren() : 0; }
int TabEditor::numBeats (int b) const { return barNode (b).getNumChildren(); }
int TabEditor::strings() const { return track().isValid() ? tab::numStrings (track()) : 6; }
juce::ValueTree TabEditor::beatAt (const Pos& p) const { return barNode (p.bar).getChild (p.beat); }

float TabEditor::stringY (int row, int s) const
{
    return (float) (rowY[(size_t) juce::jlimit (0, (int) rowY.size() - 1, row)] + topPad + (strings() - 1 - s) * lineGap);
}

const TabEditor::MeasureLayout* TabEditor::layoutFor (int b) const
{
    for (auto& m : layout)
        if (m.bar == b) return &m;
    return nullptr;
}

// ------------------------------------------------------------------ layout

void TabEditor::layoutChanged()
{
    layout.clear();
    rowY.clear();
    if (! track().isValid() || getWidth() <= 0)
    {
        repaint();
        return;
    }
    const int ns = strings();
    rowHeight = topPad + (ns - 1) * lineGap + rhythmPad + 22;
    const float avail = (float) getWidth() - 14.0f;

    struct Raw { MeasureLayout m; std::vector<float> rel; float relAppend; };
    std::vector<Raw> raw;
    float x = (float) leftPad;
    int row = 0;
    auto meas = tab::measures (score);
    int prevNum = 0, prevDen = 0;
    for (int b = 0; b < barCount(); ++b)
    {
        auto bar = barNode (b);
        auto m = meas.getChild (b);
        const int num = (int) m.getProperty (tids::tsNum, 4), den = (int) m.getProperty (tids::tsDen, 4);
        const bool showSig = num != prevNum || den != prevDen;
        prevNum = num; prevDen = den;
        Raw r;
        float bx = 14.0f + (showSig ? 22.0f : 0.0f);
        for (auto beat : bar)
        {
            const float bw = 22.0f + 16.0f * (float) std::sqrt (tab::beatLengthQ (beat) * 4.0);
            r.rel.push_back (bx + bw * 0.5f);
            bx += bw;
        }
        float w = juce::jmax (90.0f, bx + 26.0f);
        r.relAppend = w - 13.0f;
        if (x + w > avail && x > (float) leftPad)
        {
            ++row;
            x = (float) leftPad;
        }
        r.m.bar = b;
        r.m.row = row;
        r.m.x = x;
        r.m.w = w;
        raw.push_back (r);
        x += w;
    }

    // justify every full row to the available width
    const int rows = row + 1;
    for (int rr = 0; rr < rows - 1; ++rr)
    {
        float total = 0.0f;
        for (auto& r : raw) if (r.m.row == rr) total += r.m.w;
        const float scale = total > 0.0f ? (avail - (float) leftPad) / total : 1.0f;
        float xx = (float) leftPad;
        for (auto& r : raw)
            if (r.m.row == rr)
            {
                r.m.x = xx;
                r.m.w *= scale;
                for (auto& v : r.rel) v *= scale;
                r.relAppend *= scale;
                xx += r.m.w;
            }
    }
    for (auto& r : raw)
    {
        for (auto v : r.rel) r.m.beatX.push_back (r.m.x + v);
        r.m.appendX = r.m.x + r.relAppend;
        layout.push_back (r.m);
    }
    for (int rr = 0; rr < rows; ++rr)
        rowY.push_back (10 + rr * rowHeight);

    const int h = rows * rowHeight + 30;
    if (getHeight() != h)
        setSize (getWidth(), h); // re-enters resized() once with the same width
    repaint();
}

// ------------------------------------------------------------------ painting

static juce::String bendLabel (double semis)
{
    if (semis <= 1.01) return juce::String::fromUTF8 ("\xc2\xbd");
    if (semis <= 2.01) return "full";
    if (semis <= 3.01) return juce::String::fromUTF8 ("1\xc2\xbd");
    return "2";
}

void TabEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    if (! track().isValid())
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (15.0f));
        g.drawFittedText ("No tab tracks yet.\nClick \"+ Track\" to start writing, or import a MIDI file or ASCII tab.",
                          getLocalBounds().withHeight (200), juce::Justification::centred, 3);
        return;
    }

    // string names
    const auto tuning = tab::trackTuning (track());
    for (size_t r = 0; r < rowY.size(); ++r)
    {
        g.setFont (uiFont (10.5f, true));
        g.setColour (palette::textDim);
        for (int s = 0; s < (int) tuning.size(); ++s)
            g.drawText (theory::midiNoteName (tuning[(size_t) s]), 4, (int) stringY ((int) r, s) - 7, leftPad - 10, 14,
                        juce::Justification::centredRight);
    }

    for (auto& m : layout)
        drawBar (g, m);
}

void TabEditor::drawBar (juce::Graphics& g, const MeasureLayout& m)
{
    const int ns = strings();
    const float top = stringY (m.row, ns - 1), bottom = stringY (m.row, 0);
    auto bar = barNode (m.bar);
    auto meas = tab::measures (score).getChild (m.bar);
    const double content = tab::barContentQ (bar), length = tab::measureLengthQ (meas);

    // selection / playback highlight columns
    const auto sel = selectionRange();
    for (int i = 0; i < (int) m.beatX.size(); ++i)
    {
        const Pos p { m.bar, i };
        if (selActive && ! (p < sel.first) && ! (sel.second < p))
        {
            g.setColour (palette::accent.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (m.beatX[(size_t) i] - 12.0f, top - 10.0f, 24.0f, bottom - top + 20.0f));
        }
    }
    if (playQ >= 0.0)
    {
        const auto starts = tab::measureStarts (score);
        if (m.bar + 1 < (int) starts.size() && playQ >= starts[(size_t) m.bar] && playQ < starts[(size_t) m.bar + 1])
        {
            double pos = starts[(size_t) m.bar];
            int i = 0;
            for (auto beat : bar)
            {
                const double len = tab::beatLengthQ (beat);
                if (playQ >= pos && playQ < pos + len)
                {
                    g.setColour (palette::info.withAlpha (0.28f));
                    g.fillRect (juce::Rectangle<float> (m.beatX[(size_t) i] - 12.0f, top - 12.0f, 24.0f, bottom - top + 24.0f));
                }
                pos += len;
                ++i;
            }
        }
    }

    // staff
    g.setColour (palette::outline.brighter (0.25f));
    for (int s = 0; s < ns; ++s)
        g.drawHorizontalLine ((int) stringY (m.row, s), m.x, m.x + m.w);
    g.setColour (palette::textDim);
    g.drawVerticalLine ((int) m.x, top, bottom + 1.0f);
    g.drawVerticalLine ((int) (m.x + m.w), top, bottom + 1.0f);

    // bar number, fill state, time signature
    const bool over = content > length + 1.0e-6, under = content < length - 1.0e-6 && bar.getNumChildren() > 0;
    g.setFont (uiFont (10.5f, true));
    g.setColour (over ? palette::bad : (under ? palette::warn : palette::textDim));
    g.drawText (juce::String (m.bar + 1) + (over ? "  too long" : (under ? "  incomplete" : "")), (int) m.x + 3, (int) top - 32, 120, 12,
                juce::Justification::left);
    const int num = (int) meas.getProperty (tids::tsNum, 4), den = (int) meas.getProperty (tids::tsDen, 4);
    const auto prev = tab::measures (score).getChild (m.bar - 1);
    if (m.bar == 0 || (int) prev.getProperty (tids::tsNum, 4) != num || (int) prev.getProperty (tids::tsDen, 4) != den)
    {
        g.setColour (palette::text);
        g.setFont (uiFont ((bottom - top) * 0.42f, true));
        const float mid = (top + bottom) * 0.5f;
        g.drawText (juce::String (num), (int) m.x + 6, (int) top, 20, (int) (mid - top), juce::Justification::centred);
        g.drawText (juce::String (den), (int) m.x + 6, (int) mid, 20, (int) (bottom - mid), juce::Justification::centred);
    }
    if (auto marker = meas[tids::marker].toString(); marker.isNotEmpty())
    {
        g.setColour (palette::info);
        g.setFont (uiFont (11.0f, true));
        g.drawText (marker, (int) m.x + 3, (int) top - 46, 160, 13, juce::Justification::left);
    }

    // beats
    for (int i = 0; i < bar.getNumChildren(); ++i)
    {
        auto beat = bar.getChild (i);
        const float x = m.beatX[(size_t) i];
        auto nextBeat = bar.getChild (i + 1);
        const float nextX = i + 1 < (int) m.beatX.size() ? m.beatX[(size_t) i + 1] : m.appendX;
        float markY = top - 14.0f; // technique text above the staff stacks upwards

        if ((bool) beat[tids::rest])
        {
            g.setColour (palette::textDim);
            const float mid = (top + bottom) * 0.5f;
            const int dur = (int) beat.getProperty (tids::dur, 4);
            if (dur <= 2)
                g.fillRect (juce::Rectangle<float> (x - 5.0f, dur == 1 ? mid - 6.0f : mid - 2.0f, 10.0f, 4.0f));
            else
            {
                juce::Path p;
                p.startNewSubPath (x - 2.0f, mid - 8.0f);
                p.lineTo (x + 3.0f, mid - 3.0f);
                p.lineTo (x - 2.0f, mid + 2.0f);
                p.lineTo (x + 3.0f, mid + 7.0f);
                g.strokePath (p, juce::PathStrokeType (1.6f));
            }
        }

        for (auto note : beat)
        {
            const int s = (int) note[tids::string];
            if (! juce::isPositiveAndBelow (s, ns)) continue;
            const float y = stringY (m.row, s);
            const int fret = (int) note[tids::fret];
            juce::String txt = (bool) note[tids::dead] ? "x" : juce::String (fret);
            const int harm = (int) note[tids::harmonic];
            if (harm != 0) txt = "<" + txt + ">";
            if ((bool) note[tids::ghost] || (bool) note[tids::tie]) txt = "(" + txt + ")";
            g.setFont (uiFont (12.5f, true));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), txt) + 4.0f;
            g.setColour (palette::bg);
            g.fillRect (juce::Rectangle<float> (x - tw * 0.5f, y - 7.0f, tw, 14.0f));
            g.setColour ((bool) note[tids::tie] ? palette::textDim : palette::text);
            g.drawText (txt, juce::Rectangle<float> (x - tw * 0.5f, y - 7.0f, tw, 14.0f), juce::Justification::centred);

            g.setColour (palette::accent);
            g.setFont (uiFont (10.0f, true));
            // hammer-on / pull-off: arc to the next note on this string
            if ((bool) note[tids::hammer])
            {
                auto nn = tab::noteOnString (nextBeat, s);
                const bool pull = nn.isValid() && (int) nn[tids::fret] < fret;
                juce::Path arc;
                arc.startNewSubPath (x + 4.0f, y - 8.0f);
                arc.quadraticTo ((x + nextX) * 0.5f, y - 16.0f, nextX - 4.0f, y - 8.0f);
                g.strokePath (arc, juce::PathStrokeType (1.2f));
                g.drawText (pull ? "P" : "H", (int) ((x + nextX) * 0.5f) - 6, (int) y - 26, 12, 10, juce::Justification::centred);
            }
            const int slide = (int) note[tids::slide];
            if (slide == tab::slideTo)
            {
                auto nn = tab::noteOnString (nextBeat, s);
                const bool down = nn.isValid() && (int) nn[tids::fret] < fret;
                g.drawLine (x + tw * 0.5f, down ? y - 3.0f : y + 3.0f, nextX - 8.0f, down ? y + 3.0f : y - 3.0f, 1.3f);
            }
            else if (slide == tab::slideInBelow)
                g.drawLine (x - tw * 0.5f - 10.0f, y + 4.0f, x - tw * 0.5f, y - 1.0f, 1.3f);
            else if (slide == tab::slideOutDown)
                g.drawLine (x + tw * 0.5f, y - 1.0f, x + tw * 0.5f + 10.0f, y + 4.0f, 1.3f);

            const double bend = (double) note[tids::bend];
            if (bend > 0.0)
            {
                juce::Path p;
                p.startNewSubPath (x + tw * 0.5f, y);
                p.quadraticTo (x + tw * 0.5f + 9.0f, y, x + tw * 0.5f + 9.0f, top - 6.0f);
                g.strokePath (p, juce::PathStrokeType (1.3f));
                juce::Path head;
                const float hx = x + tw * 0.5f + 9.0f, hy = top - 8.0f;
                head.addTriangle (hx - 3.0f, hy + 2.0f, hx + 3.0f, hy + 2.0f, hx, hy - 4.0f);
                g.fillPath (head);
                g.drawText (((bool) note[tids::prebend] ? "pb " : "") + bendLabel (bend), (int) hx - 14, (int) top - 22, 40, 10,
                            juce::Justification::centred);
            }
            if ((bool) note[tids::vibrato])
            {
                juce::Path w;
                w.startNewSubPath (x - 6.0f, markY);
                for (int k = 0; k < 4; ++k)
                    w.quadraticTo (x - 4.5f + k * 5.0f, markY + (k % 2 ? 3.0f : -3.0f), x - 1.0f + k * 5.0f, markY);
                g.strokePath (w, juce::PathStrokeType (1.2f));
                markY -= 10.0f;
            }
            auto mark = [&] (const juce::String& t) {
                g.drawText (t, (int) x - 16, (int) markY - 5, 32, 10, juce::Justification::centred);
                markY -= 10.0f;
            };
            if ((bool) note[tids::palmMute]) mark ("P.M.");
            if ((bool) note[tids::letRing]) mark ("l.r.");
            if ((bool) note[tids::tap]) mark ("T");
            if ((bool) note[tids::accent]) mark (">");
            if (harm == tab::harmArtificial) mark ("A.H.");
            if ((bool) note[tids::staccato])
                g.fillEllipse (juce::Rectangle<float> (x - 1.5f, y + 5.0f, 3.0f, 3.0f));
        }

        // beat-level marks
        g.setColour (palette::accent);
        g.setFont (uiFont (10.0f, true));
        const int stroke = (int) beat[tids::stroke];
        if (stroke != tab::strokeNone)
        {
            const float sx = x - 12.0f;
            g.drawLine (sx, top, sx, bottom, 1.2f);
            juce::Path a;
            if (stroke == tab::strokeDown) a.addTriangle (sx - 3.0f, bottom - 5.0f, sx + 3.0f, bottom - 5.0f, sx, bottom + 1.0f);
            else a.addTriangle (sx - 3.0f, top + 5.0f, sx + 3.0f, top + 5.0f, sx, top - 1.0f);
            g.fillPath (a);
        }
        if (std::abs ((double) beat[tids::whammy]) > 0.0)
            g.drawText ("w/bar " + juce::String ((double) beat[tids::whammy], 1), (int) x - 24, (int) markY - 5, 48, 10, juce::Justification::centred);
        if (auto t = beat[tids::text].toString(); t.isNotEmpty())
        {
            g.setColour (palette::text);
            g.drawText (t, (int) x - 30, (int) top - 46, 80, 12, juce::Justification::left);
        }
    }

    drawRhythm (g, m, bar);

    // cursor
    if (m.bar == cursor.bar)
    {
        const float cx = cursor.beat < (int) m.beatX.size() ? m.beatX[(size_t) cursor.beat] : m.appendX;
        const float cy = stringY (m.row, cursorString);
        auto r = juce::Rectangle<float> (cx - 12.0f, cy - 8.0f, 24.0f, 16.0f);
        g.setColour (palette::accent.withAlpha (hasKeyboardFocus (true) ? 0.25f : 0.1f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (hasKeyboardFocus (true) ? palette::accent : palette::accentDim);
        g.drawRoundedRectangle (r, 3.0f, 1.5f);
        g.setColour (palette::accent.withAlpha (0.25f));
        g.drawVerticalLine ((int) cx, top - 4.0f, bottom + 4.0f);
    }
}

void TabEditor::drawRhythm (juce::Graphics& g, const MeasureLayout& m, const juce::ValueTree& bar)
{
    const float y0 = stringY (m.row, 0) + 9.0f;
    const float stemLen = 20.0f;
    const float yEnd = y0 + stemLen;
    auto meas = tab::measures (score).getChild (m.bar);
    const int num = (int) meas.getProperty (tids::tsNum, 4), den = (int) meas.getProperty (tids::tsDen, 4);
    const double groupLen = (den == 8 && num % 3 == 0) ? 1.5 : 1.0;

    struct Info { float x; int flags; bool rest; int dur; int dots; int tuplet; int group; };
    std::vector<Info> info;
    double pos = 0.0;
    for (int i = 0; i < bar.getNumChildren(); ++i)
    {
        auto b = bar.getChild (i);
        const int dur = (int) b.getProperty (tids::dur, 4);
        Info inf;
        inf.x = m.beatX[(size_t) i];
        inf.dur = dur;
        inf.flags = dur >= 8 ? (int) std::round (std::log2 (dur / 4.0)) : 0;
        inf.rest = (bool) b[tids::rest];
        inf.dots = (int) b[tids::dots];
        inf.tuplet = (int) b[tids::tuplet];
        inf.group = (int) std::floor (pos / groupLen + 1.0e-6);
        info.push_back (inf);
        pos += tab::beatLengthQ (b);
    }

    g.setColour (palette::textDim);
    for (size_t i = 0; i < info.size(); ++i)
    {
        const auto& a = info[i];
        if (a.rest || a.dur <= 1) continue;
        g.drawLine (a.x, a.dur == 2 ? y0 + 8.0f : y0, a.x, yEnd, 1.2f);
        if (a.dots > 0) g.fillEllipse (a.x + 3.0f, yEnd - 3.0f, 3.0f, 3.0f);
        if ((bool) bar.getChild ((int) i)[tids::tremolo])
            for (int k = 0; k < 3; ++k)
                g.drawLine (a.x - 4.0f, y0 + 6.0f + k * 3.5f, a.x + 4.0f, y0 + 3.0f + k * 3.5f, 1.4f);
    }

    // beams within each beat group, flags for lone notes
    size_t i = 0;
    while (i < info.size())
    {
        size_t j = i;
        while (j + 1 < info.size() && info[j + 1].group == info[i].group && info[j + 1].flags > 0 && ! info[j + 1].rest
               && info[j].flags > 0 && ! info[j].rest)
            ++j;
        const auto& a = info[i];
        if (! a.rest && a.flags > 0)
        {
            if (j == i)
            {
                for (int f = 0; f < a.flags; ++f)
                    g.drawLine (a.x, yEnd - f * 4.0f, a.x + 7.0f, yEnd - 5.0f - f * 4.0f, 1.4f);
            }
            else
            {
                for (size_t k = i; k <= j; ++k)
                {
                    const auto& c = info[k];
                    for (int f = 0; f < c.flags; ++f)
                    {
                        const float yy = yEnd - f * 4.0f;
                        if (k < j && info[k + 1].flags > f)
                            g.fillRect (juce::Rectangle<float> (c.x, yy - 1.2f, info[k + 1].x - c.x, 2.4f));
                        else if (f > 0 && ! (k > i && info[k - 1].flags > f))
                            g.fillRect (juce::Rectangle<float> (k == j ? c.x - 6.0f : c.x, yy - 1.2f, 6.0f, 2.4f));
                    }
                }
                if (info[i].tuplet > 0)
                {
                    g.setFont (uiFont (10.0f, true));
                    g.drawText (juce::String (info[i].tuplet), (int) ((info[i].x + info[j].x) * 0.5f) - 8, (int) yEnd + 2, 16, 10,
                                juce::Justification::centred);
                }
            }
        }
        i = j + 1;
    }
}

// ------------------------------------------------------------------ hit testing & cursor

bool TabEditor::locate (juce::Point<int> p, Pos& pos, int& string) const
{
    for (auto& m : layout)
    {
        const int ry = rowY[(size_t) m.row];
        if (p.y < ry || p.y >= ry + rowHeight || p.x < m.x || p.x >= m.x + m.w)
            continue;
        pos.bar = m.bar;
        pos.beat = (int) m.beatX.size();
        float best = std::abs ((float) p.x - m.appendX);
        for (int i = 0; i < (int) m.beatX.size(); ++i)
            if (std::abs ((float) p.x - m.beatX[(size_t) i]) < best)
            {
                best = std::abs ((float) p.x - m.beatX[(size_t) i]);
                pos.beat = i;
            }
        const int ns = strings();
        string = juce::jlimit (0, ns - 1, ns - 1 - juce::roundToInt (((float) p.y - (float) (ry + topPad)) / (float) lineGap));
        return true;
    }
    return false;
}

std::pair<TabEditor::Pos, TabEditor::Pos> TabEditor::selectionRange() const
{
    Pos a = selAnchor, b = cursor;
    if (b < a) std::swap (a, b);
    return { a, b };
}

juce::Array<juce::ValueTree> TabEditor::selectedBeats() const
{
    juce::Array<juce::ValueTree> r;
    if (! selActive)
    {
        if (auto b = beatAt (cursor); b.isValid()) r.add (b);
        return r;
    }
    const auto [a, b] = selectionRange();
    for (int bar = a.bar; bar <= b.bar; ++bar)
    {
        const int first = bar == a.bar ? a.beat : 0;
        const int last = bar == b.bar ? b.beat : numBeats (bar) - 1;
        for (int i = first; i <= juce::jmin (last, numBeats (bar) - 1); ++i)
            r.add (barNode (bar).getChild (i));
    }
    return r;
}

double TabEditor::cursorQ() const
{
    const auto starts = tab::measureStarts (score);
    double q = starts[(size_t) juce::jlimit (0, (int) starts.size() - 1, cursor.bar)];
    for (int i = 0; i < juce::jmin (cursor.beat, numBeats (cursor.bar)); ++i)
        q += tab::beatLengthQ (barNode (cursor.bar).getChild (i));
    return q;
}

std::pair<double, double> TabEditor::selectionSpanQ() const
{
    const auto starts = tab::measureStarts (score);
    if (! selActive)
    {
        const int b = juce::jlimit (0, (int) starts.size() - 2, cursor.bar);
        return { starts[(size_t) b], starts[(size_t) b + 1] };
    }
    const auto [a, b] = selectionRange();
    auto posOf = [&] (Pos p, bool end) {
        double q = starts[(size_t) p.bar];
        const int upto = juce::jmin (p.beat + (end ? 1 : 0), numBeats (p.bar));
        for (int i = 0; i < upto; ++i) q += tab::beatLengthQ (barNode (p.bar).getChild (i));
        return q;
    };
    return { posOf (a, false), juce::jmax (posOf (a, false) + 0.25, posOf (b, true)) };
}

juce::Rectangle<int> TabEditor::cursorBounds() const
{
    if (auto* m = layoutFor (cursor.bar))
        return { (int) m->x, rowY[(size_t) m->row], (int) m->w, rowHeight };
    return {};
}

void TabEditor::setCursor (Pos p, bool extend)
{
    if (extend && ! selActive) { selActive = true; selAnchor = cursor; }
    if (! extend) selActive = false;
    p.bar = juce::jlimit (0, juce::jmax (0, barCount() - 1), p.bar);
    p.beat = juce::jlimit (0, numBeats (p.bar), p.beat);
    cursor = p;
    lastDigit = -1;
    changed();
}

void TabEditor::moveCursor (int d)
{
    Pos p = cursor;
    p.beat += d;
    if (p.beat > numBeats (p.bar))
    {
        if (p.bar + 1 >= barCount())
        {
            begin ("Add measure");
            auto meas = tab::measures (score).getChild (p.bar);
            tab::addMeasure (score, -1, (int) meas.getProperty (tids::tsNum, 4), (int) meas.getProperty (tids::tsDen, 4), um());
        }
        p.bar += 1;
        p.beat = 0;
    }
    else if (p.beat < 0)
    {
        if (p.bar == 0) p.beat = 0;
        else { p.bar -= 1; p.beat = juce::jmax (0, numBeats (p.bar) - 1); }
    }
    cursor = p;
    lastDigit = -1;
    changed();
}

void TabEditor::moveBar (int d)
{
    Pos p { juce::jlimit (0, barCount() - 1, cursor.bar + d), 0 };
    cursor = p;
    changed();
}

void TabEditor::changed()
{
    repaint();
    if (onStateChanged) onStateChanged();
}

void TabEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    Pos p;
    int s;
    if (locate (e.getPosition(), p, s))
    {
        cursorString = s;
        setCursor (p, e.mods.isShiftDown());
    }
}

void TabEditor::mouseDrag (const juce::MouseEvent& e)
{
    Pos p;
    int s;
    if (e.getDistanceFromDragStart() > 4 && locate (e.getPosition(), p, s))
    {
        if (! selActive) { selActive = true; selAnchor = cursor; }
        cursor = { p.bar, juce::jmin (p.beat, juce::jmax (0, numBeats (p.bar) - 1)) };
        changed();
    }
}

// ------------------------------------------------------------------ editing

juce::ValueTree TabEditor::ensureBeatAtCursor()
{
    auto bar = barNode (cursor.bar);
    if (cursor.beat < bar.getNumChildren())
        return bar.getChild (cursor.beat);
    auto b = tab::makeBeat (entryDur, false);
    if (entryDots) b.setProperty (tids::dots, entryDots, nullptr);
    if (entryTuplet) b.setProperty (tids::tuplet, entryTuplet, nullptr);
    bar.appendChild (b, um());
    return b;
}

void TabEditor::enterFret (int fret)
{
    if (! track().isValid()) return;
    begin ("Enter note");
    auto beat = ensureBeatAtCursor();
    beat.setProperty (tids::rest, false, um());
    if (auto n = tab::noteOnString (beat, cursorString); n.isValid())
        n.setProperty (tids::fret, juce::jlimit (0, tab::maxFret, fret), um());
    else
        beat.appendChild (tab::makeNote (cursorString, fret), um());
    changed();
}

void TabEditor::setDuration (int dur)
{
    entryDur = dur;
    auto beats = selectedBeats();
    if (! beats.isEmpty())
    {
        begin ("Change duration");
        for (auto b : beats) b.setProperty (tids::dur, dur, um());
    }
    changed();
}

void TabEditor::toggleDots()
{
    auto beats = selectedBeats();
    const int v = beats.isEmpty() ? (entryDots ? 0 : 1) : ((int) beats[0][tids::dots] ? 0 : 1);
    entryDots = v;
    begin ("Dotted");
    for (auto b : beats) b.setProperty (tids::dots, v, um());
    changed();
}

void TabEditor::toggleTuplet()
{
    auto beats = selectedBeats();
    const int v = beats.isEmpty() ? (entryTuplet ? 0 : 3) : ((int) beats[0][tids::tuplet] ? 0 : 3);
    entryTuplet = v;
    begin ("Triplet");
    for (auto b : beats) b.setProperty (tids::tuplet, v, um());
    changed();
}

void TabEditor::toggleRest()
{
    if (! track().isValid()) return;
    begin ("Rest");
    auto bar = barNode (cursor.bar);
    if (cursor.beat >= bar.getNumChildren())
    {
        auto r = tab::makeBeat (entryDur, true);
        if (entryDots) r.setProperty (tids::dots, entryDots, nullptr);
        bar.appendChild (r, um());
    }
    else
    {
        auto b = bar.getChild (cursor.beat);
        const bool makeRest = ! (bool) b[tids::rest];
        if (makeRest) b.removeAllChildren (um());
        b.setProperty (tids::rest, makeRest, um());
    }
    changed();
}

void TabEditor::toggleNoteFlag (const juce::Identifier& id, const juce::String& undoName)
{
    juce::Array<juce::ValueTree> notes;
    if (selActive)
        for (auto b : selectedBeats()) for (auto n : b) notes.add (n);
    else if (auto n = tab::noteOnString (beatAt (cursor), cursorString); n.isValid())
        notes.add (n);
    if (notes.isEmpty()) return;
    const bool v = ! (bool) notes[0][id];
    begin (undoName);
    for (auto n : notes) n.setProperty (id, v, um());
    changed();
}

void TabEditor::cycleBend()
{
    auto n = tab::noteOnString (beatAt (cursor), cursorString);
    if (! n.isValid()) return;
    const double cur = (double) n[tids::bend];
    const double next = cur < 0.5 ? 1.0 : (cur < 1.5 ? 2.0 : (cur < 2.5 ? 3.0 : (cur < 3.5 ? 4.0 : 0.0)));
    begin ("Bend");
    n.setProperty (tids::bend, next, um());
    changed();
}

void TabEditor::cycleSlide()
{
    auto n = tab::noteOnString (beatAt (cursor), cursorString);
    if (! n.isValid()) return;
    begin ("Slide");
    n.setProperty (tids::slide, ((int) n[tids::slide] + 1) % 4, um());
    changed();
}

void TabEditor::cycleHarmonic (int kind)
{
    auto n = tab::noteOnString (beatAt (cursor), cursorString);
    if (! n.isValid()) return;
    begin ("Harmonic");
    n.setProperty (tids::harmonic, (int) n[tids::harmonic] == kind ? 0 : kind, um());
    changed();
}

void TabEditor::setStroke (int stroke)
{
    auto beats = selectedBeats();
    if (beats.isEmpty()) return;
    const int v = (int) beats[0][tids::stroke] == stroke ? 0 : stroke;
    begin ("Pick stroke");
    for (auto b : beats) b.setProperty (tids::stroke, v, um());
    changed();
}

void TabEditor::toggleBeatFlag (const juce::Identifier& id, const juce::String& undoName)
{
    auto beats = selectedBeats();
    if (beats.isEmpty()) return;
    const bool v = ! (bool) beats[0][id];
    begin (undoName);
    for (auto b : beats) b.setProperty (id, v, um());
    changed();
}

void TabEditor::insertBeat()
{
    if (! track().isValid()) return;
    begin ("Insert beat");
    auto r = tab::makeBeat (entryDur, true);
    barNode (cursor.bar).addChild (r, cursor.beat, um());
    changed();
}

void TabEditor::deleteNote()
{
    if (selActive) { deleteBeat(); return; }
    auto beat = beatAt (cursor);
    auto n = tab::noteOnString (beat, cursorString);
    if (! n.isValid()) return;
    begin ("Delete note");
    beat.removeChild (n, um());
    if (beat.getNumChildren() == 0) beat.setProperty (tids::rest, true, um());
    changed();
}

void TabEditor::deleteBeat()
{
    auto beats = selectedBeats();
    if (beats.isEmpty()) return;
    begin ("Delete beats");
    for (auto b : beats) b.getParent().removeChild (b, um());
    if (selActive) { cursor = selectionRange().first; selActive = false; }
    cursor.beat = juce::jlimit (0, numBeats (cursor.bar), cursor.beat);
    changed();
}

void TabEditor::copy (bool cut)
{
    clipboard.clear();
    for (auto b : selectedBeats()) clipboard.push_back (b.createCopy());
    if (cut) deleteBeat();
}

void TabEditor::paste()
{
    if (clipboard.empty() || ! track().isValid()) return;
    begin ("Paste beats");
    auto bar = barNode (cursor.bar);
    int at = cursor.beat;
    const int ns = strings();
    for (auto& b : clipboard)
    {
        auto c = b.createCopy();
        for (int i = c.getNumChildren(); --i >= 0;)
            if ((int) c.getChild (i)[tids::string] >= ns) c.removeChild (i, nullptr); // from a track with more strings
        bar.addChild (c, at++, um());
    }
    cursor.beat = at;
    selActive = false;
    changed();
}

void TabEditor::transpose (int semis)
{
    auto beats = selectedBeats();
    if (beats.isEmpty()) return;
    begin (semis > 0 ? "Transpose up" : "Transpose down");
    tab::transposeBeats (beats, tab::trackTuning (track()), semis, um());
    changed();
}

void TabEditor::insertMeasure (bool after)
{
    auto meas = tab::measures (score).getChild (cursor.bar);
    begin ("Insert measure");
    tab::addMeasure (score, cursor.bar + (after ? 1 : 0), (int) meas.getProperty (tids::tsNum, 4), (int) meas.getProperty (tids::tsDen, 4), um());
    if (after) { cursor.bar += 1; cursor.beat = 0; }
    changed();
}

void TabEditor::deleteMeasure()
{
    if (tab::numMeasures (score) <= 1) return;
    begin ("Delete measure");
    tab::removeMeasure (score, cursor.bar, um());
    cursor.bar = juce::jlimit (0, barCount() - 1, cursor.bar);
    cursor.beat = 0;
    changed();
}

bool TabEditor::keyPressed (const juce::KeyPress& k)
{
    if (! track().isValid()) return false;
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    const auto ch = k.getTextCharacter();
    const bool cmd = mods.isCommandDown() || mods.isCtrlDown();

    if (cmd)
    {
        if (code == juce::KeyPress::leftKey) { moveBar (-1); return true; }
        if (code == juce::KeyPress::rightKey) { moveBar (1); return true; }
        if (code == juce::KeyPress::upKey) { setTrackIndex (trackIndex - 1); return true; }
        if (code == juce::KeyPress::downKey) { setTrackIndex (trackIndex + 1); return true; }
        if (code == 'C') { copy (false); return true; }
        if (code == 'X') { copy (true); return true; }
        if (code == 'V') { paste(); return true; }
        if (code == 'A')
        {
            selActive = true;
            selAnchor = { 0, 0 };
            cursor = { barCount() - 1, juce::jmax (0, numBeats (barCount() - 1) - 1) };
            changed();
            return true;
        }
        return false; // undo, save, etc. go to the application
    }

    if (code == juce::KeyPress::leftKey)
    {
        if (mods.isShiftDown()) { if (! selActive) { selActive = true; selAnchor = cursor; } }
        else selActive = false;
        moveCursor (-1);
        return true;
    }
    if (code == juce::KeyPress::rightKey)
    {
        if (mods.isShiftDown()) { if (! selActive) { selActive = true; selAnchor = cursor; } }
        else selActive = false;
        moveCursor (1);
        return true;
    }
    if (code == juce::KeyPress::upKey) { cursorString = juce::jmin (strings() - 1, cursorString + 1); lastDigit = -1; changed(); return true; }
    if (code == juce::KeyPress::downKey) { cursorString = juce::jmax (0, cursorString - 1); lastDigit = -1; changed(); return true; }
    if (code == juce::KeyPress::homeKey) { setCursor ({ cursor.bar, 0 }, mods.isShiftDown()); return true; }
    if (code == juce::KeyPress::endKey) { setCursor ({ cursor.bar, numBeats (cursor.bar) }, mods.isShiftDown()); return true; }
    if (code == juce::KeyPress::escapeKey) { selActive = false; changed(); return true; }
    if (code == juce::KeyPress::deleteKey) { deleteNote(); return true; }
    if (code == juce::KeyPress::backspaceKey) { deleteBeat(); return true; }
    if (code == juce::KeyPress::insertKey) { insertBeat(); return true; }

    if (ch >= '0' && ch <= '9')
    {
        const int d = (int) (ch - '0');
        const auto now = juce::Time::getMillisecondCounter();
        int fret = d;
        if (lastDigit >= 0 && now - lastDigitTime < 750 && lastDigitPos.bar == cursor.bar && lastDigitPos.beat == cursor.beat
            && lastDigitString == cursorString && lastDigit * 10 + d <= tab::maxFret)
        {
            fret = lastDigit * 10 + d;
            lastDigit = -1;
        }
        else
        {
            lastDigit = d;
        }
        lastDigitTime = now;
        lastDigitPos = cursor;
        lastDigitString = cursorString;
        const int keep = lastDigit;
        enterFret (fret);
        lastDigit = keep;
        return true;
    }

    switch (ch)
    {
        case '+': case '=': setDuration (juce::jmax (1, (beatAt (cursor).isValid() ? (int) beatAt (cursor)[tids::dur] : entryDur) / 2)); return true;
        case '-': case '_': setDuration (juce::jmin (64, (beatAt (cursor).isValid() ? (int) beatAt (cursor)[tids::dur] : entryDur) * 2)); return true;
        case '.': toggleDots(); return true;
        case 't': toggleTuplet(); return true;
        case 'T': toggleNoteFlag (tids::tap, "Tap"); return true;
        case 'r': case 'R': toggleRest(); return true;
        case 'h': case 'H': toggleNoteFlag (tids::hammer, "Hammer-on / pull-off"); return true;
        case 's': case 'S': cycleSlide(); return true;
        case 'b': case 'B': cycleBend(); return true;
        case 'v': case 'V': toggleNoteFlag (tids::vibrato, "Vibrato"); return true;
        case 'p': case 'P': toggleNoteFlag (tids::palmMute, "Palm mute"); return true;
        case 'x': case 'X': toggleNoteFlag (tids::dead, "Dead note"); return true;
        case 'g': case 'G': toggleNoteFlag (tids::ghost, "Ghost note"); return true;
        case 'n': case 'N': cycleHarmonic (tab::harmNatural); return true;
        case 'a': case 'A': cycleHarmonic (tab::harmArtificial); return true;
        case 'l': case 'L': toggleNoteFlag (tids::letRing, "Let ring"); return true;
        case 'z': case 'Z': toggleNoteFlag (tids::tie, "Tie"); return true;
        case '>': toggleNoteFlag (tids::accent, "Accent"); return true;
        case ',': toggleNoteFlag (tids::staccato, "Staccato"); return true;
        case 'd': case 'D': setStroke (tab::strokeDown); return true;
        case 'u': case 'U': setStroke (tab::strokeUp); return true;
        case 'w': case 'W': toggleBeatFlag (tids::tremolo, "Tremolo picking"); return true;
        default: break;
    }
    return false; // space, return etc. reach the global shortcuts (play / stop)
}
} // namespace ff
