#include "World.hpp"
#include <algorithm>

EntityID World::CreateEntity() {
    u32 index;
    if (!m_freeSlots.empty()) {
        index = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        index = static_cast<u32>(m_generations.size());
        m_generations.push_back(0);
    }

    EntityID id;
    id.index = index;
    id.gen   = m_generations[index];
    m_alive.push_back(id);
    return id;
}

void World::DestroyEntity(EntityID id) {
    if (!IsAlive(id)) return;

    m_generations[id.index]++;
    m_freeSlots.push_back(id.index);

    auto it = std::find(m_alive.begin(), m_alive.end(), id);
    if (it != m_alive.end()) m_alive.erase(it);

    for (auto& cb : m_destroyCallbacks)
        cb(id);
}

bool World::IsAlive(EntityID id) const {
    if (id.index >= m_generations.size()) return false;
    return m_generations[id.index] == id.gen;
}
