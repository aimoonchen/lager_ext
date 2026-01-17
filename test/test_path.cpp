// test_path.cpp - Tests for Path system
// Module 6: Path and PathView system

#include <catch2/catch_all.hpp>
#include <lager_ext/path.h>
#include <lager_ext/value.h>

#include <string>
#include <vector>

using namespace lager_ext;
using namespace std::string_view_literals;

// ============================================================
// PathElement Tests
// ============================================================

TEST_CASE("PathElement variants", "[path][element]") {
    SECTION("string key") {
        PathElement elem = "users"sv;
        REQUIRE(std::holds_alternative<std::string_view>(elem));
        REQUIRE(std::get<std::string_view>(elem) == "users");
    }
    
    SECTION("numeric index") {
        PathElement elem = std::size_t{42};
        REQUIRE(std::holds_alternative<std::size_t>(elem));
        REQUIRE(std::get<std::size_t>(elem) == 42);
    }
}

// ============================================================
// PathView Tests
// ============================================================

TEST_CASE("PathView construction", "[path][view]") {
    SECTION("default construction (empty)") {
        PathView path;
        REQUIRE(path.empty());
        REQUIRE(path.size() == 0);
    }
    
    SECTION("from initializer list") {
        PathView path = {"users"sv, std::size_t{0}, "name"sv};
        REQUIRE(path.size() == 3);
        REQUIRE_FALSE(path.empty());
    }
    
    SECTION("from pointer and size") {
        PathElement elements[] = {"a"sv, std::size_t{1}};
        PathView path{elements, 2};
        REQUIRE(path.size() == 2);
    }
}

TEST_CASE("PathView element access", "[path][view]") {
    PathView path = {"users"sv, std::size_t{0}, "name"sv};
    
    SECTION("operator[]") {
        REQUIRE(std::get<std::string_view>(path[0]) == "users");
        REQUIRE(std::get<std::size_t>(path[1]) == 0);
        REQUIRE(std::get<std::string_view>(path[2]) == "name");
    }
    
    SECTION("front and back") {
        REQUIRE(std::get<std::string_view>(path.front()) == "users");
        REQUIRE(std::get<std::string_view>(path.back()) == "name");
    }
}

TEST_CASE("PathView subpath", "[path][view]") {
    PathView path = {"a"sv, "b"sv, "c"sv, "d"sv};
    
    SECTION("subpath from start") {
        auto sub = path.subpath(1);
        REQUIRE(sub.size() == 3);
        REQUIRE(std::get<std::string_view>(sub[0]) == "b");
    }
    
    SECTION("subpath with count") {
        auto sub = path.subpath(1, 2);
        REQUIRE(sub.size() == 2);
        REQUIRE(std::get<std::string_view>(sub[0]) == "b");
        REQUIRE(std::get<std::string_view>(sub[1]) == "c");
    }
    
    SECTION("subpath beyond size returns empty") {
        auto sub = path.subpath(10);
        REQUIRE(sub.empty());
    }
}

TEST_CASE("PathView iteration", "[path][view]") {
    PathView path = {"a"sv, "b"sv, "c"sv};
    
    SECTION("range-based for loop") {
        std::vector<std::string> keys;
        for (const auto& elem : path) {
            if (auto* key = std::get_if<std::string_view>(&elem)) {
                keys.emplace_back(*key);
            }
        }
        REQUIRE(keys.size() == 3);
        REQUIRE(keys[0] == "a");
        REQUIRE(keys[1] == "b");
        REQUIRE(keys[2] == "c");
    }
}

TEST_CASE("PathView comparison", "[path][view]") {
    PathView path1 = {"users"sv, std::size_t{0}};
    PathView path2 = {"users"sv, std::size_t{0}};
    PathView path3 = {"users"sv, std::size_t{1}};
    PathView path4 = {"items"sv};
    
    SECTION("equal paths") {
        REQUIRE(path1 == path2);
    }
    
    SECTION("different values") {
        REQUIRE(path1 != path3);
    }
    
    SECTION("different lengths") {
        REQUIRE(path1 != path4);
    }
}

