// test_diff.cpp - Tests for diff system
// Module 8: Diff system related interfaces

#include <catch2/catch_all.hpp>
#include <lager_ext/value_diff.h>
#include <lager_ext/value.h>

#include <string>

using namespace lager_ext;

// ============================================================
// Helper Functions
// ============================================================

ImmerValue create_state_v1() {
    return ImmerValue::map({
        {"name", ImmerValue{"Alice"}},
        {"age", ImmerValue{30}},
        {"items", ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{2},
            ImmerValue{3}
        })}
    });
}

ImmerValue create_state_v2() {
    return ImmerValue::map({
        {"name", ImmerValue{"Bob"}},     // Changed
        {"age", ImmerValue{30}},          // Same
        {"items", ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{2},
            ImmerValue{4}                 // Changed
        })},
        {"email", ImmerValue{"bob@test.com"}}  // Added
    });
}

// ============================================================
// DiffEntry Tests
// ============================================================

TEST_CASE("DiffEntry construction", "[diff][entry]") {
    Path path;
    path.push_back("test");
    
    SECTION("Add entry") {
        DiffEntry entry{DiffEntry::Type::Add, path, ImmerValue{}, ImmerValue{42}};
        REQUIRE(entry.type == DiffEntry::Type::Add);
        REQUIRE(entry.get_new().as<int>() == 42);
    }
    
    SECTION("Remove entry") {
        DiffEntry entry{DiffEntry::Type::Remove, path, ImmerValue{42}, ImmerValue{}};
        REQUIRE(entry.type == DiffEntry::Type::Remove);
        REQUIRE(entry.get_old().as<int>() == 42);
    }
    
    SECTION("Change entry") {
        DiffEntry entry{DiffEntry::Type::Change, path, ImmerValue{1}, ImmerValue{2}};
        REQUIRE(entry.type == DiffEntry::Type::Change);
        REQUIRE(entry.get_old().as<int>() == 1);
        REQUIRE(entry.get_new().as<int>() == 2);
    }
    
    SECTION("value() accessor") {
        // For Add/Change, returns new_value
        DiffEntry add_entry{DiffEntry::Type::Add, path, ImmerValue{}, ImmerValue{42}};
        REQUIRE(add_entry.value().as<int>() == 42);
        
        // For Remove, returns old_value
        DiffEntry remove_entry{DiffEntry::Type::Remove, path, ImmerValue{100}, ImmerValue{}};
        REQUIRE(remove_entry.value().as<int>() == 100);
    }
}

// ============================================================
// DiffEntryCollector Tests
// ============================================================

TEST_CASE("DiffEntryCollector basic diff", "[diff][collector]") {
    auto old_state = create_state_v1();
    auto new_state = create_state_v2();
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    SECTION("has changes") {
        REQUIRE(collector.has_changes());
    }
    
    SECTION("get_diffs returns entries") {
        const auto& diffs = collector.get_diffs();
        REQUIRE_FALSE(diffs.empty());
    }
}

TEST_CASE("DiffEntryCollector detects additions", "[diff][collector][add]") {
    auto old_state = ImmerValue::map({{"a", ImmerValue{1}}});
    auto new_state = ImmerValue::map({
        {"a", ImmerValue{1}},
        {"b", ImmerValue{2}}
    });
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    const auto& diffs = collector.get_diffs();
    
    bool found_add = false;
    for (const auto& entry : diffs) {
        if (entry.type == DiffEntry::Type::Add && entry.get_new().as<int>() == 2) {
            found_add = true;
            break;
        }
    }
    
    REQUIRE(found_add);
}

TEST_CASE("DiffEntryCollector detects removals", "[diff][collector][remove]") {
    auto old_state = ImmerValue::map({
        {"a", ImmerValue{1}},
        {"b", ImmerValue{2}}
    });
    auto new_state = ImmerValue::map({{"a", ImmerValue{1}}});
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    const auto& diffs = collector.get_diffs();
    
    bool found_remove = false;
    for (const auto& entry : diffs) {
        if (entry.type == DiffEntry::Type::Remove && entry.get_old().as<int>() == 2) {
            found_remove = true;
            break;
        }
    }
    
    REQUIRE(found_remove);
}

