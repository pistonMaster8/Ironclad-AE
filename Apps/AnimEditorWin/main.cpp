// main.cpp — Ironclad AE (Windows): GLFW + D3D11 + Dear ImGui animation editor.
//
// This is the Windows platform layer. The editor LOGIC (gait presets, clip
// rebuild, persistence) lives in the portable AnimEditorHost and is shared with
// the macOS build. What remains Windows-specific:
//   • DONE: window, D3D11 device/swapchain, ImGui, the always-open A panel.
//   • TODO: the 3D skinned-soldier viewport (see the SOLDIER VIEWPORT block).
//
// Build deps (fetched by CMake): GLFW, Dear ImGui (+ imgui_impl_glfw / _dx11).

#include "AnimEditorHost.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

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

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_device->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
}
static void CleanupRenderTarget() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

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
    ImGui::TextDisabled(u8"φ°"); ImGui::NextColumn();

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

// ─── Entry point ─────────────────────────────────────────────────────────────
int main(int, char**) {
    if (!glfwInit()) { std::fprintf(stderr, "glfwInit failed\n"); return 1; }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // D3D, not GL
    GLFWwindow* win = glfwCreateWindow(1280, 800, "Ironclad AE", nullptr, nullptr);
    if (!win) { glfwTerminate(); return 1; }
    HWND hwnd = glfwGetWin32Window(win);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); glfwTerminate(); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOther(win, true);
    ImGui_ImplDX11_Init(g_device, g_context);

    AnimEditorHost host;
    host.Init();
    const std::string savePath = "ironclad_ae_presets.txt";
    host.Load(savePath);

    double last = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Resize swapchain to the framebuffer if it changed.
        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw > 0 && fbh > 0 && g_rtv == nullptr) CreateRenderTarget();

        double now = glfwGetTime();
        float dt = (float)(now - last); last = now;
        host.Update(dt);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        DrawEditorPanel(host, savePath);
        ImGui::Render();

        const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);

        // ─── SOLDIER VIEWPORT (TODO — Windows renderer) ──────────────────────
        // To render the animated soldier here:
        //  1. Load Soldier.glb with cgltf (Engine/Renderer/Metal/cgltf.h is portable):
        //     positions, normals, JOINTS_0, WEIGHTS_0, indices, inverse-bind mats,
        //     node hierarchy. Build the retarget map (mixamorig -> humanoid bone).
        //  2. Port ComputeRetargetedBoneMatrices() from MetalRenderer.mm — it is pure
        //     scalar Quat/Mat4 math (now PFGE_USE_SIMD-gated), so it ports directly.
        //     Feed it host.ModelMatrices()/BoneCount() to produce ~64 bone matrices.
        //  3. D3D11: a skinned VS (pos*sum(weight_j * bone[joint_j])) + a simple
        //     lambert PS; upload vertex/index buffers once, bone matrices per frame
        //     as a cbuffer; an orbit-camera view/proj cbuffer. Draw indexed.
        //  4. Orbit camera: reuse the spherical math from the macOS EngineHost.mm
        //     (camYaw/camPitch/camDist around the chest); drive from GLFW mouse.
        // The A panel above is fully functional without this; the viewport is the
        // only remaining piece for visual parity with the macOS build.

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);   // vsync
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
