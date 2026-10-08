#pragma once

#include "../ai/decision_boundary.hpp"
#include "../core/vec2.hpp"
#include "../personality/personality_traits.hpp"
#include "../world/spatial_index.hpp"
#include "relationship_system.hpp"
#include "reputation_system.hpp"
#include <deque>
#include <map>
#include <set>

namespace npc {

using SocialContractId = uint64_t;

enum class SocialAction : uint32_t { Fulfill = 1, SendSupport = 2, Decline = 3 };
enum class ContractPhase { Open, Travelling, Declined, Fulfilled, Supported,
                           Missed, Excused, Cancelled };
enum class SocialEventKind { Informed, Intent, Resolved, BeliefChanged };

inline bool contractResolved(ContractPhase phase) {
    return phase != ContractPhase::Open && phase != ContractPhase::Travelling
        && phase != ContractPhase::Declined;
}

inline const char* contractPhaseName(ContractPhase phase) {
    switch (phase) {
        case ContractPhase::Open: return "open";
        case ContractPhase::Travelling: return "travelling";
        case ContractPhase::Declined: return "declined";
        case ContractPhase::Fulfilled: return "fulfilled";
        case ContractPhase::Supported: return "supported";
        case ContractPhase::Missed: return "missed";
        case ContractPhase::Excused: return "excused";
        case ContractPhase::Cancelled: return "cancelled";
    }
    return "unknown";
}

struct SocialActor {
    EntityId id = INVALID_ENTITY;
    std::string name;
    Vec2 position;
    PersonalityTraits personality;
    bool alive = true;
    bool available = true;
    double effortBudget = 1.0;
    double moveSpeed = 3.0;
    double responsibility = 0.5;
    double minimumContactAffinity = -0.6;
};

// A promise, request, appointment or customary duty. IDs belong to the caller;
// stable IDs keep unrelated insertions out of each other's random choices.
struct SocialContract {
    SocialContractId id = 0;
    EntityId actor = INVALID_ENTITY;
    EntityId beneficiary = INVALID_ENTITY;
    std::string topic;
    double openedAt = 0.0;
    double deadline = 0.0;
    double effort = 0.25;
    double obligation = 0.5;
    double arrivalRadius = 2.0;
    double workDuration = 0.0;
    double workStartedAt = -1.0;
    bool allowRemoteSupport = true;
    ContractPhase phase = ContractPhase::Open;
    double resolvedAt = 0.0;
    uint32_t resolutionVersion = 0;
    bool informedInTime = false;
    std::string explanation;
};

struct SocialKnowledge {
    double learnedAt = 0.0;
    double confidence = 1.0;
    std::vector<EntityId> path;
};

// Outcome and interpretation stay separate. A rumour cannot edit the contract.
struct SocialBelief {
    ContractPhase outcome = ContractPhase::Open;
    uint32_t version = 0;
    double confidence = 1.0;
    double learnedAt = 0.0;
    double expiresAt = 0.0;
    bool firstHand = true;
    std::vector<EntityId> path;
    double opinion = 0.0;
};

struct SocialEvent {
    SocialEventKind kind = SocialEventKind::Informed;
    SocialContractId contract = 0;
    EntityId actor = INVALID_ENTITY;
    EntityId beneficiary = INVALID_ENTITY;
    EntityId observer = INVALID_ENTITY;
    ContractPhase phase = ContractPhase::Open;
    double time = 0.0;
    double confidence = 1.0;
    double effortSpent = 0.0;
    bool firstHand = true;
    std::string topic;
    EntityId source = INVALID_ENTITY;
};

struct SocialDecisionInput {
    const SocialActor& actor;
    const SocialActor& beneficiary;
    const SocialContract& contract;
    double affinity;
    double confidence;
    double freeEffort;
};

// Standalone, opt-in social boundary. No dependence on an NPC decision engine.
class SocialContractSystem {
    friend struct SocialSerializer;
public:
    struct Config {
        size_t maxActors = 1024;
        size_t maxContracts = 4096;
        size_t maxKnowledgePerActor = 256;
        size_t maxEvents = 1024;
        size_t maxHops = 6;
        size_t maxTopicsPerContact = 8;
        double socialRange = 8.0;
        double gossipDecay = 0.75;
        double gossipChance = 0.5;
        double minConfidence = 0.1;
        double minNotice = 0.25;
        double beliefLifetime = 72.0;
        double opinionScale = 4.0;
        double supportCredit = 0.35;
    };

    using Proposer = std::function<std::vector<ActionProposal>(const SocialDecisionInput&)>;
    using ContactRule = std::function<bool(const SocialActor&, const SocialActor&)>;
    using ReachabilityRule = std::function<bool(const SocialActor&, const SocialActor&)>;
    using FulfillmentRule = std::function<bool(const SocialActor&, const SocialActor&, const SocialContract&)>;

