// flanger_test.cpp
// Offline checks for the TB 117 Flanger DSP. Builds on Linux or macOS:
//   make -C Tests run      (writes demo WAVs to Tests/renders)
//
// 1. Bypass: the footswitch out passes the input straight through
// 2. Delay: the wet click lands at the BBD delay; MANUAL sets it; WIDTH sweeps
// 3. SPEED: LFO rate range and the measured sweep rate
// 4. The comb: deep notches with the dry and wet mixed; pre/de-emphasis
//    flat on the dry path; the wet filter's response
// 5. REGEN: peaks grow with the knob, the loop stays stable at 10
// 6. Limiter and BBD: the diodes soften the driver; hot input stays bounded
// 7. Stability sweep: knob extremes x 10 mV-4 V x 41 Hz-15 kHz, plus noise
// 8. Automation of every control while playing; CPU; demo renders

#include "../Source/DSP/Flanger.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

using namespace tb117;

static const double kPiT = 3.14159265358979323846;
static const int kSr = 44100;
static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (! ok) ++failures;
}

static bool allFinite (const std::vector<float>& x, double limit)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (! (x[i] == x[i]) || std::fabs (x[i]) > limit) return false;
    return true;
}

static double rmsRange (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

static double peakRange (const std::vector<float>& x, size_t from, size_t to)
{
    double p = 0.0;
    if (to > x.size()) to = x.size();
    for (size_t i = from; i < to; ++i) p = std::max (p, (double) std::fabs (x[i]));
    return p;
}

static std::vector<float> sine (double f, double amp, double sec)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (amp * std::sin (2.0 * kPiT * f * i / kSr));
    return v;
}

static std::vector<float> run (Flanger& a, const std::vector<float>& in)
{
    std::vector<float> out (in.size());
    for (size_t pos = 0; pos < in.size(); pos += 256)
    {
        const int n = (int) std::min ((size_t) 256, in.size() - pos);
        a.process (&in[pos], &out[pos], n);
    }
    return out;
}

static void defaults (Flanger& a)
{
    a.setManual (0.5); a.setWidth (0.5); a.setSpeed (0.5); a.setRegen (0.5); a.setEffectOn (true);
    a.setBiasTrim (0.5); a.setLevelTrim (0.75); a.setClockTrim (0.5);
    a.setInputGainDb (0.0); a.setOutputGainDb (0.0);
}

// Static comb: WIDTH at 0 holds the delay still at MANUAL's setting
static void still (Flanger& a, double regen, double manual = 0.5)
{
    defaults (a);
    a.setWidth (0.0);
    a.setManual (manual);
    a.setRegen (regen);
}

// Steady-state gain (dB) of a sine through the unit
static double gainDb (Flanger& a, double f, double amp = 0.1)
{
    a.reset();
    std::vector<float> x = sine (f, amp, 0.5);
    std::vector<float> y = run (a, x);
    const double gi = rmsRange (x, kSr / 4, kSr / 2), go = rmsRange (y, kSr / 4, kSr / 2);
    return 20.0 * std::log10 ((go + 1e-12) / (gi + 1e-12));
}

static void writeWav (const std::string& path, const std::vector<float>& x, int sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const unsigned dataBytes = (unsigned) x.size() * 2, riff = 36 + dataBytes;
    const unsigned short fmt = 1, ch = 1, bits = 16, align = 2;
    const unsigned rate = (unsigned) sr, bps = rate * 2, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&rate, 4, 1, f); std::fwrite (&bps, 4, 1, f); std::fwrite (&align, 2, 1, f);
    std::fwrite (&bits, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i] * 0.9;
        v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v);
        const short s = (short) std::floor (v * 32767.0 + 0.5);
        std::fwrite (&s, 2, 1, f);
    }
    std::fclose (f);
}

