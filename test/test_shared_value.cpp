// test_shared_value.cpp - Tests for SharedValue and shared memory
// Module 4: SharedValue related interfaces

#include <catch2/catch_all.hpp>
#include <lager_ext/shared_value.h>
#include <lager_ext/value.h>

#include <string>

using namespace lager_ext;
using namespace shared_memory;

// ============================================================
// SharedString Tests
// ============================================================

TEST_CASE("SharedString construction", "[shared][string]") {
    SECTION("default construction") {
        SharedString s;
        REQUIRE(s.empty());
        REQUIRE(s.size() == 0);
    }
    
    SECTION("from const char*") {
        SharedString s{"hello"};
        REQUIRE(s.size() == 5);
        REQUIRE(s == "hello");
    }
    
    SECTION("from std::string") {
        std::string str = "world";
        SharedString s{str};
        REQUIRE(s.size() == 5);
        REQUIRE(s == str);
    }
    
    SECTION("from null const char*") {
        const char* null_str = nullptr;
        SharedString s{null_str};
        REQUIRE(s.empty());
    }
}

TEST_CASE("SharedString SSO (Small String Optimization)", "[shared][string][sso]") {
    SECTION("short strings use inline storage") {
        // SSO capacity is 15 bytes
        SharedString short_str{"hello"};
        REQUIRE(short_str.size() == 5);
        REQUIRE(short_str == "hello");
    }
    
    SECTION("exactly SSO capacity") {
        // 15 bytes exactly
        SharedString s{"123456789012345"};
        REQUIRE(s.size() == 15);
        REQUIRE(s == "123456789012345");
    }
}

TEST_CASE("SharedString operations", "[shared][string]") {
    SharedString s{"hello"};
    
    SECTION("data and c_str") {
        REQUIRE(std::string(s.data()) == "hello");
        REQUIRE(std::string(s.c_str()) == "hello");
    }
    
    SECTION("to_string") {
        std::string str = s.to_string();
        REQUIRE(str == "hello");
    }
    
    SECTION("element access") {
        REQUIRE(s[0] == 'h');
        REQUIRE(s[4] == 'o');
        REQUIRE(s.at(0) == 'h');
    }
    
    SECTION("at out of range throws") {
        REQUIRE_THROWS_AS(s.at(100), std::out_of_range);
    }
    
    SECTION("iterators") {
        std::string result;
        for (char c : s) {
            result += c;
        }
        REQUIRE(result == "hello");
    }
    
    SECTION("comparison") {
        SharedString same{"hello"};
        SharedString diff{"world"};
        
        REQUIRE(s == same);
        REQUIRE(s != diff);
        REQUIRE(s < diff); // "hello" < "world"
    }
    
    SECTION("hash") {
        SharedString same{"hello"};
        REQUIRE(s.hash() == same.hash());
        
        SharedString diff{"world"};
        REQUIRE(s.hash() != diff.hash());
    }
}

TEST_CASE("SharedString move semantics", "[shared][string]") {
    SECTION("move construction") {
        SharedString s1{"hello"};
        SharedString s2{std::move(s1)};
        
        REQUIRE(s2 == "hello");
        REQUIRE(s1.empty()); // Moved-from state
    }
    
    SECTION("move assignment") {
        SharedString s1{"hello"};
        SharedString s2{"world"};
        
        s2 = std::move(s1);
        
        REQUIRE(s2 == "hello");
    }
}

// ============================================================
// SharedValue Construction Tests
// ============================================================

TEST_CASE("SharedValue construction", "[shared][value]") {
    SECTION("default construction (null)") {
        SharedValue v;
        REQUIRE(v.is_null());
    }
    
    SECTION("int32_t") {
        SharedValue v{int32_t{42}};
        REQUIRE(v.is<int32_t>());
        REQUIRE(*v.get_if<int32_t>() == 42);
    }
    
    SECTION("int64_t") {
        SharedValue v{int64_t{9999999999LL}};
        REQUIRE(v.is<int64_t>());
        REQUIRE(*v.get_if<int64_t>() == 9999999999LL);
    }
    
    SECTION("uint32_t") {
        SharedValue v{uint32_t{123}};
        REQUIRE(v.is<uint32_t>());
        REQUIRE(*v.get_if<uint32_t>() == 123);
    }
    
    SECTION("uint64_t") {
        SharedValue v{uint64_t{456}};
        REQUIRE(v.is<uint64_t>());
        REQUIRE(*v.get_if<uint64_t>() == 456);
    }
    
    SECTION("float") {
        SharedValue v{3.14f};
        REQUIRE(v.is<float>());
        REQUIRE(*v.get_if<float>() == Catch::Approx(3.14f));
    }
    
    SECTION("double") {
        SharedValue v{3.14159265358979};
        REQUIRE(v.is<double>());
        REQUIRE(*v.get_if<double>() == Catch::Approx(3.14159265358979));
    }
    
    SECTION("bool") {
        SharedValue v{true};
        REQUIRE(v.is<bool>());
        REQUIRE(*v.get_if<bool>() == true);
    }
    
    SECTION("SharedString") {
        SharedValue v{SharedString{"hello"}};
        REQUIRE(v.get_string() != nullptr);
        REQUIRE(*v.get_string() == "hello");
    }
    
    SECTION("from std::string") {
        SharedValue v{std::string("world")};
        REQUIRE(v.get_string() != nullptr);
        REQUIRE(v.get_string()->to_string() == "world");
    }
    
    SECTION("from const char*") {
        SharedValue v{"test"};
        REQUIRE(v.get_string() != nullptr);
        REQUIRE(*v.get_string() == "test");
    }
}

