#include "test_runner.hpp"
#include "npc/ai/decision_boundary.hpp"
#include "npc/social/social_contract_system.hpp"
#include "npc/core/random.hpp"
#include <limits>

using namespace npc;

static SocialActor person(EntityId id, float x = 0.0f) {
    SocialActor a;
    a.id = id;
    a.name = "person_" + std::to_string(id);
    a.position = {x, 0};
    a.personality.sociability = 1.0f;
    return a;
}

static SocialContract duty(uint64_t id = 100, EntityId who = 1, EntityId forWhom = 2) {
    SocialContract c;
    c.id = id;
    c.actor = who;
    c.beneficiary = forWhom;
    c.topic = "repair the shared mill";
    c.deadline = 4.0;
    c.obligation = 0.8;
    return c;
}

static void populate(SocialContractSystem& s, size_t count = 3) {
    for (EntityId i = 1; i <= count; ++i) ASSERT_TRUE(s.upsertActor(person(i)));
}

static void chooseOnly(SocialContractSystem& s, SocialAction action) {
    s.setProposer([action](const auto&) {
        return std::vector<ActionProposal>{{static_cast<uint32_t>(action), "chosen", 1.0}};
    });
}

TEST("Boundary: guards win over arbitrary proposal weights") {
    DecisionBoundary b;
    b.require("no_teleport", [](const auto&, const auto& p) { return p.id != 1; });
    auto r = b.evaluate({1, 2, 10, 0, 0}, {{1, "teleport", 1e200}, {2, "walk", 1}}, 42);
    ASSERT_TRUE(r.selected);
    ASSERT_EQ(r.selected->id, 2u);
    ASSERT_EQ(r.options[0].violations[0], "no_teleport");
}

TEST("Boundary: no valid action produces no choice") {
    DecisionBoundary b;
    b.require("blocked", [](const auto&, const auto&) { return false; });
    ASSERT_FALSE(b.evaluate({1, 2, 10, 0, 0}, {{1, "walk", 1}}, 1).selected);
}

TEST("Boundary: invalid weights and duplicate ids fail closed") {
    DecisionBoundary b;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    auto r = b.evaluate({1, 2, 10, 0, 0},
        {{1, "a", 1}, {1, "b", 1}, {2, "c", nan}, {3, "d", -1}, {4, "e", 0}}, 1);
    ASSERT_FALSE(r.selected);
    for (const auto& p : r.options) ASSERT_FALSE(p.allowed());
}

TEST("Boundary: invalid context never reaches external guards") {
    DecisionBoundary b;
    int calls = 0;
    b.require("guard", [&](const auto&, const auto&) { ++calls; return true; });
    ASSERT_FALSE(b.evaluate({0, 2, 10, 0, 0}, {{1, "a", 1}}, 1).selected);
    ASSERT_EQ(calls, 0);
    ASSERT_THROWS(b.require("", {}));
}

TEST("Boundary: seed replay ignores proposal ordering and global random calls") {
    DecisionBoundary b;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto a = b.evaluate({1, 2, 10, 0, 0}, {{1, "a", 1}, {2, "b", 3}, {3, "c", 2}}, seed);
        Random::instance().unit();
        auto c = b.evaluate({1, 2, 10, 999, 2}, {{3, "c", 2}, {1, "a", 1}, {2, "b", 3}}, seed);
        ASSERT_EQ(a.selected->id, c.selected->id);
    }
}

TEST("Boundary: extreme weights remain usable") {
    DecisionBoundary b;
    auto r = b.evaluate({1, 2, 10, 0, 0}, {{1, "a", 1e-300}, {2, "b", 1e300}}, 1);
    ASSERT_TRUE(r.selected);
    ASSERT_EQ(r.selected->id, 2u);
}

TEST("Boundary: weighted choices vary with seed at the expected frequency") {
    DecisionBoundary b;
    int chosen = 0;
    for (uint64_t seed = 0; seed < 10000; ++seed)
        if (b.evaluate({1, 2, 10, 0, 0}, {{1, "a", 1}, {2, "b", 3}}, seed).selected->id == 2)
            ++chosen;
    ASSERT_TRUE(chosen > 7200 && chosen < 7800);
}

