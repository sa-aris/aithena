/*
 * ═══════════════════════════════════════════════════════════════════════
 *  NPC Behavior System — Medieval Village Simulation Demo
 * ═══════════════════════════════════════════════════════════════════════
 *
 *  Demonstrates all 13 subsystems working together:
 *   FSM, Behavior Tree, Utility AI, Event Bus, Perception, Memory,
 *   Emotion/Needs, Combat, Dialog, Trade, Pathfinding, Schedule,
 *   Faction/Group Behavior
 *
 *  NPCs: Alaric (Guard), Brina (Blacksmith), Cedric (Merchant),
 *         Dagna (Innkeeper), Elmund (Farmer)
 *
 *  Scenario: A day in the village with a wolf attack at 14:00
 * ═══════════════════════════════════════════════════════════════════════
 */

#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include "npc/social/faction_system.hpp"
#include "npc/social/group_behavior.hpp"
#include <iostream>
#include <iomanip>
#include <memory>

using namespace npc;

// ─── Faction IDs ─────────────────────────────────────────────────────
constexpr FactionId VILLAGE_FACTION = 1;
constexpr FactionId WOLF_FACTION    = 2;

// ─── Item IDs ────────────────────────────────────────────────────────
constexpr ItemId ITEM_SWORD       = 1;
constexpr ItemId ITEM_SHIELD      = 2;
constexpr ItemId ITEM_BREAD       = 3;
constexpr ItemId ITEM_ALE         = 4;
constexpr ItemId ITEM_IRON_ORE    = 5;
constexpr ItemId ITEM_HORSESHOE   = 6;
constexpr ItemId ITEM_HEALTH_POT  = 7;
constexpr ItemId ITEM_WHEAT       = 8;
constexpr ItemId ITEM_LEATHER     = 9;
constexpr ItemId ITEM_TOOLS       = 10;

