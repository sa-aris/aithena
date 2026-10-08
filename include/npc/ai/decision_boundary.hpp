#pragma once

#include "../core/types.hpp"
#include "../core/keyed_random.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace npc {

struct DecisionContext {
    EntityId actor = INVALID_ENTITY;
    EntityId target = INVALID_ENTITY;
    uint64_t cause = 0;
    uint64_t revision = 0;
    double time = 0.0;
};

struct ActionProposal {
    uint32_t id = 0;
    std::string name;
    double weight = 1.0;
};

struct ProposalTrace {
    ActionProposal proposal;
    std::vector<std::string> violations;
    bool allowed() const { return violations.empty(); }
};

struct DecisionReport {
    DecisionContext context;
    std::optional<ActionProposal> selected;
    std::vector<ProposalTrace> options;
};

// Policies propose possibilities; the boundary defines what is admissible.
// Guards must be pure. Evaluation never executes an action or changes state.
class DecisionBoundary {
public:
    using Guard = std::function<bool(const DecisionContext&, const ActionProposal&)>;

    void require(std::string code, Guard guard) {
        if (code.empty() || !guard)
            throw std::invalid_argument("A boundary requires a named guard");
        guards_.push_back({std::move(code), std::move(guard)});
    }

    DecisionReport evaluate(const DecisionContext& context,
                            const std::vector<ActionProposal>& proposals,
                            uint64_t seed) const {
        DecisionReport report;
        report.context = context;
        if (proposals.size() > MAX_PROPOSALS)
            throw std::length_error("Too many boundary proposals");

        std::map<uint32_t, size_t> counts;
        for (const auto& p : proposals) ++counts[p.id];
        const bool validContext = context.actor != INVALID_ENTITY && context.cause != 0
                               && std::isfinite(context.time) && context.time >= 0.0;
        double bestScore = std::numeric_limits<double>::infinity();
        uint64_t key = KeyedRandom::combine(seed, context.cause);
        key = KeyedRandom::combine(key, context.actor);
        key = KeyedRandom::combine(key, context.target);

        for (const auto& p : proposals) {
            ProposalTrace trace{p, {}};
            if (!validContext) trace.violations.push_back("invalid_context");
            if (p.id == 0 || p.name.empty()) trace.violations.push_back("invalid_action");
            if (counts[p.id] != 1) trace.violations.push_back("duplicate_action");
            if (!std::isfinite(p.weight) || p.weight <= 0.0)
                trace.violations.push_back("invalid_weight");
            // Invalid inputs do not reach caller-supplied guards.
            if (trace.allowed()) {
                for (const auto& g : guards_)
                    if (!g.second(context, p)) trace.violations.push_back(g.first);
            }
            if (trace.allowed()) {
                // Exponential race in log space avoids overflow at extreme weights.
                // Each action has its own draw, independent of enumeration order.
                const double score = std::log(-std::log(KeyedRandom::unit(key, p.id)))
                                   - std::log(p.weight);
                if (!report.selected || score < bestScore ||
                    (score == bestScore && p.id < report.selected->id)) {
                    bestScore = score;
                    report.selected = p;
                }
            }
            report.options.push_back(std::move(trace));
        }
        std::sort(report.options.begin(), report.options.end(),
                  [](const auto& a, const auto& b) { return a.proposal.id < b.proposal.id; });
        return report;
    }

    static constexpr size_t MAX_PROPOSALS = 256;

private:
    std::vector<std::pair<std::string, Guard>> guards_;
};

} // namespace npc
