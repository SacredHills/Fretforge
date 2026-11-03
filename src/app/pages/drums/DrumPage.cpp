#include "DrumPage.h"
#include "engine/TabSynth.h"
#include "engine/DrumSynth.h"
#include "core/Timeline.h"
#include "core/ProjectFile.h"
#include <cmath>

namespace ff
{
namespace dids = drums::ids;

namespace
{
juce::Colour laneColour (int lane)
{
    using namespace drums;
    if (lane <= kick2) return juce::Colour (0xffe8833a);
    if (lane <= clap) return juce::Colour (0xffe05d5d);
    if (lane <= hhOpen) return juce::Colour (0xffe6c84a);
    if (lane <= tomFloor) return juce::Colour (0xff6cc070);
    return juce::Colour (0xff5ca8e6);
}

const char* sigNames[] = { "4/4", "3/4", "2/4", "5/4", "6/4", "7/4", "6/8", "7/8", "9/8", "12/8", "5/8" };
const std::pair<int, const char*> grids[] = { { 2, "1/8" }, { 3, "1/8 triplets" }, { 4, "1/16" }, { 6, "1/16 triplets" }, { 8, "1/32" } };
} // namespace

// ================================================================== grid

DrumGrid::DrumGrid (ProjectDocument& d, EngineHost& h) : doc (d), host (h) {}

void DrumGrid::setPattern (juce::ValueTree p)
{
    pat = p;
    updateSize (getParentComponent() != nullptr ? getParentComponent()->getWidth() : getWidth());
    repaint();
}

int DrumGrid::steps() const
{
    return pat.isValid() ? juce::jmax (1, drums::lengthTicks (pat) / drums::stepTicks (pat)) : 16;
}

void DrumGrid::updateSize (int visibleWidth)
{
    const int n = steps();
    cellW = juce::jlimit (12.0f, 44.0f, (float) (visibleWidth - nameWidth - 10) / (float) n);
    setSize (juce::jmax (visibleWidth, nameWidth + (int) std::ceil (cellW * (float) n) + 10), headerHeight + drums::numPieces * laneHeight + 6);
}

juce::ValueTree DrumGrid::hitInCell (int lane, int step) const
{
    const int st = drums::stepTicks (pat);
    for (auto h : pat)
        if ((int) h[dids::lane] == lane && (int) h[dids::tick] >= step * st && (int) h[dids::tick] < (step + 1) * st)
            return h;
    return {};
}

void DrumGrid::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    const int lanes = drums::numPieces;
    const float gridRight = (float) nameWidth + cellW * (float) steps();
    for (int l = 0; l < lanes; ++l)
    {
        const int y = headerHeight + l * laneHeight;
        g.setColour (l % 2 ? palette::panel.withAlpha (0.55f) : palette::panel.withAlpha (0.25f));
        g.fillRect (0.0f, (float) y, gridRight, (float) laneHeight);
        g.setColour (laneColour (l));
        g.fillRect (2, y + 4, 3, laneHeight - 8);
        const bool sampled = laneHasSample && laneHasSample (l);
        g.setColour (sampled ? palette::accent : palette::text);
        g.setFont (uiFont (12.0f, sampled));
        g.drawText (juce::String (drums::pieceInfo (l).name) + (sampled ? " *" : ""), 10, y, nameWidth - 14, laneHeight,
                    juce::Justification::centredLeft);
    }
    if (! pat.isValid())
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (15.0f));
        g.drawFittedText ("No patterns yet. Click \"New\" for an empty pattern or \"Generate as new\" for a groove.",
                          getLocalBounds().withTrimmedLeft (nameWidth).withHeight (220), juce::Justification::centred, 2);
        return;
    }

    const int st = drums::stepTicks (pat);
    const int beatTicks = drums::ppq * 4 / juce::jmax (1, (int) pat[dids::tsDen]);
    const int barTicks = drums::barTicks (pat);
    const int n = steps();
    const float bottom = (float) (headerHeight + lanes * laneHeight);
    for (int s = 0; s <= n; ++s)
    {
        const int tick = s * st;
        const float x = (float) nameWidth + cellW * (float) s;
        const bool bar = tick % barTicks == 0, beat = tick % beatTicks == 0;
        g.setColour (bar ? palette::textDim : beat ? palette::outline.brighter (0.4f) : palette::outline);
        g.drawLine (x, (float) headerHeight - (bar ? 6.0f : 0.0f), x, bottom, bar ? 1.6f : 1.0f);
        if (bar && s < n)
        {
            g.setColour (palette::text);
            g.setFont (uiFont (11.0f, true));
            g.drawText (juce::String (tick / barTicks + 1), (int) x + 3, 2, 30, 14, juce::Justification::left);
        }
        else if (beat && s < n)
        {
            g.setColour (palette::textDim);
            g.setFont (uiFont (10.0f));
            g.drawText (juce::String ((tick % barTicks) / beatTicks + 1), (int) x + 3, 3, 20, 13, juce::Justification::left);
        }
    }

    for (auto h : pat)
    {
        const int lane = (int) h[dids::lane];
        if (! juce::isPositiveAndBelow (lane, lanes)) continue;
        const float vel = juce::jlimit (1, 127, (int) h[dids::vel]) / 127.0f;
        const float x = (float) nameWidth + cellW * (float) (int) h[dids::tick] / (float) st;
        const float y = (float) (headerHeight + lane * laneHeight);
        auto cell = juce::Rectangle<float> (x + 1.5f, y + 2.0f, juce::jmax (4.0f, cellW - 3.0f), (float) laneHeight - 4.0f);
        g.setColour (laneColour (lane).withAlpha (0.25f));
        g.fillRoundedRectangle (cell, 3.0f);
        auto level = cell.withTrimmedTop (cell.getHeight() * (1.0f - vel));
        g.setColour (laneColour (lane).withAlpha (0.55f + 0.45f * vel));
        g.fillRoundedRectangle (level, 3.0f);
        if (vel < 0.4f)
        {   // ghost note marker
            g.setColour (palette::bg);
            g.drawRoundedRectangle (cell.reduced (2.0f), 2.0f, 1.0f);
        }
    }

    if (playQ >= 0.0)
    {
        const float x = (float) nameWidth + cellW * (float) (playQ * drums::ppq / st);
        g.setColour (palette::accent);
        g.drawLine (x, 0.0f, x, bottom, 2.0f);
    }
}

