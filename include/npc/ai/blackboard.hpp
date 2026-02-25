#pragma once

#include <any>
#include <string>
#include <unordered_map>
#include <optional>

namespace npc {

class Blackboard {
public:
    template<typename T>
    void set(const std::string& key, const T& value) {
        data_[key] = value;
    }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) return std::nullopt;
        try {
            return std::any_cast<T>(it->second);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    template<typename T>
    T getOr(const std::string& key, const T& defaultValue) const {
        auto val = get<T>(key);
        return val.value_or(defaultValue);
    }

    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

    void remove(const std::string& key) {
        data_.erase(key);
    }

    void clear() {
        data_.clear();
    }

private:
    std::unordered_map<std::string, std::any> data_;
};

} // namespace npc