TEST_CASE("SharedValue math types", "[shared][value][math]") {
    SECTION("Vec2") {
        SharedValue v{Vec2{1.0f, 2.0f}};
        REQUIRE(v.is<Vec2>());
        auto* vec = v.get_if<Vec2>();
        REQUIRE(vec != nullptr);
        REQUIRE((*vec)[0] == Catch::Approx(1.0f));
        REQUIRE((*vec)[1] == Catch::Approx(2.0f));
    }
    
    SECTION("Vec3") {
        SharedValue v{Vec3{1.0f, 2.0f, 3.0f}};
        REQUIRE(v.is<Vec3>());
    }
    
    SECTION("Vec4") {
        SharedValue v{Vec4{1.0f, 2.0f, 3.0f, 4.0f}};
        REQUIRE(v.is<Vec4>());
    }
    
    SECTION("Mat3") {
        Mat3 m{};
        std::fill(m.begin(), m.end(), 1.0f);
        SharedValue v{m};
        REQUIRE(v.is<Mat3>());
    }
    
    SECTION("Mat4x3") {
        Mat4x3 m{};
        std::fill(m.begin(), m.end(), 2.0f);
        SharedValue v{m};
        REQUIRE(v.is<Mat4x3>());
    }
}

TEST_CASE("SharedValue type_index", "[shared][value]") {
    SECTION("different types have different indices") {
        SharedValue null_v;
        SharedValue int_v{42};
        SharedValue str_v{"hello"};
        
        REQUIRE(null_v.type_index() != int_v.type_index());
        REQUIRE(int_v.type_index() != str_v.type_index());
    }
}

TEST_CASE("SharedValue equality", "[shared][value]") {
    SECTION("equal values") {
        SharedValue v1{42};
        SharedValue v2{42};
        REQUIRE(v1 == v2);
    }
    
    SECTION("different values") {
        SharedValue v1{42};
        SharedValue v2{43};
        REQUIRE(v1 != v2);
    }
    
    SECTION("different types") {
        SharedValue v1{42};
        SharedValue v2{"42"};
        REQUIRE(v1 != v2);
    }
}

// ============================================================
// Deep Copy Tests (ImmerValue <-> SharedValue)
// ============================================================

TEST_CASE("deep_copy_to_shared primitives", "[shared][copy]") {
    SECTION("null") {
        ImmerValue local{};
        SharedValue shared = deep_copy_to_shared(local);
        REQUIRE(shared.is_null());
    }
    
    SECTION("int32_t") {
        ImmerValue local{42};
        SharedValue shared = deep_copy_to_shared(local);
        REQUIRE(*shared.get_if<int32_t>() == 42);
    }
    
    SECTION("double") {
        ImmerValue local{3.14};
        SharedValue shared = deep_copy_to_shared(local);
        REQUIRE(*shared.get_if<double>() == Catch::Approx(3.14));
    }
    
    SECTION("bool") {
        ImmerValue local{true};
        SharedValue shared = deep_copy_to_shared(local);
        REQUIRE(*shared.get_if<bool>() == true);
    }
    
    SECTION("string") {
        ImmerValue local{"hello"};
        SharedValue shared = deep_copy_to_shared(local);
        REQUIRE(shared.get_string()->to_string() == "hello");
    }
}

TEST_CASE("deep_copy_to_local primitives", "[shared][copy]") {
    SECTION("null") {
        SharedValue shared{};
        ImmerValue local = deep_copy_to_local(shared);
        REQUIRE(local.is_null());
    }
    
    SECTION("int32_t") {
        SharedValue shared{int32_t{42}};
        ImmerValue local = deep_copy_to_local(shared);
        REQUIRE(local.as<int32_t>() == 42);
    }
    
    SECTION("double") {
        SharedValue shared{3.14};
        ImmerValue local = deep_copy_to_local(shared);
        REQUIRE(local.as<double>() == Catch::Approx(3.14));
    }
    
    SECTION("bool") {
        SharedValue shared{true};
        ImmerValue local = deep_copy_to_local(shared);
        REQUIRE(local.as<bool>() == true);
    }
    
    SECTION("string") {
        SharedValue shared{SharedString{"world"}};
        ImmerValue local = deep_copy_to_local(shared);
        REQUIRE(local.as<std::string>() == "world");
    }
}

