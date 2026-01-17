// test_mutable_value.cpp - Tests for MutableValue construction, access, and modification
// Module 2: Mutable variant of value types

#include <catch2/catch_all.hpp>
#include <lager_ext/mutable_value.h>
#include <lager_ext/value.h>

using namespace lager_ext;

// ============================================================
// Construction Tests
// ============================================================

TEST_CASE("MutableValue default construction", "[mutable_value][construction]") {
    MutableValue v;
    REQUIRE(v.is_null());
}

TEST_CASE("MutableValue primitive construction", "[mutable_value][construction]") {
    SECTION("bool") {
        MutableValue v{true};
        REQUIRE(v.is_bool());
        REQUIRE(v.as<bool>() == true);
    }

    SECTION("int8_t") {
        MutableValue v{int8_t{-42}};
        REQUIRE(v.is<int8_t>());
        REQUIRE(v.as<int8_t>() == -42);
    }

    SECTION("int32_t") {
        MutableValue v{42};
        REQUIRE(v.is<int32_t>());
        REQUIRE(v.as<int32_t>() == 42);
    }

    SECTION("int64_t") {
        MutableValue v{int64_t{9999999999LL}};
        REQUIRE(v.is<int64_t>());
        REQUIRE(v.as<int64_t>() == 9999999999LL);
    }

    SECTION("float") {
        MutableValue v{3.14f};
        REQUIRE(v.is<float>());
        REQUIRE(v.as<float>() == Catch::Approx(3.14f));
    }

    SECTION("double") {
        MutableValue v{3.14159265358979};
        REQUIRE(v.is<double>());
        REQUIRE(v.as<double>() == Catch::Approx(3.14159265358979));
    }
}

TEST_CASE("MutableValue string construction", "[mutable_value][construction]") {
    SECTION("from const char*") {
        MutableValue v{"hello"};
        REQUIRE(v.is_string());
        REQUIRE(v.as<std::string>() == "hello");
    }

    SECTION("from std::string") {
        MutableValue v{std::string("world")};
        REQUIRE(v.is_string());
        REQUIRE(v.as<std::string>() == "world");
    }
}

TEST_CASE("MutableValue math type construction", "[mutable_value][construction]") {
    SECTION("Vec2") {
        MutableValue v{Vec2{1.0f, 2.0f}};
        REQUIRE(v.is_vec2());
        auto vec = v.as<Vec2>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
    }

    SECTION("Vec3") {
        MutableValue v{Vec3{1.0f, 2.0f, 3.0f}};
        REQUIRE(v.is_vec3());
    }

    SECTION("Vec4") {
        MutableValue v{Vec4{1.0f, 2.0f, 3.0f, 4.0f}};
        REQUIRE(v.is_vec4());
    }

    SECTION("Vec2 factory") {
        auto v = MutableValue::vec2(5.0f, 6.0f);
        REQUIRE(v.is_vec2());
        auto vec = v.as<Vec2>();
        REQUIRE(vec[0] == Catch::Approx(5.0f));
        REQUIRE(vec[1] == Catch::Approx(6.0f));
    }

    SECTION("Vec3 factory") {
        auto v = MutableValue::vec3(1.0f, 2.0f, 3.0f);
        REQUIRE(v.is_vec3());
    }

    SECTION("Vec4 factory") {
        auto v = MutableValue::vec4(1.0f, 2.0f, 3.0f, 4.0f);
        REQUIRE(v.is_vec4());
    }

    SECTION("Mat3") {
        Mat3 m{};
        std::fill(m.begin(), m.end(), 1.0f);
        MutableValue v{m};
        REQUIRE(v.is_mat3());
        auto result = v.as_mat3();
        REQUIRE(result[0] == Catch::Approx(1.0f));
    }
}

TEST_CASE("MutableValue container construction", "[mutable_value][construction]") {
    SECTION("empty map") {
        auto v = MutableValue::map();
        REQUIRE(v.is_map());
        REQUIRE(v.size() == 0);
    }

    SECTION("empty vector") {
        auto v = MutableValue::vector();
        REQUIRE(v.is_vector());
        REQUIRE(v.size() == 0);
    }
}

// ============================================================
// Type Predicates Tests
// ============================================================

