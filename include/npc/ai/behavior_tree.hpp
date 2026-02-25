#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include <algorithm>
#include "../core/types.hpp"
#include "blackboard.hpp"

namespace npc {

// ─── Base Node ───────────────────────────────────────────────────────

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual NodeStatus tick(Blackboard& bb) = 0;
    virtual void reset() {}
    void setName(const std::string& name) { name_ = name; }
    const std::string& name() const { return name_; }
protected:
    std::string name_ = "unnamed";
};

using BTNodePtr = std::unique_ptr<BTNode>;

// ═══════════════════════════════════════════════════════════════════════
// LEAF NODES
// ═══════════════════════════════════════════════════════════════════════

class ActionNode : public BTNode {
public:
    using ActionFn = std::function<NodeStatus(Blackboard&)>;

    ActionNode(std::string name, ActionFn fn)
        : fn_(std::move(fn)) { name_ = std::move(name); }

    NodeStatus tick(Blackboard& bb) override { return fn_(bb); }

private:
    ActionFn fn_;
};

class ConditionNode : public BTNode {
public:
    using ConditionFn = std::function<bool(const Blackboard&)>;

    ConditionNode(std::string name, ConditionFn fn)
        : fn_(std::move(fn)) { name_ = std::move(name); }

    NodeStatus tick(Blackboard& bb) override {
        return fn_(bb) ? NodeStatus::Success : NodeStatus::Failure;
    }

private:
    ConditionFn fn_;
};

// ═══════════════════════════════════════════════════════════════════════
// COMPOSITE NODES
// ═══════════════════════════════════════════════════════════════════════

class SequenceNode : public BTNode {
public:
    explicit SequenceNode(std::string name = "Sequence") { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }

    NodeStatus tick(Blackboard& bb) override {
        for (size_t i = runningIdx_; i < children_.size(); ++i) {
            auto status = children_[i]->tick(bb);
            if (status == NodeStatus::Running) {
                runningIdx_ = i;
                return NodeStatus::Running;
            }
            if (status == NodeStatus::Failure) {
                runningIdx_ = 0;
                return NodeStatus::Failure;
            }
        }
        runningIdx_ = 0;
        return NodeStatus::Success;
    }

    void reset() override {
        runningIdx_ = 0;
        for (auto& c : children_) c->reset();
    }

private:
    std::vector<BTNodePtr> children_;
    size_t runningIdx_ = 0;
};

class SelectorNode : public BTNode {
public:
    explicit SelectorNode(std::string name = "Selector") { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }

    NodeStatus tick(Blackboard& bb) override {
        for (size_t i = runningIdx_; i < children_.size(); ++i) {
            auto status = children_[i]->tick(bb);
            if (status == NodeStatus::Running) {
                runningIdx_ = i;
                return NodeStatus::Running;
            }
            if (status == NodeStatus::Success) {
                runningIdx_ = 0;
                return NodeStatus::Success;
            }
        }
        runningIdx_ = 0;
        return NodeStatus::Failure;
    }

    void reset() override {
        runningIdx_ = 0;
        for (auto& c : children_) c->reset();
    }

private:
    std::vector<BTNodePtr> children_;
    size_t runningIdx_ = 0;
};

class ParallelNode : public BTNode {
public:
    // successThreshold: how many children must succeed for overall success
    ParallelNode(int successThreshold, std::string name = "Parallel")
        : successThreshold_(successThreshold) { name_ = std::move(name); }

    void addChild(BTNodePtr child) { children_.push_back(std::move(child)); }

    NodeStatus tick(Blackboard& bb) override {
        int successCount = 0;
        int failureCount = 0;

        for (auto& child : children_) {
            auto status = child->tick(bb);
            if (status == NodeStatus::Success) ++successCount;
            else if (status == NodeStatus::Failure) ++failureCount;
        }

        if (successCount >= successThreshold_)
            return NodeStatus::Success;
        if (failureCount > static_cast<int>(children_.size()) - successThreshold_)
            return NodeStatus::Failure;
        return NodeStatus::Running;
    }

