#pragma once
#include "../Core/Math.hpp"
#include "../Core/Handle.hpp"
#include "../Core/StringID.hpp"
#include <cstdint>
#include <array>

// ─── Asset handles ────────────────────────────────────────────────────────────
using MeshHandle     = TypedHandle<struct MeshTag>;
using MaterialHandle = TypedHandle<struct MaterialTag>;
using TextureHandle  = TypedHandle<struct TextureTag>;

// ─── Behavior state (used by the animation system) ──────────────────────────
enum class BehaviorState : uint8_t {
    Idle        = 0,
    Moving      = 1,
    Attacking   = 2,
    Retreating  = 3,
    Stunned     = 4,
    Dead        = 5,
};

// ─── Transform ────────────────────────────────────────────────────────────────
struct TransformComponent {
    Vec3 position { Vec3Make(0,0,0) };
    Quat rotation {};
    f32  scale    { 1.0f };
};

// ─── Renderable ───────────────────────────────────────────────────────────────
struct RenderableComponent {
    MeshHandle     mesh;
    MaterialHandle material;
    Vec3           tint       { Vec3Make(1,1,1) };
    bool           castShadow { true };
    bool           visible    { true };
};

// ─── Selection ────────────────────────────────────────────────────────────────
struct SelectionComponent {
    bool selected { false };
    bool hovered  { false };
    f32  radius   { 0.5f };
};

// ─── Movement ─────────────────────────────────────────────────────────────────
struct MoveComponent {
    Vec3 destination    {};
    bool hasDestination { false };
    f32  speed          { 3.0f };
    f32  arrivalRadius  { 0.3f };
};

// ─── Health ───────────────────────────────────────────────────────────────────
struct HealthComponent {
    f32  current { 100.0f };
    f32  max     { 100.0f };
    bool IsAlive() const { return current > 0.0f; }
};
