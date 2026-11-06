#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace td;
static juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
static const double loopSizes[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };

juce::AudioProcessorValueTreeState::ParameterLayout TekkdeckProcessor::makeLayout()
{
    using F = juce::AudioParameterFloat; using C = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    const auto fl = [&] (const char* id, const char* name, float mn, float mx, float def, float step = 0.0f)
    { l.add (std::make_unique<F> (juce::ParameterID { id, 1 }, U (name), juce::NormalisableRange<float> (mn, mx, step), def)); };
    const auto ch = [&] (const char* id, const char* name, juce::StringArray items, int def)
    { l.add (std::make_unique<C> (juce::ParameterID { id, 1 }, U (name), items, def)); };
    fl ("gain", "Pegel", -36, 6, -6);
    ch ("mode", "Modus", { "Stretch", "Tempo + Pitch" }, 0);
    fl ("pitch", "Pitch", -12, 12, 0, 1);
    ch ("keytarget", "Zielton", { "Aus", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H" }, 0);
    fl ("low", "Bass", -30, 6, 0); fl ("mid", "Mitten", -30, 6, 0); fl ("high", "Höhen", -30, 6, 0);
    fl ("filter", "Filter", -1, 1, 0);
    ch ("quant", "Sprung", { "Beat", "1 Takt", "2 Takte", "4 Takte" }, 1);
    ch ("loopsize", "Loop-Länge", { "1/4", "1/2", "1", "2", "4", "8", "16", "32" }, 4);
    ch ("loop", "Loop", { "Aus", "An" }, 0);
    ch ("clock", "Takt", { "Mit FL", "Frei" }, 0);
    ch ("play", "Play", { "Stop", "Play" }, 0);
    return l;
}

TekkdeckProcessor::TekkdeckProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Tekkdeck Song"), apvts (*this, nullptr, "TEKKDECK", makeLayout())
{
    for (auto& c : cues) c.store (-1.0);
    formats.registerBasicFormats();
    deck.prepare (44100.0f);
    startThread (juce::Thread::Priority::low);
}
TekkdeckProcessor::~TekkdeckProcessor() { stopThread (8000); }

void TekkdeckProcessor::prepareToPlay (double sampleRate, int) { rate = sampleRate; deck.prepare ((float) sampleRate); }

void TekkdeckProcessor::retire (std::shared_ptr<void> d)
{
    const juce::ScopedLock l (retireLock);
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const Old& o) { return now - o.when > 5000; }), retired.end());
    if (d != nullptr) retired.push_back ({ std::move (d), now });
}

void TekkdeckProcessor::loadSong (const juce::File& f)
{
    { const juce::ScopedLock l (pendingLock); pending = f; pendingGrid.reset(); }
    notify();
}

void TekkdeckProcessor::run()
{
    while (! threadShouldExit())
    {
        wait (-1);
        juce::File file; std::unique_ptr<SongGrid> known;
        { const juce::ScopedLock l (pendingLock); file = pending; known = std::move (pendingGrid); pending = juce::File(); }
        if (file == juce::File() || threadShouldExit()) continue;

        busy.store (true); setStatus (U ("Lade ") + file.getFileName());
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
        {
            setStatus (file.existsAsFile() ? U ("Diese Datei lässt sich nicht lesen. Nimm WAV, AIFF, FLAC oder OGG.") : U ("Datei nicht gefunden: ") + file.getFullPathName());
            busy.store (false); ++version; continue;
        }
        const auto total = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1500.0));
        juce::AudioBuffer<float> b (2, total);
        reader->read (&b, 0, total, 0, true, true);
        auto song = std::make_unique<Song>();
        song->rate = (float) reader->sampleRate;
        song->name = file.getFileNameWithoutExtension().toStdString();
        song->path = file.getFullPathName().toStdString();
        song->l.assign (b.getReadPointer (0), b.getReadPointer (0) + total);
        if (reader->numChannels > 1) song->r.assign (b.getReadPointer (1), b.getReadPointer (1) + total);

        std::vector<float> mono ((size_t) total);
        for (int i = 0; i < total; ++i) mono[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i));

        auto peaks = std::make_unique<Peaks>();
        {
            float state = 0;
            const float coef = 1.0f - std::exp (-2.0f * kPi * 180.0f / song->rate);
            for (int i = 0; i < total; i += Peaks::STEP)
            {
                float top = 0, bass = 0;
                for (int k = i; k < juce::jmin (total, i + Peaks::STEP); ++k)
                {
                    state += coef * (mono[(size_t) k] - state);
                    top = juce::jmax (top, std::fabs (mono[(size_t) k])); bass = juce::jmax (bass, std::fabs (state));
                }
                peaks->full.push_back (top); peaks->bass.push_back (bass);
            }
        }
        if (known == nullptr)
        {
            setStatus (U ("Erkenne Tempo, Takt und Tonart ..."));
            known = std::make_unique<SongGrid> (analyzeSong (mono, song->rate));
        }
        installSong (std::move (song), std::move (known), std::move (peaks));
        setStatus ({}); busy.store (false); ++version;
    }
}

