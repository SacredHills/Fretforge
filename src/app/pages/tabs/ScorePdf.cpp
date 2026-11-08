#include "ScorePdf.h"
#include "core/MusicTheory.h"

namespace ff
{
namespace tids = tab::ids;

bool ScorePdf::writeImagesAsPdf (const std::vector<juce::Image>& pages, float pw, float ph, const juce::String& title, const juce::File& file)
{
    juce::MemoryOutputStream out;
    std::vector<juce::int64> offsets;
    auto beginObj = [&] { offsets.push_back ((juce::int64) out.getPosition()); out << (int) offsets.size() << " 0 obj\n"; };
    auto escape = [] (const juce::String& s) {
        juce::String r;
        for (auto c : s)
        {
            if (c == '(' || c == ')' || c == '\\') r << "\\";
            r << (c < 128 ? juce::String::charToString (c) : juce::String ("?"));
        }
        return r;
    };

    out << "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
    const int n = (int) pages.size();
    beginObj();
    out << "<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    beginObj();
    out << "<< /Type /Pages /Count " << n << " /Kids [";
    for (int i = 0; i < n; ++i) out << (3 + i * 3) << " 0 R ";
    out << "] >>\nendobj\n";

    const auto w = juce::String (pw, 2), h = juce::String (ph, 2);
    for (int i = 0; i < n; ++i)
    {
        const int base = 3 + i * 3;
        beginObj();
        out << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << w << " " << h << "] /Resources << /XObject << /Im0 " << (base + 2)
            << " 0 R >> >> /Contents " << (base + 1) << " 0 R >>\nendobj\n";
        const juce::String content = "q " + w + " 0 0 " + h + " 0 0 cm /Im0 Do Q\n";
        beginObj();
        out << "<< /Length " << content.length() << " >>\nstream\n" << content << "endstream\nendobj\n";

        juce::MemoryOutputStream jpeg;
        juce::JPEGImageFormat fmt;
        fmt.setQuality (0.9f);
        if (! fmt.writeImageToStream (pages[(size_t) i], jpeg)) return false;
        beginObj();
        out << "<< /Type /XObject /Subtype /Image /Width " << pages[(size_t) i].getWidth() << " /Height " << pages[(size_t) i].getHeight()
            << " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length " << (int) jpeg.getDataSize() << " >>\nstream\n";
        out.write (jpeg.getData(), jpeg.getDataSize());
        out << "\nendstream\nendobj\n";
    }
    beginObj();
    out << "<< /Title (" << escape (title) << ") /Producer (RAVENYR " << FF_VERSION_STRING << ") >>\nendobj\n";
    const int infoObj = (int) offsets.size();

    const auto xref = out.getPosition();
    out << "xref\n0 " << (int) offsets.size() + 1 << "\n0000000000 65535 f \n";
    for (auto o : offsets) out << juce::String (o).paddedLeft ('0', 10) << " 00000 n \n";
    out << "trailer\n<< /Size " << (int) offsets.size() + 1 << " /Root 1 0 R /Info " << infoObj << " 0 R >>\nstartxref\n"
        << (juce::int64) xref << "\n%%EOF\n";

    juce::TemporaryFile tmp (file);
    if (! tmp.getFile().replaceWithData (out.getData(), out.getDataSize())) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

juce::Result ScorePdf::write (ProjectDocument& doc, EngineHost& host, const juce::ValueTree& score, const juce::File& file, const Options& opt)
{
    const int numTracks = tab::tracks (score).getNumChildren();
    if (numTracks == 0) return juce::Result::fail ("The score has no tracks.");

    const float pw = opt.letter ? 612.0f : 595.28f, ph = opt.letter ? 792.0f : 841.89f;
    const float margin = 36.0f;                         // half an inch
    constexpr int editorWidth = 820;                     // layout width in editor pixels
    const float scale = (pw - 2.0f * margin) / (float) editorWidth;   // points per editor pixel
    constexpr float dpi = 200.0f;
    const float pxPerPt = dpi / 72.0f;
    const int imgW = juce::roundToInt (pw * pxPerPt), imgH = juce::roundToInt (ph * pxPerPt);
    const float contentH = (ph - 2.0f * margin) / scale;  // editor pixels per page
    std::vector<juce::Image> pages;

    TabEditor ed (doc, host);
    ed.setPrintMode (true);
    ed.setScore (score);
    auto title = score[tids::title].toString();
    if (title.isEmpty()) title = "Untitled";

    for (int t = 0; t < numTracks; ++t)
    {
        if (! opt.allTracks && t != opt.track) continue;
        ed.setTrackIndex (t);
        ed.setSize (editorWidth, 100);
        ed.layoutChanged();
        const auto& rows = ed.rows();
        const auto track = tab::tracks (score).getChild (t);

        size_t r = 0;
        int pageNo = 0;
        while (r < rows.size() || pageNo == 0)
        {
            juce::Image img (juce::Image::RGB, imgW, imgH, true);
            juce::Graphics g (img);
            g.fillAll (juce::Colours::white);
            g.addTransform (juce::AffineTransform::scale (pxPerPt));   // now in points
            g.setColour (juce::Colours::black);

            float y = margin;
            if (pageNo == 0)
            {
                g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
                g.drawText (title, juce::Rectangle<float> (margin, y, pw - 2 * margin, 26.0f), juce::Justification::centred);
                y += 26.0f;
                if (auto artist = score[tids::artist].toString(); artist.isNotEmpty())
                {
                    g.setFont (juce::FontOptions (12.0f));
                    g.drawText (artist, juce::Rectangle<float> (margin, y, pw - 2 * margin, 16.0f), juce::Justification::centred);
                    y += 16.0f;
                }
                juce::StringArray tuningNames;
                for (int note : tab::trackTuning (track)) tuningNames.add (theory::midiNoteName (note));
                juce::String info = track[tids::name].toString() + "    Tuning: " + tuningNames.joinIntoString (" ");
                if ((int) track[tids::capo] > 0) info << "    Capo " << (int) track[tids::capo];
                g.setFont (juce::FontOptions (9.5f));
                g.drawText (info, juce::Rectangle<float> (margin, y + 2.0f, pw - 2 * margin, 13.0f), juce::Justification::left);
                y += 20.0f;
            }

            // systems that fit on this page
            const float startY = y;
            const float available = (ph - margin - 14.0f - startY) / scale;
            const int firstRowY = r < rows.size() ? rows[r].y : 0;
            size_t end = r;
            while (end < rows.size() && (float) (rows[end].y + rows[end].height - firstRowY) <= available)
                ++end;
            if (end == r && r < rows.size()) end = r + 1;   // a system taller than a page: print it anyway
            if (r < rows.size())
            {
                const int lastY = rows[end - 1].y + rows[end - 1].height;
                juce::Graphics::ScopedSaveState save (g);
                g.addTransform (juce::AffineTransform::translation (0.0f, (float) -firstRowY).scaled (scale).translated (margin, startY));
                g.reduceClipRegion (0, firstRowY, editorWidth, lastY - firstRowY);
                ed.paintEntireComponent (g, false);
            }
            juce::ignoreUnused (contentH);
            r = end;
            ++pageNo;

            g.setColour (juce::Colour (0xff666666));
            g.setFont (juce::FontOptions (8.5f));
            g.drawText (title + "  -  " + track[tids::name].toString() + "  -  page " + juce::String (pageNo),
                        juce::Rectangle<float> (margin, ph - margin + 6.0f, pw - 2 * margin, 12.0f), juce::Justification::centredRight);
            pages.push_back (img);
            if (pages.size() > 400) return juce::Result::fail ("The score is too long to export (more than 400 pages).");
        }
    }
    if (! writeImagesAsPdf (pages, pw, ph, title, file))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}
} // namespace ff
