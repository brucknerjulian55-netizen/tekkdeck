// Prueft Erkennung und Wiedergabe ohne JUCE: g++ -std=c++17 -O2 -ISource Tests/deck_test.cpp -o deck_test
#include "dsp/Deck.h"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace td;

constexpr float RATE = 44100;
static float tone (float hz, float t) { return std::sin (2 * kPi * hz * t); }

/** Einfacher Testsong: Kick auf jedem Beat, Bassnote auf der Eins lauter, Hats dazwischen, A-Moll-Akkord darueber.
    drift > 0 laesst das Tempo langsam schwanken wie bei einer Live-Band. */
static std::vector<float> makeSong (double bpm, double seconds, double startAt, double drift, std::vector<double>* truth = nullptr, int rootSemi = 0)
{
    const int n = (int) (seconds * RATE);
    std::vector<float> x ((size_t) n, 0.0f);
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    double t = startAt; int beat = 0;
    const float scale = std::pow (2.0f, (float) rootSemi / 12.0f);
    while (t < seconds)
    {
        if (truth != nullptr) truth->push_back (t);
        const int at = (int) (t * RATE);
        const bool one = beat % 4 == 0;
        const double T = 60.0 / bpm * (1.0 + drift * std::sin (2 * M_PI * t / 23.0));
        for (int i = 0; i < (int) (0.2f * RATE) && at + i < n; ++i)
        {
            const float s = (float) i / RATE, e = std::exp (-s * 16.0f);
            x[(size_t) (at + i)] += (one ? 1.0f : 0.62f) * e * tone (50.0f + 120.0f * std::exp (-s * 60.0f), s);
        }
        const int hat = at + (int) (T * 0.5 * RATE);
        for (int i = 0; i < (int) (0.03f * RATE) && hat + i < n; ++i)
            x[(size_t) (hat + i)] += 0.1f * std::exp (-(float) i / RATE * 120.0f) * uni (rng);
        // Akkorde wechseln pro Takt: A-Moll, F, C, G (typisch fuer Moll-Pop)
        static const float prog[4][3] = { { 220.0f, 261.63f, 329.63f }, { 174.61f, 220.0f, 261.63f }, { 261.63f, 329.63f, 392.0f }, { 196.0f, 246.94f, 293.66f } };
        const auto& ch = prog[(beat / 4) % 4];
        for (int i = 0; i < (int) (T * RATE) && at + i < n; ++i)
        {
            const float s = (float) i / RATE;
            x[(size_t) (at + i)] += 0.06f * std::exp (-s * 3.0f) * (tone (ch[0] * scale, s) + tone (ch[1] * scale, s) + tone (ch[2] * scale, s));
        }
        t += T; ++beat;
    }
    return x;
}

static double measureBeatError (const SongGrid& g, const std::vector<double>& truth)
{
    double worst = 0, sum = 0; int cnt = 0;
    for (auto t : truth)
    {
        if (t < g.beats.front() || t > g.beats.back()) continue;
        double near = 1e9;
        for (auto b : g.beats) if (std::fabs (b - t) < std::fabs (near)) near = b - t;
        worst = std::max (worst, std::fabs (near)); sum += near; ++cnt;
    }
    std::printf ("  Raster gegen Soll: Mittel %+.1f ms\n", sum / cnt * 1000);
    return worst;
}

