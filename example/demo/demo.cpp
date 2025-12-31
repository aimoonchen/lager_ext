// at_lens.cpp
// Implementation of lager::lenses::at demo (Scheme 3)
#include "lager_ext/value.h"
#include "lager_ext/builders.h"
#include "lager_ext/serialization.h"
#include "lager_ext/static_path.h"
#include "lager_ext/lager_lens.h"
#include "lager_ext/editor_engine.h"
#include "lager_ext/string_path.h"

#include <lager/lenses/at.hpp>
#include <iostream>
#include <iomanip>
namespace lager_ext {
// ============================================================
// Demo function for lager::lenses::at with Value
//
// Note: No additional functions are needed here!
// Value already implements the container interface (at, set, count, size)
// so lager::lenses::at works out of the box.
// ============================================================
void demo_at_lens()
{
    using namespace lager::lenses;

    std::cout << "\n=== Scheme 3: lager::lenses::at with Value Demo ===\n\n";

    // Use common test data
    Value data = create_sample_data();

    std::cout << "Data structure:\n";
    print_value(data, "", 1);

    // -------------------------------------------------------
    // Test 1: Simple single-level access
    // -------------------------------------------------------
    std::cout << "\n--- Test 1: Single-level at() ---\n";

    auto config_lens = at(std::string{ "config" });
    auto config_opt = lager::view(config_lens, data);  // returns optional<Value>

    if (config_opt.has_value()) {
        std::cout << "data.at(\"config\") = " << value_to_string(*config_opt) << "\n";
    }
    else {
        std::cout << "data.at(\"config\") = (not found)\n";
    }

    // -------------------------------------------------------
    // Test 2: Nested access (config.theme)
    // -------------------------------------------------------
    std::cout << "\n--- Test 2: Nested access ---\n";

    auto config_result = lager::view(at(std::string{ "config" }), data);
    if (config_result.has_value()) {
        auto theme_result = lager::view(at(std::string{ "theme" }), *config_result);
        if (theme_result.has_value()) {
            std::cout << "config.theme = " << value_to_string(*theme_result) << "\n";
        }
    }

    // -------------------------------------------------------
    // Test 3: Array access (users[0])
    // -------------------------------------------------------
    std::cout << "\n--- Test 3: Array access ---\n";

    auto users_result = lager::view(at(std::string{ "users" }), data);
    if (users_result.has_value()) {
        auto first_user_result = lager::view(at(size_t{ 0 }), *users_result);
        if (first_user_result.has_value()) {
            std::cout << "users[0] = " << value_to_string(*first_user_result) << "\n";

            auto name_result = lager::view(at(std::string{ "name" }), *first_user_result);
            if (name_result.has_value()) {
                std::cout << "users[0].name = " << value_to_string(*name_result) << "\n";
            }
        }
    }

    // -------------------------------------------------------
    // Test 4: Set operation
    // -------------------------------------------------------
    std::cout << "\n--- Test 4: Set operation ---\n";

    auto config_val = lager::view(at(std::string{ "config" }), data);
    if (config_val.has_value()) {
        // Update version inside config
        Value new_config = lager::set(at(std::string{ "version" }), *config_val,
            std::make_optional(Value{ 3 }));

        // Update config in root
        Value new_data = lager::set(at(std::string{ "config" }), data,
            std::make_optional(new_config));

        // Verify
        auto verify = lager::view(at(std::string{ "config" }), new_data);
        if (verify.has_value()) {
            auto ver = lager::view(at(std::string{ "version" }), *verify);
            if (ver.has_value()) {
                std::cout << "After set: config.version = " << value_to_string(*ver) << "\n";
            }
        }
    }

    // -------------------------------------------------------
    // Test 5: Non-existent key access
    // -------------------------------------------------------
    std::cout << "\n--- Test 5: Non-existent key access ---\n";

    auto nonexistent = lager::view(at(std::string{ "nonexistent" }), data);
    if (nonexistent.has_value()) {
        std::cout << "data.nonexistent = " << value_to_string(*nonexistent) << "\n";
    }
    else {
        std::cout << "data.nonexistent = (not found, optional is empty)\n";
    }

    // -------------------------------------------------------
    // Summary
    // -------------------------------------------------------
    std::cout << "\n--- Summary ---\n";
    std::cout << "By implementing at(), set(), count(), size() on Value:\n";
    std::cout << "  1. Can use lager::lenses::at directly\n";
    std::cout << "  2. No need for custom Path, key_lens(), index_lens()\n";
    std::cout << "  3. Returns optional<Value> for safe access\n";
    std::cout << "  4. Works with both string keys and numeric indices\n";
    std::cout << "\nTrade-offs:\n";
    std::cout << "  - Nested access requires chaining optionals\n";
    std::cout << "  - Custom path_lens provides more ergonomic API for deep paths\n";
    std::cout << "\n=== Demo End ===\n\n";
}

void demo_lager_lens()
{
    std::cout << "\n=== Scheme 2: lager::lens<Value, Value> Demo ===\n\n";

    // Use common test data
    Value data = create_sample_data();

    std::cout << "Data structure:\n";
    print_value(data, "", 1);

    // Test lager_path_lens with lager::view
    std::cout << "\n--- Test 1: GET using lager::view ---\n";
    Path name_path = { std::string{"users"}, size_t{0}, std::string{"name"} };
    auto lens = lager_path_lens(name_path);

    std::cout << "Path: " << path_to_string(name_path) << "\n";
    std::cout << "lager::view(lens, data) = " << value_to_string(lager::view(lens, data)) << "\n";

    // Test lager::set
    std::cout << "\n--- Test 2: SET using lager::set ---\n";
    Value updated = lager::set(lens, data, Value{ std::string{"Alicia"} });
    std::cout << "After lager::set(lens, data, \"Alicia\"):\n";
    std::cout << "New value: " << value_to_string(lager::view(lens, updated)) << "\n";

    // Test lager::over
    std::cout << "\n--- Test 3: OVER using lager::over ---\n";
    Path age_path = { std::string{"users"}, size_t{1}, std::string{"age"} };
    auto age_lens = lager_path_lens(age_path);

    std::cout << "Original age: " << value_to_string(lager::view(age_lens, data)) << "\n";
    Value incremented = lager::over(age_lens, data, [](Value v) {
        if (auto* n = v.get_if<int>()) {
            return Value{ *n + 5 };
        }
        return v;
        });
    std::cout << "After lager::over +5: " << value_to_string(lager::view(age_lens, incremented)) << "\n";

    // Test composition
    std::cout << "\n--- Test 4: Composition with zug::comp ---\n";
    LagerValueLens config_version = zug::comp(lager_key_lens("config"), lager_key_lens("version"));
    std::cout << "config.version = " << value_to_string(lager::view(config_version, data)) << "\n";

    // Compare with static_path_lens (compile-time known path)
    std::cout << "\n--- Test 5: static_path_lens (compile-time) ---\n";
    auto static_lens = static_path_lens("users", 0, "name");
    std::cout << "static_path_lens(\"users\", 0, \"name\") = "
        << value_to_string(lager::view(static_lens, data)) << "\n";

    // Test cache (access same path multiple times)
    std::cout << "\n--- Test 6: Lens Cache Demo ---\n";
    clear_lens_cache();

    for (int i = 0; i < 5; ++i) {
        auto lens_again = lager_path_lens(name_path);
        lager::view(lens_again, data);
    }

    auto cache_stats = get_lens_cache_stats();
    std::cout << "Cache stats after 5 accesses to same path:\n";
    std::cout << "  Hits: " << cache_stats.hits << "\n";
    std::cout << "  Misses: " << cache_stats.misses << "\n";
    std::cout << "  Hit rate: " << (cache_stats.hit_rate * 100.0) << "%\n";
    std::cout << "  Cache size: " << cache_stats.size << "/" << cache_stats.capacity << "\n";

    std::cout << "\n=== Demo End ===\n\n";
}

void demo_string_path()
{
    std::cout << "\n=== String Path API Demo ===\n\n";

    // Create test data:
    // {
    //   "users": [
    //     { "name": "Alice", "profile": { "city": "Beijing", "tags/skills": ["c++", "rust"] } },
    //     { "name": "Bob", "profile": { "city": "Shanghai" } }
    //   ],
    //   "config": { "version": 1, "theme~mode": "dark" }
    // }

    // Build inner structures first
    ValueVector alice_tags;
    alice_tags = alice_tags.push_back(immer::box<Value>{"c++"});
    alice_tags = alice_tags.push_back(immer::box<Value>{"rust"});

    ValueMap alice_profile;
    alice_profile = alice_profile.set("city", immer::box<Value>{"Beijing"});
    alice_profile = alice_profile.set("tags/skills", immer::box<Value>{alice_tags});  // key with '/'

    ValueMap alice;
    alice = alice.set("name", immer::box<Value>{"Alice"});
    alice = alice.set("profile", immer::box<Value>{alice_profile});

    ValueMap bob_profile;
    bob_profile = bob_profile.set("city", immer::box<Value>{"Shanghai"});

    ValueMap bob;
    bob = bob.set("name", immer::box<Value>{"Bob"});
    bob = bob.set("profile", immer::box<Value>{bob_profile});

    ValueVector users;
    users = users.push_back(immer::box<Value>{alice});
    users = users.push_back(immer::box<Value>{bob});

    ValueMap config;
    config = config.set("version", immer::box<Value>{1});
    config = config.set("theme~mode", immer::box<Value>{"dark"});  // key with '~'

    ValueMap root;
    root = root.set("users", immer::box<Value>{users});
    root = root.set("config", immer::box<Value>{config});

    Value data{ root };

    std::cout << "Data structure:\n";
    print_value(data, "", 1);

    // --- Test 1: Basic path parsing ---
    std::cout << "\n--- Test 1: String Path Parsing ---\n";

    std::vector<std::string> test_paths = {
        "",                           // root
        "/users",                     // simple key
        "/users/0",                   // array index
        "/users/0/name",              // nested path
        "/users/0/profile/city",      // deep nesting
        "/config/theme~0mode",        // ~ escape: ~0 -> ~
        "/users/0/profile/tags~1skills",  // / escape: ~1 -> /
        "/users/0/profile/tags~1skills/0" // array in escaped key
    };

    for (const auto& path_str : test_paths) {
        auto path = parse_string_path(path_str);
        auto round_trip = path_to_string_path(path);
        std::cout << "  \"" << path_str << "\" -> Path{";
        for (size_t i = 0; i < path.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::visit([](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    std::cout << "\"" << v << "\"";
                }
                else {
                    std::cout << v;
                }
                }, path[i]);
        }
        std::cout << "} -> \"" << round_trip << "\"\n";
    }