    explicit SocialContractSystem(uint64_t seed = 1) : seed_(seed) {}

    const Config& config() const { return config_; }
    bool configure(const Config& config) {
        if (!validConfig(config) || config.maxActors < actors_.size()
            || config.maxContracts < contracts_.size()) return false;
        // Notice is a historical fact once duties exist. Other evidence limits
        // may change only when the retained records still satisfy them.
        if (!contracts_.empty() && config.minNotice != config_.minNotice) return false;
        for (const auto& entry : knowledge_) {
            if (entry.second.size() > config.maxKnowledgePerActor) return false;
            for (const auto& record : entry.second)
                if (record.second.confidence < config.minConfidence ||
                    record.second.path.size() > config.maxHops + 1) return false;
        }
        for (const auto& entry : beliefs_) {
            if (entry.second.size() > config.maxKnowledgePerActor) return false;
            for (const auto& record : entry.second)
                if (record.second.confidence < config.minConfidence ||
                    record.second.path.size() > config.maxHops + 1) return false;
        }
        config_ = config;
        while (events_.size() > config_.maxEvents) events_.pop_front();
        ++revision_;
        return true;
    }

    void setProposer(Proposer proposer) { proposer_ = std::move(proposer); ++revision_; }
    void setContactRule(ContactRule rule) { contactRule_ = std::move(rule); ++revision_; }
    void setReachabilityRule(ReachabilityRule rule) {
        reachabilityRule_ = std::move(rule); ++revision_;
    }
    void setFulfillmentRule(FulfillmentRule rule) {
        fulfillmentRule_ = std::move(rule); ++revision_;
    }
    uint64_t seed() const { return seed_; }
    // Existing contracts retain their once-committed intentions. Reseeding is
    // allowed only before the first contract to keep a run's causal identity.
    bool setSeed(uint64_t seed) {
        if (!contracts_.empty()) return false;
        seed_ = seed; ++revision_; return true;
    }
    double time() const { return time_; }
    uint64_t revision() const { return revision_; }

    RelationshipSystem& relationships() { return relationships_; }
    const RelationshipSystem& relationships() const { return relationships_; }
    ReputationSystem& reputation() { return reputation_; }
    const ReputationSystem& reputation() const { return reputation_; }
    const std::map<EntityId, SocialActor>& actors() const { return actors_; }
    const std::map<SocialContractId, SocialContract>& contracts() const { return contracts_; }

    const SocialActor* actor(EntityId id) const {
        auto it = actors_.find(id);
        return it == actors_.end() ? nullptr : &it->second;
    }
    const SocialContract* contract(SocialContractId id) const {
        auto it = contracts_.find(id);
        return it == contracts_.end() ? nullptr : &it->second;
    }
    const SocialKnowledge* knowledge(EntityId observer, SocialContractId id) const {
        auto owner = knowledge_.find(observer);
        if (owner == knowledge_.end()) return nullptr;
        auto it = owner->second.find(id);
        return it == owner->second.end() ? nullptr : &it->second;
    }
    const SocialBelief* belief(EntityId observer, SocialContractId id) const {
        auto owner = beliefs_.find(observer);
        if (owner == beliefs_.end()) return nullptr;
        auto it = owner->second.find(id);
        return it == owner->second.end() ? nullptr : &it->second;
    }

    bool upsertActor(const SocialActor& value) {
        if (!validActor(value)) return false;
        auto existing = actor(value.id);
        if (existing && existing->name != value.name) return false;
        if (!existing && actors_.size() >= config_.maxActors) return false;
        for (const auto& a : actors_)
            if (a.first != value.id && a.second.name == value.name) return false;
        actors_[value.id] = value;
        spatial_.update(value.id, value.position);
        ++revision_;
        return true;
    }

    // Preserve identity and history when an NPC despawns; cancel live duties.
    bool removeActor(EntityId id) {
        auto it = actors_.find(id);
        if (it == actors_.end()) return false;
        it->second.alive = false;
        it->second.available = false;
        spatial_.remove(id);
        for (auto& item : contracts_)
            if (!contractResolved(item.second.phase) &&
                (item.second.actor == id || item.second.beneficiary == id))
                resolve(item.second, ContractPhase::Cancelled, time_);
        ++revision_;
        return true;
    }

