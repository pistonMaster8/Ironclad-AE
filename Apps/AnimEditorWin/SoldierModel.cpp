// SoldierModel.cpp — portable scalar port of the Metal GltfModel skinning path.
//
// Faithful translation of LoadGltfModel / BuildRetargetMap /
// ComputeRetargetedBoneMatrices (Engine/Renderer/Metal/MetalRenderer.mm) onto
// the engine's row-major scalar Mat4/Quat. The math is identical; only the
// storage convention differs (simd column-major → row-major m[row][col]), and
// glTF's column-major matrices are transposed on load.

#include "SoldierModel.hpp"
#include "../../Engine/Animation/Core/AnimationTypes.hpp"  // HumanoidBone, Quat ops, BoneTransform

// This file is the scalar (row-major Mat4) port used by the Windows/Linux build.
// It is only ever compiled with PFGE_USE_SIMD=0; the macOS app uses the simd
// path in MetalRenderer.mm instead. Editors defaulting to the Apple simd path
// will flag the m[row][col] accesses below — that is expected, not a bug.
#if PFGE_USE_SIMD
#  error "SoldierModel.cpp requires the scalar math path (build with -DPFGE_USE_SIMD=0)"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <queue>
#include <vector>

// cgltf single-header implementation lives in this TU for the Windows build
// (the Metal build defines it in MetalRenderer.mm instead).
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include "../../Engine/Renderer/Metal/cgltf.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif

namespace ae {

// ─── Small math helpers (row-major, M*v convention) ──────────────────────────

// glTF stores matrices column-major (element[col*4+row]); transpose to our
// row-major Mat4 (m[row][col]).
static Mat4 Mat4FromGltfColMajor(const float* p) {
    Mat4 m;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            m.m[row][col] = p[col * 4 + row];
    return m;
}

// Compose a TRS matrix (T translation, R rotation xyzw, S scale).
static Mat4 MakeTRS(const float T[3], Quat R, const float S[3]) {
    BoneTransform bt;
    bt.translation = Vec3Make(T[0], T[1], T[2]);
    bt.rotation    = R;
    bt.scale       = Vec3Make(S[0], S[1], S[2]);
    return bt.ToMatrix();
}

// Rotation quaternion from a model matrix with scale removed (the retarget
// input). Mirrors the column-normalising QuatFromMat4 used by the Metal path.
static Quat QuatFromModelMatNormalized(const Mat4& m) {
    // Basis vectors are the matrix columns: col j = (m[0][j], m[1][j], m[2][j]).
    Vec3 c0 = Vec3Norm(Vec3Make(m.m[0][0], m.m[1][0], m.m[2][0]));
    Vec3 c1 = Vec3Norm(Vec3Make(m.m[0][1], m.m[1][1], m.m[2][1]));
    Vec3 c2 = Vec3Norm(Vec3Make(m.m[0][2], m.m[1][2], m.m[2][2]));
    Mat4 r = Mat4Identity();
    r.m[0][0] = c0.x; r.m[1][0] = c0.y; r.m[2][0] = c0.z;
    r.m[0][1] = c1.x; r.m[1][1] = c1.y; r.m[2][1] = c1.z;
    r.m[0][2] = c2.x; r.m[1][2] = c2.y; r.m[2][2] = c2.z;
    return QuatFromMat4(r);
}

// Transform a homogeneous point: M*v (row-major).
static Vec4 Mat4MulVec4(const Mat4& m, Vec4 v) {
    return Vec4Make(
        m.m[0][0]*v.x + m.m[0][1]*v.y + m.m[0][2]*v.z + m.m[0][3]*v.w,
        m.m[1][0]*v.x + m.m[1][1]*v.y + m.m[1][2]*v.z + m.m[1][3]*v.w,
        m.m[2][0]*v.x + m.m[2][1]*v.y + m.m[2][2]*v.z + m.m[2][3]*v.w,
        m.m[3][0]*v.x + m.m[3][1]*v.y + m.m[3][2]*v.z + m.m[3][3]*v.w);
}

// ─── Load ────────────────────────────────────────────────────────────────────

bool SoldierModel::Load(const char* path) {
    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success)
        return false;
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        cgltf_free(data);
        return false;
    }
    if (data->meshes_count == 0 || data->skins_count == 0) {
        cgltf_free(data);
        return false;
    }

    // ─── Mesh attributes ─────────────────────────────────────────────────────
    cgltf_primitive& prim = data->meshes[0].primitives[0];
    cgltf_accessor *posAcc=nullptr, *nrmAcc=nullptr, *uvAcc=nullptr,
                   *jtAcc=nullptr,  *wtAcc=nullptr;
    for (size_t a = 0; a < prim.attributes_count; ++a) {
        cgltf_attribute& attr = prim.attributes[a];
        if (attr.type == cgltf_attribute_type_position)                   posAcc = attr.data;
        if (attr.type == cgltf_attribute_type_normal)                     nrmAcc = attr.data;
        if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvAcc = attr.data;
        if (attr.type == cgltf_attribute_type_joints   && attr.index == 0) jtAcc = attr.data;
        if (attr.type == cgltf_attribute_type_weights  && attr.index == 0) wtAcc = attr.data;
    }
    if (!posAcc || !jtAcc || !wtAcc) {
        cgltf_free(data);
        return false;
    }

    size_t vCount = posAcc->count;
    m_vertices.resize(vCount);
    float mnX=1e9f, mxX=-1e9f, mnY=1e9f, mxY=-1e9f, mnZ=1e9f, mxZ=-1e9f;

    for (size_t i = 0; i < vCount; ++i) {
        float pos[3]={0,0,0}, nrm[3]={0,1,0}, uv[2]={0,0}, wt[4]={1,0,0,0};
        cgltf_uint jt[4]={0,0,0,0};
        cgltf_accessor_read_float(posAcc, i, pos, 3);
        if (nrmAcc) cgltf_accessor_read_float(nrmAcc, i, nrm, 3);
        if (uvAcc)  cgltf_accessor_read_float(uvAcc,  i, uv,  2);
        if (wtAcc)  cgltf_accessor_read_float(wtAcc,  i, wt,  4);
        cgltf_accessor_read_uint(jtAcc, i, jt, 4);

        SkinnedVertex& v = m_vertices[i];
        v.px = pos[0]; v.py = pos[1]; v.pz = pos[2];
        v.nx = nrm[0]; v.ny = nrm[1]; v.nz = nrm[2];
        v.u  = uv[0];  v.v  = uv[1];
        v.joints[0]=(uint16_t)jt[0]; v.joints[1]=(uint16_t)jt[1];
        v.joints[2]=(uint16_t)jt[2]; v.joints[3]=(uint16_t)jt[3];
        v.weights[0]=wt[0]; v.weights[1]=wt[1]; v.weights[2]=wt[2]; v.weights[3]=wt[3];

        mnX=std::min(mnX,pos[0]); mxX=std::max(mxX,pos[0]);
        mnY=std::min(mnY,pos[1]); mxY=std::max(mxY,pos[1]);
        mnZ=std::min(mnZ,pos[2]); mxZ=std::max(mxZ,pos[2]);
    }

    // ─── Indices (normalised to 32-bit) ──────────────────────────────────────
    if (prim.indices) {
        cgltf_accessor* idxAcc = prim.indices;
        m_indices.resize(idxAcc->count);
        for (size_t i = 0; i < idxAcc->count; ++i) {
            cgltf_uint val; cgltf_accessor_read_uint(idxAcc, i, &val, 1);
            m_indices[i] = (uint32_t)val;
        }
    } else {
        m_indices.resize(vCount);
        for (uint32_t i = 0; i < (uint32_t)vCount; ++i) m_indices[i] = i;
    }

    // ─── Node hierarchy ──────────────────────────────────────────────────────
    m_nodeCount = (int)std::min(data->nodes_count, (size_t)kMaxNodes);
    for (int i = 0; i < m_nodeCount; ++i) {
        cgltf_node& node       = data->nodes[i];
        m_nodeParent[i]        = -1;
        m_nodeToHumanoid[i]    = -1;
        m_nodeRestModelRot[i]  = QuatIdentity();
        m_nodeName[i]          = node.name ? node.name : "";
        m_nodeHasMat[i]        = (bool)node.has_matrix;
        if (node.has_matrix) {
            m_nodeMat[i] = Mat4FromGltfColMajor(node.matrix);
        } else {
            float defT[3]={0,0,0}, defR[4]={0,0,0,1}, defS[3]={1,1,1};
            if (node.has_translation) std::memcpy(defT, node.translation, 12);
            if (node.has_rotation)    std::memcpy(defR, node.rotation,    16);
            if (node.has_scale)       std::memcpy(defS, node.scale,       12);
            std::memcpy(m_nodeT[i], defT, 12);
            std::memcpy(m_nodeR[i], defR, 16);
            std::memcpy(m_nodeS[i], defS, 12);
        }
    }
    for (int i = 0; i < m_nodeCount; ++i) {
        cgltf_node& node = data->nodes[i];
        for (size_t c = 0; c < node.children_count; ++c) {
            int ci = (int)(node.children[c] - data->nodes);
            if (ci >= 0 && ci < m_nodeCount) m_nodeParent[ci] = i;
        }
    }

    // BFS topological sort (parent before child).
    m_sortedNodeCount = 0;
    std::vector<bool> vis(m_nodeCount, false);
    std::queue<int> bfsQ;
    for (int i = 0; i < m_nodeCount; ++i)
        if (m_nodeParent[i] == -1) { bfsQ.push(i); vis[i] = true; }
    while (!bfsQ.empty()) {
        int n = bfsQ.front(); bfsQ.pop();
        m_sortedNodes[m_sortedNodeCount++] = n;
        cgltf_node& node = data->nodes[n];
        for (size_t c = 0; c < node.children_count; ++c) {
            int ci = (int)(node.children[c] - data->nodes);
            if (ci >= 0 && ci < m_nodeCount && !vis[ci]) { vis[ci] = true; bfsQ.push(ci); }
        }
    }

    // ─── Scale from the root node's effective world-space height ──────────────
    {
        Mat4 rootWorld = Mat4Identity();
        if (m_sortedNodeCount > 0) {
            int rn = m_sortedNodes[0];
            if (m_nodeHasMat[rn]) {
                rootWorld = m_nodeMat[rn];
            } else {
                Quat R = Quat{ m_nodeR[rn][0], m_nodeR[rn][1], m_nodeR[rn][2], m_nodeR[rn][3] };
                rootWorld = MakeTRS(m_nodeT[rn], R, m_nodeS[rn]);
            }
        }
        float wmnY = 1e9f, wmxY = -1e9f;
        float bx[2]={mnX,mxX}, by[2]={mnY,mxY}, bz[2]={mnZ,mxZ};
        for (int ci = 0; ci < 8; ++ci) {
            Vec4 w = Mat4MulVec4(rootWorld,
                Vec4Make(bx[(ci>>0)&1], by[(ci>>1)&1], bz[(ci>>2)&1], 1.0f));
            wmnY = std::min(wmnY, w.y);
            wmxY = std::max(wmxY, w.y);
        }
        float worldHeight = wmxY - wmnY;
        m_scale = (worldHeight > 0.001f) ? 2.0f / worldHeight : 1.0f;
    }

    // ─── Skin ────────────────────────────────────────────────────────────────
    cgltf_skin& skin = data->skins[0];
    m_jointCount = (uint32_t)std::min(skin.joints_count, (size_t)kMaxJoints);
    for (uint32_t j = 0; j < m_jointCount; ++j) {
        m_jointNode[j] = (int)(skin.joints[j] - data->nodes);
        if (skin.inverse_bind_matrices) {
            float m16[16];
            cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m16, 16);
            m_invBind[j] = Mat4FromGltfColMajor(m16);
        } else {
            m_invBind[j] = Mat4Identity();
        }
    }

    cgltf_free(data);

    BuildRetargetMap();

    m_loaded = true;
    return true;
}