    // --- Test 2: GET operations ---
    std::cout << "\n--- Test 2: GET by String Path ---\n";

    std::cout << "  get_by_path(\"/users/0/name\") = "
        << value_to_string(get_by_path(data, "/users/0/name")) << "\n";

    std::cout << "  get_by_path(\"/users/1/profile/city\") = "
        << value_to_string(get_by_path(data, "/users/1/profile/city")) << "\n";

    std::cout << "  get_by_path(\"/config/version\") = "
        << value_to_string(get_by_path(data, "/config/version")) << "\n";

    // Access key with special characters (escaped)
    std::cout << "  get_by_path(\"/config/theme~0mode\") = "  // ~0 -> ~
        << value_to_string(get_by_path(data, "/config/theme~0mode")) << "\n";

    std::cout << "  get_by_path(\"/users/0/profile/tags~1skills\") = "  // ~1 -> /
        << value_to_string(get_by_path(data, "/users/0/profile/tags~1skills")) << "\n";

    std::cout << "  get_by_path(\"/users/0/profile/tags~1skills/0\") = "
        << value_to_string(get_by_path(data, "/users/0/profile/tags~1skills/0")) << "\n";

    // Non-existent path
    std::cout << "  get_by_path(\"/nonexistent\") = "
        << value_to_string(get_by_path(data, "/nonexistent")) << "\n";

    // --- Test 3: SET operations ---
    std::cout << "\n--- Test 3: SET by String Path ---\n";

    // Change Alice's name
    Value updated1 = set_by_path(data, "/users/0/name", Value{ std::string{"Alicia"} });
    std::cout << "  After set_by_path(\"/users/0/name\", \"Alicia\"):\n";
    std::cout << "    users[0].name = " << value_to_string(get_by_path(updated1, "/users/0/name")) << "\n";

    // Update config version
    Value updated2 = set_by_path(data, "/config/version", Value{ 2 });
    std::cout << "  After set_by_path(\"/config/version\", 2):\n";
    std::cout << "    config.version = " << value_to_string(get_by_path(updated2, "/config/version")) << "\n";

    // --- Test 4: OVER operations ---
    std::cout << "\n--- Test 4: OVER by String Path ---\n";

