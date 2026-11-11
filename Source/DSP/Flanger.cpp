// Flanger.cpp
// See Flanger.h for the signal path. Component values are from the drawing;
// the estimates (things the drawing doesn't fix) are marked.

#include "Flanger.h"

#include <cmath>
#include <cstring>

namespace tb117
{

namespace
{
    // Supply: single +15 V, signals swing around the 7.5 V reference
    const double kRail4558  = 6.0;             // 4558 swing either side of 7.5 V
    const double kRail5532  = 6.4;             // NE5532 swing
    const double kSlew4558  = 1.0e6;           // V/s (4558, about 1 V/us)
    const double kSlew5532  = 9.0e6;           // V/s (NE5532)

    // Input stage: 0.1u, 1K into 470K bias; 10K feedback, 1K + 33n to ground
    const double kCin = 0.1e-6, kRbiasIn = 470.0e3 + 1.0e3;
    const double kRfIn = 10.0e3, kRgIn = 1.0e3, kCgIn = 33.0e-9;
    // BBD driver: 150K from A, feedback 100K || 100p || (27K + 1N4148 pair)
    const double kRdry = 150.0e3, kRfDrv = 100.0e3, kCfDrv = 100.0e-12, kRdiode = 27.0e3;
    const double kIs = 2.52e-9, kNVt = 1.752 * 0.02585;      // 1N4148
    // 1K / 10n, then 0.1u into 100K at the BBD inputs
    const double kRrc = 1.0e3, kCrc = 10.0e-9, kCbbd = 0.1e-6, kRbbd = 100.0e3;
    // LEVEL trim 22K from the joined outputs to ground, wiper -> 0.1u -> 30K
    const double kLevelPot = 22.0e3, kCout = 0.1e-6;
    // Multiple-feedback low-pass: R1 30K in (plus the trim's source
    // resistance), R2 30K to the output, R3 5K1 to pin 2, C1 6n8 to ground,
    // C2 150p across the op-amp
    const double kMfbRin = 30.0e3, kMfbR2 = 30.0e3, kMfbR3 = 5.1e3;
    const double kMfbC1 = 6.8e-9, kMfbC2 = 150.0e-12;
    // Sallen-Key: 7K5, 2K4, 6n8 to the output, 3n3 to ground
    const double kSkR1 = 7.5e3, kSkR2 = 2.4e3, kSkC1 = 6.8e-9, kSkC2 = 3.3e-9;
    // REGEN: 0.1u, 50K lin pot, 15n + 120K
    const double kCreg = 0.1e-6, kRegPot = 50.0e3, kCregIn = 15.0e-9, kRregIn = 120.0e3;
    // Mixer: 150K dry, 82K wet, 100K || (10K + 3n3); 2.2u into 10K
    const double kRmixDry = 150.0e3, kRmixWet = 82.0e3, kRfMix = 100.0e3;
    const double kRdeMix = 10.0e3, kCdeMix = 3.3e-9, kCoutMix = 2.2e-6, kRload = 10.0e3;

    // Sweep: SWEEP 500K log + 4K7 into 15u + 15u (7u5)
    const double kSweepPot = 500.0e3, kSweepMin = 4.7e3, kLfoC = 7.5e-6;
    // MANUAL 50K lin across the supply, wiper -> 100K, 470K from +15 V
    const double kVcc = 15.0, kManPot = 50.0e3, kRman = 100.0e3, kRmanUp = 470.0e3;
    const double kBufHigh = 13.5;              // 4558 follower's top swing
    // WIDTH 50K lin, wiper -> 22K into the clock's current mirror
    const double kWidPot = 50.0e3, kRcv = 22.0e3, kVbe = 0.62;