// ─── Forward declarations ────────────────────────────────────────────
void buildVillageMap(GameWorld& world);
void setupFactions(FactionSystem& factions);
void setupItems(TradeSystem& trade);
std::shared_ptr<NPC> createAlaric(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createBrina(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createCedric(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createDagna(GameWorld& world, std::shared_ptr<Pathfinder> pf);
std::shared_ptr<NPC> createElmund(GameWorld& world, std::shared_ptr<Pathfinder> pf);
void spawnWolves(GameWorld& world, FactionSystem& factions, std::shared_ptr<Pathfinder> pf);
void runCombatEncounter(GameWorld& world, FactionSystem& factions);
void runTradeDemo(GameWorld& world);
void runDialogDemo(GameWorld& world);

// ═══════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════

int main() {
    std::cout << R"(
 ╔══════════════════════════════════════════════════════════════╗
 ║     NPC Behavior System — Medieval Village Simulation       ║
 ║     13 Subsystems Working Together                          ║
 ╚══════════════════════════════════════════════════════════════╝
)" << "\n";

    // ─── Create world ────────────────────────────────────────────────
    GameWorld world(40, 25);
    buildVillageMap(world);

    // ─── Setup factions ──────────────────────────────────────────────
    FactionSystem factions;
    setupFactions(factions);

    // ─── Create shared pathfinder ────────────────────────────────────
    auto pathfinder = std::make_shared<Pathfinder>(
        world.width(), world.height(),
        [&world](int x, int y) { return world.isWalkable(x, y); },
        [&world](int x, int y) { return world.movementCost(x, y); }
    );

    // ─── Create NPCs ────────────────────────────────────────────────
    auto alaric = createAlaric(world, pathfinder);
    auto brina  = createBrina(world, pathfinder);
    auto cedric = createCedric(world, pathfinder);
    auto dagna  = createDagna(world, pathfinder);
    auto elmund = createElmund(world, pathfinder);

    // Register NPCs to factions
    factions.addMember(VILLAGE_FACTION, alaric->id);
    factions.addMember(VILLAGE_FACTION, brina->id);
    factions.addMember(VILLAGE_FACTION, cedric->id);
    factions.addMember(VILLAGE_FACTION, dagna->id);
    factions.addMember(VILLAGE_FACTION, elmund->id);

    // Subscribe to events
    alaric->subscribeToEvents(world.events());
    brina->subscribeToEvents(world.events());
    cedric->subscribeToEvents(world.events());
    dagna->subscribeToEvents(world.events());
    elmund->subscribeToEvents(world.events());

    // Add NPCs to world
    world.addNPC(alaric);
    world.addNPC(brina);
    world.addNPC(cedric);
    world.addNPC(dagna);
    world.addNPC(elmund);

    // ═════════════════════════════════════════════════════════════════
    //  SIMULATION LOOP
    // ═════════════════════════════════════════════════════════════════

    std::cout << "=== Village Map ===\n";
    world.printMap();
    std::cout << "\n  Legend: G=Guard B=Blacksmith M=Merchant I=Innkeeper F=Farmer\n"
              << "         .=Grass #=Road H=Building T=Forest ~=Water X=Wall\n\n";
    std::cout << "========================================\n";
    std::cout << "  SIMULATION START — Day 1, 06:00\n";
    std::cout << "========================================\n\n";

    float dt = 0.25f; // each step = 15 minutes
    bool wolfSpawned = false;
    bool tradeDone = false;
    bool dialogDone = false;

    // Simulate from 06:00 to 22:00 (one full day)
    for (float simTime = 0.0f; simTime < 16.0f; simTime += dt) {
        float currentHour = world.time().currentHour();

        // ─── Major time announcements ────────────────────────────────
        int hourInt = static_cast<int>(currentHour);
        int minute = static_cast<int>((currentHour - hourInt) * 60);

        if (minute == 0) {
            std::cout << "\n--- " << world.time().formatClock()
                      << " (" << timeOfDayToString(world.time().getTimeOfDay()) << ") ---\n";
        }

        // ─── Trigger wolf attack at 14:00 ───────────────────────────
        if (currentHour >= 14.0f && !wolfSpawned) {
            std::cout << "\n"
                      << "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                      << "  !! WOLF PACK SPOTTED NEAR THE VILLAGE! !!\n"
                      << "  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n";

            world.events().publish(WorldEvent{
                "wolf_attack", "A pack of wolves approaches the village!",
                Vec2(35.0f, 12.0f), 0.8f
            });

            spawnWolves(world, factions, pathfinder);
            wolfSpawned = true;
        }

        // ─── Combat encounter (wolf attack phase) ────────────────────
        if (wolfSpawned && currentHour >= 14.25f && currentHour < 15.5f) {
            runCombatEncounter(world, factions);
        }

        // ─── Trade demo at 10:00 ────────────────────────────────────
        if (currentHour >= 10.0f && !tradeDone) {
            runTradeDemo(world);
            tradeDone = true;
        }

        // ─── Dialog demo at 12:00 ───────────────────────────────────
        if (currentHour >= 12.0f && !dialogDone) {
            runDialogDemo(world);
            dialogDone = true;
        }

        // ─── World update ────────────────────────────────────────────
        world.update(dt);
    }

    // ═════════════════════════════════════════════════════════════════
    //  END OF DAY SUMMARY
    // ═════════════════════════════════════════════════════════════════

    std::cout << "\n========================================\n";
    std::cout << "  END OF DAY SUMMARY\n";
    std::cout << "========================================\n\n";

    for (const auto& npc : world.npcs()) {
        if (npc->type == NPCType::Enemy) continue;
        std::cout << "  " << npc->getInfo() << "\n";

        // Print need summary
        const auto& needs = npc->emotions.needs();
        std::cout << "    Needs: ";
        for (const auto& [type, need] : needs) {
            if (need.isUrgent()) {
                std::cout << needToString(type) << "=" << static_cast<int>(need.value) << "! ";
            }
        }
        std::cout << "\n";

        // Print recent memories
        auto memories = npc->memory.allMemories();
        if (!memories.empty()) {
            std::cout << "    Recent memories: ";
            int shown = 0;
            for (auto it = memories.rbegin(); it != memories.rend() && shown < 3; ++it, ++shown) {
                std::cout << "\"" << it->description << "\" ";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "=== Final Village Map ===\n";
    world.printMap();
    std::cout << "\n  Simulation complete.\n";

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
//  WORLD SETUP
// ═══════════════════════════════════════════════════════════════════════

void buildVillageMap(GameWorld& world) {
    // Default: all grass (already set)

    // Roads (horizontal main road)
    for (int x = 0; x < 40; ++x) {
        world.setCell(x, 12, CellType::Road, 0.8f);
        world.setCell(x, 13, CellType::Road, 0.8f);
    }
    // Vertical road
    for (int y = 5; y < 20; ++y) {
        world.setCell(20, y, CellType::Road, 0.8f);
        world.setCell(21, y, CellType::Road, 0.8f);
    }

    // Tavern (top-left area)
    for (int y = 3; y <= 6; ++y)
        for (int x = 5; x <= 10; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(8, 6, CellType::Door, 1.0f, true);

    // Forge (left of road)
    for (int y = 8; y <= 10; ++y)
        for (int x = 14; x <= 18; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(16, 10, CellType::Door, 1.0f, true);

    // Market (right of crossroad)
    for (int y = 10; y <= 11; ++y)
        for (int x = 23; x <= 27; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);
    world.setCell(25, 11, CellType::Door, 1.0f, true);

    // Houses
    for (int y = 16; y <= 18; ++y)
        for (int x = 5; x <= 8; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);

    for (int y = 16; y <= 18; ++y)
        for (int x = 25; x <= 28; ++x)
            world.setCell(x, y, CellType::Building, 999.0f, false);

    // Farm area (bottom-right)
    for (int y = 19; y <= 23; ++y)
        for (int x = 30; x <= 38; ++x)
            world.setCell(x, y, CellType::Grass, 1.2f);

    // Forest (right edge - where wolves come from)
    for (int y = 0; y <= 24; ++y)
        for (int x = 37; x <= 39; ++x)
            world.setCell(x, y, CellType::Forest, 2.0f);

    // Water (small pond)
    for (int y = 1; y <= 2; ++y)
        for (int x = 30; x <= 33; ++x)
            world.setCell(x, y, CellType::Water, 999.0f, false);

    // Village gate (wall with opening)
    for (int x = 0; x <= 3; ++x) {
        world.setCell(x, 12, CellType::Wall, 999.0f, false);
        world.setCell(x, 13, CellType::Wall, 999.0f, false);
    }
    world.setCell(3, 12, CellType::Door, 1.0f, true);
    world.setCell(3, 13, CellType::Door, 1.0f, true);

    // Named locations
    world.addLocation("Tavern",     8.0f,  7.0f);
    world.addLocation("TavernRoom", 7.0f,  4.0f);
    world.addLocation("Forge",      16.0f, 11.0f);
    world.addLocation("SmithHouse", 6.0f,  17.0f);
    world.addLocation("Market",     25.0f, 12.0f);
    world.addLocation("MerchHouse", 26.0f, 17.0f);
    world.addLocation("FarmHouse",  26.0f, 17.0f);
    world.addLocation("Farm",       34.0f, 21.0f);
    world.addLocation("Square",     20.0f, 12.0f);
    world.addLocation("Gate",       3.0f,  12.0f);
    world.addLocation("Village",    20.0f, 12.0f);
    world.addLocation("ForestEdge", 36.0f, 12.0f);
}

void setupFactions(FactionSystem& factions) {
    factions.addFaction(VILLAGE_FACTION, "Village");
    factions.addFaction(WOLF_FACTION, "Wolves");
    factions.setRelation(VILLAGE_FACTION, WOLF_FACTION, -100.0f); // hostile
}

void setupItems(TradeSystem& trade) {
    trade.registerItem({ITEM_SWORD,      "Iron Sword",     ItemCategory::Weapon,   50.0f, 3.0f});
    trade.registerItem({ITEM_SHIELD,     "Wooden Shield",  ItemCategory::Armor,    30.0f, 4.0f});
    trade.registerItem({ITEM_BREAD,      "Fresh Bread",    ItemCategory::Food,      3.0f, 0.2f});
    trade.registerItem({ITEM_ALE,        "Ale",            ItemCategory::Food,      5.0f, 0.5f});
    trade.registerItem({ITEM_IRON_ORE,   "Iron Ore",       ItemCategory::Material, 15.0f, 5.0f});
    trade.registerItem({ITEM_HORSESHOE,  "Horseshoe",      ItemCategory::Tool,     12.0f, 1.0f});
    trade.registerItem({ITEM_HEALTH_POT, "Health Potion",  ItemCategory::Potion,   25.0f, 0.3f});
    trade.registerItem({ITEM_WHEAT,      "Wheat",          ItemCategory::Food,      2.0f, 1.0f});
    trade.registerItem({ITEM_LEATHER,    "Leather",        ItemCategory::Material,  8.0f, 1.5f});
    trade.registerItem({ITEM_TOOLS,      "Farming Tools",  ItemCategory::Tool,     20.0f, 3.0f});
}

// ═══════════════════════════════════════════════════════════════════════
//  NPC CREATION — FSM setup for each NPC type
// ═══════════════════════════════════════════════════════════════════════

std::shared_ptr<NPC> createAlaric(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(1, "Alaric", NPCType::Guard);
    npc->position = Vec2(20.0f, 12.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->schedule = ScheduleSystem::createGuardSchedule();

    // Combat stats — strong fighter
    npc->combat.stats = {120.0f, 120.0f, 20.0f, 15.0f, 6.0f, 0.15f, {}};
    npc->combat.stats.abilities.push_back(
        {"Sword Strike", AbilityType::Melee, DamageType::Physical, 15.0f, 2.0f, 0.05f, 0.0f, 0.0f});
    npc->combat.stats.abilities.push_back(
        {"Shield Bash", AbilityType::Melee, DamageType::Physical, 8.0f, 1.5f, 0.1f, 0.0f, 0.0f});

    // Patrol waypoints
    std::vector<Vec2> patrolRoute = {
        Vec2(5.0f, 12.0f), Vec2(20.0f, 12.0f),
        Vec2(35.0f, 12.0f), Vec2(20.0f, 6.0f),
        Vec2(20.0f, 18.0f), Vec2(20.0f, 12.0f)
    };
    int patrolIdx = 0;

    // ─── FSM States ──────────────────────────────────────────────────
    npc->fsm.addState("Idle",
        [npc = npc.get()](Blackboard& bb, float dt) {
            // Check schedule
            auto activity = bb.get<std::string>("scheduled_activity");
            if (activity && *activity == "Patrol") {
                bb.set<bool>("wants_patrol", true);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Standing idle.");
        });

    npc->fsm.addState("Patrol",
        [npc = npc.get(), patrolRoute, patrolIdx](Blackboard& bb, float dt) mutable {
            if (!npc->isMoving) {
                patrolIdx = (patrolIdx + 1) % patrolRoute.size();
                npc->moveTo(patrolRoute[patrolIdx]);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Starting patrol route.");
        });

    npc->fsm.addState("Combat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto target = npc->combat.selectTarget();
            if (!target) return;

            float dist = npc->position.distanceTo(target->position);
            if (dist > 2.0f) {
                npc->moveTo(target->position);
            } else {
                npc->isMoving = false;
                auto* ability = npc->combat.selectAbility(dist);
                if (ability) {
                    auto* enemy = world.findNPC(target->entityId);
                    if (enemy && enemy->combat.stats.isAlive()) {
                        auto result = npc->combat.dealDamage(enemy->combat, *ability);
                        npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                            "Attacking with " + ability->name + "! Dealt " +
                            std::to_string(static_cast<int>(result.damageDealt)) + " damage." +
                            (result.isCrit ? " CRITICAL!" : "") +
                            (result.targetKilled ? " ENEMY DEFEATED!" : ""));

                        if (result.targetKilled) {
                            npc->memory.addMemory(MemoryType::Combat,
                                "Defeated a wolf in combat", 0.5f,
                                target->entityId, 0.9f, bb.getOr<float>("_time", 0.0f));
                            npc->emotions.addEmotion(EmotionType::Happy, 0.4f, 1.0f);
                            world.events().publish(CombatEvent{
                                npc->id, target->entityId,
                                result.damageDealt, true, npc->position});
                        }
                    }
                }
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "ALERT! Enemy detected! Engaging combat!");
            npc->emotions.addEmotion(EmotionType::Angry, 0.5f, 2.0f);
            npc->emotions.depletNeed(NeedType::Safety, 20.0f);
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Thirst, 15.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Heading to the Tavern for a meal.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 30.0f * dt);
            npc->emotions.satisfyNeed(NeedType::Comfort, 10.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Going to sleep. Night watch will wait.");
        });

    // ─── FSM Transitions ─────────────────────────────────────────────
    // Priority: Combat > Flee > Eat > Patrol > Idle
    npc->fsm.addTransition("Idle", "Patrol",
        [](const Blackboard& bb) { return bb.getOr<bool>("wants_patrol", false); }, 1);
    npc->fsm.addTransition("Idle", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);

    npc->fsm.addTransition("Patrol", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Idle", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Eat", "Combat",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);

    npc->fsm.addTransition("Combat", "Patrol",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);

    npc->fsm.addTransition("Patrol", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 3);
    npc->fsm.addTransition("Eat", "Patrol",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Patrol";
        }, 1);

    npc->fsm.addTransition("Patrol", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);

    npc->fsm.setInitialState("Idle");
    return npc;
}

std::shared_ptr<NPC> createBrina(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(2, "Brina", NPCType::Blacksmith);
    npc->position = Vec2(16.0f, 11.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->schedule = ScheduleSystem::createBlacksmithSchedule();

    npc->combat.stats = {80.0f, 80.0f, 15.0f, 10.0f, 4.0f, 0.1f, {}};
    npc->combat.stats.abilities.push_back(
        {"Hammer Strike", AbilityType::Melee, DamageType::Physical, 12.0f, 1.5f, 0.08f, 0.0f, 0.0f});

    // Setup trade
    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_SWORD, 3);
    npc->trade.inventory.addItem(ITEM_SHIELD, 2);
    npc->trade.inventory.addItem(ITEM_HORSESHOE, 5);
    npc->trade.inventory.addItem(ITEM_IRON_ORE, 10);

    // ─── FSM ─────────────────────────────────────────────────────────
    npc->fsm.addState("Work",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Fun, 2.0f * dt);
            npc->trade.updatePrices();
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Working at the forge. Hammering iron.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Social, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Taking a break. Heading to eat.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Square");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Social, 15.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Fun, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Socializing at the square. Mood: " + npc->emotions.getMoodString());
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Going home to rest.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("SmithHouse");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Danger! Retreating to safety!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.6f, 2.0f);
        });

    // Transitions
    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Socialize", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);
    npc->fsm.addTransition("Work", "Flee",
        [](const Blackboard& bb) {
            return bb.getOr<bool>("has_threats", false) && bb.getOr<float>("flee_modifier", 0.0f) > 0.3f;
        }, 8);
    npc->fsm.addTransition("Flee", "Work",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);

    npc->fsm.setInitialState("Work");
    return npc;
}

std::shared_ptr<NPC> createCedric(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(3, "Cedric", NPCType::Merchant);
    npc->position = Vec2(25.0f, 12.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->schedule = ScheduleSystem::createMerchantSchedule();

    npc->combat.stats = {60.0f, 60.0f, 8.0f, 5.0f, 3.0f, 0.05f, {}};

    setupItems(npc->trade);
    npc->trade.inventory = Inventory(300.0f, 200.0f);
    npc->trade.inventory.addItem(ITEM_BREAD, 20);
    npc->trade.inventory.addItem(ITEM_ALE, 15);
    npc->trade.inventory.addItem(ITEM_HEALTH_POT, 5);
    npc->trade.inventory.addItem(ITEM_LEATHER, 8);
    npc->trade.inventory.addItem(ITEM_TOOLS, 3);

    npc->fsm.addState("Trade",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->trade.updatePrices();
            npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Opening shop. Stock: " + std::to_string(npc->trade.inventory.totalItems()) +
                     " items. Gold: " + std::to_string(static_cast<int>(npc->trade.inventory.gold())));
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Lunch break at the Tavern.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Closing shop. Time for rest.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("MerchHouse");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Wolves?! Running for cover! My goods!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.8f, 3.0f);
        });

    npc->fsm.addTransition("Trade", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Trade",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Trade";
        }, 1);
    npc->fsm.addTransition("Trade", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);
    npc->fsm.addTransition("Trade", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 8);
    npc->fsm.addTransition("Eat", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 8);
    npc->fsm.addTransition("Flee", "Trade",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);

    npc->fsm.setInitialState("Trade");
    return npc;
}

std::shared_ptr<NPC> createDagna(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(4, "Dagna", NPCType::Innkeeper);
    npc->position = Vec2(8.0f, 7.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->schedule = ScheduleSystem::createInnkeeperSchedule();

    npc->combat.stats = {70.0f, 70.0f, 10.0f, 8.0f, 3.0f, 0.05f, {}};

    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_BREAD, 30);
    npc->trade.inventory.addItem(ITEM_ALE, 25);
    npc->trade.buyMarkup = 1.2f;

    // Dialog setup
    DialogTree greetTree("greeting");
    DialogNode root;
    root.id = "root";
    root.speakerText = "Welcome to the Tavern! What can I get you?";
    root.friendlyText = "Ah, my favorite customer! The usual?";
    root.hostileText = "What do you want? Make it quick.";
    root.options = {
        {"I'd like some bread and ale.", "serve", nullptr, nullptr, -100},
        {"Any news from the village?", "gossip", nullptr, nullptr, -100},
        {"Just passing through.", "END", nullptr, nullptr, -100}
    };
    greetTree.addNode(root);

    DialogNode serveNode;
    serveNode.id = "serve";
    serveNode.speakerText = "Coming right up! That'll be 8 gold.";
    serveNode.options = {{"Thanks!", "END", nullptr, nullptr, -100}};
    greetTree.addNode(serveNode);

    DialogNode gossipNode;
    gossipNode.id = "gossip";
    gossipNode.speakerText = "I heard wolves have been spotted near the forest. Be careful out there!";
    gossipNode.options = {
        {"I'll keep my eyes open. Thanks.", "END", nullptr, nullptr, -100},
        {"Wolves? Maybe I should talk to the guard.", "END", nullptr, nullptr, -100}
    };
    greetTree.addNode(gossipNode);
    npc->dialog.addTree("greeting", std::move(greetTree));

    npc->fsm.addState("Work",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
            npc->emotions.satisfyNeed(NeedType::Social, 3.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Serving customers at the Tavern.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Hunger, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Taking a meal break.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 30.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Closing up the Tavern. Goodnight!");
        });

    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);

    npc->fsm.setInitialState("Work");
    return npc;
}

