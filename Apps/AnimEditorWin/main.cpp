// main.cpp — Ironclad AE (Windows): GLFW + D3D11 + Dear ImGui animation editor.
//
// Windows platform layer with full visual parity to the macOS build:
//   • Window, D3D11 device/swapchain/depth, Dear ImGui.
//   • The always-open A panel (gait presets, live edit, save).
//   • A 3D skinned-soldier viewport: Soldier.glb is retargeted from the shared
//     procedural humanoid pose (SoldierModel) and drawn with a skinned VS + toon
//     lambert PS, framed by an orbit camera (mirrors the macOS EngineHost).
//
// The editor LOGIC (presets, clip rebuild, persistence) lives in the portable
// AnimEditorHost; the retarget/skinning math lives in the portable SoldierModel.
// Both are shared, scalar-path equivalents of the macOS EngineHost/MetalRenderer.
//
// Build deps (fetched by CMake): GLFW, Dear ImGui (+ imgui_impl_glfw / _dx11).

#include "AnimEditorHost.hpp"
#include "SoldierModel.hpp"
#include "../../Engine/Animation/Core/AnimationTypes.hpp"  // Vec3 helpers

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_dx11.h"

// ─── D3D11 device state ──────────────────────────────────────────────────────
static ID3D11Device*           g_device   = nullptr;
static ID3D11DeviceContext*    g_context  = nullptr;
static IDXGISwapChain*         g_swap     = nullptr;
static ID3D11RenderTargetView* g_rtv      = nullptr;
static ID3D11Texture2D*        g_depthTex = nullptr;
static ID3D11DepthStencilView* g_dsv      = nullptr;
static int                     g_width    = 0;
static int                     g_height   = 0;

// ─── Viewport (skinned soldier) state ────────────────────────────────────────
static ID3D11VertexShader*   g_skinVS      = nullptr;
static ID3D11PixelShader*    g_skinPS      = nullptr;
static ID3D11InputLayout*    g_inputLayout = nullptr;
static ID3D11Buffer*         g_vbuf        = nullptr;
static ID3D11Buffer*         g_ibuf        = nullptr;
static ID3D11Buffer*         g_frameCB     = nullptr;
static ID3D11Buffer*         g_objectCB    = nullptr;
static ID3D11Buffer*         g_bonesCB     = nullptr;
static ID3D11RasterizerState*   g_raster   = nullptr;
static ID3D11DepthStencilState* g_depthSS  = nullptr;
static UINT                  g_soldierIndexCount = 0;
static ae::SoldierModel      g_soldier;

// Orbit camera — spherical around target (mirrors macOS EngineHost defaults).
struct OrbitCamera {
    float yaw   = 0.35f;
    float pitch = 0.20f;
    float dist  = 3.2f;
    Vec3  target { 0.0f, 0.9f, 0.0f };
};
static OrbitCamera g_cam;

// ─── Constant-buffer layouts (must match the HLSL cbuffers below) ─────────────
// Mat4 is the engine's row-major struct (16 contiguous floats, row 0 first),
// uploaded to `row_major float4x4` so HLSL `mul(M, v)` evaluates M*v directly.
struct FrameCB {
    Mat4  viewProj;                  // 64
    float lightDir[3];   float _p0;  // 16
    float lightColor[3]; float _p1;  // 16
    float ambient[3];    float _p2;  // 16
    float cameraPos[3];  float _p3;  // 16
};
struct ObjectCB {
    Mat4  model;                     // 64
    float tint[3];       float _p4;  // 16
};
struct BonesCB {
    Mat4 bones[ae::kMaxJoints];      // 64 * 64
};

