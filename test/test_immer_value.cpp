// test_immer_value.cpp - Tests for ImmerValue construction, access, and modification
// Module 1: Core ImmerValue functionality

#include <catch2/catch_all.hpp>
#include <lager_ext/value.h>

using namespace lager_ext;

// ============================================================
// Construction Tests
// ============================================================

TEST_CASE("ImmerValue default construction", "[value][construction]") {
    ImmerValue v;
    REQUIRE(v.is_null());
    REQUIRE(v.type_index() == 22);  // std::monostate is last in variant
}

TEST_CASE("ImmerValue primitive construction", "[value][construction]") {
    SECTION("int8_t") {
        ImmerValue v{int8_t{-42}};
        REQUIRE(v.is<int8_t>());
        REQUIRE(v.as<int8_t>() == -42);
    }

    SECTION("int16_t") {
        ImmerValue v{int16_t{-1000}};
        REQUIRE(v.is<int16_t>());
        REQUIRE(v.as<int16_t>() == -1000);
    }

    SECTION("int32_t") {
        ImmerValue v{42};
        REQUIRE(v.is<int32_t>());
        REQUIRE(v.as<int32_t>() == 42);
    }

    SECTION("int64_t") {
        ImmerValue v{int64_t{9999999999LL}};
        REQUIRE(v.is<int64_t>());
        REQUIRE(v.as<int64_t>() == 9999999999LL);
    }

    SECTION("uint8_t") {
        ImmerValue v{uint8_t{255}};
        REQUIRE(v.is<uint8_t>());
        REQUIRE(v.as<uint8_t>() == 255);
    }

    SECTION("uint16_t") {
        ImmerValue v{uint16_t{65535}};
        REQUIRE(v.is<uint16_t>());
        REQUIRE(v.as<uint16_t>() == 65535);
    }

    SECTION("uint32_t") {
        ImmerValue v{uint32_t{4000000000U}};
        REQUIRE(v.is<uint32_t>());
        REQUIRE(v.as<uint32_t>() == 4000000000U);
    }

    SECTION("uint64_t") {
        ImmerValue v{uint64_t{18446744073709551615ULL}};
        REQUIRE(v.is<uint64_t>());
        REQUIRE(v.as<uint64_t>() == 18446744073709551615ULL);
    }

    SECTION("float") {
        ImmerValue v{3.14f};
        REQUIRE(v.is<float>());
        REQUIRE(v.as<float>() == Catch::Approx(3.14f));
    }

    SECTION("double") {
        ImmerValue v{3.14159265358979};
        REQUIRE(v.is<double>());
        REQUIRE(v.as<double>() == Catch::Approx(3.14159265358979));
    }

    SECTION("bool true") {
        ImmerValue v{true};
        REQUIRE(v.is<bool>());
        REQUIRE(v.as<bool>() == true);
    }

    SECTION("bool false") {
        ImmerValue v{false};
        REQUIRE(v.is<bool>());
        REQUIRE(v.as<bool>() == false);
    }
}

TEST_CASE("ImmerValue string construction", "[value][construction]") {
    SECTION("from const char*") {
        ImmerValue v{"hello"};
        REQUIRE(v.is_string());
        REQUIRE(v.as_string() == "hello");
    }

    SECTION("from std::string lvalue") {
        std::string s = "world";
        ImmerValue v{s};
        REQUIRE(v.is_string());
        REQUIRE(v.as_string() == "world");
    }

    SECTION("from std::string rvalue") {
        ImmerValue v{std::string("moved")};
        REQUIRE(v.is_string());
        REQUIRE(v.as_string() == "moved");
    }

    SECTION("string_view access") {
        ImmerValue v{"test_string_view"};
        REQUIRE(v.as_string_view() == "test_string_view");
    }
}

