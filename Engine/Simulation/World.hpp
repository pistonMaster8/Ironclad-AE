#pragma once
#include "Components.hpp"
#include <vector>
#include <unordered_map>
#include <optional>
#include <functional>

// Sparse-set component storage.
// Maps EntityID → dense index. Dense arrays stay packed for cache performance.
// Games own their ComponentStorage<T> instances directly (not inside World).
template<typename T>
class ComponentStorage {
public:
    void Add(EntityID id, T&& component) {
        PFGE_ASSERT(!Has(id));
        m_sparse[id.index] = static_cast<u32>(m_dense.size());
        m_entities.push_back(id);
        m_dense.push_back(std::move(component));
    }

    void Remove(EntityID id) {
        auto it = m_sparse.find(id.index);
        if (it == m_sparse.end()) return;
        u32 denseIdx = it->second;
        u32 lastIdx  = static_cast<u32>(m_dense.size()) - 1;
        if (denseIdx != lastIdx) {
            m_dense[denseIdx]    = std::move(m_dense[lastIdx]);
            m_entities[denseIdx] = m_entities[lastIdx];
            m_sparse[m_entities[denseIdx].index] = denseIdx;
        }
        m_dense.pop_back();
        m_entities.pop_back();
        m_sparse.erase(it);
    }

    [[nodiscard]] T* Get(EntityID id) {
        auto it = m_sparse.find(id.index);
        if (it == m_sparse.end()) return nullptr;
        return &m_dense[it->second];
    }

    [[nodiscard]] const T* Get(EntityID id) const {
        auto it = m_sparse.find(id.index);
        if (it == m_sparse.end()) return nullptr;
        return &m_dense[it->second];
    }

    [[nodiscard]] bool Has(EntityID id) const {
        return m_sparse.count(id.index) > 0;
    }

    void ForEach(std::function<void(EntityID, T&)> fn) {
        for (u32 i = 0; i < m_dense.size(); ++i)
            fn(m_entities[i], m_dense[i]);
    }

    [[nodiscard]] u32 Count() const { return static_cast<u32>(m_dense.size()); }
    [[nodiscard]] std::vector<T>& Data() { return m_dense; }
    [[nodiscard]] const std::vector<EntityID>& Entities() const { return m_entities; }

private:
    std::unordered_map<u32, u32> m_sparse;
    std::vector<T>               m_dense;
    std::vector<EntityID>        m_entities;
};

// World: manages entity IDs only.
// Games own ComponentStorage<T> members directly and register OnDestroy
// callbacks so entity destruction automatically propagates to each storage:
//   world.OnDestroy([&](EntityID id){ transforms.Remove(id); moves.Remove(id); });
class World {
public:
    EntityID CreateEntity();
    void     DestroyEntity(EntityID id);
    bool     IsAlive(EntityID id) const;

    u32 EntityCount() const { return static_cast<u32>(m_alive.size()); }
    const std::vector<EntityID>& Alive() const { return m_alive; }

    void OnDestroy(std::function<void(EntityID)> cb) {
        m_destroyCallbacks.push_back(std::move(cb));
    }

private:
    std::vector<u32>      m_freeSlots;
    std::vector<u16>      m_generations;
    std::vector<EntityID> m_alive;
    std::vector<std::function<void(EntityID)>> m_destroyCallbacks;
};