    // Increment version
    Value updated3 = over_by_path(data, "/config/version", [](Value v) {
        if (auto* n = v.get_if<int>()) {
            return Value{ *n + 10 };
        }
        return v;
        });
    std::cout << "  After over_by_path(\"/config/version\", n + 10):\n";
    std::cout << "    config.version = " << value_to_string(get_by_path(updated3, "/config/version")) << "\n";

    // --- Test 5: Using with lager ecosystem ---
    std::cout << "\n--- Test 5: Direct lens usage with lager::view/set/over ---\n";

    // Get lens once, reuse multiple times
    auto name_lens = string_path_lens("/users/0/name");

    std::cout << "  lens = string_path_lens(\"/users/0/name\")\n";
    std::cout << "  lager::view(lens, data) = " << value_to_string(lager::view(name_lens, data)) << "\n";

    auto after_set = lager::set(name_lens, data, Value{ std::string{"Alice2"} });
    std::cout << "  lager::set(lens, data, \"Alice2\") -> " << value_to_string(lager::view(name_lens, after_set)) << "\n";

    auto after_over = lager::over(name_lens, data, [](Value v) {
        if (auto* s = v.get_if<std::string>()) {
            return Value{ *s + " (modified)" };
        }
        return v;
        });
    std::cout << "  lager::over(lens, data, fn) -> " << value_to_string(lager::view(name_lens, after_over)) << "\n";

    // --- Summary ---
    std::cout << "\n--- Summary ---\n";
    std::cout << "String Path API provides:\n";
    std::cout << "  1. Familiar path syntax: \"/users/0/name\"\n";
    std::cout << "  2. Escape sequences for special characters (~0 for ~, ~1 for /)\n";
    std::cout << "  3. Convenience functions: get_by_path(), set_by_path(), over_by_path()\n";
    std::cout << "  4. Full lager integration: string_path_lens() returns LagerValueLens\n";
    std::cout << "  5. Immutable operations: all set/over return new Value\n";
    std::cout << "\n=== Demo End ===\n\n";
}

void demo_immer_diff()
{
    std::cout << "\n=== immer::diff Demo ===\n\n";

    // --- immer::vector comparison (manual) ---
    std::cout << "--- immer::vector comparison (manual) ---\n";
    std::cout << "Note: immer::diff does NOT support vector, must compare manually\n\n";

    ValueVector old_vec;
    old_vec = old_vec.push_back(ValueBox{ Value{std::string{"Alice"}} });
    old_vec = old_vec.push_back(ValueBox{ Value{std::string{"Bob"}} });
    old_vec = old_vec.push_back(ValueBox{ Value{std::string{"Charlie"}} });

    ValueVector new_vec;
    new_vec = new_vec.push_back(ValueBox{ Value{std::string{"Alice"}} });
    new_vec = new_vec.push_back(ValueBox{ Value{std::string{"Bobby"}} });
    new_vec = new_vec.push_back(ValueBox{ Value{std::string{"Charlie"}} });
    new_vec = new_vec.push_back(ValueBox{ Value{std::string{"David"}} });

    std::cout << "Old: [Alice, Bob, Charlie]\n";
    std::cout << "New: [Alice, Bobby, Charlie, David]\n\n";

    std::cout << "Manual comparison:\n";

    size_t old_size = old_vec.size();
    size_t new_size = new_vec.size();
    size_t common_size = std::min(old_size, new_size);

    for (size_t i = 0; i < common_size; ++i) {
        const auto& old_box = old_vec[i];
        const auto& new_box = new_vec[i];

        auto* old_str = old_box->get_if<std::string>();
        auto* new_str = new_box->get_if<std::string>();

        if (old_str && new_str) {
            if (old_box == new_box) {
                std::cout << "  [" << i << "] retained: " << *old_str << " (same pointer)\n";
            }
            else if (*old_str == *new_str) {
                std::cout << "  [" << i << "] retained: " << *old_str << " (same value)\n";
            }
            else {
                std::cout << "  [" << i << "] modified: " << *old_str << " -> " << *new_str << "\n";
            }
        }
    }

    for (size_t i = common_size; i < old_size; ++i) {
        if (auto* str = old_vec[i]->get_if<std::string>()) {
            std::cout << "  [" << i << "] removed: " << *str << "\n";
        }
    }

    for (size_t i = common_size; i < new_size; ++i) {
        if (auto* str = new_vec[i]->get_if<std::string>()) {
            std::cout << "  [" << i << "] added: " << *str << "\n";
        }
    }

    // --- immer::map diff ---
    std::cout << "\n--- immer::map diff (using immer::diff) ---\n";

    ValueMap old_map;
    old_map = old_map.set("name", ValueBox{ Value{std::string{"Tom"}} });
    old_map = old_map.set("age", ValueBox{ Value{25} });
    old_map = old_map.set("city", ValueBox{ Value{std::string{"Beijing"}} });

    ValueMap new_map;
    new_map = new_map.set("name", ValueBox{ Value{std::string{"Tom"}} });
    new_map = new_map.set("age", ValueBox{ Value{26} });
    new_map = new_map.set("email", ValueBox{ Value{std::string{"tom@x.com"}} });

    std::cout << "Old: {name: Tom, age: 25, city: Beijing}\n";
    std::cout << "New: {name: Tom, age: 26, email: tom@x.com}\n\n";

    std::cout << "immer::diff results:\n";

    immer::diff(
        old_map,
        new_map,
        [](const auto& removed) {
            std::cout << "  [removed] key=" << removed.first << "\n";
        },
        [](const auto& added) {
            std::cout << "  [added] key=" << added.first << "\n";
        },
        [](const auto& old_kv, const auto& new_kv) {
            if (old_kv.second.get() == new_kv.second.get()) {
                std::cout << "  [retained] key=" << old_kv.first << " (same pointer)\n";
            }
            else {
                std::cout << "  [modified] key=" << old_kv.first << "\n";
            }
        }
    );

    std::cout << "\n=== Demo End ===\n\n";
}

