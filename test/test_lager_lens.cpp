// test_lager_lens.cpp - Tests for LagerLens system
// Module 7: LagerLens, PathLens, ZoomedValue related interfaces

#include <catch2/catch_all.hpp>
#include <lager_ext/lager_lens.h>
#include <lager_ext/value.h>

#include <string>

using namespace lager_ext;
using namespace std::string_view_literals;

// ============================================================
// Helper Functions
// ============================================================

ImmerValue create_test_state() {
    // Create a nested structure for testing:
    // {
    //   "users": [
    //     {"name": "Alice", "age": 30},
    //     {"name": "Bob", "age": 25}
    //   ],
    //   "settings": {
    //     "theme": "dark",
    //     "volume": 80
    //   }
    // }
    return ImmerValue::map({
        {"users", ImmerValue::vector({
            ImmerValue::map({{"name", ImmerValue{"Alice"}}, {"age", ImmerValue{30}}}),
            ImmerValue::map({{"name", ImmerValue{"Bob"}}, {"age", ImmerValue{25}}})
        })},
        {"settings", ImmerValue::map({
            {"theme", ImmerValue{"dark"}},
            {"volume", ImmerValue{80}}
        })}
    });
}

// ============================================================
// key_lens Tests
// ============================================================

TEST_CASE("key_lens basic operations", "[lens][key]") {
    auto state = create_test_state();
    auto settings_lens = key_lens("settings");
    
    SECTION("get") {
        auto settings = lager::view(settings_lens, state);
        REQUIRE(settings.is_map());
        REQUIRE(settings.at("theme").as<std::string>() == "dark");
    }
    
    SECTION("set") {
        auto new_settings = ImmerValue::map({{"theme", ImmerValue{"light"}}});
        auto new_state = lager::set(settings_lens, state, new_settings);
        
        auto updated = lager::view(settings_lens, new_state);
        REQUIRE(updated.at("theme").as<std::string>() == "light");
    }
    
    SECTION("non-existent key returns null") {
        auto missing_lens = key_lens("missing");
        auto result = lager::view(missing_lens, state);
        REQUIRE(result.is_null());
    }
}

// ============================================================
// index_lens Tests
// ============================================================

TEST_CASE("index_lens basic operations", "[lens][index]") {
    auto users = ImmerValue::vector({
        ImmerValue{1},
        ImmerValue{2},
        ImmerValue{3}
    });
    
    SECTION("get") {
        auto lens = index_lens(0);
        auto result = lager::view(lens, users);
        REQUIRE(result.as<int>() == 1);
    }
    
    SECTION("set") {
        auto lens = index_lens(1);
        auto new_users = lager::set(lens, users, ImmerValue{100});
        
        REQUIRE(new_users.at(1).as<int>() == 100);
        REQUIRE(new_users.at(0).as<int>() == 1); // Others unchanged
        REQUIRE(new_users.at(2).as<int>() == 3);
    }
    
    SECTION("out of bounds returns null") {
        auto lens = index_lens(100);
        auto result = lager::view(lens, users);
        REQUIRE(result.is_null());
    }
}

// ============================================================
// PathLens Tests
// ============================================================

TEST_CASE("PathLens construction", "[lens][pathlens]") {
    SECTION("default construction") {
        PathLens lens;
        REQUIRE(lens.empty());
        REQUIRE(lens.depth() == 0);
    }
    
    SECTION("from Path") {
        Path path;
        path.push_back("users");
        path.push_back(std::size_t{0});
        
        PathLens lens{path};
        REQUIRE(lens.depth() == 2);
    }
}

TEST_CASE("PathLens navigation", "[lens][pathlens]") {
    SECTION("key method") {
        PathLens lens;
        auto result = lens.key("users").key("name");
        REQUIRE(result.depth() == 2);
    }
    
    SECTION("index method") {
        PathLens lens;
        auto result = lens.key("users").index(0);
        REQUIRE(result.depth() == 2);
    }
    
    SECTION("operator/ with string") {
        PathLens lens;
        auto result = lens / "users" / "name";
        REQUIRE(result.depth() == 2);
    }
    
    SECTION("operator/ with index") {
        PathLens lens;
        auto result = lens / "users" / 0 / "name";
        REQUIRE(result.depth() == 3);
    }
}