TEST_CASE("ImmerValue math type construction", "[value][construction]") {
    SECTION("Vec2") {
        ImmerValue v{Vec2{1.0f, 2.0f}};
        REQUIRE(v.is_vec2());
        auto vec = v.as<Vec2>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
    }

    SECTION("Vec3") {
        ImmerValue v{Vec3{1.0f, 2.0f, 3.0f}};
        REQUIRE(v.is_vec3());
        auto vec = v.as<Vec3>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
        REQUIRE(vec[2] == Catch::Approx(3.0f));
    }

    SECTION("Vec4") {
        ImmerValue v{Vec4{1.0f, 2.0f, 3.0f, 4.0f}};
        REQUIRE(v.is_vec4());
        auto vec = v.as<Vec4>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
        REQUIRE(vec[2] == Catch::Approx(3.0f));
        REQUIRE(vec[3] == Catch::Approx(4.0f));
    }

    SECTION("Vec2 factory") {
        auto v = ImmerValue::vec2(5.0f, 6.0f);
        REQUIRE(v.is_vec2());
        auto vec = v.as<Vec2>();
        REQUIRE(vec[0] == Catch::Approx(5.0f));
        REQUIRE(vec[1] == Catch::Approx(6.0f));
    }

    SECTION("Vec3 factory") {
        auto v = ImmerValue::vec3(1.0f, 2.0f, 3.0f);
        REQUIRE(v.is_vec3());
    }

    SECTION("Vec4 factory") {
        auto v = ImmerValue::vec4(1.0f, 2.0f, 3.0f, 4.0f);
        REQUIRE(v.is_vec4());
    }

    SECTION("Mat3") {
        Mat3 m{};
        std::fill(m.begin(), m.end(), 1.0f);
        ImmerValue v{m};
        REQUIRE(v.is_mat3());
        auto result = v.as_mat3();
        REQUIRE(result[0] == Catch::Approx(1.0f));
    }

    SECTION("Mat4x3") {
        Mat4x3 m{};
        std::fill(m.begin(), m.end(), 2.0f);
        ImmerValue v{m};
        REQUIRE(v.is_mat4x3());
        auto result = v.as_mat4x3();
        REQUIRE(result[0] == Catch::Approx(2.0f));
    }
}

TEST_CASE("ImmerValue container construction", "[value][construction]") {
    SECTION("map from initializer list") {
        auto v = ImmerValue::map({
            {"name", ImmerValue{"Alice"}},
            {"age", ImmerValue{30}}
        });
        REQUIRE(v.is_map());
        REQUIRE(v.size() == 2);
        REQUIRE(v.at("name").as_string() == "Alice");
        REQUIRE(v.at("age").as<int>() == 30);
    }

    SECTION("vector from initializer list") {
        auto v = ImmerValue::vector({
            ImmerValue{1},
            ImmerValue{2},
            ImmerValue{3}
        });
        REQUIRE(v.is_vector());
        REQUIRE(v.size() == 3);
        REQUIRE(v.at(0).as<int>() == 1);
        REQUIRE(v.at(1).as<int>() == 2);
        REQUIRE(v.at(2).as<int>() == 3);
    }

    SECTION("nested map") {
        auto inner = ImmerValue::map({{"x", ImmerValue{10}}});
        auto outer = ImmerValue::map({{"inner", inner}});
        REQUIRE(outer.at("inner").at("x").as<int>() == 10);
    }
}

// ============================================================
// Access Tests
// ============================================================

TEST_CASE("ImmerValue type predicates", "[value][access]") {
    SECTION("is_null") {
        REQUIRE(ImmerValue{}.is_null());
        REQUIRE_FALSE(ImmerValue{42}.is_null());
    }

    SECTION("is_string") {
        REQUIRE(ImmerValue{"hello"}.is_string());
        REQUIRE_FALSE(ImmerValue{42}.is_string());
    }

    SECTION("is_map") {
        auto v = ImmerValue::map({});
        REQUIRE(v.is_map());
    }

    SECTION("is_vector") {
        auto v = ImmerValue::vector({});
        REQUIRE(v.is_vector());
    }

    SECTION("is_math_type") {
        REQUIRE(ImmerValue{Vec2{0, 0}}.is_math_type());
        REQUIRE(ImmerValue{Vec3{0, 0, 0}}.is_math_type());
        REQUIRE(ImmerValue{Vec4{0, 0, 0, 0}}.is_math_type());
        REQUIRE_FALSE(ImmerValue{42}.is_math_type());
    }
}

TEST_CASE("ImmerValue as<T> with default", "[value][access]") {
    SECTION("returns value when type matches") {
        ImmerValue v{42};
        REQUIRE(v.as<int>(0) == 42);
    }

    SECTION("returns default when type mismatches") {
        ImmerValue v{"hello"};
        REQUIRE(v.as<int>(99) == 99);
    }

    SECTION("returns default for null") {
        ImmerValue v;
        REQUIRE(v.as<int>(42) == 42);
    }
}

