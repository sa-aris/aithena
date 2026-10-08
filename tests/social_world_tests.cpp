#include "test_runner.hpp"
#include "npc/world/world.hpp"
#include "npc/npc.hpp"
#include "npc/serialization/json.hpp"
#include "npc/world/simulation_manager.hpp"
#include "npc/navigation/steering.hpp"
#ifdef NPC_TEST_THREADS
#include "npc/threading/thread_safety.hpp"
#endif
#include <limits>

using namespace npc;

static std::shared_ptr<NPC> resident(EntityId id, Vec2 position) {
    auto n = std::make_shared<NPC>(id, "resident_" + std::to_string(id), NPCType::Villager);
    n->position = position;
    n->verbose = false;
    return n;
}

static SocialContract request(GameWorld& w, uint64_t id = 10) {
    SocialContract c;
    c.id = id; c.actor = 1; c.beneficiary = 2; c.topic = "deliver tools";
    c.openedAt = w.social().time(); c.deadline = c.openedAt + 2;
    c.allowRemoteSupport = false;
    return c;
}

static void fulfillOnly(GameWorld& w) {
    w.social().setProposer([](const auto&) { return std::vector<ActionProposal>{{1, "fulfill", 1}}; });
}

TEST("World: social simulation requires explicit activation") {
    GameWorld w(20, 20);
    w.addNPC(resident(1, {1, 1})); w.addNPC(resident(2, {5, 1}));
    w.update(0.1f);
    ASSERT_TRUE(w.social().actors().empty());
    w.enableSocialSimulation();
    ASSERT_EQ(w.social().actors().size(), 2u);
    ASSERT_NEAR(w.social().time(), w.time().totalHours(), 1e-6);
}

TEST("World: intentions become physical movement and completed work") {
    GameWorld w(20, 20);
    auto a = resident(1, {1, 1}), b = resident(2, {5, 1});
    w.addNPC(a); w.addNPC(b); w.enableSocialSimulation(); fulfillOnly(w);
    ASSERT_TRUE(w.social().open(request(w)));
    ASSERT_TRUE(w.social().inform(1, 10));
    int completed = 0;
    w.events().subscribe<SocialEvent>([&](const auto& e) {
        if (e.kind == SocialEventKind::Resolved && e.phase == ContractPhase::Fulfilled) ++completed;
    });
    w.update(0.1f);
    ASSERT_TRUE(a->isMoving);
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Travelling);
    for (int i = 0; i < 15; ++i) w.update(0.1f);
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Fulfilled);
    ASSERT_EQ(completed, 1);
    ASSERT_LT(a->combat.stats.stamina.current, 100.0f);
    ASSERT_FALSE(b->memory.allMemories().empty());
}

TEST("World: walls block plans and social contact") {
    GameWorld w(10, 10);
    w.addNPC(resident(1, {1, 1})); w.addNPC(resident(2, {3, 1}));
    w.setCell(2, 1, CellType::Wall, 1, false);
    w.enableSocialSimulation(); fulfillOnly(w);
    w.social().open(request(w)); w.social().inform(1, 10);
    ASSERT_FALSE(w.social().decide(10).selected);
}

TEST("World: urgent needs prevent work until they are satisfied") {
    GameWorld w(10, 10);
    auto a = resident(1, {1, 1});
    w.addNPC(a); w.addNPC(resident(2, {2, 1}));
    a->emotions.depletNeed(NeedType::Hunger, 100);
    w.enableSocialSimulation(); fulfillOnly(w);
    w.social().open(request(w)); w.social().inform(1, 10);
    w.updateSocial();
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Open);
    ASSERT_FALSE(a->isMoving);
    a->emotions.satisfyNeed(NeedType::Hunger, 100);
    w.updateSocial();
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Fulfilled);
}

TEST("World: an external support commit cannot replenish spent stamina") {
    GameWorld w(10, 10);
    auto a = resident(1, {1, 1});
    w.addNPC(a); w.addNPC(resident(2, {2, 1})); w.enableSocialSimulation();
    auto c = request(w); c.allowRemoteSupport = true;
    w.social().open(c); w.social().inform(1, 10);
    w.social().setProposer([](const auto&) { return std::vector<ActionProposal>{{2, "support", 1}}; });
    ASSERT_TRUE(w.social().commit(w.social().decide(10)));
    w.updateSocial();
    ASSERT_NEAR(a->combat.stats.stamina.current, 91.25, 1e-4);
    w.updateSocial();
    ASSERT_NEAR(a->combat.stats.stamina.current, 91.25, 1e-4);
}