// ─── Row-major (M*v) camera math, D3D clip space (z ∈ [0,1]) ──────────────────
static Mat4 LookAtRH(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 z = Vec3Norm(Vec3Sub(eye, center));    // toward viewer
    Vec3 x = Vec3Norm(Vec3Cross(up, z));
    Vec3 y = Vec3Cross(z, x);
    Mat4 m = Mat4Identity();
    m.m[0][0]=x.x; m.m[0][1]=x.y; m.m[0][2]=x.z; m.m[0][3]=-Vec3Dot(x, eye);
    m.m[1][0]=y.x; m.m[1][1]=y.y; m.m[1][2]=y.z; m.m[1][3]=-Vec3Dot(y, eye);
    m.m[2][0]=z.x; m.m[2][1]=z.y; m.m[2][2]=z.z; m.m[2][3]=-Vec3Dot(z, eye);
    m.m[3][0]=0;   m.m[3][1]=0;   m.m[3][2]=0;   m.m[3][3]=1;
    return m;
}
static Mat4 PerspectiveRH(float fovY, float aspect, float zn, float zf) {
    float f = 1.0f / tanf(fovY * 0.5f);
    Mat4 m{};
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = zf / (zn - zf);
    m.m[2][3] = (zn * zf) / (zn - zf);
    m.m[3][2] = -1.0f;
    m.m[3][3] = 0.0f;
    return m;
}

// ─── Render target / depth ───────────────────────────────────────────────────
static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width  = (UINT)(g_width  > 0 ? g_width  : 1);
    dd.Height = (UINT)(g_height > 0 ? g_height : 1);
    dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    g_device->CreateTexture2D(&dd, nullptr, &g_depthTex);
    if (g_depthTex) g_device->CreateDepthStencilView(g_depthTex, nullptr, &g_dsv);
}
static void CleanupRenderTarget() {
    if (g_dsv)      { g_dsv->Release();      g_dsv = nullptr; }
    if (g_depthTex) { g_depthTex->Release(); g_depthTex = nullptr; }
    if (g_rtv)      { g_rtv->Release();      g_rtv = nullptr; }
}

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            want, 1, D3D11_SDK_VERSION, &sd, &g_swap, &g_device, &fl, &g_context) != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swap)    { g_swap->Release();    g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device = nullptr; }
}

// ─── Skinned-soldier viewport: HLSL ──────────────────────────────────────────
// Skinned VS (pos = Σ weightⱼ · bone[jointⱼ] · pos) + toon lambert PS, matching
// the macOS skinnedVS / unitFS pair. All cbuffer matrices are row_major so that
// mul(M, v) == M*v for the engine's row-major Mat4 byte layout.
static const char* kViewportHLSL = R"(
cbuffer FrameCB : register(b0) {
    row_major float4x4 viewProj;
    float3 lightDir;   float _p0;
    float3 lightColor; float _p1;
    float3 ambient;    float _p2;
    float3 cameraPos;  float _p3;
};
cbuffer ObjectCB : register(b1) {
    row_major float4x4 model;
    float3 tint;       float _p4;
};
cbuffer BonesCB : register(b2) {
    row_major float4x4 bones[64];
};

struct VSIn {
    float3 pos     : POSITION;
    float3 nrm     : NORMAL;
    float2 uv      : TEXCOORD0;
    uint4  joints  : JOINTS;
    float4 weights : WEIGHTS;
};
struct VSOut {
    float4 pos      : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 nrm      : TEXCOORD1;
};

VSOut VSMain(VSIn i) {
    float4x4 skin = i.weights.x * bones[i.joints.x]
                  + i.weights.y * bones[i.joints.y]
                  + i.weights.z * bones[i.joints.z]
                  + i.weights.w * bones[i.joints.w];
    float4 skinned  = mul(skin, float4(i.pos, 1.0));
    float4 worldPos = mul(model, skinned);
    VSOut o;
    o.pos      = mul(viewProj, worldPos);
    o.worldPos = worldPos.xyz;
    o.nrm      = normalize(mul(model, mul(skin, float4(i.nrm, 0.0))).xyz);
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    float NdotL = saturate(dot(normalize(i.nrm), -normalize(lightDir)));
    float diff  = NdotL > 0.6 ? 1.0 : (NdotL > 0.3 ? 0.6 : 0.3);
    float3 c    = tint * (ambient + lightColor * diff);
    return float4(c, 1.0);
}
)";

static ID3DBlob* CompileHLSL(const char* entry, const char* target) {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* blob = nullptr;
    ID3DBlob* err  = nullptr;
    HRESULT hr = D3DCompile(kViewportHLSL, std::strlen(kViewportHLSL), nullptr,
                            nullptr, nullptr, entry, target, flags, 0, &blob, &err);
    if (FAILED(hr)) {
        if (err) { std::fprintf(stderr, "HLSL %s: %s\n", entry, (const char*)err->GetBufferPointer()); err->Release(); }
        return nullptr;
    }
    if (err) err->Release();
    return blob;
}

