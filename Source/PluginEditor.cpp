#include "PluginEditor.h"
#include "ui/Themes.h"
using namespace td;
using juce::Colour; using juce::Graphics; using juce::Rectangle; using juce::String;

namespace theme
{
    inline Colour plate, plateHi, well, line, ink, dim, hot, acc;
    inline int current = -1;
    inline juce::File choiceFile()   // gemeinsam mit Tekkforge, beide Plugins tragen dasselbe Design
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tekkforge").getChildFile ("design.txt");
    }
    inline void apply (int i)
    {
        i = (i + tk::NUM_THEMES) % tk::NUM_THEMES;
        const auto& t = tk::themeDef (i);
        plate = Colour (t.plate); plateHi = Colour (t.plateHi); well = Colour (t.well); line = Colour (t.line);
        ink = Colour (t.ink); dim = Colour (t.dim); hot = Colour (t.hot); acc = Colour (t.accent[3]);
        current = i;
    }
    inline void load() { if (current < 0) apply (choiceFile().loadFileAsString().getIntValue()); }
    inline void save() { choiceFile().getParentDirectory().createDirectory(); choiceFile().replaceWithText (String (current)); }
    inline juce::Font font (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
}
static String U (const char* s) { return String::fromUTF8 (s); }
static String U (const std::string& s) { return String::fromUTF8 (s.c_str()); }

class DeckLook : public juce::LookAndFeel_V4
{
public:
    DeckLook() { setDefaultSansSerifTypefaceName ("Bahnschrift"); }
    void drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        const auto b = Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f; const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        const auto arc = [&] (float from, float to, Colour col)
        {
            juce::Path p; p.addCentredArc (c.x, c.y, r - 2, r - 2, 0, from, to, true);
            g.setColour (col); g.strokePath (p, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arc (a0, a1, theme::well); arc (a0, ang, theme::acc);
        g.setColour (theme::plateHi); g.fillEllipse (c.x - r + 7, c.y - r + 7, (r - 7) * 2, (r - 7) * 2);
        juce::Path t; t.addRoundedRectangle (-1.5f, -(r - 8), 3.0f, r * 0.42f, 1.5f);
        g.setColour (theme::ink); g.fillPath (t, juce::AffineTransform::rotation (ang).translated (c));
    }
};

class Ticking : public juce::Component { public: virtual void tick() {} };

class Knob : public Ticking
{
public:
    Knob (TekkdeckProcessor& p, const char* id, const char* title) : param (*p.apvts.getParameter (id)), name (U (title))
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRotaryParameters (juce::degreesToRadians (-135.0f) + juce::MathConstants<float>::twoPi, juce::degreesToRadians (135.0f) + juce::MathConstants<float>::twoPi, true);
        addAndMakeVisible (s);
        att = std::make_unique<juce::SliderParameterAttachment> (param, s, nullptr);
        s.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
    }
    void resized() override { auto b = getLocalBounds().removeFromTop (getHeight() - 30); const int z = juce::jmin (46, b.getHeight()); s.setBounds (b.withSizeKeepingCentre (z, z)); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().removeFromBottom (30);
        g.setColour (theme::dim); g.setFont (theme::font (12.0f)); g.drawText (name, b.removeFromTop (14), juce::Justification::centred, false);
        g.setColour (theme::ink); g.drawText (shown, b, juce::Justification::centred, false);
    }
    void tick() override { const auto v = param.getCurrentValueAsText(); if (v != shown) { shown = v; repaint(); } }
private:
    juce::RangedAudioParameter& param; String name, shown; juce::Slider s;
    std::unique_ptr<juce::SliderParameterAttachment> att;
};