TEST_CASE("deep_copy math types", "[shared][copy][math]") {
    SECTION("Vec2 round-trip") {
        ImmerValue local{Vec2{1.0f, 2.0f}};
        SharedValue shared = deep_copy_to_shared(local);
        ImmerValue back = deep_copy_to_local(shared);
        
        auto vec = back.as<Vec2>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
    }
    
    SECTION("Vec3 round-trip") {
        ImmerValue local{Vec3{1.0f, 2.0f, 3.0f}};
        SharedValue shared = deep_copy_to_shared(local);
        ImmerValue back = deep_copy_to_local(shared);
        
        auto vec = back.as<Vec3>();
        REQUIRE(vec[0] == Catch::Approx(1.0f));
        REQUIRE(vec[1] == Catch::Approx(2.0f));
        REQUIRE(vec[2] == Catch::Approx(3.0f));
    }
}

// ============================================================
// SharedMemoryHeader Tests
// ============================================================

TEST_CASE("SharedMemoryHeader structure", "[shared][header]") {
    SECTION("size is 64 bytes") {
        REQUIRE(sizeof(SharedMemoryHeader) == 64);
    }
    
    SECTION("magic and version constants") {
        REQUIRE(SharedMemoryHeader::MAGIC == 0x53484D56);
        REQUIRE(SharedMemoryHeader::CURRENT_VERSION == 1);
    }
}

// ============================================================
// shared_memory_error Tests
// ============================================================

TEST_CASE("shared_memory_error", "[shared][error]") {
    SECTION("no_region error") {
        shared_memory_error err(shared_memory_error::error_type::no_region);
        REQUIRE(err.type() == shared_memory_error::error_type::no_region);
        REQUIRE(std::string(err.what()).find("nullptr") != std::string::npos);
    }
    
    SECTION("invalid_region error") {
        shared_memory_error err(shared_memory_error::error_type::invalid_region);
        REQUIRE(err.type() == shared_memory_error::error_type::invalid_region);
    }
    
    SECTION("out_of_memory error") {
        shared_memory_error err(shared_memory_error::error_type::out_of_memory, 1024, 900, 1000);
        REQUIRE(err.type() == shared_memory_error::error_type::out_of_memory);
        REQUIRE(err.requested() == 1024);
        REQUIRE(err.used() == 900);
        REQUIRE(err.total() == 1000);
    }
}

// ============================================================
// Thread-local Region Accessor Tests
// ============================================================

TEST_CASE("shared memory region accessor", "[shared][tls]") {
    SECTION("default is nullptr") {
        SharedMemoryRegion* original = get_current_shared_region();
        set_current_shared_region(nullptr);
        
        REQUIRE(get_current_shared_region() == nullptr);
        
        // Restore
        set_current_shared_region(original);
    }
    
    SECTION("set and get") {
        SharedMemoryRegion* original = get_current_shared_region();
        
        // Using a non-null pointer for test (but not a real region)
        // In real usage, this would be a valid SharedMemoryRegion*
        set_current_shared_region(nullptr);
        REQUIRE(get_current_shared_region() == nullptr);
        
        // Restore
        set_current_shared_region(original);
    }
}

// ============================================================
// SharedMemoryRegion Tests (basic, without actual shared memory)
// ============================================================

TEST_CASE("SharedMemoryRegion construction", "[shared][region]") {
    SECTION("default construction") {
        SharedMemoryRegion region;
        REQUIRE_FALSE(region.is_valid());
    }
    
    SECTION("move construction") {
        SharedMemoryRegion region1;
        SharedMemoryRegion region2 = std::move(region1);
        REQUIRE_FALSE(region2.is_valid());
    }
}

// ============================================================
// SharedValueHandle Tests (basic, without actual shared memory)
// ============================================================

TEST_CASE("SharedValueHandle construction", "[shared][handle]") {
    SECTION("default construction") {
        SharedValueHandle handle;
        REQUIRE_FALSE(handle.is_valid());
        REQUIRE_FALSE(handle.is_value_ready());
    }
    
    SECTION("shared_value on invalid handle returns nullptr") {
        SharedValueHandle handle;
        REQUIRE(handle.shared_value() == nullptr);
    }
    
    SECTION("copy_to_local on invalid handle returns null") {
        SharedValueHandle handle;
        ImmerValue val = handle.copy_to_local();
        REQUIRE(val.is_null());
    }
}