    bool open(const SocialContract& value) {
        if (contracts_.size() >= config_.maxContracts || contracts_.count(value.id)
            || !validContract(value) || value.openedAt != time_
            || value.phase != ContractPhase::Open || value.resolutionVersion != 0
            || !value.explanation.empty()) return false;
        auto a = actor(value.actor), b = actor(value.beneficiary);
        if (!a || !b || !a->alive || !b->alive ||
            knowledge_[b->id].size() >= config_.maxKnowledgePerActor) return false;
        SocialContract clean = value;
        clean.resolvedAt = 0.0;
        clean.informedInTime = false;
        clean.workStartedAt = -1.0;
        contracts_.emplace(clean.id, std::move(clean));
        // The beneficiary knows the request exists; the actor must learn it.
        inform(value.beneficiary, value.id);
        ++revision_;
        return true;
    }

    // An observation supplied by the host's perception/dialogue layer.
    bool inform(EntityId observer, SocialContractId id) {
        auto a = actor(observer);
        auto c = contract(id);
        if (!a || !a->alive || !c) return false;
        SocialKnowledge info{time_, 1.0, {observer}};
        return acceptKnowledge(observer, id, std::move(info));
    }

    double reservedEffort(EntityId id) const {
        double sum = 0.0;
        for (const auto& c : contracts_)
            if (c.second.actor == id && c.second.phase == ContractPhase::Travelling)
                sum += c.second.effort;
        return sum;
    }

    DecisionReport decide(SocialContractId id) const {
        auto c = contract(id);
        if (!c) return {};
        auto a = actor(c->actor), b = actor(c->beneficiary);
        if (!a || !b) return {};
        const auto* info = knowledge(a->id, id);
        const double confidence = info ? info->confidence : 0.0;
        const double free = std::max(0.0, a->effortBudget - reservedEffort(a->id));
        const double affinity = relationships_.getValue(a->name, b->name) / 100.0;
        const SocialDecisionInput input{*a, *b, *c, affinity, confidence, free};
        const auto proposals = proposer_ ? proposer_(input) : defaultProposals(input);

        DecisionBoundary boundary;
        boundary.require("finite_social_context", [&](const auto&, const auto&) {
            return std::isfinite(affinity) && affinity >= -1.0 && affinity <= 1.0;
        });
        boundary.require("contract_open", [&](const auto&, const auto&) {
            return c->phase == ContractPhase::Open;
        });
        boundary.require("participants_alive", [&](const auto&, const auto&) {
            return a->alive && b->alive;
        });
        boundary.require("known_in_time", [&](const auto&, const auto&) {
            return info && confidence >= config_.minConfidence
                && info->learnedAt <= c->deadline - config_.minNotice;
        });
        boundary.require("before_deadline", [&](const auto&, const auto&) {
            return time_ < c->deadline;
        });
        boundary.require("supported_action", [](const auto&, const auto& p) {
            return p.id >= static_cast<uint32_t>(SocialAction::Fulfill)
                && p.id <= static_cast<uint32_t>(SocialAction::Decline);
        });
        boundary.require("available", [&](const auto&, const auto& p) {
            return p.id == static_cast<uint32_t>(SocialAction::Decline) || a->available;
        });
        boundary.require("effort_budget", [&](const auto&, const auto& p) {
            const double cost = p.id == static_cast<uint32_t>(SocialAction::Fulfill)
                              ? c->effort : p.id == static_cast<uint32_t>(SocialAction::SendSupport)
                              ? c->effort * config_.supportCredit : 0.0;
            return cost <= free;
        });
        boundary.require("remote_allowed", [&](const auto&, const auto& p) {
            return p.id != static_cast<uint32_t>(SocialAction::SendSupport)
                || c->allowRemoteSupport;
        });
        boundary.require("personal_contact_boundary", [&](const auto&, const auto& p) {
            return p.id != static_cast<uint32_t>(SocialAction::Fulfill)
                || affinity >= a->minimumContactAffinity;
        });
        boundary.require("can_arrive", [&](const auto&, const auto& p) {
            if (p.id != static_cast<uint32_t>(SocialAction::Fulfill)) return true;
            if (reachabilityRule_ && !reachabilityRule_(*a, *b)) return false;
            const double distance = a->position.distanceTo(b->position);
            if (distance <= c->arrivalRadius) return c->workDuration < c->deadline - time_;
            return a->moveSpeed > 0.0 &&
                (distance - c->arrivalRadius) / a->moveSpeed + c->workDuration < c->deadline - time_;
        });
        return boundary.evaluate({a->id, b->id, c->id, revision_, time_}, proposals, seed_);
    }