    void reset() override {
        for (auto& c : children_) c->reset();
    }

private:
    std::vector<BTNodePtr> children_;
    int successThreshold_;
};

// ═══════════════════════════════════════════════════════════════════════
// DECORATOR NODES
// ═══════════════════════════════════════════════════════════════════════

class InverterNode : public BTNode {
public:
    explicit InverterNode(BTNodePtr child) : child_(std::move(child)) { name_ = "Inverter"; }

    NodeStatus tick(Blackboard& bb) override {
        auto status = child_->tick(bb);
        if (status == NodeStatus::Success) return NodeStatus::Failure;
        if (status == NodeStatus::Failure) return NodeStatus::Success;
        return NodeStatus::Running;
    }

    void reset() override { child_->reset(); }

private:
    BTNodePtr child_;
};

class RepeaterNode : public BTNode {
public:
    RepeaterNode(BTNodePtr child, int maxRepeats = -1)
        : child_(std::move(child)), maxRepeats_(maxRepeats) { name_ = "Repeater"; }

    NodeStatus tick(Blackboard& bb) override {
        if (maxRepeats_ > 0 && count_ >= maxRepeats_) {
            count_ = 0;
            return NodeStatus::Success;
        }
        auto status = child_->tick(bb);
        if (status == NodeStatus::Running) return NodeStatus::Running;
        ++count_;
        if (maxRepeats_ < 0) return NodeStatus::Running; // infinite
        if (count_ >= maxRepeats_) {
            count_ = 0;
            return NodeStatus::Success;
        }
        return NodeStatus::Running;
    }

    void reset() override { count_ = 0; child_->reset(); }

private:
    BTNodePtr child_;
    int maxRepeats_;
    int count_ = 0;
};

class CooldownNode : public BTNode {
public:
    CooldownNode(BTNodePtr child, float cooldownTime)
        : child_(std::move(child)), cooldownTime_(cooldownTime) { name_ = "Cooldown"; }

    NodeStatus tick(Blackboard& bb) override {
        auto now = bb.getOr<float>("_time", 0.0f);
        if (now < readyTime_) return NodeStatus::Failure;
        auto status = child_->tick(bb);
        if (status == NodeStatus::Success || status == NodeStatus::Failure) {
            readyTime_ = now + cooldownTime_;
        }
        return status;
    }

    void reset() override { readyTime_ = 0.0f; child_->reset(); }

private:
    BTNodePtr child_;
    float cooldownTime_;
    float readyTime_ = 0.0f;
};

class ConditionGuardNode : public BTNode {
public:
    using ConditionFn = std::function<bool(const Blackboard&)>;

    ConditionGuardNode(ConditionFn cond, BTNodePtr child)
        : cond_(std::move(cond)), child_(std::move(child)) { name_ = "ConditionGuard"; }

    NodeStatus tick(Blackboard& bb) override {
        if (!cond_(bb)) return NodeStatus::Failure;
        return child_->tick(bb);
    }

    void reset() override { child_->reset(); }

private:
    ConditionFn cond_;
    BTNodePtr child_;
};

class AlwaysSucceedNode : public BTNode {
public:
    explicit AlwaysSucceedNode(BTNodePtr child) : child_(std::move(child)) { name_ = "AlwaysSucceed"; }

    NodeStatus tick(Blackboard& bb) override {
        child_->tick(bb);
        return NodeStatus::Success;
    }

    void reset() override { child_->reset(); }

private:
    BTNodePtr child_;
};

class UntilFailNode : public BTNode {
public:
    explicit UntilFailNode(BTNodePtr child) : child_(std::move(child)) { name_ = "UntilFail"; }

    NodeStatus tick(Blackboard& bb) override {
        auto status = child_->tick(bb);
        if (status == NodeStatus::Failure) return NodeStatus::Success;
        return NodeStatus::Running;
    }