// ─── Retarget map ─────────────────────────────────────────────────────────────
// Exact Mixamo joint name (sans "mixamorig:" prefix) → humanoid ordinal. Mirrors
// MetalRenderer.mm's table; fingers / the extra chest joint stay unmapped and
// ride their animated parent at rest orientation.
void SoldierModel::BuildRetargetMap() {
    struct HMap { const char* name; HumanoidBone hb; };
    static const HMap kMixamoMap[] = {
        {"Hips", HumanoidBone::Pelvis},
        {"Spine", HumanoidBone::Spine01}, {"Spine1", HumanoidBone::Spine02},
        {"Neck", HumanoidBone::Neck}, {"Head", HumanoidBone::Head},
        {"LeftShoulder", HumanoidBone::ClavicleL}, {"LeftArm", HumanoidBone::UpperArmL},
        {"LeftForeArm", HumanoidBone::LowerArmL},  {"LeftHand", HumanoidBone::HandL},
        {"RightShoulder", HumanoidBone::ClavicleR},{"RightArm", HumanoidBone::UpperArmR},
        {"RightForeArm", HumanoidBone::LowerArmR}, {"RightHand", HumanoidBone::HandR},
        {"LeftUpLeg", HumanoidBone::UpperLegL}, {"LeftLeg", HumanoidBone::LowerLegL},
        {"LeftFoot", HumanoidBone::FootL},      {"LeftToeBase", HumanoidBone::ToeL},
        {"RightUpLeg", HumanoidBone::UpperLegR},{"RightLeg", HumanoidBone::LowerLegR},
        {"RightFoot", HumanoidBone::FootR},     {"RightToeBase", HumanoidBone::ToeR},
    };

    // Rest-pose model-space rotation for every node (parent-before-child order).
    std::vector<Mat4> worldMat(m_nodeCount);
    for (int si = 0; si < m_sortedNodeCount; ++si) {
        int n = m_sortedNodes[si];
        Mat4 local;
        if (m_nodeHasMat[n]) {
            local = m_nodeMat[n];
        } else {
            Quat R = Quat{ m_nodeR[n][0], m_nodeR[n][1], m_nodeR[n][2], m_nodeR[n][3] };
            local = MakeTRS(m_nodeT[n], R, m_nodeS[n]);
        }
        worldMat[n] = (m_nodeParent[n] < 0)
            ? local : Mat4Mul(worldMat[m_nodeParent[n]], local);
        m_nodeRestModelRot[n] = QuatFromModelMatNormalized(worldMat[n]);
    }

    // Match each joint node's name to a humanoid bone.
    m_retargetMappedCount = 0;
    for (uint32_t j = 0; j < m_jointCount; ++j) {
        int n = m_jointNode[j];
        if (n < 0 || n >= m_nodeCount) continue;
        const std::string& nm = m_nodeName[n];
        size_t c = nm.find_last_of(':');
        std::string bare = (c == std::string::npos) ? nm : nm.substr(c + 1);
        for (const HMap& e : kMixamoMap) {
            if (bare == e.name) {
                m_nodeToHumanoid[n] = (int)e.hb;
                m_retargetMappedCount++;
                break;
            }
        }
    }
}