class Strip : public Ticking
{
public:
    Strip (TekkdeckProcessor& p, const char* id, int cols = 0) : param (*p.apvts.getParameter (id)), columns (cols)
    {
        items = dynamic_cast<juce::AudioParameterChoice&> (param).choices;
    }
    int value() const { return (int) param.convertFrom0to1 (param.getValue()); }
    void paint (Graphics& g) override
    {
        const int cols = columns > 0 ? columns : items.size(), rows = (items.size() + cols - 1) / cols;
        const float w = (float) getWidth() / (float) cols, h = (float) getHeight() / (float) rows;
        for (int i = 0; i < items.size(); ++i)
        {
            Rectangle<float> r ((float) (i % cols) * w, (float) (i / cols) * h, w, h); r.reduce (1.5f, 1.5f);
            const bool on = i == value();
            g.setColour (on ? theme::acc : theme::well); g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? theme::well : theme::ink); g.setFont (theme::font (12.5f, on));
            g.drawFittedText (items[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.75f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int cols = columns > 0 ? columns : items.size(), rows = (items.size() + cols - 1) / cols;
        const int cx = juce::jlimit (0, cols - 1, e.x * cols / getWidth()), cy = juce::jlimit (0, rows - 1, e.y * rows / getHeight());
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.convertTo0to1 ((float) juce::jmin (items.size() - 1, cy * cols + cx)));
        param.endChangeGesture();
    }
    void tick() override { if (value() != last) { last = value(); repaint(); } }
private:
    juce::RangedAudioParameter& param; int columns, last = -1; juce::StringArray items;
};

class Title : public juce::Component
{
public:
    explicit Title (const char* t) : text (U (t)) { setInterceptsMouseClicks (false, false); }
    void paint (Graphics& g) override { g.setColour (theme::ink); g.setFont (theme::font (14.0f, true)); g.drawText (text, getLocalBounds(), juce::Justification::centredLeft, false); }
private:
    String text;
};

static void drawPeaks (Graphics& g, const Peaks& p, Rectangle<float> b, double fromIdx, double toIdx)
{
    const int cols = (int) b.getWidth(), n = (int) p.full.size();
    const float mid = b.getCentreY(), h = b.getHeight() * 0.48f;
    for (int x = 0; x < cols; ++x)
    {
        const double a = fromIdx + (toIdx - fromIdx) * x / cols, e = fromIdx + (toIdx - fromIdx) * (x + 1) / cols;
        float top = 0, bass = 0;
        for (int i = juce::jmax (0, (int) a); i < juce::jmin (n, juce::jmax ((int) a + 1, (int) e)); ++i)
        { top = juce::jmax (top, p.full[(size_t) i]); bass = juce::jmax (bass, p.bass[(size_t) i]); }
        const float px = b.getX() + (float) x;
        g.setColour (theme::dim.withAlpha (0.55f)); g.fillRect (px, mid - top * h, 1.0f, top * h * 2.0f);
        g.setColour (theme::acc); g.fillRect (px, mid - bass * h, 1.0f, bass * h * 2.0f);
    }
}

/** Ganzer Song. Klick springt im Takt an diese Stelle. */
class Overview : public Ticking
{
public:
    explicit Overview (TekkdeckProcessor& pr) : p (pr) {}
    void tick() override { repaint(); }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        const auto* s = p.song(); const auto* pk = p.peaks(); const auto* gr = p.grid();
        if (s == nullptr || pk == nullptr || gr == nullptr) return;
        auto b = o.reduced (4.0f);
        drawPeaks (g, *pk, b, 0, (double) pk->full.size());
        const double total = (double) s->length() / s->rate;
        for (int i = 0; i < NUM_CUES; ++i)
            if (const double c = p.cues[i].load(); c >= 0)
            {
                const float x = b.getX() + (float) (c / total) * b.getWidth();
                g.setColour (theme::ink); g.fillRect (x, b.getY(), 2.0f, b.getHeight());
                g.setFont (theme::font (11.0f, true)); g.drawText (String (i + 1), (int) x + 3, (int) b.getY(), 14, 12, juce::Justification::centredLeft, false);
            }
        const float x = b.getX() + (float) (gr->beatToSeconds (p.deck.nowBeat.load()) / total) * b.getWidth();
        g.setColour (theme::hot); g.fillRect (x - 1.0f, b.getY(), 2.0f, b.getHeight());
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const auto* s = p.song()) p.seekToSeconds ((double) e.x / getWidth() * s->length() / s->rate);
    }
private:
    TekkdeckProcessor& p;
};