TEST_CASE("PathView serialization", "[path][view]") {
    SECTION("to_string_path (JSON Pointer format)") {
        PathView path = {"users"sv, std::size_t{0}, "name"sv};
        std::string str = path.to_string_path();
        REQUIRE(str == "/users/0/name");
    }
    
    SECTION("to_dot_notation") {
        PathView path = {"users"sv, std::size_t{0}, "name"sv};
        std::string str = path.to_dot_notation();
        REQUIRE(str == ".users[0].name");
    }
    
    SECTION("empty path to_dot_notation") {
        PathView path;
        std::string str = path.to_dot_notation();
        REQUIRE(str == "(root)");
    }
}

// ============================================================
// Path Tests
// ============================================================

TEST_CASE("Path construction", "[path]") {
    SECTION("default construction") {
        Path path;
        REQUIRE(path.empty());
        REQUIRE(path.size() == 0);
    }
    
    SECTION("from string literal (zero-copy)") {
        Path path{"/users/0/name"};
        REQUIRE(path.size() == 3);
        REQUIRE(std::get<std::string_view>(path[0]) == "users");
        REQUIRE(std::get<std::size_t>(path[1]) == 0);
        REQUIRE(std::get<std::string_view>(path[2]) == "name");
    }
    
    SECTION("from rvalue string") {
        Path path{std::string("/items/5/title")};
        REQUIRE(path.size() == 3);
    }
    
    SECTION("from string_view (copies)") {
        std::string_view sv = "/a/b";
        Path path{sv};
        REQUIRE(path.size() == 2);
    }
    
    SECTION("from PathView") {
        PathView view = {"x"sv, "y"sv};
        Path path{view};
        REQUIRE(path.size() == 2);
    }
}

TEST_CASE("Path push_back", "[path]") {
    Path path;
    
    SECTION("push_back string literal") {
        path.push_back("users");
        path.push_back("name");
        
        REQUIRE(path.size() == 2);
        REQUIRE(std::get<std::string_view>(path[0]) == "users");
        REQUIRE(std::get<std::string_view>(path[1]) == "name");
    }
    
    SECTION("push_back index") {
        path.push_back(std::size_t{42});
        REQUIRE(path.size() == 1);
        REQUIRE(std::get<std::size_t>(path[0]) == 42);
    }
    
    SECTION("push_back string_view") {
        std::string_view sv = "dynamic";
        path.push_back(sv);
        REQUIRE(std::get<std::string_view>(path[0]) == "dynamic");
    }
    
    SECTION("push_back rvalue string") {
        path.push_back(std::string("moved"));
        REQUIRE(std::get<std::string_view>(path[0]) == "moved");
    }
    
    SECTION("chained push_back") {
        path.push_back("a").push_back(std::size_t{1}).push_back("b");
        REQUIRE(path.size() == 3);
    }
}

TEST_CASE("Path pop_back", "[path]") {
    Path path;
    path.push_back("a");
    path.push_back("b");
    path.push_back("c");
    
    SECTION("pop removes last element") {
        path.pop_back();
        REQUIRE(path.size() == 2);
        REQUIRE(std::get<std::string_view>(path.back()) == "b");
    }
    
    SECTION("multiple pops") {
        path.pop_back();
        path.pop_back();
        REQUIRE(path.size() == 1);
        REQUIRE(std::get<std::string_view>(path[0]) == "a");
    }
}

TEST_CASE("Path clear", "[path]") {
    Path path;
    path.push_back("a");
    path.push_back("b");
    
    path.clear();
    
    REQUIRE(path.empty());
    REQUIRE(path.size() == 0);
}

TEST_CASE("Path reserve", "[path]") {
    Path path;
    path.reserve(100);
    
    REQUIRE(path.capacity() >= 100);
    REQUIRE(path.empty()); // Reserve doesn't add elements
}