TEST_CASE("DiffEntryCollector detects changes", "[diff][collector][change]") {
    auto old_state = ImmerValue::map({{"value", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"value", ImmerValue{99}}});
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    const auto& diffs = collector.get_diffs();
    
    REQUIRE(diffs.size() == 1);
    REQUIRE(diffs[0].type == DiffEntry::Type::Change);
    REQUIRE(diffs[0].get_old().as<int>() == 1);
    REQUIRE(diffs[0].get_new().as<int>() == 99);
}

TEST_CASE("DiffEntryCollector no changes", "[diff][collector]") {
    auto state = ImmerValue::map({{"a", ImmerValue{1}}});
    
    DiffEntryCollector collector;
    collector.diff(state, state);
    
    REQUIRE_FALSE(collector.has_changes());
    REQUIRE(collector.get_diffs().empty());
}

TEST_CASE("DiffEntryCollector clear", "[diff][collector]") {
    auto old_state = ImmerValue::map({{"a", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"a", ImmerValue{2}}});
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    REQUIRE(collector.has_changes());
    
    collector.clear();
    
    REQUIRE_FALSE(collector.has_changes());
    REQUIRE(collector.get_diffs().empty());
}

TEST_CASE("DiffEntryCollector recursive mode", "[diff][collector]") {
    auto old_state = ImmerValue::map({
        {"nested", ImmerValue::map({{"value", ImmerValue{1}}})}
    });
    auto new_state = ImmerValue::map({
        {"nested", ImmerValue::map({{"value", ImmerValue{2}}})}
    });
    
    SECTION("recursive = true (default)") {
        DiffEntryCollector collector;
        collector.diff(old_state, new_state, true);
        
        REQUIRE(collector.is_recursive());
        REQUIRE(collector.has_changes());
        
        // Should find the deep change
        const auto& diffs = collector.get_diffs();
        bool found_nested_change = false;
        for (const auto& entry : diffs) {
            if (entry.path.size() >= 2) {  // nested/value
                found_nested_change = true;
                break;
            }
        }
        REQUIRE(found_nested_change);
    }
    
    SECTION("recursive = false") {
        DiffEntryCollector collector;
        collector.diff(old_state, new_state, false);
        
        REQUIRE_FALSE(collector.is_recursive());
        // Should still detect the change at the container level
        REQUIRE(collector.has_changes());
    }
}

TEST_CASE("DiffEntryCollector as_value_tree", "[diff][collector][tree]") {
    auto old_state = ImmerValue::map({{"a", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"a", ImmerValue{2}}});
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    auto tree = collector.as_value_tree();
    
    REQUIRE(tree.is_map());
}

TEST_CASE("DiffEntryCollector entry extraction", "[diff][collector]") {
    auto old_state = ImmerValue::map({{"x", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"x", ImmerValue{2}}});
    
    DiffEntryCollector collector;
    collector.diff(old_state, new_state);
    
    auto tree = collector.as_value_tree();
    
    // The tree stores pointers as uint64_t
    auto* node = tree.get_if<BoxedValueMap>();
    REQUIRE(node != nullptr);
    
    auto* x_node = node->get().find("x");
    REQUIRE(x_node != nullptr);
    
    REQUIRE(DiffEntryCollector::is_entry_node(*x_node));
    
    auto* entry = DiffEntryCollector::get_entry(*x_node);
    REQUIRE(entry != nullptr);
    REQUIRE(entry->type == DiffEntry::Type::Change);
}

// ============================================================
// DiffValueCollector Tests
// ============================================================

TEST_CASE("DiffValueCollector basic diff", "[diff][value_collector]") {
    auto old_state = create_state_v1();
    auto new_state = create_state_v2();
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    
    SECTION("has changes") {
        REQUIRE(collector.has_changes());
    }
    
    SECTION("get returns ImmerValue") {
        const ImmerValue& result = collector.get();
        REQUIRE(result.is_map());
    }
}

TEST_CASE("DiffValueCollector no changes", "[diff][value_collector]") {
    auto state = ImmerValue::map({{"a", ImmerValue{1}}});
    
    DiffValueCollector collector;
    collector.diff(state, state);
    
    REQUIRE_FALSE(collector.has_changes());
}

TEST_CASE("DiffValueCollector clear", "[diff][value_collector]") {
    auto old_state = ImmerValue::map({{"a", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"a", ImmerValue{2}}});
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    REQUIRE(collector.has_changes());
    
    collector.clear();
    
    REQUIRE_FALSE(collector.has_changes());
}