void DrumGrid::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    moved = false;
    const int lane = laneAt (e.y);
    if (! juce::isPositiveAndBelow (lane, (int) drums::numPieces) || e.y < headerHeight) return;
    if (e.x < nameWidth)
    {
        if (e.mods.isPopupMenu()) { if (onLaneMenu) onLaneMenu (lane); }
        else host.engine.previewDrum (lane, 0.9f);
        return;
    }
    if (! pat.isValid()) return;
    const int step = stepAt (e.x);
    if (! juce::isPositiveAndBelow (step, steps())) return;
    auto hit = hitInCell (lane, step);

    if (e.mods.isPopupMenu())
    {
        if (! hit.isValid()) return;
        juce::PopupMenu m;
        m.addItem (1, "Ghost (40)");
        m.addItem (2, "Soft (70)");
        m.addItem (3, "Normal (100)");
        m.addItem (4, "Accent (127)");
        m.addSeparator();
        m.addItem (5, "Delete");
        juce::Component::SafePointer<DrumGrid> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, hit] (int r) mutable {
            if (safe == nullptr || r == 0) return;
            safe->doc.undo().beginNewTransaction ("Drum velocity");
            if (r == 5) hit.getParent().removeChild (hit, &safe->doc.undo());
            else hit.setProperty (dids::vel, r == 1 ? 40 : r == 2 ? 70 : r == 3 ? 100 : 127, &safe->doc.undo());
        });
        return;
    }

    doc.undo().beginNewTransaction ("Edit drums");
    if (hit.isValid())
    {
        drag = Drag::velocity;
        dragHit = hit;
        dragStartVel = (int) hit[dids::vel];
        return;
    }
    paintVel = e.mods.isShiftDown() ? 40 : e.mods.isAltDown() ? 127 : 100;
    drums::setHit (pat, lane, step * drums::stepTicks (pat), paintVel, &doc.undo());
    host.engine.previewDrum (lane, paintVel / 127.0f);
    drag = Drag::paint;
    dragLane = lane;
}

void DrumGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 3) moved = true;
    if (drag == Drag::velocity && dragHit.isValid())
    {
        const int v = juce::jlimit (1, 127, dragStartVel - e.getDistanceFromDragStartY());
        dragHit.setProperty (dids::vel, v, &doc.undo());
    }
    else if (drag == Drag::paint)
    {
        const int step = stepAt (e.x);
        if (juce::isPositiveAndBelow (step, steps()) && ! hitInCell (dragLane, step).isValid())
            drums::setHit (pat, dragLane, step * drums::stepTicks (pat), paintVel, &doc.undo());
    }
}

void DrumGrid::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::velocity && ! moved && dragHit.isValid())
        dragHit.getParent().removeChild (dragHit, &doc.undo());   // plain click on a hit removes it
    drag = Drag::none;
    dragHit = {};
}

// ================================================================== song strip

SongStrip::SongStrip (ProjectDocument& d) : doc (d) {}

void SongStrip::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    blocks.clear();
    auto songTree = drums::song (drums);
    if (! songTree.isValid() || songTree.getNumChildren() == 0)
    {
        g.setColour (palette::textDim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Song arrangement is empty: select a pattern and click \"+ Add pattern\". Sections play left to right.",
                    getLocalBounds().reduced (10, 0), juce::Justification::centredLeft);
        return;
    }
    double totalQ = 0.0;
    std::vector<double> lens;
    for (auto s : songTree)
    {
        auto p = drums::patternById (drums, s[dids::pattern].toString());
        const double q = p.isValid() ? drums::lengthTicks (p) / (double) drums::ppq * (int) s.getProperty (dids::repeats, 1) : 4.0;
        lens.push_back (q);
        totalQ += q;
    }
    const float w = (float) getWidth() - 12.0f;
    const float minW = 70.0f;
    float scale = w / (float) juce::jmax (1.0, totalQ);
    float x = 6.0f;
    int i = 0;
    for (auto s : songTree)
    {
        auto p = drums::patternById (drums, s[dids::pattern].toString());
        const float bw = juce::jmax (minW, (float) lens[(size_t) i] * scale);
        auto r = juce::Rectangle<float> (x, 6.0f, bw - 4.0f, (float) getHeight() - 12.0f);
        blocks.push_back (r);
        g.setColour (i == selected ? palette::accent.withAlpha (0.45f) : palette::well);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (i == selected ? palette::accent : palette::outline.brighter (0.3f));
        g.drawRoundedRectangle (r, 4.0f, 1.2f);
        g.setColour (p.isValid() ? palette::text : palette::bad);
        g.setFont (uiFont (12.0f, true));
        g.drawText (p.isValid() ? p[dids::name].toString() : juce::String ("(missing)"), r.reduced (6.0f, 4.0f).removeFromTop (16.0f),
                    juce::Justification::left);
        g.setColour (palette::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText ("x" + juce::String ((int) s.getProperty (dids::repeats, 1)), r.reduced (6.0f, 4.0f).removeFromBottom (14.0f),
                    juce::Justification::left);
        x += bw;
        ++i;
    }
    if (playQ >= 0.0)
    {
        // playhead: find the block and the fraction inside it
        double acc = 0.0;
        for (size_t k = 0; k < lens.size() && k < blocks.size(); ++k)
        {
            if (playQ < acc + lens[k])
            {
                const float px = blocks[k].getX() + blocks[k].getWidth() * (float) ((playQ - acc) / lens[k]);
                g.setColour (palette::accent);
                g.drawLine (px, 2.0f, px, (float) getHeight() - 2.0f, 2.0f);
                break;
            }
            acc += lens[k];
        }
    }
}

void SongStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i].contains (e.position))
        {
            selected = i;
            if (onSelect) onSelect (i);
            repaint();
            return;
        }
}

// ================================================================== page