    bool commit(const DecisionReport& choice) {
        auto it = contracts_.find(choice.context.cause);
        if (it == contracts_.end() || !choice.selected ||
            choice.context.revision != revision_ || choice.context.time != time_ ||
            choice.context.actor != it->second.actor ||
            choice.context.target != it->second.beneficiary) return false;
        // Recheck external relationship/routing inputs as well as the revision.
        const auto current = decide(it->first);
        if (!current.selected || current.selected->id != choice.selected->id ||
            current.selected->name != choice.selected->name ||
            current.selected->weight != choice.selected->weight) return false;
        auto& c = it->second;
        if (choice.selected->id == static_cast<uint32_t>(SocialAction::Fulfill)) {
            c.phase = ContractPhase::Travelling;
            emit(SocialEventKind::Intent, c, c.actor);
        } else if (choice.selected->id == static_cast<uint32_t>(SocialAction::SendSupport)) {
            const double cost = c.effort * config_.supportCredit;
            actors_.at(c.actor).effortBudget -= cost;
            resolve(c, ContractPhase::Supported, time_, cost);
        } else {
            // A private refusal does not immediately become public evidence.
            c.phase = ContractPhase::Declined;
            emit(SocialEventKind::Intent, c, c.actor);
        }
        ++revision_;
        return true;
    }

    // Intent is not completion. Presence, time and the reserved resource are
    // checked again when the host reports that the action actually happened.
    bool complete(SocialContractId id) {
        auto it = contracts_.find(id);
        if (it == contracts_.end()) return false;
        auto& c = it->second;
        auto& a = actors_.at(c.actor);
        const auto& b = actors_.at(c.beneficiary);
        const auto affinity = relationships_.getValue(a.name, b.name) / 100.0;
        if (c.phase != ContractPhase::Travelling || time_ >= c.deadline ||
            !a.alive || !b.alive || !a.available ||
            !std::isfinite(affinity) || affinity < a.minimumContactAffinity ||
            reservedEffort(a.id) > a.effortBudget ||
            a.position.distanceTo(b.position) > c.arrivalRadius ||
            (contactRule_ && !contactRule_(a, b)) ||
            (fulfillmentRule_ && !fulfillmentRule_(a, b, c))) return false;
        if (c.workDuration > 0.0)
            for (const auto& item : contracts_)
                if (item.first != id && item.second.actor == c.actor &&
                    item.second.phase == ContractPhase::Travelling &&
                    item.second.workDuration > 0.0 && item.second.workStartedAt >= 0.0)
                    return false;
        if (c.workStartedAt < 0.0) { c.workStartedAt = time_; ++revision_; }
        if (time_ < c.workStartedAt + c.workDuration) return false;
        a.effortBudget -= c.effort;
        resolve(c, ContractPhase::Fulfilled, time_, c.effort);
        ++revision_;
        return true;
    }

    bool cancel(SocialContractId id, std::string reason) {
        auto it = contracts_.find(id);
        if (it == contracts_.end() || contractResolved(it->second.phase) || reason.empty())
            return false;
        it->second.explanation = std::move(reason);
        resolve(it->second, ContractPhase::Cancelled, time_);
        ++revision_;
        return true;
    }

    // A changed relationship, schedule or need can reopen a private intention.
    // The same cause keeps the same random draws; repeated reconsideration alone
    // cannot manufacture a different preference.
    bool reconsider(SocialContractId id) {
        auto it = contracts_.find(id);
        if (it == contracts_.end() || time_ >= it->second.deadline ||
            (it->second.phase != ContractPhase::Travelling &&
             it->second.phase != ContractPhase::Declined)) return false;
        it->second.phase = ContractPhase::Open;
        it->second.workStartedAt = -1.0;
        ++revision_;
        return true;
    }

    // Host-verified new evidence. Only the beneficiary learns the correction
    // immediately; others must observe it or receive it through conversation.
    bool excuse(SocialContractId id, std::string evidence) {
        auto it = contracts_.find(id);
        if (it == contracts_.end() || it->second.phase != ContractPhase::Missed
            || evidence.empty()) return false;
        it->second.explanation = std::move(evidence);
        resolve(it->second, ContractPhase::Excused, time_);
        ++revision_;
        return true;
    }

    bool advanceTo(double now) {
        if (!std::isfinite(now) || now < time_) return false;
        if (now != time_) { time_ = now; ++revision_; }
        for (auto& item : contracts_) {
            auto& c = item.second;
            if (contractResolved(c.phase)) continue;
            if (c.phase == ContractPhase::Travelling && c.workStartedAt >= 0.0) {
                const auto& a = actors_.at(c.actor);
                const auto& b = actors_.at(c.beneficiary);
                const auto affinity = relationships_.getValue(a.name, b.name) / 100.0;
                if (!a.available || a.position.distanceTo(b.position) > c.arrivalRadius ||
                    !std::isfinite(affinity) || affinity < a.minimumContactAffinity ||
                    reservedEffort(a.id) > a.effortBudget ||
                    (contactRule_ && !contactRule_(a, b)) ||
                    (fulfillmentRule_ && !fulfillmentRule_(a, b, c))) {
                    c.workStartedAt = -1.0; ++revision_;
                }
            }
            if (!actors_.at(c.actor).alive || !actors_.at(c.beneficiary).alive)
                resolve(c, ContractPhase::Cancelled, now);
            else if (now >= c.deadline)
                resolve(c, ContractPhase::Missed, c.deadline);
        }
        for (auto& owner : beliefs_)
            for (auto it = owner.second.begin(); it != owner.second.end(); ) {
                if (it->second.expiresAt <= time_) it = owner.second.erase(it);
                else ++it;
            }
        return true;
    }