// ─── Per-frame retarget ───────────────────────────────────────────────────────
void SoldierModel::ComputeBones(const Mat4* humanoidModelMats, int humanoidBoneCount,
                                Mat4* outBones) const {
    // Per-humanoid-bone model-space rotation = the retarget "delta" the Metal
    // path feeds via scene.skinnedBoneRot.
    Quat deltas[(int)HumanoidBone::Count];
    for (int h = 0; h < (int)HumanoidBone::Count; ++h) deltas[h] = QuatIdentity();
    int hc = std::min(humanoidBoneCount, (int)HumanoidBone::Count);
    for (int h = 0; h < hc; ++h)
        deltas[h] = QuatFromModelMatNormalized(humanoidModelMats[h]);

    const Quat qId = QuatIdentity();
    std::vector<Mat4> worldMat(m_nodeCount);
    std::vector<Quat> animModelRot(m_nodeCount);

    for (int si = 0; si < m_sortedNodeCount; ++si) {
        int n      = m_sortedNodes[si];
        int parent = m_nodeParent[n];
        Quat parentModelRot = (parent < 0) ? qId : animModelRot[parent];

        Mat4 local;
        if (m_nodeHasMat[n]) {
            local = m_nodeMat[n];
            Mat4 w = (parent < 0) ? local : Mat4Mul(worldMat[parent], local);
            animModelRot[n] = QuatFromModelMatNormalized(w);
        } else {
            int  h = m_nodeToHumanoid[n];
            Quat localRot;
            if (h >= 0) {
                // Mixamo rig faces -Z, humanoid faces +Z: a Z-reflection between
                // the two world bases negates the quaternion's x and y parts.
                Quat d  = deltas[h];
                Quat dm = Quat{ -d.x, -d.y, d.z, d.w };
                Quat modelRot   = QuatMul(dm, m_nodeRestModelRot[n]);
                localRot        = QuatMul(QuatConjugate(parentModelRot), modelRot);
                animModelRot[n] = modelRot;
            } else {
                // Unmapped: keep rest local rotation, ride the animated parent.
                localRot        = Quat{ m_nodeR[n][0], m_nodeR[n][1], m_nodeR[n][2], m_nodeR[n][3] };
                animModelRot[n] = QuatMul(parentModelRot, localRot);
            }
            local = MakeTRS(m_nodeT[n], localRot, m_nodeS[n]);
        }
        worldMat[n] = (parent < 0) ? local : Mat4Mul(worldMat[parent], local);
    }

    for (uint32_t j = 0; j < m_jointCount && j < (uint32_t)kMaxJoints; ++j) {
        int n = m_jointNode[j];
        outBones[j] = (n >= 0 && n < m_nodeCount)
            ? Mat4Mul(worldMat[n], m_invBind[j])
            : Mat4Identity();
    }
    for (uint32_t j = m_jointCount; j < (uint32_t)kMaxJoints; ++j)
        outBones[j] = Mat4Identity();
}

} // namespace ae
