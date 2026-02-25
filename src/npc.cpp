#include "npc/npc.hpp"
#include "npc/world/world.hpp"

namespace npc {

void NPC::update(float dt, GameWorld& world) {
    float currentTime = world.time().totalHours();
    std::string timeStr = world.time().formatClock();

    // ─── 1. Update needs & emotions ──────────────────────────────────
    emotions.update(dt);

    // ─── 2. Update memory ────────────────────────────────────────────
    memory.update(dt);

    // ─── 3. Update perception ────────────────────────────────────────
    std::vector<SensoryInput> sensoryInputs;
    for (const auto& other : world.npcs()) {
        if (other->id == id) continue;
        SensoryInput si;
        si.entityId = other->id;
        si.position = other->position;
        si.noiseLevel = other->combat.inCombat ? 0.8f : 0.3f;
        si.isHostile = (other->type == NPCType::Enemy);
        sensoryInputs.push_back(si);
    }
    perception.update(position, facing, sensoryInputs, currentTime, dt);

    // ─── 4. Update combat threat evaluation ──────────────────────────
    auto threats = perception.getThreats();
    std::vector<PerceivedEntity> perceivedVec;
    for (const auto& [eid, pe] : perception.perceived()) {
        perceivedVec.push_back(pe);
    }
    combat.evaluateThreats(perceivedVec, position);
    combat.update(dt);

    // ─── 5. Populate blackboard for AI ───────────────────────────────
    auto& bb = fsm.blackboard();
    bb.set<float>("_time", currentTime);
    bb.set<float>("health_pct", combat.stats.healthPercent());
    bb.set<bool>("in_combat", combat.inCombat);
    bb.set<bool>("has_threats", !threats.empty());
    bb.set<int>("threat_count", combat.threatCount());
    bb.set<bool>("should_flee", combat.shouldFlee());
    bb.set<bool>("has_urgent_need", emotions.hasUrgentNeed());
    bb.set<float>("mood", emotions.getMood());
    bb.set<std::string>("dominant_emotion", emotionToString(emotions.getDominantEmotion()));
    bb.set<float>("flee_modifier", emotions.getFleeModifier());

    // Schedule info
    auto currentActivity = schedule.getCurrentActivity(world.time().currentHour());
    if (currentActivity) {
        bb.set<std::string>("scheduled_activity", activityToString(currentActivity->activity));
        bb.set<std::string>("scheduled_location", currentActivity->location);
    }

    // ─── 6. Update FSM (main behavior driver) ───────────────────────
    fsm.update(dt);

    // ─── 7. Movement ─────────────────────────────────────────────────
    updateMovement(dt);
}

} // namespace npc