TEST("World: despawn cancels live obligations") {
    GameWorld w(10, 10);
    w.addNPC(resident(1, {1, 1})); w.addNPC(resident(2, {3, 1}));
    w.enableSocialSimulation(); w.social().open(request(w)); w.social().inform(1, 10);
    w.npcs().erase(w.npcs().begin());
    w.updateSocial();
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Cancelled);
}

TEST("World: invalid elapsed time does not move the clock") {
    GameWorld w(10, 10);
    const auto before = w.time().totalHours();
    w.update(-1); w.update(std::numeric_limits<float>::quiet_NaN());
    ASSERT_NEAR(w.time().totalHours(), before, 1e-6);
}

TEST("Core: JSON nonfinite values have valid output and large finite values roundtrip") {
    using namespace npc::serial;
    ASSERT_EQ(toString(JsonValue(std::numeric_limits<double>::quiet_NaN())), "null");
    ASSERT_EQ(toString(JsonValue(std::numeric_limits<double>::infinity())), "null");
    const double large = 1e100;
    ASSERT_NEAR(parse(toString(JsonValue(large))).asDouble() / large, 1.0, 1e-12);
}

TEST("Core: spatial cells on either side of zero remain distinct") {
    SpatialIndex index(1);
    index.update(1, {-1, -1}); index.update(2, {1, 1}); index.update(3, {-1, 1});
    auto ids = index.nearby({-1, -1}, 0.1f);
    ASSERT_EQ(ids.size(), 1u); ASSERT_EQ(ids[0], 1u);
    index.update(1, {1, -1});
    ASSERT_TRUE(index.nearby({-1, -1}, 0.1f).empty());
    ASSERT_EQ(index.nearby({1, -1}, 0.1f).size(), 1u);
}

TEST("SimulationManager: LOD updates also advance social obligations") {
    GameWorld w(10, 10); SimulationManager sim(w);
    auto a = resident(1, {1, 1}); sim.spawnNPC(a); sim.spawnNPC(resident(2, {3, 1}));
    sim.spawnNPC(a);
    ASSERT_EQ(w.npcs().size(), 2u);
    w.enableSocialSimulation(); fulfillOnly(w);
    w.social().open(request(w)); w.social().inform(1, 10);
    sim.update(0.1f);
    ASSERT_TRUE(w.social().contract(10)->phase == ContractPhase::Fulfilled);
    sim.lodConfig().backgroundInterval = 0;
    sim.lodConfig().dormantInterval = 0;
    sim.setPlayerPosition({10000, 10000});
    ASSERT_NO_THROW(sim.update(0.1f));
}

TEST("SimulationManager: despawn cleans up skill event callbacks") {
    GameWorld w(10, 10); SimulationManager sim(w);
    auto a = resident(1, {1, 1}); sim.spawnNPC(a);
    w.events().publish(TradeEvent{1, 2, 1, 1, 1});
    const auto before = a->skills.summary();
    sim.despawnNPC(1);
    w.events().publish(TradeEvent{1, 2, 1, 1, 1});
    ASSERT_EQ(a->skills.summary(), before);
    a.reset();
    ASSERT_NO_THROW(w.events().publish(TradeEvent{1, 2, 1, 1, 1}));
}

TEST("NPC: scoped retirement detaches core and skill callbacks") {
    GameWorld w(10, 10);
    auto a = resident(1, {1, 1});
    SubscriptionGroup lifetime;
    a->subscribeToEvents(w.events(), &lifetime);
    w.events().publish(TradeEvent{1, 2, 1, 1, 1});
    const auto before = a->skills.summary();
    ASSERT_FALSE(before.empty());
    lifetime.releaseAll();
    w.events().publish(TradeEvent{1, 2, 1, 1, 1});
    ASSERT_EQ(a->skills.summary(), before);
    a.reset();
    ASSERT_NO_THROW(w.events().publish(WorldEvent{"meeting", "Repair meeting", {1, 1}, 0.2f}));
    ASSERT_NO_THROW(w.events().publish(TradeEvent{1, 2, 1, 1, 1}));
}

