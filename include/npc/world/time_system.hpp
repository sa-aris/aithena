#pragma once

#include "../core/types.hpp"
#include "../event/event_system.hpp"
#include <iostream>
#include <functional>

namespace npc {

class TimeSystem {
public:
    explicit TimeSystem(float startHour = 6.0f)
        : currentHour_(startHour), day_(1) {}

    void update(float dt, EventBus& events) {
        float prevHour = currentHour_;
        currentHour_ += dt;

        // New day rollover
        if (currentHour_ >= 24.0f) {
            currentHour_ -= 24.0f;
            day_++;
        }

        // Publish time events at major transitions
        auto prevTod = getTimeOfDayAt(prevHour);
        auto currTod = getTimeOfDay();
        if (prevTod != currTod) {
            events.publish(TimeEvent{currentHour_, currTod, day_});
        }
    }

    float currentHour() const { return currentHour_; }
    int day() const { return day_; }
    float totalHours() const { return (day_ - 1) * 24.0f + currentHour_; }

    TimeOfDay getTimeOfDay() const {
        return getTimeOfDayAt(currentHour_);
    }

    bool isDayTime() const {
        auto tod = getTimeOfDay();
        return tod != TimeOfDay::Night;
    }

    bool isNightTime() const { return !isDayTime(); }

    std::string formatClock() const {
        int h = static_cast<int>(currentHour_) % 24;
        int m = static_cast<int>((currentHour_ - static_cast<int>(currentHour_)) * 60.0f);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        return std::string(buf);
    }

    std::string formatFull() const {
        return "Day " + std::to_string(day_) + " " + formatClock();
    }

    void setHour(float h) { currentHour_ = h; }

private:
    static TimeOfDay getTimeOfDayAt(float hour) {
        if (hour >= 5.0f  && hour < 7.0f)  return TimeOfDay::Dawn;
        if (hour >= 7.0f  && hour < 12.0f) return TimeOfDay::Morning;
        if (hour >= 12.0f && hour < 13.0f) return TimeOfDay::Noon;
        if (hour >= 13.0f && hour < 17.0f) return TimeOfDay::Afternoon;
        if (hour >= 17.0f && hour < 20.0f) return TimeOfDay::Evening;
        return TimeOfDay::Night;
    }

    float currentHour_;
    int day_;
};

} // namespace npc
