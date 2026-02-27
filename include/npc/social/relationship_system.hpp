#pragma once

#include "../core/types.hpp"
#include <map>
#include <utility>
#include <algorithm>
#include <cmath>

namespace npc {

class RelationshipSystem {
public:
    // Get relationship value between two NPCs (-100 to 100)
    float getRelationship(EntityId a, EntityId b) const {
        auto key = makeKey(a, b);
        auto it = relationships_.find(key);
        return (it != relationships_.end()) ? it->second : 0.0f;
    }

    // Modify relationship by delta (clamped to [-100, 100])
    void modifyRelationship(EntityId a, EntityId b, float delta) {
        auto key = makeKey(a, b);
        float current = getRelationship(a, b);
        relationships_[key] = std::clamp(current + delta, -100.0f, 100.0f);
    }

    // Set relationship to exact value
    void setRelationship(EntityId a, EntityId b, float value) {
        auto key = makeKey(a, b);
        relationships_[key] = std::clamp(value, -100.0f, 100.0f);
    }

    bool areFriends(EntityId a, EntityId b) const {
        return getRelationship(a, b) > 30.0f;
    }

    bool areEnemies(EntityId a, EntityId b) const {
        return getRelationship(a, b) < -30.0f;
    }

    bool areCloseFriends(EntityId a, EntityId b) const {
        return getRelationship(a, b) > 60.0f;
    }

    const std::map<std::pair<EntityId, EntityId>, float>& all() const {
        return relationships_;
    }

private:
    // Ensure consistent key ordering (smaller id first)
    static std::pair<EntityId, EntityId> makeKey(EntityId a, EntityId b) {
        return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    std::map<std::pair<EntityId, EntityId>, float> relationships_;
};

} // namespace npc
