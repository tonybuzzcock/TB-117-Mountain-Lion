# TB 117 Flanger (Mountain Lion VST)

A model of the original **MXR M117 Flanger** (the SAD1024 version), built from the schematic
("MXR Flanger Original", drawing by Stellan Lehrberg, in `Docs/`), as a **VST 2.4, 64-bit Intel
plugin for Mac OS X 10.8.5** (Pro Tools 11 through Blue Cat PatchWork). Non-commercial, for
personal use.

The modern Mac version (CLAP / VST3 / AU) lives in `TB-117-2026-MAC`.

## Controls

| Panel | What it is in the circuit |
|---|---|
| MANUAL | 50K lin across the supply, buffered (100K / 470K): sets the BBD clock with WIDTH at 0 (about 2.8 ms fully anticlockwise to 0.5 ms clockwise) |
| WIDTH | 50K lin from the MANUAL voltage to the LFO triangle; its wiper drives the clock (wiper loading included). At 10 the LFO alone sets the delay, about 0.6 - 4.3 ms |
| SPEED | SWEEP 500K log + 4K7 into the 15u + 15u integrator: about 0.08 Hz to 8.9 Hz |
| REGEN. | 50K lin from the filtered wet signal, 15n + 120K back into the BBD driver |
| Footswitch | Effect in / bypass; the red LED lights with the effect in |
| Input, Output | Plugin-only trims (±12 dB, ±24 dB) |

Presets: 8 slots in `~/Library/Application Support/TB/TB 117 Flanger/Presets.txt`.

## The model

- **Input stage** (NE5532): 10K over 1K + 33n, x1 at DC rising to x11 above 4.8 kHz (pre-emphasis).
- **BBD driver** (4558, inverting): 100K || 100p || (27K + 1N4148 pair) over 150K. The diodes
  soften big swings before the BBD. Solved from the diode equation (table built at start-up).
- **SAD1024**: both 512-stage sections in series (1024 stages), clocked sample-and-hold chain with
  the clock aliasing and staircase output, 1K / 10n in front, 0.1u couplings.
- **Wet filter**: 4558 multiple-feedback low-pass (30K + trim, 30K, 5K1, 6n8, 150p) and Sallen-Key
  (7K5, 2K4, 6n8, 3n3): flat within 2 dB to 9 kHz, -31 dB at 25 kHz.
- **Mixer** (NE5532): dry 150K, wet 82K, feedback 100K || (10K + 3n3), the exact inverse of the
  input stage's pre-emphasis.
- Op-amp output swing and slew on the single 15 V supply; 4x oversampling.

### Estimates (things the drawing doesn't fix; tune by ear against a real pedal)

All are named constants in `Source/DSP/Flanger.cpp`:

- **Clock per microamp** (`kDelayAmps`): set so MANUAL fully clockwise with WIDTH at 0 gives 0.5 ms.
  The real figure depends on the 62p ramp, the BC557 / BC547 latch threshold and the clock trim.
- **Clock trim current** (`kItrim`, 15 µA, trim centred).
- **LFO swing** (`kLfoLow` / `kLfoHigh`, 1.8 - 11.4 V). As drawn, the bias divider (15K / 10K, 6 V)
  puts the Schmitt trigger's lower threshold below the 4558's swing, which would stop the LFO; the
  swing is taken as the integrator's usable range on the 15 V supply.
- **SAD1024** gain through both sections (0.8) and clipping level (2 V peak).
- The drawing shows **1M and 4.7µ** between WIDTH's wiper and the clock input. Taken literally that
  would smooth the sweep away (a 4.7 s time constant into the 22K input), so the wiper drives the
  clock directly. It is probably a supply or start-up filter drawn in the wrong place.

## Build and install

GitHub Actions builds on every push (`.github/workflows/build.yml`): Linux DSP and plugin tests,
then the Mac bundle on an Intel runner with a check that it declares Mac OS X 10.8.5 as its
minimum and only links libraries that ship with 10.8.5. Panel screenshots go to the `gui-preview`
branch.

1. Actions -> latest run -> Artifacts -> **TB-117-mountain-lion-vst** -> download.
2. Unzip twice, copy **TB 117 Flanger.vst** to `/Library/Audio/Plug-Ins/VST/`.
3. Rescan in Blue Cat PatchWork.

Local tests (Linux or macOS): `make -C Tests run vst-run`, and `make -C Tests asan`.