std::shared_ptr<NPC> createElmund(GameWorld& world, std::shared_ptr<Pathfinder> pf) {
    auto npc = std::make_shared<NPC>(5, "Elmund", NPCType::Farmer);
    npc->position = Vec2(34.0f, 21.0f);
    npc->pathfinder = pf;
    npc->factionId = VILLAGE_FACTION;
    npc->schedule = ScheduleSystem::createFarmerSchedule();

    // Weak in combat - runs away
    npc->combat.stats = {50.0f, 50.0f, 5.0f, 3.0f, 4.0f, 0.02f, {}};
    npc->combat.stats.abilities.push_back(
        {"Pitchfork Jab", AbilityType::Melee, DamageType::Physical, 5.0f, 1.5f, 0.1f, 0.0f, 0.0f});

    setupItems(npc->trade);
    npc->trade.inventory.addItem(ITEM_WHEAT, 30);
    npc->trade.inventory.addItem(ITEM_BREAD, 5);

    npc->fsm.addState("Work",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Farm");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Fun, 1.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Working in the fields. Good harvest today.");
        });

    npc->fsm.addState("Eat",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            } else {
                npc->emotions.satisfyNeed(NeedType::Hunger, 20.0f * dt);
                npc->emotions.satisfyNeed(NeedType::Social, 5.0f * dt);
            }
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Heading to the Tavern for a meal.");
        });

    npc->fsm.addState("Socialize",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Square");
            if (loc && !npc->isAtLocation(Vec2(loc->x, loc->y))) {
                npc->moveTo(Vec2(loc->x, loc->y));
            }
            npc->emotions.satisfyNeed(NeedType::Social, 10.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "Meeting friends at the square.");
        });

    npc->fsm.addState("Sleep",
        [npc = npc.get()](Blackboard& bb, float dt) {
            npc->emotions.satisfyNeed(NeedType::Sleep, 25.0f * dt);
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)), "Long day. Time for bed.");
        });

    npc->fsm.addState("Flee",
        [npc = npc.get(), &world](Blackboard& bb, float dt) {
            auto* loc = world.getLocation("Tavern");
            if (loc) npc->moveTo(Vec2(loc->x, loc->y));
        },
        [npc = npc.get()](Blackboard& bb) {
            npc->log(formatTime(bb.getOr<float>("_time", 0.0f)),
                     "WOLVES! Running to the tavern for safety!!");
            npc->emotions.addEmotion(EmotionType::Fearful, 0.9f, 4.0f);
            npc->emotions.depletNeed(NeedType::Safety, 50.0f);
            npc->memory.addMemory(MemoryType::WorldEvent,
                "Fled from wolves near the farm", -0.7f, std::nullopt, 0.8f,
                bb.getOr<float>("_time", 0.0f));
        });

    // Transitions
    npc->fsm.addTransition("Work", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Work",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Work";
        }, 1);
    npc->fsm.addTransition("Work", "Socialize",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Socialize";
        }, 1);
    npc->fsm.addTransition("Socialize", "Eat",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Eat";
        }, 2);
    npc->fsm.addTransition("Eat", "Sleep",
        [](const Blackboard& bb) {
            auto act = bb.get<std::string>("scheduled_activity");
            return act && *act == "Sleep";
        }, 1);

    // Flee on any threat (cowardly farmer)
    npc->fsm.addTransition("Work", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Eat", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Socialize", "Flee",
        [](const Blackboard& bb) { return bb.getOr<bool>("has_threats", false); }, 10);
    npc->fsm.addTransition("Flee", "Work",
        [](const Blackboard& bb) { return !bb.getOr<bool>("has_threats", false); }, 1);

    npc->fsm.setInitialState("Work");
    return npc;
}

