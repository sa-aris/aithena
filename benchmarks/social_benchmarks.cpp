#include "npc/social/social_contract_system.hpp"
#include <chrono>
#include <iostream>

int main() {
    using namespace npc;
    std::cout << "population,decisions_and_resolution_ms,ten_gossip_waves_ms\n";
    for (EntityId count : {64u, 256u, 1024u}) {
        SocialContractSystem social(42);
        for (EntityId i = 1; i <= count; ++i) {
            SocialActor a; a.id = i; a.name = std::to_string(i);
            a.position = {static_cast<float>((i - 1) % 32) * 3,
                          static_cast<float>((i - 1) / 32) * 3};
            if (!social.upsertActor(a)) return 1;
        }
        for (EntityId i = 1; i <= count; ++i) {
            SocialContract c; c.id = i; c.actor = i;
            c.beneficiary = i == count ? 1 : i + 1;
            c.topic = "shared work"; c.deadline = 4;
            if (!social.open(c) || !social.inform(i, c.id)) return 1;
        }
        const auto start = std::chrono::steady_clock::now();
        for (EntityId i = 1; i <= count; ++i) {
            const auto choice = social.decide(i);
            if (choice.selected) social.commit(choice);
            social.complete(i);
        }
        social.advanceTo(4);
        const auto resolved = std::chrono::steady_clock::now();
        for (uint64_t wave = 0; wave < 10; ++wave) social.spreadRumors(wave);
        const auto finished = std::chrono::steady_clock::now();
        const auto ms = [](auto duration) {
            return std::chrono::duration<double, std::milli>(duration).count();
        };
        std::cout << count << ',' << ms(resolved - start) << ',' << ms(finished - resolved) << '\n';
    }
}