TEST_CASE("MutableValue type predicates", "[mutable_value][access]") {
    SECTION("is_null") {
        REQUIRE(MutableValue{}.is_null());
        REQUIRE_FALSE(MutableValue{42}.is_null());
    }

    SECTION("is_string") {
        REQUIRE(MutableValue{"hello"}.is_string());
        REQUIRE_FALSE(MutableValue{42}.is_string());
    }

    SECTION("is_numeric") {
        REQUIRE(MutableValue{42}.is_numeric());
        REQUIRE(MutableValue{3.14}.is_numeric());
        REQUIRE(MutableValue{3.14f}.is_numeric());
        REQUIRE_FALSE(MutableValue{"hello"}.is_numeric());
    }

    SECTION("is_map") {
        auto v = MutableValue::map();
        REQUIRE(v.is_map());
    }

    SECTION("is_vector") {
        auto v = MutableValue::vector();
        REQUIRE(v.is_vector());
    }

    SECTION("is_vector_math") {
        REQUIRE(MutableValue{Vec2{0, 0}}.is_vector_math());
        REQUIRE(MutableValue{Vec3{0, 0, 0}}.is_vector_math());
        REQUIRE(MutableValue{Vec4{0, 0, 0, 0}}.is_vector_math());
        REQUIRE_FALSE(MutableValue{42}.is_vector_math());
    }

    SECTION("is_math_type") {
        REQUIRE(MutableValue{Vec2{0, 0}}.is_math_type());
        Mat3 m{};
        REQUIRE(MutableValue{m}.is_math_type());
    }
}

// ============================================================
// Access Tests
// ============================================================

TEST_CASE("MutableValue get_or with default", "[mutable_value][access]") {
    SECTION("returns value when type matches") {
        MutableValue v{42};
        REQUIRE(v.get_or<int32_t>(0) == 42);
    }

    SECTION("returns default when type mismatches") {
        MutableValue v{"hello"};
        REQUIRE(v.get_or<int32_t>(99) == 99);
    }

    SECTION("returns default for null") {
        MutableValue v;
        REQUIRE(v.get_or<int32_t>(42) == 42);
    }
}

TEST_CASE("MutableValue as_number conversion", "[mutable_value][access]") {
    SECTION("from double") {
        MutableValue v{3.14};
        REQUIRE(v.as_number() == Catch::Approx(3.14));
    }

    SECTION("from float") {
        MutableValue v{2.5f};
        REQUIRE(v.as_number() == Catch::Approx(2.5));
    }

    SECTION("from int32_t") {
        MutableValue v{42};
        REQUIRE(v.as_number() == Catch::Approx(42.0));
    }

    SECTION("default for non-numeric") {
        MutableValue v{"not a number"};
        REQUIRE(v.as_number(-1.0) == Catch::Approx(-1.0));
    }
}

// ============================================================
// Map Modification Tests (In-place mutation)
// ============================================================

TEST_CASE("MutableValue map operations", "[mutable_value][modification]") {
    auto v = MutableValue::map();

    SECTION("set and get") {
        v.set("name", MutableValue{"Alice"});
        v.set("age", MutableValue{30});

        REQUIRE(v.size() == 2);
        
        auto* name = v.get("name");
        REQUIRE(name != nullptr);
        REQUIRE(name->as<std::string>() == "Alice");
        
        auto* age = v.get("age");
        REQUIRE(age != nullptr);
        REQUIRE(age->as<int32_t>() == 30);
    }

    SECTION("contains") {
        v.set("key", MutableValue{1});
        REQUIRE(v.contains("key"));
        REQUIRE_FALSE(v.contains("missing"));
    }

    SECTION("count") {
        v.set("key", MutableValue{1});
        REQUIRE(v.count("key") == 1);
        REQUIRE(v.count("missing") == 0);
    }

    SECTION("erase") {
        v.set("a", MutableValue{1});
        v.set("b", MutableValue{2});
        
        REQUIRE(v.size() == 2);
        REQUIRE(v.erase("a"));
        REQUIRE(v.size() == 1);
        REQUIRE_FALSE(v.contains("a"));
        REQUIRE(v.contains("b"));
    }

    SECTION("erase non-existent key") {
        v.set("a", MutableValue{1});
        REQUIRE_FALSE(v.erase("missing"));
        REQUIRE(v.size() == 1);
    }

    SECTION("get missing key returns nullptr") {
        auto* result = v.get("missing");
        REQUIRE(result == nullptr);
    }
}

// ============================================================
// Vector Modification Tests (In-place mutation)
// ============================================================

TEST_CASE("MutableValue vector operations", "[mutable_value][modification]") {
    auto v = MutableValue::vector();

    SECTION("push_back and get") {
        v.push_back(MutableValue{1});
        v.push_back(MutableValue{2});
        v.push_back(MutableValue{3});

        REQUIRE(v.size() == 3);
        
        auto* elem0 = v.get(0);
        REQUIRE(elem0 != nullptr);
        REQUIRE(elem0->as<int32_t>() == 1);
        
        auto* elem2 = v.get(2);
        REQUIRE(elem2 != nullptr);
        REQUIRE(elem2->as<int32_t>() == 3);
    }

    SECTION("set by index") {
        v.push_back(MutableValue{1});
        v.push_back(MutableValue{2});
        
        v.set(0, MutableValue{100});
        
        auto* elem = v.get(0);
        REQUIRE(elem != nullptr);
        REQUIRE(elem->as<int32_t>() == 100);
    }

    SECTION("get out of bounds returns nullptr") {
        v.push_back(MutableValue{1});
        auto* result = v.get(100);
        REQUIRE(result == nullptr);
    }
}