// Build the soldier mesh GPU buffers, shaders, and pipeline state. Returns false
// (viewport simply stays empty) if the asset or pipeline can't be created.
static bool InitViewport() {
    // Find Soldier.glb relative to the exe (CMake copies it there) or the repo.
    const char* candidates[] = {
        "Soldier.glb", "PlaceholderModels/Soldier.glb",
        "../PlaceholderModels/Soldier.glb", "../../PlaceholderModels/Soldier.glb",
    };
    bool loaded = false;
    for (const char* p : candidates) if (g_soldier.Load(p)) { loaded = true; break; }
    if (!loaded) { std::fprintf(stderr, "Soldier.glb not found; viewport disabled\n"); return false; }

    // Vertex / index buffers (immutable).
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = (UINT)(g_soldier.Vertices().size() * sizeof(ae::SkinnedVertex));
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA srd = { g_soldier.Vertices().data(), 0, 0 };
        if (FAILED(g_device->CreateBuffer(&bd, &srd, &g_vbuf))) return false;
    }
    {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_IMMUTABLE;
        bd.ByteWidth = (UINT)(g_soldier.Indices().size() * sizeof(uint32_t));
        bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA srd = { g_soldier.Indices().data(), 0, 0 };
        if (FAILED(g_device->CreateBuffer(&bd, &srd, &g_ibuf))) return false;
        g_soldierIndexCount = (UINT)g_soldier.Indices().size();
    }

    // Shaders + input layout.
    ID3DBlob* vsBlob = CompileHLSL("VSMain", "vs_5_0");
    ID3DBlob* psBlob = CompileHLSL("PSMain", "ps_5_0");
    if (!vsBlob || !psBlob) { if (vsBlob) vsBlob->Release(); if (psBlob) psBlob->Release(); return false; }
    g_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_skinVS);
    g_device->CreatePixelShader (psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_skinPS);

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "JOINTS",   0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "WEIGHTS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    HRESULT hrLayout = g_device->CreateInputLayout(layout, 5, vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(), &g_inputLayout);
    vsBlob->Release(); psBlob->Release();
    if (FAILED(hrLayout)) return false;

    // Constant buffers (dynamic, updated per frame).
    auto makeCB = [](UINT bytes, ID3D11Buffer** out) {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = (bytes + 15u) & ~15u;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(g_device->CreateBuffer(&bd, nullptr, out));
    };
    if (!makeCB(sizeof(FrameCB),  &g_frameCB))  return false;
    if (!makeCB(sizeof(ObjectCB), &g_objectCB)) return false;
    if (!makeCB(sizeof(BonesCB),  &g_bonesCB))  return false;

    // Rasterizer (no cull → robust to mesh winding) + depth (less, z∈[0,1]).
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    g_device->CreateRasterizerState(&rd, &g_raster);

    D3D11_DEPTH_STENCIL_DESC ds = {};
    ds.DepthEnable = TRUE;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    g_device->CreateDepthStencilState(&ds, &g_depthSS);

    return g_skinVS && g_skinPS && g_inputLayout && g_raster && g_depthSS;
}

static void ShutdownViewport() {
    if (g_bonesCB)     g_bonesCB->Release();
    if (g_objectCB)    g_objectCB->Release();
    if (g_frameCB)     g_frameCB->Release();
    if (g_ibuf)        g_ibuf->Release();
    if (g_vbuf)        g_vbuf->Release();
    if (g_inputLayout) g_inputLayout->Release();
    if (g_depthSS)     g_depthSS->Release();
    if (g_raster)      g_raster->Release();
    if (g_skinPS)      g_skinPS->Release();
    if (g_skinVS)      g_skinVS->Release();
}

template <class T>
static void UploadCB(ID3D11Buffer* cb, const T& data) {
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(g_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        std::memcpy(ms.pData, &data, sizeof(T));
        g_context->Unmap(cb, 0);
    }
}