void TekkdeckProcessor::installSong (std::unique_ptr<Song> s, std::unique_ptr<SongGrid> g, std::unique_ptr<Peaks> p)
{
    const juce::ScopedLock l (retireLock);
    gridPtr.store (nullptr, std::memory_order_release);
    songPtr.store (s.get(), std::memory_order_release);
    peaksPtr.store (p.get(), std::memory_order_release);
    gridPtr.store (g.get(), std::memory_order_release);
    deck.offset.store ((double) g->firstDownbeat);
    retire (std::shared_ptr<void> (liveSong.release(), [] (void* x) { delete static_cast<Song*> (x); }));
    retire (std::shared_ptr<void> (liveGrid.release(), [] (void* x) { delete static_cast<SongGrid*> (x); }));
    retire (std::shared_ptr<void> (livePeaks.release(), [] (void* x) { delete static_cast<Peaks*> (x); }));
    liveSong = std::move (s); liveGrid = std::move (g); livePeaks = std::move (p);
}

void TekkdeckProcessor::keepPositionOn (const SongGrid& next)
{
    // Gleiche Stelle im Song, nur mit neuer Beat-Zaehlung
    const auto* g = grid(); if (g == nullptr) return;
    const double now = deck.nowBeat.load(), ppq = now - deck.offset.load();
    deck.offset.store (std::round (next.secondsToBeat (g->beatToSeconds (now)) - ppq));
}

void TekkdeckProcessor::installGrid (std::unique_ptr<SongGrid> g)
{
    const juce::ScopedLock l (retireLock);
    gridPtr.store (g.get(), std::memory_order_release);
    retire (std::shared_ptr<void> (liveGrid.release(), [] (void* x) { delete static_cast<SongGrid*> (x); }));
    liveGrid = std::move (g);
    ++version;
}

double TekkdeckProcessor::quantBeats() const
{
    static const double q[] = { 1, 4, 8, 16 };
    return q[(int) pf ("quant")];
}

void TekkdeckProcessor::seekToSeconds (double seconds)
{
    const auto* g = grid(); if (g == nullptr) return;
    double target = std::round (g->secondsToBeat (seconds));
    const double q = quantBeats();
    if (q >= 4.0) target = g->firstDownbeat + 4.0 * std::round ((target - g->firstDownbeat) / 4.0);
    deck.requestJump (target, q);
}

void TekkdeckProcessor::triggerCue (int i)
{
    const auto* g = grid(); if (g == nullptr || i < 0 || i >= NUM_CUES) return;
    const double c = cues[i].load();
    if (c >= 0) seekToSeconds (c);
    else cues[i].store (g->beatToSeconds (std::round (deck.nowBeat.load())));
}
void TekkdeckProcessor::clearCue (int i) { cues[i].store (-1.0); }

int TekkdeckProcessor::autoKeyShift (double bpm) const
{
    const auto* g = grid();
    const int target = (int) pf ("keytarget");
    if (g == nullptr || target == 0) return 0;
    int d = (target - 1 - g->key % 12 + 12) % 12;
    if (d > 6) d -= 12;
    if ((int) pf ("mode") == VARISPEED) d -= (int) std::round (12.0 * std::log2 (bpm / g->bpm));
    return (d % 12 + 18) % 12 - 6;
}

void TekkdeckProcessor::gridDouble()
{
    const auto* g = grid(); if (g == nullptr) return;
    auto n = std::make_unique<SongGrid> (*g);
    n->beats.clear();
    for (size_t i = 0; i + 1 < g->beats.size(); ++i) { n->beats.push_back (g->beats[i]); n->beats.push_back (0.5 * (g->beats[i] + g->beats[i + 1])); }
    n->beats.push_back (g->beats.back());
    n->bpm = g->bpm * 2; n->firstDownbeat = g->firstDownbeat * 2 % 4;
    keepPositionOn (*n);
    installGrid (std::move (n));
}
void TekkdeckProcessor::gridHalve()
{
    const auto* g = grid(); if (g == nullptr) return;
    auto n = std::make_unique<SongGrid> (*g);
    n->beats.clear();
    for (size_t i = (size_t) (g->firstDownbeat % 2); i < g->beats.size(); i += 2) n->beats.push_back (g->beats[i]);
    n->bpm = g->bpm / 2; n->firstDownbeat = g->firstDownbeat / 2 % 4;
    keepPositionOn (*n);
    installGrid (std::move (n));
}
void TekkdeckProcessor::gridNudge (double ms)
{
    const auto* g = grid(); if (g == nullptr) return;
    auto n = std::make_unique<SongGrid> (*g);
    for (auto& b : n->beats) b += ms * 0.001;
    installGrid (std::move (n));
}
void TekkdeckProcessor::gridSetOne()
{
    const auto* g = grid(); if (g == nullptr) return;
    auto n = std::make_unique<SongGrid> (*g);
    const int here = (int) std::round (deck.nowBeat.load());
    n->firstDownbeat = ((here % 4) + 4) % 4;
    // Song-Eins auf die naechste FL-Eins legen
    const double off = deck.offset.load();
    double shift = std::fmod (n->firstDownbeat - off, 4.0);
    if (shift > 2) shift -= 4; else if (shift < -2) shift += 4;
    deck.offset.store (off + shift);
    installGrid (std::move (n));
}

void TekkdeckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            const int note = msg.getNoteNumber();
            if (note >= 36 && note < 36 + NUM_CUES) triggerCue (note - 36);
            else if (note == 44) { auto* p = apvts.getParameter ("loop"); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); }
        }
    }
    double bpm = 170, ppq = 0; bool hostRun = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto p = pos->getPpqPosition()) ppq = *p;
            hostRun = pos->getIsPlaying();
        }
    hostBpm.store (bpm, std::memory_order_relaxed);

    HostClock clk; clk.bpm = bpm;
    if ((int) pf ("clock") == 0) { clk.ppq = ppq; clk.running = hostRun; freePpq = ppq; }
    else
    {
        clk.ppq = freePpq; clk.running = pf ("play") > 0.5f;
        if (clk.running) freePpq += (double) n / rate * bpm / 60.0;
    }

    Controls c;
    c.mode = (int) pf ("mode");
    c.pitch = pf ("pitch") + (float) autoKeyShift (bpm);
    c.gain = juce::Decibels::decibelsToGain (pf ("gain"));
    c.low = pf ("low"); c.mid = pf ("mid"); c.high = pf ("high"); c.filter = pf ("filter");
    c.loopOn = pf ("loop") > 0.5f; c.loopBeats = loopSizes[(int) pf ("loopsize")];
    const bool lp = c.loopOn;
    if (lp && ! lastPlay) deck.loopStart.store (std::floor (deck.nowBeat.load()));   // Loop startet am aktuellen Beat
    lastPlay = lp;

    auto* L = buffer.getWritePointer (0); auto* R = buffer.getWritePointer (1);
    deck.process (L, R, n, song(), grid(), clk, c);
    meterL.store (juce::jmax (buffer.getMagnitude (0, 0, n), meterL.load() * 0.9f), std::memory_order_relaxed);
    meterR.store (juce::jmax (buffer.getMagnitude (1, 0, n), meterR.load() * 0.9f), std::memory_order_relaxed);
}

void TekkdeckProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("SONG"), nullptr);
    if (const auto* s = song(); s != nullptr)
    {
        juce::ValueTree t ("SONG");
        t.setProperty ("path", juce::String::fromUTF8 (s->path.c_str()), nullptr);
        t.setProperty ("offset", deck.offset.load(), nullptr);
        if (const auto* g = grid(); g != nullptr)
        {
            t.setProperty ("beats", juce::MemoryBlock (g->beats.data(), g->beats.size() * sizeof (double)).toBase64Encoding(), nullptr);
            t.setProperty ("bpm", g->bpm, nullptr); t.setProperty ("down", g->firstDownbeat, nullptr);
            t.setProperty ("key", g->key, nullptr); t.setProperty ("steady", g->steady, nullptr);
        }
        for (int i = 0; i < NUM_CUES; ++i) t.setProperty ("cue" + juce::String (i), cues[i].load(), nullptr);
        state.addChild (t, -1, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void TekkdeckProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (auto t = state.getChildWithName ("SONG"); t.isValid())
    {
        auto g = std::make_unique<SongGrid>();
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (t.getProperty ("beats").toString()) && mb.getSize() >= 2 * sizeof (double))
        {
            g->beats.resize (mb.getSize() / sizeof (double));
            std::memcpy (g->beats.data(), mb.getData(), mb.getSize());
            g->bpm = t.getProperty ("bpm"); g->firstDownbeat = t.getProperty ("down");
            g->key = t.getProperty ("key"); g->steady = t.getProperty ("steady");
        }
        else g.reset();
        for (int i = 0; i < NUM_CUES; ++i) cues[i].store ((double) t.getProperty ("cue" + juce::String (i), -1.0));
        { const juce::ScopedLock l (pendingLock); pending = juce::File (t.getProperty ("path").toString()); pendingGrid = std::move (g); }
        notify();
        state.removeChild (t, nullptr);
    }
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* TekkdeckProcessor::createEditor() { return new TekkdeckEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TekkdeckProcessor(); }