void demo_recursive_diff_collector()
{
    std::cout << "\n=== DiffCollector Demo ===\n\n";

    // Create old state
    ValueMap user1;
    user1 = user1.set("name", ValueBox{ Value{std::string{"Alice"}} });
    user1 = user1.set("age", ValueBox{ Value{25} });

    ValueMap user2;
    user2 = user2.set("name", ValueBox{ Value{std::string{"Bob"}} });
    user2 = user2.set("age", ValueBox{ Value{30} });

    ValueVector users_old;
    users_old = users_old.push_back(ValueBox{ Value{user1} });
    users_old = users_old.push_back(ValueBox{ Value{user2} });

    ValueMap old_root;
    old_root = old_root.set("users", ValueBox{ Value{users_old} });
    old_root = old_root.set("version", ValueBox{ Value{1} });

    Value old_state{ old_root };

    // Create new state (with modifications)
    ValueMap user1_new;
    user1_new = user1_new.set("name", ValueBox{ Value{std::string{"Alice"}} });
    user1_new = user1_new.set("age", ValueBox{ Value{26} });  // modified
    user1_new = user1_new.set("email", ValueBox{ Value{std::string{"alice@x.com"}} }); // added

    ValueMap user3;
    user3 = user3.set("name", ValueBox{ Value{std::string{"Charlie"}} });
    user3 = user3.set("age", ValueBox{ Value{35} });

    ValueVector users_new;
    users_new = users_new.push_back(ValueBox{ Value{user1_new} });
    users_new = users_new.push_back(ValueBox{ Value{user2} });  // unchanged
    users_new = users_new.push_back(ValueBox{ Value{user3} });  // added

    ValueMap new_root;
    new_root = new_root.set("users", ValueBox{ Value{users_new} });
    new_root = new_root.set("version", ValueBox{ Value{2} });  // modified

    Value new_state{ new_root };

    // Print states
    std::cout << "--- Old State ---\n";
    print_value(old_state, "", 1);

    std::cout << "\n--- New State ---\n";
    print_value(new_state, "", 1);

    // Collect diffs (recursive mode - default)
    std::cout << "\n--- Recursive Diff Results ---\n";
    DiffCollector collector;
    collector.diff(old_state, new_state);  // recursive = true (default)
    collector.print_diffs();
    std::cout << "\nDetected " << collector.get_diffs().size() << " change(s)\n";

    // Collect diffs (shallow mode)
    std::cout << "\n--- Shallow Diff Results ---\n";
    collector.diff(old_state, new_state, false);  // recursive = false
    collector.print_diffs();
    std::cout << "\nDetected " << collector.get_diffs().size() << " change(s)\n";

    // Quick check using has_any_difference
    std::cout << "\n--- Quick Difference Check ---\n";
    std::cout << "has_any_difference (recursive): "
        << (has_any_difference(old_state, new_state) ? "true" : "false") << "\n";
    std::cout << "has_any_difference (shallow):   "
        << (has_any_difference(old_state, new_state, false) ? "true" : "false") << "\n";

    std::cout << "\n=== Demo End ===\n\n";
}

void demo_shared_state() {
    std::cout << "\n=== Shared State Demo ===\n\n";
    std::cout << "This demo simulates cross-process state sharing within a single process.\n";
    std::cout << "In real use, Publisher and Subscriber would be in different processes.\n\n";

    const std::string shm_name = "lager_ext_demo";
    const std::size_t shm_size = 1024 * 1024;  // 1MB

    // Create publisher (main process)
    std::cout << "Creating StatePublisher...\n";
    StatePublisher publisher({ shm_name, shm_size, true });

    if (!publisher.is_valid()) {
        std::cout << "Failed to create publisher!\n";
        return;
    }

    // Publish initial state
    Value initial_state = create_sample_data();
    std::cout << "\nPublishing initial state:\n";
    print_value(initial_state, "  ");
    publisher.publish(initial_state);
    std::cout << "Published version: " << publisher.version() << "\n";

    // Create subscriber (child process)
    std::cout << "\nCreating StateSubscriber...\n";
    StateSubscriber subscriber({ shm_name, shm_size, false });

    if (!subscriber.is_valid()) {
        std::cout << "Failed to create subscriber!\n";
        return;
    }

    // Read initial state
    std::cout << "\nSubscriber reading initial state:\n";
    print_value(subscriber.current(), "  ");
    std::cout << "Subscriber version: " << subscriber.version() << "\n";

    // Make a change and publish diff
    std::cout << "\n--- Modifying state (changing Alice's age to 26) ---\n";

    Value modified_state = initial_state;
    // Navigate: users[0].age
    if (auto* users_vec = modified_state.at("users").get_if<ValueVector>()) {
        if (users_vec->size() > 0) {
            Value alice = (*users_vec)[0].get();
            alice = alice.set("age", Value{ 26 });
            auto new_vec = users_vec->set(0, ValueBox{ alice });
            modified_state = modified_state.set("users", Value{ new_vec });
        }
    }

    std::cout << "\nPublishing diff...\n";
    bool used_diff = publisher.publish_diff(initial_state, modified_state);
    std::cout << "Used diff: " << (used_diff ? "yes" : "no (full state was smaller)") << "\n";
    std::cout << "Published version: " << publisher.version() << "\n";

    // Subscriber polls for update
    std::cout << "\nSubscriber polling for update...\n";
    if (subscriber.poll()) {
        std::cout << "Received update! New state:\n";
        print_value(subscriber.current(), "  ");
        std::cout << "Subscriber version: " << subscriber.version() << "\n";
    }
    else {
        std::cout << "No update available.\n";
    }

    // Show statistics
    std::cout << "\n--- Statistics ---\n";
    auto pub_stats = publisher.stats();
    std::cout << "Publisher:\n";
    std::cout << "  Total publishes: " << pub_stats.total_publishes << "\n";
    std::cout << "  Full publishes: " << pub_stats.full_publishes << "\n";
    std::cout << "  Diff publishes: " << pub_stats.diff_publishes << "\n";
    std::cout << "  Total bytes written: " << pub_stats.total_bytes_written << "\n";

    auto sub_stats = subscriber.stats();
    std::cout << "Subscriber:\n";
    std::cout << "  Total updates: " << sub_stats.total_updates << "\n";
    std::cout << "  Full updates: " << sub_stats.full_updates << "\n";
    std::cout << "  Diff updates: " << sub_stats.diff_updates << "\n";
    std::cout << "  Total bytes read: " << sub_stats.total_bytes_read << "\n";
    std::cout << "  Missed updates: " << sub_stats.missed_updates << "\n";

    std::cout << "\n=== Demo Complete ===\n";
}

static std::string widget_type_name(WidgetType type) {
    switch (type) {
    case WidgetType::LineEdit: return "QLineEdit";
    case WidgetType::SpinBox: return "QSpinBox";
    case WidgetType::DoubleSpinBox: return "QDoubleSpinBox";
    case WidgetType::CheckBox: return "QCheckBox";
    case WidgetType::ColorPicker: return "ColorPicker";
    case WidgetType::Slider: return "QSlider";
    case WidgetType::ComboBox: return "QComboBox";
    case WidgetType::Vector3Edit: return "Vector3Edit";
    case WidgetType::FileSelector: return "QFileDialog";
    case WidgetType::ReadOnly: return "QLabel";
    default: return "Unknown";
    }
}