// Draw one retargeted soldier. Called after the RTV/DSV are cleared and bound.
static void RenderViewport(AnimEditorHost& host) {
    if (!g_skinVS || g_soldierIndexCount == 0 || g_height <= 0) return;

    // Camera.
    Vec3 eye = Vec3Make(
        g_cam.target.x + g_cam.dist * cosf(g_cam.pitch) * sinf(g_cam.yaw),
        g_cam.target.y + g_cam.dist * sinf(g_cam.pitch),
        g_cam.target.z + g_cam.dist * cosf(g_cam.pitch) * cosf(g_cam.yaw));
    float aspect = (float)g_width / (float)g_height;
    Mat4 view = LookAtRH(eye, g_cam.target, Vec3Make(0, 1, 0));
    Mat4 proj = PerspectiveRH(45.0f * (3.14159265f / 180.0f), aspect, 0.05f, 100.0f);

    FrameCB fcb{};
    fcb.viewProj = Mat4Mul(proj, view);
    Vec3 ld = Vec3Norm(Vec3Make(-0.4f, -1.0f, -0.5f));
    fcb.lightDir[0]=ld.x; fcb.lightDir[1]=ld.y; fcb.lightDir[2]=ld.z;
    fcb.lightColor[0]=1.0f; fcb.lightColor[1]=0.98f; fcb.lightColor[2]=0.92f;
    fcb.ambient[0]=0.26f; fcb.ambient[1]=0.28f; fcb.ambient[2]=0.32f;
    fcb.cameraPos[0]=eye.x; fcb.cameraPos[1]=eye.y; fcb.cameraPos[2]=eye.z;
    UploadCB(g_frameCB, fcb);

    // Model: uniform glTF→~2-unit scale, identity facing (matches AE facingYaw=0),
    // at the origin. Ally-blue tint, mirroring the macOS shadow-disc soldier.
    ObjectCB ocb{};
    float s = g_soldier.ModelScale();
    ocb.model = Mat4Identity();
    ocb.model.m[0][0] = s; ocb.model.m[1][1] = s; ocb.model.m[2][2] = s;
    ocb.tint[0]=0.30f; ocb.tint[1]=0.55f; ocb.tint[2]=1.0f;
    UploadCB(g_objectCB, ocb);

    // Bones: retarget the live procedural humanoid pose onto the mesh skeleton.
    BonesCB bcb{};
    g_soldier.ComputeBones(host.ModelMatrices(), host.BoneCount(), bcb.bones);
    UploadCB(g_bonesCB, bcb);

    D3D11_VIEWPORT vp = { 0, 0, (float)g_width, (float)g_height, 0.0f, 1.0f };
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_raster);
    g_context->OMSetDepthStencilState(g_depthSS, 0);

    UINT stride = sizeof(ae::SkinnedVertex), offset = 0;
    g_context->IASetInputLayout(g_inputLayout);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->IASetVertexBuffers(0, 1, &g_vbuf, &stride, &offset);
    g_context->IASetIndexBuffer(g_ibuf, DXGI_FORMAT_R32_UINT, 0);

    g_context->VSSetShader(g_skinVS, nullptr, 0);
    g_context->PSSetShader(g_skinPS, nullptr, 0);
    ID3D11Buffer* vsCBs[] = { g_frameCB, g_objectCB, g_bonesCB };
    g_context->VSSetConstantBuffers(0, 3, vsCBs);
    g_context->PSSetConstantBuffers(0, 1, &g_frameCB);

    g_context->DrawIndexed(g_soldierIndexCount, 0, 0);
}