TEST("Social: identity, numeric inputs and registry bounds are checked") {
    SocialContractSystem s;
    auto cfg = s.config(); cfg.maxActors = 2;
    ASSERT_TRUE(s.configure(cfg));
    populate(s, 2);
    ASSERT_FALSE(s.upsertActor(person(3)));
    auto a = person(1); a.name = "renamed";
    ASSERT_FALSE(s.upsertActor(a));
    a = person(2); a.name = person(1).name;
    ASSERT_FALSE(s.upsertActor(a));
    a = person(1); a.effortBudget = std::numeric_limits<double>::infinity();
    ASSERT_FALSE(s.upsertActor(a));
    ASSERT_FALSE(s.advanceTo(-1));
    ASSERT_FALSE(s.advanceTo(std::numeric_limits<double>::quiet_NaN()));
}

TEST("Social: duplicate contracts and stale creation times are rejected") {
    SocialContractSystem s; populate(s);
    ASSERT_TRUE(s.open(duty()));
    ASSERT_FALSE(s.open(duty()));
    ASSERT_TRUE(s.advanceTo(1));
    ASSERT_FALSE(s.open(duty(101)));
    auto c = duty(101); c.openedAt = 1;
    ASSERT_TRUE(s.open(c));
}

TEST("Social: no knowledge means no decision and no blame") {
    SocialContractSystem s; populate(s);
    ASSERT_TRUE(s.open(duty()));
    ASSERT_FALSE(s.decide(100).selected);
    s.advanceTo(4);
    ASSERT_TRUE(s.contract(100)->phase == ContractPhase::Missed);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), 0.0, 1e-6);
    ASSERT_FALSE(s.belief(3, 100));
}

TEST("Social: late information cannot create an actionable duty") {
    SocialContractSystem s; populate(s); s.open(duty());
    s.advanceTo(3.9); s.inform(1, 100);
    ASSERT_FALSE(s.decide(100).selected);
    s.advanceTo(4);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), 0.0, 1e-6);
}

TEST("Social: personal red lines reject even a forced contact proposal") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    s.relationships().setValue(person(1).name, person(2).name, -90);
    chooseOnly(s, SocialAction::Fulfill);
    auto r = s.decide(100);
    ASSERT_FALSE(r.selected);
    ASSERT_TRUE(std::find(r.options[0].violations.begin(), r.options[0].violations.end(),
                         "personal_contact_boundary") != r.options[0].violations.end());
}

TEST("Social: insufficient resources and unavailable actors cannot fulfill") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill);
    auto a = person(1); a.effortBudget = 0.1; s.upsertActor(a);
    ASSERT_FALSE(s.decide(100).selected);
    a.effortBudget = 1; a.available = false; s.upsertActor(a);
    ASSERT_FALSE(s.decide(100).selected);
}

TEST("Social: reachability and travel time are hard boundaries") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill);
    s.setReachabilityRule([](const auto&, const auto&) { return false; });
    ASSERT_FALSE(s.decide(100).selected);
    s.setReachabilityRule({});
    s.upsertActor(person(1, 100));
    ASSERT_FALSE(s.decide(100).selected);
}

TEST("Social: committing a stale or forged choice has no effect") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill);
    auto stale = s.decide(100); s.inform(3, 100);
    ASSERT_FALSE(s.commit(stale));
    auto forged = s.decide(100); forged.selected->id = 3;
    ASSERT_FALSE(s.commit(forged));
    ASSERT_TRUE(s.contract(100)->phase == ContractPhase::Open);
}

TEST("Social: external relation changes are rechecked before committing") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill);
    auto stale = s.decide(100);
    s.relationships().setValue(person(1).name, person(2).name, -90);
    ASSERT_FALSE(s.commit(stale));
}

TEST("Social: intention reserves effort without claiming completion") {
    SocialContractSystem s; populate(s); s.upsertActor(person(1, 6));
    s.open(duty()); s.inform(1, 100); chooseOnly(s, SocialAction::Fulfill);
    ASSERT_TRUE(s.commit(s.decide(100)));
    ASSERT_TRUE(s.contract(100)->phase == ContractPhase::Travelling);
    ASSERT_NEAR(s.reservedEffort(1), 0.25, 1e-6);
    ASSERT_FALSE(s.complete(100));
    ASSERT_FALSE(s.belief(2, 100));
    s.upsertActor(person(1));
    ASSERT_TRUE(s.complete(100));
    ASSERT_NEAR(s.reservedEffort(1), 0.0, 1e-6);
    ASSERT_NEAR(s.actor(1)->effortBudget, 0.75, 1e-6);
    ASSERT_FALSE(s.complete(100));
    ASSERT_FALSE(s.commit(s.decide(100)));
}