TEST("Steering: crowded queues brake and overlapping people separate") {
    SteeringSystem steering;
    SteeringAgent a{1, {0, 0}, {2, 0}}, b{2, {0.1f, 0}, {2, 0}};
    const auto brake = steering.followQueue(a, b, 3);
    ASSERT_LT(brake.x, 0);
    ASSERT_NEAR(brake.y, 0, 1e-6f);
    std::vector<Vec2> positions{a.position, b.position};
    const auto corrections = SteeringSystem::resolveOverlaps(positions, {a, b});
    ASSERT_TRUE(positions[0].distanceTo(positions[1]) > 0.1f);
    ASSERT_LT(corrections[0].delta.x, 0);
    ASSERT_TRUE(corrections[1].delta.x > 0);
}

#ifdef NPC_TEST_THREADS
TEST("Parallel ticker: submitted work completes with scheduled fatigue") {
    GameWorld w(10, 10);
    auto a = resident(1, {1, 1});
    a->schedule.addEntry(6, 7, ActivityType::Work, "Workshop");
    w.addNPC(a);
    TaskScheduler scheduler(1);
    DeferredDispatcher dispatcher;
    ParallelNPCTicker ticker(scheduler, w, dispatcher);
    ticker.tickBackground({1}, {{1, 1.0f}});
    ticker.wait();
    ASSERT_TRUE(ticker.allDone());
    ASSERT_NEAR(a->schedule.conditions().fatigue, 0.03f, 1e-6f);
    ticker.tickDormant({1}, {{1, 0.1f}});
    ticker.wait();
    ASSERT_TRUE(ticker.allDone());
}
#endif

TEST("Dialog: rejected options do not run effects or change nodes") {
    DialogSystem dialog;
    DialogTree tree("greeting");
    DialogNode root;
    root.id = "root";
    DialogOption option;
    option.text = "Request access";
    option.nextNodeId = "END";
    option.minReputation = 30;
    option.condition = [](float, float mood) { return mood >= 0; };
    int effects = 0;
    option.effect = [&](bool, StoryFlags&) { ++effects; };
    root.options.push_back(option);
    tree.addNode(root);
    dialog.addTree("greeting", std::move(tree));
    ASSERT_TRUE(dialog.startDialog("greeting"));
    auto& rng = RandomGenerator::instance();
    rng.seed(1);
    StoryFlags flags;
    PersonalityTraits traits;
    float delta = 0;
    ASSERT_FALSE(dialog.selectOption(0, 20, 1, traits, 1, 0, rng, flags, delta));
    ASSERT_FALSE(dialog.selectOption(0, 40, -1, traits, 1, 0, rng, flags, delta));
    ASSERT_EQ(effects, 0);
    ASSERT_EQ(dialog.currentNode()->id, "root");
    ASSERT_TRUE(dialog.selectOption(0, 40, 1, traits, 1, 0, rng, flags, delta));
    ASSERT_EQ(effects, 1);
    ASSERT_FALSE(dialog.isInDialog());
}

TEST("Path cache: zero capacity disables storage safely") {
    PathCache cache(0);
    cache.put({0, 0, 1, 1}, {{0, 0}, {1, 1}});
    ASSERT_EQ(cache.size(), 0u);
    ASSERT_TRUE(cache.get({0, 0, 1, 1}) == nullptr);
}

TEST("Path cache: copies own their LRU links after source removal") {
    PathCache source(2);
    source.put({0, 0, 1, 0}, {{0, 0}, {1, 0}});
    source.put({0, 0, 2, 0}, {{0, 0}, {2, 0}});
    PathCache copy(source);
    PathCache assigned;
    assigned = source;
    source.clear();
    ASSERT_TRUE(copy.get({0, 0, 1, 0}) != nullptr);
    copy.put({0, 0, 3, 0}, {{0, 0}, {3, 0}});
    ASSERT_TRUE(copy.get({0, 0, 2, 0}) == nullptr);
    ASSERT_TRUE(assigned.get({0, 0, 2, 0}) != nullptr);
    assigned.invalidateCell(1, 0);
    ASSERT_EQ(assigned.size(), 1u);
    PathCache moved(std::move(assigned));
    ASSERT_TRUE(moved.get({0, 0, 2, 0}) != nullptr);
}

