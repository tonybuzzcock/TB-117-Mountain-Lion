// Params.h
// Parameter table shared by the VST wrapper, the editor and the preset store.
//
// Front-panel controls first (the four knobs left to right and the
// footswitch), then the plugin-only controls in the editor's bottom strip,
// then the PCB trimpots (in the drop-down panel).

#ifndef TB117_PARAMS_H
#define TB117_PARAMS_H

namespace tb117
{

enum Param
{
    // Front panel
    kManual = 0, kWidth, kSpeed, kRegen, kEffect,
    // Plugin only (bottom strip of the editor)
    kInput, kOutput,
    // PCB trimpots (drop-down panel)
    kBiasTrim, kLevelTrim, kClockTrim,
    kNumParams
};

enum Kind { kKnob, kRateHz, kSwitch, kDb12, kDb24 };

const char* const kParamNames[kNumParams] = {
    "Manual", "Width", "Speed", "Regen", "Effect",
    "Input", "Output",
    "BiasTrim", "LvlTrim", "ClkTrim"
};

const int kParamKind[kNumParams] = {
    kKnob, kKnob, kRateHz, kKnob, kSwitch,
    kDb12, kDb24,
    kKnob, kKnob, kKnob
};

const float kDefaults[kNumParams] = {
    0.5f, 0.5f, 0.5f, 0.5f, 1.0f,       // MXR: start with every knob at noon
    0.5f, 0.5f,
    0.5f, 0.75f, 0.5f
};

const double kInputRangeDb  = 12.0;
const double kOutputRangeDb = 24.0;

inline bool isOn (float v) { return v >= 0.5f; }

} // namespace tb117

#endif