DrumPage::DrumPage (ProjectDocument& d, EngineHost& h, SessionController& s, juce::PropertiesFile& st)
    : doc (d), host (h), session (s), settings (st), grid (d, h), strip (d)
{
    grid.onLaneMenu = [this] (int lane) { showLaneMenu (lane); };
    grid.laneHasSample = [this] (int lane) { return drums::samplePath (drums, lane).isNotEmpty(); };
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (strip);

    patternCombo.setTooltip ("Pattern to edit");
    patternCombo.onChange = [this] {
        if (updating || patternCombo.getSelectedItemIndex() < 0) return;
        selectPattern (drums::patterns (drums).getChild (patternCombo.getSelectedItemIndex())[dids::id].toString());
    };
    newButton.setTooltip ("New empty pattern (meter and length of the current one)");
    newButton.onClick = [this] {
        auto cur = current();
        doc.undo().beginNewTransaction ("New drum pattern");
        auto p = drums::addPattern (drums, "Pattern " + juce::String (drums::patterns (drums).getNumChildren() + 1),
                                    cur.isValid() ? (int) cur[dids::tsNum] : 4, cur.isValid() ? (int) cur[dids::tsDen] : 4,
                                    cur.isValid() ? (int) cur[dids::bars] : 1, &doc.undo());
        selectPattern (p[dids::id].toString());
    };
    dupButton.onClick = [this] {
        auto cur = current();
        if (! cur.isValid()) return;
        auto c = cur.createCopy();
        c.setProperty (dids::id, timeline::newId(), nullptr);
        c.setProperty (dids::name, cur[dids::name].toString() + " copy", nullptr);
        doc.undo().beginNewTransaction ("Duplicate drum pattern");
        drums::patterns (drums).addChild (c, drums::patterns (drums).indexOf (cur) + 1, &doc.undo());
        selectPattern (c[dids::id].toString());
    };
    renameButton.onClick = [this] {
        auto cur = current();
        if (! cur.isValid()) return;
        auto* w = new juce::AlertWindow ("Rename pattern", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", cur[dids::name].toString());
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<DrumPage> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, cur] (int r) {
            const auto n = w->getTextEditorContents ("name").trim();
            if (safe == nullptr || r != 1 || n.isEmpty()) return;
            safe->doc.undo().beginNewTransaction ("Rename pattern");
            safe->doc.set (cur, dids::name, n);
        }), true);
    };
    deleteButton.onClick = [this] {
        auto cur = current();
        if (! cur.isValid()) return;
        doc.undo().beginNewTransaction ("Delete drum pattern");
        const auto id = cur[dids::id].toString();
        auto songTree = drums::song (drums);
        for (int i = songTree.getNumChildren(); --i >= 0;)
            if (songTree.getChild (i)[dids::pattern].toString() == id) songTree.removeChild (i, &doc.undo());
        const int idx = drums::patterns (drums).indexOf (cur);
        drums::patterns (drums).removeChild (cur, &doc.undo());
        auto next = drums::patterns (drums).getChild (juce::jmax (0, idx - 1));
        selectPattern (next.isValid() ? next[dids::id].toString() : juce::String());
    };

    for (int i = 0; i < (int) std::size (sigNames); ++i) sigCombo.addItem (sigNames[i], i + 1);
    sigCombo.setTooltip ("Time signature of the pattern");
    sigCombo.onChange = [this] {
        auto cur = current();
        if (updating || ! cur.isValid()) return;
        const auto t = juce::StringArray::fromTokens (sigCombo.getText(), "/", "");
        doc.undo().beginNewTransaction ("Pattern meter");
        doc.set (cur, dids::tsNum, t[0].getIntValue());
        doc.set (cur, dids::tsDen, t[1].getIntValue());
        drums::setBars (cur, (int) cur[dids::bars], &doc.undo());  // drop hits past the new end
    };
    for (int b : { 1, 2, 4, 8, 16 }) barsCombo.addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
    barsCombo.setTooltip ("Pattern length");
    barsCombo.onChange = [this] {
        auto cur = current();
        if (updating || ! cur.isValid()) return;
        doc.undo().beginNewTransaction ("Pattern length");
        drums::setBars (cur, barsCombo.getSelectedId(), &doc.undo());
    };
    for (auto& [n, name] : grids) gridCombo.addItem (name, n);
    gridCombo.setTooltip ("Editing grid (hits keep their exact position when you change it)");
    gridCombo.onChange = [this] { if (! updating && current().isValid()) doc.set (current(), dids::grid, gridCombo.getSelectedId(), false); };

    auto setupSlider = [] (juce::Slider& sl, double lo, double hi, double step, const juce::String& suffix) {
        sl.setSliderStyle (juce::Slider::LinearBar);
        sl.setRange (lo, hi, step);
        sl.setTextValueSuffix (suffix);
        sl.setColour (juce::Slider::trackColourId, palette::well);
    };
    setupSlider (swingSlider, 0.0, 50.0, 1.0, "% swing");
    swingSlider.setTooltip ("Swing: delays off-beat 8ths (or 16ths on a 16th grid)");
    swingSlider.onValueChange = [this] { if (! updating && current().isValid()) doc.set (current(), dids::swing, swingSlider.getValue() / 100.0, false); };
    setupSlider (tempoSlider, 20.0, 400.0, 1.0, " BPM");
    tempoSlider.setTooltip ("Project tempo (shared with the Studio)");
    tempoSlider.onDragStart = [this] { doc.undo().beginNewTransaction ("Tempo"); };
    tempoSlider.onValueChange = [this] { if (! updating) doc.setTempo (tempoSlider.getValue()); };

    playButton.onClick = [this] {
        if (host.engine.isPlaying() && ! songMode) { host.engine.stop(); return; }
        songMode = false;
        host.engine.stop();
        rebuildSequence();
        pushEngine();
        host.engine.locateSeconds (0.0);
        host.engine.play();
    };
    playSongButton.onClick = [this] {
        if (host.engine.isPlaying() && songMode) { host.engine.stop(); return; }
        if (drums::song (drums).getNumChildren() == 0) { message ("Play song", "Add patterns to the song arrangement first."); return; }
        songMode = true;
        host.engine.stop();
        rebuildSequence();
        pushEngine();
        host.engine.locateSeconds (0.0);
        host.engine.play();
    };
    loopButton.setClickingTogglesState (true);
    loopButton.setToggleState (true, juce::dontSendNotification);
    loopButton.setTooltip ("Loop the pattern / song");
    loopButton.onClick = [this] { pushEngine(); };
    clickButton.setClickingTogglesState (true);
    clickButton.setToggleState (settings.getBoolValue ("drumClick", false), juce::dontSendNotification);
    clickButton.onClick = [this] { settings.setValue ("drumClick", clickButton.getToggleState()); pushEngine(); };

    for (int g = 0; g < drums::numGenres; ++g) genreCombo.addItem (drums::genreName (g), g + 1);
    genreCombo.setSelectedId (settings.getIntValue ("drumGenre", 1), juce::dontSendNotification);
    genreCombo.onChange = [this] { settings.setValue ("drumGenre", genreCombo.getSelectedId()); };
    genreCombo.setTooltip ("Style for the generator");
    for (auto* b : { &halfButton, &fillButton, &crashButton }) b->setClickingTogglesState (true);
    fillButton.setToggleState (true, juce::dontSendNotification);
    crashButton.setToggleState (true, juce::dontSendNotification);
    halfButton.setTooltip ("Half-time feel (backbeat on 3)");
    fillButton.setTooltip ("Put a fill at the end of the last bar");
    crashButton.setTooltip ("Crash cymbal on the first beat");
    setupSlider (complexitySlider, 0.0, 100.0, 1.0, "% busy");
    complexitySlider.setValue (50.0, juce::dontSendNotification);
    complexitySlider.setTooltip ("Complexity: syncopation, ghost notes, open hats, kick density, blast variations");
    generateButton.setTooltip ("Replace the current pattern with a generated groove (each click gives a new variation; undoable)");
    generateButton.onClick = [this] { generate (false); };
    generateNewButton.setTooltip ("Generate a groove into a new pattern");
    generateNewButton.onClick = [this] { generate (true); };

    setupSlider (humanTimeSlider, 0.0, 30.0, 0.5, " ms");
    humanTimeSlider.setTooltip ("Humanize timing: random push/pull of each hit (same every playback)");
    humanTimeSlider.onValueChange = [this] { if (! updating) doc.set (drums, dids::humanizeMs, humanTimeSlider.getValue(), false); };
    setupSlider (humanVelSlider, 0.0, 50.0, 1.0, "% vel");
    humanVelSlider.setTooltip ("Humanize velocity: random variation of each hit's strength");
    humanVelSlider.onValueChange = [this] { if (! updating) doc.set (drums, dids::humanizeVel, humanVelSlider.getValue() / 100.0, false); };
    setupSlider (volumeSlider, 0.0, 150.0, 1.0, "%");
    volumeSlider.setTooltip ("Drum kit volume");
    volumeSlider.onValueChange = [this] { if (! updating) doc.set (drums, dids::volume, volumeSlider.getValue() / 100.0, false); };

    importButton.setTooltip ("Import a whole MIDI drum file: repeating parts become patterns, arranged as song sections");
    importButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> ("Import MIDI drums", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mid;*.midi");
        juce::Component::SafePointer<DrumPage> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
            if (safe == nullptr || fc.getResult() == juce::File()) return;
            safe->doc.undo().beginNewTransaction ("Import MIDI drums");
            auto r = drums::importMidi (safe->drums, fc.getResult(), &safe->doc.undo());
            if (r.result.failed()) { safe->message ("Import failed", r.result.getErrorMessage()); return; }
            if (r.tempo > 0.0) safe->doc.setTempo (r.tempo);
            safe->selectPattern (r.firstPatternId);
            juce::String msg;
            msg << "Imported " << r.bars << " bars (" << r.hits << " hits) as " << r.patterns << (r.patterns == 1 ? " pattern" : " patterns")
                << " arranged in " << r.sections << (r.sections == 1 ? " song section" : " song sections") << ".";
            if (r.tempo > 0.0) msg << "\nTempo set to " << juce::String (r.tempo, 1) << " BPM.";
            msg << "\nUse \"Play song\" to hear the whole file.";
            if (! r.warnings.isEmpty()) msg << "\n\n" << r.warnings.joinIntoString ("\n");
            safe->message ("Import complete", msg);
        });
    };
    exportButton.setTooltip ("Export the current pattern or the whole song as a General MIDI drum file");
    exportButton.onClick = [this] {
        juce::PopupMenu m;
        m.addItem (1, "Current pattern...", current().isValid());
        m.addItem (2, "Whole song...", drums::song (drums).getNumChildren() > 0);
        juce::Component::SafePointer<DrumPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [safe] (int r) {
            if (safe == nullptr || r == 0) return;
            const auto pat = r == 1 ? safe->current() : juce::ValueTree();
            const auto name = juce::File::createLegalFileName (r == 1 ? pat[dids::name].toString() : juce::String ("Drum song")) + ".mid";
            safe->chooser = std::make_unique<juce::FileChooser> ("Export MIDI drums",
                juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.mid");
            safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                            | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [safe, pat] (const juce::FileChooser& fc) {
                                            if (safe == nullptr || fc.getResult() == juce::File()) return;
                                            const auto f = fc.getResult().withFileExtension (".mid");
                                            auto res = drums::exportMidi (safe->drums, pat, safe->doc.tempo(), f);
                                            safe->message (res.wasOk() ? "Export complete" : "Export failed", res.wasOk() ? "Wrote " + f.getFullPathName() : res.getErrorMessage());
                                        });
        });
    };
    renderButton.setTooltip ("Render the song (or the current pattern) to audio and add it as a new track in the Studio, starting at bar 1");
    renderButton.onClick = [this] { renderToStudio(); };

    addSectionButton.setTooltip ("Append the current pattern to the song (after the selected section)");
    addSectionButton.onClick = [this] {
        auto cur = current();
        if (! cur.isValid()) return;
        doc.undo().beginNewTransaction ("Add song section");
        const int at = strip.selected >= 0 ? strip.selected + 1 : -1;
        drums::addSection (drums, cur[dids::id].toString(), 1, at, &doc.undo());
        strip.selected = at >= 0 ? at : drums::song (drums).getNumChildren() - 1;
    };
    removeSectionButton.onClick = [this] {
        auto songTree = drums::song (drums);
        if (! juce::isPositiveAndBelow (strip.selected, songTree.getNumChildren())) return;
        doc.undo().beginNewTransaction ("Remove song section");
        songTree.removeChild (strip.selected, &doc.undo());
        strip.selected = juce::jmin (strip.selected, songTree.getNumChildren() - 1);
    };
    auto move = [this] (int dir) {
        auto songTree = drums::song (drums);
        const int to = strip.selected + dir;
        if (! juce::isPositiveAndBelow (strip.selected, songTree.getNumChildren()) || ! juce::isPositiveAndBelow (to, songTree.getNumChildren())) return;
        doc.undo().beginNewTransaction ("Move song section");
        songTree.moveChild (strip.selected, to, &doc.undo());
        strip.selected = to;
    };
    leftButton.onClick = [move] { move (-1); };
    rightButton.onClick = [move] { move (1); };
    auto repeats = [this] (int delta) {
        auto sec = drums::song (drums).getChild (strip.selected);
        if (! sec.isValid()) return;
        doc.undo().beginNewTransaction ("Section repeats");
        doc.set (sec, dids::repeats, juce::jlimit (1, 64, (int) sec.getProperty (dids::repeats, 1) + delta));
    };
    lessButton.onClick = [repeats] { repeats (-1); };
    moreButton.onClick = [repeats] { repeats (1); };
    lessButton.setTooltip ("Fewer repeats of the selected section");
    moreButton.setTooltip ("More repeats of the selected section");
    strip.onSelect = [this] (int i) {
        auto sec = drums::song (drums).getChild (i);
        if (sec.isValid()) selectPattern (sec[dids::pattern].toString());
    };

    for (auto* l : { &swingLabel, &tempoLabel, &complexityLabel, &humanLabel, &volumeLabel, &songLabel })
        styleLabel (*l, 12.0f, palette::textDim, true);
    humanLabel.setText ("Humanize", juce::dontSendNotification);
    volumeLabel.setText ("Kit", juce::dontSendNotification);
    styleLabel (status, 12.0f);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &patternCombo, &newButton, &dupButton, &renameButton, &deleteButton, &sigCombo, &barsCombo, &gridCombo, &swingSlider,
             &tempoSlider, &playButton, &playSongButton, &loopButton, &clickButton, &genreCombo, &halfButton, &complexitySlider,
             &fillButton, &crashButton, &generateButton, &generateNewButton, &humanLabel, &humanTimeSlider, &humanVelSlider,
             &volumeLabel, &volumeSlider, &importButton, &exportButton, &renderButton, &addSectionButton, &removeSectionButton,
             &leftButton, &rightButton, &lessButton, &moreButton, &songLabel, &status })
        addAndMakeVisible (c);

    rebind();
    startTimerHz (30);
}

