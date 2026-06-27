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
| Skinned soldier  | `MetalRenderer.mm` (`GltfModel`)| `SoldierModel` (portable C++)  |
| Math/anim        | Apple `simd` (`PFGE_USE_SIMD=1`)| scalar path (`PFGE_USE_SIMD=0`)|

The Apple `simd` vs. scalar split is controlled by `PFGE_USE_SIMD` (Engine/Core/
Types.hpp), decoupled from `__APPLE__`. Windows/MSVC defaults to the scalar
`Quat`/`Mat4` path. `SoldierModel` is the scalar port of the Metal `GltfModel`
loader + `ComputeRetargetedBoneMatrices`; the math is identical, only the matrix
storage convention differs (simd column-major → row-major), and glTF's
column-major matrices are transposed on load.

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

Full visual + functional parity with the macOS build:

- **A panel**: preset cycle button, per-field value/φ inputs, Save, and the
  editor host driving the `AnimationController` on the selected preset.
- **Soldier viewport**: `Soldier.glb` is loaded with cgltf and skinned by
  retargeting the live procedural humanoid pose onto the Mixamo skeleton
  (`SoldierModel::ComputeBones`). Drawn with a skinned VS + toon-lambert PS
  (matching the macOS `skinnedVS`/`unitFS`), framed by an orbit camera.
- **Camera**: left-drag orbits, right/middle-drag pans, scroll zooms — same
  spherical math and limits as the macOS `EngineHost`. Space cycles the preset
  and `S` saves, matching the macOS keyboard shortcuts.

`Soldier.glb` is copied next to the executable at build time. At runtime the
viewport also searches the repo's `PlaceholderModels/` as a fallback; if the
asset can't be found the panel still works and the viewport stays empty.

The portable core (`AnimEditorHost`, `SoldierModel`) is verified building and
running on the scalar path.
