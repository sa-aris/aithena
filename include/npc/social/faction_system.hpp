#pragma once

#include "../core/types.hpp"
#include <string>
#include <map>
#include <set>
#include <algorithm>

namespace npc {

struct Faction {
    FactionId id;
    std::string name;
    std::set<EntityId> members;
};

class FactionSystem {
public:
    void addFaction(FactionId id, const std::string& name) {
        factions_[id] = Faction{id, name, {}};
    }

    void addMember(FactionId factionId, EntityId entityId) {
        auto it = factions_.find(factionId);
        if (it != factions_.end()) {
            it->second.members.insert(entityId);
            entityFaction_[entityId] = factionId;
        }
    }

    void removeMember(FactionId factionId, EntityId entityId) {
        auto it = factions_.find(factionId);
        if (it != factions_.end()) {
            it->second.members.erase(entityId);
            entityFaction_.erase(entityId);
        }
    }

    FactionId getFactionOf(EntityId entityId) const {
        auto it = entityFaction_.find(entityId);
        return (it != entityFaction_.end()) ? it->second : NO_FACTION;
    }

    float getRelation(FactionId a, FactionId b) const {
        if (a == b) return 100.0f; // same faction = fully allied
        auto key = makeKey(a, b);
        auto it = relations_.find(key);
        return (it != relations_.end()) ? it->second : 0.0f;
    }

    void setRelation(FactionId a, FactionId b, float value) {
        relations_[makeKey(a, b)] = std::clamp(value, -100.0f, 100.0f);
    }

    void modifyRelation(FactionId a, FactionId b, float delta) {
        auto key = makeKey(a, b);
        float current = getRelation(a, b);
        relations_[key] = std::clamp(current + delta, -100.0f, 100.0f);
    }

    bool areAllied(FactionId a, FactionId b) const { return getRelation(a, b) > 50.0f; }
    bool areHostile(FactionId a, FactionId b) const { return getRelation(a, b) < -50.0f; }
    bool areNeutral(FactionId a, FactionId b) const {
        float r = getRelation(a, b);
        return r >= -50.0f && r <= 50.0f;
    }

    bool areSameFaction(EntityId a, EntityId b) const {
        return getFactionOf(a) == getFactionOf(b) && getFactionOf(a) != NO_FACTION;
    }

    bool areEntitiesHostile(EntityId a, EntityId b) const {
        FactionId fa = getFactionOf(a);
        FactionId fb = getFactionOf(b);
        if (fa == NO_FACTION || fb == NO_FACTION) return false;
        return areHostile(fa, fb);
    }

    const Faction* getFaction(FactionId id) const {
        auto it = factions_.find(id);
        return (it != factions_.end()) ? &it->second : nullptr;
    }

    std::set<EntityId> getFactionMembers(FactionId id) const {
        auto it = factions_.find(id);
        return (it != factions_.end()) ? it->second.members : std::set<EntityId>{};
    }

    const std::map<FactionId, Faction>& factions() const { return factions_; }

private:
    static uint64_t makeKey(FactionId a, FactionId b) {
        auto lo = std::min(a, b);
        auto hi = std::max(a, b);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    }

    std::map<FactionId, Faction> factions_;
    std::map<uint64_t, float> relations_;
    std::map<EntityId, FactionId> entityFaction_;
};

} // namespace npc