DrumPage::~DrumPage()
{
    root.removeListener (this);
}

void DrumPage::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    drums = drums::getOrCreate (root);
    root.addListener (this);
    strip.setDrums (drums);
    strip.selected = -1;
    refresh();
    rebuildSequence();
    reloadKit (true);
}

juce::ValueTree DrumPage::current() const
{
    auto p = drums::patternById (drums, drums[dids::selected].toString());
    return p.isValid() ? p : drums::patterns (drums).getChild (0);
}

void DrumPage::selectPattern (const juce::String& id)
{
    drums.setProperty (dids::selected, id, nullptr);   // view state: not an undoable edit
    refresh();
    if (! host.engine.isPlaying() || ! songMode) rebuildSequence();
}

void DrumPage::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    auto cur = current();
    patternCombo.clear (juce::dontSendNotification);
    int i = 1;
    for (auto p : drums::patterns (drums))
        patternCombo.addItem (juce::String (i) + ". " + p[dids::name].toString(), i), ++i;
    patternCombo.setSelectedItemIndex (drums::patterns (drums).indexOf (cur), juce::dontSendNotification);
    const bool has = cur.isValid();
    for (auto* c : std::initializer_list<juce::Component*> { &dupButton, &renameButton, &deleteButton, &sigCombo, &barsCombo, &gridCombo,
                                                             &swingSlider, &playButton, &generateButton, &addSectionButton })
        c->setEnabled (has);
    if (has)
    {
        const auto sig = juce::String ((int) cur[dids::tsNum]) + "/" + juce::String ((int) cur[dids::tsDen]);
        sigCombo.setSelectedId (0, juce::dontSendNotification);
        for (int k = 0; k < sigCombo.getNumItems(); ++k)
            if (sigCombo.getItemText (k) == sig) sigCombo.setSelectedItemIndex (k, juce::dontSendNotification);
        if (sigCombo.getSelectedId() == 0) sigCombo.setText (sig, juce::dontSendNotification);
        barsCombo.setSelectedId ((int) cur[dids::bars], juce::dontSendNotification);
        if (barsCombo.getSelectedId() == 0) barsCombo.setText (juce::String ((int) cur[dids::bars]) + " bars", juce::dontSendNotification);
        gridCombo.setSelectedId ((int) cur.getProperty (dids::grid, 4), juce::dontSendNotification);
        swingSlider.setValue ((double) cur.getProperty (dids::swing, 0.0) * 100.0, juce::dontSendNotification);
    }
    tempoSlider.setValue (doc.tempo(), juce::dontSendNotification);
    humanTimeSlider.setValue ((double) drums.getProperty (dids::humanizeMs, 0.0), juce::dontSendNotification);
    humanVelSlider.setValue ((double) drums.getProperty (dids::humanizeVel, 0.0) * 100.0, juce::dontSendNotification);
    volumeSlider.setValue ((double) drums.getProperty (dids::volume, 0.8) * 100.0, juce::dontSendNotification);

    if (grid.pattern() != cur) grid.setPattern (cur);
    else { grid.updateSize (viewport.getMaximumVisibleWidth()); grid.repaint(); }

    const auto songC = drums::compile (drums, {}, 1);
    const double secs = songC.lengthQ * 60.0 / juce::jmax (10.0, doc.tempo());
    songLabel.setText ("Song: " + juce::String (drums::song (drums).getNumChildren()) + " sections, " + juce::String ((int) songC.bars.size())
                           + " bars, " + juce::String ((int) secs / 60) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2),
                       juce::dontSendNotification);
    strip.repaint();
    status.setText (has ? juce::String (cur.getNumChildren()) + " hits" + dotSep() + "click: add / remove" + dotSep() + "Shift-click: ghost"
                              + dotSep() + "Alt-click: accent" + dotSep() + "drag a hit up/down: velocity" + dotSep() + "right-click: presets"
                              + dotSep() + "click a name to hear it, right-click to load a sample" + dotSep() + "Space: play / stop"
                        : juce::String(), juce::dontSendNotification);
}