TEST_CASE("DiffValueCollector is_diff_node", "[diff][value_collector]") {
    auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    
    const auto& tree = collector.get();
    
    // Navigate to the diff node
    auto key_node = tree.at("key");
    REQUIRE(DiffValueCollector::is_diff_node(key_node));
}

TEST_CASE("DiffValueCollector get_diff_type", "[diff][value_collector]") {
    auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    
    auto key_node = collector.get().at("key");
    auto type = DiffValueCollector::get_diff_type(key_node);
    
    REQUIRE(type == DiffEntry::Type::Change);
}

TEST_CASE("DiffValueCollector get_old/new_value", "[diff][value_collector]") {
    auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    
    auto key_node = collector.get().at("key");
    
    SECTION("get_old_value") {
        auto old_val = DiffValueCollector::get_old_value(key_node);
        REQUIRE(old_val.as<int>() == 1);
    }
    
    SECTION("get_new_value") {
        auto new_val = DiffValueCollector::get_new_value(key_node);
        REQUIRE(new_val.as<int>() == 2);
    }
}

// ============================================================
// DiffNodeView Tests
// ============================================================

TEST_CASE("DiffNodeView parse and access", "[diff][view]") {
    auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
    
    DiffValueCollector collector;
    collector.diff(old_state, new_state);
    
    auto key_node = collector.get().at("key");
    
    DiffNodeView view;
    bool parsed = view.parse(key_node);
    
    REQUIRE(parsed);
    REQUIRE(view.type == DiffEntry::Type::Change);
    REQUIRE(view.has_old());
    REQUIRE(view.has_new());
    REQUIRE(view.get_old().as<int>() == 1);
    REQUIRE(view.get_new().as<int>() == 2);
}

TEST_CASE("DiffNodeView parse invalid", "[diff][view]") {
    // Not a diff node
    ImmerValue not_diff{42};
    
    DiffNodeView view;
    bool parsed = view.parse(not_diff);
    
    REQUIRE_FALSE(parsed);
}

TEST_CASE("DiffNodeView value() accessor", "[diff][view]") {
    SECTION("for Change type") {
        auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
        auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
        
        DiffValueCollector collector;
        collector.diff(old_state, new_state);
        
        DiffNodeView view;
        view.parse(collector.get().at("key"));
        
        // Change returns new_value
        REQUIRE(view.value().as<int>() == 2);
    }
}

// ============================================================
// has_any_difference Tests
// ============================================================

TEST_CASE("has_any_difference function", "[diff]") {
    SECTION("identical values") {
        auto v = ImmerValue{42};
        REQUIRE_FALSE(has_any_difference(v, v));
    }
    
    SECTION("different primitives") {
        REQUIRE(has_any_difference(ImmerValue{1}, ImmerValue{2}));
    }
    
    SECTION("different types") {
        REQUIRE(has_any_difference(ImmerValue{42}, ImmerValue{"42"}));
    }
    
    SECTION("nested difference") {
        auto old_state = ImmerValue::map({
            {"nested", ImmerValue::map({{"value", ImmerValue{1}}})}
        });
        auto new_state = ImmerValue::map({
            {"nested", ImmerValue::map({{"value", ImmerValue{2}}})}
        });
        
        REQUIRE(has_any_difference(old_state, new_state, true));
    }
    
    SECTION("structural sharing (same pointer)") {
        auto shared = ImmerValue::map({{"key", ImmerValue{1}}});
        REQUIRE_FALSE(has_any_difference(shared, shared));
    }
}

// ============================================================
// diff_as_value Convenience Function Tests
// ============================================================

TEST_CASE("diff_as_value function", "[diff]") {
    auto old_state = ImmerValue::map({{"key", ImmerValue{1}}});
    auto new_state = ImmerValue::map({{"key", ImmerValue{2}}});
    
    SECTION("returns diff tree") {
        auto diff = diff_as_value(old_state, new_state);
        REQUIRE(diff.is_map());
        REQUIRE(DiffValueCollector::is_diff_node(diff.at("key")));
    }
    
    SECTION("no changes returns empty or null") {
        auto same = ImmerValue::map({{"key", ImmerValue{1}}});
        auto diff = diff_as_value(same, same);
        // When no changes, result should be empty or null
        // (exact behavior depends on implementation)
        REQUIRE((diff.is_null() || (diff.is_map() && diff.size() == 0)));
    }
}

// ============================================================
// apply_diff Tests
// ============================================================

