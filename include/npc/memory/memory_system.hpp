#pragma once

#include "../core/types.hpp"
#include <vector>
#include <string>
#include <optional>
#include <algorithm>

namespace npc {

struct Memory {
    MemoryType type;
    std::optional<EntityId> entityId;
    std::string description;
    float emotionalImpact = 0.0f;  // -1 (very negative) to +1 (very positive)
    float timestamp = 0.0f;        // game time when created
    float importance = 0.5f;       // 0 (trivial) to 1 (unforgettable)
    float decayRate = 0.01f;       // importance loss per game hour
    float currentStrength = 1.0f;  // decays over time
};

class MemorySystem {
public:
    explicit MemorySystem(size_t maxMemories = 100)
        : maxMemories_(maxMemories) {}

    void addMemory(Memory mem) {
        mem.currentStrength = 1.0f;
        memories_.push_back(std::move(mem));

        // If over capacity, forget the weakest memory
        if (memories_.size() > maxMemories_) {
            forgetWeakest();
        }
    }

    void addMemory(MemoryType type, const std::string& desc,
                   float emotionalImpact = 0.0f,
                   std::optional<EntityId> entity = std::nullopt,
                   float importance = 0.5f, float timestamp = 0.0f) {
        Memory m;
        m.type = type;
        m.description = desc;
        m.emotionalImpact = emotionalImpact;
        m.entityId = entity;
        m.importance = importance;
        m.timestamp = timestamp;
        addMemory(std::move(m));
    }

    void update(float dt) {
        for (auto& m : memories_) {
            m.currentStrength -= m.decayRate * dt;
            m.currentStrength = std::max(0.0f, m.currentStrength);
        }
        // Remove fully decayed unimportant memories
        memories_.erase(
            std::remove_if(memories_.begin(), memories_.end(),
                [](const Memory& m) {
                    return m.currentStrength <= 0.0f && m.importance < 0.7f;
                }),
            memories_.end()
        );
    }

    std::vector<Memory> recall(MemoryType type) const {
        std::vector<Memory> result;
        for (const auto& m : memories_) {
            if (m.type == type) result.push_back(m);
        }
        std::sort(result.begin(), result.end(),
            [](const Memory& a, const Memory& b) {
                return a.timestamp > b.timestamp;
            });
        return result;
    }

    std::vector<Memory> recallAbout(EntityId entity) const {
        std::vector<Memory> result;
        for (const auto& m : memories_) {
            if (m.entityId.has_value() && *m.entityId == entity) {
                result.push_back(m);
            }
        }
        return result;
    }

    float getOpinionOf(EntityId entity) const {
        float opinion = 0.0f;
        int count = 0;
        for (const auto& m : memories_) {
            if (m.entityId.has_value() && *m.entityId == entity) {
                opinion += m.emotionalImpact * m.currentStrength;
                ++count;
            }
        }
        return count > 0 ? std::clamp(opinion / count, -1.0f, 1.0f) : 0.0f;
    }

    bool hasMemoryOf(MemoryType type, std::optional<EntityId> entity = std::nullopt) const {
        for (const auto& m : memories_) {
            if (m.type == type) {
                if (!entity.has_value() || (m.entityId == entity))
                    return true;
            }
        }
        return false;
    }

    const std::vector<Memory>& allMemories() const { return memories_; }

    Memory* mostRecent() {
        if (memories_.empty()) return nullptr;
        return &*std::max_element(memories_.begin(), memories_.end(),
            [](const Memory& a, const Memory& b) { return a.timestamp < b.timestamp; });
    }

private:
    void forgetWeakest() {
        if (memories_.empty()) return;
        auto weakest = std::min_element(memories_.begin(), memories_.end(),
            [](const Memory& a, const Memory& b) {
                return (a.currentStrength * a.importance) < (b.currentStrength * b.importance);
            });
        memories_.erase(weakest);
    }

    std::vector<Memory> memories_;
    size_t maxMemories_;
};

} // namespace npc