void DrumPage::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& id)
{
    if (id == dids::selected) return;
    if (node.hasType (dids::KIT)) { reloadKit (false); grid.repaint(); return; }
    if (node == drums || node.isAChildOf (drums) || node == doc.transport())
        triggerAsyncUpdate();
    juce::ignoreUnused (id);
}

// ------------------------------------------------------------------ engine

void DrumPage::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    auto& e = host.engine;
    if (active)
    {
        if (e.isPlaying()) e.stop();
        e.tabMode = true;
        rebuildSequence();
        pushEngine();
        grid.updateSize (viewport.getMaximumVisibleWidth());
    }
    else
    {
        if (e.isPlaying()) e.stop();
        e.tabMode = false;
        doc.refreshEngine();
    }
}

void DrumPage::rebuildSequence()
{
    if (! active) return;
    auto seq = buildDrumSequence (drums, songMode ? juce::ValueTree() : current(), doc.tempo());
    host.engine.submitTabSequence (std::move (seq));
    pushEngine();
}

void DrumPage::pushEngine()
{
    if (! active) return;
    auto& e = host.engine;
    e.punchEnabled = false;
    e.clickWhilePlaying = clickButton.getToggleState();
    const double spq = 60.0 / juce::jmax (10.0, doc.tempo());
    const auto pat = songMode ? juce::ValueTree() : current();
    const double lenQ = drums::compile (drums, pat, 1).lengthQ;
    e.setTempo (doc.tempo());
    if (pat.isValid()) e.setTimeSignature ((int) pat[dids::tsNum], (int) pat[dids::tsDen]);
    e.loopStartSec = 0.0;
    e.loopEndSec = lenQ * spq;
    e.loopEnabled = loopButton.getToggleState() && lenQ > 0.0;
}

