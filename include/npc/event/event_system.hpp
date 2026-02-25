#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <any>
#include <cstdint>
#include <string>
#include "../core/types.hpp"
#include "../core/vec2.hpp"

namespace npc {

using SubscriptionId = uint32_t;

// ─── Event Types ─────────────────────────────────────────────────────

struct CombatEvent {
    EntityId attacker;
    EntityId defender;
    float damage;
    bool killed;
    Vec2 location;
};

struct PerceptionEvent {
    EntityId observer;
    EntityId target;
    AwarenessLevel level;
    Vec2 location;
};

struct DialogEvent {
    EntityId speaker;
    EntityId listener;
    std::string dialogId;
    std::string chosenOption;
};

struct TradeEvent {
    EntityId buyer;
    EntityId seller;
    ItemId item;
    int quantity;
    float price;
};

struct TimeEvent {
    float currentHour;
    TimeOfDay timeOfDay;
    int day;
};

struct DeathEvent {
    EntityId deceased;
    EntityId killer;
    Vec2 location;
};

struct FactionEvent {
    FactionId faction1;
    FactionId faction2;
    float relationChange;
};

struct WorldEvent {
    std::string eventType;
    std::string description;
    Vec2 location;
    float severity; // 0-1
};

// ─── Event Bus ───────────────────────────────────────────────────────

class EventBus {
public:
    template<typename EventT>
    SubscriptionId subscribe(std::function<void(const EventT&)> callback) {
        auto id = nextId_++;
        auto typeIdx = std::type_index(typeid(EventT));

        auto wrapper = [callback](const std::any& event) {
            callback(std::any_cast<const EventT&>(event));
        };

        subscribers_[typeIdx].push_back({id, wrapper});
        return id;
    }

    void unsubscribe(SubscriptionId id) {
        for (auto& [type, subs] : subscribers_) {
            subs.erase(
                std::remove_if(subs.begin(), subs.end(),
                    [id](const Subscriber& s) { return s.id == id; }),
                subs.end()
            );
        }
    }

    template<typename EventT>
    void publish(const EventT& event) {
        auto typeIdx = std::type_index(typeid(EventT));
        auto it = subscribers_.find(typeIdx);
        if (it == subscribers_.end()) return;

        for (auto& sub : it->second) {
            sub.callback(event);
        }
    }

    void clear() {
        subscribers_.clear();
    }

private:
    struct Subscriber {
        SubscriptionId id;
        std::function<void(const std::any&)> callback;
    };

    SubscriptionId nextId_ = 1;
    std::unordered_map<std::type_index, std::vector<Subscriber>> subscribers_;
};

} // namespace npc
