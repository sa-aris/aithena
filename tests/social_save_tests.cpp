#include "test_runner.hpp"
#include "npc/serialization/social_serializer.hpp"

using namespace npc;

static void setup(SocialContractSystem& s) {
    for (EntityId i = 1; i <= 3; ++i) {
        SocialActor a; a.id = i; a.name = std::to_string(i);
        a.personality.sociability = 1; ASSERT_TRUE(s.upsertActor(a));
    }
    SocialContract c; c.id = UINT64_MAX; c.actor = 1; c.beneficiary = 2;
    c.topic = "deliver the harvest"; c.deadline = 4; c.obligation = 0.8;
    ASSERT_TRUE(s.open(c)); ASSERT_TRUE(s.inform(1, c.id));
}

static serial::JsonValue textRoundtrip(const SocialContractSystem& s) {
    return serial::parse(serial::toString(SocialSerializer::toJson(s)));
}

TEST("Social save: identifiers, seed and decisions survive a text roundtrip") {
    SocialContractSystem a(UINT64_MAX), b; setup(a);
    auto expected = a.decide(UINT64_MAX).selected->id;
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_TRUE(b.seed() == UINT64_MAX);
    ASSERT_TRUE(b.contract(UINT64_MAX));
    ASSERT_EQ(b.decide(UINT64_MAX).selected->id, expected);
    ASSERT_TRUE(b.drainEvents().empty());
}

TEST("Social save: pending intentions keep their resource reservations") {
    SocialContractSystem a, b; setup(a);
    a.setProposer([](const auto&) { return std::vector<ActionProposal>{{1, "work", 1}}; });
    ASSERT_TRUE(a.commit(a.decide(UINT64_MAX)));
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_NEAR(b.reservedEffort(1), 0.25, 1e-6);
    ASSERT_TRUE(b.complete(UINT64_MAX));
    ASSERT_NEAR(b.actor(1)->effortBudget, 0.75, 1e-6);
}

TEST("Social save: corrections after reload undo the right saturated contribution") {
    SocialContractSystem a, b; setup(a);
    a.reputation().setReputation("1", -99); a.relationships().setValue("2", "1", -99);
    a.advanceTo(4); a.observeOutcome(3, UINT64_MAX);
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_TRUE(b.excuse(UINT64_MAX, "verified injury"));
    ASSERT_TRUE(b.observeOutcome(3, UINT64_MAX));
    ASSERT_NEAR(b.reputation().reputationOf("1"), -99, 1e-5);
    ASSERT_NEAR(b.relationships().getValue("2", "1"), -99, 1e-5);
}

TEST("Social save: rumor epochs and evidence paths survive reload") {
    SocialContractSystem a, b; setup(a);
    auto cfg = a.config(); cfg.gossipChance = 1; a.configure(cfg);
    a.advanceTo(4); a.spreadRumors(4);
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_FALSE(b.spreadRumors(4));
    ASSERT_TRUE(b.belief(3, UINT64_MAX));
    ASSERT_FALSE(b.belief(3, UINT64_MAX)->firstHand);
    ASSERT_EQ(b.belief(3, UINT64_MAX)->path.size(), a.belief(3, UINT64_MAX)->path.size());
}

TEST("Social save: expired evidence keeps its idempotence ledger") {
    SocialContractSystem a, b; setup(a); a.advanceTo(4); a.advanceTo(77);
    const auto reputation = a.reputation().reputationOf("1");
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_FALSE(b.belief(2, UINT64_MAX));
    ASSERT_FALSE(b.observeOutcome(2, UINT64_MAX));
    ASSERT_NEAR(b.reputation().reputationOf("1"), reputation, 1e-6);
    ASSERT_TRUE(b.retire(UINT64_MAX));
    SocialContractSystem c;
    ASSERT_TRUE(SocialSerializer::fromJson(c, textRoundtrip(b)));
}

TEST("Social save: invalid format is atomic and invalidates no existing state") {
    SocialContractSystem s; setup(s);
    const auto before = serial::toString(SocialSerializer::toJson(s));
    auto bad = SocialSerializer::toJson(s); bad["version"] = 99;
    std::string error;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad, &error));
    ASSERT_FALSE(error.empty());
    ASSERT_EQ(serial::toString(SocialSerializer::toJson(s)), before);
}

TEST("Social save: duplicate identities and cyclic evidence are rejected") {
    SocialContractSystem s; setup(s);
    auto bad = SocialSerializer::toJson(s);
    auto actors = bad["actors"].asArray(); actors.push_back(actors[0]); bad["actors"] = actors;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s);
    bad["knowledge"][0]["path"] = serial::JsonArray{1, 1};
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
}

TEST("Social save: missing resource and opinion evidence cannot load") {
    SocialContractSystem s; setup(s); s.advanceTo(4);
    auto bad = SocialSerializer::toJson(s);
    bad["relation_accounts"] = serial::JsonArray{};
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s); bad["actors"][0]["budget"] = -1;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s); bad["contracts"][0]["phase"] = 999;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
}

TEST("Social save: host hooks remain installed and old decisions are stale") {
    SocialContractSystem s; setup(s);
    s.setProposer([](const auto&) { return std::vector<ActionProposal>{{3, "decline", 1}}; });
    auto stale = s.decide(UINT64_MAX);
    auto data = textRoundtrip(s);
    ASSERT_TRUE(SocialSerializer::fromJson(s, data));
    ASSERT_FALSE(s.commit(stale));
    ASSERT_EQ(s.decide(UINT64_MAX).selected->id, 3u);
}