/** Ausschnitt um die aktuelle Stelle mit Beatraster. Die rote Linie ist jetzt. */
class Zoom : public Ticking
{
public:
    explicit Zoom (TekkdeckProcessor& pr) : p (pr) {}
    void tick() override { repaint(); }
    void paint (Graphics& g) override
    {
        auto o = getLocalBounds().toFloat();
        g.setColour (theme::well); g.fillRoundedRectangle (o, 6.0f);
        auto b = o.reduced (4.0f);
        const auto* s = p.song(); const auto* pk = p.peaks(); const auto* gr = p.grid();
        if (s == nullptr || pk == nullptr || gr == nullptr)
        {
            g.setColour (p.busy.load() ? theme::dim : theme::ink); g.setFont (theme::font (20.0f, true));
            g.drawText (p.busy.load() ? p.statusText() : (p.statusText().isNotEmpty() ? p.statusText() : U ("Song hierher ziehen oder laden")), b, juce::Justification::centred, false);
            return;
        }
        const double now = p.deck.nowBeat.load(), span = 12.0;
        const double from = now - span / 2, to = now + span / 2;
        const double a = gr->beatToSeconds (from) * s->rate / Peaks::STEP, e = gr->beatToSeconds (to) * s->rate / Peaks::STEP;
        drawPeaks (g, *pk, b, a, e);
        for (int k = (int) std::ceil (from); k <= (int) to; ++k)
        {
            const float x = b.getX() + (float) ((k - from) / span) * b.getWidth();
            const bool one = ((k - gr->firstDownbeat) % 4 + 4) % 4 == 0;
            g.setColour (one ? theme::ink : theme::line); g.fillRect (x, b.getY(), one ? 2.0f : 1.0f, b.getHeight());
            if (one)
            {
                g.setColour (theme::ink); g.setFont (theme::font (12.0f, true));
                g.drawText (String ((k - gr->firstDownbeat) / 4 + 1), (int) x + 4, (int) b.getY() + 2, 40, 14, juce::Justification::centredLeft, false);
            }
        }
        for (int i = 0; i < NUM_CUES; ++i)
            if (const double c = p.cues[i].load(); c >= 0)
            {
                const double k = gr->secondsToBeat (c);
                if (k < from || k > to) continue;
                const float x = b.getX() + (float) ((k - from) / span) * b.getWidth();
                g.setColour (theme::acc); g.fillRoundedRectangle (x - 1, b.getBottom() - 18, 18, 18, 3.0f);
                g.setColour (theme::well); g.setFont (theme::font (12.0f, true));
                g.drawText (String (i + 1), Rectangle<float> (x - 1, b.getBottom() - 18, 18, 18), juce::Justification::centred, false);
            }
        g.setColour (theme::hot); g.fillRect (b.getCentreX() - 1.0f, b.getY(), 2.0f, b.getHeight());
    }
private:
    TekkdeckProcessor& p;
};

class CuePads : public Ticking
{
public:
    explicit CuePads (TekkdeckProcessor& pr) : p (pr) {}
    Rectangle<int> pad (int i) const
    {
        const int w = getWidth() / 4, h = getHeight() / 2;
        return Rectangle<int> ((i % 4) * w, (i / 4) * h, w, h).reduced (3);
    }
    void paint (Graphics& g) override
    {
        for (int i = 0; i < NUM_CUES; ++i)
        {
            const bool set = p.cues[i].load() >= 0;
            auto r = pad (i).toFloat();
            g.setColour (set ? theme::acc : theme::well); g.fillRoundedRectangle (r, 6.0f);
            g.setColour (set ? theme::well : theme::dim); g.setFont (theme::font (18.0f, true));
            g.drawText (String (i + 1), r, juce::Justification::centred, false);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < NUM_CUES; ++i)
            if (pad (i).contains (e.getPosition()))
            {
                if (e.mods.isPopupMenu()) p.clearCue (i); else p.triggerCue (i);
                repaint();
            }
    }
    void tick() override
    {
        int mask = 0; for (int i = 0; i < NUM_CUES; ++i) mask |= (p.cues[i].load() >= 0 ? 1 : 0) << i;
        if (mask != last) { last = mask; repaint(); }
    }
private:
    TekkdeckProcessor& p; int last = -1;
};

