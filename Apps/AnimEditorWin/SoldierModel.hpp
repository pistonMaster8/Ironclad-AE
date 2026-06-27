// SoldierModel.hpp — portable (no platform deps) skinned glTF soldier.
//
// This is the scalar (PFGE_USE_SIMD=0) port of the GltfModel machinery in
// MetalRenderer.mm: it loads Soldier.glb (cgltf), builds the Mixamo→humanoid
// retarget map, and produces per-frame skinning matrices by retargeting the
// procedural humanoid pose onto the mesh's own bind skeleton. The Windows D3D11
// shell uploads vertices/indices once and the bone matrices each frame.
//
// Math uses the engine's row-major scalar Mat4/Quat (Engine/Core/Math.hpp); the
// math is identical to the Metal version, only the storage convention differs.

#pragma once
#include "../../Engine/Core/Math.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace ae {

// Matches the Metal SkinnedGpuVertex layout (tightly packed, 56 bytes) so the
// D3D11 input layout offsets line up: pos@0 nrm@12 uv@24 joints@32 weights@40.
struct SkinnedVertex {
    float    px, py, pz;     // offset  0
    float    nx, ny, nz;     // offset 12
    float    u,  v;          // offset 24
    uint16_t joints[4];      // offset 32
    float    weights[4];     // offset 40
};                           // total  56
static_assert(sizeof(SkinnedVertex) == 56, "SkinnedVertex must stay tightly packed");

inline constexpr int kMaxJoints = 64;   // mirrors MetalRenderer kMaxJoints
inline constexpr int kMaxNodes  = 256;  // mirrors MetalRenderer kMaxNodes

class SoldierModel {
public:
    // Parse a .glb/.gltf, upload-ready mesh + skeleton + retarget map. false on error.
    bool Load(const char* path);

    bool  Loaded()     const { return m_loaded; }
    float ModelScale() const { return m_scale; }   // glTF→~2-unit-tall normalisation

    const std::vector<SkinnedVertex>& Vertices() const { return m_vertices; }
    const std::vector<uint32_t>&      Indices()  const { return m_indices; }

    // Retarget the procedural humanoid pose onto this mesh, writing kMaxJoints
    // skinning matrices (row-major). humanoidModelMats are the controller's
    // model-space bone matrices, indexed by HumanoidBone ordinal.
    void ComputeBones(const Mat4* humanoidModelMats, int humanoidBoneCount,
                      Mat4* outBones /*[kMaxJoints]*/) const;

    // # of joints mapped to a humanoid bone — viewport falls back to bind pose
    // (rest) when this is too low to drive a believable retarget.
    int RetargetMappedCount() const { return m_retargetMappedCount; }

private:
    bool m_loaded = false;

    std::vector<SkinnedVertex> m_vertices;
    std::vector<uint32_t>      m_indices;   // always 32-bit (drawn as R32_UINT)
    float                      m_scale = 1.0f;

    // ─── Skeleton / skin ─────────────────────────────────────────────────────
    uint32_t m_jointCount = 0;
    Mat4     m_invBind[kMaxJoints];
    int      m_jointNode[kMaxJoints];

    int   m_nodeCount = 0;
    int   m_nodeParent[kMaxNodes];
    bool  m_nodeHasMat[kMaxNodes];
    Mat4  m_nodeMat[kMaxNodes];     // used when m_nodeHasMat[i]
    float m_nodeT[kMaxNodes][3];    // rest translation
    float m_nodeR[kMaxNodes][4];    // rest rotation (xyzw)
    float m_nodeS[kMaxNodes][3];    // rest scale

    int   m_sortedNodes[kMaxNodes]; // parent-before-child order
    int   m_sortedNodeCount = 0;

    std::string m_nodeName[kMaxNodes];

    // ─── Retarget (built once after load) ────────────────────────────────────
    int  m_nodeToHumanoid[kMaxNodes];   // HumanoidBone ordinal driving node, or -1
    Quat m_nodeRestModelRot[kMaxNodes]; // rest-pose model-space rotation per node
    int  m_retargetMappedCount = 0;

    void BuildRetargetMap();
};

} // namespace ae
