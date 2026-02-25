#pragma once

#include "../core/types.hpp"
#include "../core/vec2.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace npc {

class GroupBehavior {
public:
    void setLeader(EntityId leader) { leader_ = leader; }
    EntityId leader() const { return leader_; }

    void addMember(EntityId id) {
        if (std::find(members_.begin(), members_.end(), id) == members_.end()) {
            members_.push_back(id);
        }
    }

    void removeMember(EntityId id) {
        members_.erase(
            std::remove(members_.begin(), members_.end(), id),
            members_.end()
        );
    }

    void setFormation(FormationType type) { formation_ = type; }
    FormationType formation() const { return formation_; }

    const std::vector<EntityId>& members() const { return members_; }
    int size() const { return static_cast<int>(members_.size()); }

    // Get formation position relative to leader position
    Vec2 getFormationPosition(int memberIndex, Vec2 leaderPos, Vec2 leaderFacing) const {
        if (memberIndex < 0 || members_.empty()) return leaderPos;

        Vec2 right = {-leaderFacing.y, leaderFacing.x}; // perpendicular
        float spacing = 2.0f;

        switch (formation_) {
            case FormationType::Line: {
                // Members line up perpendicular to leader's facing
                int half = size() / 2;
                float offset = (memberIndex - half) * spacing;
                return leaderPos + right * offset;
            }
            case FormationType::Column: {
                // Members line up behind leader
                Vec2 back = leaderFacing * -1.0f;
                return leaderPos + back * spacing * (memberIndex + 1);
            }
            case FormationType::Circle: {
                // Members form a circle around leader
                float angle = (2.0f * 3.14159f * memberIndex) / std::max(1, size());
                float radius = spacing * 2.0f;
                return leaderPos + Vec2(std::cos(angle), std::sin(angle)) * radius;
            }
            case FormationType::Wedge: {
                // V-formation
                int side = memberIndex % 2 == 0 ? 1 : -1;
                int row = (memberIndex / 2) + 1;
                Vec2 back = leaderFacing * -1.0f;
                return leaderPos + back * (spacing * row) + right * (spacing * row * side * 0.5f);
            }
        }
        return leaderPos;
    }

    struct GroupOrder {
        enum Type { MoveTo, Attack, Defend, Retreat, Regroup };
        Type type;
        Vec2 targetPos;
        EntityId targetEntity = INVALID_ENTITY;
    };

    void issueOrder(const GroupOrder& order) {
        currentOrder_ = order;
        hasOrder_ = true;
    }

    bool hasOrder() const { return hasOrder_; }
    const GroupOrder& currentOrder() const { return currentOrder_; }
    void clearOrder() { hasOrder_ = false; }

private:
    EntityId leader_ = INVALID_ENTITY;
    std::vector<EntityId> members_;
    FormationType formation_ = FormationType::Line;
    GroupOrder currentOrder_;
    bool hasOrder_ = false;
};

} // namespace npc
