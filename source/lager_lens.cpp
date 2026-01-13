// lager_lens.cpp
// Implementation of lager::lens<Value, Value> scheme

#include <lager_ext/lager_lens.h>
#include <lager_ext/path_utils.h>

#include <list>
#include <unordered_map>
#include <zug/compose.hpp>

namespace lager_ext {

namespace {

// Hash function for Path
struct PathHash {
    std::size_t operator()(const Path& path) const {
        std::size_t hash = 0;
        for (const auto& elem : path) {
            std::size_t elem_hash = std::visit(
                [](const auto& v) -> std::size_t {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, std::string_view>) {
                        return std::hash<std::string_view>{}(v);
                    } else {
                        return std::hash<std::size_t>{}(v);
                    }
                },
                elem);
            // Combine hashes using FNV-1a style mixing
            hash ^= elem_hash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

// ============================================================
// Simple LRU Cache for lens objects (single-threaded)
// ============================================================
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {}

    const Value* get(const Key& key) {
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return nullptr;
        }
        // Move to front (most recently used)
        lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
        return &it->second->second;
    }

    void put(const Key& key, Value value) {
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            it->second->second = std::move(value);
            lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
            return;
        }

        // Evict if at capacity
        if (cache_map_.size() >= capacity_) {
            auto& lru = lru_list_.back();
            cache_map_.erase(lru.first);
            lru_list_.pop_back();
        }

        // Insert new entry at front
        lru_list_.emplace_front(key, std::move(value));
        cache_map_[key] = lru_list_.begin();
    }

    void clear() {
        cache_map_.clear();
        lru_list_.clear();
        hits_ = misses_ = 0;
    }

    std::size_t size() const { return cache_map_.size(); }

    void record_hit() { ++hits_; }
    void record_miss() { ++misses_; }

    struct Stats {
        std::size_t hits = 0;
        std::size_t misses = 0;
        std::size_t size = 0;
        std::size_t capacity = 0;
        double hit_rate() const {
            auto total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    Stats stats() const { return Stats{hits_, misses_, cache_map_.size(), capacity_}; }

private:
    using ListType = std::list<std::pair<Key, Value>>;
    using MapType = std::unordered_map<Key, typename ListType::iterator, Hash>;

    std::size_t capacity_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
    ListType lru_list_;
    MapType cache_map_;
};

// Global lens cache with default capacity of 256
LRUCache<Path, LagerValueLens, PathHash>& get_lens_cache() {
    static LRUCache<Path, LagerValueLens, PathHash> cache(256);
    return cache;
}

LagerValueLens build_path_lens_uncached(const Path& path) {
    if (path.empty()) {
        return zug::identity;
    }

    // Single lens: capture path once, traverse directly
    return lager::lenses::getset(
        [path](const Value& root) -> Value { return get_at_path(root, path); },
        [path](Value root, Value new_val) -> Value { return set_at_path(root, path, std::move(new_val)); });
}

} // anonymous namespace

// Build lens from path using lager::lens<Value, Value>
// Uses LRU cache for frequently accessed paths
LagerValueLens lager_path_lens(const Path& path) {
    auto& cache = get_lens_cache();

    if (const auto* cached = cache.get(path)) {
        cache.record_hit();
        return *cached;
    }

    cache.record_miss();
    LagerValueLens lens = build_path_lens_uncached(path);
    cache.put(path, lens);

    return lens;
}

void clear_lens_cache() {
    get_lens_cache().clear();
}

LensCacheStats get_lens_cache_stats() {
    auto stats = get_lens_cache().stats();
    return LensCacheStats{stats.hits, stats.misses, stats.size, stats.capacity, stats.hit_rate()};
}

// ============================================================
// Structured Error Reporting
// ============================================================

namespace {

std::string get_error_message(PathErrorCode code, const PathElement& elem, std::size_t index) {
    auto elem_str = std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string_view>) {
                return "key \"" + std::string{v} + "\"";
            } else {
                return "index " + std::to_string(v);
            }
        },
        elem);

    switch (code) {
    case PathErrorCode::Success:
        return "Success";
    case PathErrorCode::KeyNotFound:
        return "Key not found: " + elem_str + " at path position " + std::to_string(index);
    case PathErrorCode::IndexOutOfRange:
        return "Index out of range: " + elem_str + " at path position " + std::to_string(index);
    case PathErrorCode::TypeMismatch:
        return "Type mismatch: expected container at " + elem_str + " (path position " + std::to_string(index) + ")";
    case PathErrorCode::NullValue:
        return "Null value encountered at " + elem_str + " (path position " + std::to_string(index) + ")";
    case PathErrorCode::EmptyPath:
        return "Empty path";
    default:
        return "Unknown error";
    }
}