TEST("Social save: cancellation followed by later awareness still roundtrips") {
    SocialContractSystem s;
    SocialActor a; a.id = 1; a.name = "a"; s.upsertActor(a);
    a.id = 2; a.name = "b"; s.upsertActor(a);
    SocialContract c; c.id = 1; c.actor = 1; c.beneficiary = 2; c.topic = "appointment"; c.deadline = 4;
    s.open(c); s.cancel(1, "venue closed"); s.inform(1, 1);
    SocialContractSystem restored;
    ASSERT_TRUE(SocialSerializer::fromJson(restored, textRoundtrip(s)));
}

TEST("Core JSON: adjacent doubles do not lose clock precision") {
    const auto value = std::nextafter(1.0, 2.0);
    ASSERT_TRUE(serial::parse(serial::toString(serial::JsonValue(value))).asDouble() == value);
}

TEST("Social save: partially completed sustained work resumes after reload") {
    SocialContractSystem a, b;
    for (EntityId id : {1u, 2u}) { SocialActor actor; actor.id = id; actor.name = std::to_string(id); a.upsertActor(actor); }
    SocialContract c; c.id = 1; c.actor = 1; c.beneficiary = 2; c.topic = "repair";
    c.deadline = 4; c.workDuration = 1;
    a.open(c); a.inform(1, 1);
    a.setProposer([](const auto&) { return std::vector<ActionProposal>{{1, "work", 1}}; });
    a.commit(a.decide(1)); ASSERT_FALSE(a.complete(1)); a.advanceTo(0.5);
    ASSERT_TRUE(SocialSerializer::fromJson(b, textRoundtrip(a)));
    ASSERT_FALSE(b.complete(1)); b.advanceTo(1.1); ASSERT_TRUE(b.complete(1));
}

TEST("Core JSON: mutable array indices are checked") {
    auto value = serial::parse("[1]"); value[0] = 2;
    ASSERT_EQ(value[0].asInt(), 2);
    ASSERT_THROWS(value[-1] = 0);
    ASSERT_THROWS(value[size_t{3}] = 0);
}

TEST("Social save: unmatched or inconsistent opinion totals are rejected") {
    SocialContractSystem s; setup(s); s.advanceTo(4);
    auto bad = SocialSerializer::toJson(s);
    bad["relation_accounts"][0]["total"] = 0;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s);
    bad["reputation_accounts"][0]["last"] = 99;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
}

TEST("Social save: malformed social graphs cannot introduce invalid floats") {
    SocialContractSystem s; setup(s); s.advanceTo(4);
    const auto before = serial::toString(SocialSerializer::toJson(s));
    auto bad = SocialSerializer::toJson(s);
    bad["relationships"]["pairs"][0]["value"] = 1e100;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s);
    bad["relationships"]["pairs"][0]["trust"] = -1;
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    bad = SocialSerializer::toJson(s); bad["reputation"] = serial::JsonObject{};
    ASSERT_FALSE(SocialSerializer::fromJson(s, bad));
    ASSERT_EQ(serial::toString(SocialSerializer::toJson(s)), before);
}

TEST("Core JSON: incomplete tokens and trailing input are rejected") {
    for (const auto* input : {"", "tru", "falsex", "nul", "true false", "-", "01",
             "1.", "1e", "1e+", "1e309", "9223372036854775808", "[1,]", "{\"x\":1,}",
             "\"unfinished", "\"escape\\", "\"\\x\"", "\"line\nfeed\""})
        ASSERT_THROWS(serial::parse(input));
    ASSERT_THROWS(serial::parse(std::string_view{}));
    ASSERT_TRUE(serial::parse(" \ttrue\r\n").asBool());
    ASSERT_EQ(serial::parse("-9223372036854775808").asInt(), INT64_MIN);
    ASSERT_NEAR(serial::parse("1.25e+2").asDouble(), 125, 1e-9);
}

TEST("Core JSON: Unicode pairs and bounded nesting are validated") {
    const std::string emoji = "\xF0\x9F\x8C\xB8";
    ASSERT_EQ(serial::parse("\"\\uD83C\\uDF38\"").asString(), emoji);
    ASSERT_EQ(serial::parse(serial::toString(emoji)).asString(), emoji);
    for (const auto* input : {"\"\\uD800\"", "\"\\uDC00\"", "\"\\uD800\\u0041\"",
             "\"\\u123\"", "\"\\uZZZZ\"", "\"\xC0\x80\"", "\"\xED\xA0\x80\""})
        ASSERT_THROWS(serial::parse(input));
    const auto nested = std::string(130, '[') + "0" + std::string(130, ']');
    ASSERT_THROWS(serial::parse(nested));
    ASSERT_EQ(serial::parse(serial::toString(std::string("a\0b", 3))).asString(), std::string("a\0b", 3));
}

TEST("Core JSON: decimal roundtrips do not depend on the global C++ locale") {
    struct Comma : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
    struct RestoreLocale {
        std::locale prior = std::locale();
        ~RestoreLocale() { std::locale::global(prior); }
    } restore;
    std::locale::global(std::locale(std::locale::classic(), new Comma));
    ASSERT_EQ(serial::toString(1.25), "1.25");
    ASSERT_NEAR(serial::parse("1.25").asDouble(), 1.25, 1e-12);
    for (double value : {0.1, -0.0, 1.2345678901234567, 1e100})
        ASSERT_EQ(serial::parse(serial::toString(value)).asDouble(), value);
}

int main(int argc, char**) { return npc::test::run_all(argc > 1); }