TEST_CASE("apply_diff function", "[diff][apply]") {
    auto old_state = ImmerValue::map({
        {"name", ImmerValue{"Alice"}},
        {"age", ImmerValue{30}}
    });
    
    auto new_state = ImmerValue::map({
        {"name", ImmerValue{"Bob"}},
        {"age", ImmerValue{30}}
    });
    
    SECTION("apply diff produces same result") {
        auto diff = diff_as_value(old_state, new_state);
        auto result = apply_diff(old_state, diff);
        
        REQUIRE(result.at("name").as<std::string>() == "Bob");
        REQUIRE(result.at("age").as<int>() == 30);
    }
}

// ============================================================
// Vector Diff Tests
// ============================================================

TEST_CASE("Diff vector changes", "[diff][vector]") {
    auto old_vec = ImmerValue::vector({
        ImmerValue{1},
        ImmerValue{2},
        ImmerValue{3}
    });
    
    SECTION("element change") {
        auto new_vec = ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{99},
            ImmerValue{3}
        });
        
        REQUIRE(has_any_difference(old_vec, new_vec));
        
        DiffEntryCollector collector;
        collector.diff(old_vec, new_vec);
        
        const auto& diffs = collector.get_diffs();
        REQUIRE(diffs.size() == 1);
        REQUIRE(diffs[0].type == DiffEntry::Type::Change);
    }
    
    SECTION("length change (append)") {
        auto new_vec = ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{2},
            ImmerValue{3},
            ImmerValue{4}
        });
        
        REQUIRE(has_any_difference(old_vec, new_vec));
    }
    
    SECTION("length change (shrink)") {
        auto new_vec = ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{2}
        });
        
        REQUIRE(has_any_difference(old_vec, new_vec));
    }
}

// ============================================================
// Complex Nested Structure Tests
// ============================================================

TEST_CASE("Diff complex nested structures", "[diff][nested]") {
    auto old_state = ImmerValue::map({
        {"users", ImmerValue::vector({
            ImmerValue::map({
                {"id", ImmerValue{1}},
                {"profile", ImmerValue::map({
                    {"name", ImmerValue{"Alice"}},
                    {"tags", ImmerValue::vector({
                        ImmerValue{"admin"},
                        ImmerValue{"active"}
                    })}
                })}
            })
        })}
    });
    
    auto new_state = ImmerValue::map({
        {"users", ImmerValue::vector({
            ImmerValue::map({
                {"id", ImmerValue{1}},
                {"profile", ImmerValue::map({
                    {"name", ImmerValue{"Alice Updated"}},
                    {"tags", ImmerValue::vector({
                        ImmerValue{"admin"},
                        ImmerValue{"active"}
                    })}
                })}
            })
        })}
    });
    
    SECTION("detects deep change") {
        REQUIRE(has_any_difference(old_state, new_state));
    }
    
    SECTION("finds the specific change") {
        DiffEntryCollector collector;
        collector.diff(old_state, new_state);
        
        REQUIRE(collector.has_changes());
        
        const auto& diffs = collector.get_diffs();
        bool found_name_change = false;
        
        for (const auto& entry : diffs) {
            if (entry.type == DiffEntry::Type::Change) {
                if (entry.get_old().is<std::string>() && 
                    entry.get_old().as<std::string>() == "Alice") {
                    found_name_change = true;
                    break;
                }
            }
        }
        
        REQUIRE(found_name_change);
    }
}

// ============================================================
// Edge Cases
// ============================================================

TEST_CASE("Diff edge cases", "[diff][edge]") {
    SECTION("null to value") {
        auto diff_result = diff_as_value(ImmerValue{}, ImmerValue{42});
        REQUIRE(DiffValueCollector::is_diff_node(diff_result));
    }
    
    SECTION("value to null") {
        auto diff_result = diff_as_value(ImmerValue{42}, ImmerValue{});
        REQUIRE(DiffValueCollector::is_diff_node(diff_result));
    }
    
    SECTION("empty map to non-empty") {
        auto empty = ImmerValue::map();
        auto filled = ImmerValue::map({{"key", ImmerValue{1}}});
        
        REQUIRE(has_any_difference(empty, filled));
    }
    
    SECTION("empty vector to non-empty") {
        auto empty = ImmerValue::vector();
        auto filled = ImmerValue::vector({ImmerValue{1}});
        
        REQUIRE(has_any_difference(empty, filled));
    }
}
