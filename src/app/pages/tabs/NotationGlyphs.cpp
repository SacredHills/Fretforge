#include "NotationGlyphs.h"

namespace ff::notation
{
namespace
{
juce::PathStrokeType stroke (float w) { return juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); }
}

void drawTrebleClef (juce::Graphics& g, float x, float gy, float sp)
{
    // designed in staff spaces with the origin on the G line; y grows downwards
    auto P = [&] (float px, float py) { return juce::Point<float> (x + px * sp, gy + py * sp); };
    juce::Path p;
    p.startNewSubPath (P (0.18f, 0.12f));
    p.cubicTo (P (0.15f, 0.62f), P (-0.75f, 0.70f), P (-0.80f, 0.05f));       // curl around the G line
    p.cubicTo (P (-0.85f, -0.75f), P (0.05f, -1.25f), P (0.55f, -1.75f));     // up to the right
    p.cubicTo (P (1.05f, -2.30f), P (0.95f, -3.55f), P (0.45f, -4.25f));      // top loop, right side
    p.cubicTo (P (0.15f, -4.55f), P (-0.30f, -3.90f), P (-0.20f, -3.20f));    // top loop, left side
    p.cubicTo (P (-0.10f, -2.20f), P (0.10f, 0.20f), P (0.30f, 2.15f));       // long spine down
    p.cubicTo (P (0.38f, 2.75f), P (-0.20f, 3.10f), P (-0.55f, 2.70f));       // bottom hook
    g.strokePath (p, stroke (sp * 0.17f));
    g.fillEllipse (juce::Rectangle<float> (sp * 0.62f, sp * 0.62f).withCentre (P (-0.45f, 2.55f)));
    // thicken the descending part of the curl
    juce::Path thick;
    thick.startNewSubPath (P (-0.80f, 0.05f));
    thick.cubicTo (P (-0.85f, -0.75f), P (0.05f, -1.25f), P (0.55f, -1.75f));
    g.strokePath (thick, stroke (sp * 0.30f));
}

void drawBassClef (juce::Graphics& g, float x, float fy, float sp)
{
    auto P = [&] (float px, float py) { return juce::Point<float> (x + px * sp, fy + py * sp); };
    juce::Path p;
    p.startNewSubPath (P (-0.30f, 0.0f));
    p.cubicTo (P (-0.35f, -0.85f), P (1.25f, -1.05f), P (1.35f, 0.05f));
    p.cubicTo (P (1.45f, 1.25f), P (0.40f, 2.15f), P (-0.55f, 2.55f));
    g.strokePath (p, stroke (sp * 0.22f));
    g.fillEllipse (juce::Rectangle<float> (sp * 0.62f, sp * 0.62f).withCentre (P (-0.18f, 0.05f)));
    g.fillEllipse (juce::Rectangle<float> (sp * 0.28f, sp * 0.28f).withCentre (P (1.85f, -0.48f)));
    g.fillEllipse (juce::Rectangle<float> (sp * 0.28f, sp * 0.28f).withCentre (P (1.85f, 0.48f)));
}

void drawOctaveMark (juce::Graphics& g, float x, float y, float sp)
{
    g.setFont (juce::FontOptions (sp * 1.25f, juce::Font::bold));
    g.drawText ("8", juce::Rectangle<float> (x - sp, y - sp * 0.65f, sp * 2.0f, sp * 1.3f), juce::Justification::centred, false);
}

void drawSharp (juce::Graphics& g, float cx, float cy, float sp)
{
    const float w = sp * 0.42f, h = sp * 1.35f;
    g.drawLine (cx - w * 0.45f, cy - h, cx - w * 0.45f, cy + h * 0.85f, sp * 0.11f);
    g.drawLine (cx + w * 0.45f, cy - h * 0.85f, cx + w * 0.45f, cy + h, sp * 0.11f);
    g.drawLine (cx - w, cy - sp * 0.25f, cx + w, cy - sp * 0.50f, sp * 0.24f);
    g.drawLine (cx - w, cy + sp * 0.50f, cx + w, cy + sp * 0.25f, sp * 0.24f);
}

void drawFlat (juce::Graphics& g, float cx, float cy, float sp)
{
    const float left = cx - sp * 0.30f;
    g.drawLine (left, cy - sp * 1.75f, left, cy + sp * 0.50f, sp * 0.11f);
    juce::Path bowl;
    bowl.startNewSubPath (left, cy + sp * 0.50f);
    bowl.cubicTo (left + sp * 0.95f, cy - sp * 0.05f, left + sp * 0.75f, cy - sp * 0.75f, left, cy - sp * 0.15f);
    g.strokePath (bowl, stroke (sp * 0.16f));
}