void DrumPage::togglePlay()
{
    if (host.engine.isPlaying()) { host.engine.stop(); return; }
    if (songMode) playSongButton.onClick(); else playButton.onClick();
}

void DrumPage::timerCallback()
{
    // samples are resampled to the device rate: reload when it changes (and retry hand-overs that did not fit)
    if (host.engine.getSampleRate() > 0.0 && (std::abs (host.engine.getSampleRate() - loadedRate) > 0.5 || kitDirty))
        reloadKit (std::abs (host.engine.getSampleRate() - loadedRate) > 0.5);
    if (! active) return;
    auto& e = host.engine;
    const bool playing = e.isPlaying();
    playButton.setButtonText (playing && ! songMode ? "Stop" : "Play pattern");
    playSongButton.setButtonText (playing && songMode ? "Stop" : "Play song");
    playButton.setToggleState (playing && ! songMode, juce::dontSendNotification);
    playSongButton.setToggleState (playing && songMode, juce::dontSendNotification);
    if (! playing)
    {
        grid.setPlayheadQ (-1.0);
        if (strip.playQ >= 0.0) { strip.playQ = -1.0; strip.repaint(); }
        return;
    }
    const double q = e.getPositionSeconds() * juce::jmax (10.0, doc.tempo()) / 60.0;
    if (! songMode)
    {
        grid.setPlayheadQ (q);
        return;
    }
    strip.playQ = q;
    strip.repaint();
    // show the playhead in the grid when the section under it is the pattern being edited
    double acc = 0.0;
    double local = -1.0;
    for (auto s : drums::song (drums))
    {
        auto p = drums::patternById (drums, s[dids::pattern].toString());
        if (! p.isValid()) continue;
        const double len = drums::lengthTicks (p) / (double) drums::ppq;
        const int reps = (int) s.getProperty (dids::repeats, 1);
        if (q < acc + len * reps)
        {
            if (p == current()) local = std::fmod (q - acc, len);
            break;
        }
        acc += len * reps;
    }
    grid.setPlayheadQ (local);
}

// ------------------------------------------------------------------ generator & output

void DrumPage::generate (bool asNew)
{
    auto cur = current();
    drums::GenOptions o;
    o.genre = juce::jmax (0, genreCombo.getSelectedId() - 1);
    o.halfTime = halfButton.getToggleState();
    o.complexity = complexitySlider.getValue() / 100.0;
    o.fill = fillButton.getToggleState();
    o.crashOnOne = crashButton.getToggleState();
    o.bars = cur.isValid() ? (int) cur[dids::bars] : 2;
    o.tsNum = cur.isValid() ? (int) cur[dids::tsNum] : 4;
    o.tsDen = cur.isValid() ? (int) cur[dids::tsDen] : 4;
    o.seed = seed++;
    auto g = drums::generate (o);
    if (asNew || ! cur.isValid())
    {
        doc.undo().beginNewTransaction ("Generate drum pattern");
        g.setProperty (dids::name, g[dids::name].toString() + " " + juce::String (drums::patterns (drums).getNumChildren() + 1), nullptr);
        drums::patterns (drums).appendChild (g, &doc.undo());
        selectPattern (g[dids::id].toString());
    }
    else
    {
        doc.undo().beginNewTransaction ("Generate drums");
        drums::replaceHits (cur, g, &doc.undo());
        doc.set (cur, dids::grid, (int) g[dids::grid]);
        doc.set (cur, dids::swing, (double) g[dids::swing]);
    }
    status.setText (juce::String ("Generated ") + drums::genreName (o.genre) + ", variation " + juce::String ((int) o.seed)
                        + " - click Generate again for another one", juce::dontSendNotification);
}