    // The beneficiary can disclose an outcome in person. This does not peek at
    // the world ledger to grant remote bystanders first-hand knowledge.
    bool observeOutcome(EntityId observer, SocialContractId id) {
        auto c = contract(id);
        auto a = actor(observer);
        if (!c || !a || !a->alive || !contractResolved(c->phase) ||
            time_ >= c->resolvedAt + config_.beliefLifetime ||
            !canContact(*actor(c->beneficiary), *a)) return false;
        inform(observer, id);
        SocialBelief evidence = directBelief(*c, observer);
        return acceptBelief(observer, id, std::move(evidence));
    }

    // One staged conversation wave per monotonically increasing epoch. All
    // senders read the same snapshot; newly received news waits for the next wave.
    bool spreadRumors(uint64_t epoch) {
        if (lastGossipEpoch_ && epoch <= *lastGossipEpoch_) return false;
        lastGossipEpoch_ = epoch;
        using Key = std::pair<EntityId, SocialContractId>;
        std::map<Key, SocialKnowledge> news;
        std::map<Key, SocialBelief> reports;
        for (const auto& owner : knowledge_) {
            auto sender = actor(owner.first);
            if (!sender || !sender->alive || !sender->available) continue;
            // A conversation has finite attention. Pick a reproducible subset
            // once per sender and wave rather than broadcasting every memory.
            std::vector<std::pair<uint64_t, SocialContractId>> topics;
            const auto topicSeed = KeyedRandom::combine(KeyedRandom::combine(seed_, epoch), sender->id);
            for (const auto& item : owner.second) {
                const auto* c = contract(item.first);
                if (c && (!contractResolved(c->phase) || time_ < c->resolvedAt + config_.beliefLifetime))
                    topics.emplace_back(KeyedRandom::combine(topicSeed, item.first), item.first);
            }
            std::sort(topics.begin(), topics.end());
            if (topics.size() > config_.maxTopicsPerContact) topics.resize(config_.maxTopicsPerContact);
            auto neighbours = spatial_.nearby(sender->position, static_cast<float>(config_.socialRange));
            std::sort(neighbours.begin(), neighbours.end());
            for (const auto receiverId : neighbours) {
                auto receiver = actor(receiverId);
                if (!receiver || receiverId == sender->id || !canContact(*sender, *receiver)) continue;
                const auto receiverKnowledge = knowledge_.find(receiverId);
                const bool hasKnowledgeRoom = receiverKnowledge == knowledge_.end() ||
                    receiverKnowledge->second.size() < config_.maxKnowledgePerActor;
                for (const auto& topic : topics) {
                    const auto id = topic.second;
                    const auto& infoKnown = owner.second.at(id);
                    uint64_t key = KeyedRandom::combine(epoch, id);
                    key = KeyedRandom::combine(key, sender->id);
                    key = KeyedRandom::combine(key, receiverId);
                    const double chance = config_.gossipChance *
                        (0.25 + 0.75 * sender->personality.sociability);
                    if (KeyedRandom::unit(seed_, key) >= chance) continue;
                    const double trust = relationships_.getTrust(receiver->name, sender->name) / 100.0;
                    if (!unitValue(trust)) continue;
                    const double attenuation = config_.gossipDecay * (0.5 + 0.5 * trust);
                    const auto* receiverInfo = knowledge(receiverId, id);
                    const double newsConfidence = infoKnown.confidence * attenuation;
                    if (newsConfidence >= config_.minConfidence &&
                        (!receiverInfo || newsConfidence >= receiverInfo->confidence) &&
                        (receiverInfo || hasKnowledgeRoom) &&
                        canForward(infoKnown.path, receiverId)) {
                        auto info = infoKnown;
                        info.confidence = newsConfidence;
                        info.learnedAt = time_;
                        info.path.push_back(receiverId);
                        stage(news, Key{receiverId, id}, std::move(info));
                    }
                    const auto* known = belief(sender->id, id);
                    if (!known || known->expiresAt <= time_ || !canForward(known->path, receiverId)) continue;
                    const auto* receiverReport = belief(receiverId, id);
                    const double reportConfidence = known->confidence * attenuation;
                    if (reportConfidence < config_.minConfidence ||
                        (receiverReport && (receiverReport->version > known->version ||
                            (receiverReport->version == known->version &&
                             (receiverReport->firstHand || receiverReport->confidence > reportConfidence)))))
                        continue;
                    auto report = *known;
                    report.confidence = reportConfidence;
                    report.learnedAt = time_;
                    report.firstHand = false;
                    report.path.push_back(receiverId);
                    stage(reports, Key{receiverId, id}, std::move(report));
                }
            }
        }
        for (auto& entry : news) acceptKnowledge(entry.first.first, entry.first.second, std::move(entry.second));
        for (auto& entry : reports) acceptBelief(entry.first.first, entry.first.second, std::move(entry.second));
        ++revision_;
        return true;
    }