TEST_CASE("PathLens get/set", "[lens][pathlens]") {
    auto state = create_test_state();
    
    SECTION("get nested value") {
        PathLens lens = root / "users" / 0 / "name";
        auto name = lens.get(state);
        REQUIRE(name.as<std::string>() == "Alice");
    }
    
    SECTION("set nested value") {
        PathLens lens = root / "users" / 0 / "name";
        auto new_state = lens.set(state, ImmerValue{"Charlie"});
        
        auto updated = lens.get(new_state);
        REQUIRE(updated.as<std::string>() == "Charlie");
        
        // Original unchanged (immutable)
        REQUIRE(lens.get(state).as<std::string>() == "Alice");
    }
    
    SECTION("over with function") {
        PathLens lens = root / "users" / 0 / "age";
        auto new_state = lens.over(state, [](const ImmerValue& v) {
            return ImmerValue{v.as<int>() + 1};
        });
        
        REQUIRE(lens.get(new_state).as<int>() == 31);
        REQUIRE(lens.get(state).as<int>() == 30);
    }
}

TEST_CASE("PathLens to_string", "[lens][pathlens]") {
    PathLens lens = root / "users" / 0 / "name";
    std::string str = lens.to_string();
    REQUIRE(str == ".users[0].name");
}

TEST_CASE("PathLens concat", "[lens][pathlens]") {
    PathLens base = root / "users" / 0;
    PathLens sub = root / "profile" / "email";
    
    PathLens combined = base.concat(sub);
    REQUIRE(combined.depth() == 4);
}

TEST_CASE("PathLens parent", "[lens][pathlens]") {
    PathLens lens = root / "users" / 0 / "name";
    PathLens parent = lens.parent();
    
    REQUIRE(parent.depth() == 2);
    REQUIRE(lens.depth() == 3); // Original unchanged
}

TEST_CASE("PathLens comparison", "[lens][pathlens]") {
    PathLens lens1 = root / "users" / 0;
    PathLens lens2 = root / "users" / 0;
    PathLens lens3 = root / "users" / 1;
    
    REQUIRE(lens1 == lens2);
    REQUIRE(lens1 != lens3);
}

// ============================================================
// ZoomedValue Tests
// ============================================================

TEST_CASE("ZoomedValue construction", "[lens][zoom]") {
    auto state = create_test_state();
    
    SECTION("at root") {
        ZoomedValue zoomed{state};
        REQUIRE(zoomed.at_root());
        REQUIRE(zoomed.depth() == 0);
    }
    
    SECTION("with path") {
        Path path;
        path.push_back("users");
        
        ZoomedValue zoomed{state, path};
        REQUIRE_FALSE(zoomed.at_root());
        REQUIRE(zoomed.depth() == 1);
    }
}

TEST_CASE("ZoomedValue navigation", "[lens][zoom]") {
    auto state = create_test_state();
    ZoomedValue root_zoom{state};
    
    SECTION("key navigation") {
        auto users = root_zoom.key("users");
        REQUIRE(users.depth() == 1);
    }
    
    SECTION("index navigation") {
        auto first = root_zoom / "users" / 0;
        REQUIRE(first.depth() == 2);
    }
    
    SECTION("chained navigation") {
        auto name = root_zoom / "users" / 0 / "name";
        REQUIRE(name.depth() == 3);
    }
}

TEST_CASE("ZoomedValue get", "[lens][zoom]") {
    auto state = create_test_state();
    ZoomedValue root_zoom{state};
    
    SECTION("get primitive") {
        auto name = (root_zoom / "users" / 0 / "name").get();
        REQUIRE(name.as<std::string>() == "Alice");
    }
    
    SECTION("get container") {
        auto users = (root_zoom / "users").get();
        REQUIRE(users.is_vector());
        REQUIRE(users.size() == 2);
    }
    
    SECTION("dereference operator") {
        auto name = *(root_zoom / "users" / 0 / "name");
        REQUIRE(name.as<std::string>() == "Alice");
    }
}

