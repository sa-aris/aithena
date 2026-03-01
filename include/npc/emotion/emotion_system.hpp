#pragma once

#include "../core/types.hpp"
#include "../personality/personality_traits.hpp"
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace npc {

struct Need {
    NeedType type;
    float value = 80.0f;         // 0 (desperate) to 100 (fully satisfied)
    float decayRate = 2.0f;      // units per game hour
    float urgencyThreshold = 30.0f;
    float criticalThreshold = 10.0f;

    bool isUrgent() const { return value <= urgencyThreshold; }
    bool isCritical() const { return value <= criticalThreshold; }

    // Normalized urgency: 0 = satisfied, 1 = desperate
    float urgency() const {
        return 1.0f - std::clamp(value / 100.0f, 0.0f, 1.0f);
    }
};

struct EmotionState {
    EmotionType type;
    float intensity = 0.5f;  // 0-1
    float duration = 1.0f;   // game hours remaining
    float elapsed = 0.0f;
};

class EmotionSystem {
public:
    EmotionSystem() {
        // Initialize default needs
        needs_[NeedType::Hunger]  = {NeedType::Hunger,  80.0f, 4.0f,  30.0f, 10.0f};
        needs_[NeedType::Thirst]  = {NeedType::Thirst,  85.0f, 5.0f,  30.0f, 10.0f};
        needs_[NeedType::Sleep]   = {NeedType::Sleep,   90.0f, 3.0f,  25.0f,  8.0f};
        needs_[NeedType::Social]  = {NeedType::Social,  70.0f, 1.5f,  25.0f, 10.0f};
        needs_[NeedType::Fun]     = {NeedType::Fun,     60.0f, 1.0f,  20.0f,  5.0f};
        needs_[NeedType::Safety]  = {NeedType::Safety, 100.0f, 0.5f,  40.0f, 20.0f};
        needs_[NeedType::Comfort] = {NeedType::Comfort, 75.0f, 0.8f,  20.0f,  5.0f};
    }

    void update(float dt) {
        // Decay needs
        for (auto& [type, need] : needs_) {
            need.value -= need.decayRate * dt;
            need.value = std::max(0.0f, need.value);
        }

        // Update emotions
        for (auto& e : emotions_) {
            e.elapsed += dt;
        }
        // Remove expired emotions
        emotions_.erase(
            std::remove_if(emotions_.begin(), emotions_.end(),
                [](const EmotionState& e) { return e.elapsed >= e.duration; }),
            emotions_.end()
        );

        // Update mood based on needs and emotions
        updateMood();
    }

    void addEmotion(EmotionType type, float intensity = 0.5f, float duration = 1.0f) {
        // Apply personality modifiers to emotion intensity
        float adjustedIntensity = intensity;
        if (type == EmotionType::Fearful) {
            adjustedIntensity *= fearIntensityMod_;
        } else if (type == EmotionType::Angry) {
            adjustedIntensity *= angerIntensityMod_;
        }
        adjustedIntensity = std::clamp(adjustedIntensity, 0.0f, 1.0f);

        // If same emotion exists, intensify it
        for (auto& e : emotions_) {
            if (e.type == type) {
                e.intensity = std::min(1.0f, e.intensity + adjustedIntensity * 0.5f);
                e.duration = std::max(e.duration - e.elapsed, duration);
                e.elapsed = 0.0f;
                return;
            }
        }
        emotions_.push_back({type, adjustedIntensity, duration, 0.0f});
    }

    void satisfyNeed(NeedType type, float amount) {
        auto it = needs_.find(type);
        if (it != needs_.end()) {
            it->second.value = std::min(100.0f, it->second.value + amount);
        }
    }

    void depletNeed(NeedType type, float amount) {
        auto it = needs_.find(type);
        if (it != needs_.end()) {
            it->second.value = std::max(0.0f, it->second.value - amount);
        }
    }

    NeedType getMostUrgentNeed() const {
        NeedType most = NeedType::Hunger;
        float highestUrgency = -1.0f;
        for (const auto& [type, need] : needs_) {
            float u = need.urgency();
            if (u > highestUrgency) {
                highestUrgency = u;
                most = type;
            }
        }
        return most;
    }

    bool hasUrgentNeed() const {
        for (const auto& [type, need] : needs_) {
            if (need.isUrgent()) return true;
        }
        return false;
    }

    bool hasCriticalNeed() const {
        for (const auto& [type, need] : needs_) {
            if (need.isCritical()) return true;
        }
        return false;
    }

    float getMood() const { return mood_; }

    EmotionType getDominantEmotion() const {
        if (emotions_.empty()) return EmotionType::Neutral;
        auto it = std::max_element(emotions_.begin(), emotions_.end(),
            [](const EmotionState& a, const EmotionState& b) {
                return a.intensity < b.intensity;
            });
        return it->type;
    }

    std::string getMoodString() const {
        if (mood_ > 0.5f) return "Joyful";
        if (mood_ > 0.2f) return "Content";
        if (mood_ > -0.2f) return "Neutral";
        if (mood_ > -0.5f) return "Uneasy";
        return "Distressed";
    }

    const Need& getNeed(NeedType type) const {
        return needs_.at(type);
    }

    Need& getNeed(NeedType type) {
        return needs_.at(type);
    }

    const std::map<NeedType, Need>& needs() const { return needs_; }
    const std::vector<EmotionState>& emotions() const { return emotions_; }

    // Decision modifiers: how emotions/needs affect behavior weights
    float getCombatModifier() const {
        float mod = 1.0f;
        float safety = needs_.at(NeedType::Safety).value / 100.0f;
        mod *= (0.5f + safety * 0.5f);

        for (const auto& e : emotions_) {
            if (e.type == EmotionType::Angry) {
                mod *= (1.0f + e.intensity * 0.3f * angerIntensityMod_);
            }
            if (e.type == EmotionType::Fearful) {
                mod *= (1.0f - e.intensity * 0.4f * fearIntensityMod_);
            }
        }
        return std::clamp(mod, 0.1f, 2.0f);
    }

    float getSocialModifier() const {
        float social = needs_.at(NeedType::Social).urgency();
        float mod = 0.5f + social * 0.5f;
        if (mood_ > 0.2f) mod *= 1.2f;
        if (mood_ < -0.3f) mod *= 0.7f;
        return std::clamp(mod, 0.1f, 2.0f);
    }

    float getFleeModifier() const {
        float mod = 0.0f;
        for (const auto& e : emotions_) {
            if (e.type == EmotionType::Fearful) mod += e.intensity;
        }
        float safety = 1.0f - needs_.at(NeedType::Safety).value / 100.0f;
        mod += safety * 0.5f;
        mod *= fearIntensityMod_;
        return std::clamp(mod, 0.0f, 1.0f);
    }

private:
    void updateMood() {
        // Mood = weighted average of need satisfaction + emotional influence
        float needScore = 0.0f;
        float totalWeight = 0.0f;
        for (const auto& [type, need] : needs_) {
            float w = 1.0f;
            if (type == NeedType::Safety) w = 2.0f;
            if (type == NeedType::Hunger || type == NeedType::Sleep) w = 1.5f;
            needScore += (need.value / 100.0f) * w;
            totalWeight += w;
        }
        needScore = (needScore / totalWeight) * 2.0f - 1.0f; // map to [-1, 1]

        float emotionScore = 0.0f;
        for (const auto& e : emotions_) {
            float val = 0.0f;
            switch (e.type) {
                case EmotionType::Happy:     val =  1.0f; break;
                case EmotionType::Surprised: val =  0.2f; break;
                case EmotionType::Neutral:   val =  0.0f; break;
                case EmotionType::Sad:       val = -0.5f; break;
                case EmotionType::Angry:     val = -0.3f; break;
                case EmotionType::Fearful:   val = -0.7f; break;
                case EmotionType::Disgusted: val = -0.4f; break;
            }
            emotionScore += val * e.intensity;
        }
        if (!emotions_.empty()) {
            emotionScore /= static_cast<float>(emotions_.size());
        }

        float emotionWeight = 0.4f * moodStabilityMod_;
        float needWeight = 1.0f - emotionWeight;
        mood_ = std::clamp(needScore * needWeight + emotionScore * emotionWeight, -1.0f, 1.0f);
    }

    std::map<NeedType, Need> needs_;
    std::vector<EmotionState> emotions_;
    float mood_ = 0.5f;

    // Personality influence modifiers
    float fearIntensityMod_ = 1.0f;
    float angerIntensityMod_ = 1.0f;
    float socialDecayMod_ = 1.0f;
    float moodStabilityMod_ = 1.0f;

public:
    void applyPersonality(const PersonalityTraits& p) {
        fearIntensityMod_ = p.fearIntensityMultiplier();
        angerIntensityMod_ = p.angerIntensityMultiplier();
        socialDecayMod_ = p.socialDecayMultiplier();
        moodStabilityMod_ = p.moodStabilityMultiplier();
        needs_[NeedType::Social].decayRate *= socialDecayMod_;
    }
};

} // namespace npc