std::pair<Value, PathErrorCode> try_get_element(const Value& current, const PathElement& elem) {
    return std::visit(
        [&current](const auto& key) -> std::pair<Value, PathErrorCode> {
            using T = std::decay_t<decltype(key)>;

            if (current.is_null()) {
                return {Value{}, PathErrorCode::NullValue};
            }

            if constexpr (std::is_same_v<T, std::string_view>) {
                if (auto* map = current.get_if<ValueMap>()) {
                    // Convert string_view to string for map lookup
                    if (auto found = map->find(std::string{key}); found != nullptr) {
                        return {found->get(), PathErrorCode::Success};
                    }
                    return {Value{}, PathErrorCode::KeyNotFound};
                }
                return {Value{}, PathErrorCode::TypeMismatch};
            } else {
                if (auto* vec = current.get_if<ValueVector>()) {
                    if (key < vec->size()) {
                        return {(*vec)[key].get(), PathErrorCode::Success};
                    }
                    return {Value{}, PathErrorCode::IndexOutOfRange};
                }
                return {Value{}, PathErrorCode::TypeMismatch};
            }
        },
        elem);
}

} // anonymous namespace

PathAccessResult get_at_path_safe(const Value& root, const Path& path) {
    PathAccessResult result;
    result.value = root;

    if (path.empty()) {
        result.success = true;
        result.error_code = PathErrorCode::EmptyPath;
        result.error_message = "Empty path (returns root)";
        return result;
    }

    Value current = root;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto& elem = path[i];
        auto [next_val, error_code] = try_get_element(current, elem);

        if (error_code != PathErrorCode::Success) {
            result.success = false;
            result.error_code = error_code;
            result.error_message = get_error_message(error_code, elem, i);
            result.failed_at_index = i;
            result.value = Value{};
            return result;
        }

        result.resolved_path.push_back(elem);
        current = std::move(next_val);
    }

    result.success = true;
    result.error_code = PathErrorCode::Success;
    result.value = std::move(current);
    return result;
}

PathAccessResult set_at_path_safe(const Value& root, const Path& path, Value new_val) {
    PathAccessResult result;

    if (path.empty()) {
        result.success = true;
        result.error_code = PathErrorCode::EmptyPath;
        result.error_message = "Empty path (replaces root)";
        result.value = std::move(new_val);
        return result;
    }

    Value current = root;
    Path parent_path;

    for (std::size_t i = 0; i + 1 < path.size(); ++i) {
        const auto& elem = path[i];
        auto [next_val, error_code] = try_get_element(current, elem);

        if (error_code != PathErrorCode::Success) {
            result.success = false;
            result.error_code = error_code;
            result.error_message = get_error_message(error_code, elem, i);
            result.failed_at_index = i;
            result.value = root;
            return result;
        }

        result.resolved_path.push_back(elem);
        parent_path.push_back(elem);
        current = std::move(next_val);
    }

    const auto& last_elem = path.back();
    bool can_set = std::visit(
        [&current](const auto& key) -> bool {
            using T = std::decay_t<decltype(key)>;
            if constexpr (std::is_same_v<T, std::string_view>) {
                return current.is_null() || current.get_if<ValueMap>() != nullptr;
            } else {
                return current.get_if<ValueVector>() != nullptr;
            }
        },
        last_elem);

    if (!can_set) {
        result.success = false;
        result.error_code = PathErrorCode::TypeMismatch;
        result.error_message = get_error_message(PathErrorCode::TypeMismatch, last_elem, path.size() - 1);
        result.failed_at_index = path.size() - 1;
        result.value = root;
        return result;
    }

    result.success = true;
    result.error_code = PathErrorCode::Success;
    result.resolved_path = path;
    result.value = set_at_path(root, path, std::move(new_val));
    return result;
}

// ============================================================
// PathLens implementation
// ============================================================

Value PathLens::get(const Value& root) const {
    return get_at_path(root, path_);
}

Value PathLens::set(const Value& root, Value new_val) const {
    return set_at_path(root, path_, std::move(new_val));
}

} // namespace lager_ext