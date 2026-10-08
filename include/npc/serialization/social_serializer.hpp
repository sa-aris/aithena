#pragma once

#include "json.hpp"
#include "save_load.hpp"
#include "../social/social_contract_system.hpp"
#include <charconv>

namespace npc {

// Versioned dynamic state. Policies, perception hooks and pending audit events
// are host-owned; loading preserves the installed hooks and emits no events.
struct SocialSerializer {
    using J = serial::JsonValue;

    static J toJson(const SocialContractSystem& s) {
        serial::JsonObject root, cfg;
        root["version"] = 1;
        root["seed"] = std::to_string(s.seed_);
        root["time"] = s.time_;
        root["epoch"] = s.lastGossipEpoch_ ? J(std::to_string(*s.lastGossipEpoch_)) : J(nullptr);
        cfg["maxActors"] = static_cast<uint64_t>(s.config_.maxActors);
        cfg["maxContracts"] = static_cast<uint64_t>(s.config_.maxContracts);
        cfg["maxKnowledgePerActor"] = static_cast<uint64_t>(s.config_.maxKnowledgePerActor);
        cfg["maxEvents"] = static_cast<uint64_t>(s.config_.maxEvents);
        cfg["maxHops"] = static_cast<uint64_t>(s.config_.maxHops);
        cfg["maxTopicsPerContact"] = static_cast<uint64_t>(s.config_.maxTopicsPerContact);
        cfg["socialRange"] = s.config_.socialRange;
        cfg["gossipDecay"] = s.config_.gossipDecay;
        cfg["gossipChance"] = s.config_.gossipChance;
        cfg["minConfidence"] = s.config_.minConfidence;
        cfg["minNotice"] = s.config_.minNotice;
        cfg["beliefLifetime"] = s.config_.beliefLifetime;
        cfg["opinionScale"] = s.config_.opinionScale;
        cfg["supportCredit"] = s.config_.supportCredit;
        root["config"] = std::move(cfg);
        serial::JsonArray actors, contracts, knowledge, beliefs, opinions, relations, reputation;
        for (const auto& entry : s.actors_) {
            const auto& a = entry.second;
            serial::JsonObject j;
            j["id"] = a.id; j["name"] = a.name;
            j["x"] = a.position.x; j["y"] = a.position.y;
            j["alive"] = a.alive; j["available"] = a.available;
            j["budget"] = a.effortBudget; j["speed"] = a.moveSpeed;
            j["responsibility"] = a.responsibility; j["contact_floor"] = a.minimumContactAffinity;
            j["traits"] = serial::JsonArray{a.personality.courage, a.personality.sociability,
                a.personality.greed, a.personality.patience, a.personality.intelligence};
            actors.emplace_back(std::move(j));
        }
        for (const auto& entry : s.contracts_) {
            const auto& c = entry.second;
            serial::JsonObject j;
            j["id"] = std::to_string(c.id); j["actor"] = c.actor; j["beneficiary"] = c.beneficiary;
            j["topic"] = c.topic; j["opened"] = c.openedAt; j["deadline"] = c.deadline;
            j["effort"] = c.effort; j["obligation"] = c.obligation; j["radius"] = c.arrivalRadius;
            j["duration"] = c.workDuration; j["work_started"] = c.workStartedAt;
            j["remote"] = c.allowRemoteSupport; j["phase"] = static_cast<int>(c.phase);
            j["resolved"] = c.resolvedAt; j["resolution_version"] = c.resolutionVersion;
            j["informed"] = c.informedInTime; j["explanation"] = c.explanation;
            contracts.emplace_back(std::move(j));
        }
        for (const auto& owner : s.knowledge_)
            for (const auto& entry : owner.second) {
                serial::JsonObject j = recordKey(owner.first, entry.first);
                j["learned"] = entry.second.learnedAt; j["confidence"] = entry.second.confidence;
                j["path"] = pathJson(entry.second.path); knowledge.emplace_back(std::move(j));
            }
        for (const auto& owner : s.beliefs_)
            for (const auto& entry : owner.second) {
                const auto& b = entry.second;
                serial::JsonObject j = recordKey(owner.first, entry.first);
                j["outcome"] = static_cast<int>(b.outcome); j["version"] = b.version;
                j["confidence"] = b.confidence; j["learned"] = b.learnedAt; j["expires"] = b.expiresAt;
                j["first_hand"] = b.firstHand; j["path"] = pathJson(b.path); j["opinion"] = b.opinion;
                beliefs.emplace_back(std::move(j));
            }
        for (const auto& owner : s.opinions_)
            for (const auto& entry : owner.second) {
                serial::JsonObject j = recordKey(owner.first, entry.first);
                j["value"] = entry.second; opinions.emplace_back(std::move(j));
            }
        for (const auto& entry : s.relationshipOpinions_) {
            auto j = accountJson(entry.second);
            j["observer"] = entry.first.first; j["target"] = entry.first.second;
            relations.emplace_back(std::move(j));
        }
        for (const auto& entry : s.reputationOpinions_) {
            auto j = accountJson(entry.second); j["target"] = entry.first;
            reputation.emplace_back(std::move(j));
        }
        root["actors"] = std::move(actors); root["contracts"] = std::move(contracts);
        root["knowledge"] = std::move(knowledge); root["beliefs"] = std::move(beliefs);
        root["opinions"] = std::move(opinions); root["relation_accounts"] = std::move(relations);
        root["reputation_accounts"] = std::move(reputation);
        root["relationships"] = WorldSerializer::toJson(s.relationships_);
        root["reputation"] = WorldSerializer::toJson(s.reputation_);
        return root;
    }