TEST_CASE("ImmerValue as_number conversion", "[value][access]") {
    SECTION("from double") {
        ImmerValue v{3.14};
        REQUIRE(v.as_number() == Catch::Approx(3.14));
    }

    SECTION("from float") {
        ImmerValue v{2.5f};
        REQUIRE(v.as_number() == Catch::Approx(2.5));
    }

    SECTION("from int64_t") {
        ImmerValue v{int64_t{1000}};
        REQUIRE(v.as_number() == Catch::Approx(1000.0));
    }

    SECTION("from int32_t") {
        ImmerValue v{42};
        REQUIRE(v.as_number() == Catch::Approx(42.0));
    }

    SECTION("default for non-numeric") {
        ImmerValue v{"not a number"};
        REQUIRE(v.as_number(-1.0) == Catch::Approx(-1.0));
    }
}

TEST_CASE("ImmerValue map access", "[value][access]") {
    auto v = ImmerValue::map({
        {"name", ImmerValue{"Bob"}},
        {"count", ImmerValue{5}}
    });

    SECTION("at with existing key") {
        REQUIRE(v.at("name").as_string() == "Bob");
    }

    SECTION("at with missing key returns null") {
        REQUIRE(v.at("missing").is_null());
    }

    SECTION("contains") {
        REQUIRE(v.contains("name"));
        REQUIRE_FALSE(v.contains("missing"));
    }

    SECTION("count") {
        REQUIRE(v.count("name") == 1);
        REQUIRE(v.count("missing") == 0);
    }

    SECTION("size") {
        REQUIRE(v.size() == 2);
    }
}

TEST_CASE("ImmerValue vector access", "[value][access]") {
    auto v = ImmerValue::vector({
        ImmerValue{10},
        ImmerValue{20},
        ImmerValue{30}
    });

    SECTION("at with valid index") {
        REQUIRE(v.at(0).as<int>() == 10);
        REQUIRE(v.at(1).as<int>() == 20);
        REQUIRE(v.at(2).as<int>() == 30);
    }

    SECTION("at with invalid index returns null") {
        REQUIRE(v.at(100).is_null());
    }

    SECTION("contains index") {
        REQUIRE(v.contains(std::size_t{0}));
        REQUIRE(v.contains(std::size_t{2}));
        REQUIRE_FALSE(v.contains(std::size_t{3}));
    }

    SECTION("size") {
        REQUIRE(v.size() == 3);
    }
}

// ============================================================
// Modification Tests (Immutable - returns new value)
// ============================================================

TEST_CASE("ImmerValue map modification", "[value][modification]") {
    auto v = ImmerValue::map({
        {"a", ImmerValue{1}},
        {"b", ImmerValue{2}}
    });

    SECTION("set existing key") {
        auto v2 = v.set("a", ImmerValue{100});
        
        // Original unchanged
        REQUIRE(v.at("a").as<int>() == 1);
        
        // New value updated
        REQUIRE(v2.at("a").as<int>() == 100);
        REQUIRE(v2.at("b").as<int>() == 2);  // Other keys preserved
    }

    SECTION("set new key") {
        auto v2 = v.set("c", ImmerValue{3});
        
        REQUIRE(v.size() == 2);
        REQUIRE(v2.size() == 3);
        REQUIRE(v2.at("c").as<int>() == 3);
    }

    SECTION("set with string_view key") {
        std::string_view key = "d";
        auto v2 = v.set(key, ImmerValue{4});
        REQUIRE(v2.at("d").as<int>() == 4);
    }

    SECTION("chained modifications") {
        auto v2 = v.set("a", ImmerValue{10})
                   .set("b", ImmerValue{20})
                   .set("c", ImmerValue{30});
        
        REQUIRE(v2.at("a").as<int>() == 10);
        REQUIRE(v2.at("b").as<int>() == 20);
        REQUIRE(v2.at("c").as<int>() == 30);
    }
}

TEST_CASE("ImmerValue vector modification", "[value][modification]") {
    auto v = ImmerValue::vector({
        ImmerValue{1},
        ImmerValue{2},
        ImmerValue{3}
    });

    SECTION("set existing index") {
        auto v2 = v.set(std::size_t{1}, ImmerValue{200});
        
        // Original unchanged
        REQUIRE(v.at(1).as<int>() == 2);
        
        // New value updated
        REQUIRE(v2.at(0).as<int>() == 1);
        REQUIRE(v2.at(1).as<int>() == 200);
        REQUIRE(v2.at(2).as<int>() == 3);
    }

    SECTION("set out of bounds returns original") {
        auto v2 = v.set(std::size_t{100}, ImmerValue{999});
        REQUIRE(v2.size() == v.size());  // No change
    }
}

TEST_CASE("ImmerValue set_vivify", "[value][modification]") {
    auto v = ImmerValue::map({});

    SECTION("creates nested structure") {
        // set_vivify should create intermediate maps/vectors as needed
        // This tests the vivify behavior for maps
        auto v2 = v.set_vivify("key", ImmerValue{42});
        REQUIRE(v2.at("key").as<int>() == 42);
    }
}