// ═══════════════════════════════════════════════════════════════════════
//  WOLF SPAWN
// ═══════════════════════════════════════════════════════════════════════

void spawnWolves(GameWorld& world, FactionSystem& factions, std::shared_ptr<Pathfinder> pf) {
    for (int i = 0; i < 3; ++i) {
        EntityId wid = 100 + i;
        auto wolf = std::make_shared<NPC>(wid, "Wolf_" + std::to_string(i + 1), NPCType::Enemy);
        wolf->position = Vec2(36.0f, 10.0f + i * 2.0f);
        wolf->pathfinder = pf;
        wolf->factionId = WOLF_FACTION;
        wolf->verbose = false;
        wolf->moveSpeed = 5.0f;

        wolf->combat.stats = {40.0f, 40.0f, 12.0f, 4.0f, 7.0f, 0.1f, {}};
        wolf->combat.stats.abilities.push_back(
            {"Bite", AbilityType::Melee, DamageType::Physical, 10.0f, 1.5f, 0.03f, 0.0f, 0.0f});

        // Wolves are always aggressive — simple FSM
        wolf->fsm.addState("Hunt",
            [wolf = wolf.get(), &world](Blackboard& bb, float dt) {
                // Move toward village center
                Vec2 target(20.0f, 12.0f);

                // If sees an NPC, chase them
                auto threats = wolf->perception.getThreats();
                if (!threats.empty()) {
                    // Actually wolves see villagers as targets, not threats
                }

                // Find nearest villager
                float bestDist = 999.0f;
                NPC* nearest = nullptr;
                for (auto& other : world.npcs()) {
                    if (other->type == NPCType::Enemy) continue;
                    if (!other->combat.stats.isAlive()) continue;
                    float d = wolf->position.distanceTo(other->position);
                    if (d < bestDist) {
                        bestDist = d;
                        nearest = other.get();
                    }
                }

                if (nearest && bestDist < 15.0f) {
                    if (bestDist > 2.0f) {
                        wolf->moveTo(nearest->position);
                    } else {
                        wolf->isMoving = false;
                        auto* ability = wolf->combat.selectAbility(bestDist);
                        if (ability) {
                            auto result = wolf->combat.dealDamage(nearest->combat, *ability);
                            if (result.targetKilled) {
                                world.events().publish(CombatEvent{
                                    wolf->id, nearest->id,
                                    result.damageDealt, true, wolf->position});
                            }
                        }
                    }
                } else {
                    wolf->moveTo(target);
                }
            });

        wolf->fsm.setInitialState("Hunt");
        factions.addMember(WOLF_FACTION, wid);
        wolf->subscribeToEvents(world.events());

        // Force villager NPCs to perceive wolves
        for (auto& villagerNpc : world.npcs()) {
            if (villagerNpc->type != NPCType::Enemy) {
                villagerNpc->perception.forceAwareness(
                    wid, wolf->position, AwarenessLevel::Combat, true,
                    world.time().totalHours());
            }
        }

        world.addNPC(wolf);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  COMBAT ENCOUNTER
// ═══════════════════════════════════════════════════════════════════════

void runCombatEncounter(GameWorld& world, FactionSystem& factions) {
    // Update wolf perception of villagers
    for (auto& npcPtr : world.npcs()) {
        if (npcPtr->type == NPCType::Enemy && npcPtr->combat.stats.isAlive()) {
            // Wolves perceive all villagers
            for (auto& other : world.npcs()) {
                if (other->type != NPCType::Enemy && other->combat.stats.isAlive()) {
                    npcPtr->perception.forceAwareness(
                        other->id, other->position, AwarenessLevel::Combat, true,
                        world.time().totalHours());
                }
            }
        }
        // Villagers perceive alive wolves
        if (npcPtr->type != NPCType::Enemy) {
            for (auto& wolf : world.npcs()) {
                if (wolf->type == NPCType::Enemy && wolf->combat.stats.isAlive()) {
                    npcPtr->perception.forceAwareness(
                        wolf->id, wolf->position, AwarenessLevel::Combat, true,
                        world.time().totalHours());
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  TRADE DEMO
// ═══════════════════════════════════════════════════════════════════════

void runTradeDemo(GameWorld& world) {
    std::cout << "\n--- Trade Demo: Elmund buys tools from Cedric ---\n";

    auto* cedric = world.findNPC("Cedric");
    auto* elmund = world.findNPC("Elmund");
    if (!cedric || !elmund) return;

    float price = cedric->trade.getPrice(ITEM_TOOLS, true);
    std::cout << "  Cedric's price for Farming Tools: " << static_cast<int>(price) << " gold\n";
    std::cout << "  Elmund's gold: " << static_cast<int>(elmund->trade.inventory.gold()) << "\n";

    auto result = cedric->trade.sell(ITEM_TOOLS, 1, elmund->trade.inventory);
    std::cout << "  Trade result: " << result.message << "\n";

    if (result.success) {
        cedric->memory.addMemory(MemoryType::Trade,
            "Sold farming tools to Elmund", 0.3f, elmund->id, 0.5f,
            world.time().totalHours());
        elmund->memory.addMemory(MemoryType::Trade,
            "Bought farming tools from Cedric", 0.2f, cedric->id, 0.5f,
            world.time().totalHours());

        world.events().publish(TradeEvent{elmund->id, cedric->id, ITEM_TOOLS, 1, result.price});
    }
    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════════════════════
//  DIALOG DEMO
// ═══════════════════════════════════════════════════════════════════════

void runDialogDemo(GameWorld& world) {
    std::cout << "\n--- Dialog Demo: Talking to Dagna at the Tavern ---\n";

    auto* dagna = world.findNPC("Dagna");
    if (!dagna) return;

    float reputation = 30.0f; // neutral-positive
    float mood = dagna->emotions.getMood();

    if (dagna->dialog.startDialog("greeting", reputation)) {
        dagna->dialog.printCurrent(dagna->name, reputation, mood);

        // Auto-select "gossip" option (index 1)
        std::cout << "  [Player chooses: Any news from the village?]\n";
        dagna->dialog.selectOption(1);

        if (dagna->dialog.isInDialog()) {
            dagna->dialog.printCurrent(dagna->name, reputation, mood);
            std::cout << "  [Player chooses: I'll keep my eyes open.]\n";
            dagna->dialog.selectOption(0);
        }

        dagna->memory.addMemory(MemoryType::Interaction,
            "Had a conversation about wolves", 0.1f, std::nullopt, 0.4f,
            world.time().totalHours());
    }
    std::cout << "\n";
}