// A decaying plucked string (Karplus-Strong), added into buf at start
static void pluck (std::vector<float>& buf, size_t start, double freq, double amp, double dur)
{
    const int period = std::max (2, (int) (kSr / freq + 0.5));
    std::vector<double> line (period);
    unsigned seed = (unsigned) (freq * 1000.0) + (unsigned) start;
    for (int i = 0; i < period; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        line[i] = ((seed >> 9) / 4194304.0 - 1.0) * amp;
    }
    const size_t n = (size_t) (dur * kSr);
    int idx = 0;
    double prev = 0.0;
    for (size_t i = 0; i < n && start + i < buf.size(); ++i)
    {
        const double cur = line[idx];
        const double next = 0.996 * 0.5 * (cur + prev);
        prev = cur;
        line[idx] = next;
        buf[start + i] += (float) cur;
        if (++idx >= period) idx = 0;
    }
}

static std::vector<float> makeChords (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double chords[4][4] = { { 82.4, 123.5, 164.8, 207.7 }, { 110.0, 164.8, 220.0, 277.2 },
                                  { 98.0, 146.8, 196.0, 246.9 }, { 73.4, 110.0, 146.8, 185.0 } };
    for (int c = 0; c * 2.0 < seconds; ++c)
        for (int s = 0; s < 4; ++s)
            pluck (v, (size_t) ((c * 2.0 + s * 0.012) * kSr), chords[c % 4][s], 0.22, 2.0);
    return v;
}

static std::vector<float> makeRiff (double seconds)
{
    std::vector<float> v ((size_t) (seconds * kSr), 0.0f);
    const double notes[8] = { 82.4, 98.0, 110.0, 123.5, 110.0, 98.0, 146.8, 130.8 };
    for (int i = 0; i * 0.25 < seconds; ++i)
        pluck (v, (size_t) (i * 0.25 * kSr), notes[i % 8], 0.35, 0.6);
    return v;
}

static std::vector<float> noise (double amp, double sec, unsigned seed)
{
    std::vector<float> v ((size_t) (sec * kSr));
    for (size_t i = 0; i < v.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        v[i] = (float) (amp * ((seed >> 9) / 4194304.0 - 1.0));
    }
    return v;
}