// ============================================================
// Comparison Tests
// ============================================================

TEST_CASE("ImmerValue equality", "[value][comparison]") {
    SECTION("equal primitives") {
        REQUIRE(ImmerValue{42} == ImmerValue{42});
        REQUIRE(ImmerValue{3.14} == ImmerValue{3.14});
        REQUIRE(ImmerValue{true} == ImmerValue{true});
    }

    SECTION("unequal primitives") {
        REQUIRE(ImmerValue{42} != ImmerValue{43});
        REQUIRE(ImmerValue{true} != ImmerValue{false});
    }

    SECTION("equal strings") {
        REQUIRE(ImmerValue{"hello"} == ImmerValue{"hello"});
    }

    SECTION("unequal strings") {
        REQUIRE(ImmerValue{"hello"} != ImmerValue{"world"});
    }

    SECTION("equal vectors") {
        auto v1 = ImmerValue::vector({ImmerValue{1}, ImmerValue{2}});
        auto v2 = ImmerValue::vector({ImmerValue{1}, ImmerValue{2}});
        REQUIRE(v1 == v2);
    }

    SECTION("unequal vectors") {
        auto v1 = ImmerValue::vector({ImmerValue{1}, ImmerValue{2}});
        auto v2 = ImmerValue::vector({ImmerValue{1}, ImmerValue{3}});
        REQUIRE(v1 != v2);
    }

    SECTION("equal maps") {
        auto m1 = ImmerValue::map({{"a", ImmerValue{1}}});
        auto m2 = ImmerValue::map({{"a", ImmerValue{1}}});
        REQUIRE(m1 == m2);
    }

    SECTION("different types are not equal") {
        REQUIRE(ImmerValue{42} != ImmerValue{"42"});
        REQUIRE(ImmerValue{42} != ImmerValue{42.0});
    }
}

// ============================================================
// Serialization Tests
// ============================================================

TEST_CASE("ImmerValue JSON serialization", "[value][serialization]") {
    SECTION("primitive to JSON") {
        REQUIRE(to_json(ImmerValue{42}, true) == "42");
        REQUIRE(to_json(ImmerValue{true}, true) == "true");
        REQUIRE(to_json(ImmerValue{false}, true) == "false");
    }

    SECTION("string to JSON") {
        REQUIRE(to_json(ImmerValue{"hello"}, true) == "\"hello\"");
    }

    SECTION("null to JSON") {
        REQUIRE(to_json(ImmerValue{}, true) == "null");
    }

    SECTION("round-trip JSON") {
        auto original = ImmerValue::map({
            {"name", ImmerValue{"test"}},
            {"value", ImmerValue{123}}
        });
        
        std::string json = to_json(original, true);
        auto parsed = from_json(json);
        
        REQUIRE(parsed.at("name").as_string() == "test");
        REQUIRE(parsed.at("value").as<int>() == 123);
    }
}

TEST_CASE("ImmerValue binary serialization", "[value][serialization]") {
    SECTION("primitive round-trip") {
        ImmerValue original{42};
        auto buffer = serialize(original);
        auto restored = deserialize(buffer);
        REQUIRE(restored.as<int>() == 42);
    }

    SECTION("string round-trip") {
        ImmerValue original{"test string"};
        auto buffer = serialize(original);
        auto restored = deserialize(buffer);
        REQUIRE(restored.as_string() == "test string");
    }

    SECTION("complex structure round-trip") {
        auto original = ImmerValue::map({
            {"users", ImmerValue::vector({
                ImmerValue::map({{"name", ImmerValue{"Alice"}}}),
                ImmerValue::map({{"name", ImmerValue{"Bob"}}})
            })},
            {"count", ImmerValue{2}}
        });
        
        auto buffer = serialize(original);
        auto restored = deserialize(buffer);
        
        REQUIRE(restored.at("count").as<int>() == 2);
        REQUIRE(restored.at("users").at(0).at("name").as_string() == "Alice");
        REQUIRE(restored.at("users").at(1).at("name").as_string() == "Bob");
    }

    SECTION("math types round-trip") {
        auto original = ImmerValue::vec3(1.0f, 2.0f, 3.0f);
        auto buffer = serialize(original);
        auto restored = deserialize(buffer);
        
        REQUIRE(restored.is_vec3());
        auto vec = restored.as<Vec3>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
        REQUIRE(vec[2] == Catch::Approx(3.0f));
    }
}