void DrumPage::renderToStudio()
{
    const bool useSong = drums::song (drums).getNumChildren() > 0;
    const auto pat = useSong ? juce::ValueTree() : current();
    if (! useSong && ! pat.isValid()) { message ("Render", "Create a pattern first."); return; }
    const double spq = 60.0 / juce::jmax (10.0, doc.tempo());
    const auto c = drums::compile (drums, pat, 1);
    if (c.events.empty()) { message ("Render", "There are no drum hits to render."); return; }
    std::vector<DrumHit> hits;
    for (auto& e : c.events) hits.push_back ({ juce::jmax (0.0, e.q * spq + e.offsetSec), e.piece, e.velocity });
    std::stable_sort (hits.begin(), hits.end(), [] (const DrumHit& a, const DrumHit& b) { return a.sec < b.sec; });
    const double sr = host.engine.getSampleRate() > 0 ? host.engine.getSampleRate() : 48000.0;
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> owned;
    DrumSynth::SampleSet set {};
    for (int p = 0; p < drums::numPieces; ++p)
        if (auto rel = drums::samplePath (drums, p); rel.isNotEmpty())
            if (auto b = loadSampleFile (project::Bundle::fromPortablePath (doc.mediaBundle(), rel), sr))
            {
                set[(size_t) p] = b.get();
                owned.push_back (std::move (b));
            }
    juce::AudioBuffer<float> buf;
    DrumSynth::renderOffline (hits, sr, c.lengthQ * spq + 3.0, (float) (double) drums.getProperty (dids::volume, 0.8), buf, &set);

    const auto name = useSong ? juce::String ("Drums") : "Drums - " + pat[dids::name].toString();
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("FretForgeRender");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        if (os == nullptr) { message ("Render failed", "Could not write a temporary file."); return; }
        auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
        auto writer = wav.createWriterFor (os, opts);
        if (writer == nullptr || ! writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()))
        { message ("Render failed", "Could not write the WAV file."); return; }
    }
    auto r = session.importFiles ({ file }, {}, 0.0);
    file.deleteFile();
    if (r.failed()) { message ("Render failed", r.getErrorMessage()); return; }
    message ("Rendered to the Studio", "Added the track \"" + name + "\" (" + juce::String (c.lengthQ * spq, 1)
                                           + " s at " + juce::String (doc.tempo(), 1) + " BPM) to the Studio, starting at bar 1.");
}

// ------------------------------------------------------------------ sample kit

std::unique_ptr<juce::AudioBuffer<float>> DrumPage::loadSampleFile (const juce::File& file, double sampleRate)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return nullptr;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int len = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 10.0));
    juce::AudioBuffer<float> raw (channels, len);
    reader->read (&raw, 0, len, 0, true, channels > 1);
    if (std::abs (reader->sampleRate - sampleRate) < 0.5) return std::make_unique<juce::AudioBuffer<float>> (std::move (raw));
    const double ratio = reader->sampleRate / sampleRate;
    const int outLen = (int) std::ceil (len / ratio);
    auto out = std::make_unique<juce::AudioBuffer<float>> (channels, outLen);
    for (int ch = 0; ch < channels; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.process (ratio, raw.getReadPointer (ch), out->getWritePointer (ch), outLen, len, 0);
    }
    return out;
}

void DrumPage::reloadKit (bool force)
{
    const double sr = host.engine.getSampleRate();
    if (sr <= 0.0) { kitDirty = true; return; }
    if (force) for (auto& p : loadedPaths) p = "\x01";   // invalidate
    kitDirty = false;
    juce::StringArray missing;
    for (int p = 0; p < drums::numPieces; ++p)
    {
        const auto rel = drums::samplePath (drums, p);
        if (rel == loadedPaths[(size_t) p]) continue;
        std::unique_ptr<juce::AudioBuffer<float>> buf;
        if (rel.isNotEmpty())
        {
            buf = loadSampleFile (project::Bundle::fromPortablePath (doc.mediaBundle(), rel), sr);
            if (buf == nullptr) missing.add (juce::String (drums::pieceInfo (p).name) + ": " + rel);
        }
        if (! host.engine.setDrumSample (p, std::move (buf))) { kitDirty = true; continue; }  // queue full: retry
        loadedPaths[(size_t) p] = rel;
    }
    loadedRate = sr;
    if (! missing.isEmpty())
        status.setText ("Missing drum samples (built-in sound used): " + missing.joinIntoString ("; "), juce::dontSendNotification);
}

void DrumPage::showLaneMenu (int lane)
{
    const auto name = juce::String (drums::pieceInfo (lane).name);
    const bool has = drums::samplePath (drums, lane).isNotEmpty();
    juce::PopupMenu m;
    m.addSectionHeader (name + (has ? " - sample: " + juce::File::createFileWithoutCheckingPath (drums::samplePath (drums, lane)).getFileName()
                                    : juce::String (" - built-in sound")));
    m.addItem (1, "Load sample for " + name + "...");
    m.addItem (2, "Use the built-in sound", has);
    m.addSeparator();
    m.addItem (3, "Load kit from folder (matches file names)...");
    m.addItem (4, "Reset the whole kit to built-in sounds");
    juce::Component::SafePointer<DrumPage> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, lane] (int r) {
        if (safe == nullptr || r == 0) return;
        if (r == 1)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("Sample for " + juce::String (drums::pieceInfo (lane).name),
                                                                 juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, lane] (const juce::FileChooser& fc) {
                if (safe == nullptr || fc.getResult() == juce::File()) return;
                safe->doc.undo().beginNewTransaction ("Drum sample");
                safe->assignSample (lane, fc.getResult());
            });
        }
        else if (r == 2)
        {
            safe->doc.undo().beginNewTransaction ("Built-in drum sound");
            drums::setSamplePath (safe->drums, lane, {}, &safe->doc.undo());
        }
        else if (r == 3) safe->loadKitFolder();
        else if (r == 4)
        {
            safe->doc.undo().beginNewTransaction ("Built-in drum kit");
            for (int p = 0; p < drums::numPieces; ++p) drums::setSamplePath (safe->drums, p, {}, &safe->doc.undo());
        }
    });
}

void DrumPage::assignSample (int piece, const juce::File& file)
{
    auto probe = loadSampleFile (file, 44100.0);
    if (probe == nullptr) { message ("Drum sample", "Could not read " + file.getFileName() + " as audio."); return; }
    project::Bundle::createStructure (doc.mediaBundle());
    auto dir = kitFolder();
    dir.createDirectory();
    auto dst = dir.getChildFile (juce::File::createLegalFileName (juce::String (drums::pieceInfo (piece).name) + " - " + file.getFileName()));
    if (dst != file && ! file.copyFileTo (dst)) { message ("Drum sample", "Could not copy " + file.getFileName() + " into the project."); return; }
    drums::setSamplePath (drums, piece, project::Bundle::toPortablePath (doc.mediaBundle(), dst), &doc.undo());
}