void demo_editor_engine() {
    std::cout << "\n";
    std::cout << "+==============================================================+\n";
    std::cout << "|    Editor-Engine Cross-Process State Management Demo        |\n";
    std::cout << "+==============================================================+\n\n";

    // ===== Step 1: Initialize Engine (Process B) =====
    std::cout << "=== Step 1: Initialize Engine (Process B) ===\n";
    EngineSimulator engine;
    engine.initialize_sample_scene();
    std::cout << "Engine initialized with sample scene.\n";
    engine.print_state();

    // ===== Step 2: Editor Gets Initial State (Process A) =====
    std::cout << "\n=== Step 2: Editor Gets Initial State (Process A) ===\n";
    EditorController editor;

    // Set up effects to notify engine of changes
    editor.set_effects({
        // on_state_changed - send diff to engine
        [&engine](const DiffResult& diff) {
            std::cout << "\n[Editor -> Engine] State changed, sending diff...\n";
            engine.apply_diff(diff);
        },
        // on_selection_changed
        [](const std::string& object_id) {
            std::cout << "[Editor] Selection changed to: " << object_id << "\n";
        }
        });

    SceneState initial_state = engine.get_initial_state();
    editor.initialize(initial_state);
    std::cout << "Editor initialized with " << initial_state.objects.size() << " objects.\n";

    // ===== Step 3: Select an Object for Editing =====
    std::cout << "\n=== Step 3: Select Object for Editing ===\n";
    // SelectObject is a SystemAction - won't be recorded to undo history
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"light_sun"} });

    const SceneObject* selected = editor.get_selected_object();
    if (selected) {
        std::cout << "Selected: " << selected->id << " (Type: " << selected->type << ")\n";
        std::cout << "Current data:\n";
        print_value(selected->data, "  ", 1);
    }

    // ===== Step 4: Generate Qt UI Bindings =====
    std::cout << "\n=== Step 4: Generate Qt UI Bindings ===\n";
    if (selected) {
        auto bindings = generate_property_bindings(editor, *selected);
        std::cout << "Generated " << bindings.size() << " property bindings:\n";

        for (const auto& binding : bindings) {
            std::cout << "  - " << binding.meta.display_name
                << " (" << binding.property_path << ")"
                << " -> " << widget_type_name(binding.meta.widget_type);

            if (binding.meta.range) {
                std::cout << " [" << binding.meta.range->min_value
                    << " - " << binding.meta.range->max_value << "]";
            }

            // Show current value
            Value current = binding.getter();
            std::cout << " = " << value_to_string(current) << "\n";
        }
    }

    // ===== Step 5: Edit Property (simulating Qt UI interaction) =====
    std::cout << "\n=== Step 5: Edit Property (Qt UI Simulation) ===\n";
    std::cout << "Changing light intensity from 1.5 to 2.0...\n";
    editor.set_property("intensity", Value{ 2.0 });

    selected = editor.get_selected_object();
    if (selected) {
        std::cout << "Updated data:\n";
        print_value(selected->data, "  ", 1);
    }

    // ===== Step 6: Edit Another Property =====
    std::cout << "\n=== Step 6: Edit Another Property ===\n";
    std::cout << "Changing light color to #FF0000...\n";
    editor.set_property("color", Value{ std::string{"#FF0000"} });

    // ===== Step 7: Undo/Redo Demo =====
    std::cout << "\n=== Step 7: Undo/Redo Demo ===\n";
    std::cout << "Can undo: " << (editor.can_undo() ? "yes" : "no") << "\n";
    std::cout << "Can redo: " << (editor.can_redo() ? "yes" : "no") << "\n";

    std::cout << "\nPerforming UNDO...\n";
    editor.undo();

    selected = editor.get_selected_object();
    if (selected) {
        Value color = editor.get_property("color");
        std::cout << "Color after undo: " << value_to_string(color) << "\n";
    }

    std::cout << "\nPerforming REDO...\n";
    editor.redo();

    selected = editor.get_selected_object();
    if (selected) {
        Value color = editor.get_property("color");
        std::cout << "Color after redo: " << value_to_string(color) << "\n";
    }

    // ===== Step 8: Switch to Different Object =====
    std::cout << "\n=== Step 8: Switch to Different Object ===\n";
    // SelectObject is a SystemAction - won't be recorded to undo history
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"cube_1"} });

    selected = editor.get_selected_object();
    if (selected) {
        std::cout << "Now editing: " << selected->id << " (Type: " << selected->type << ")\n";
        std::cout << "Properties:\n";

        auto bindings = generate_property_bindings(editor, *selected);
        for (const auto& binding : bindings) {
            Value current = binding.getter();
            std::cout << "  " << binding.meta.display_name << ": "
                << value_to_string(current) << "\n";
        }
    }

    // ===== Summary =====
    std::cout << "\n";
    std::cout << "+==============================================================+\n";
    std::cout << "|                     Demo Summary                            |\n";
    std::cout << "+==============================================================+\n";
    std::cout << "| 1. Engine creates scene objects with reflection data        |\n";
    std::cout << "| 2. Editor receives initial state from Engine                |\n";
    std::cout << "| 3. User selects object -> Qt UI is generated from metadata |\n";
    std::cout << "| 4. User edits property -> State updated via lager reducer   |\n";
    std::cout << "| 5. State diff is sent to Engine for application             |\n";
    std::cout << "| 6. Undo/Redo works through state history stack              |\n";
    std::cout << "+==============================================================+\n\n";
}

void demo_property_editing() {
    std::cout << "\n=== Property Editing Demo ===\n\n";

    EngineSimulator engine;
    engine.initialize_sample_scene();

    EditorController editor;
    editor.initialize(engine.get_initial_state());

    // Select the camera object - SystemAction, won't be recorded to undo history
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"camera_main"} });

    const SceneObject* camera = editor.get_selected_object();
    if (!camera) {
        std::cout << "Failed to select camera!\n";
        return;
    }

    std::cout << "Editing: " << camera->id << "\n";
    std::cout << "Initial position.y: " << value_to_string(editor.get_property("position.y")) << "\n";

    // Simulate UI editing - change position Y
    std::cout << "\nSimulating slider change: position.y -> 10.0\n";
    editor.set_property("position.y", Value{ 10.0 });

    std::cout << "New position.y: " << value_to_string(editor.get_property("position.y")) << "\n";

    // Batch update - UserAction, will be recorded to undo history
    std::cout << "\nSimulating batch update (drag 3D gizmo):\n";
    editor.dispatch(actions::SetProperties{ payloads::SetProperties{std::map<std::string, Value>{
        {"position.x", Value{5.0}},
        {"position.y", Value{7.5}},
        {"position.z", Value{-15.0}}
    }} });

    std::cout << "New position: ("
        << value_to_string(editor.get_property("position.x")) << ", "
        << value_to_string(editor.get_property("position.y")) << ", "
        << value_to_string(editor.get_property("position.z")) << ")\n";

    std::cout << "\n=== Demo End ===\n\n";
}