TEST("Social: competing intentions cannot reserve the same effort twice") {
    SocialContractSystem s; populate(s); chooseOnly(s, SocialAction::Fulfill);
    auto a = duty(); a.effort = 0.75;
    auto b = duty(101, 1, 3); b.effort = 0.75;
    s.open(a); s.open(b); s.inform(1, 100); s.inform(1, 101);
    ASSERT_TRUE(s.commit(s.decide(100)));
    ASSERT_FALSE(s.decide(101).selected);
    ASSERT_TRUE(s.cancel(100, "mill repair deferred"));
    ASSERT_TRUE(s.decide(101).selected);
}

TEST("Social: shrinking a budget invalidates completion of reserved work") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill); s.commit(s.decide(100));
    auto a = person(1); a.effortBudget = 0.0; s.upsertActor(a);
    ASSERT_FALSE(s.complete(100));
}

TEST("Social: private refusal remains private until the deadline") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Decline);
    ASSERT_TRUE(s.commit(s.decide(100)));
    ASSERT_FALSE(s.belief(2, 100));
    ASSERT_FALSE(s.observeOutcome(3, 100));
    s.advanceTo(3.99);
    ASSERT_FALSE(s.belief(2, 100));
    s.advanceTo(4);
    ASSERT_TRUE(s.belief(2, 100));
    ASSERT_LT(s.belief(2, 100)->opinion, 0.0);
    ASSERT_FALSE(s.belief(3, 100));
}

TEST("Social: missed deadlines resolve once and release reservations") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill); s.commit(s.decide(100));
    s.advanceTo(4);
    auto reputation = s.reputation().reputationOf(person(1).name);
    s.advanceTo(5);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), reputation, 1e-6);
    ASSERT_NEAR(s.reservedEffort(1), 0.0, 1e-6);
    ASSERT_FALSE(s.complete(100));
}

TEST("Social: remote support consumes effort and earns partial credit") {
    SocialContractSystem s; populate(s); s.upsertActor(person(1, 100));
    s.open(duty()); s.inform(1, 100); chooseOnly(s, SocialAction::SendSupport);
    ASSERT_TRUE(s.commit(s.decide(100)));
    ASSERT_TRUE(s.contract(100)->phase == ContractPhase::Supported);
    ASSERT_NEAR(s.actor(1)->effortBudget, 1.0 - 0.25 * 0.35, 1e-6);
    ASSERT_NEAR(s.belief(2, 100)->opinion, 0.8 * 0.35 * 4.0, 1e-6);
}

TEST("Social: remote support can be disabled by a contract") {
    SocialContractSystem s; populate(s); auto c = duty(); c.allowRemoteSupport = false;
    s.open(c); s.inform(1, 100); chooseOnly(s, SocialAction::SendSupport);
    ASSERT_FALSE(s.decide(100).selected);
}

TEST("Social: death or despawn cancels duties without blame") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    ASSERT_TRUE(s.removeActor(1));
    ASSERT_TRUE(s.contract(100)->phase == ContractPhase::Cancelled);
    ASSERT_FALSE(s.decide(100).selected);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), 0.0, 1e-6);
}

TEST("Social: outcomes require local contact and respect occlusion") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    s.upsertActor(person(3, 100));
    ASSERT_FALSE(s.observeOutcome(3, 100));
    s.upsertActor(person(3));
    s.setContactRule([](const auto&, const auto&) { return false; });
    ASSERT_FALSE(s.observeOutcome(3, 100));
    s.setContactRule({});
    ASSERT_TRUE(s.observeOutcome(3, 100));
    ASSERT_TRUE(s.belief(3, 100)->firstHand);
}

TEST("Social: gossip uses staged waves, confidence decay and no cycles") {
    SocialContractSystem s; populate(s);
    s.upsertActor(person(1, 100)); s.upsertActor(person(2, 0)); s.upsertActor(person(3, 7));
    s.upsertActor(person(4, 14));
    auto cfg = s.config(); cfg.gossipChance = 1; s.configure(cfg);
    s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    ASSERT_TRUE(s.spreadRumors(1));
    ASSERT_TRUE(s.belief(3, 100));
    ASSERT_FALSE(s.belief(4, 100));
    ASSERT_FALSE(s.belief(3, 100)->firstHand);
    ASSERT_LT(s.belief(3, 100)->confidence, 1.0);
    ASSERT_FALSE(s.spreadRumors(1));
    ASSERT_TRUE(s.spreadRumors(2));
    ASSERT_TRUE(s.belief(4, 100));
    ASSERT_LT(s.belief(4, 100)->confidence, s.belief(3, 100)->confidence);
    auto reputation = s.reputation().reputationOf(person(1).name);
    s.spreadRumors(3); s.spreadRumors(4);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), reputation, 1e-6);
    ASSERT_TRUE(s.belief(2, 100)->firstHand);
}

