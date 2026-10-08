#include "test_runner.hpp"
#include "npc/world/world.hpp"
#include "npc/npc.hpp"
#include "npc/serialization/json.hpp"
#include "npc/world/simulation_manager.hpp"
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

int main(int argc, char**) { return npc::test::run_all(argc > 1); }