    // --- Estimates (not fixed by the drawing) ---------------------------------
    // The LFO triangle's swing. With the bias divider as drawn (15K / 10K, 6 V)
    // the Schmitt's lower threshold falls below the 4558's swing, so the swing
    // is taken as the integrator's usable range on the 15 V supply.
    const double kLfoLow = 1.8, kLfoHigh = 11.4;
    const double kLfoDrive = 6.0;              // volts across the rate resistor
    // CLOCK trim: 500K across the supply, wiper -> 470K into the mirror
    const double kClkPot = 500.0e3, kRclk = 470.0e3;
    inline double trimCurrent (double c)
    {
        const double v = kVcc * c - kVbe;
        return v > 0.0 ? v / (kRclk + c * (1.0 - c) * kClkPot) : 0.0;
    }
    // Clock: delay = kDelayAmps / current (62p ramp, latch threshold, /2 in the
    // 4013), set so MANUAL fully clockwise with WIDTH at 0 gives 0.5 ms with
    // the CLOCK trim centred.
    const double kDelayAmps = 0.5e-3 * ((kBufHigh - kVbe) / kRcv + 6.88 / (kRclk + 0.25 * kClkPot));
    const double kMinDelay = 512.0 / 1.5e6;     // SAD1024 clock limit, 1.5 MHz
    const double kMaxDelay = 25.0e-3;
    // SAD1024: signal gain from pin 2 to the joined outputs 11/12, and the
    // clipping level. The drawing's resistors alone keep the REGEN loop well
    // below 1, yet real M117s scream and self-oscillate near the top of
    // REGEN; the gain is set so that, with the LEVEL trim at its default,
    // the loop reaches 1.15 with REGEN full up (as on the user's pedal).
    // LEVEL then tames it or makes it wilder.
    const double kBbdGain = 2.1;
    const double kBbdClip = 2.0;               // V peak at the input, BIAS centred
    const double kBiasSpan = 2.0;              // V of bias shift either side across the BIAS trim

    inline double audioTaper (double x)
    {
        const double k = 4.394449154672439;    // 10% of the track at mid-rotation
        if (x <= 0.0) return 0.0;
        if (x >= 1.0) return 1.0;
        return (std::exp (k * x) - 1.0) / (std::exp (k) - 1.0);
    }