TEST("Social: new evidence corrects opinion once without informing everyone") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    s.observeOutcome(3, 100);
    ASSERT_TRUE(s.excuse(100, "bridge closure confirmed by the guard"));
    ASSERT_NEAR(s.belief(2, 100)->opinion, 0.0, 1e-6);
    ASSERT_TRUE(s.belief(3, 100)->outcome == ContractPhase::Missed);
    ASSERT_TRUE(s.observeOutcome(3, 100));
    ASSERT_TRUE(s.belief(3, 100)->outcome == ContractPhase::Excused);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), 0.0, 1e-6);
    ASSERT_FALSE(s.excuse(100, "repeat"));
    ASSERT_FALSE(s.observeOutcome(3, 100));
}

TEST("Social: expired belief cannot be reobserved to farm reputation") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    auto value = s.reputation().reputationOf(person(1).name);
    s.advanceTo(77);
    ASSERT_FALSE(s.belief(2, 100));
    ASSERT_FALSE(s.observeOutcome(2, 100));
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), value, 1e-6);
    ASSERT_TRUE(s.retire(100));
    ASSERT_FALSE(s.contract(100));
    ASSERT_FALSE(s.knowledge(1, 100));
}

TEST("Social: knowledge and audit capacities stay bounded") {
    SocialContractSystem s; populate(s);
    auto cfg = s.config(); cfg.maxKnowledgePerActor = 1; cfg.maxEvents = 2; s.configure(cfg);
    ASSERT_TRUE(s.open(duty()));
    ASSERT_FALSE(s.open(duty(101)));
    s.inform(1, 100); s.inform(3, 100); s.advanceTo(4);
    ASSERT_TRUE(s.drainEvents().size() <= 2);
}

TEST("Social: configuration cannot invalidate retained evidence or notice history") {
    SocialContractSystem s; populate(s);
    auto cfg = s.config(); cfg.gossipChance = 1; ASSERT_TRUE(s.configure(cfg));
    s.open(duty()); s.inform(1, 100); s.advanceTo(4); s.spreadRumors(1);
    cfg.minNotice += 1;
    ASSERT_FALSE(s.configure(cfg));
    cfg = s.config(); cfg.minConfidence = 0.99;
    ASSERT_FALSE(s.configure(cfg));
    ASSERT_NEAR(s.config().minConfidence, 0.1, 1e-6);
}

TEST("Social: a conversation respects its finite topic budget") {
    SocialContractSystem s; populate(s);
    s.upsertActor(person(1, 100));
    auto cfg = s.config(); cfg.gossipChance = 1; cfg.maxTopicsPerContact = 1;
    ASSERT_TRUE(s.configure(cfg));
    for (uint64_t id : {100u, 101u, 102u}) {
        ASSERT_TRUE(s.open(duty(id))); ASSERT_TRUE(s.inform(1, id));
    }
    s.advanceTo(4); ASSERT_TRUE(s.spreadRumors(1));
    int heard = 0;
    for (uint64_t id : {100u, 101u, 102u}) if (s.belief(3, id)) ++heard;
    ASSERT_EQ(heard, 1);
}

TEST("Social: actor insertion order and unrelated contracts preserve decisions") {
    SocialContractSystem a(42), b(42);
    populate(a);
    for (EntityId i = 3; i >= 1; --i) b.upsertActor(person(i));
    a.open(duty()); b.open(duty()); a.inform(1, 100); b.inform(1, 100);
    b.open(duty(101, 3, 2)); b.inform(3, 101);
    ASSERT_EQ(a.decide(100).selected->id, b.decide(100).selected->id);
}

TEST("Social: evidence corrections restore a saturated reputation correctly") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    s.reputation().setReputation(person(1).name, -99);
    s.relationships().setValue(person(2).name, person(1).name, -99);
    s.advanceTo(4);
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), -100, 1e-6);
    s.excuse(100, "verified emergency");
    ASSERT_NEAR(s.reputation().reputationOf(person(1).name), -99, 1e-6);
    ASSERT_NEAR(s.relationships().getValue(person(2).name, person(1).name), -99, 1e-6);
}

