// Tekkdeck - erkennt Tempo, Beats, Takt-Eins und Tonart eines Songs (ohne JUCE, damit es testbar bleibt).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace td
{
constexpr float kPi = 3.14159265358979f;

inline void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const std::complex<float> step = std::polar (1.0f, -2.0f * kPi / (float) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k, w *= step)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
            }
        }
    }
}

struct SongGrid
{
    std::vector<double> beats;     // Beat-Zeiten in Sekunden
    double bpm = 120;
    int firstDownbeat = 0;         // Index des ersten Beats, der eine Takt-Eins ist
    int key = 0;                   // 0-11 Dur ab C, 12-23 Moll ab C
    bool steady = true;            // false = Tempo schwankt, Beats folgen dem Song einzeln

    double beatToSeconds (double b) const
    {
        const int n = (int) beats.size();
        if (n < 2) return b * 60.0 / bpm;
        if (b <= 0) return beats[0] + b * (beats[1] - beats[0]);
        if (b >= n - 1) return beats[(size_t) n - 1] + (b - (n - 1)) * (beats[(size_t) n - 1] - beats[(size_t) n - 2]);
        const int i = (int) b;
        return beats[(size_t) i] + (b - i) * (beats[(size_t) i + 1] - beats[(size_t) i]);
    }
    double secondsToBeat (double s) const
    {
        const int n = (int) beats.size();
        if (n < 2) return s * bpm / 60.0;
        if (s <= beats[0]) return (s - beats[0]) / (beats[1] - beats[0]);
        if (s >= beats[(size_t) n - 1]) return (n - 1) + (s - beats[(size_t) n - 1]) / (beats[(size_t) n - 1] - beats[(size_t) n - 2]);
        const int i = (int) (std::upper_bound (beats.begin(), beats.end(), s) - beats.begin()) - 1;
        return i + (s - beats[(size_t) i]) / (beats[(size_t) i + 1] - beats[(size_t) i]);
    }
    /** Tempo an einer Stelle des Songs */
    double localBpm (double b) const
    {
        const int n = (int) beats.size();
        if (n < 2) return bpm;
        const int i = std::clamp ((int) b, 0, n - 2);
        return 60.0 / std::max (0.05, beats[(size_t) i + 1] - beats[(size_t) i]);
    }
};

inline std::string keyName (int k)
{
    static const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H" };
    return std::string (n[k % 12]) + (k >= 12 ? " Moll" : " Dur");
}

struct OnsetData { std::vector<float> all, low, lowPower; std::vector<std::array<float, 8>> mids; float fps = 0; double delay = 0; };

inline OnsetData onsets (const std::vector<float>& mono, float rate)
{
    constexpr int N = 512, hop = 128;
    const int dec = std::max (1, (int) std::round (rate / 11025.0f));
    const float sr = rate / (float) dec;
    std::vector<float> d;
    d.reserve (mono.size() / (size_t) dec + 1);
    for (size_t i = 0; i + (size_t) dec <= mono.size(); i += (size_t) dec)
    {
        float s = 0;
        for (int k = 0; k < dec; ++k) s += mono[i + (size_t) k];
        d.push_back (s / (float) dec);
    }
    OnsetData o; o.fps = sr / (float) hop; o.delay = 0.73 * N / sr;
    std::vector<float> win (N), prev (N / 2, 0.0f);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2 * kPi * (float) i / N);
    const int lowBins = std::max (2, (int) (200.0f / sr * N));
    std::vector<std::complex<float>> a (N);
    for (size_t f = 0; f + N <= d.size(); f += hop)
    {
        for (int i = 0; i < N; ++i) a[(size_t) i] = d[f + (size_t) i] * win[(size_t) i];
        fft (a);
        float flux = 0, low = 0, power = 0;
        std::array<float, 8> bandsNow {};
        for (int k = 1; k < N / 2; ++k)
        {
            const float mag = std::abs (a[(size_t) k]), m = std::log (1.0f + 40.0f * mag);
            const float up = std::max (0.0f, m - prev[(size_t) k]);
            flux += up;
            if (k < lowBins) { low += up; power += mag; }
            const float hz = (float) k * sr / N;
            if (hz >= 200.0f && hz < 2600.0f) bandsNow[(size_t) std::min (7, (int) (std::log2 (hz / 200.0f) / 3.7f * 8.0f))] += m;
            prev[(size_t) k] = m;
        }
        o.all.push_back (flux); o.low.push_back (low); o.lowPower.push_back (power); o.mids.push_back (bandsNow);
    }
    // Gleichanteil ueber rund 1 s abziehen, damit nur Anschlaege uebrig bleiben
    const int w = (int) o.fps;
    std::vector<float> clean (o.all.size());
    double run = 0;
    for (int i = 0; i < (int) o.all.size(); ++i)
    {
        run += o.all[(size_t) i];
        if (i > w) run -= o.all[(size_t) (i - w - 1)];
        clean[(size_t) i] = std::max (0.0f, o.all[(size_t) i] - (float) (run / std::min (i + 1, w + 1)));
    }
    o.all = std::move (clean);
    return o;
}