// ============================================================
// Path-based Access Tests
// ============================================================

TEST_CASE("MutableValue path access", "[mutable_value][path]") {
    auto root = MutableValue::map();
    
    // Build nested structure
    auto user = MutableValue::map();
    user.set("name", MutableValue{"Alice"});
    user.set("age", MutableValue{30});
    
    auto settings = MutableValue::map();
    settings.set("theme", MutableValue{"dark"});
    
    root.set("user", std::move(user));
    root.set("settings", std::move(settings));

    SECTION("get_at_path existing path") {
        Path path;
        path.push_back("user");
        path.push_back("name");
        
        auto* result = root.get_at_path(path);
        REQUIRE(result != nullptr);
        REQUIRE(result->as<std::string>() == "Alice");
    }

    SECTION("get_at_path missing path") {
        Path path;
        path.push_back("missing");
        path.push_back("key");
        
        auto* result = root.get_at_path(path);
        REQUIRE(result == nullptr);
    }

    SECTION("has_path") {
        Path existing;
        existing.push_back("user");
        existing.push_back("name");
        REQUIRE(root.has_path(existing));

        Path missing;
        missing.push_back("user");
        missing.push_back("missing");
        REQUIRE_FALSE(root.has_path(missing));
    }

    SECTION("set_at_path") {
        Path path;
        path.push_back("user");
        path.push_back("age");
        
        root.set_at_path(path, MutableValue{31});
        
        auto* result = root.get_at_path(path);
        REQUIRE(result != nullptr);
        REQUIRE(result->as<int32_t>() == 31);
    }

    SECTION("erase_at_path") {
        Path path;
        path.push_back("settings");
        path.push_back("theme");
        
        REQUIRE(root.erase_at_path(path));
        REQUIRE_FALSE(root.has_path(path));
    }
}

// ============================================================
// Clone Tests
// ============================================================

TEST_CASE("MutableValue clone", "[mutable_value][clone]") {
    SECTION("clone primitive") {
        MutableValue original{42};
        MutableValue cloned = original.clone();
        
        REQUIRE(cloned.as<int32_t>() == 42);
    }

    SECTION("clone string") {
        MutableValue original{"hello"};
        MutableValue cloned = original.clone();
        
        REQUIRE(cloned.as<std::string>() == "hello");
    }

    SECTION("clone map is independent") {
        auto original = MutableValue::map();
        original.set("key", MutableValue{1});
        
        MutableValue cloned = original.clone();
        cloned.set("key", MutableValue{999});
        
        // Original unchanged
        REQUIRE(original.get("key")->as<int32_t>() == 1);
        // Clone has new value
        REQUIRE(cloned.get("key")->as<int32_t>() == 999);
    }

    SECTION("clone vector is independent") {
        auto original = MutableValue::vector();
        original.push_back(MutableValue{1});
        original.push_back(MutableValue{2});
        
        MutableValue cloned = original.clone();
        cloned.set(0, MutableValue{100});
        
        // Original unchanged
        REQUIRE(original.get(0)->as<int32_t>() == 1);
        // Clone has new value
        REQUIRE(cloned.get(0)->as<int32_t>() == 100);
    }

    SECTION("deep clone nested structure") {
        auto original = MutableValue::map();
        auto nested = MutableValue::map();
        nested.set("x", MutableValue{10});
        original.set("nested", std::move(nested));
        
        MutableValue cloned = original.clone();
        
        // Modify cloned nested value
        Path path;
        path.push_back("nested");
        path.push_back("x");
        cloned.set_at_path(path, MutableValue{999});
        
        // Original unchanged
        REQUIRE(original.get_at_path(path)->as<int32_t>() == 10);
        // Clone modified
        REQUIRE(cloned.get_at_path(path)->as<int32_t>() == 999);
    }
}

// ============================================================
// to_string Tests
// ============================================================

TEST_CASE("MutableValue to_string", "[mutable_value][string]") {
    SECTION("null") {
        MutableValue v;
        REQUIRE(v.to_string() == "null");
    }

    SECTION("bool") {
        REQUIRE(MutableValue{true}.to_string() == "true");
        REQUIRE(MutableValue{false}.to_string() == "false");
    }

    SECTION("integer") {
        MutableValue v{42};
        REQUIRE(v.to_string() == "42");
    }

    SECTION("string") {
        MutableValue v{"hello"};
        auto str = v.to_string();
        REQUIRE(str.find("hello") != std::string::npos);
    }
}