class FixRow : public Ticking
{
public:
    FixRow (TekkdeckProcessor& pr) : p (pr)
    {
        const auto add = [this] (juce::TextButton& b, const char* t, std::function<void()> fn)
        {
            b.setButtonText (U (t)); b.onClick = std::move (fn);
            b.setColour (juce::TextButton::buttonColourId, theme::well); b.setColour (juce::TextButton::textColourOffId, theme::ink);
            b.setColour (juce::ComboBox::outlineColourId, theme::line); addAndMakeVisible (b);
        };
        add (half, "BPM halbieren", [this] { p.gridHalve(); });
        add (twice, "BPM verdoppeln", [this] { p.gridDouble(); });
        add (back, "Raster früher", [this] { p.gridNudge (-5.0); });
        add (fwd, "Raster später", [this] { p.gridNudge (5.0); });
        add (one, "Hier ist die Eins", [this] { p.gridSetOne(); });
    }
    void resized() override
    {
        auto b = getLocalBounds(); const int h = b.getHeight() / 3, w = b.getWidth() / 2;
        auto r = b.removeFromTop (h); half.setBounds (r.removeFromLeft (w).reduced (2)); twice.setBounds (r.reduced (2));
        r = b.removeFromTop (h); back.setBounds (r.removeFromLeft (w).reduced (2)); fwd.setBounds (r.reduced (2));
        one.setBounds (b.reduced (2));
    }
private:
    TekkdeckProcessor& p; juce::TextButton half, twice, back, fwd, one;
};

TekkdeckEditor::TekkdeckEditor (TekkdeckProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    theme::load();
    look = std::make_unique<DeckLook>();
    setLookAndFeel (look.get());
    juce::LookAndFeel::setDefaultLookAndFeel (look.get());
    update = std::make_unique<tk::UpdateBanner> ("brucknerjulian55-netizen/tekkdeck", JucePlugin_VersionString);
    update->fill = [] { return theme::acc; };
    update->text = [] { return theme::well; };
    update->setBounds (254, 34, 170, 24);
    addChildComponent (*update);
    setSize (1280, 720);
    rebuild();
    startTimerHz (30);
}
TekkdeckEditor::~TekkdeckEditor()
{
    stopTimer(); parts.clear();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TekkdeckEditor::rebuild()
{
    lastTheme = theme::current;
    ticking.clear(); parts.clear();
    const auto add = [this] (std::unique_ptr<juce::Component> c, Rectangle<int> b)
    {
        addAndMakeVisible (*c); c->setBounds (b);
        if (auto* t = dynamic_cast<Ticking*> (c.get())) ticking.push_back (t);
        parts.push_back (std::move (c));
    };
    auto& p = proc;
    add (std::make_unique<Overview> (p), { 10, 70, 1260, 60 });
    add (std::make_unique<Zoom> (p), { 10, 136, 1260, 300 });

    add (std::make_unique<Title> ("Cues"), { 12, 444, 200, 20 });
    add (std::make_unique<CuePads> (p), { 8, 464, 400, 180 });
    add (std::make_unique<Title> ("Sprung auf"), { 12, 650, 90, 26 });
    add (std::make_unique<Strip> (p, "quant"), { 100, 650, 308, 26 });
    add (std::make_unique<Strip> (p, "clock"), { 12, 684, 150, 26 });
    add (std::make_unique<Strip> (p, "play"), { 170, 684, 238, 26 });

    add (std::make_unique<Title> ("Loop"), { 424, 444, 200, 20 });
    add (std::make_unique<Strip> (p, "loop"), { 424, 466, 230, 34 });
    add (std::make_unique<Strip> (p, "loopsize", 4), { 424, 504, 230, 80 });
    add (std::make_unique<Title> ("Raster korrigieren"), { 670, 444, 200, 20 });
    add (std::make_unique<FixRow> (p), { 668, 464, 290, 120 });

    add (std::make_unique<Title> ("Tempo-Art"), { 424, 594, 200, 20 });
    add (std::make_unique<Strip> (p, "mode"), { 424, 616, 230, 34 });
    add (std::make_unique<Title> ("Ton anpassen an"), { 670, 594, 200, 20 });
    add (std::make_unique<Strip> (p, "keytarget", 7), { 668, 616, 290, 60 });

    add (std::make_unique<Title> ("Klang"), { 974, 444, 200, 20 });
    const std::pair<const char*, const char*> ks[] = { { "low", "Bass" }, { "mid", "Mitten" }, { "high", "Höhen" }, { "filter", "Filter" }, { "pitch", "Pitch" }, { "gain", "Pegel" } };
    int i = 0;
    for (auto& k : ks)
    {
        add (std::make_unique<Knob> (p, k.first, k.second), { 974 + (i % 3) * 100, 468 + (i / 3) * 104, 96, 96 });
        ++i;
    }
    repaint();
}

void TekkdeckEditor::paint (Graphics& g)
{
    g.fillAll (theme::plate);
    g.setColour (theme::ink); g.setFont (theme::font (21.0f, true));
    g.drawText ("Tekkdeck", 14, 8, 116, 50, juce::Justification::centredLeft, false);

    const auto btn = [&] (Rectangle<int> r, const String& t, bool on)
    {
        g.setColour (on ? theme::acc : theme::well); g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (on ? theme::well : theme::ink); g.setFont (theme::font (13.0f, on));
        g.drawFittedText (t, r.reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    };
    btn (loadBtn, U ("Song laden"), true);
    btn (themeBtn, U (tk::themeDef (theme::current).name), false);

    const auto* s = proc.song(); const auto* gr = proc.grid();
    if (s != nullptr)
    {
        g.setColour (theme::ink); g.setFont (theme::font (15.0f, true));
        g.drawText (U (s->name), 254, 12, 360, 22, juce::Justification::centredLeft, true);
        g.setColour (theme::dim); g.setFont (theme::font (12.5f));
        if (proc.busy.load()) g.drawText (proc.statusText(), 254, 34, 360, 18, juce::Justification::centredLeft, true);
    }
    if (gr != nullptr)
    {
        const double host = proc.hostBpm.load();
        const double change = (host / gr->bpm - 1.0) * 100.0;
        g.setColour (theme::ink); g.setFont (theme::font (19.0f, true));
        g.drawText (String (gr->bpm, 2) + U (" > ") + String (host, 1) + " BPM", 620, 10, 240, 26, juce::Justification::centredLeft, false);
        g.setColour (std::fabs (change) > 30.0 ? theme::hot : theme::dim); g.setFont (theme::font (13.0f));
        g.drawText ((change >= 0 ? "+" : "") + String (change, 1) + " % Tempo" + (gr->steady ? String() : U ("   Tempo schwankt, Raster folgt dem Song")), 620, 36, 300, 18, juce::Justification::centredLeft, true);
        g.setColour (theme::ink); g.setFont (theme::font (17.0f, true));
        const int shift = proc.autoKeyShift (host) + (int) proc.pf ("pitch");
        g.drawText (U (keyName (gr->key)), 930, 10, 150, 26, juce::Justification::centredLeft, false);
        g.setColour (theme::dim); g.setFont (theme::font (13.0f));
        g.drawText (shift != 0 ? (shift > 0 ? "+" : "") + String (shift) + U (" Halbtöne") : U ("Originaltonart"), 930, 36, 150, 18, juce::Justification::centredLeft, false);
    }
    auto m = Rectangle<float> (1086.0f, 16.0f, 74.0f, 32.0f);
    for (float v : { shownL, shownR })
    {
        auto bar = m.removeFromTop (14.0f); m.removeFromTop (4.0f);
        const float n = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (v, -48.0f) + 48.0f) / 48.0f);
        g.setColour (theme::well); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (v >= 0.99f ? theme::hot : theme::acc); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * n), 3.0f);
    }
}