/** Faltet die Anschlaege auf eine Periode. Liefert Treffsicherheit und Phase (in Frames). */
inline double foldScore (const std::vector<float>& oe, double period, double& phase)
{
    constexpr int bins = 48;
    double h[bins] {};
    double total = 1.0e-9;
    for (size_t i = 0; i < oe.size(); ++i)
    {
        const double x = std::fmod ((double) i, period) / period * bins;
        h[std::min (bins - 1, (int) x)] += oe[i];
        total += oe[i];
    }
    int best = 0; double top = 0;
    for (int i = 0; i < bins; ++i)
    {
        const double v = h[i] + 0.7 * (h[(i + 1) % bins] + h[(i + bins - 1) % bins]);
        if (v > top) { top = v; best = i; }
    }
    double w = 1.0e-9, c = 0;
    for (int k = -2; k <= 2; ++k) { const double v = h[(best + k + bins) % bins]; w += v; c += v * k; }
    phase = ((best + c / w + 0.5) / bins) * period;
    return top / total;
}

inline SongGrid analyzeSong (const std::vector<float>& mono, float rate, std::vector<float>* outEnvelope = nullptr)
{
    SongGrid g;
    const auto o = onsets (mono, rate);
    if (o.all.size() < 200) return g;
    // Fuer die Lage der Beats zaehlt der Bass mehr: Sonst rastet das Raster auf Hi-Hats zwischen den Kicks ein
    std::vector<float> oe (o.all.size());
    {
        double ma = 1.0e-9, ml = 1.0e-9;
        for (size_t i = 0; i < oe.size(); ++i) { ma += o.all[i]; ml += o.low[i]; }
        const float ka = (float) ((double) oe.size() / ma), kl = (float) ((double) oe.size() / ml);
        for (size_t i = 0; i < oe.size(); ++i) oe[i] = o.all[i] * ka + 3.0f * o.low[i] * kl;
    }
    if (outEnvelope != nullptr) *outEnvelope = oe;
    const double seconds = (double) mono.size() / rate;

    // Grobes Tempo: Autokorrelation, leicht gewichtet Richtung schnelle Dance-Tempi
    const int maxLag = std::min ((int) (o.fps * 60.0f / 60.0f), (int) oe.size() / 2);
    const int minLag = (int) (o.fps * 60.0f / 220.0f);
    std::vector<float> ac ((size_t) maxLag * 2 + 2, 0.0f);
    for (int l = minLag / 2; l <= std::min (maxLag * 2, (int) oe.size() / 2); ++l)
    {
        double s = 0;
        for (size_t i = (size_t) l; i < oe.size(); i += 1) s += oe[i] * oe[i - (size_t) l];
        ac[(size_t) l] = (float) (s / (double) (oe.size() - (size_t) l));
    }
    int bestLag = minLag; float bestScore = -1;
    for (int l = minLag; l <= maxLag; ++l)
    {
        const float b = 60.0f * o.fps / (float) l;
        const float w = std::exp (-0.5f * std::pow (std::log2 (b / 140.0f) / 1.0f, 2.0f));
        const float s = w * (ac[(size_t) l] + 0.5f * ac[std::min ((size_t) l * 2, ac.size() - 1)] + 0.25f * ac[(size_t) l / 2]);
        if (s > bestScore) { bestScore = s; bestLag = l; }
    }

    // Feintempo: ueber den ganzen Song falten
    double bpm = 60.0 * o.fps / bestLag, phase = 0, top = -1, bestBpm = bpm;
    for (double b = bpm * 0.97; b <= bpm * 1.03; b += 0.02)
    {
        double ph; const double s = foldScore (oe, 60.0 * o.fps / b, ph);
        if (s > top) { top = s; bestBpm = b; phase = ph; }
    }
    // Sehr schmal nachsuchen und glatte Tempi bevorzugen, die meisten Produktionen laufen im Raster
    for (double b = bestBpm - 0.03; b <= bestBpm + 0.03; b += 0.002)
    {
        double ph; const double s = foldScore (oe, 60.0 * o.fps / b, ph);
        if (s > top) { top = s; bestBpm = b; phase = ph; }
    }
    {
        double ph; const double whole = std::round (bestBpm);
        const double s = foldScore (oe, 60.0 * o.fps / whole, ph);
        if (s >= top * 0.97) { bestBpm = whole; phase = ph; top = s; }
    }
    bpm = bestBpm;
    const double T = 60.0 / bpm;
    const double latency = o.delay;      // Der Analyse-Frame meldet einen Anschlag etwas zu frueh
    const double first = phase / o.fps + latency;
    std::vector<double> grid;
    for (double t = std::fmod (first, T); t < seconds; t += T) grid.push_back (t);

    // Beat-Verfolgung nach Ellis: zeigt, ob der Song vom festen Raster wegdriftet
    const double Tf = T * o.fps;
    const int n = (int) oe.size();
    std::vector<float> score ((size_t) n, 0.0f); std::vector<int> back ((size_t) n, -1);
    float mean = 1.0e-6f; for (auto v : oe) mean += v; mean /= (float) n;
    constexpr float alpha = 400.0f;
    for (int i = 0; i < n; ++i)
    {
        const int a = i - (int) (Tf * 1.25), b = i - (int) (Tf * 0.75);
        float best = 0; int who = -1;
        for (int k = std::max (0, a); k <= b && k >= 0; ++k)
        {
            const float lr = std::log ((float) (i - k) / (float) Tf);
            const float c = score[(size_t) k] - alpha * lr * lr * mean;
            if (c > best) { best = c; who = k; }
        }
        score[(size_t) i] = oe[(size_t) i] + best; back[(size_t) i] = who;
    }
    int end = (int) (std::max_element (score.begin() + std::max (0, n - (int) (Tf * 2)), score.end()) - score.begin());
    std::vector<double> track;
    for (int i = end; i >= 0; i = back[(size_t) i]) track.push_back ((double) i / o.fps + latency);
    std::reverse (track.begin(), track.end());

    // Abweichung zum festen Raster
    double dev = 0; int count = 0;
    {
        std::vector<double> diffs;
        for (auto t : track)
        {
            const double x = std::fmod (t - first + 1000.0 * T, T);
            diffs.push_back (std::min (x, T - x));
        }
        std::sort (diffs.begin(), diffs.end());
        if (! diffs.empty()) { dev = diffs[diffs.size() * 3 / 4]; count = (int) diffs.size(); }
    }
    g.steady = dev < 0.025 || count < 8;
    g.bpm = bpm;
    g.beats = g.steady ? grid : track;
    if (! g.steady && g.beats.size() >= 2)
        g.bpm = 60.0 * (double) (g.beats.size() - 1) / (g.beats.back() - g.beats.front());

    // Takt-Eins: Bass-Anschlaege plus Klangwechsel. Abschnitte beginnen fast immer auf der Eins
    double phaseScore[4] {};
    {
        // Mittlerer Klang je Beat, danach Veraenderung zum Beat davor: Harmonien wechseln meist auf der Eins
        const size_t nb = g.beats.size();
        std::vector<std::array<float, 8>> beatBands (nb);
        std::vector<float> beatLow (nb, 0.0f);
        for (size_t b = 0; b + 1 < nb; ++b)
        {
            const int f0 = std::max (0, (int) ((g.beats[b] - latency) * o.fps));
            const int f1 = std::min (n, (int) ((g.beats[b + 1] - latency) * o.fps));
            const int hit = std::min (n, f0 + std::max (2, (int) (Tf * 0.25)));
            for (int f = f0; f < hit; ++f) beatLow[b] = std::max (beatLow[b], o.lowPower[(size_t) f]);
            for (int f = f0; f < f1; ++f) for (size_t k = 0; k < 8; ++k) beatBands[b][k] += o.mids[(size_t) f][k] / std::max (1, f1 - f0);
        }
        double low[4] {}, change[4] {}; int cnt[4] {};
        for (size_t b = 1; b + 1 < nb; ++b)
        {
            double d = 0;
            for (size_t k = 0; k < 8; ++k) d += std::fabs (beatBands[b][k] - beatBands[b - 1][k]);
            low[b % 4] += beatLow[b]; change[b % 4] += d; ++cnt[b % 4];
        }
        double lowMean = 1.0e-9, changeMean = 1.0e-9;
        for (int k = 0; k < 4; ++k) { low[k] /= std::max (1, cnt[k]); change[k] /= std::max (1, cnt[k]); lowMean += low[k] / 4; changeMean += change[k] / 4; }
        for (int k = 0; k < 4; ++k) phaseScore[k] = low[k] / lowMean + 2.0 * change[k] / changeMean;
    }
    g.firstDownbeat = (int) (std::max_element (phaseScore, phaseScore + 4) - phaseScore);

    // Tonart: Chroma gegen die Krumhansl-Profile
    {
        constexpr int N = 8192;
        const int dec = std::max (1, (int) std::round (rate / 11025.0f));
        const float sr = rate / (float) dec;
        double chroma[12] {};
        std::vector<std::complex<float>> a (N);
        const size_t len = mono.size() / (size_t) dec;
        const size_t hop = std::max<size_t> (N, len / 120);
        for (size_t s = 0; s + N < len; s += hop)
        {
            for (int i = 0; i < N; ++i)
            {
                float v = 0;
                for (int k = 0; k < dec; ++k) v += mono[(s + (size_t) i) * (size_t) dec + (size_t) k];
                a[(size_t) i] = v * (0.5f - 0.5f * std::cos (2 * kPi * (float) i / N));
            }
            fft (a);
            for (int k = (int) (55.0f / sr * N); k < (int) (2000.0f / sr * N); ++k)
            {
                const float hz = (float) k * sr / N;
                const int pc = ((int) std::round (12.0f * std::log2 (hz / 261.63f)) % 12 + 12) % 12;
                chroma[pc] += std::abs (a[(size_t) k]);
            }
        }
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        const auto corr = [&] (const double* prof, int root)
        {
            double mc = 0, mp = 0;
            for (int i = 0; i < 12; ++i) { mc += chroma[i]; mp += prof[i]; }
            mc /= 12; mp /= 12;
            double num = 0, a2 = 0, b2 = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = chroma[(i + root) % 12] - mc, y = prof[i] - mp;
                num += x * y; a2 += x * x; b2 += y * y;
            }
            return num / std::sqrt (a2 * b2 + 1.0e-12);
        };
        double bestK = -2;
        for (int r = 0; r < 12; ++r)
        {
            if (const double c = corr (maj, r); c > bestK) { bestK = c; g.key = r; }
            if (const double c = corr (mnr, r); c > bestK) { bestK = c; g.key = r + 12; }
        }
    }
    return g;
}
} // namespace td
