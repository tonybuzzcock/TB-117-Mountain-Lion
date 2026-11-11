// Flanger.h
// MXR M117 Flanger (the original SAD1024 version; drawing "MXR Flanger
// Original" by Stellan Lehrberg), modelled from the schematic. Single +15 V
// supply, everything biased at the 7.5 V reference (30K / 30K, 2.2u).
//
//   input -> 0.1u / 1K -> NE5532 (pins 5-7), 470K bias: non-inverting
//      pre-emphasis, 10K feedback with 1K + 33n to ground (x1 at DC, x11
//      above 4.8 kHz)                                         = node A
//   A -> 150K -> 4558 BBD driver (pins 5-7, inverting): feedback
//      100K || 100p || (27K + 1N4148 pair); REGEN also sums in here
//   -> 1K / 10n -> 0.1u (100K to the bias trim) -> SAD1024 section A (pin 2)
//   -> outputs 5/6 -> 0.1u -> section B (pin 15): both 512-stage sections
//      in series, 1024 stages
//   -> outputs 11/12 joined into the 22K LEVEL trim, wiper -> 0.1u -> 30K
//   -> 4558 multiple-feedback low-pass (30K, 5K1, 6n8, 150p; gain -1)
//   -> 4558 Sallen-Key low-pass (7K5, 2K4, 6n8, 3n3)          = node W
//   NE5532 mixer (pins 1-3, inverting): A through 150K, W through 82K,
//      feedback 100K || (10K + 3n3) (de-emphasis, the inverse of the input
//      stage) -> 2.2u -> 1K -> output (10K to ground)
//   REGEN: W -> 0.1u -> 50K lin pot -> wiper -> 15n -> 120K -> driver input
//
//   Sweep: 4558 integrator (15u + 15u back to back, SWEEP 500K log + 4K7)
//   and 4558 Schmitt trigger (82K / 100K) make a triangle LFO; MANUAL (50K
//   lin, 100K / 470K) is buffered by a 4558 follower; WIDTH (50K lin) runs
//   from the MANUAL voltage to the triangle, and its wiper drives the clock's
//   current source (22K into a BC548 mirror, plus the 500K / 470K trim). The
//   ramp on 62p (BC557 / BC547 latch) is divided by the 4013 into the
//   SAD1024's two-phase clock.
//
// Modelled as a circuit: the audio path's resistors, capacitors and op-amps
// (output swing and slew on the single supply), the diode limiter, the BBD as
// a clocked sample-and-hold chain with its clock aliasing and staircase
// output, MANUAL and WIDTH as the pots they are (with the wiper's loading).
// Modelled behaviourally (values estimated, see Flanger.cpp): the clock
// oscillator's frequency per microamp, the LFO's swing, the SAD1024's gain and
// clipping level.
//
// Volts in, volts out (1.0 = 1 V). Runs at 4x with Oversampler4x.
// Plain C++98, no allocation after construction.

#ifndef TB117_FLANGER_H
#define TB117_FLANGER_H

#include "Filters.h"
#include "Oversampler4x.h"

namespace tb117
{

class Flanger
{
public:
    Flanger();

    void prepare (double sampleRate);
    void reset();

    // Front panel (0..1 pot rotation), in the pedal's order
    void setManual (double v);          // 50K lin: delay with WIDTH at 0
    void setWidth (double v);           // 50K lin: MANUAL voltage -> LFO
    void setSpeed (double v);           // SWEEP 500K log: LFO rate
    void setRegen (double v);           // 50K lin: feedback
    void setEffectOn (bool on);         // footswitch: effect in / bypass
    // PCB trimpots (0..1 rotation)
    void setBiasTrim (double v);        // 5K: SAD1024 bias, headroom and clipping
    void setLevelTrim (double v);       // 22K: wet level out of the SAD1024
    void setClockTrim (double v);       // 500K: clock offset current, delay range
    // Plugin only
    void setInputGainDb (double dB);
    void setOutputGainDb (double dB);

    // Mono in, mono out
    void process (const float* in, float* out, int numSamples);

    static int latencySamples() { return Oversampler4x::latencySamples(); }

    // Introspection for the panel and tests
    static double lfoHz (double speedPot);
    static double manualVolts (double manualPot);
    // Delay for a pot setting and an LFO voltage (lfo -1..1 across its swing)
    static double delayMs (double manualPot, double widthPot, double lfo, double clockTrim = 0.5);
    // Small-signal gain round the REGEN loop in the mid band (1 = the edge
    // of self-oscillation)
    static double regenLoopGain (double regenPot, double levelTrim);
    double currentDelayMs() const { return 1000.0 * lastDelay; }
    double currentClockHz() const;      // the SAD1024 clock
    double lfoValue() const { return lfoTri; }                   // -1..1
    double peakBbd() const { return peakBbdLevel; }
    void   clearPeakBbd() { peakBbdLevel = 0.0; }
    double driverOut (double inputCurrentAmps) const;            // limiter curve, V
    static double wetFilterDb (double hz);                       // MFB + Sallen-Key, analog

    enum { kStages = 1024, kRing = 4096, kTable = 2049 };

private:
    double tickOs (double x);
    void   updateRegenFilter();
    void   updateLevelTrim();
    double limiter (double i) const;

    double fsBase, fsOs;
    Oversampler4x up, down;

    // Targets and smoothed values
    double manT, widT, spdT, regT, effectT, inT, outT, biasT, levelT, clockT;
    double man, wid, spd, reg, effect, inG, outG, bias, level, clock;
    double smKnob, smRate, smSwitch;
    double regenSet, levelSet;

    // Audio path
    FirstOrder inHp;            // 0.1u into the 470K bias
    FirstOrder preEmph;         // input stage: 10K over 1K + 33n
    FirstOrder drvLp;           // 100p across the driver's 100K
    FirstOrder rcLp;            // 1K / 10n into the BBD
    FirstOrder bbdHp;           // 0.1u / 100K at pin 2
    FirstOrder midHp;           // 0.1u / 100K between the sections
    FirstOrder outCouple;       // 0.1u into the 30K
    Biquad     mfb;             // multiple-feedback low-pass
    Biquad     sk;              // Sallen-Key low-pass
    FirstOrder regHp;           // 0.1u into the REGEN pot
    FirstOrder regNet;          // 15n + 120K into the driver's summing node
    FirstOrder deEmph;          // mixer: 100K || (10K + 3n3)
    FirstOrder outHp;           // 2.2u into the 10K
    Slew slewIn, slewDrv, slewMfb, slewSk, slewMix;

    // Diode limiter: driver output against summing-node current
    double table[kTable];
    double tableStep, tableSlopeOut;

    // BBD: one stream at twice the clock (the two output phases interleaved)
    float ring[kRing];
    int ringPos;
    double tickPhase, bbdHeld, lastBbdIn, wetNode;

    // LFO
    double lfoPhase, lfoTri;

    double lastDelay;
    double peakBbdLevel;
};

} // namespace tb117

#endif
