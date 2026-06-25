#pragma once
#include "../../Core/Types.hpp"

// ─── Runtime-tunable gait parameters (Ironclad AE animation editor) ───────────
// Four editable presets — idle, walk, jog, run — each a full GaitParams set plus
// per-field phase offsets. The editor previews + edits one preset at a time.

struct GaitParams {
    f32 duration;     // one full stride (two steps), seconds
    f32 thighAmp;     // hip fore/aft swing amplitude (deg)
    f32 thighBias;    // forward mean of hip swing (deg)
    f32 kneeBase;     // stance knee flex (deg)
    f32 kneeSwing;    // extra swing-phase knee flex (deg)
    f32 armAmp;       // shoulder fore/aft swing (deg)
    f32 lean;         // constant forward torso lean (deg)
    f32 bob;          // pelvis vertical bob amplitude (world units)
    f32 sway;         // pelvis lateral sway amplitude (world units)
    f32 pelvYaw;      // pelvis transverse rotation (deg)
    f32 foot;         // ankle pitch amplitude (deg)
    f32 elbowSwing;   // dynamic elbow flex over the swing (deg, on top of a 28° base)
};

static constexpr int kGaitFieldCount   = 12;         // floats per GaitParams
static constexpr int kGaitPresetCount   = 4;         // idle, walk, jog, run

extern GaitParams gGaitParams[kGaitPresetCount];     // values
extern GaitParams gGaitPhase[kGaitPresetCount];      // per-field phase offsets (deg)

const char* GaitPresetName(int preset);   // "idle" / "walk" / "jog" / "run"
const char* GaitClipName(int preset);     // "soldier_idle" ...
const char* GaitFieldLabel(int field);    // "duration" / "thighAmp" / ...

f32  GaitParamGet(int preset, int field);
void GaitParamSet(int preset, int field, f32 value);
f32  GaitPhaseGet(int preset, int field);
void GaitPhaseSet(int preset, int field, f32 value);