#include "test_runner.hpp"
#include "npc/npc_capi.h"
#include "npc/serialization/json.hpp"
#include <limits>
#include <memory>

namespace {
using World = std::unique_ptr<NpcWorld, decltype(&npc_world_destroy)>;
World world() { return World(npc_world_create(16, 16), npc_world_destroy); }
struct Updates { NpcHandle expected; int count = 0; bool valid = true; };
void update(NpcHandle handle, float, void* userdata) {
    auto& state = *static_cast<Updates*>(userdata);
    state.valid = state.valid && handle == state.expected;
    ++state.count;
}
void capture(const char* json, void* userdata) {
    *static_cast<std::string*>(userdata) = json;
}
}

TEST("C API: invalid world dimensions fail without crossing the ABI") {
    ASSERT_TRUE(npc_world_create(0, 16) == nullptr);
    ASSERT_TRUE(npc_world_create(16, -1) == nullptr);
    auto w = world();
    ASSERT_TRUE(w != nullptr);
    const float initial = npc_world_get_total_time(w.get());
    npc_world_update(w.get(), 0.25f);
    ASSERT_NEAR(npc_world_get_total_time(w.get()), initial + 0.25f, 1e-6f);
}

TEST("C API: IDs are nonzero and unique within their world") {
    auto w = world(), other = world();
    ASSERT_TRUE(npc_create(w.get(), 0, "Alice", NPC_TYPE_VILLAGER) == nullptr);
    ASSERT_TRUE(npc_create(w.get(), 1, nullptr, NPC_TYPE_VILLAGER) == nullptr);
    ASSERT_TRUE(npc_create(w.get(), 1, "Alice", NPC_TYPE_VILLAGER) != nullptr);
    ASSERT_TRUE(npc_create(w.get(), 1, "Ben", NPC_TYPE_GUARD) == nullptr);
    ASSERT_TRUE(npc_create(other.get(), 1, "Ben", NPC_TYPE_GUARD) != nullptr);
}

TEST("C API: a different world cannot destroy an owned NPC") {
    auto w = world(), other = world();
    auto alice = npc_create(w.get(), 1, "Alice", NPC_TYPE_VILLAGER);
    ASSERT_TRUE(alice != nullptr);
    npc_destroy(other.get(), alice);
    ASSERT_EQ(std::string(npc_get_name(alice)), "Alice");
    npc_set_position(alice, 3, 4);
    ASSERT_NEAR(npc_get_position(alice).x, 3, 1e-6f);
    npc_destroy(w.get(), alice);
    ASSERT_TRUE(npc_create(w.get(), 1, "Ben", NPC_TYPE_GUARD) != nullptr);
}

TEST("C API: handles and FSM callbacks survive registry growth") {
    auto w = world();
    auto alice = npc_create(w.get(), 1, "Alice", NPC_TYPE_VILLAGER);
    ASSERT_TRUE(alice != nullptr);
    Updates state{alice};
    npc_fsm_add_state(alice, "working", update, nullptr, nullptr, &state);
    npc_fsm_set_initial(alice, "working");
    for (uint32_t id = 2; id <= 64; ++id)
        ASSERT_TRUE(npc_create(w.get(), id, "Resident", NPC_TYPE_VILLAGER) != nullptr);
    npc_world_update(w.get(), 0.01f);
    ASSERT_EQ(state.count, 1);
    ASSERT_TRUE(state.valid);
    ASSERT_EQ(std::string(npc_fsm_get_state(alice)), "working");
}

TEST("C API: retired NPCs stop receiving updates and world events") {
    auto w = world();
    Updates state{nullptr};
    for (int i = 0; i < 32; ++i) {
        auto alice = npc_create(w.get(), 1, "Alice", NPC_TYPE_VILLAGER);
        ASSERT_TRUE(alice != nullptr);
        state.expected = alice;
        npc_fsm_add_state(alice, "working", update, nullptr, nullptr, &state);
        npc_fsm_set_initial(alice, "working");
        npc_world_update(w.get(), 0.01f);
        npc_destroy(w.get(), alice);
        const int before = state.count;
        npc_world_fire_event(w.get(), "meeting", "Repair meeting", 0.2f);
        npc_world_update(w.get(), 0.01f);
        ASSERT_EQ(state.count, before);
    }
    ASSERT_EQ(state.count, 32);
    ASSERT_TRUE(state.valid);
}

TEST("C API: event JSON preserves escaped text and finite numbers") {
    auto w = world();
    std::string captured;
    npc_world_on_world_event(w.get(), capture, &captured);
    const char* type = "notice\"\\";
    const char* description = "Alice said \"hello\".\nC:\\Village\t\x01";
    npc_world_fire_event(w.get(), type, description, 0.375f);
    auto event = npc::serial::parse(captured);
    ASSERT_EQ(event["type"].asString(), type);
    ASSERT_EQ(event["description"].asString(), description);
    ASSERT_NEAR(event["severity"].asDouble(), 0.375, 1e-9);
    npc_world_fire_event(w.get(), "notice", "", std::numeric_limits<float>::quiet_NaN());
    ASSERT_TRUE(npc::serial::parse(captured)["severity"].isNull());
}

TEST("C API: blackboards, health and bounded relationship buffers cross the ABI") {
    auto w = world();
    auto alice = npc_create(w.get(), 1, "Alice", NPC_TYPE_VILLAGER);
    ASSERT_TRUE(alice != nullptr);
    npc_bb_set_string(alice, "task", "repair");
    npc_bb_set_int(alice, "tools", 3);
    ASSERT_EQ(std::string(npc_bb_get_string(alice, "task")), "repair");
    ASSERT_EQ(npc_bb_get_int(alice, "tools", -1), 3);
    const float health = npc_get_health(alice);
    npc_deal_damage(alice, 5);
    ASSERT_NEAR(npc_get_health(alice), health - 5, 1e-6f);
    npc_heal(alice, 5);
    ASSERT_NEAR(npc_get_health(alice), health, 1e-6f);
    std::unique_ptr<NpcRelSys, decltype(&npc_rel_destroy)> rs(npc_rel_create(), npc_rel_destroy);
    ASSERT_TRUE(rs != nullptr);
    npc_rel_modify(rs.get(), "Alice", "Ben", 20);
    ASSERT_NEAR(npc_rel_get_value(rs.get(), "Alice", "Ben"), 20, 1e-6f);
    char buffer[4] = {'x', 'x', 'x', 'x'};
    const int written = npc_rel_narrative(rs.get(), "Alice", "Ben", 1, buffer, sizeof(buffer));
    ASSERT_TRUE(written >= 0 && written < static_cast<int>(sizeof(buffer)));
    ASSERT_EQ(buffer[written], '\0');
}

TEST("C API: runtime version agrees with the configured library") {
    ASSERT_EQ(std::string(npc_version()), NPC_EXPECTED_VERSION);
}

int main(int argc, char**) { return npc::test::run_all(argc > 1); }