TEST_CASE("ZoomedValue set", "[lens][zoom]") {
    auto state = create_test_state();
    ZoomedValue root_zoom{state};
    
    SECTION("set returns new root") {
        auto name_zoom = root_zoom / "users" / 0 / "name";
        auto new_state = name_zoom.set(ImmerValue{"Charlie"});
        
        // New state has updated value
        ZoomedValue new_zoom{new_state};
        REQUIRE((new_zoom / "users" / 0 / "name").get().as<std::string>() == "Charlie");
        
        // Original unchanged
        REQUIRE(name_zoom.get().as<std::string>() == "Alice");
    }
}

TEST_CASE("ZoomedValue over", "[lens][zoom]") {
    auto state = create_test_state();
    ZoomedValue root_zoom{state};
    
    SECTION("over with transform function") {
        auto age_zoom = root_zoom / "users" / 0 / "age";
        auto new_state = age_zoom.over([](const ImmerValue& v) {
            return ImmerValue{v.as<int>() * 2};
        });
        
        ZoomedValue new_zoom{new_state};
        REQUIRE((new_zoom / "users" / 0 / "age").get().as<int>() == 60);
    }
}

TEST_CASE("ZoomedValue parent", "[lens][zoom]") {
    auto state = create_test_state();
    auto deep = ZoomedValue{state} / "users" / 0 / "name";
    
    auto parent = deep.parent();
    REQUIRE(parent.depth() == 2);
    
    // Parent's parent
    auto grandparent = parent.parent();
    REQUIRE(grandparent.depth() == 1);
}

TEST_CASE("ZoomedValue with_root", "[lens][zoom]") {
    auto state = create_test_state();
    auto name_zoom = ZoomedValue{state} / "users" / 0 / "name";
    
    // Set value and get new state
    auto new_state = name_zoom.set(ImmerValue{"Charlie"});
    
    // Create new zoom with same path but new root
    auto updated_zoom = name_zoom.with_root(new_state);
    REQUIRE(updated_zoom.get().as<std::string>() == "Charlie");
}

TEST_CASE("ZoomedValue to_lens", "[lens][zoom]") {
    auto state = create_test_state();
    auto zoom = ZoomedValue{state} / "settings" / "theme";
    
    PathLens lens = zoom.to_lens();
    REQUIRE(lens.depth() == 2);
    
    // Lens should work the same way
    auto result = lens.get(state);
    REQUIRE(result.as<std::string>() == "dark");
}

// ============================================================
// zoom() Factory Function Tests
// ============================================================

TEST_CASE("zoom factory functions", "[lens][zoom]") {
    auto state = create_test_state();
    
    SECTION("zoom at root") {
        auto z = zoom(state);
        REQUIRE(z.at_root());
    }
    
    SECTION("zoom with path") {
        Path path;
        path.push_back("users");
        
        auto z = zoom(state, path);
        REQUIRE(z.depth() == 1);
    }
    
    SECTION("zoom with variadic args") {
        auto z = zoom(state, "users", std::size_t{0}, "name");
        REQUIRE(z.depth() == 3);
        REQUIRE(z.get().as<std::string>() == "Alice");
    }
}

// ============================================================
// Convenience Functions Tests
// ============================================================

TEST_CASE("get_at convenience function", "[lens][convenience]") {
    auto state = create_test_state();
    
    SECTION("get nested value") {
        auto name = get_at(state, "users", std::size_t{0}, "name");
        REQUIRE(name.as<std::string>() == "Alice");
    }
}

TEST_CASE("set_at convenience function", "[lens][convenience]") {
    auto state = create_test_state();
    
    SECTION("set nested value") {
        auto new_state = set_at(state, ImmerValue{"Charlie"}, "users", std::size_t{0}, "name");
        
        auto name = get_at(new_state, "users", std::size_t{0}, "name");
        REQUIRE(name.as<std::string>() == "Charlie");
    }
}