    static bool fromJson(SocialContractSystem& s, const J& root, std::string* error = nullptr) {
        try {
            require(root.isObject() && integer(root, "version") == 1, "Unsupported social save version");
            SocialContractSystem next(unsigned64(root["seed"]));
            const auto& cfg = root["config"];
            SocialContractSystem::Config config;
            config.maxActors = count(cfg, "maxActors");
            config.maxContracts = count(cfg, "maxContracts");
            config.maxKnowledgePerActor = count(cfg, "maxKnowledgePerActor");
            config.maxEvents = count(cfg, "maxEvents");
            config.maxHops = count(cfg, "maxHops");
            config.maxTopicsPerContact = count(cfg, "maxTopicsPerContact");
            config.socialRange = number(cfg, "socialRange");
            config.gossipDecay = number(cfg, "gossipDecay");
            config.gossipChance = number(cfg, "gossipChance");
            config.minConfidence = number(cfg, "minConfidence");
            config.minNotice = number(cfg, "minNotice");
            config.beliefLifetime = number(cfg, "beliefLifetime");
            config.opinionScale = number(cfg, "opinionScale");
            config.supportCredit = number(cfg, "supportCredit");
            require(next.configure(config), "Invalid social limits");
            next.time_ = number(root, "time");
            require(next.time_ >= 0.0, "Negative social time");
            if (!root["epoch"].isNull()) next.lastGossipEpoch_ = unsigned64(root["epoch"]);
            for (const auto& j : array(root, "actors", config.maxActors)) {
                SocialActor a;
                a.id = entity(j, "id"); a.name = string(j, "name");
                a.position = {static_cast<float>(number(j, "x")), static_cast<float>(number(j, "y"))};
                a.alive = boolean(j, "alive"); a.available = boolean(j, "available");
                a.effortBudget = number(j, "budget"); a.moveSpeed = number(j, "speed");
                a.responsibility = number(j, "responsibility");
                a.minimumContactAffinity = number(j, "contact_floor");
                const auto& t = array(j, "traits", 5); require(t.size() == 5, "Invalid traits");
                for (const auto& v : t) require(v.isNumber() && std::isfinite(v.asDouble()), "Invalid trait");
                a.personality = {t[0].asFloat(), t[1].asFloat(), t[2].asFloat(), t[3].asFloat(), t[4].asFloat()};
                require(!next.actor(a.id) && next.upsertActor(a), "Invalid or duplicate actor");
            }
            for (const auto& j : array(root, "contracts", config.maxContracts)) {
                SocialContract c;
                c.id = unsigned64(j["id"]); c.actor = entity(j, "actor"); c.beneficiary = entity(j, "beneficiary");
                c.topic = string(j, "topic"); c.openedAt = number(j, "opened"); c.deadline = number(j, "deadline");
                c.effort = number(j, "effort"); c.obligation = number(j, "obligation");
                c.arrivalRadius = number(j, "radius"); c.allowRemoteSupport = boolean(j, "remote");
                c.workDuration = number(j, "duration"); c.workStartedAt = number(j, "work_started");
                c.phase = phase(j, "phase"); c.resolvedAt = number(j, "resolved");
                c.resolutionVersion = integer(j, "resolution_version");
                c.informedInTime = boolean(j, "informed"); c.explanation = string(j, "explanation");
                require(SocialContractSystem::validContract(c) && c.openedAt <= next.time_
                    && next.actor(c.actor) && next.actor(c.beneficiary), "Invalid contract");
                require(c.workStartedAt == -1.0 || (c.workStartedAt >= c.openedAt && c.workStartedAt <= next.time_),
                    "Invalid work start");
                if (contractResolved(c.phase)) {
                    require(c.resolutionVersion > 0 && c.resolvedAt >= c.openedAt
                        && c.resolvedAt <= next.time_, "Invalid resolution");
                    if (c.phase == ContractPhase::Missed) require(c.resolvedAt == c.deadline, "Invalid missed deadline");
                    if (c.phase == ContractPhase::Fulfilled || c.phase == ContractPhase::Supported)
                        require(c.resolvedAt < c.deadline, "Late fulfillment");
                } else require(c.resolutionVersion == 0 && c.resolvedAt == 0.0
                    && c.deadline > next.time_, "Expired open contract");
                require(next.contracts_.emplace(c.id, std::move(c)).second, "Duplicate contract");
            }
            const size_t recordLimit = checkedProduct(config.maxActors, config.maxContracts);
            for (const auto& j : array(root, "knowledge", recordLimit)) {
                const auto observer = entity(j, "observer"); const auto id = unsigned64(j["contract"]);
                SocialKnowledge k{number(j, "learned"), number(j, "confidence"), readPath(next, j, observer)};
                require(next.actor(observer) && next.contract(id) && SocialContractSystem::unitValue(k.confidence)
                    && k.confidence >= config.minConfidence && k.learnedAt >= next.contract(id)->openedAt
                    && k.learnedAt <= next.time_, "Invalid knowledge");
                auto& records = next.knowledge_[observer];
                require(records.size() < config.maxKnowledgePerActor && records.emplace(id, std::move(k)).second,
                    "Duplicate or excessive knowledge");
            }
            for (const auto& entry : next.contracts_) {
                const auto& c = entry.second; const auto k = next.knowledge(c.actor, c.id);
                const bool inTime = k && k->learnedAt <= c.deadline - config.minNotice;
                require(contractResolved(c.phase) ? (!c.informedInTime || inTime) : c.informedInTime == inTime,
                    "Inconsistent notice time");
            }
            for (const auto& j : array(root, "beliefs", recordLimit)) {
                const auto observer = entity(j, "observer"); const auto id = unsigned64(j["contract"]);
                SocialBelief b{phase(j, "outcome"), integer(j, "version"), number(j, "confidence"),
                    number(j, "learned"), number(j, "expires"), boolean(j, "first_hand"),
                    readPath(next, j, observer), number(j, "opinion")};
                const auto c = next.contract(id);
                require(next.actor(observer) && c && contractResolved(b.outcome)
                    && b.version > 0 && b.version <= c->resolutionVersion
                    && SocialContractSystem::unitValue(b.confidence) && b.confidence >= config.minConfidence
                    && b.learnedAt >= c->openedAt && b.learnedAt <= next.time_
                    && b.expiresAt > next.time_ && b.expiresAt > b.learnedAt, "Invalid belief");
                require((b.version == c->resolutionVersion && b.outcome == c->phase) ||
                    (c->phase == ContractPhase::Excused && b.version == 1 && b.outcome == ContractPhase::Missed),
                    "Contradictory belief");
                auto& records = next.beliefs_[observer];
                require(records.size() < config.maxKnowledgePerActor && records.emplace(id, std::move(b)).second,
                    "Duplicate or excessive belief");
            }
            for (const auto& j : array(root, "opinions", recordLimit)) {
                const auto observer = entity(j, "observer"); const auto id = unsigned64(j["contract"]);
                const auto value = number(j, "value");
                require(next.actor(observer) && next.contract(id) && std::abs(value) <= 250.0,
                    "Invalid opinion");
                require(next.opinions_[observer].emplace(id, value).second, "Duplicate opinion");
            }
            for (const auto& owner : next.beliefs_)
                for (const auto& entry : owner.second) {
                    const auto& records = next.opinions_[owner.first];
                    auto it = records.find(entry.first);
                    require(it != records.end() && it->second == entry.second.opinion, "Missing opinion ledger");
                }
            for (const auto& j : array(root, "relation_accounts", checkedProduct(config.maxActors, config.maxActors))) {
                const auto observer = entity(j, "observer"), target = entity(j, "target");
                require(next.actor(observer) && next.actor(target) && observer != target, "Invalid relation account");
                require(next.relationshipOpinions_.emplace(std::make_pair(observer, target), readAccount(j)).second,
                    "Duplicate relation account");
            }
            for (const auto& j : array(root, "reputation_accounts", config.maxActors)) {
                const auto target = entity(j, "target"); require(next.actor(target), "Invalid reputation account");
                require(next.reputationOpinions_.emplace(target, readAccount(j)).second, "Duplicate reputation account");
            }
            std::map<std::pair<EntityId, EntityId>, double> relationTotals;
            std::map<EntityId, double> reputationTotals;
            for (const auto& owner : next.opinions_)
                for (const auto& entry : owner.second) {
                    if (entry.second == 0.0) continue;
                    const auto target = next.contract(entry.first)->actor;
                    require(owner.first != target, "Self opinion");
                    relationTotals[{owner.first, target}] += entry.second;
                    reputationTotals[target] += entry.second;
                }
            for (const auto& entry : relationTotals) {
                auto it = next.relationshipOpinions_.find(entry.first);
                require(it != next.relationshipOpinions_.end() && std::abs(it->second.total - entry.second) < 1e-8,
                    "Missing relation account");
            }
            for (const auto& entry : reputationTotals) {
                auto it = next.reputationOpinions_.find(entry.first);
                require(it != next.reputationOpinions_.end() && std::abs(it->second.total - entry.second) < 1e-8,
                    "Missing reputation account");
            }
            for (const auto& entry : next.relationshipOpinions_)
                require(std::abs(entry.second.total - relationTotals[entry.first]) < 1e-8,
                    "Unmatched relation account");
            for (const auto& entry : next.reputationOpinions_)
                require(std::abs(entry.second.total - reputationTotals[entry.first]) < 1e-8,
                    "Unmatched reputation account");
            require(root["relationships"].isObject() && root["reputation"].isObject()
                && finiteNumbers(root["relationships"]) && finiteNumbers(root["reputation"]), "Invalid social graph");
            std::set<std::pair<std::string, std::string>> pairs;
            for (const auto& pair : array(root["relationships"], "pairs", checkedProduct(config.maxActors, config.maxActors))) {
                const auto a = string(pair, "a"), b = string(pair, "b");
                const auto value = number(pair, "value"), trust = number(pair, "trust");
                require(!a.empty() && !b.empty() && std::abs(value) <= 100.0 && trust >= 0.0 && trust <= 100.0 &&
                    pairs.emplace(a, b).second, "Invalid relationship");
                array(pair, "history", RelationshipData::MAX_HISTORY);
            }
            const auto& reputations = root["reputation"]["reputations"];
            require(reputations.isObject(), "Invalid reputation map");
            for (const auto& entry : reputations.asObject())
                require(!entry.first.empty() && entry.second.isNumber() && std::abs(entry.second.asDouble()) <= 100.0,
                    "Invalid reputation");
            require(root["reputation"]["crimes"].isArray(), "Invalid crime records");
            WorldSerializer::fromJson(next.relationships_, root["relationships"]);
            WorldSerializer::fromJson(next.reputation_, root["reputation"]);
            require(s.revision_ != std::numeric_limits<uint64_t>::max(), "Revision exhausted");
            next.revision_ = s.revision_ + 1;
            next.proposer_ = s.proposer_; next.contactRule_ = s.contactRule_; next.reachabilityRule_ = s.reachabilityRule_;
            next.fulfillmentRule_ = s.fulfillmentRule_;
            s = std::move(next);
            if (error) error->clear();
            return true;
        } catch (const std::exception& ex) {
            if (error) *error = ex.what();
            return false;
        }
    }

private:
    static void require(bool valid, const char* message) {
        if (!valid) throw std::invalid_argument(message);
    }
    static double number(const J& j, const char* key) {
        const auto& v = j[key]; require(v.isNumber() && std::isfinite(v.asDouble()), "Expected finite number");
        return v.asDouble();
    }
    static uint32_t integer(const J& j, const char* key) {
        const auto& v = j[key];
        require(v.isInt() && v.asInt() >= 0 && v.asInt() <= std::numeric_limits<uint32_t>::max(), "Invalid integer");
        return static_cast<uint32_t>(v.asInt());
    }
    static EntityId entity(const J& j, const char* key) {
        const auto id = integer(j, key); require(id != INVALID_ENTITY, "Invalid entity"); return id;
    }
    static size_t count(const J& j, const char* key) {
        return integer(j, key);
    }
    static bool boolean(const J& j, const char* key) {
        require(j[key].isBool(), "Expected boolean"); return j[key].asBool();
    }
    static std::string string(const J& j, const char* key) {
        require(j[key].isString(), "Expected string"); return j[key].asString();
    }
    static uint64_t unsigned64(const J& j) {
        require(j.isString(), "Expected decimal identifier");
        const auto& s = j.asString(); require(!s.empty() && s.size() <= 20, "Invalid identifier");
        uint64_t value = 0;
        const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
        require(result.ec == std::errc{} && result.ptr == s.data() + s.size(), "Invalid identifier");
        return value;
    }
    static ContractPhase phase(const J& j, const char* key) {
        const auto value = integer(j, key);
        require(value <= static_cast<uint32_t>(ContractPhase::Cancelled), "Unknown contract phase");
        return static_cast<ContractPhase>(value);
    }
    static const serial::JsonArray& array(const J& j, const char* key, size_t maximum) {
        require(j[key].isArray() && j[key].asArray().size() <= maximum, "Invalid or excessive array");
        return j[key].asArray();
    }
    static size_t checkedProduct(size_t a, size_t b) {
        require(b == 0 || a <= std::numeric_limits<size_t>::max() / b, "Limit overflow"); return a * b;
    }
    static serial::JsonObject recordKey(EntityId observer, SocialContractId id) {
        return {{"observer", observer}, {"contract", std::to_string(id)}};
    }
    static J pathJson(const std::vector<EntityId>& path) {
        serial::JsonArray out; for (const auto id : path) out.emplace_back(id); return out;
    }
    static std::vector<EntityId> readPath(const SocialContractSystem& s, const J& j, EntityId owner) {
        std::vector<EntityId> out; std::set<EntityId> seen;
        for (const auto& v : array(j, "path", s.config_.maxHops + 1)) {
            require(v.isInt() && v.asInt() > 0 && v.asInt() <= std::numeric_limits<EntityId>::max(), "Invalid path entity");
            const auto id = static_cast<EntityId>(v.asInt());
            require(s.actor(id) && seen.insert(id).second, "Invalid or cyclic evidence path"); out.push_back(id);
        }
        require(!out.empty() && out.back() == owner, "Invalid evidence receiver"); return out;
    }
    static serial::JsonObject accountJson(const SocialContractSystem::OpinionAccount& a) {
        return {{"base", a.base}, {"total", a.total}, {"last", a.lastValue}};
    }
    static SocialContractSystem::OpinionAccount readAccount(const J& j) {
        const auto last = number(j, "last"); require(std::abs(last) <= 100.0, "Invalid displayed opinion");
        const auto base = number(j, "base"), total = number(j, "total");
        require(std::isfinite(base + total) &&
            std::abs(static_cast<float>(std::clamp(base + total, -100.0, 100.0)) - last) < 1e-5,
            "Inconsistent opinion account");
        return {base, total, static_cast<float>(last), true};
    }
    static bool finiteNumbers(const J& j, const std::string& key = {}) {
        if (j.isNumber()) return std::isfinite(j.asDouble()) &&
            (key == "time" || std::abs(j.asDouble()) <= std::numeric_limits<float>::max());
        if (j.isArray()) for (const auto& v : j.asArray()) { if (!finiteNumbers(v)) return false; }
        if (j.isObject()) for (const auto& v : j.asObject()) { if (!finiteNumbers(v.second, v.first)) return false; }
        return true;
    }
};

} // namespace npc