TEST("Social: reconsideration releases a reservation and cannot reroll unchanged inputs") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill);
    auto original = s.decide(100);
    ASSERT_TRUE(s.commit(original));
    ASSERT_TRUE(s.reconsider(100));
    ASSERT_NEAR(s.reservedEffort(1), 0.0, 1e-6);
    ASSERT_EQ(s.decide(100).selected->id, original.selected->id);
    s.advanceTo(4);
    ASSERT_FALSE(s.reconsider(100));
}

TEST("Social: an informed actor knows their own outcome without a self opinion") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    ASSERT_TRUE(s.belief(1, 100));
    ASSERT_NEAR(s.belief(1, 100)->opinion, 0.0, 1e-6);
    ASSERT_NEAR(s.relationships().getValue(person(1).name, person(1).name), 0.0, 1e-6);
}

TEST("Social: equally informed observers apply their own norms and sympathies") {
    SocialContractSystem s; populate(s, 4);
    auto lenient = person(3); lenient.responsibility = 0.1; s.upsertActor(lenient);
    auto strict = person(4); strict.responsibility = 0.9; s.upsertActor(strict);
    s.relationships().setValue(strict.name, person(2).name, 70);
    s.open(duty()); s.inform(1, 100); s.advanceTo(4);
    ASSERT_TRUE(s.observeOutcome(3, 100)); ASSERT_TRUE(s.observeOutcome(4, 100));
    ASSERT_NEAR(s.belief(3, 100)->confidence, s.belief(4, 100)->confidence, 1e-9);
    ASSERT_LT(s.belief(4, 100)->opinion, s.belief(3, 100)->opinion);
}

TEST("Social: sustained work takes time and interruptions reset progress") {
    SocialContractSystem s; populate(s); auto c = duty(); c.workDuration = 1.0;
    s.open(c); s.inform(1, 100); chooseOnly(s, SocialAction::Fulfill); s.commit(s.decide(100));
    ASSERT_FALSE(s.complete(100));
    s.advanceTo(0.5);
    ASSERT_FALSE(s.complete(100));
    s.upsertActor(person(1, 10)); s.advanceTo(0.6);
    ASSERT_NEAR(s.contract(100)->workStartedAt, -1.0, 1e-6);
    s.upsertActor(person(1)); s.advanceTo(0.7);
    ASSERT_FALSE(s.complete(100));
    s.advanceTo(1.5); ASSERT_FALSE(s.complete(100));
    s.advanceTo(1.8); ASSERT_TRUE(s.complete(100));
}

TEST("Social: game prerequisites are rechecked before applying an effect") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill); s.commit(s.decide(100));
    bool hasTools = false;
    s.setFulfillmentRule([&](const auto&, const auto&, const auto&) { return hasTools; });
    ASSERT_FALSE(s.complete(100));
    ASSERT_NEAR(s.actor(1)->effortBudget, 1.0, 1e-6);
    hasTools = true;
    ASSERT_TRUE(s.complete(100));
}

TEST("Social: one actor cannot perform two sustained jobs simultaneously") {
    SocialContractSystem s; populate(s); chooseOnly(s, SocialAction::Fulfill);
    auto a = duty(); a.workDuration = 1;
    auto b = duty(101, 1, 3); b.workDuration = 1;
    s.open(a); s.open(b); s.inform(1, 100); s.inform(1, 101);
    ASSERT_TRUE(s.commit(s.decide(100))); ASSERT_TRUE(s.commit(s.decide(101)));
    ASSERT_FALSE(s.complete(100)); ASSERT_FALSE(s.complete(101));
    ASSERT_NEAR(s.contract(101)->workStartedAt, -1.0, 1e-9);
    s.advanceTo(1.1); ASSERT_TRUE(s.complete(100)); ASSERT_FALSE(s.complete(101));
    s.advanceTo(2.2); ASSERT_TRUE(s.complete(101));
}

TEST("Social: personal boundaries still apply when the action reaches its target") {
    SocialContractSystem s; populate(s); s.open(duty()); s.inform(1, 100);
    chooseOnly(s, SocialAction::Fulfill); ASSERT_TRUE(s.commit(s.decide(100)));
    s.relationships().setValue(person(1).name, person(2).name, -90);
    ASSERT_FALSE(s.complete(100)); ASSERT_NEAR(s.actor(1)->effortBudget, 1.0, 1e-9);
}

int main(int argc, char**) { return npc::test::run_all(argc > 1); }
