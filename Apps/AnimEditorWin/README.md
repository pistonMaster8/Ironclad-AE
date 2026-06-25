# Ironclad AE — Windows port

Cross-platform animation editor. The **editor logic** (4 gait presets, live clip
rebuild, phase-shift, persistence) is shared, portable C++ in `AnimEditorHost`.
Only the platform shell differs per OS.

| Layer            | macOS                          | Windows                         |
|------------------|--------------------------------|---------------------------------|
| Window / input   | AppKit + MTKView               | GLFW                            |
| GPU              | Metal (`MetalRenderer.mm`)     | D3D11 (`main.cpp`)              |
| UI (A panel)     | SwiftUI                        | Dear ImGui                      |
| Host             | `EngineHost.mm` (Obj-C++)      | `AnimEditorHost` (portable C++) |
| Math/anim        | Apple `simd` (`PFGE_USE_SIMD=1`)| scalar path (`PFGE_USE_SIMD=0`)|

The Apple `simd` vs. scalar split is controlled by `PFGE_USE_SIMD` (Engine/Core/
Types.hpp), decoupled from `__APPLE__`. Windows/MSVC defaults to the scalar
`Quat`/`Mat4` path, which is verified to compile and run.

## Build (Windows)

Requires the Windows SDK + a C++17/20 toolchain (MSVC or clang-cl). GLFW and
Dear ImGui are fetched automatically by CMake.

```bat
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
cmake --build build-win --config Release --target IroncladAE
build-win\bin\Release\IroncladAE.exe
```

(Or `-G Ninja` with a clang-cl/MSVC environment.)

## Status

- **Done**: window, D3D11 device/swapchain, Dear ImGui, the always-open A panel
  (preset cycle button + per-field value/φ inputs + Save), and the full editor
  host driving the `AnimationController` on the selected preset. The portable
  core is verified building and running on the scalar path.
- **TODO (one piece)**: the 3D skinned-soldier viewport. See the `SOLDIER
  VIEWPORT` block in `main.cpp` — load `Soldier.glb` (cgltf), port
  `ComputeRetargetedBoneMatrices` (pure scalar math) from `MetalRenderer.mm`,
  add a skinned VS + lambert PS, and an orbit camera. The A panel is fully
  functional without it.