    // Explicit retirement keeps persistent state bounded. IDs must not be reused
    // by the host. Historical opinion remains in the relationship/reputation graph.
    bool retire(SocialContractId id) {
        auto it = contracts_.find(id);
        if (it == contracts_.end() || !contractResolved(it->second.phase) ||
            time_ < it->second.resolvedAt + config_.beliefLifetime) return false;
        for (const auto& owner : opinions_) {
            const auto entry = owner.second.find(id);
            if (entry == owner.second.end() || entry->second == 0.0) continue;
            const auto target = it->second.actor;
            auto& relation = relationshipOpinions_.at({owner.first, target});
            relation.rebase(relationships_.getValue(actors_.at(owner.first).name, actors_.at(target).name));
            relation.base += entry->second; relation.total -= entry->second;
            relation.lastValue = relationships_.getValue(actors_.at(owner.first).name, actors_.at(target).name);
            auto& publicOpinion = reputationOpinions_.at(target);
            publicOpinion.rebase(reputation_.reputationOf(actors_.at(target).name));
            publicOpinion.base += entry->second; publicOpinion.total -= entry->second;
            publicOpinion.lastValue = reputation_.reputationOf(actors_.at(target).name);
        }
        contracts_.erase(it);
        for (auto& owner : knowledge_) owner.second.erase(id);
        for (auto& owner : beliefs_) owner.second.erase(id);
        for (auto& owner : opinions_) owner.second.erase(id);
        ++revision_;
        return true;
    }

    std::vector<SocialEvent> drainEvents() {
        std::vector<SocialEvent> out(events_.begin(), events_.end());
        events_.clear();
        return out;
    }

private:
    static bool unitValue(double v) { return std::isfinite(v) && v >= 0.0 && v <= 1.0; }
    static bool validActor(const SocialActor& a) {
        return a.id != INVALID_ENTITY && !a.name.empty()
            && std::isfinite(a.position.x) && std::isfinite(a.position.y)
            && std::abs(a.position.x) < 1e6 && std::abs(a.position.y) < 1e6
            && std::isfinite(a.effortBudget) && a.effortBudget >= 0.0 && a.effortBudget <= 1e6
            && std::isfinite(a.moveSpeed) && a.moveSpeed >= 0.0 && a.moveSpeed <= 1e6
            && std::isfinite(a.minimumContactAffinity) && a.minimumContactAffinity >= -1.0
            && a.minimumContactAffinity <= 1.0
            && unitValue(a.responsibility) && unitValue(a.personality.courage)
            && unitValue(a.personality.sociability) && unitValue(a.personality.greed)
            && unitValue(a.personality.patience) && unitValue(a.personality.intelligence);
    }
    static bool validContract(const SocialContract& c) {
        return c.id != 0 && c.actor != INVALID_ENTITY && c.beneficiary != INVALID_ENTITY
            && c.actor != c.beneficiary && !c.topic.empty()
            && std::isfinite(c.openedAt) && c.openedAt >= 0.0
            && std::isfinite(c.deadline) && c.deadline > c.openedAt
            && std::isfinite(c.workDuration) && c.workDuration >= 0.0
            && c.workDuration < c.deadline - c.openedAt
            && unitValue(c.obligation) && std::isfinite(c.effort) && c.effort > 0.0 && c.effort <= 1e6
            && std::isfinite(c.arrivalRadius) && c.arrivalRadius >= 0.0 && c.arrivalRadius <= 1e6;
    }
    static bool validConfig(const Config& c) {
        return c.maxActors > 0 && c.maxContracts > 0 && c.maxKnowledgePerActor > 0
            && c.maxEvents > 0 && c.maxHops > 0 && c.maxHops <= 64
            && c.maxTopicsPerContact > 0 && c.maxTopicsPerContact <= 256
            && c.maxActors <= UINT32_MAX && c.maxContracts <= UINT32_MAX
            && c.maxKnowledgePerActor <= UINT32_MAX && c.maxEvents <= UINT32_MAX
            && std::isfinite(c.socialRange) && c.socialRange > 0.0 && c.socialRange <= 1e4
            && unitValue(c.gossipChance) && unitValue(c.gossipDecay) && c.gossipDecay < 1.0
            && unitValue(c.minConfidence) && c.minConfidence > 0.0
            && std::isfinite(c.minNotice) && c.minNotice >= 0.0
            && std::isfinite(c.beliefLifetime) && c.beliefLifetime > 0.0
            && std::isfinite(c.opinionScale) && c.opinionScale >= 0.0 && c.opinionScale <= 100.0
            && unitValue(c.supportCredit);
    }

