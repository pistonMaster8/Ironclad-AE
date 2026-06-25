#include "PhysicsSystem.hpp"
#include <cmath>
#include <algorithm>

bool PhysicsSystem::Integrate(ProjectileState& state, f32 dt) {
    if (!state.active) return false;

    // Gravity + drag
    Vec3 gravity = Vec3Make(0, kGravity, 0);
    f32 speedSq = Vec3Dot(state.velocity, state.velocity);
    Vec3 dragForce = Vec3Make(
        -state.drag * state.velocity.x,
        -state.drag * state.velocity.y,
        -state.drag * state.velocity.z
    );

    Vec3 accel = Vec3Make(
        (gravity.x + dragForce.x) / state.mass,
        (gravity.y + dragForce.y) / state.mass,
        (gravity.z + dragForce.z) / state.mass
    );

    // Semi-implicit Euler
    state.velocity.x += accel.x * dt;
    state.velocity.y += accel.y * dt;
    state.velocity.z += accel.z * dt;

    state.position.x += state.velocity.x * dt;
    state.position.y += state.velocity.y * dt;
    state.position.z += state.velocity.z * dt;

    // Ground collision
    TerrainSample ground = QueryGround(state.position.x, state.position.z);
    f32 groundY = ground.height + state.radius;
    if (state.position.y < groundY) {
        state.position.y = groundY;
        ResolveBounce(state, ground.normal);
        state.bounceCount++;

        if (state.bounceCount >= kMaxBounces) {
            // Absorb remaining velocity
            state.velocity.x *= 0.3f;
            state.velocity.z *= 0.3f;
            state.velocity.y = 0.0f;
        }
    }

    // Sleep threshold
    float speed = sqrtf(speedSq);
    if (state.bounceCount >= kMaxBounces && speed < kSleepSpeed) {
        state.active = false;
        return false;
    }
    return true;
}

void PhysicsSystem::ResolveBounce(ProjectileState& state, Vec3 normal) {
    // Reflect velocity about surface normal with restitution
    f32 vDotN = Vec3Dot(state.velocity, normal);
    if (vDotN >= 0) return; // already separating

    state.velocity.x -= (1.0f + state.restitution) * vDotN * normal.x;
    state.velocity.y -= (1.0f + state.restitution) * vDotN * normal.y;
    state.velocity.z -= (1.0f + state.restitution) * vDotN * normal.z;

    // Friction on tangential component
    constexpr f32 kFriction = 0.4f;
    Vec3 vTangent = {
        state.velocity.x - vDotN * normal.x,
        state.velocity.y - vDotN * normal.y,
        state.velocity.z - vDotN * normal.z,
    };
    state.velocity.x -= kFriction * vTangent.x;
    state.velocity.y -= kFriction * vTangent.y;
    state.velocity.z -= kFriction * vTangent.z;
}

TerrainSample PhysicsSystem::QueryGround(f32 /*x*/, f32 /*z*/) const {
    return { kGroundY, Vec3Make(0, 1, 0) };
}

bool PhysicsSystem::Raycast(Vec3 origin, Vec3 dir, f32 maxDist,
                            Vec3& outHit, Vec3& outNorm) const {
    if (fabsf(dir.y) < 1e-6f) return false;
    f32 t = (kGroundY - origin.y) / dir.y;
    if (t < 0 || t > maxDist) return false;
    outHit  = Vec3Make(origin.x + dir.x * t, kGroundY, origin.z + dir.z * t);
    outNorm = Vec3Make(0, 1, 0);
    return true;
}

bool PhysicsSystem::SweepSphere(Vec3 start, Vec3 end, f32 radius,
                                Vec3& outHit, Vec3& outNorm) const {
    Vec3 dir = Vec3Make(end.x - start.x, end.y - start.y, end.z - start.z);
    f32 len  = Vec3Len(dir);
    if (len < 1e-6f) return false;
    Vec3 ndir = Vec3Norm(dir);
    return Raycast(
        Vec3Make(start.x, start.y - radius, start.z),
        ndir, len, outHit, outNorm
    );
}

bool PhysicsSystem::OverlapAABB(Vec3 center, Vec3 halfExtents,
                                Vec3 aabbMin, Vec3 aabbMax) const {
    return (center.x - halfExtents.x <= aabbMax.x && center.x + halfExtents.x >= aabbMin.x)
        && (center.y - halfExtents.y <= aabbMax.y && center.y + halfExtents.y >= aabbMin.y)
        && (center.z - halfExtents.z <= aabbMax.z && center.z + halfExtents.z >= aabbMin.z);
}
