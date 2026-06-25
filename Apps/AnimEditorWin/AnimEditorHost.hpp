// AnimEditorHost.hpp — portable (no platform deps) animation-editor core.
// Drives one soldier's AnimationController and the four editable gait presets.
// Reused by the Windows app; equivalent to the macOS EngineHost's editor logic.

#pragma once
#include "../../Engine/Animation/Animation.hpp"
#include "../../Engine/Simulation/Components.hpp"
#include <string>

class AnimEditorHost {
public:
    void Init();
    void Update(f32 dt);          // advances the controller on the selected preset

    // Preset selection (0=idle 1=walk 2=jog 3=run)
    void        CyclePreset();
    int         Preset()    const { return m_preset; }
    const char* PresetName() const { return GaitPresetName(m_preset); }

    // Editable parameters for the current preset
    int         FieldCount() const { return kGaitFieldCount; }
    const char* FieldLabel(int f) const { return GaitFieldLabel(f); }
    f32  Value(int f) const { return GaitParamGet(m_preset, f); }
    void SetValue(int f, f32 v);
    f32  Phase(int f) const { return GaitPhaseGet(m_preset, f); }
    void SetPhase(int f, f32 v);

    // Per-frame skinning output (humanoid skeleton space) for the renderer/retarget.
    const Mat4* SkinningMatrices() const { return m_ctrl.GetSkinningMatrices(); }
    const Mat4* ModelMatrices()    const { return m_ctrl.modelPose.modelMats; }
    int         BoneCount()        const { return m_ctrl.GetBoneCount(); }
    const Skeleton& GetSkeleton()  const { return m_skel; }

    // Persistence (simple text file — replaces macOS NSUserDefaults).
    void Save(const std::string& path) const;
    void Load(const std::string& path);

private:
    void Rebuild();   // rebuild clips after an edit, preserving the current clip phase

    Skeleton            m_skel;
    AnimationLibrary    m_lib;
    AnimationController  m_ctrl;
    int  m_preset { 0 };
    bool m_dirty  { false };
};