    std::vector<ActionProposal> defaultProposals(const SocialDecisionInput& in) const {
        const double affinity = std::clamp(in.affinity, -1.0, 1.0);
        const double care = (1.0 - in.actor.personality.greed) * 0.3
                          + in.actor.responsibility * in.contract.obligation;
        const double willingness = std::clamp(0.1 + affinity * 0.8 + care, 0.01, 2.0);
        return {{1, "fulfill", willingness * in.confidence},
                {2, "send_support", 0.15 + care * 0.3},
                {3, "decline", std::clamp(0.6 - affinity * 0.7 - care * 0.3, 0.02, 2.0)}};
    }

    bool canContact(const SocialActor& a, const SocialActor& b) const {
        return a.alive && b.alive && a.available && b.available
            && a.position.distanceTo(b.position) <= config_.socialRange
            && (!contactRule_ || contactRule_(a, b));
    }
    bool canForward(const std::vector<EntityId>& path, EntityId receiver) const {
        return !path.empty() && path.size() <= config_.maxHops
            && std::find(path.begin(), path.end(), receiver) == path.end();
    }
    template<class T>
    static void stage(std::map<std::pair<EntityId, SocialContractId>, T>& batch,
                      std::pair<EntityId, SocialContractId> key, T value) {
        auto it = batch.find(key);
        if (it == batch.end() || better(value, it->second)) batch[key] = std::move(value);
    }
    static bool better(const SocialKnowledge& a, const SocialKnowledge& b) {
        return a.confidence > b.confidence ||
            (a.confidence == b.confidence && a.path < b.path);
    }
    static bool better(const SocialBelief& a, const SocialBelief& b) {
        if (a.version != b.version) return a.version > b.version;
        if (a.firstHand != b.firstHand) return a.firstHand;
        return a.confidence > b.confidence ||
            (a.confidence == b.confidence && a.path < b.path);
    }
    bool acceptKnowledge(EntityId observer, SocialContractId id, SocialKnowledge info) {
        if (!unitValue(info.confidence) || info.confidence < config_.minConfidence) return false;
        auto& records = knowledge_[observer];
        auto it = records.find(id);
        if (it != records.end()) {
            if (!better(info, it->second)) return false;
            // Learning stronger evidence does not reset the original notice time.
            info.learnedAt = std::min(info.learnedAt, it->second.learnedAt);
        } else if (records.size() >= config_.maxKnowledgePerActor) return false;
        records[id] = std::move(info);
        auto& c = contracts_.at(id);
        if (observer == c.actor && !contractResolved(c.phase))
            c.informedInTime = records[id].learnedAt <= c.deadline - config_.minNotice;
        emit(SocialEventKind::Informed, c, observer, records[id].confidence,
             records[id].path.size() == 1);
        events_.back().phase = ContractPhase::Open;
        if (records[id].path.size() > 1)
            events_.back().source = records[id].path[records[id].path.size() - 2];
        ++revision_;
        return true;
    }
    SocialBelief directBelief(const SocialContract& c, EntityId observer) const {
        return {c.phase, c.resolutionVersion, 1.0, time_,
                c.resolvedAt + config_.beliefLifetime, true, {observer}, 0.0};
    }
    double opinionFor(const SocialContract& c, const SocialBelief& report, EntityId observer) const {
        double value = 0.0;
        if (report.outcome == ContractPhase::Fulfilled) value = 1.0;
        if (report.outcome == ContractPhase::Supported) value = config_.supportCredit;
        if (report.outcome == ContractPhase::Missed && c.informedInTime) value = -1.0;
        const auto& watcher = actors_.at(observer);
        const auto& beneficiary = actors_.at(c.beneficiary);
        const double sympathy = std::clamp(
            relationships_.getValue(watcher.name, beneficiary.name) / 100.0, -1.0, 1.0);
        const double normWeight = 0.25 + 1.5 * watcher.responsibility;
        return value * c.obligation * report.confidence * config_.opinionScale
             * normWeight * (1.0 + 0.35 * sympathy);
    }
    bool acceptBelief(EntityId observer, SocialContractId id, SocialBelief report) {
        const auto& c = contracts_.at(id);
        auto a = actor(observer);
        if (!a || !a->alive || !unitValue(report.confidence) || report.confidence < config_.minConfidence
            || report.expiresAt <= time_) return false;
        auto& records = beliefs_[observer];
        auto it = records.find(id);
        if (it != records.end() && !better(report, it->second)) return false;
        if (it == records.end() && records.size() >= config_.maxKnowledgePerActor) return false;
        report.opinion = observer == c.actor ? 0.0 : opinionFor(c, report, observer);
        auto& applied = opinions_[observer][id];
        const double delta = report.opinion - applied;
        applied = report.opinion;
        if (delta != 0.0) {
            const auto& target = actors_.at(c.actor);
            applyOpinion(observer, c.actor, delta);
            RelationshipEvent ev;
            ev.type = RelationshipEventType::Custom;
            ev.initiator = target.name;
            ev.target = a->name;
            ev.delta = static_cast<float>(delta);
            ev.simTime = time_;
            ev.note = c.topic + ": " + contractPhaseName(report.outcome);
            relationships_.get(a->name, target.name).addEvent(std::move(ev));
        }
        records[id] = std::move(report);
        emit(SocialEventKind::BeliefChanged, c, observer, records[id].confidence,
             records[id].firstHand);
        events_.back().phase = records[id].outcome;
        if (records[id].path.size() > 1)
            events_.back().source = records[id].path[records[id].path.size() - 2];
        ++revision_;
        return true;
    }
    void resolve(SocialContract& c, ContractPhase phase, double at, double effort = 0.0) {
        c.phase = phase;
        c.resolvedAt = at;
        ++c.resolutionVersion;
        emit(SocialEventKind::Resolved, c, c.beneficiary, 1.0, true, effort);
        events_.back().time = at;
        acceptBelief(c.beneficiary, c.id, directBelief(c, c.beneficiary));
        if (knowledge(c.actor, c.id)) acceptBelief(c.actor, c.id, directBelief(c, c.actor));
        ++revision_;
    }
    struct OpinionAccount {
        double base = 0.0;
        double total = 0.0;
        float lastValue = 0.0f;
        bool initialized = false;
        void rebase(float current) {
            if (!initialized) { base = current; initialized = true; }
            else if (current != lastValue) base = current - total;
        }
    };
    void applyOpinion(EntityId observer, EntityId target, double delta) {
        const auto& a = actors_.at(observer);
        const auto& b = actors_.at(target);
        auto& relation = relationshipOpinions_[{observer, target}];
        relation.rebase(relationships_.getValue(a.name, b.name));
        relation.total += delta;
        relationships_.setValue(a.name, b.name, static_cast<float>(relation.base + relation.total));
        relation.lastValue = relationships_.getValue(a.name, b.name);
        auto& publicOpinion = reputationOpinions_[target];
        publicOpinion.rebase(reputation_.reputationOf(b.name));
        publicOpinion.total += delta;
        reputation_.setReputation(b.name, static_cast<float>(publicOpinion.base + publicOpinion.total));
        publicOpinion.lastValue = reputation_.reputationOf(b.name);
    }
    void emit(SocialEventKind kind, const SocialContract& c, EntityId observer,
              double confidence = 1.0, bool firstHand = true, double effort = 0.0) {
        if (events_.size() == config_.maxEvents) events_.pop_front();
        events_.push_back({kind, c.id, c.actor, c.beneficiary, observer, c.phase,
                           time_, confidence, effort, firstHand, c.topic});
    }

    Config config_;
    uint64_t seed_;
    double time_ = 0.0;
    uint64_t revision_ = 0;
    std::optional<uint64_t> lastGossipEpoch_;
    std::map<EntityId, SocialActor> actors_;
    std::map<SocialContractId, SocialContract> contracts_;
    std::map<EntityId, std::map<SocialContractId, SocialKnowledge>> knowledge_;
    std::map<EntityId, std::map<SocialContractId, SocialBelief>> beliefs_;
    std::map<EntityId, std::map<SocialContractId, double>> opinions_;
    std::map<std::pair<EntityId, EntityId>, OpinionAccount> relationshipOpinions_;
    std::map<EntityId, OpinionAccount> reputationOpinions_;
    std::deque<SocialEvent> events_;
    SpatialIndex spatial_{8.0f};
    RelationshipSystem relationships_;
    ReputationSystem reputation_;
    Proposer proposer_;
    ContactRule contactRule_;
    ReachabilityRule reachabilityRule_;
    FulfillmentRule fulfillmentRule_;
};

} // namespace npc
