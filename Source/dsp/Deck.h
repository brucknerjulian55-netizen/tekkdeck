// Tekkdeck - Wiedergabe. Jeder Ausgabe-Frame berechnet seine Song-Position neu aus der Host-Position,
// deshalb kann der Song nicht wegdriften: Es gibt keine aufsummierte Abspielposition.
#pragma once
#include "Analysis.h"
#include <atomic>
#include <cstring>
#include <memory>

namespace td
{
struct Song
{
    std::vector<float> l, r;       // r leer = mono
    float rate = 44100;
    std::string name, path;
    int length() const { return (int) l.size(); }
};

enum PlayMode { STRETCH = 0, VARISPEED };

struct Controls
{
    int mode = STRETCH;
    float pitch = 0;          // Halbtoene, im Modus VARISPEED zusaetzlich zur Tempoaenderung
    float gain = 1;
    float low = 0, mid = 0, high = 0;   // dB
    float filter = 0;         // -1 Tiefpass bis +1 Hochpass, 0 aus
    bool loopOn = false;
    double loopBeats = 4;
};

struct HostClock
{
    double bpm = 170, ppq = 0;
    bool running = false;
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] {}, z2[2] {};
    float run (float x, int c) { const float y = b0 * x + z1[c]; z1[c] = b1 * x - a1 * y + z2[c]; z2[c] = b2 * x - a2 * y; return y; }
    void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    enum { LOWSHELF, PEAK, HIGHSHELF, LP, HP };
    void set (int kind, float hz, float db, float q, float sr)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = 2.0f * kPi * std::min (hz, sr * 0.45f) / sr;
        const float c = std::cos (w), s = std::sin (w), al = s / (2.0f * q), sq = 2.0f * std::sqrt (A) * al;
        float B0, B1, B2, A0, A1, A2;
        switch (kind)
        {
            case PEAK: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            case LOWSHELF:
                B0 = A * ((A + 1) - (A - 1) * c + sq); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sq);
                A0 = (A + 1) + (A - 1) * c + sq; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sq; break;
            case HIGHSHELF:
                B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
                A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq; break;
            case LP: B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            default: B0 = (1 + c) / 2; B1 = -1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

class Deck
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        frame = (int) (sr * 0.03f) & ~1;              // 30 ms: kurz genug fuer Kicks
        hop = frame / 2;
        ringSize = 1 << 17;
        ring[0].assign ((size_t) ringSize, 0.0f); ring[1].assign ((size_t) ringSize, 0.0f);
        win.resize ((size_t) frame); a.resize ((size_t) frame); b.resize ((size_t) frame); mono.resize ((size_t) frame); prevMono.resize ((size_t) hop);
        for (int i = 0; i < frame; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) frame);
        reset();
    }
    void reset()
    {
        std::fill (ring[0].begin(), ring[0].end(), 0.0f); std::fill (ring[1].begin(), ring[1].end(), 0.0f);
        readAt = writeAt = 0; haveAhead = 0; lastValid = false;
        for (auto& f : eq) f.clear();
        filt.clear();
    }

    std::atomic<double> offset { 0.0 };          // Song-Beat = FL-Beat + offset
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> nowBeat { 0.0 };         // aktuelle Position fuer die Anzeige
    std::atomic<double> pendingTarget { 0.0 };
    std::atomic<double> quantBeats { 4.0 };
    std::atomic<bool> pendingJump { false };

    /** Springt beim naechsten Raster-Punkt auf einen Song-Beat, ohne den Takt zu verlieren. */
    void requestJump (double songBeat, double quant) { pendingTarget.store (songBeat); quantBeats.store (quant); pendingJump.store (true); }

    void process (float* L, float* R, int n, const Song* song, const SongGrid* grid, const HostClock& clock, const Controls& c)
    {
        std::memset (L, 0, sizeof (float) * (size_t) n); std::memset (R, 0, sizeof (float) * (size_t) n);
        if (song == nullptr || grid == nullptr || grid->beats.size() < 2) { idle (n); return; }

        // Sprung auf den naechsten Rasterpunkt legen
        if (pendingJump.exchange (false))
        {
            const double q = std::max (0.25, quantBeats.load());
            // Bei gestopptem Transport gibt es nichts abzuwarten
            jumpPpq = clock.running ? std::ceil (clock.ppq / q - 1.0e-6) * q : clock.ppq;
            jumpOffset = pendingTarget.load() - jumpPpq;
            haveJump = true;
        }
        if (haveJump && clock.ppq >= jumpPpq) { offset.store (jumpOffset); haveJump = false; if (c.loopOn) loopStart.store (std::floor (jumpPpq + jumpOffset)); }

        nowBeat.store (songBeatAt (clock.ppq, c, grid), std::memory_order_relaxed);

        while (haveAhead < n + hop) makeFrame (song, grid, clock, c);

        for (int i = 0; i < n; ++i)
        {
            const int p = (readAt + i) & (ringSize - 1);
            L[i] = ring[0][(size_t) p]; R[i] = ring[1][(size_t) p];
            ring[0][(size_t) p] = 0; ring[1][(size_t) p] = 0;
        }
        readAt = (readAt + n) & (ringSize - 1);
        haveAhead -= n;
        tone (L, R, n, c);
    }

    /** Song-Position fuer eine Host-Position, mit Loop. Reine Funktion, also frei von Drift. */
    double songBeatAt (double ppq, const Controls& c, const SongGrid*) const
    {
        const double off = haveJump && ppq >= jumpPpq ? jumpOffset : offset.load (std::memory_order_relaxed);
        double b = ppq + off;
        if (c.loopOn)
        {
            const double s = loopStart.load (std::memory_order_relaxed);
            if (b >= s) b = s + std::fmod (b - s, c.loopBeats);
        }
        return b;
    }

    int frameSize() const { return frame; }

