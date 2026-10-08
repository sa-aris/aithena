#include "npc/world/world.hpp"
#include "npc/npc.hpp"

namespace npc {

NPC* GameWorld::findNPC(EntityId id) {
    for (auto& npc : npcs_) {
        if (npc->id == id) return npc.get();
    }
    return nullptr;
}

NPC* GameWorld::findNPC(const std::string& name) {
    for (auto& npc : npcs_) {
        if (npc->name == name) return npc.get();
    }
    return nullptr;
}

void GameWorld::update(float dt) {
    if (!std::isfinite(dt) || dt < 0.0f) return;
    time_.update(dt, events_);

    // Process scheduled world events
    eventManager_.update(time_.currentHour(), *this);

    for (auto& npc : npcs_) {
        npc->update(dt, *this);
    }
    updateSocial();
}

bool GameWorld::socialLineOfSight(Vec2 from, Vec2 to) const {
    int x = from.gridX(), y = from.gridY();
    const int tx = to.gridX(), ty = to.gridY();
    const int dx = std::abs(tx - x), dy = -std::abs(ty - y);
    const int sx = x < tx ? 1 : -1, sy = y < ty ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        if (!inBounds(x, y)) return false;
        const auto type = cell(x, y).type;
        if (type == CellType::Wall) return false;
        if (x == tx && y == ty) return true;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}

void GameWorld::enableSocialSimulation(bool enabled) {
    socialEnabled_ = enabled;
    if (!enabled) return;
    social_.setContactRule([this](const SocialActor& a, const SocialActor& b) {
        return socialLineOfSight(a.position, b.position);
    });
    social_.setReachabilityRule([this](const SocialActor& a, const SocialActor& b) {
        auto n = findNPC(a.id);
        if (n && n->pathfinder) {
            const auto route = n->pathfinder->findPath(a.position, b.position);
            return !route.empty() && route.back().distanceTo(b.position) < 0.5f;
        }
        // Without a pathfinder, NPC::moveTo uses straight-line movement.
        int x = a.position.gridX(), y = a.position.gridY();
        const int tx = b.position.gridX(), ty = b.position.gridY();
        const int dx = std::abs(tx - x), dy = -std::abs(ty - y);
        const int sx = x < tx ? 1 : -1, sy = y < ty ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            if (!isWalkable(x, y)) return false;
            if (x == tx && y == ty) return true;
            const int twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    });
    syncSocialActors();
}

void GameWorld::syncSocialActors() {
    std::set<EntityId> present;
    for (const auto& n : npcs_) {
        if (!n) continue;
        present.insert(n->id);
        SocialActor a;
        if (auto existing = social_.actor(n->id)) a = *existing;
        a.id = n->id;
        a.name = n->name;
        a.position = n->position;
        a.personality = n->personality;
        a.alive = n->combat.stats.isAlive();
        a.available = !n->combat.inCombat && !n->emotions.hasUrgentNeed() &&
            n->fsm.blackboard().getOr<bool>("social_available", true);
        a.moveSpeed = n->moveSpeed;
        const float stamina = n->combat.stats.stamina.current;
        auto prior = socialStamina_.find(n->id);
        a.effortBudget = prior == socialStamina_.end() ? stamina / 100.0
            : std::max(0.0, a.effortBudget + (stamina - prior->second) / 100.0);
        if (!social_.upsertActor(a))
            throw std::invalid_argument("NPC identity or physical state is invalid for social simulation");
        socialStamina_[n->id] = stamina;
    }
    std::vector<EntityId> removed;
    for (const auto& a : social_.actors())
        if (a.second.alive && !present.count(a.first)) removed.push_back(a.first);
    for (const auto id : removed) social_.removeActor(id);
    if (!social_.advanceTo(time_.totalHours()))
        throw std::invalid_argument("World time precedes social simulation time");
}

void GameWorld::updateSocial() {
    if (!socialEnabled_) return;
    syncSocialActors();
    std::vector<SocialContractId> pending;
    std::set<EntityId> engaged;
    for (const auto& item : social_.contracts()) {
        const auto& c = item.second;
        if (c.phase == ContractPhase::Travelling) {
            engaged.insert(c.actor);
            social_.complete(c.id);
        } else if (c.phase == ContractPhase::Open) pending.push_back(c.id);
    }
    std::sort(pending.begin(), pending.end(), [this](auto left, auto right) {
        const auto& a = *social_.contract(left);
        const auto& b = *social_.contract(right);
        return a.deadline < b.deadline || (a.deadline == b.deadline && a.id < b.id);
    });
    for (auto id : pending) {
        const auto who = social_.contract(id)->actor;
        if (engaged.count(who)) continue;
        if (social_.commit(social_.decide(id))) {
            engaged.insert(who);
            social_.complete(id);
        }
    }
    social_.spreadRumors(static_cast<uint64_t>(std::floor(social_.time())));
    // Apply authoritative intentions even if an audit event was evicted. Keep
    // tracking a moving beneficiary and clear only the adapter's own movement.
    for (const auto& item : social_.contracts()) {
        const auto& c = item.second;
        auto n = findNPC(c.actor);
        if (!n) continue;
        auto& bb = n->fsm.blackboard();
        const auto tracked = bb.getOr<uint64_t>("social_contract", 0);
        if (c.phase == ContractPhase::Travelling) {
            auto target = findNPC(c.beneficiary);
            bb.set<uint64_t>("social_contract", c.id);
            bb.set<std::string>("social_intent", c.topic);
            if (target && social_.actor(c.actor)->available &&
                (!n->isMoving || n->moveTarget != target->position))
                n->moveTo(target->position);
            else if (!social_.actor(c.actor)->available && target && n->moveTarget == target->position)
                n->isMoving = false;
        } else if (tracked == c.id) {
            bb.remove("social_contract");
            bb.remove("social_intent");
            auto target = findNPC(c.beneficiary);
            if (target && n->moveTarget == target->position) {
                n->isMoving = false;
                n->currentPath.clear();
            }
        }
    }
    for (const auto& n : npcs_) {
        if (!n) continue;
        if (const auto a = social_.actor(n->id)) {
            n->combat.stats.stamina.current = std::min(n->combat.stats.stamina.max,
                static_cast<float>(a->effortBudget * 100.0));
            socialStamina_[n->id] = n->combat.stats.stamina.current;
        }
    }
    for (const auto& e : social_.drainEvents()) {
        auto n = findNPC(e.observer);
        if (n) {
            if (e.kind == SocialEventKind::BeliefChanged || e.kind == SocialEventKind::Informed) {
                Memory memory;
                memory.type = MemoryType::Interaction;
                memory.entityId = e.actor;
                memory.description = e.topic + (e.kind == SocialEventKind::BeliefChanged
                    ? std::string(": ") + contractPhaseName(e.phase) : ": learned");
                memory.timestamp = static_cast<float>(e.time);
                memory.dayCreated = static_cast<int>(e.time / 24.0) + 1;
                memory.source = e.firstHand ? MemorySource::Observed : MemorySource::Hearsay;
                memory.reliability = static_cast<float>(e.confidence);
                if (e.source != INVALID_ENTITY) memory.sourceEntity = e.source;
                if (e.kind == SocialEventKind::BeliefChanged) {
                    if (auto report = social_.belief(e.observer, e.contract)) {
                        memory.hopCount = static_cast<int>(report->path.size()) - 1;
                    }
                }
                n->memory.addMemory(std::move(memory));
            }
        }
        events_.publish(e);
    }
}

void GameWorld::printMap() const {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            // Check if any NPC is at this position
            bool npcHere = false;
            for (const auto& npc : npcs_) {
                if (npc->position.gridX() == x && npc->position.gridY() == y) {
                    // NPC marker
                    char c = '?';
                    switch (npc->type) {
                        case NPCType::Guard:      c = 'G'; break;
                        case NPCType::Merchant:   c = 'M'; break;
                        case NPCType::Blacksmith:  c = 'B'; break;
                        case NPCType::Villager:   c = 'V'; break;
                        case NPCType::Innkeeper:  c = 'I'; break;
                        case NPCType::Farmer:     c = 'F'; break;
                        case NPCType::Enemy:      c = 'W'; break;
                    }
                    std::cout << c;
                    npcHere = true;
                    break;
                }
            }
            if (npcHere) continue;

            switch (grid_[y][x].type) {
                case CellType::Grass:    std::cout << '.'; break;
                case CellType::Road:     std::cout << '#'; break;
                case CellType::Building: std::cout << 'H'; break;
                case CellType::Water:    std::cout << '~'; break;
                case CellType::Forest:   std::cout << 'T'; break;
                case CellType::Wall:     std::cout << 'X'; break;
                case CellType::Door:     std::cout << 'D'; break;
            }
        }
        std::cout << '\n';
    }
}

} // namespace npc
