#pragma once

#include "../core/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>
#include <iostream>

namespace npc {

// Forward declaration
class NPC;

struct DialogOption {
    std::string text;
    std::string nextNodeId;
    std::function<bool(float reputation, float mood)> condition; // show/hide
    std::function<void()> effect;  // side effect when chosen
    int minReputation = -100;  // simple reputation gate
};

struct DialogNode {
    std::string id;
    std::string speakerText;
    std::vector<DialogOption> options;
    bool isTerminal = false;

    // Text variants based on relationship
    std::string friendlyText;   // reputation > 50
    std::string hostileText;    // reputation < -50

    std::string getText(float reputation) const {
        if (reputation > 50.0f && !friendlyText.empty()) return friendlyText;
        if (reputation < -50.0f && !hostileText.empty()) return hostileText;
        return speakerText;
    }
};

class DialogTree {
public:
    DialogTree() = default;
    explicit DialogTree(std::string id) : treeId_(std::move(id)) {}

    void addNode(DialogNode node) {
        if (nodes_.empty()) rootId_ = node.id;
        nodes_[node.id] = std::move(node);
    }

    void setRoot(const std::string& id) { rootId_ = id; }

    const DialogNode* getNode(const std::string& id) const {
        auto it = nodes_.find(id);
        return (it != nodes_.end()) ? &it->second : nullptr;
    }

    const std::string& rootId() const { return rootId_; }
    const std::string& treeId() const { return treeId_; }

private:
    std::string treeId_;
    std::string rootId_;
    std::map<std::string, DialogNode> nodes_;
};

class DialogSystem {
public:
    void addTree(const std::string& id, DialogTree tree) {
        trees_[id] = std::move(tree);
    }

    bool startDialog(const std::string& treeId, [[maybe_unused]] float reputation = 0.0f) {
        auto it = trees_.find(treeId);
        if (it == trees_.end()) return false;

        activeTree_ = treeId;
        currentNodeId_ = it->second.rootId();
        return true;
    }

    const DialogNode* currentNode() const {
        if (!activeTree_) return nullptr;
        auto it = trees_.find(*activeTree_);
        if (it == trees_.end()) return nullptr;
        return it->second.getNode(currentNodeId_);
    }

    std::vector<std::pair<int, const DialogOption*>> getAvailableOptions(
            float reputation, float mood) const {
        std::vector<std::pair<int, const DialogOption*>> result;
        auto* node = currentNode();
        if (!node) return result;

        for (int i = 0; i < static_cast<int>(node->options.size()); ++i) {
            const auto& opt = node->options[i];
            bool show = true;
            if (opt.condition) show = opt.condition(reputation, mood);
            if (reputation < opt.minReputation) show = false;
            if (show) result.push_back({i, &opt});
        }
        return result;
    }

    bool selectOption(int index) {
        auto* node = currentNode();
        if (!node || index < 0 || index >= static_cast<int>(node->options.size()))
            return false;

        const auto& opt = node->options[index];
        if (opt.effect) opt.effect();
        lastChoice_ = opt.text;

        if (opt.nextNodeId.empty() || opt.nextNodeId == "END") {
            endDialog();
            return true;
        }

        currentNodeId_ = opt.nextNodeId;
        return true;
    }

    void endDialog() {
        activeTree_ = std::nullopt;
        currentNodeId_.clear();
    }

    bool isInDialog() const { return activeTree_.has_value(); }
    const std::string& lastChoice() const { return lastChoice_; }

    // Print current dialog state to console
    void printCurrent(const std::string& speakerName, float reputation, float mood) const {
        auto* node = currentNode();
        if (!node) return;

        std::cout << "  " << speakerName << ": \""
                  << node->getText(reputation) << "\"\n";

        auto options = getAvailableOptions(reputation, mood);
        for (const auto& [idx, opt] : options) {
            std::cout << "    [" << idx + 1 << "] " << opt->text << "\n";
        }
    }

private:
    std::map<std::string, DialogTree> trees_;
    std::optional<std::string> activeTree_;
    std::string currentNodeId_;
    std::string lastChoice_;
};

} // namespace npc