private:
    void idle (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int p = (readAt + i) & (ringSize - 1);
            ring[0][(size_t) p] = 0; ring[1][(size_t) p] = 0;
        }
        readAt = (readAt + n) & (ringSize - 1); writeAt = readAt; haveAhead = 0; lastValid = false;
    }

    static float cubic (const std::vector<float>& s, double pos)
    {
        const int i = (int) std::floor (pos);
        if (i < 1 || i + 2 >= (int) s.size()) return 0.0f;
        const float f = (float) (pos - i), y0 = s[(size_t) i - 1], y1 = s[(size_t) i], y2 = s[(size_t) i + 1], y3 = s[(size_t) i + 2];
        return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
    }

    void makeFrame (const Song* song, const SongGrid* grid, const HostClock& clock, const Controls& c)
    {
        // Mittelpunkt dieses Frames liegt so weit in der Zukunft
        const double aheadSec = (double) (haveAhead + frame / 2) / sr;
        const double ppqAt = clock.ppq + aheadSec * clock.bpm / 60.0;
        const double beat = songBeatAt (ppqAt, c, grid);
        const double srcCenter = grid->beatToSeconds (beat) * song->rate;
        const float gain = clock.running ? 1.0f : 0.0f;

        // Lesegeschwindigkeit: im Stretch-Modus nur Tonhoehe, sonst zusaetzlich Songtempo zu Host-Tempo
        double step = (double) song->rate / sr * std::pow (2.0, (double) c.pitch / 12.0);
        if (c.mode == VARISPEED) step *= clock.bpm / grid->localBpm (beat);

        double start = srcCenter - 0.5 * frame * step;
        const double natural = lastStart + hop * step;

        if (lastValid && std::fabs (start - natural) < song->rate * 0.05 && c.mode == VARISPEED)
            start = natural + (start - natural) * 0.05;       // Phase halten, Abweichung sanft ausregeln
        else if (lastValid && std::fabs (start - natural) < song->rate * 0.25)
        {
            // Rund um Viertel und Achtel laeuft der Song kurz im Originaltempo. Der Anschlag bleibt so unveraendert
            // und liegt exakt auf dem Beat. Nur die Abschnitte dazwischen werden gestreckt oder gestaucht.
            const double gridPpq = std::round (ppqAt * 2.0) * 0.5;
            const double gridBeat = songBeatAt (gridPpq, c, grid);
            const double dtSamples = (ppqAt - gridPpq) * 60.0 / clock.bpm * sr;
            const bool onGrid = std::fabs (gridBeat * 2.0 - std::round (gridBeat * 2.0)) < 0.02;
            if (onGrid && dtSamples >= -hop && dtSamples <= 3.0 * hop)
                start = grid->beatToSeconds (gridBeat) * song->rate + (dtSamples - 0.5 * frame) * step;
            else
                start = alignTo (song, start, natural, step);
        }

        for (int i = 0; i < frame; ++i)
        {
            const double pos = start + i * step;
            const float w = win[(size_t) i] * gain;
            const int p = (writeAt + i) & (ringSize - 1);
            ring[0][(size_t) p] += cubic (song->l, pos) * w;
            ring[1][(size_t) p] += (song->r.empty() ? cubic (song->l, pos) : cubic (song->r, pos)) * w;
        }
        lastStart = start; lastValid = gain > 0;
        writeAt = (writeAt + hop) & (ringSize - 1);
        haveAhead += hop;
    }

    double alignTo (const Song* s, double start, double natural, double step)
    {
        const int len = hop, stride = 4;
        const int range = (int) (sr * 0.008f);    // +- 8 ms Suchbereich
        const int count = len / stride;
        for (int i = 0; i < count; ++i)
        {
            const double p = natural + i * stride * step;
            prevMono[(size_t) i] = cubic (s->l, p);
        }
        double best = start, top = -1.0e30;
        for (int d = -range; d <= range; d += 4)
        {
            const double cand = start + d * step;
            double xy = 0, yy = 1.0e-9;
            for (int i = 0; i < count; ++i)
            {
                const double p = cand + i * stride * step;
                const int idx = (int) p;
                const float y = idx >= 0 && idx < (int) s->l.size() ? s->l[(size_t) idx] : 0.0f;
                xy += (double) prevMono[(size_t) i] * y; yy += (double) y * y;
            }
            const double score = xy / std::sqrt (yy);
            if (score > top) { top = score; best = cand; }
        }
        return best;
    }

    void tone (float* L, float* R, int n, const Controls& c)
    {
        const float key = c.low * 3.1f + c.mid * 7.3f + c.high * 11.7f + c.filter * 137.0f;
        if (key != lastKey)
        {
            lastKey = key;
            eq[0].set (Biquad::LOWSHELF, 180.0f, c.low, 0.7f, sr);
            eq[1].set (Biquad::PEAK, 1000.0f, c.mid, 0.7f, sr);
            eq[2].set (Biquad::HIGHSHELF, 4000.0f, c.high, 0.7f, sr);
            if (c.filter < -0.01f) filt.set (Biquad::LP, 20000.0f * std::pow (0.0075f, -c.filter), 0, 0.9f, sr);
            else if (c.filter > 0.01f) filt.set (Biquad::HP, 20.0f * std::pow (150.0f, c.filter), 0, 0.9f, sr);
        }
        const bool f = std::fabs (c.filter) > 0.01f;
        for (int i = 0; i < n; ++i)
        {
            float x = L[i], y = R[i];
            for (auto& e : eq) { x = e.run (x, 0); y = e.run (y, 1); }
            if (f) { x = filt.run (x, 0); y = filt.run (y, 1); }
            gainNow += (c.gain - gainNow) * 0.002f;
            L[i] = x * gainNow; R[i] = y * gainNow;
        }
    }

    float sr = 44100, gainNow = 1, lastKey = 1.0e30f;
    int frame = 1024, hop = 512, ringSize = 1, readAt = 0, writeAt = 0, haveAhead = 0;
    double lastStart = 0, jumpPpq = 0, jumpOffset = 0;
    bool lastValid = false, haveJump = false;
    std::vector<float> ring[2], win, a, b, mono, prevMono;
    Biquad eq[3], filt;
};
} // namespace td
