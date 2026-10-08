#include "npc/npc.hpp"
#include "npc/world/world.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    using namespace npc;
    const uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 42;
    GameWorld world(24, 12);
    const char* names[] = {"Mira", "Ada", "Baran", "Derya", "Eren"};
    for (EntityId id = 1; id <= 5; ++id) {
        auto n = std::make_shared<NPC>(id, names[id - 1], NPCType::Villager);
        n->position = {2.0f + (id - 1) * 3.0f, 4};
        n->personality.sociability = 0.9f;
        n->verbose = false;
        world.addNPC(n);
    }
    world.enableSocialSimulation();
    // Set a new standalone seed before adding requests; preserve world hooks.
    // The scenario's identities and causes stay fixed across runs.
    auto& social = world.social();
    social.setSeed(seed);
    social.relationships().setValue("Ada", "Mira", 65);
    social.relationships().setValue("Baran", "Mira", -80);
    social.relationships().setValue("Derya", "Mira", 15);
    auto config = social.config(); config.gossipChance = 0.9; social.configure(config);

    world.events().subscribe<SocialEvent>([&](const SocialEvent& e) {
        if (e.kind == SocialEventKind::Informed) return;
        const auto* person = social.actor(e.observer);
        const auto* subject = social.actor(e.actor);
        std::cout << e.time << "h: " << (person ? person->name : "unknown") << " / "
                  << (subject ? subject->name : "unknown") << " / "
                  << e.topic << " / " << contractPhaseName(e.phase);
        if (e.kind == SocialEventKind::BeliefChanged)
            std::cout << " / " << (e.firstHand ? "observed" : "hearsay")
                      << " / confidence=" << e.confidence;
        std::cout << '\n';
    });

    for (EntityId who : {2u, 3u, 4u}) {
        SocialContract c;
        c.id = 100 + who; c.actor = who; c.beneficiary = 1;
        c.topic = "join the communal repair shift";
        c.openedAt = social.time(); c.deadline = c.openedAt + 2.0;
        c.obligation = 0.7; c.effort = 0.2;
        c.workDuration = 0.25;
        if (!social.open(c) || !social.inform(who, c.id)) return 1;
    }
    for (int step = 0; step < 60; ++step) world.update(0.1f);
    std::cout << "seed=" << seed << '\n';
    for (const auto& item : social.contracts()) {
        const auto& c = item.second;
        std::cout << social.actor(c.actor)->name << ": " << contractPhaseName(c.phase)
                  << ", reputation=" << social.reputation().reputationOf(social.actor(c.actor)->name) << '\n';
        if (!contractResolved(c.phase)) return 1;
    }
    return 0;
}