void demo_undo_redo() {
    std::cout << "\n=== Undo/Redo Demo ===\n\n";

    EngineSimulator engine;
    engine.initialize_sample_scene();

    EditorController editor;
    editor.set_effects({
        [](const DiffResult& diff) {
            std::cout << "  [Diff] " << diff.modified.size() << " modifications\n";
        },
        nullptr
        });

    editor.initialize(engine.get_initial_state());
    // SelectObject is a SystemAction - won't be recorded to undo history
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"light_sun"} });

    std::cout << "Initial intensity: "
        << value_to_string(editor.get_property("intensity")) << "\n";

    // Make several changes
    std::cout << "\n--- Making changes ---\n";

    std::cout << "Set intensity = 2.0\n";
    editor.set_property("intensity", Value{ 2.0 });

    std::cout << "Set intensity = 3.0\n";
    editor.set_property("intensity", Value{ 3.0 });

    std::cout << "Set intensity = 4.0\n";
    editor.set_property("intensity", Value{ 4.0 });

    std::cout << "\nCurrent intensity: "
        << value_to_string(editor.get_property("intensity")) << "\n";
    std::cout << "Undo stack size: " << editor.get_model().undo_stack.size() << "\n";
    std::cout << "Redo stack size: " << editor.get_model().redo_stack.size() << "\n";

    // Undo all changes
    std::cout << "\n--- Undoing all changes ---\n";
    while (editor.can_undo()) {
        editor.undo();
        std::cout << "After undo: intensity = "
            << value_to_string(editor.get_property("intensity")) << "\n";
    }

    // Redo all changes
    std::cout << "\n--- Redoing all changes ---\n";
    while (editor.can_redo()) {
        editor.redo();
        std::cout << "After redo: intensity = "
            << value_to_string(editor.get_property("intensity")) << "\n";
    }

    std::cout << "\n=== Demo End ===\n\n";
}

void demo_action_categories() {
    std::cout << "\n";
    std::cout << "+==============================================================+\n";
    std::cout << "|      User Action vs System Action - Undo Filtering Demo     |\n";
    std::cout << "+==============================================================+\n\n";

    EngineSimulator engine;
    engine.initialize_sample_scene();

    EditorController editor;
    editor.initialize(engine.get_initial_state());

    auto print_undo_status = [&editor]() {
        std::cout << "  Undo stack size: " << editor.get_model().undo_stack.size()
            << ", Redo stack size: " << editor.get_model().redo_stack.size() << "\n";
        };

    std::cout << "=== Initial State ===\n";
    print_undo_status();

    // ===== System Actions (should NOT affect undo history) =====
    std::cout << "\n=== System Actions (should NOT be recorded to undo) ===\n";

    std::cout << "\n1. SelectObject (SystemAction) - selecting 'light_sun':\n";
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"light_sun"} });
    print_undo_status();
    std::cout << "   -> Undo stack unchanged (selection is not undoable)\n";

    std::cout << "\n2. SelectObject (SystemAction) - selecting 'cube_1':\n";
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"cube_1"} });
    print_undo_status();
    std::cout << "   -> Undo stack still unchanged\n";

    std::cout << "\n3. LoadObjects (SystemAction) - simulating batch load:\n";
    // Create a test object
    SceneObject test_obj;
    test_obj.id = "loaded_obj_1";
    test_obj.type = "LoadedMesh";
    test_obj.data = MapBuilder()
        .set("name", Value{ std::string{"Loaded Object"} })
        .finish();

    editor.dispatch(actions::LoadObjects{ payloads::LoadObjects{{test_obj}} });
    print_undo_status();
    std::cout << "   -> Undo stack still unchanged (loading is not undoable)\n";
    std::cout << "   -> Object count: " << editor.get_model().scene.objects.size() << "\n";

    // ===== User Actions (SHOULD affect undo history) =====
    std::cout << "\n=== User Actions (SHOULD be recorded to undo) ===\n";

    // First, select an object to edit
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"light_sun"} });

    std::cout << "\n4. SetProperty (UserAction) - changing intensity to 5.0:\n";
    editor.set_property("intensity", Value{ 5.0 });
    print_undo_status();
    std::cout << "   -> Undo stack increased by 1 (user edit is undoable)\n";

    std::cout << "\n5. SetProperty (UserAction) - changing intensity to 8.0:\n";
    editor.set_property("intensity", Value{ 8.0 });
    print_undo_status();
    std::cout << "   -> Undo stack increased by 1 again\n";

    std::cout << "\n6. SetProperties (UserAction) - batch update:\n";
    editor.dispatch(actions::SetProperties{ payloads::SetProperties{std::map<std::string, Value>{
        {"color", Value{std::string{"#00FF00"}}},
        {"enabled", Value{false}}
    }} });
    print_undo_status();
    std::cout << "   -> Undo stack increased by 1 (batch edit is one undoable unit)\n";

    // ===== Mixed Operations Demo =====
    std::cout << "\n=== Mixed Operations - Interleaving User and System Actions ===\n";

    std::cout << "\n7. Switching selection (SystemAction):\n";
    editor.dispatch(actions::SelectObject{ payloads::SelectObject{"cube_1"} });
    print_undo_status();
    std::cout << "   -> Undo stack unchanged\n";

    std::cout << "\n8. SetProperty on new object (UserAction):\n";
    editor.set_property("visible", Value{ false });
    print_undo_status();
    std::cout << "   -> Undo stack increased by 1\n";

    // ===== Undo Demo =====
    std::cout << "\n=== Undo Demo - Only User Actions are reversed ===\n";

    std::cout << "\nUndoing operations:\n";
    int undo_count = 0;
    while (editor.can_undo()) {
        editor.undo();
        undo_count++;
        std::cout << "  Undo #" << undo_count << ": ";
        print_undo_status();
    }

    std::cout << "\nTotal undos performed: " << undo_count << "\n";
    std::cout << "Note: Selection changes and LoadObjects were NOT included in undo!\n";

    // ===== Summary =====
    std::cout << "\n";
    std::cout << "+==============================================================+\n";
    std::cout << "|                     Summary                                 |\n";
    std::cout << "+==============================================================+\n";
    std::cout << "| UserAction (recorded to undo):                              |\n";
    std::cout << "|   - SetProperty, SetProperties, AddObject, RemoveObject     |\n";
    std::cout << "|                                                             |\n";
    std::cout << "| SystemAction (NOT recorded to undo):                        |\n";
    std::cout << "|   - SelectObject, LoadObjects, SyncFromEngine, etc.         |\n";
    std::cout << "|                                                             |\n";
    std::cout << "| Benefits:                                                   |\n";
    std::cout << "|   - Undo history only contains meaningful user edits        |\n";
    std::cout << "|   - Incremental loading won't pollute undo stack            |\n";
    std::cout << "|   - Selection changes don't create unnecessary history      |\n";
    std::cout << "+==============================================================+\n\n";
}