void DrumPage::loadKitFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of drum samples", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    juce::Component::SafePointer<DrumPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc) {
        if (safe == nullptr || ! fc.getResult().isDirectory()) return;
        std::array<juce::File, drums::numPieces> chosen;
        juce::StringArray unknown;
        for (const auto& entry : juce::RangedDirectoryIterator (fc.getResult(), true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"))
        {
            const int piece = drums::pieceForSampleName (entry.getFile().getFileName());
            if (piece < 0) { unknown.add (entry.getFile().getFileName()); continue; }
            if (chosen[(size_t) piece] == juce::File()) chosen[(size_t) piece] = entry.getFile();
        }
        safe->doc.undo().beginNewTransaction ("Load drum kit");
        juce::StringArray mapped;
        for (int p = 0; p < drums::numPieces; ++p)
            if (chosen[(size_t) p] != juce::File())
            {
                safe->assignSample (p, chosen[(size_t) p]);
                mapped.add (juce::String (drums::pieceInfo (p).name) + " = " + chosen[(size_t) p].getFileName());
            }
        juce::String msg = mapped.isEmpty() ? juce::String ("No file names matched a kit piece (use names like kick, snare, hihat open, tom 1, ride bell...).")
                                            : "Loaded " + juce::String (mapped.size()) + " sounds:\n" + mapped.joinIntoString ("\n");
        if (! unknown.isEmpty())
            msg << "\n\nNot recognised (" << unknown.size() << "): " << unknown.joinIntoString (", ").substring (0, 400)
                << "\nRight-click a drum name to assign a file by hand.";
        safe->message ("Drum kit", msg);
    });
}

void DrumPage::message (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (title).withMessage (text).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

// ------------------------------------------------------------------ layout

void DrumPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    g.setColour (palette::panel);
    g.fillRect (getLocalBounds().removeFromTop (84));
    g.setColour (palette::outline);
    g.drawHorizontalLine (42, 0.0f, (float) getWidth());
    g.drawHorizontalLine (84, 0.0f, (float) getWidth());
}

void DrumPage::resized()
{
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop (42).reduced (8, 7);
    patternCombo.setBounds (row1.removeFromLeft (190)); row1.removeFromLeft (4);
    newButton.setBounds (row1.removeFromLeft (48)); row1.removeFromLeft (2);
    dupButton.setBounds (row1.removeFromLeft (72)); row1.removeFromLeft (2);
    renameButton.setBounds (row1.removeFromLeft (62)); row1.removeFromLeft (2);
    deleteButton.setBounds (row1.removeFromLeft (56)); row1.removeFromLeft (10);
    sigCombo.setBounds (row1.removeFromLeft (64)); row1.removeFromLeft (4);
    barsCombo.setBounds (row1.removeFromLeft (76)); row1.removeFromLeft (4);
    gridCombo.setBounds (row1.removeFromLeft (110)); row1.removeFromLeft (4);
    swingSlider.setBounds (row1.removeFromLeft (92)); row1.removeFromLeft (10);
    tempoSlider.setBounds (row1.removeFromLeft (88));
    clickButton.setBounds (row1.removeFromRight (50)); row1.removeFromRight (4);
    loopButton.setBounds (row1.removeFromRight (50)); row1.removeFromRight (4);
    playSongButton.setBounds (row1.removeFromRight (82)); row1.removeFromRight (4);
    playButton.setBounds (row1.removeFromRight (94));

    auto row2 = r.removeFromTop (42).reduced (8, 7);
    genreCombo.setBounds (row2.removeFromLeft (150)); row2.removeFromLeft (4);
    halfButton.setBounds (row2.removeFromLeft (70)); row2.removeFromLeft (2);
    complexitySlider.setBounds (row2.removeFromLeft (92)); row2.removeFromLeft (2);
    fillButton.setBounds (row2.removeFromLeft (40)); row2.removeFromLeft (2);
    crashButton.setBounds (row2.removeFromLeft (76)); row2.removeFromLeft (4);
    generateButton.setBounds (row2.removeFromLeft (76)); row2.removeFromLeft (2);
    generateNewButton.setBounds (row2.removeFromLeft (112)); row2.removeFromLeft (12);
    renderButton.setBounds (row2.removeFromRight (116)); row2.removeFromRight (4);
    exportButton.setBounds (row2.removeFromRight (100)); row2.removeFromRight (2);
    importButton.setBounds (row2.removeFromRight (98)); row2.removeFromRight (10);
    humanLabel.setBounds (row2.removeFromLeft (64));
    humanTimeSlider.setBounds (row2.removeFromLeft (juce::jmax (60, juce::jmin (84, row2.getWidth() / 3)))); row2.removeFromLeft (2);
    humanVelSlider.setBounds (row2.removeFromLeft (juce::jmax (60, juce::jmin (84, row2.getWidth() / 2)))); row2.removeFromLeft (6);
    volumeLabel.setBounds (row2.removeFromLeft (24));
    volumeSlider.setBounds (row2.removeFromLeft (juce::jmax (50, juce::jmin (80, row2.getWidth()))));

    status.setBounds (r.removeFromBottom (24).reduced (10, 0));
    auto songArea = r.removeFromBottom (96).reduced (8, 4);
    auto songButtons = songArea.removeFromTop (28);
    songLabel.setBounds (songButtons.removeFromLeft (260));
    for (auto* b : { &addSectionButton, &removeSectionButton, &leftButton, &rightButton, &lessButton, &moreButton })
    {
        b->setBounds (songButtons.removeFromLeft (b == &addSectionButton ? 104 : b == &removeSectionButton ? 70 : 32));
        songButtons.removeFromLeft (3);
    }
    strip.setBounds (songArea);
    viewport.setBounds (r.reduced (0, 2));
    grid.updateSize (viewport.getMaximumVisibleWidth());
}
} // namespace ff