void drawNatural (juce::Graphics& g, float cx, float cy, float sp)
{
    const float w = sp * 0.30f;
    g.drawLine (cx - w, cy - sp * 1.25f, cx - w, cy + sp * 0.45f, sp * 0.11f);
    g.drawLine (cx + w, cy - sp * 0.45f, cx + w, cy + sp * 1.25f, sp * 0.11f);
    g.drawLine (cx - w, cy - sp * 0.25f, cx + w, cy - sp * 0.50f, sp * 0.22f);
    g.drawLine (cx - w, cy + sp * 0.50f, cx + w, cy + sp * 0.25f, sp * 0.22f);
}

void drawAccidental (juce::Graphics& g, int alter, float cx, float cy, float sp)
{
    if (alter > 0) drawSharp (g, cx, cy, sp);
    else if (alter < 0) drawFlat (g, cx, cy, sp);
    else drawNatural (g, cx, cy, sp);
}

void drawNotehead (juce::Graphics& g, float cx, float cy, float sp, bool filled, bool cross)
{
    if (cross)
    {
        const float r = sp * 0.48f;
        g.drawLine (cx - r, cy - r, cx + r, cy + r, sp * 0.16f);
        g.drawLine (cx - r, cy + r, cx + r, cy - r, sp * 0.16f);
        return;
    }
    juce::Path head;
    head.addEllipse (-sp * 0.62f, -sp * 0.42f, sp * 1.24f, sp * 0.84f);
    head.applyTransform (juce::AffineTransform::rotation (-0.35f).translated (cx, cy));
    if (filled) g.fillPath (head);
    else g.strokePath (head, juce::PathStrokeType (sp * 0.18f));
}

void drawRest (juce::Graphics& g, int dur, float cx, float top, float sp)
{
    const float mid = top + 2.0f * sp;
    if (dur <= 1) { g.fillRect (juce::Rectangle<float> (cx - sp * 0.6f, top + sp, sp * 1.2f, sp * 0.5f)); return; }
    if (dur == 2) { g.fillRect (juce::Rectangle<float> (cx - sp * 0.6f, mid - sp * 0.5f, sp * 1.2f, sp * 0.5f)); return; }
    if (dur == 4)
    {
        juce::Path z;
        z.startNewSubPath (cx - sp * 0.25f, mid - sp * 1.5f);
        z.lineTo (cx + sp * 0.35f, mid - sp * 0.7f);
        z.lineTo (cx - sp * 0.30f, mid + sp * 0.1f);
        z.lineTo (cx + sp * 0.35f, mid + sp * 0.8f);
        z.quadraticTo (cx - sp * 0.55f, mid + sp * 0.5f, cx - sp * 0.05f, mid + sp * 1.4f);
        g.strokePath (z, stroke (sp * 0.24f));
        return;
    }
    // eighth and shorter: slanted stem with one hook per flag
    const int hooks = juce::jlimit (1, 4, (int) std::round (std::log2 (dur / 4.0)));
    const float x0 = cx + sp * 0.35f, y0 = mid - sp * 1.0f;
    const float y1 = y0 + sp * (1.6f + hooks * 0.55f);
    g.drawLine (x0, y0, x0 - sp * 0.45f, y1, sp * 0.13f);
    for (int k = 0; k < hooks; ++k)
    {
        const float hy = y0 + k * sp;
        const float hx = x0 - (hy - y0) / (y1 - y0) * sp * 0.45f;
        g.fillEllipse (juce::Rectangle<float> (sp * 0.42f, sp * 0.42f).withCentre ({ hx - sp * 0.62f, hy + sp * 0.12f }));
        juce::Path hook;
        hook.startNewSubPath (hx - sp * 0.62f, hy + sp * 0.18f);
        hook.quadraticTo (hx - sp * 0.2f, hy + sp * 0.38f, hx, hy);
        g.strokePath (hook, stroke (sp * 0.12f));
    }
}

void drawFlags (juce::Graphics& g, float sx, float sy, float sp, int count, bool up)
{
    const float dir = up ? 1.0f : -1.0f;  // flags hang down from an up-stem
    for (int k = 0; k < count; ++k)
    {
        const float y = sy + dir * k * sp * 0.75f;
        juce::Path f;
        f.startNewSubPath (sx, y);
        f.cubicTo (sx + sp * 0.15f, y + dir * sp * 0.7f, sx + sp * 1.05f, y + dir * sp * 0.9f, sx + sp * 0.75f, y + dir * sp * 2.1f);
        g.strokePath (f, stroke (sp * 0.17f));
    }
}

void drawTempoNote (juce::Graphics& g, float x, float baseline, float size)
{
    const float sp = size * 0.36f;
    drawNotehead (g, x + sp * 0.6f, baseline - sp * 0.45f, sp, true, false);
    g.drawLine (x + sp * 1.15f, baseline - sp * 0.55f, x + sp * 1.15f, baseline - sp * 3.4f, sp * 0.14f);
}
} // namespace ff::notation
