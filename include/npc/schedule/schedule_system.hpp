#pragma once

#include "../core/types.hpp"
#include <vector>
#include <string>
#include <optional>
#include <algorithm>

namespace npc {

struct ScheduleEntry {
    int startHour;   // 0-23
    int endHour;     // 0-23 (can wrap: startHour=22, endHour=6 means overnight)
    ActivityType activity;
    std::string location;  // location name in the world
    int priority = 0;      // higher = more important

    bool isActiveAt(int hour) const {
        if (startHour <= endHour) {
            return hour >= startHour && hour < endHour;
        }
        // Wrapping (e.g., 22:00 - 06:00)
        return hour >= startHour || hour < endHour;
    }
};

class ScheduleSystem {
public:
    void addEntry(ScheduleEntry entry) {
        schedule_.push_back(std::move(entry));
        std::sort(schedule_.begin(), schedule_.end(),
            [](const ScheduleEntry& a, const ScheduleEntry& b) {
                return a.priority > b.priority;
            });
    }

    void addEntry(int startHour, int endHour, ActivityType activity,
                  const std::string& location, int priority = 0) {
        addEntry({startHour, endHour, activity, location, priority});
    }

    std::optional<ScheduleEntry> getCurrentActivity(float currentHour) const {
        int hour = static_cast<int>(currentHour) % 24;

        for (const auto& entry : schedule_) {
            if (entry.isActiveAt(hour)) {
                return entry;
            }
        }
        return std::nullopt;
    }

    std::optional<ScheduleEntry> getNextActivity(float currentHour) const {
        int hour = static_cast<int>(currentHour) % 24;

        // Find the next activity that starts after current hour
        int bestDist = 25;
        const ScheduleEntry* best = nullptr;

        for (const auto& entry : schedule_) {
            int dist = (entry.startHour - hour + 24) % 24;
            if (dist > 0 && dist < bestDist) {
                bestDist = dist;
                best = &entry;
            }
        }

        if (best) return *best;
        return std::nullopt;
    }

    const std::vector<ScheduleEntry>& entries() const { return schedule_; }

    void clearSchedule() { schedule_.clear(); }

    // ─── Preset schedules ────────────────────────────────────────────

    static ScheduleSystem createGuardSchedule() {
        ScheduleSystem s;
        s.addEntry(6, 7,   ActivityType::Eat,      "Tavern",   0);
        s.addEntry(7, 12,  ActivityType::Patrol,   "Village",  1);
        s.addEntry(12, 13, ActivityType::Eat,      "Tavern",   0);
        s.addEntry(13, 19, ActivityType::Patrol,   "Village",  1);
        s.addEntry(19, 20, ActivityType::Eat,      "Tavern",   0);
        s.addEntry(20, 22, ActivityType::Socialize,"Tavern",   0);
        s.addEntry(22, 6,  ActivityType::Guard,    "Gate",     2);
        return s;
    }

    static ScheduleSystem createBlacksmithSchedule() {
        ScheduleSystem s;
        s.addEntry(6, 7,   ActivityType::Eat,      "Tavern",     0);
        s.addEntry(7, 12,  ActivityType::Work,     "Forge",      1);
        s.addEntry(12, 13, ActivityType::Eat,      "Tavern",     0);
        s.addEntry(13, 17, ActivityType::Work,     "Forge",      1);
        s.addEntry(17, 19, ActivityType::Socialize,"Square",     0);
        s.addEntry(19, 20, ActivityType::Eat,      "Tavern",     0);
        s.addEntry(20, 6,  ActivityType::Sleep,    "SmithHouse", 0);
        return s;
    }

    static ScheduleSystem createMerchantSchedule() {
        ScheduleSystem s;
        s.addEntry(6, 7,   ActivityType::Eat,      "Tavern",      0);
        s.addEntry(7, 12,  ActivityType::Trade,    "Market",      1);
        s.addEntry(12, 13, ActivityType::Eat,      "Tavern",      0);
        s.addEntry(13, 18, ActivityType::Trade,    "Market",      1);
        s.addEntry(18, 20, ActivityType::Socialize,"Tavern",      0);
        s.addEntry(20, 6,  ActivityType::Sleep,    "MerchHouse",  0);
        return s;
    }

    static ScheduleSystem createInnkeeperSchedule() {
        ScheduleSystem s;
        s.addEntry(5, 6,   ActivityType::Work,     "Tavern",      1);
        s.addEntry(6, 12,  ActivityType::Work,     "Tavern",      1);
        s.addEntry(12, 14, ActivityType::Eat,      "Tavern",      0);
        s.addEntry(14, 22, ActivityType::Work,     "Tavern",      1);
        s.addEntry(22, 5,  ActivityType::Sleep,    "TavernRoom",  0);
        return s;
    }

    static ScheduleSystem createFarmerSchedule() {
        ScheduleSystem s;
        s.addEntry(5, 6,   ActivityType::Eat,      "Tavern",      0);
        s.addEntry(6, 12,  ActivityType::Work,     "Farm",        1);
        s.addEntry(12, 13, ActivityType::Eat,      "Tavern",      0);
        s.addEntry(13, 17, ActivityType::Work,     "Farm",        1);
        s.addEntry(17, 19, ActivityType::Socialize,"Square",      0);
        s.addEntry(19, 20, ActivityType::Eat,      "Tavern",      0);
        s.addEntry(20, 5,  ActivityType::Sleep,    "FarmHouse",   0);
        return s;
    }

private:
    std::vector<ScheduleEntry> schedule_;
};

} // namespace npc