int main (int argc, char** argv)
{
    const std::string renderDir = argc > 1 ? argv[1] : ".";
    Flanger* ap = new Flanger();
    Flanger& a = *ap;
    a.prepare (kSr);
    const int lat = Flanger::latencySamples();

    std::printf ("\n1. Bypass\n");
    {
        defaults (a);
        a.setEffectOn (false);
        a.reset();
        std::vector<float> x = sine (440.0, 0.3, 0.5), y = run (a, x);
        // The oversampler's filters delay by the reported latency to within a
        // fraction of a sample, so compare at the best of the two nearest lags
        double maxd = 1e9;
        for (int L = lat - 1; L <= lat; ++L)
        {
            double m = 0.0;
            for (size_t i = kSr / 10; i < x.size(); ++i) m = std::max (m, (double) std::fabs (y[i] - x[i - L]));
            maxd = std::min (maxd, m);
        }
        std::printf ("  bypassed vs input (latency aligned): max diff %.2g of 0.3\n", maxd);
        check (maxd < 0.02, "footswitch out passes the input straight through");
        a.setEffectOn (true);
    }

    std::printf ("\n2. BBD delay, MANUAL and WIDTH\n");
    {
        std::printf ("  MANUAL (WIDTH 0): %.2f ms at 0, %.2f at 5, %.2f at 10\n",
                     Flanger::delayMs (0.0, 0.0, 0.0), Flanger::delayMs (0.5, 0.0, 0.0), Flanger::delayMs (1.0, 0.0, 0.0));
        bool mono = true;
        for (int k = 1; k <= 20; ++k) if (Flanger::delayMs (k / 20.0, 0.0, 0.0) > Flanger::delayMs ((k - 1) / 20.0, 0.0, 0.0)) mono = false;
        check (mono, "MANUAL clockwise shortens the delay (the buffer tops out near the end)");
        check (Flanger::delayMs (0.0, 0.0, 0.0) > 2.0 && Flanger::delayMs (1.0, 0.0, 0.0) < 0.7, "MANUAL covers about 0.5 - 3 ms");

        still (a, 0.0, 0.0);
        a.reset();
        std::vector<float> x ((size_t) (0.1 * kSr), 0.0f);
        for (int i = 0; i < 20; ++i) x[200 + i] = (float) (0.3 * std::sin (kPiT * i / 20.0));
        std::vector<float> y = run (a, x);
        size_t dryAt = 0; double best = 0.0;
        for (size_t i = 200; i < (size_t) (230 + lat); ++i) if (std::fabs (y[i]) > best) { best = std::fabs (y[i]); dryAt = i; }
        size_t wetAt = 0; double bw = 0.0;
        const size_t from = dryAt + (size_t) (0.0010 * kSr);
        for (size_t i = from; i < from + (size_t) (0.005 * kSr); ++i) if (std::fabs (y[i]) > bw) { bw = std::fabs (y[i]); wetAt = i; }
        const double ms = 1000.0 * (double) (wetAt - dryAt) / kSr, nominal = Flanger::delayMs (0.0, 0.0, 0.0);
        std::printf ("  MANUAL 0: wet copy %.2f ms after the dry (nominal %.2f), dry %.3f, wet %.3f\n", ms, nominal, best, bw);
        check (std::fabs (ms - nominal) < 0.25, "wet click lands at the BBD delay");
        check (bw > 0.5 * best && bw < 2.5 * best, "wet copy about as loud as the dry, or louder (LEVEL trim)");
        check (std::fabs (a.currentClockHz() - 512.0 / (nominal * 1.0e-3)) < 1.0, "clock: 512 periods for 1024 stages");

        defaults (a);
        a.setWidth (1.0);
        a.setSpeed (0.8);
        a.reset();
        double lo = 1e9, hi = 0.0;
        std::vector<float> z (256, 0.0f), o (256);
        for (int b = 0; b < 4 * kSr / 256; ++b)
        {
            a.process (&z[0], &o[0], 256);
            lo = std::min (lo, a.currentDelayMs());
            hi = std::max (hi, a.currentDelayMs());
        }
        std::printf ("  WIDTH 10: sweeps %.2f - %.2f ms (clock %.0f - %.0f kHz)\n", lo, hi, 512.0 / hi, 512.0 / lo);
        check (std::fabs (lo - Flanger::delayMs (0.5, 1.0, 1.0)) < 0.05
               && std::fabs (hi - Flanger::delayMs (0.5, 1.0, -1.0)) < 0.05, "full WIDTH covers the LFO's range");
        check (hi / lo > 5.0, "a wide sweep");
        double lo5 = 1e9, hi5 = 0.0;
        a.setWidth (0.5);
        for (int b = 0; b < 4 * kSr / 256; ++b)
        {
            a.process (&z[0], &o[0], 256);
            if (b > kSr / 256) { lo5 = std::min (lo5, a.currentDelayMs()); hi5 = std::max (hi5, a.currentDelayMs()); }
        }
        std::printf ("  WIDTH 5: sweeps %.2f - %.2f ms\n", lo5, hi5);
        check (hi5 - lo5 < hi - lo && hi5 - lo5 > 0.1 * (hi - lo), "WIDTH 5 sweeps less far");
        a.setWidth (0.0);
        double lo0 = 1e9, hi0 = 0.0;
        for (int b = 0; b < 2 * kSr / 256; ++b)
        {
            a.process (&z[0], &o[0], 256);
            if (b > kSr / 256) { lo0 = std::min (lo0, a.currentDelayMs()); hi0 = std::max (hi0, a.currentDelayMs()); }
        }
        check (hi0 - lo0 < 0.01, "WIDTH 0: no sweep");
    }

    std::printf ("\n3. SPEED\n");
    {
        std::printf ("  LFO: %.3f Hz at 0, %.2f at 5, %.2f at 10\n", Flanger::lfoHz (0.0), Flanger::lfoHz (0.5), Flanger::lfoHz (1.0));
        bool mono = true;
        for (int k = 1; k <= 20; ++k) if (Flanger::lfoHz (k / 20.0) <= Flanger::lfoHz ((k - 1) / 20.0)) mono = false;
        check (mono, "rate rises clockwise");
        check (Flanger::lfoHz (0.0) > 0.05 && Flanger::lfoHz (0.0) < 0.2 && Flanger::lfoHz (1.0) > 5.0 && Flanger::lfoHz (1.0) < 15.0,
               "about 0.1 - 10 Hz");
        defaults (a);
        a.setSpeed (0.7);
        a.reset();
        std::vector<float> z (64, 0.0f), o (64);
        int crossings = 0; double prev = 0.0;
        const int blocks = 5 * kSr / 64;
        for (int b = 0; b < blocks; ++b)
        {
            a.process (&z[0], &o[0], 64);
            const double v = a.lfoValue();
            if (b > 0 && prev < 0.0 && v >= 0.0) ++crossings;
            prev = v;
        }
        const double hz = crossings / 5.0, expect = Flanger::lfoHz (0.7);
        std::printf ("  SPEED 7: %.2f Hz measured, %.2f expected\n", hz, expect);
        check (std::fabs (hz - expect) < 0.25, "sweep rate as set");
    }

    std::printf ("\n4. The comb and the wet filter (WIDTH 0, MANUAL 0, REGEN 0)\n");
    {
        still (a, 0.0, 0.0);
        double mx = -100.0, mn = 100.0, fmx = 0.0, fmn = 0.0;
        for (double f = 150.0; f < 3000.0; f *= 1.02)
        {
            const double g = gainDb (a, f);
            if (g > mx) { mx = g; fmx = f; }
            if (g < mn) { mn = g; fmn = f; }
        }
        std::printf ("  150 Hz - 3 kHz: peak %+.1f dB at %.0f Hz, notch %+.1f dB at %.0f Hz\n", mx, fmx, mn, fmn);
        check (mx - mn > 12.0, "deep notches");
        check (mx < 6.5 && mx > -3.0, "comb peaks a few dB up (wet a little hotter than dry)");

        const double ref = Flanger::wetFilterDb (100.0);
        double worst = 0.0;
        for (double f = 100.0; f <= 9000.0; f *= 1.1) worst = std::max (worst, std::fabs (Flanger::wetFilterDb (f) - ref));
        std::printf ("  wet filter: %+.1f dB at 100 Hz, %+.1f at 5 kHz, %+.1f at 10 kHz, %+.1f at 15 kHz, %+.1f at 25 kHz\n",
                     ref, Flanger::wetFilterDb (5000.0), Flanger::wetFilterDb (10000.0), Flanger::wetFilterDb (15000.0), Flanger::wetFilterDb (25000.0));
        check (worst < 2.0, "wet filter flat within 2 dB to 9 kHz");
        check (Flanger::wetFilterDb (25000.0) - ref < -15.0, "and steep above (anti-alias)");

        // Effect on, delay at its shortest: notches go up out of the way; with
        // pre- and de-emphasis cancelling, the level stays near the dry gain
        still (a, 0.0, 1.0);
        const double g100 = gainDb (a, 100.0), g300 = gainDb (a, 300.0), g1k = gainDb (a, 1000.0);
        std::printf ("  MANUAL 10 (0.5 ms): 100 Hz %+.1f dB, 300 Hz %+.1f dB, first notch at 1 kHz %+.1f dB\n", g100, g300, g1k);
        check (std::fabs (g100 - g300) < 3.0, "flat well below the first notch");
        check (g1k < g300 - 10.0, "first notch at 1 / (2 x delay)");
    }

    std::printf ("\n5. REGEN\n");
    {
        double last = -100.0;
        bool grows = true;
        for (int k = 0; k <= 2; ++k)
        {
            still (a, k * 0.5, 0.0);
            double mx = -100.0, mn = 100.0;
            for (double f = 300.0; f < 1500.0; f *= 1.015)
            {
                const double g = gainDb (a, f);
                mx = std::max (mx, g); mn = std::min (mn, g);
            }
            std::printf ("  REGEN %d: peaks %+.1f dB, notches %+.1f dB\n", k * 5, mx, mn);
            if (k > 0 && mx <= last) grows = false;
            last = mx;
        }
        check (grows, "regeneration peaks rise with the knob");

        std::printf ("  loop gain (LEVEL trim default): %.2f at noon, %.2f at 2 o'clock, %.2f at 3, %.2f at 10\n",
                     Flanger::regenLoopGain (0.5, 0.75), Flanger::regenLoopGain (0.7, 0.75),
                     Flanger::regenLoopGain (0.8, 0.75), Flanger::regenLoopGain (1.0, 0.75));
        check (Flanger::regenLoopGain (0.7, 0.75) > 0.93 && Flanger::regenLoopGain (0.7, 0.75) < 1.0,
               "2 o'clock: a steady ring, right at the edge (the user's favourite sound)");
        check (Flanger::regenLoopGain (0.8, 0.75) > 1.0, "wild from 3 o'clock, like the pedal");
        check (std::fabs (Flanger::regenLoopGain (1.0, 0.75) - 1.15) < 0.02, "full-up strength unchanged");
        check (Flanger::regenLoopGain (1.0, 0.5) < 0.8, "LEVEL trim down tames it");

        // Below the edge the ringing still dies away once the input stops
        still (a, 0.55, 0.0);
        a.setWidth (1.0);
        a.reset();
        std::vector<float> x = noise (0.3, 1.0, 7), z (3 * kSr, 0.0f);
        run (a, x);
        std::vector<float> y = run (a, z);
        const double tail = peakRange (y, 2 * kSr, 3 * kSr);
        std::printf ("  REGEN 5.5, 2 s after the input stops: %.2g\n", tail);
        check (tail < 1e-3, "rings down below the edge");

        // Full REGEN: it takes off on its own, held by the diode limiter
        still (a, 1.0, 0.0);
        a.setWidth (1.0);
        a.reset();
        run (a, x);
        std::vector<float> y2 = run (a, z);
        const double osc = rmsRange (y2, 2 * kSr, 3 * kSr), pk = peakRange (y2, 0, y2.size());
        std::printf ("  REGEN 10, input stopped: still %.2f V rms, peak %.2f V\n", osc, pk);
        check (osc > 0.05, "self-oscillation keeps going");
        check (allFinite (y2, 8.0) && pk < 7.0, "and stays bounded (diodes, BBD clip, rails)");
    }

    std::printf ("\n6. Diode limiter and BBD\n");
    {
        const double v1 = a.driverOut (1.0e-6), v20 = a.driverOut (20.0e-6), v60 = a.driverOut (60.0e-6);
        std::printf ("  driver: %.3f V at 1 uA, %.2f V at 20 uA, %.2f V at 60 uA (linear %.1f)\n", v1, v20, v60, 60.0e-6 * 1.0e5);
        check (std::fabs (v1 - 0.1) < 0.01, "small signals: 100K feedback, gain 2/3");
        check (v60 < 3.0 && v60 > v20, "the 1N4148s and 27K soften big swings");
        check (std::fabs (a.driverOut (-60.0e-6) + v60) < 1e-6, "symmetrical");

        defaults (a);
        a.setRegen (1.0);
        a.reset();
        a.clearPeakBbd();
        std::vector<float> x = sine (220.0, 3.0, 1.0), y = run (a, x);
        std::printf ("  3 V in: output peak %.2f, BBD drive %.2f of clip\n", peakRange (y, kSr / 2, kSr), a.peakBbd());
        check (allFinite (y, 9.0) && peakRange (y, kSr / 2, kSr) < 7.0, "hot input stays bounded (rails, limiter, BBD clip)");

        a.reset();
        a.clearPeakBbd();
        std::vector<float> g = sine (220.0, 0.2, 1.0);
        run (a, g);
        std::printf ("  0.2 V guitar level: BBD drive %.2f of clip\n", a.peakBbd());
        check (a.peakBbd() < 0.7, "guitar level stays clean in the BBD");

        still (a, 0.0, 0.0);
        a.reset();
        std::vector<float> h = sine (9000.0, 0.3, 0.5), yh = run (a, h);
        check (allFinite (yh, 4.0), "clean at a 9 kHz tone");
    }

    std::printf ("\n6b. PCB trims\n");
    {
        std::printf ("  CLOCK trim (MANUAL 0): %.2f ms at 0, %.2f centred, %.2f at 10\n",
                     Flanger::delayMs (0, 0, 0, 0.0), Flanger::delayMs (0, 0, 0, 0.5), Flanger::delayMs (0, 0, 0, 1.0));
        check (Flanger::delayMs (0, 0, 0, 0.0) > Flanger::delayMs (0, 0, 0, 1.0) + 0.3, "CLOCK trim shifts the delay range");

        // LEVEL: the wet copy's level follows the trim
        double wetAt[2];
        for (int k = 0; k < 2; ++k)
        {
            still (a, 0.0, 0.0);
            a.setLevelTrim (k == 0 ? 0.4 : 0.9);
            a.reset();
            std::vector<float> x ((size_t) (0.1 * kSr), 0.0f);
            for (int i = 0; i < 20; ++i) x[200 + i] = (float) (0.3 * std::sin (kPiT * i / 20.0));
            std::vector<float> y = run (a, x);
            wetAt[k] = peakRange (y, 200 + lat + (size_t) (0.0022 * kSr), 200 + lat + (size_t) (0.0040 * kSr));
        }
        std::printf ("  LEVEL trim 4: wet %.3f, 9: wet %.3f\n", wetAt[0], wetAt[1]);
        check (wetAt[1] > 1.8 * wetAt[0], "LEVEL trim sets the wet level");

        // BIAS: off-centre, one side clips first (even harmonics)
        double h2[2];
        for (int k = 0; k < 2; ++k)
        {
            still (a, 0.0, 1.0);
            a.setLevelTrim (0.75);
            a.setBiasTrim (k == 0 ? 0.5 : 0.9);
            a.reset();
            const double f0 = 441.0;
            std::vector<float> y = run (a, sine (f0, 1.0, 0.5));
            double c1 = 0, s1 = 0, c2 = 0, s2 = 0;
            for (size_t i = kSr / 4; i < y.size(); ++i)
            {
                const double ph = 2.0 * kPiT * f0 * i / kSr;
                c1 += y[i] * std::cos (ph); s1 += y[i] * std::sin (ph);
                c2 += y[i] * std::cos (2 * ph); s2 += y[i] * std::sin (2 * ph);
            }
            h2[k] = std::sqrt (c2 * c2 + s2 * s2) / std::sqrt (c1 * c1 + s1 * s1);
        }
        std::printf ("  2nd harmonic at 1 V: BIAS centred %.4f, BIAS 9 %.4f\n", h2[0], h2[1]);
        check (h2[1] > 3.0 * h2[0], "BIAS off-centre clips one side first");
        a.setBiasTrim (0.5); a.setLevelTrim (0.75); a.setClockTrim (0.5);
    }

    std::printf ("\n7. Stability sweep\n");
    {
        const double amps[4] = { 0.01, 0.1, 1.0, 4.0 };
        const double freqs[5] = { 41.0, 110.0, 440.0, 2000.0, 15000.0 };
        const double corners[5][4] = { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 0, 1, 1, 1 }, { 1, 0, 0, 1 }, { 0, 1, 0, 1 } };
        int runs = 0; bool ok = true;
        for (int c = 0; c < 5; ++c)
            for (int ai = 0; ai < 4; ++ai)
                for (int fi = 0; fi < 5; ++fi)
                {
                    a.setManual (corners[c][0]); a.setWidth (corners[c][1]); a.setSpeed (corners[c][2]); a.setRegen (corners[c][3]);
                    a.setEffectOn (true);
                    a.reset();
                    std::vector<float> y = run (a, sine (freqs[fi], amps[ai], 0.4));
                    if (! allFinite (y, 10.0)) ok = false;
                    ++runs;
                }
        unsigned seed = 1;
        for (int r = 0; r < 20; ++r)
        {
            double f[4];
            for (int k = 0; k < 4; ++k) { seed = seed * 1664525u + 1013904223u; f[k] = (seed >> 8) / 16777216.0; }
            a.setManual (f[0]); a.setWidth (f[1]); a.setSpeed (f[2]); a.setRegen (f[3]);
            a.reset();
            std::vector<float> y = run (a, noise (2.0, 0.4, seed));
            if (! allFinite (y, 10.0)) ok = false;
            ++runs;
        }
        std::printf ("  %d runs\n", runs);
        check (ok, "no NaNs or runaways anywhere");
    }

    std::printf ("\n8. Automation, CPU, renders\n");
    {
        defaults (a);
        a.reset();
        std::vector<float> x = makeRiff (4.0), y (x.size());
        for (size_t pos = 0; pos < x.size(); pos += 64)
        {
            const double t = (double) pos / x.size();
            a.setManual (0.5 + 0.5 * std::sin (2 * kPiT * 3 * t));
            a.setWidth (0.5 + 0.5 * std::sin (2 * kPiT * 5 * t));
            a.setSpeed (0.5 + 0.5 * std::sin (2 * kPiT * 6 * t));
            a.setRegen (0.5 + 0.5 * std::sin (2 * kPiT * 7 * t));
            a.setBiasTrim (0.5 + 0.5 * std::sin (2 * kPiT * 8 * t));
            a.setLevelTrim (0.5 + 0.5 * std::sin (2 * kPiT * 10 * t));
            a.setClockTrim (0.5 + 0.5 * std::sin (2 * kPiT * 11 * t));
            a.setEffectOn (std::sin (2 * kPiT * 4 * t) > 0.0);
            a.setInputGainDb (12.0 * std::sin (2 * kPiT * 2 * t));
            a.setOutputGainDb (12.0 * std::sin (2 * kPiT * 9 * t));
            const int n = (int) std::min ((size_t) 64, x.size() - pos);
            a.process (&x[pos], &y[pos], n);
        }
        check (allFinite (y, 10.0), "every control automated while playing");

        defaults (a);
        a.reset();
        std::vector<float> ten = makeChords (10.0);
        std::clock_t t0 = std::clock();
        std::vector<float> yc = run (a, ten);
        const double secs = (double) (std::clock() - t0) / CLOCKS_PER_SEC;
        std::printf ("  CPU: %.1f%% of one core at 44.1 kHz\n", 10.0 * secs);
        check (secs < 3.0, "runs well under real time");

        struct R { const char* name; double m, w, s, r; bool chords; };
        const R renders[] = {
            { "dry_chords",           0.5, 0.5, 0.5, 0.5, true },
            { "noon_chords",          0.5, 0.5, 0.5, 0.5, true },
            { "slow_jet_chords",      0.3, 1.0, 0.25, 0.85, true },
            { "screaming_regen_riff", 0.4, 0.7, 0.3, 0.95, false },
            { "fast_riff",            0.5, 0.6, 0.8, 0.4, false },
            { "manual_comb_riff",     0.2, 0.0, 0.5, 0.8, false },
        };
        std::vector<float> riff = makeRiff (8.0), chords = makeChords (8.0);
        for (size_t k = 0; k < sizeof (renders) / sizeof (renders[0]); ++k)
        {
            defaults (a);
            a.setManual (renders[k].m); a.setWidth (renders[k].w); a.setSpeed (renders[k].s); a.setRegen (renders[k].r);
            a.setEffectOn (k != 0);
            a.reset();
            const std::vector<float>& src = renders[k].chords ? chords : riff;
            std::vector<float> o = run (a, src);
            writeWav (renderDir + "/" + renders[k].name + ".wav", o, kSr);
        }
        check (true, "demo renders written");
    }

    delete ap;
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL DSP TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