int main()
{
    int fails = 0;
    const auto check = [&] (bool ok, const char* what) { if (! ok) { ++fails; std::printf ("FEHLER: %s\n", what); } };

    for (double bpm : { 128.0, 140.0, 150.0, 174.0 })
    {
        std::vector<double> truth;
        const auto song = makeSong (bpm, 60.0, 0.37, 0.0, &truth);
        const auto g = analyzeSong (song, RATE);
        const double err = measureBeatError (g, truth);
        int oneIdx = -1;
        for (size_t i = 0; i < g.beats.size() && oneIdx < 0; ++i) if (std::fabs (g.beats[i] - truth[0]) < 0.03) oneIdx = (int) i;
        const bool downbeat = oneIdx >= 0 && (oneIdx - g.firstDownbeat) % 4 == 0;
        std::printf ("Song %5.1f BPM: erkannt %.2f, schlechtester Beat %.1f ms, Takt-Eins %s, %s, gleichmaessig %d\n",
                     bpm, g.bpm, err * 1000, downbeat ? "ok" : "falsch", keyName (g.key).c_str(), g.steady);
        check (std::fabs (g.bpm - bpm) < 0.05, "BPM"); check (err < 0.02, "Beatlage"); check (downbeat, "Takt-Eins"); check (g.steady, "Gleichmaessig");
    }
    {
        const auto song = makeSong (140, 60, 0.2, 0.0, nullptr, 3);
        const auto g = analyzeSong (song, RATE);
        std::printf ("Tonart, um 3 Halbtoene verschoben (Soll C Moll): %s\n", keyName (g.key).c_str());
        check (g.key == 12, "Tonart");
    }
    {
        std::vector<double> truth;
        const auto song = makeSong (130, 90.0, 0.5, 0.02, &truth);
        const auto g = analyzeSong (song, RATE);
        const double err = measureBeatError (g, truth);
        std::printf ("Schwankendes Tempo (2%%): %.2f BPM, gleichmaessig %d, schlechtester Beat %.1f ms\n", g.bpm, g.steady, err * 1000);
        check (! g.steady, "Drift erkannt"); check (err < 0.03, "Drift verfolgt");
    }

    // Wiedergabe: Song mit 140 BPM auf FL mit 178 BPM, Kicks muessen auf den Viertel landen
    const auto makeSongObj = [&] (double bpm, double secs, std::vector<double>* truth)
    {
        auto s = std::make_unique<Song>(); s->l = makeSong (bpm, secs, 0.3, 0.0, truth); s->rate = RATE; return s;
    };
    const auto run = [&] (Deck& deck, const Song* s, const SongGrid* g, double hostBpm, double seconds, Controls c, std::vector<float>& out)
    {
        const int block = 256, n = (int) (seconds * RATE);
        out.assign ((size_t) n, 0.0f);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        HostClock clk; clk.bpm = hostBpm; clk.running = true;
        for (int i = 0; i + block <= n; i += block)
        {
            clk.ppq = (double) i / RATE * hostBpm / 60.0;
            deck.process (L.data(), R.data(), block, s, g, clk, c);
            std::copy (L.begin(), L.end(), out.begin() + i);
        }
    };
    const auto kickError = [&] (const std::vector<float>& out, double hostBpm, double from, double to)
    {
        // Anschlag je Beat suchen und Abstand zum Soll messen
        const double T = 60.0 / hostBpm; double worst = 0, sum = 0; int cnt = 0, late = 0;
        for (double t = std::ceil (from / T) * T; t < to; t += T)
        {
            const int c = (int) (t * RATE), w = (int) (0.05 * RATE);
            float last = 0; double hit = t; float best = 0;
            for (int i = c - w; i < c + w; i += 16)
            {
                float e = 0; for (int k = 0; k < 64; ++k) e = std::max (e, std::fabs (out[(size_t) (i + k)]));
                if (e - last > best) { best = e - last; hit = (double) i / RATE; }
                last = e;
            }
            worst = std::max (worst, std::fabs (hit - t)); sum += hit - t; ++cnt;
            if (std::fabs (hit - t) > 0.006) ++late;
            if (std::getenv ("TD_DEBUG") && std::fabs (hit - t) > 0.006) std::printf ("    Beat %.3f s: %+.1f ms, Anstieg %.3f\n", t, (hit - t) * 1000, best);
        }
        std::printf ("  Kicks: Mittel %+.1f ms, schlechtester %.1f ms, ueber 6 ms: %d von %d Beats\n", sum / cnt * 1000, worst * 1000, late, cnt);
        return worst;
    };

    std::vector<double> truth;
    auto song = makeSongObj (140, 120, &truth);
    const SongGrid grid = analyzeSong (song->l, RATE);
    auto deck = std::make_unique<Deck>(); deck->prepare (RATE);
    for (int mode : { STRETCH, VARISPEED })
    {
        deck->reset(); deck->offset.store ((double) grid.firstDownbeat);
        Controls c; c.mode = mode; std::vector<float> out;
        run (*deck, song.get(), &grid, 178, 60, c, out);
        std::printf ("140 BPM auf 178 BPM, Modus %s:\n", mode == STRETCH ? "Stretch" : "Tempo+Pitch");
        check (kickError (out, 178, 1.0, 58.0) < 0.012, "Kicks auf dem Beat");
    }

    // Tonhoehe: 440-Hz-Ton muss im Stretch-Modus bleiben und bei Tempo+Pitch steigen
    {
        auto s = std::make_unique<Song>(); s->rate = RATE; s->l.resize ((size_t) (RATE * 40));
        for (size_t i = 0; i < s->l.size(); ++i) s->l[i] = 0.5f * tone (440.0f, (float) i / RATE);
        SongGrid g; g.bpm = 120; for (int i = 0; i < 90; ++i) g.beats.push_back (i * 0.5);
        const auto measure = [&] (Controls c)
        {
            deck->reset(); deck->offset.store (0.0);
            std::vector<float> out; run (*deck, s.get(), &g, 150, 10, c, out);
            int cr = 0; double firstCr = 0, lastCr = 0;
            for (size_t i = (size_t) RATE; i < (size_t) (9 * RATE); ++i)
                if (out[i - 1] <= 0 && out[i] > 0) { const double p = (double) i - 1 + (double) (-out[i - 1] / (out[i] - out[i - 1])); if (cr == 0) firstCr = p; lastCr = p; ++cr; }
            return RATE * (cr - 1) / (lastCr - firstCr);
        };
        Controls a; const double h1 = measure (a);
        Controls b; b.mode = VARISPEED; const double h2 = measure (b);
        Controls p; p.pitch = 3; const double h3 = measure (p);
        std::printf ("Ton 440 Hz, 120 auf 150 BPM: Stretch %.1f Hz, Tempo+Pitch %.1f Hz (Soll 550), Stretch +3 st %.1f Hz (Soll 523.3)\n", h1, h2, h3);
        check (std::fabs (h1 - 440) < 3, "Stretch haelt Tonhoehe"); check (std::fabs (h2 - 550) < 3, "Varispeed"); check (std::fabs (h3 - 523.25) < 4, "Pitch");
    }

    // Sprung im Takt: Ein Cue muss genau auf dem Rasterpunkt landen
    {
        deck->reset(); deck->offset.store ((double) grid.firstDownbeat);
        Controls c; const int block = 256; std::vector<float> L ((size_t) block), R ((size_t) block);
        HostClock clk; clk.bpm = 170; clk.running = true;
        bool asked = false; double landed = -1;
        const double target = grid.firstDownbeat + 64.0;
        for (int i = 0; i < 500; ++i)
        {
            clk.ppq = 3.3 + (double) i * block / RATE * 170.0 / 60.0;
            if (! asked && clk.ppq > 5.0) { deck->requestJump (target, 4.0); asked = true; }
            deck->process (L.data(), R.data(), block, song.get(), &grid, clk, c);
            if (landed < 0 && deck->offset.load() > grid.firstDownbeat + 1.0) landed = clk.ppq;
        }
        const double beatAt = deck->nowBeat.load() - clk.ppq, frac = std::fmod (beatAt - grid.firstDownbeat + 4000.0, 4.0);
        std::printf ("Cue-Sprung: gelandet bei FL-Beat %.3f (Takt 8.0), Takt-Abstand %.3f\n", landed, frac);
        check (landed >= 8.0 && landed < 8.05, "Sprung auf Takt"); check (frac < 0.001 || frac > 3.999, "Takt bleibt");
    }

    // Loop: Song-Position muss in den Grenzen bleiben
    {
        Controls c; c.loopOn = true; c.loopBeats = 2; deck->loopStart.store (32.0); deck->offset.store (32.0);
        double lo = 1e9, hi = -1e9;
        for (double p = 0; p < 40; p += 0.01) { const double b = deck->songBeatAt (p, c, &grid); lo = std::min (lo, b); hi = std::max (hi, b); }
        std::printf ("Loop: %.2f bis %.2f (Soll 32 bis 34)\n", lo, hi);
        check (lo >= 32.0 && hi < 34.0, "Loop");
    }
    std::printf (fails ? "FEHLER: %d\n" : "alles ok (%d Fehler)\n", fails);
    return fails != 0;
}
