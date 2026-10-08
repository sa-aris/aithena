#include "npc/npc_capi.h"
#include <string.h>

int main(void) {
    NpcWorld* world = npc_world_create(8, 8);
    NpcHandle alice;
    if (!world) return 1;
    alice = npc_create(world, 1, "Alice", NPC_TYPE_VILLAGER);
    if (!alice) { npc_world_destroy(world); return 2; }
    npc_bb_set_int(alice, "tools", 3);
    npc_world_update(world, 0.1f);
    if (strcmp(npc_get_name(alice), "Alice") != 0 ||
        npc_bb_get_int(alice, "tools", -1) != 3 || !npc_is_alive(alice)) {
        npc_world_destroy(world);
        return 3;
    }
    npc_destroy(world, alice);
    npc_world_fire_event(world, "meeting", "Repair meeting", 0.2f);
    npc_world_destroy(world);
    return 0;
}