// ─── A panel (Dear ImGui) ────────────────────────────────────────────────────
static void DrawEditorPanel(AnimEditorHost& host, const std::string& savePath) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 520), ImGuiCond_FirstUseEver);
    ImGui::Begin("Animation Editor");

    // Cycle button: idle -> walk -> jog -> run
    char label[64];
    std::snprintf(label, sizeof(label), "<  %s  >", host.PresetName());
    if (ImGui::Button(label, ImVec2(-1, 0))) host.CyclePreset();
    ImGui::Separator();

    ImGui::Columns(3, nullptr, false);
    ImGui::SetColumnWidth(0, 110); ImGui::SetColumnWidth(1, 80);
    ImGui::TextDisabled("field"); ImGui::NextColumn();
    ImGui::TextDisabled("value"); ImGui::NextColumn();
    ImGui::TextDisabled("\xCF\x86\xC2\xB0"); ImGui::NextColumn();  // UTF-8 "φ°" (avoids C++20 char8_t)

    for (int f = 0; f < host.FieldCount(); ++f) {
        ImGui::PushID(f);
        ImGui::TextUnformatted(host.FieldLabel(f)); ImGui::NextColumn();
        float v = host.Value(f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputFloat("##v", &v, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
            host.SetValue(f, v);
        ImGui::NextColumn();
        float ph = host.Phase(f);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputFloat("##p", &ph, 0, 0, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
            host.SetPhase(f, ph);
        ImGui::NextColumn();
        ImGui::PopID();
    }
    ImGui::Columns(1);
    ImGui::Separator();
    if (ImGui::Button("SAVE", ImVec2(-1, 0))) host.Save(savePath);
    ImGui::End();
}

// ─── Mouse-driven orbit camera (GLFW) ────────────────────────────────────────
static void ScrollCallback(GLFWwindow*, double, double yoff) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_cam.dist *= (1.0f - (float)yoff * 0.1f);    // matches EngineHost zoomDelta
    g_cam.dist = fmaxf(0.8f, fminf(20.0f, g_cam.dist));
}

static void UpdateCameraInput(GLFWwindow* win) {
    static double lastX = 0, lastY = 0;
    static bool   have  = false;
    double mx, my; glfwGetCursorPos(win, &mx, &my);
    double dx = have ? (mx - lastX) : 0.0;
    double dy = have ? (my - lastY) : 0.0;
    lastX = mx; lastY = my; have = true;

    if (ImGui::GetIO().WantCaptureMouse) return;

    bool left  = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT)  == GLFW_PRESS;
    bool right = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
                 glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

    if (left && !right) {                          // orbit
        g_cam.yaw   += (float)dx * 0.008f;
        g_cam.pitch += (float)dy * 0.008f;
        g_cam.pitch  = fmaxf(-0.20f, fminf(1.45f, g_cam.pitch));
    } else if (right) {                            // pan
        float rightX =  cosf(g_cam.yaw), rightZ = -sinf(g_cam.yaw);
        float scale  = g_cam.dist * 0.0015f;
        g_cam.target.x -= rightX * (float)dx * scale;
        g_cam.target.z -= rightZ * (float)dx * scale;
        g_cam.target.y += (float)dy * scale;
    }
}

// ─── Entry point ─────────────────────────────────────────────────────────────
int main(int, char**) {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // D3D, not GL
    GLFWwindow* win = glfwCreateWindow(1280, 800, "Ironclad AE", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    glfwGetFramebufferSize(win, &g_width, &g_height);
    HWND hwnd = glfwGetWin32Window(win);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); glfwTerminate(); return 1; }
    glfwSetScrollCallback(win, ScrollCallback);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOther(win, true);
    ImGui_ImplDX11_Init(g_device, g_context);

    AnimEditorHost host;
    host.Init();
    const std::string savePath = "ironclad_ae_presets.txt";
    host.Load(savePath);

    InitViewport();   // viewport is optional; the panel works regardless

    bool prevSpace = false, prevS = false;
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Resize swapchain + depth when the framebuffer changes.
        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw > 0 && fbh > 0 && (fbw != g_width || fbh != g_height)) {
            CleanupRenderTarget();
            g_width = fbw; g_height = fbh;
            g_swap->ResizeBuffers(0, (UINT)fbw, (UINT)fbh, DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        if (g_rtv == nullptr) CreateRenderTarget();

        // Keyboard parity with macOS: Space cycles preset, S saves.
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            bool space = glfwGetKey(win, GLFW_KEY_SPACE) == GLFW_PRESS;
            bool sKey  = glfwGetKey(win, GLFW_KEY_S)     == GLFW_PRESS;
            if (space && !prevSpace) host.CyclePreset();
            if (sKey  && !prevS)     host.Save(savePath);
            prevSpace = space; prevS = sKey;
        }
        UpdateCameraInput(win);

        double now = glfwGetTime();
        float dt = (float)(now - last); last = now;
        host.Update(dt);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawEditorPanel(host, savePath);
        ImGui::Render();

        const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, g_dsv);
        g_context->ClearRenderTargetView(g_rtv, clear);
        if (g_dsv) g_context->ClearDepthStencilView(g_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

        RenderViewport(host);   // skinned soldier, behind the ImGui panel

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);   // vsync
    }

    ShutdownViewport();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