TEST_CASE("over_at convenience function", "[lens][convenience]") {
    auto state = create_test_state();
    
    SECTION("over nested value") {
        auto new_state = over_at(state, 
            [](const ImmerValue& v) { return ImmerValue{v.as<int>() + 10}; },
            "users", std::size_t{0}, "age");
        
        auto age = get_at(new_state, "users", std::size_t{0}, "age");
        REQUIRE(age.as<int>() == 40);
    }
}

// ============================================================
// make_path Helper Tests
// ============================================================

TEST_CASE("make_path function", "[lens][path]") {
    SECTION("with mixed elements") {
        PathLens lens = make_path("users", std::size_t{0}, "name");
        REQUIRE(lens.depth() == 3);
    }
}

// ============================================================
// get_at_path / set_at_path Tests
// ============================================================

TEST_CASE("get_at_path function", "[lens][path]") {
    auto state = create_test_state();
    
    SECTION("get with PathView") {
        PathView path = {"users"sv, std::size_t{0}, "name"sv};
        auto result = get_at_path(state, path);
        REQUIRE(result.as<std::string>() == "Alice");
    }
    
    SECTION("get with Path") {
        Path path;
        path.push_back("settings");
        path.push_back("theme");
        
        auto result = get_at_path(state, path);
        REQUIRE(result.as<std::string>() == "dark");
    }
}

TEST_CASE("set_at_path function", "[lens][path]") {
    auto state = create_test_state();
    
    SECTION("set with Path") {
        Path path;
        path.push_back("settings");
        path.push_back("volume");
        
        auto new_state = set_at_path(state, path, ImmerValue{50});
        
        auto volume = get_at_path(new_state, path);
        REQUIRE(volume.as<int>() == 50);
    }
}

// ============================================================
// Safe Access Tests
// ============================================================

TEST_CASE("get_at_path_safe", "[lens][safe]") {
    auto state = create_test_state();
    
    SECTION("success case") {
        Path path;
        path.push_back("users");
        path.push_back(std::size_t{0});
        path.push_back("name");
        
        auto result = get_at_path_safe(state, path);
        REQUIRE(result.success);
        REQUIRE(result.value.as<std::string>() == "Alice");
        REQUIRE(result.error_code == PathErrorCode::Success);
    }
    
    SECTION("missing key") {
        Path path;
        path.push_back("missing");
        
        auto result = get_at_path_safe(state, path);
        REQUIRE_FALSE(result.success);
        REQUIRE(result.error_code == PathErrorCode::KeyNotFound);
    }
    
    SECTION("index out of range") {
        Path path;
        path.push_back("users");
        path.push_back(std::size_t{100});
        
        auto result = get_at_path_safe(state, path);
        REQUIRE_FALSE(result.success);
        REQUIRE(result.error_code == PathErrorCode::IndexOutOfRange);
    }
    
    SECTION("get_or returns default on failure") {
        Path path;
        path.push_back("missing");
        
        auto result = get_at_path_safe(state, path);
        auto value = result.get_or(ImmerValue{"default"});
        REQUIRE(value.as<std::string>() == "default");
    }
}

// ============================================================
// Lens Cache Tests
// ============================================================

TEST_CASE("Lens cache operations", "[lens][cache]") {
    // Clear cache first
    clear_lens_cache();
    
    SECTION("clear_lens_cache") {
        // Create some lenses to populate cache
        Path path;
        path.push_back("test");
        auto lens = lager_path_lens(path);
        
        clear_lens_cache();
        
        auto stats = get_lens_cache_stats();
        REQUIRE(stats.size == 0);
    }
    
    SECTION("get_lens_cache_stats") {
        clear_lens_cache();
        
        // Create lens twice - should hit cache second time
        Path path;
        path.push_back("test");
        
        lager_path_lens(path); // Miss
        lager_path_lens(path); // Hit
        
        auto stats = get_lens_cache_stats();
        REQUIRE(stats.misses >= 1);
        // Note: hits may vary depending on implementation
    }
}