TEST_CASE("Path copy semantics", "[path]") {
    Path original;
    original.push_back("users");
    original.push_back(std::size_t{0});
    
    SECTION("copy construction") {
        Path copy{original};
        
        REQUIRE(copy.size() == 2);
        REQUIRE(std::get<std::string_view>(copy[0]) == "users");
        REQUIRE(std::get<std::size_t>(copy[1]) == 0);
        
        // Original unchanged
        REQUIRE(original.size() == 2);
    }
    
    SECTION("copy assignment") {
        Path copy;
        copy = original;
        
        REQUIRE(copy.size() == 2);
        REQUIRE(original.size() == 2);
    }
}

TEST_CASE("Path move semantics", "[path]") {
    Path original;
    original.push_back("users");
    original.push_back(std::size_t{0});
    
    SECTION("move construction") {
        Path moved{std::move(original)};
        
        REQUIRE(moved.size() == 2);
        REQUIRE(std::get<std::string_view>(moved[0]) == "users");
    }
    
    SECTION("move assignment") {
        Path moved;
        moved = std::move(original);
        
        REQUIRE(moved.size() == 2);
    }
}

TEST_CASE("Path conversion to PathView", "[path]") {
    Path path;
    path.push_back("a");
    path.push_back(std::size_t{1});
    
    SECTION("implicit conversion") {
        PathView view = path;
        REQUIRE(view.size() == 2);
    }
    
    SECTION("explicit view()") {
        PathView view = path.view();
        REQUIRE(view.size() == 2);
    }
}

TEST_CASE("Path comparison", "[path]") {
    Path path1;
    path1.push_back("users");
    path1.push_back(std::size_t{0});
    
    Path path2;
    path2.push_back("users");
    path2.push_back(std::size_t{0});
    
    Path path3;
    path3.push_back("users");
    path3.push_back(std::size_t{1});
    
    SECTION("equal paths") {
        REQUIRE(path1 == path2);
    }
    
    SECTION("different paths") {
        REQUIRE(path1 != path3);
    }
    
    SECTION("compare with PathView") {
        PathView view = {"users"sv, std::size_t{0}};
        REQUIRE(path1 == view);
    }
}

TEST_CASE("Path serialization", "[path]") {
    Path path;
    path.push_back("users");
    path.push_back(std::size_t{0});
    path.push_back("name");
    
    SECTION("to_string_path") {
        std::string str = path.to_string_path();
        REQUIRE(str == "/users/0/name");
    }
    
    SECTION("to_dot_notation") {
        std::string str = path.to_dot_notation();
        REQUIRE(str == ".users[0].name");
    }
}

TEST_CASE("Path parsing edge cases", "[path][parsing]") {
    SECTION("root path") {
        Path path{"/"};
        REQUIRE(path.empty());
    }
    
    SECTION("single segment") {
        Path path{"/users"};
        REQUIRE(path.size() == 1);
        REQUIRE(std::get<std::string_view>(path[0]) == "users");
    }
    
    SECTION("numeric segments") {
        Path path{"/0/1/2"};
        REQUIRE(path.size() == 3);
        REQUIRE(std::get<std::size_t>(path[0]) == 0);
        REQUIRE(std::get<std::size_t>(path[1]) == 1);
        REQUIRE(std::get<std::size_t>(path[2]) == 2);
    }
    
    SECTION("mixed segments") {
        Path path{"/users/0/profile/settings/1"};
        REQUIRE(path.size() == 5);
    }
}

// ============================================================
// Path Assign Tests
// ============================================================

TEST_CASE("Path assign from iterators", "[path]") {
    Path original;
    original.push_back("a");
    original.push_back("b");
    original.push_back("c");
    
    Path target;
    target.push_back("x"); // Will be replaced
    
    target.assign(original.begin(), original.end());
    
    REQUIRE(target.size() == 3);
    REQUIRE(std::get<std::string_view>(target[0]) == "a");
    REQUIRE(std::get<std::string_view>(target[1]) == "b");
    REQUIRE(std::get<std::string_view>(target[2]) == "c");
}
