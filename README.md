# NPC Behavior System (C++17)

A comprehensive, standalone NPC behavior system for games, written in modern C++17. Features 13 interconnected AI subsystems that drive believable NPC behavior — from daily routines and emotional states to combat tactics and dynamic trading.

## Features

| Subsystem | Description |
|-----------|-------------|
| **Finite State Machine** | State-based behavior with guarded transitions and priorities |
| **Behavior Tree** | Composites (Sequence, Selector, Parallel), decorators, and leaf nodes with fluent builder API |
| **Utility AI** | Score-based decision making with configurable curves (linear, sigmoid, exponential) |
| **Perception** | Sight cone, hearing range, awareness levels (Unaware → Suspicious → Alert → Combat) |
| **Memory** | NPC remembers events and interactions; memories decay over time based on importance |
| **Emotion & Needs** | Sims-like needs (hunger, sleep, social, safety...) and emotions that affect decisions |
| **Combat AI** | Threat assessment, target selection, ability cooldowns, flee/heal logic |
| **Dialog** | Branching dialog trees with reputation-based text variants and side effects |
| **Trade** | Dynamic pricing (supply/demand, scarcity), buy/sell/barter mechanics |
| **Pathfinding** | A* on grid with 8-directional movement, path smoothing, line-of-sight checks |
| **Schedule** | Time-based daily routines (wake, eat, work, socialize, sleep) |
| **Faction & Groups** | Faction relations, group formations (line, wedge, circle), leader-follower |
| **Event Bus** | Type-safe publish-subscribe system connecting all subsystems |

## Building

```bash
mkdir build && cd build
cmake ..
make
```

**Requirements:** C++17 compiler (g++ 9+, clang++ 10+, MSVC 19.20+), CMake 3.16+

## Running the Demo

```bash
./village_sim
```

Simulates a medieval village with 5 NPCs (Guard, Blacksmith, Merchant, Innkeeper, Farmer) over one full day, including a wolf attack event that triggers combat, flee responses, and memory formation.

## Project Structure

```
NPC/
├── CMakeLists.txt
├── include/npc/
│   ├── core/          # types.hpp, vec2.hpp, random.hpp
│   ├── event/         # event_system.hpp (pub-sub event bus)
│   ├── world/         # world.hpp, time_system.hpp
│   ├── ai/            # fsm.hpp, behavior_tree.hpp, utility_ai.hpp, blackboard.hpp
│   ├── perception/    # perception_system.hpp
│   ├── memory/        # memory_system.hpp
│   ├── emotion/       # emotion_system.hpp
│   ├── navigation/    # pathfinding.hpp (A*)
│   ├── combat/        # combat_system.hpp
│   ├── dialog/        # dialog_system.hpp
│   ├── trade/         # trade_system.hpp
│   ├── social/        # faction_system.hpp, group_behavior.hpp
│   ├── schedule/      # schedule_system.hpp
│   └── npc.hpp        # Main NPC class composing all systems
├── src/               # Implementation files
└── examples/
    └── village_sim.cpp  # Medieval village demo
```

## License

This project is open source and available for educational and game development purposes.
