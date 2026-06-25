// AnimEditorHost.cpp — portable animation-editor core implementation.

#include "AnimEditorHost.hpp"
#include <cstdio>

void AnimEditorHost::Init() {
    m_skel = Skeleton::CreateHumanoid();
    m_lib  = AnimationLibrary::BuildSoldierLibrary(m_skel);
    m_ctrl.Init(&m_skel, &m_lib);
    m_ctrl.overrideClip = m_lib.Find(GaitClipName(m_preset));
}

void AnimEditorHost::Update(f32 dt) {
    if (m_dirty) Rebuild();
    m_ctrl.overrideClip = m_lib.Find(GaitClipName(m_preset));
    TransformComponent xf;            // soldier stays at origin; editor previews in place
    m_ctrl.Update(dt, xf);
}

void AnimEditorHost::Rebuild() {
    f32 clipTime = m_ctrl.sampler.GetCurrentTime();
    m_lib = AnimationLibrary::BuildSoldierLibrary(m_skel);
    m_ctrl.library = &m_lib;
    m_ctrl.overrideClip = m_lib.Find(GaitClipName(m_preset));
    if (m_ctrl.overrideClip) {
        m_ctrl.sampler.SetClip(m_ctrl.overrideClip, 0.f);
        m_ctrl.sampler.layers[0].time = clipTime;
    }
    m_dirty = false;
}

void AnimEditorHost::CyclePreset() {
    m_preset = (m_preset + 1) % kGaitPresetCount;
    m_ctrl.overrideClip = m_lib.Find(GaitClipName(m_preset));
}

void AnimEditorHost::SetValue(int f, f32 v) {
    if (GaitParamGet(m_preset, f) == v) return;
    GaitParamSet(m_preset, f, v);
    m_dirty = true;
}
void AnimEditorHost::SetPhase(int f, f32 v) {
    if (GaitPhaseGet(m_preset, f) == v) return;
    GaitPhaseSet(m_preset, f, v);
    m_dirty = true;
}

void AnimEditorHost::Save(const std::string& path) const {
    FILE* fp = std::fopen(path.c_str(), "w");
    if (!fp) return;
    for (int p = 0; p < kGaitPresetCount; ++p)
        for (int f = 0; f < kGaitFieldCount; ++f)
            std::fprintf(fp, "%d %d %g %g\n", p, f, GaitParamGet(p, f), GaitPhaseGet(p, f));
    std::fclose(fp);
}
void AnimEditorHost::Load(const std::string& path) {
    FILE* fp = std::fopen(path.c_str(), "r");
    if (!fp) return;
    int p, f; float v, ph;
    while (std::fscanf(fp, "%d %d %g %g", &p, &f, &v, &ph) == 4) {
        if (p >= 0 && p < kGaitPresetCount && f >= 0 && f < kGaitFieldCount) {
            GaitParamSet(p, f, v);
            GaitPhaseSet(p, f, ph);
        }
    }
    std::fclose(fp);
}