    void reset() override { child_->reset(); }

private:
    BTNodePtr child_;
};

// ═══════════════════════════════════════════════════════════════════════
// BEHAVIOR TREE
// ═══════════════════════════════════════════════════════════════════════

class BehaviorTree {
public:
    BehaviorTree() = default;
    explicit BehaviorTree(BTNodePtr root) : root_(std::move(root)) {}

    void setRoot(BTNodePtr root) { root_ = std::move(root); }

    NodeStatus tick(Blackboard& bb) {
        if (!root_) return NodeStatus::Failure;
        return root_->tick(bb);
    }

    void reset() {
        if (root_) root_->reset();
    }

private:
    BTNodePtr root_;
};

// ═══════════════════════════════════════════════════════════════════════
// BUILDER (Fluent API)
// ═══════════════════════════════════════════════════════════════════════

class BehaviorTreeBuilder {
public:
    BehaviorTreeBuilder& selector(const std::string& name = "Selector") {
        auto node = std::make_unique<SelectorNode>(name);
        pushComposite(std::move(node));
        return *this;
    }

    BehaviorTreeBuilder& sequence(const std::string& name = "Sequence") {
        auto node = std::make_unique<SequenceNode>(name);
        pushComposite(std::move(node));
        return *this;
    }

    BehaviorTreeBuilder& parallel(int successThreshold, const std::string& name = "Parallel") {
        auto node = std::make_unique<ParallelNode>(successThreshold, name);
        pushComposite(std::move(node));
        return *this;
    }

    BehaviorTreeBuilder& action(const std::string& name,
                                ActionNode::ActionFn fn) {
        addLeaf(std::make_unique<ActionNode>(name, std::move(fn)));
        return *this;
    }

    BehaviorTreeBuilder& condition(const std::string& name,
                                   ConditionNode::ConditionFn fn) {
        addLeaf(std::make_unique<ConditionNode>(name, std::move(fn)));
        return *this;
    }

    BehaviorTreeBuilder& inverter() {
        decoratorStack_.push_back(DecoratorType::Inverter);
        return *this;
    }

    BehaviorTreeBuilder& end() {
        if (compositeStack_.size() > 1) {
            auto child = std::move(compositeStack_.back());
            compositeStack_.pop_back();
            addToCurrentComposite(std::move(child));
        }
        return *this;
    }

    BehaviorTree build() {
        while (compositeStack_.size() > 1) {
            end();
        }
        BehaviorTree tree;
        if (!compositeStack_.empty()) {
            tree.setRoot(std::move(compositeStack_.front()));
        }
        compositeStack_.clear();
        return tree;
    }

private:
    enum class DecoratorType { Inverter };

    void pushComposite(BTNodePtr node) {
        compositeStack_.push_back(std::move(node));
    }

    void addLeaf(BTNodePtr leaf) {
        BTNodePtr node = std::move(leaf);
        // Apply pending decorators
        while (!decoratorStack_.empty()) {
            auto dec = decoratorStack_.back();
            decoratorStack_.pop_back();
            if (dec == DecoratorType::Inverter) {
                node = std::make_unique<InverterNode>(std::move(node));
            }
        }
        addToCurrentComposite(std::move(node));
    }

    void addToCurrentComposite(BTNodePtr child) {
        if (compositeStack_.empty()) {
            compositeStack_.push_back(std::move(child));
            return;
        }
        auto* raw = compositeStack_.back().get();
        if (auto* seq = dynamic_cast<SequenceNode*>(raw)) {
            seq->addChild(std::move(child));
        } else if (auto* sel = dynamic_cast<SelectorNode*>(raw)) {
            sel->addChild(std::move(child));
        } else if (auto* par = dynamic_cast<ParallelNode*>(raw)) {
            par->addChild(std::move(child));
        }
    }

    std::vector<BTNodePtr> compositeStack_;
    std::vector<DecoratorType> decoratorStack_;
};

} // namespace npc