void TekkdeckEditor::resized() {}

void TekkdeckEditor::mouseDown (const juce::MouseEvent& e)
{
    if (loadBtn.contains (e.getPosition())) chooseSong();
    else if (themeBtn.contains (e.getPosition())) { theme::apply (theme::current + 1); theme::save(); }
}

void TekkdeckEditor::chooseSong()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Song laden"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [sp = SafePointer<TekkdeckEditor> (this)] (const juce::FileChooser& c)
                          { if (sp != nullptr && c.getResult().existsAsFile()) sp->proc.loadSong (c.getResult()); });
}

void TekkdeckEditor::filesDropped (const juce::StringArray& f, int, int) { if (! f.isEmpty()) proc.loadSong (juce::File (f[0])); }

void TekkdeckEditor::timerCallback()
{
    if (theme::current != lastTheme) { rebuild(); return; }
    for (auto* t : ticking) t->tick();
    const float l = proc.meterL.load(), r = proc.meterR.load();
    const String info = String (proc.hostBpm.load(), 1) + proc.statusText() + String (proc.version.load()) + String (proc.pf ("pitch")) + String (proc.pf ("keytarget")) + String (proc.pf ("mode"));
    if (l != shownL || r != shownR || info != shownInfo) { shownL = l; shownR = r; shownInfo = info; repaint (0, 0, getWidth(), 66); }
}