// ============================================================
// Example: Define a schema with static paths
//
// Imagine this is generated from C++ class reflection:
//
// struct User {
//     std::string name;
//     int age;
//     std::string email;
// };
//
// struct AppConfig {
//     std::string title;
//     std::vector<User> users;
//     struct {
//         int width;
//         int height;
//     } window;
// };
// ============================================================

namespace schema {

    using namespace static_path;

    // ============================================================
    // Path definitions - These are compile-time constants
    // ============================================================

    // Root-level paths
    using TitlePath = StaticPath<K<"title">>;
    using UsersPath = StaticPath<K<"users">>;
    using WindowPath = StaticPath<K<"window">>;

    // Window sub-paths
    using WindowWidthPath = StaticPath<K<"window">, K<"width">>;
    using WindowHeightPath = StaticPath<K<"window">, K<"height">>;

    // User paths (parameterized by index)
    template<std::size_t Idx>
    using UserPath = StaticPath<K<"users">, I<Idx>>;

    template<std::size_t Idx>
    using UserNamePath = StaticPath<K<"users">, I<Idx>, K<"name">>;

    template<std::size_t Idx>
    using UserAgePath = StaticPath<K<"users">, I<Idx>, K<"age">>;

    template<std::size_t Idx>
    using UserEmailPath = StaticPath<K<"users">, I<Idx>, K<"email">>;

    // ============================================================
    // Type-safe accessors
    // ============================================================

    struct AppConfigPaths {
        // Singleton paths
        static constexpr auto title() { return TitlePath{}; }
        static constexpr auto users() { return UsersPath{}; }
        static constexpr auto window() { return WindowPath{}; }
        static constexpr auto window_width() { return WindowWidthPath{}; }
        static constexpr auto window_height() { return WindowHeightPath{}; }

        // Indexed user access
        template<std::size_t I>
        struct user {
            static constexpr auto path() { return UserPath<I>{}; }
            static constexpr auto name() { return UserNamePath<I>{}; }
            static constexpr auto age() { return UserAgePath<I>{}; }
            static constexpr auto email() { return UserEmailPath<I>{}; }
        };
    };

} // namespace schema

// ============================================================
// Helper: Create sample data
// ============================================================

static Value create_sample_state() {
    // Create users using Builder API for O(n) construction
    Value user0 = MapBuilder()
        .set("name", Value{ "Alice" })
        .set("age", Value{ 30 })
        .set("email", Value{ "alice@example.com" })
        .finish();

    Value user1 = MapBuilder()
        .set("name", Value{ "Bob" })
        .set("age", Value{ 25 })
        .set("email", Value{ "bob@example.com" })
        .finish();

    Value user2 = MapBuilder()
        .set("name", Value{ "Charlie" })
        .set("age", Value{ 35 })
        .set("email", Value{ "charlie@example.com" })
        .finish();

    // Create users array using Builder API
    Value users = VectorBuilder()
        .push_back(user0)
        .push_back(user1)
        .push_back(user2)
        .finish();

    // Create window config using Builder API
    Value window = MapBuilder()
        .set("width", Value{ 1920 })
        .set("height", Value{ 1080 })
        .finish();

    // Create root state using Builder API
    return MapBuilder()
        .set("title", Value{ "My Application" })
        .set("users", users)
        .set("window", window)
        .finish();
}

// ============================================================
// Demo function implementation
// ============================================================