TEST("Schedule: travel uses the remaining daytime or overnight window") {
    ScheduleEntry day{8, 12, ActivityType::Work, "Workshop"};
    ScheduleEntry night{22, 2, ActivityType::Sleep, "Home"};
    ASSERT_TRUE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 8, day, 1));
    ASSERT_FALSE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 11.5f, day, 1));
    ASSERT_FALSE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 12, day, 1));
    ASSERT_TRUE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 23.5f, night, 1));
    ASSERT_FALSE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 25.9f, night, 1));
    ASSERT_FALSE(ScheduleSystem::canReachInTime({0, 0}, {1, 0}, 8, day, 0));
    ASSERT_TRUE(std::isinf(ScheduleSystem::travelTime({0, 0}, {1, 0}, 0)));
    ASSERT_NEAR(ScheduleSystem::travelTime({1, 0}, {1, 0}, 0), 0, 1e-6f);
    ScheduleSystem schedule;
    schedule.addEntry(day);
    schedule.addEntry(12, 14, ActivityType::Eat, "Cafe");
    auto activity = schedule.resolveWithTravel(11.5f, 11.5f, DayOfWeek::Monday, {0, 0},
        [](const std::string&) { return std::optional<Vec2>{{1, 0}}; }, 1);
    ASSERT_EQ(activity.reason, "travel_skip");
    ASSERT_TRUE(activity.activity == ActivityType::Eat);
}

TEST("Group: one surviving flanker receives a finite position") {
    GroupBehavior group;
    group.addMember(1);
    group.addMember(2);
    group.onAllyKilled(2);
    auto positions = group.computeFlankPositions({10, 10}, {1, 0});
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_EQ(positions.front().first, 1u);
    ASSERT_TRUE(std::isfinite(positions.front().second.x));
    ASSERT_TRUE(std::isfinite(positions.front().second.y));
}

TEST("Group: encirclement rotates with the approach direction") {
    GroupBehavior group;
    group.addMember(1);
    group.addMember(2);
    auto positions = group.computeEncirclementPositions({10, 10}, {0, 1});
    ASSERT_EQ(positions.size(), 2u);
    ASSERT_NEAR(positions.front().second.x, 10, 1e-5f);
    ASSERT_NEAR(positions.front().second.y, 14, 1e-5f);
    ASSERT_NEAR(positions.back().second.y, 6, 1e-5f);
}

TEST("Group: rally recovery depends on elapsed time rather than tick count") {
    GroupBehavior coarse;
    coarse.addMember(1);
    coarse.setLeader(1);
    for (int i = 0; i < 8; ++i) coarse.onFlankAttacked();
    ASSERT_TRUE(coarse.isRouting());
    coarse.rally();
    ASSERT_TRUE(coarse.tacticalState() == TacticalState::Rallying);
    auto fine = coarse;
    auto position = [](EntityId) { return Vec2{}; };
    coarse.update(1, position);
    for (int i = 0; i < 10; ++i) fine.update(0.1f, position);
    ASSERT_NEAR(coarse.morale().value, fine.morale().value, 1e-4f);
    ASSERT_NEAR(coarse.morale().value, 40.5f, 1e-4f);
    fine.update(0, position);
    fine.update(-1, position);
    ASSERT_NEAR(coarse.morale().value, fine.morale().value, 1e-4f);
}

#ifdef NPC_TEST_THREADS
TEST("Scheduler: an unknown processor count selects one worker") {
    ASSERT_EQ(TaskScheduler::defaultWorkerCount(0), 1u);
    ASSERT_EQ(TaskScheduler::defaultWorkerCount(1), 1u);
    ASSERT_EQ(TaskScheduler::defaultWorkerCount(8), 7u);
}

TEST("Scheduler: shutdown drains accepted tasks and rejects new work") {
    TaskScheduler scheduler(1);
    auto result = scheduler.submitAsync([] { return 42; });
    scheduler.shutdown();
    ASSERT_EQ(result.get(), 42);
    bool rejected = false;
    try { scheduler.submitAsync([] { return 7; }); }
    catch (const std::logic_error&) { rejected = true; }
    ASSERT_TRUE(rejected);
    ASSERT_EQ(scheduler.pending(), 0u);
}
#endif

int main(int argc, char**) { return npc::test::run_all(argc > 1); }