    inline double clamp01 (double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

    inline double bbdClip (double v)
    {
        const double knee = 0.7 * kBbdClip;
        const double a = std::fabs (v);
        if (a <= knee) return v;
        const double c = knee + (kBbdClip - knee) * std::tanh ((a - knee) / (kBbdClip - knee));
        return v < 0.0 ? -c : c;
    }

    // REGEN's track as it behaves on the user's pedal: the regeneration
    // climbs quickly through the first half, a steady ring at 2 o'clock, wild
    // from 3 o'clock, and the same full-up strength. (The drawing says 50K lin;
    // matched by ear to the hardware.) Wiper fraction for a rotation.
    const double kRegenCurve = 1.76;
    inline double regenTrack (double x)
    {
        if (x <= 0.0) return 0.0;
        if (x >= 1.0) return 1.0;
        return 1.0 - std::pow (1.0 - x, kRegenCurve);
    }

    inline double highpassFc (double r, double c) { return 1.0 / (2.0 * kPi * r * c); }

    // The limiter branch: total feedback current for a diode voltage vd, and
    // the output voltage that goes with it
    inline void limiterPoint (double vd, double& iTotal, double& v)
    {
        const double ib = 2.0 * kIs * std::sinh (vd / kNVt);
        v = vd + kRdiode * ib;
        iTotal = v / kRfDrv + ib;
    }

    const double kTableMaxI = 300.0e-6;        // summing-node current covered by the table

    // Clock current for a WIDTH wiper voltage and source resistance
    inline double clockDelay (double vth, double rth, double itrim)
    {
        double i = (vth - kVbe) / (rth + kRcv);
        if (i < 0.0) i = 0.0;
        i += itrim;
        if (i < 1.0e-6) i = 1.0e-6;
        double d = kDelayAmps / i;
        if (d < kMinDelay) d = kMinDelay;
        if (d > kMaxDelay) d = kMaxDelay;
        return d;
    }
}

//==============================================================================
double Flanger::lfoHz (double pot)
{
    // Log track wired so clockwise takes resistance out: the rate climbs
    // evenly on a log scale.
    const double r = kSweepMin + kSweepPot * audioTaper (1.0 - clamp01 (pot));
    return kLfoDrive / (2.0 * (kLfoHigh - kLfoLow) * r * kLfoC);
}

double Flanger::manualVolts (double pot)
{
    const double m = clamp01 (pot);
    const double rw = kRman + m * (1.0 - m) * kManPot;      // wiper source + 100K
    double v = (kVcc * m / rw + kVcc / kRmanUp) / (1.0 / rw + 1.0 / kRmanUp);
    if (v > kBufHigh) v = kBufHigh;
    return v;
}

double Flanger::delayMs (double manualPot, double widthPot, double lfo, double clockTrim)
{
    const double w = clamp01 (widthPot);
    const double vm = manualVolts (manualPot);
    const double vl = kLfoLow + (kLfoHigh - kLfoLow) * 0.5 * (lfo + 1.0);
    return 1000.0 * clockDelay (vm + w * (vl - vm), w * (1.0 - w) * kWidPot, trimCurrent (clamp01 (clockTrim)));
}

double Flanger::regenLoopGain (double regenPot, double levelTrim)
{
    const double x = regenTrack (clamp01 (regenPot)), t = clamp01 (levelTrim);
    const double drv = kRfDrv / (kRregIn + x * (1.0 - x) * kRegPot);           // wiper -> driver
    const double mfbGain = kMfbR2 / (kMfbRin + t * (1.0 - t) * kLevelPot);    // LEVEL wiper -> MFB
    return x * drv * kBbdGain * t * mfbGain;                                    // Sallen-Key: x1
}

double Flanger::currentClockHz() const
{
    return lastDelay > 0.0 ? (kStages / 2) / lastDelay : 0.0;
}

double Flanger::driverOut (double i) const { return limiter (i); }

double Flanger::wetFilterDb (double hz)
{
    // |H(jw)| of both low-pass sections from their component values
    const double w = 2.0 * kPi * hz;
    const double rin = kMfbRin + 0.75 * 0.25 * kLevelPot;    // LEVEL trim at its default
    const double g = kMfbR2 / rin;
    const double r1 = 1.0 - w * w * kMfbC1 * kMfbC2 * kMfbR2 * kMfbR3;
    const double i1 = w * kMfbC2 * (kMfbR2 + kMfbR3 + kMfbR2 * kMfbR3 / rin);
    const double r2 = 1.0 - w * w * kSkR1 * kSkR2 * kSkC1 * kSkC2;
    const double i2 = w * kSkC2 * (kSkR1 + kSkR2);
    const double m = g / std::sqrt ((r1 * r1 + i1 * i1) * (r2 * r2 + i2 * i2));
    return 20.0 * std::log10 (m);
}

//==============================================================================
Flanger::Flanger()
    : fsBase (44100.0), fsOs (176400.0),
      manT (0.5), widT (0.5), spdT (0.5), regT (0.5), effectT (1.0), inT (1.0), outT (1.0),
      biasT (0.5), levelT (0.75), clockT (0.5),
      regenSet (-1.0), levelSet (-1.0), tableStep (1.0), tableSlopeOut (0.0),
      ringPos (0), tickPhase (0.0), bbdHeld (0.0), lastBbdIn (0.0), wetNode (0.0),
      lfoPhase (0.25), lfoTri (0.0), lastDelay (1.0e-3), peakBbdLevel (0.0)
{
    // Limiter table: solve the feedback network for evenly spaced currents
    tableStep = 2.0 * kTableMaxI / (kTable - 1);
    for (int k = 0; k < kTable; ++k)
    {
        const double target = -kTableMaxI + k * tableStep;
        double lo = -1.5, hi = 1.5, v = 0.0, it = 0.0;
        for (int n = 0; n < 80; ++n)
        {
            const double mid = 0.5 * (lo + hi);
            limiterPoint (mid, it, v);
            if (it < target) lo = mid; else hi = mid;
        }
        limiterPoint (0.5 * (lo + hi), it, v);
        table[k] = v;
    }
    // Beyond the table the diodes are hard on: slope of 100K || 27K
    tableSlopeOut = kRfDrv * kRdiode / (kRfDrv + kRdiode);
    prepare (44100.0);
}

double Flanger::limiter (double i) const
{
    const double x = (i + kTableMaxI) / tableStep;
    if (x <= 0.0) return table[0] + (i + kTableMaxI) * tableSlopeOut;
    if (x >= kTable - 1) return table[kTable - 1] + (i - kTableMaxI) * tableSlopeOut;
    const int k = (int) x;
    const double f = x - k;
    return table[k] + (table[k + 1] - table[k]) * f;
}

void Flanger::prepare (double sampleRate)
{
    fsBase = sampleRate > 1000.0 ? sampleRate : 44100.0;
    fsOs = fsBase * Oversampler4x::kFactor;

    smKnob   = std::exp (-1.0 / (0.020 * fsBase));
    smRate   = std::exp (-1.0 / (0.150 * fsBase));
    smSwitch = std::exp (-1.0 / (0.006 * fsBase));

    inHp.highpass (highpassFc (kRbiasIn, kCin), fsOs);
    // 1 + Rf / (Rg + 1/sC) = (s C (Rf + Rg) + 1) / (s C Rg + 1)
    preEmph.setAnalog (kCgIn * (kRfIn + kRgIn), 1.0, kCgIn * kRgIn, 1.0, fsOs);

    {
        double f = 1.0 / (2.0 * kPi * kRfDrv * kCfDrv);
        if (f > 0.45 * fsOs) f = 0.45 * fsOs;
        drvLp.lowpass (f, fsOs);
        f = 1.0 / (2.0 * kPi * kRrc * kCrc);
        if (f > 0.45 * fsOs) f = 0.45 * fsOs;
        rcLp.lowpass (f, fsOs);
    }
    bbdHp.highpass (highpassFc (kRbbd, kCbbd), fsOs);
    midHp.highpass (highpassFc (kRbbd, kCbbd), fsOs);
    levelSet = -1.0;
    level = levelT;
    updateLevelTrim();
    // Sallen-Key: 1 / (s^2 R1 R2 C1 C2 + s C2 (R1 + R2) + 1)
    sk.setAnalog (0.0, 0.0, 1.0, kSkR1 * kSkR2 * kSkC1 * kSkC2, kSkC2 * (kSkR1 + kSkR2), 1.0, fsOs);

    regHp.highpass (highpassFc (kRegPot, kCreg), fsOs);
    regenSet = -1.0;
    updateRegenFilter();

    // Mixer feedback 100K (1 + s 10K C) / (1 + s 110K C), normalised to 100K
    deEmph.setAnalog (kRdeMix * kCdeMix, 1.0, (kRdeMix + kRfMix) * kCdeMix, 1.0, fsOs);
    outHp.highpass (highpassFc (kRload, kCoutMix), fsOs);

    slewIn.step  = slewMix.step = kSlew5532 / fsOs;
    slewDrv.step = slewMfb.step = slewSk.step = kSlew4558 / fsOs;

    reset();
}

void Flanger::updateLevelTrim()
{
    if (std::fabs (level - levelSet) < 1.0e-4) return;
    levelSet = level;
    // The trim's wiper is the MFB's source: R1 = 30K + t (1 - t) 22K
    const double r1 = kMfbRin + level * (1.0 - level) * kLevelPot;
    outCouple.highpass (highpassFc (r1, kCout), fsOs);
    // MFB: -(R2/R1) / (s^2 C1 C2 R2 R3 + s C2 (R2 + R3 + R2 R3 / R1) + 1)
    mfb.setAnalog (0.0, 0.0, -kMfbR2 / r1,
                   kMfbC1 * kMfbC2 * kMfbR2 * kMfbR3,
                   kMfbC2 * (kMfbR2 + kMfbR3 + kMfbR2 * kMfbR3 / r1), 1.0, fsOs);
}

void Flanger::updateRegenFilter()
{
    if (std::fabs (reg - regenSet) < 1.0e-4) return;
    regenSet = reg;
    // Wiper: Thevenin x * V through x (1 - x) 50K, then 15n + 120K into the
    // virtual earth: current = x V s C / (s C (Rth + 120K) + 1)
    const double w = regenTrack (reg);
    const double rs = kRregIn + w * (1.0 - w) * kRegPot;
    regNet.setAnalog (w * kCregIn, 0.0, kCregIn * rs, 1.0, fsOs);
}

void Flanger::reset()
{
    man = manT; wid = widT; spd = spdT; reg = regT; effect = effectT; inG = inT; outG = outT;
    bias = biasT; level = levelT; clock = clockT;
    regenSet = -1.0;
    updateRegenFilter();
    levelSet = -1.0;
    updateLevelTrim();

    up.reset(); down.reset();
    inHp.reset(); preEmph.reset(); drvLp.reset(); rcLp.reset(); bbdHp.reset(); midHp.reset();
    outCouple.reset(); mfb.reset(); sk.reset(); regHp.reset(); regNet.reset();
    deEmph.reset(); outHp.reset();
    slewIn.reset(); slewDrv.reset(); slewMfb.reset(); slewSk.reset(); slewMix.reset();

    std::memset (ring, 0, sizeof (ring));
    ringPos = 0;
    tickPhase = 0.0; bbdHeld = 0.0; lastBbdIn = 0.0; wetNode = 0.0;
    lfoPhase = 0.25; lfoTri = 0.0;
    lastDelay = delayMs (man, wid, 0.0, clock) * 1.0e-3;
    peakBbdLevel = 0.0;
}

void Flanger::setManual (double v)         { manT = clamp01 (v); }
void Flanger::setWidth (double v)          { widT = clamp01 (v); }
void Flanger::setSpeed (double v)          { spdT = clamp01 (v); }
void Flanger::setRegen (double v)          { regT = clamp01 (v); }
void Flanger::setBiasTrim (double v)       { biasT = clamp01 (v); }
void Flanger::setLevelTrim (double v)      { levelT = clamp01 (v); }
void Flanger::setClockTrim (double v)      { clockT = clamp01 (v); }
void Flanger::setEffectOn (bool on)        { effectT = on ? 1.0 : 0.0; }
void Flanger::setInputGainDb (double dB)   { inT = std::pow (10.0, dB / 20.0); }
void Flanger::setOutputGainDb (double dB)  { outT = std::pow (10.0, dB / 20.0); }

//==============================================================================
double Flanger::tickOs (double x)
{
    // LFO: integrator + Schmitt trigger, a linear triangle
    lfoPhase += lfoHz (spd) / fsOs;
    if (lfoPhase >= 1.0) lfoPhase -= std::floor (lfoPhase);
    lfoTri = lfoPhase < 0.5 ? 4.0 * lfoPhase - 1.0 : 3.0 - 4.0 * lfoPhase;

    // Clock: WIDTH's wiper between the MANUAL buffer and the triangle
    {
        const double vm = manualVolts (man);
        const double vl = kLfoLow + (kLfoHigh - kLfoLow) * 0.5 * (lfoTri + 1.0);
        lastDelay = clockDelay (vm + wid * (vl - vm), wid * (1.0 - wid) * kWidPot, trimCurrent (clock));
    }
    const double delay = lastDelay;

    // Input stage (NE5532, pre-emphasis)
    const double a = slewIn.tick (railClip (preEmph.tick (inHp.tick (x * inG)), kRail5532));

    // REGEN from the wet node (one sample back; the BBD delay is far longer)
    const double iReg = regNet.tick (regHp.tick (wetNode));

    // BBD driver: inverting, diode-limited feedback, 100p across it
    const double drv = slewDrv.tick (railClip (drvLp.tick (-limiter (a / kRdry + iReg)), kRail4558));

    // 1K / 10n, coupling into the SAD1024; the BIAS trim moves the operating
    // point, so one side clips first (the DC is blocked further on)
    const double off = (bias - 0.5) * 2.0 * kBiasSpan;
    const double bin = bbdClip (bbdHp.tick (rcLp.tick (drv)) + off);
    {
        const double lvl = std::fabs (bin) / kBbdClip;
        if (lvl > peakBbdLevel) peakBbdLevel = lvl;
    }

    // SAD1024, 1024 stages: the two output phases interleave into a stream at
    // twice the clock, each value the average of the two newest buckets
    // 1024 ticks back (the 22K trim balancing pins 11 and 12).
    double step = (kStages / delay) / fsOs;                  // ticks per oversampled sample
    if (step < 1.0e-3) step = 1.0e-3;
    const double inv = 1.0 / step;
    double pos = (1.0 - tickPhase) * inv;
    double last = 0.0, acc = 0.0;
    int guard = 0;
    while (pos <= 1.0 && guard < 256)
    {
        acc += bbdHeld * (pos - last);
        const double s = lastBbdIn + (bin - lastBbdIn) * pos;
        ring[ringPos] = (float) s;
        const int ia = (ringPos - kStages) & (kRing - 1);
        const int ib = (ringPos - kStages - 1) & (kRing - 1);
        bbdHeld = 0.5 * ((double) ring[ia] + (double) ring[ib]);
        ringPos = (ringPos + 1) & (kRing - 1);
        last = pos;
        pos += inv;
        ++guard;
    }
    acc += bbdHeld * (1.0 - last);
    tickPhase = 1.0 - (pos - 1.0) * step;
    if (tickPhase < 0.0) tickPhase = 0.0;
    if (tickPhase >= 1.0) tickPhase = 0.999999;
    lastBbdIn = bin;

    // Between the sections (0.1u / 100K) and out through the LEVEL trim
    const double bout = outCouple.tick (midHp.tick (kBbdGain * level * acc));

    // Wet filters: MFB (inverting) then Sallen-Key
    const double m = slewMfb.tick (railClip (mfb.tick (bout), kRail4558));
    wetNode = slewSk.tick (railClip (sk.tick (m), kRail4558));

    // Mixer: -(100K || (10K + 3n3)) (A / 150K + W / 82K)
    const double i = a / kRmixDry + wetNode / kRmixWet;
    const double mix = slewMix.tick (railClip (-kRfMix * deEmph.tick (i), kRail5532));
    const double wetOut = outHp.tick (mix);

    // Footswitch: effect in, or straight through
    return effect * wetOut + (1.0 - effect) * x;
}

void Flanger::process (const float* in, float* out, int numSamples)
{
    const double aK = 1.0 - smKnob, aR = 1.0 - smRate, aW = 1.0 - smSwitch;
    double xs[Oversampler4x::kFactor], ys[Oversampler4x::kFactor];

    for (int n = 0; n < numSamples; ++n)
    {
        man += (manT - man) * aK;
        wid += (widT - wid) * aK;
        spd += (spdT - spd) * aR;
        reg += (regT - reg) * aK;
        effect += (effectT - effect) * aW;
        inG += (inT - inG) * aK;
        outG += (outT - outG) * aK;
        bias += (biasT - bias) * aK;
        level += (levelT - level) * aK;
        clock += (clockT - clock) * aK;
        updateRegenFilter();
        updateLevelTrim();

        float s = in[n];
        if (! (s == s)) s = 0.0f;
        up.upsample ((double) s, xs);
        for (int k = 0; k < Oversampler4x::kFactor; ++k)
            ys[k] = tickOs (xs[k]);
        out[n] = (float) (down.downsample (ys) * outG);
    }
}

} // namespace tb117
