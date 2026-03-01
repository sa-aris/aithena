#pragma once

#include "../core/types.hpp"
#include <string>
#include <map>
#include <vector>
#include <optional>
#include <algorithm>
#include <cmath>

namespace npc {

struct Item {
    ItemId id;
    std::string name;
    ItemCategory category;
    float basePrice = 10.0f;
    float weight = 1.0f;
};

class Inventory {
public:
    explicit Inventory(float maxWeight = 100.0f, float gold = 50.0f)
        : maxWeight_(maxWeight), gold_(gold) {}

    bool addItem(ItemId id, int quantity = 1) {
        items_[id] += quantity;
        return true;
    }

    bool removeItem(ItemId id, int quantity = 1) {
        auto it = items_.find(id);
        if (it == items_.end() || it->second < quantity) return false;
        it->second -= quantity;
        if (it->second <= 0) items_.erase(it);
        return true;
    }

    bool hasItem(ItemId id, int quantity = 1) const {
        auto it = items_.find(id);
        return it != items_.end() && it->second >= quantity;
    }

    int getQuantity(ItemId id) const {
        auto it = items_.find(id);
        return (it != items_.end()) ? it->second : 0;
    }

    float gold() const { return gold_; }
    void addGold(float amount) { gold_ += amount; }
    bool spendGold(float amount) {
        if (gold_ < amount) return false;
        gold_ -= amount;
        return true;
    }

    int totalItems() const {
        int total = 0;
        for (const auto& [id, qty] : items_) total += qty;
        return total;
    }

    const std::map<ItemId, int>& items() const { return items_; }
    float maxWeight() const { return maxWeight_; }

private:
    std::map<ItemId, int> items_;
    float maxWeight_;
    float gold_;
};

class TradeSystem {
public:
    Inventory inventory{200.0f, 100.0f};
    float buyMarkup = 1.3f;    // 30% markup when NPC sells
    float sellMarkdown = 0.6f; // 40% markdown when NPC buys
    float scarcityMod_ = 1.0f;
    float relDiscountMod_ = 1.0f;

    void applyPersonality(float buyMarkupMul, float sellMarkdownMul,
                          float scarcityMul, float relDiscountMul) {
        buyMarkup *= buyMarkupMul;
        sellMarkdown *= sellMarkdownMul;
        scarcityMod_ = scarcityMul;
        relDiscountMod_ = relDiscountMul;
    }

    void registerItem(Item item) {
        itemDB_[item.id] = std::move(item);
    }

    const Item* getItemInfo(ItemId id) const {
        auto it = itemDB_.find(id);
        return (it != itemDB_.end()) ? &it->second : nullptr;
    }

    float getPrice(ItemId id, bool playerBuying) const {
        auto* item = getItemInfo(id);
        if (!item) return 0.0f;

        float price = item->basePrice;

        // Supply/demand modifier
        auto modIt = priceModifiers_.find(id);
        if (modIt != priceModifiers_.end()) {
            price *= modIt->second;
        }

        // Scarcity: less stock -> higher price (personality-modulated)
        int stock = inventory.getQuantity(id);
        if (playerBuying && stock <= 2) {
            price *= (1.0f + 0.5f * scarcityMod_);
        }

        // Apply markup/markdown
        if (playerBuying) {
            price *= buyMarkup;
        } else {
            price *= sellMarkdown;
        }

        // Relationship discount (personality-modulated)
        price *= (1.0f - relationshipDiscount_ * relDiscountMod_);

        return std::round(price * 100.0f) / 100.0f;
    }

    bool canAfford(float price, const Inventory& buyerInv) const {
        return buyerInv.gold() >= price;
    }

    struct TradeResult {
        bool success;
        float price;
        std::string message;
    };

    // Player buys from NPC
    TradeResult sell(ItemId id, int quantity, Inventory& buyerInv) {
        if (!inventory.hasItem(id, quantity)) {
            return {false, 0.0f, "Out of stock."};
        }

        float totalPrice = getPrice(id, true) * quantity;
        if (!canAfford(totalPrice, buyerInv)) {
            return {false, totalPrice, "Not enough gold."};
        }

        inventory.removeItem(id, quantity);
        buyerInv.addItem(id, quantity);
        buyerInv.spendGold(totalPrice);
        inventory.addGold(totalPrice);

        auto* item = getItemInfo(id);
        std::string name = item ? item->name : "item";
        return {true, totalPrice, "Sold " + std::to_string(quantity) + "x " + name +
                " for " + std::to_string(static_cast<int>(totalPrice)) + " gold."};
    }

    // Player sells to NPC
    TradeResult buy(ItemId id, int quantity, Inventory& sellerInv) {
        if (!sellerInv.hasItem(id, quantity)) {
            return {false, 0.0f, "You don't have that item."};
        }

        float totalPrice = getPrice(id, false) * quantity;
        if (inventory.gold() < totalPrice) {
            return {false, totalPrice, "I can't afford that right now."};
        }

        sellerInv.removeItem(id, quantity);
        inventory.addItem(id, quantity);
        sellerInv.addGold(totalPrice);
        inventory.spendGold(totalPrice);

        auto* item = getItemInfo(id);
        std::string name = item ? item->name : "item";
        return {true, totalPrice, "Bought " + std::to_string(quantity) + "x " + name +
                " for " + std::to_string(static_cast<int>(totalPrice)) + " gold."};
    }

    // Barter: trade items directly
    bool barter(ItemId offered, int offeredQty,
                ItemId requested, int requestedQty,
                Inventory& other) {
        float offeredValue = getPrice(offered, false) * offeredQty;
        float requestedValue = getPrice(requested, true) * requestedQty;

        if (offeredValue < requestedValue * 0.8f) return false; // not fair enough

        if (!other.hasItem(offered, offeredQty)) return false;
        if (!inventory.hasItem(requested, requestedQty)) return false;

        other.removeItem(offered, offeredQty);
        inventory.addItem(offered, offeredQty);
        inventory.removeItem(requested, requestedQty);
        other.addItem(requested, requestedQty);
        return true;
    }

    void updatePrices() {
        for (auto& [id, item] : itemDB_) {
            int stock = inventory.getQuantity(id);
            float modifier = 1.0f;
            if (stock == 0) modifier = 2.0f;
            else if (stock <= 2) modifier = 1.5f;
            else if (stock <= 5) modifier = 1.1f;
            else if (stock >= 20) modifier = 0.8f;
            priceModifiers_[id] = modifier;
        }
    }

    void setRelationshipDiscount(float discount) {
        relationshipDiscount_ = std::clamp(discount, 0.0f, 0.3f);
    }

    const std::map<ItemId, Item>& itemDB() const { return itemDB_; }

private:
    std::map<ItemId, Item> itemDB_;
    std::map<ItemId, float> priceModifiers_;
    float relationshipDiscount_ = 0.0f;
};

} // namespace npc