void demo_static_path() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << " Static Path Lens Demo (Compile-time Paths)\n";
    std::cout << "============================================================\n\n";

    using namespace schema;

    // Create sample state
    Value state = create_sample_state();

    std::cout << "Initial state:\n";
    print_value(state);
    std::cout << "\n";

    // --------------------------------------------------------
    // Demo 1: Basic compile-time path access
    // --------------------------------------------------------
    std::cout << "--- Demo 1: Compile-time Path Access ---\n\n";

    // Get title using static path
    auto title = TitlePath::get(state);
    std::cout << "TitlePath::get(state) = " << value_to_string(title) << "\n";

    // Get window dimensions
    auto width = WindowWidthPath::get(state);
    auto height = WindowHeightPath::get(state);
    std::cout << "WindowWidthPath::get(state) = " << value_to_string(width) << "\n";
    std::cout << "WindowHeightPath::get(state) = " << value_to_string(height) << "\n";

    // Get user data using indexed paths
    auto user0_name = UserNamePath<0>::get(state);
    auto user1_age = UserAgePath<1>::get(state);
    auto user2_email = UserEmailPath<2>::get(state);

    std::cout << "UserNamePath<0>::get(state) = " << value_to_string(user0_name) << "\n";
    std::cout << "UserAgePath<1>::get(state) = " << value_to_string(user1_age) << "\n";
    std::cout << "UserEmailPath<2>::get(state) = " << value_to_string(user2_email) << "\n\n";

    // --------------------------------------------------------
    // Demo 2: Compile-time immutable updates
    // --------------------------------------------------------
    std::cout << "--- Demo 2: Compile-time Immutable Updates ---\n\n";

    // Update title
    Value state2 = TitlePath::set(state, Value{ "Updated App Title" });
    std::cout << "After TitlePath::set(state, \"Updated App Title\"):\n";
    std::cout << "  New title = " << value_to_string(TitlePath::get(state2)) << "\n";
    std::cout << "  Original title = " << value_to_string(TitlePath::get(state)) << "\n\n";

    // Update nested value
    Value state3 = UserAgePath<0>::set(state, Value{ 31 });
    std::cout << "After UserAgePath<0>::set(state, 31):\n";
    std::cout << "  New age = " << value_to_string(UserAgePath<0>::get(state3)) << "\n";
    std::cout << "  Original age = " << value_to_string(UserAgePath<0>::get(state)) << "\n\n";

    // --------------------------------------------------------
    // Demo 3: Using the type-safe accessor pattern
    // --------------------------------------------------------
    std::cout << "--- Demo 3: Type-safe Accessor Pattern ---\n\n";

    // Using AppConfigPaths for cleaner access
    auto title_v2 = AppConfigPaths::title().get(state);
    auto user1_name = AppConfigPaths::user<1>::name().get(state);

    std::cout << "AppConfigPaths::title().get(state) = " << value_to_string(title_v2) << "\n";
    std::cout << "AppConfigPaths::user<1>::name().get(state) = " << value_to_string(user1_name) << "\n\n";

    // --------------------------------------------------------
    // Demo 4: Using the lens directly
    // --------------------------------------------------------
    std::cout << "--- Demo 4: Using Lens Directly ---\n\n";

    // Get the lens object
    auto user0_name_lens = UserNamePath<0>::to_lens();

    // Use it like a regular lens
    auto name1 = user0_name_lens.get(state);
    auto state4 = user0_name_lens.set(state, Value{ "Alicia" });
    auto name2 = user0_name_lens.get(state4);

    std::cout << "user0_name_lens.get(state) = " << value_to_string(name1) << "\n";
    std::cout << "After set to \"Alicia\": " << value_to_string(name2) << "\n\n";

    // --------------------------------------------------------
    // Demo 5: Path metadata
    // --------------------------------------------------------
    std::cout << "--- Demo 5: Path Metadata ---\n\n";

    std::cout << "Path depths (compile-time constants):\n";
    std::cout << "  TitlePath::depth = " << TitlePath::depth << "\n";
    std::cout << "  WindowWidthPath::depth = " << WindowWidthPath::depth << "\n";
    std::cout << "  UserNamePath<0>::depth = " << UserNamePath<0>::depth << "\n\n";

    std::cout << "Convert to runtime path:\n";
    auto runtime_path = UserEmailPath<2>::to_runtime_path();
    std::cout << "  UserEmailPath<2>::to_runtime_path() = "
        << path_to_string(runtime_path) << "\n\n";

    // --------------------------------------------------------
    // Demo 6: Path composition
    // --------------------------------------------------------
    std::cout << "--- Demo 6: Path Composition ---\n\n";

    using namespace static_path;

    // Compose paths using ConcatPathT
    using BasePath = StaticPath<K<"users">, I<0>>;
    using FieldPath = StaticPath<K<"name">>;
    using FullPath = ConcatPathT<BasePath, FieldPath>;

    auto composed_result = FullPath::get(state);
    std::cout << "ConcatPathT<users[0], name>::get(state) = "
        << value_to_string(composed_result) << "\n";

    // Extend path using ExtendPathT
    using ExtendedPath = ExtendPathT<BasePath, K<"age">>;
    auto extended_result = ExtendedPath::get(state);
    std::cout << "ExtendPathT<users[0], age>::get(state) = "
        << value_to_string(extended_result) << "\n\n";

    // --------------------------------------------------------
    // Demo 7: Using macros for path definition
    // --------------------------------------------------------
    std::cout << "--- Demo 7: Using Macros ---\n\n";

    using MacroPath = STATIC_PATH(STATIC_KEY("window"), STATIC_KEY("width"));
    auto macro_result = MacroPath::get(state);
    std::cout << "STATIC_PATH(STATIC_KEY(\"window\"), STATIC_KEY(\"width\"))::get(state) = "
        << value_to_string(macro_result) << "\n\n";

#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    // --------------------------------------------------------
    // Demo 8: JSON Pointer Syntax (C++20 only)
    // --------------------------------------------------------
    std::cout << "--- Demo 8: JSON Pointer Syntax (C++20) ---\n\n";

    // Define paths using JSON Pointer syntax (LiteralPath)
    using TitlePathJP = static_path::LiteralPath<"/title">;
    using UserNamePathJP = static_path::LiteralPath<"/users/0/name">;
    using WindowWidthPathJP = static_path::LiteralPath<"/window/width">;

    // Use them just like regular StaticPath
    auto title_jp = TitlePathJP::get(state);
    auto user0_name_jp = UserNamePathJP::get(state);
    auto width_jp = WindowWidthPathJP::get(state);

    std::cout << "LiteralPath<\"/title\">::get(state) = "
        << value_to_string(title_jp) << "\n";
    std::cout << "LiteralPath<\"/users/0/name\">::get(state) = "
        << value_to_string(user0_name_jp) << "\n";
    std::cout << "LiteralPath<\"/window/width\">::get(state) = "
        << value_to_string(width_jp) << "\n\n";

    // Verify they work the same as manually defined paths
    std::cout << "Verification (should match Demo 1):\n";
    std::cout << "  TitlePathJP::depth = " << TitlePathJP::depth << "\n";
    std::cout << "  UserNamePathJP::depth = " << UserNamePathJP::depth << "\n";

    // Set using JSON Pointer path
    auto state5 = UserNamePathJP::set(state, Value{ "Alice (via JSON Pointer)" });
    std::cout << "  After set via JSON Pointer: "
        << value_to_string(UserNamePathJP::get(state5)) << "\n\n";
#else
    std::cout << "--- Demo 8: JSON Pointer Syntax ---\n\n";
    std::cout << "(Requires C++20 - not available in current build)\n\n";
#endif

    // --------------------------------------------------------
    // Summary
    // --------------------------------------------------------
    std::cout << "============================================================\n";
    std::cout << " Static Path Summary\n";
    std::cout << "============================================================\n\n";
    std::cout << "Advantages:\n";
    std::cout << "  1. Zero runtime overhead for path construction\n";
    std::cout << "  2. Compile-time type checking of path structure\n";
    std::cout << "  3. IDE autocomplete for path definitions\n";
    std::cout << "  4. Paths can be reused as type aliases\n";
    std::cout << "  5. Compatible with runtime Path for debugging\n";
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    std::cout << "  6. JSON Pointer syntax for familiar path definitions (C++20)\n";
#endif
    std::cout << "\n";

    std::cout << "Use cases:\n";
    std::cout << "  - C++ class reflection with known schema\n";
    std::cout << "  - Configuration files with fixed structure\n";
    std::cout << "  - Database ORM-like access patterns\n";
    std::cout << "  - Any scenario where paths are known at compile time\n\n";

    std::cout << "Syntax comparison:\n";
    std::cout << "  Manual:       StaticPath<K<\"users\">, I<0>, K<\"name\">>\n";
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
    std::cout << "  JSON Pointer: LiteralPath<\"/users/0/name\"> (C++20)\n";
#endif
    std::cout << "\n";
}
} // namespace lager_ext
