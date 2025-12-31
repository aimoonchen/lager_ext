# lager_ext API Guide

This document provides a comprehensive overview of the public APIs in the `lager_ext` library.

## Table of Contents

- [lager\_ext API Guide](#lager_ext-api-guide)
  - [Table of Contents](#table-of-contents)
  - [1. Value Types](#1-value-types)
    - [1.1 Type Aliases](#11-type-aliases)
    - [1.2 Supported Data Types](#12-supported-data-types)
    - [1.3 Construction](#13-construction)
    - [1.4 Type Checking](#14-type-checking)
    - [1.5 Value Access](#15-value-access)
    - [1.6 Modification (Immutable Operations)](#16-modification-immutable-operations)
    - [1.7 Comparison Operators (C++20)](#17-comparison-operators-c20)
  - [2. Builder API](#2-builder-api)
    - [2.1 Overview](#21-overview)
    - [2.2 MapBuilder](#22-mapbuilder)
    - [2.3 VectorBuilder](#23-vectorbuilder)
    - [2.4 ArrayBuilder \& TableBuilder](#24-arraybuilder--tablebuilder)
  - [3. Serialization](#3-serialization)
    - [3.1 Binary Serialization](#31-binary-serialization)
    - [3.2 JSON Serialization](#32-json-serialization)
  - [4. Path Operations](#4-path-operations)
    - [4.1 Path Types](#41-path-types)
    - [4.2 PathLens](#42-pathlens)
    - [4.3 Static Path Lens](#43-static-path-lens)
    - [4.4 Unified Path API (`path::`)](#44-unified-path-api-path)
    - [4.5 String Path Operations](#45-string-path-operations)
    - [4.6 Path Utilities](#46-path-utilities)
  - [5. Diff Operations](#5-diff-operations)
    - [5.1 DiffEntry](#51-diffentry)
    - [5.2 DiffEntryCollector](#52-diffentrycollector)
    - [5.3 DiffValueCollector (Tree-structured Result)](#53-diffvaluecollector-tree-structured-result)
    - [5.4 DiffNodeView (Optimized Access)](#54-diffnodeview-optimized-access)
    - [5.5 Quick Difference Check](#55-quick-difference-check)
    - [5.6 Choosing Between Collectors](#56-choosing-between-collectors)
  - [6. Shared State (Cross-Process)](#6-shared-state-cross-process)
    - [6.1 SharedState](#61-sharedstate)
    - [6.2 Memory Region Operations](#62-memory-region-operations)
  - [7. Header Reference](#7-header-reference)
  - [8. Usage Examples](#8-usage-examples)
    - [8.1 Basic Usage](#81-basic-usage)
    - [8.2 Working with Math Types](#82-working-with-math-types)
    - [8.3 Thread Safety](#83-thread-safety)
  - [Notes](#notes)

---

## 1. Value Types

### 1.1 Type Aliases

```cpp
#include <lager_ext/value.h>

// Default single-threaded Value (recommended for most use cases)
using Value = lager_ext::UnsafeValue;

// Thread-safe Value for multi-threaded scenarios
using SyncValue = lager_ext::ThreadSafeValue;
```

**Memory Policy Variants:**

| Type Alias | Memory Policy | Thread Safety | Performance |
|------------|---------------|---------------|-------------|
| `Value` (alias for `UnsafeValue`) | `unsafe_memory_policy` | Single-threaded only | Fastest (10-30% faster) |
| `SyncValue` (alias for `ThreadSafeValue`) | `thread_safe_memory_policy` | Thread-safe | Standard |

### 1.2 Supported Data Types

**Primitive Types:**

| C++ Type | Description | Example |
|----------|-------------|---------|
| `int8_t` | 8-bit signed integer | `Value(int8_t{42})` |
| `int16_t` | 16-bit signed integer | `Value(int16_t{1000})` |
| `int32_t` / `int` | 32-bit signed integer | `Value(42)` |
| `int64_t` | 64-bit signed integer | `Value(int64_t{9999999999})` |
| `uint8_t` | 8-bit unsigned integer | `Value(uint8_t{255})` |
| `uint16_t` | 16-bit unsigned integer | `Value(uint16_t{65535})` |
| `uint32_t` | 32-bit unsigned integer | `Value(100u)` |
| `uint64_t` | 64-bit unsigned integer | `Value(uint64_t{0})` |
| `float` | 32-bit floating point | `Value(3.14f)` |
| `double` | 64-bit floating point | `Value(3.14159265)` |
| `bool` | Boolean | `Value(true)` |
| `std::string` | String | `Value("hello")` |

**Math Types (for graphics/game development):**

| Type | Elements | Size | Storage |
|------|----------|------|---------|
| `Vec2` | 2 floats | 8 bytes | inline |
| `Vec3` | 3 floats | 12 bytes | inline |
| `Vec4` | 4 floats | 16 bytes | inline |
| `Mat3` | 9 floats (3x3) | 36 bytes | boxed (`immer::box`) |
| `Mat4x3` | 12 floats (4x3) | 48 bytes | boxed |
| `Mat4` | 16 floats (4x4) | 64 bytes | boxed |

> **Note:** Larger matrices are boxed (heap-allocated) to keep the `Value` variant size compact (~48 bytes).

**Container Types (Immutable):**

| Type | Description | Underlying Type |
|------|-------------|-----------------|
| `ValueMap` | Key-value map | `immer::map<string, ValueBox>` |
| `ValueVector` | Dynamic array | `immer::vector<ValueBox>` |
| `ValueArray` | Fixed-size array | `immer::array<ValueBox>` |
| `ValueTable` | ID-indexed table | `immer::table<TableEntry>` |

### 1.3 Construction

```cpp
#include <lager_ext/value.h>
using namespace lager_ext;

// Null
Value null_val;                         // Default is null
Value null_val2 = Value{std::monostate{}};

// Primitives
Value int_val(42);
Value double_val(3.14);
Value bool_val(true);
Value str_val("hello");

// Math types
Value vec2_val = Value::vec2(1.0f, 2.0f);
Value vec3_val = Value::vec3(1.0f, 2.0f, 3.0f);
Value vec4_val = Value::vec4(1.0f, 2.0f, 3.0f, 4.0f);
Value mat3_val = Value::identity_mat3();

// Using raw arrays
float data[3] = {1.0f, 2.0f, 3.0f};
Value vec3_from_ptr = Value::vec3(data);

// Factory functions for containers
Value my_map = Value::map({
    {"name", "Alice"},
    {"age", 30}
});

Value my_vector = Value::vector({1, 2, 3, "four", 5.0});

Value my_table = Value::table({
    {"id1", Value::map({{"name", "Item1"}})},
    {"id2", Value::map({{"name", "Item2"}})}
});
```

### 1.4 Type Checking

```cpp
// Check specific types
val.is_null();
val.is<int>();
val.is<std::string>();
val.is<ValueMap>();
val.is<ValueVector>();

// Math type checks
val.is_vec2();
val.is_vec3();
val.is_vec4();
val.is_mat3();
val.is_mat4x3();
val.is_math_type();  // any math type

// Get type index (std::variant index)
std::size_t idx = val.type_index();
```

### 1.5 Value Access

```cpp
// Safe access with default value (recommended)
int i = val.as_int(0);              // Returns 0 if not an int
double d = val.as_double(0.0);
float f = val.as_float(0.0f);
bool b = val.as_bool(false);
std::string s = val.as_string("");
int64_t l = val.as_int64(0);
double n = val.as_number(0.0);      // Converts any numeric type

// Math types
Vec2 v2 = val.as_vec2({});
Vec3 v3 = val.as_vec3({});
Mat3 m3 = val.as_mat3({});

// Containers
ValueMap m = val.as_map({});
ValueVector v = val.as_vector({});

// String view (no copy, returns empty if not string)
std::string_view sv = val.as_string_view();

// Generic template access
auto ptr = val.get_if<std::string>();  // returns const T* or nullptr
int i = val.get_or<int>(42);           // returns value or default

// Element access
Value name = obj.at("name");           // Map key access, returns null if not found
Value first = vec.at(0);               // Vector index access
Value with_default = obj.at_or("key", Value{42});

// Existence checks
bool has_key = obj.contains("name");
bool has_index = vec.contains(0);
std::size_t key_count = obj.count("name");  // 0 or 1

// Size
std::size_t sz = val.size();           // Container size, 0 for non-containers
```

### 1.6 Modification (Immutable Operations)

All modifications return a **new** `Value` (immutable semantics):

```cpp
// Set key in map (returns new map)
Value updated_obj = obj.set("email", "alice@example.com");

// Set index in vector (returns new vector)
Value updated_vec = vec.set(0, "new first");

// Vivify: auto-creates intermediate structures
Value new_obj = null_val.set_vivify("key", 42);      // {"key": 42}
Value new_vec = null_val.set_vivify(0, "first");     // ["first"]
```

### 1.7 Comparison Operators (C++20)

```cpp
Value a(42);
Value b(42);

a == b;   // true
a != b;   // false
a < b;    // Uses three-way comparison
a <=> b;  // std::partial_ordering (supports floating-point NaN)
```

---

## 2. Builder API

### 2.1 Overview

Builders provide **O(n)** construction of immutable containers using immer's transient API. Without builders, each modification would be O(log n), resulting in O(n log n) for building a container from scratch.

```cpp
#include <lager_ext/builders.h>
using namespace lager_ext;
```

**Available Builders:**

| Builder | Output Type | Thread-Safe Variant |
|---------|-------------|---------------------|
| `MapBuilder` | `Value` containing `ValueMap` | `SyncMapBuilder` |
| `VectorBuilder` | `Value` containing `ValueVector` | `SyncVectorBuilder` |
| `ArrayBuilder` | `Value` containing `ValueArray` | `SyncArrayBuilder` |
| `TableBuilder` | `Value` containing `ValueTable` | `SyncTableBuilder` |

### 2.2 MapBuilder

```cpp
// Basic usage
Value config = MapBuilder()
    .set("width", 1920)
    .set("height", 1080)
    .set("fullscreen", true)
    .finish();

// From existing map (incremental modification)
Value updated = MapBuilder(existing_map)
    .set("new_key", "new_value")
    .finish();

// Advanced operations
MapBuilder builder;
builder.set("counter", 0);

// Check existence
if (!builder.contains("name")) {
    builder.set("name", "default");
}

// Get a value
Value current = builder.get("counter");

// Update a value using function
builder.update_at("counter", [](const Value& v) {
    return Value{v.as_int(0) + 1};
});

// Upsert (update or insert)
builder.upsert("items", [](const Value& current) {
    if (current.is_null()) {
        return Value::vector({});
    }
    return current;
});

// Set at nested path with auto-vivification
builder.set_in({"users", size_t(0), "name"}, "Alice");

// Update at nested path
builder.update_in({"users", size_t(0), "age"}, [](const Value& v) {
    return Value{v.as_int(0) + 1};
});

Value result = builder.finish();
```

### 2.3 VectorBuilder

```cpp
// Build a vector
Value items = VectorBuilder()
    .push_back("item1")
    .push_back("item2")
    .push_back(42)
    .finish();

// Modify existing vector
Value updated = VectorBuilder(existing_vector)
    .push_back("new_item")
    .set(0, "modified_first")
    .finish();

// Get current size
VectorBuilder builder;
builder.push_back(1).push_back(2).push_back(3);
std::size_t sz = builder.size();  // 3

// Get value at index
Value second = builder.get(1);

// Update at index
builder.update_at(0, [](const Value& v) {
    return Value{v.as_int(0) * 2};
});
```

### 2.4 ArrayBuilder & TableBuilder

```cpp
// ArrayBuilder (similar to VectorBuilder)
Value arr = ArrayBuilder()
    .push_back(1)
    .push_back(2)
    .finish();

// TableBuilder (for ID-indexed collections)
Value entities = TableBuilder()
    .insert("player1", Value::map({{"hp", 100}, {"x", 0.0f}}))
    .insert("enemy1", Value::map({{"hp", 50}, {"x", 10.0f}}))
    .finish();

// Update existing entity
TableBuilder builder(entities);
builder.update("player1", [](const Value& player) {
    return player.set("hp", player.at("hp").as_int(0) - 10);
});
```

---

## 3. Serialization

### 3.1 Binary Serialization

```cpp
#include <lager_ext/serialization.h>
using namespace lager_ext;

Value data = MapBuilder()
    .set("name", "test")
    .set("values", Value::vector({1, 2, 3}))
    .finish();

// Serialize to buffer
ByteBuffer buffer = serialize(data);

// Deserialize from buffer
Value restored = deserialize(buffer);

// Get serialized size without serializing
std::size_t size = serialized_size(data);

// Serialize to pre-allocated buffer (zero-copy)
std::vector<uint8_t> my_buffer(size);
std::size_t written = serialize_to(data, my_buffer.data(), my_buffer.size());

// Deserialize from raw pointer (useful for memory-mapped files)
Value from_raw = deserialize(my_buffer.data(), my_buffer.size());
```

**Binary Format Type Tags:**

| Tag | Type | Data Size |
|-----|------|-----------|
| 0x00 | null | 0 bytes |
| 0x01 | int32 | 4 bytes (little-endian) |
| 0x02 | float | 4 bytes (IEEE 754) |
| 0x03 | double | 8 bytes (IEEE 754) |
| 0x04 | bool | 1 byte |
| 0x05 | string | 4-byte length + UTF-8 data |
| 0x06 | map | 4-byte count + key-value pairs |
| 0x07 | vector | 4-byte count + elements |
| 0x08 | array | 4-byte count + elements |
| 0x09 | table | 4-byte count + entries |
| 0x0A | int64 | 8 bytes |
| 0x0B-0x0F | other integers | varies |
| 0x10 | Vec2 | 8 bytes |
| 0x11 | Vec3 | 12 bytes |
| 0x12 | Vec4 | 16 bytes |
| 0x13 | Mat3 | 36 bytes |
| 0x14 | Mat4x3 | 48 bytes |
| 0x15 | Mat4 | 64 bytes |

### 3.2 JSON Serialization

```cpp
#include <lager_ext/serialization.h>

Value data = /* ... */;

// Convert to JSON (pretty-printed)
std::string json = to_json(data, false);

// Convert to JSON (compact)
std::string compact_json = to_json(data, true);

// Parse JSON
std::string error;
Value parsed = from_json(json, &error);
if (!error.empty()) {
    std::cerr << "Parse error: " << error << std::endl;
}

// Parse without error handling
Value parsed2 = from_json(json);
```

---

## 4. Path Operations

### 4.1 Path Types

```cpp
#include <lager_ext/path_utils.h>
using namespace lager_ext;

// PathElement: either a string key or numeric index
using PathElement = std::variant<std::string, std::size_t>;

// Path: sequence of path elements
using Path = std::vector<PathElement>;

// Example paths
Path p1 = {"users", size_t(0), "name"};     // .users[0].name
Path p2 = {"config", "settings", "theme"};  // .config.settings.theme
```

### 4.2 PathLens

`PathLens` provides type-erased runtime access to nested values:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// Create a path lens for nested access
PathLens path = make_path_lens({"users", size_t(0), "name"});

// Read value at path
Value name = path.view(root);

// Set value at path (returns new root)
Value new_root = path.set(root, "NewName");

// Update value at path
Value updated = path.over(root, [](const Value& v) {
    return Value{v.as_string("") + "_modified"};
});
```

### 4.3 Static Path Lens

Compile-time path composition using `lager::lenses`:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// Method 1: Variadic arguments (simple and recommended)
auto lens = static_path_lens("users", 0, "name");

// Method 2: String literal syntax (C++20 NTTP)
auto lens = static_path_lens<"/users/0/name">();

// Method 3: Explicit lens composition (full control)
auto lens = zug::comp(
    key_lens("users"),
    index_lens(0),
    key_lens("name")
);

// Use with lager::view / lager::set / lager::over
Value name = lager::view(lens, root);
Value new_root = lager::set(lens, root, "Alice");
```

> **Note:** `static_path_lens<"/a/0/b">()` and `static_path_lens("a", 0, "b")` are **functionally equivalent** with identical runtime performance. Both generate the same `RuntimePath` internally. The difference is purely syntactic:
> - Use `static_path_lens<"/path">()` for JSON Pointer style strings
> - Use `static_path_lens("a", 0, "b")` for variadic argument style

**Static Path Types (C++20):**

For compile-time path type definitions, use `LiteralPath` or `StaticPath`:

```cpp
#include <lager_ext/static_path.h>
using namespace lager_ext;

// Define path using string literal syntax (C++20 NTTP) - Recommended
using UserNamePath = LiteralPath<"/users/0/name">;

// Use static methods directly
Value name = UserNamePath::get(root);
Value new_root = UserNamePath::set(root, Value{"Alice"});

// Convert to runtime path if needed
Path runtime_path = UserNamePath::to_runtime_path();
```

> **Note:** Core types are aliased to the `lager_ext` namespace for convenience:
> - `lager_ext::LiteralPath` (alias for `lager_ext::static_path::LiteralPath`)
> - `lager_ext::StaticPath`, `lager_ext::K`, `lager_ext::I`

**Advanced: Path Composition with Segments**

For dynamic path construction or extending paths, use `StaticPath` with segment types:

```cpp
#include <lager_ext/static_path.h>
using namespace lager_ext;

// Equivalent to LiteralPath<"/users/0/name">
using UserNamePath = StaticPath<K<"users">, I<0>, K<"name">>;

// Extend an existing path
using UsersPath = LiteralPath<"/users">;
using FirstUserPath = ExtendPathT<UsersPath, I<0>>;           // /users/0
using FirstUserNamePath = ExtendPathT<FirstUserPath, K<"name">>; // /users/0/name

// Concatenate two paths
using FullPath = ConcatPathT<LiteralPath<"/users">, LiteralPath<"/0/name">>;

// Template with dynamic index (compile-time constant)
template<std::size_t N>
using NthUserName = StaticPath<K<"users">, I<N>, K<"name">>;

using User0Name = NthUserName<0>;  // /users/0/name
using User5Name = NthUserName<5>;  // /users/5/name
```

### 4.4 Unified Path API (`path::`)

For a single entry point to all path operations, use the `path` namespace:

```cpp
#include <lager_ext/path.h>
using namespace lager_ext;

// ========== Create Lenses ==========

// Compile-time string literal (C++20 NTTP)
auto lens1 = path::lens<"/users/0/name">();

// Runtime string path
std::string dynamic_path = "/users/" + std::to_string(user_id) + "/name";
auto lens2 = path::lens(dynamic_path);

// Variadic path elements
auto lens3 = path::lens("users", 0, "name");

// ========== Builder Style ==========

auto builder = path::builder() / "users" / 0 / "name";
Value name = builder.get(root);
Value updated = builder.set(root, Value{"Alice"});

// ========== Direct Access (without lens) ==========

// Get value at path
Value name = path::get(root, "/users/0/name");
Value name = path::get(root, "users", 0, "name");

// Set value at path
Value updated = path::set(root, "/users/0/name", Value{"Alice"});

// Update with function
Value updated = path::over(root, "/users/0/age", [](const Value& v) {
    return Value{v.as_int(0) + 1};
});

// ========== Safe Access (with error handling) ==========

Path elements = {"users", size_t(0), "name"};
PathAccessResult result = path::safe_get(root, elements);
if (result) {
    std::cout << "Found: " << result.value.as_string() << std::endl;
} else {
    std::cout << "Error: " << result.error_message << std::endl;
}

// ========== Utilities ==========

// Parse string to path elements
Path elements = path::parse("/users/0/name");

// Convert path elements to string
std::string str = path::to_string(elements);      // ".users[0].name"
std::string ptr = path::to_json_pointer(elements); // "/users/0/name"

// Cache management
path::clear_cache();
auto stats = path::cache_stats();
```

### 4.5 String Path Operations

Parse RFC 6901 JSON Pointer style paths:

```cpp
#include <lager_ext/string_path.h>
using namespace lager_ext;

// Parse a path string
Path path = parse_string_path("/users/0/name");
// Result: {"users", 0, "name"}

// Convert path to string
std::string str = path_to_string_path(path);
// Result: "/users/0/name"

// JSON Pointer escape sequences
// ~0 = literal ~
// ~1 = literal /
Path escaped = parse_string_path("/a~1b/c~0d");
// Result: {"a/b", "c~d"}
```

### 4.6 Path Utilities

```cpp
#include <lager_ext/path_utils.h>
using namespace lager_ext;

// Get value at path
Value val = get_at_path(root, {"users", size_t(0), "name"});

// Set value at path (returns new root)
Value new_root = set_at_path(root, {"users", size_t(0), "name"}, "Alice");

// Set with auto-vivification (creates intermediate structures)
Value new_root = set_at_path_vivify(Value{}, {"a", "b", "c"}, 123);
// Result: {"a": {"b": {"c": 123}}}

// Delete at path
Value without = delete_at_path(root, {"users", size_t(0)});
```

---

## 5. Diff Operations

### 5.1 DiffEntry

`DiffEntry` represents a single change between two values:

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

struct DiffEntry {
    enum class Type { Add, Remove, Change };
    
    Type type;           // Type of change
    Path path;           // Path to the changed value
    ValueBox old_value;  // Old value box (zero-copy reference)
    ValueBox new_value;  // New value box (zero-copy reference)
    
    // Convenience accessors
    const Value& get_old() const;  // Dereference old_value box
    const Value& get_new() const;  // Dereference new_value box
    const Value& value() const;    // Returns appropriate value based on type
};
```

> **Performance Note:** `DiffEntry` uses `ValueBox` (alias for `immer::box<Value>`) internally for zero-copy storage. When you access values via `get_old()` or `get_new()`, you get a reference to the original data without any copying.

### 5.2 DiffEntryCollector

`DiffEntryCollector` compares two values and collects all differences as a flat list:

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

Value old_val = /* ... */;
Value new_val = /* ... */;

// Create a collector and compute differences
DiffEntryCollector collector;
collector.diff(old_val, new_val);  // recursive by default

// Check if there are any changes
if (collector.has_changes()) {
    // Get all differences as a vector
    const auto& diffs = collector.get_diffs();
    
    for (const auto& entry : diffs) {
        switch (entry.type) {
            case DiffEntry::Type::Add:
                std::cout << "Added at " << path_to_string(entry.path) 
                          << ": " << to_json(entry.get_new(), true) << std::endl;
                break;
            case DiffEntry::Type::Remove:
                std::cout << "Removed at " << path_to_string(entry.path) 
                          << ": " << to_json(entry.get_old(), true) << std::endl;
                break;
            case DiffEntry::Type::Change:
                std::cout << "Changed at " << path_to_string(entry.path)
                          << ": " << to_json(entry.get_old(), true) 
                          << " -> " << to_json(entry.get_new(), true) << std::endl;
                break;
        }
    }
    
    // Or use built-in print
    collector.print_diffs();
}

// Non-recursive diff (only top-level changes)
collector.clear();
collector.diff(old_val, new_val, false);
```

### 5.3 DiffValueCollector (Tree-structured Result)

`DiffValueCollector` organizes diff results as a `Value` tree, mirroring the original data structure. This is more efficient than `DiffEntryCollector` when you need to process changes by path, as it builds the tree structure during traversal (single pass).

**Structure:**
- Intermediate nodes mirror the original structure (maps/vectors)
- Leaf nodes containing changes have special keys: `_diff_type`, `_old`, `_new`

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

Value old_val = Value::map({
    {"user", Value::map({
        {"name", "Alice"},
        {"age", 25}
    })}
});

Value new_val = Value::map({
    {"user", Value::map({
        {"name", "Bob"},      // changed
        {"age", 25},
        {"email", "bob@example.com"}  // added
    })}
});

// Compute diff as a Value tree
DiffValueCollector collector;
collector.diff(old_val, new_val);

// Get the result as a Value tree
const Value& tree = collector.get();

// Result structure:
// {
//   "user": {
//     "name": {
//       "_diff_type": 2,       // DiffEntry::Type::Change as uint8_t
//       "_old": "Alice",
//       "_new": "Bob"
//     },
//     "email": {
//       "_diff_type": 0,       // DiffEntry::Type::Add as uint8_t
//       "_new": "bob@example.com"
//     }
//   }
// }

// Print the diff tree
collector.print();

// Traverse the result like any Value
Value user_diffs = tree.at("user");
Value name_diff = user_diffs.at("name");

// Check if a node is a diff leaf, then get its type
if (DiffValueCollector::is_diff_node(name_diff)) {
    DiffEntry::Type type = DiffValueCollector::get_diff_type(name_diff);
    
    if (type == DiffEntry::Type::Change) {
        Value old_v = DiffValueCollector::get_old_value(name_diff);   // "Alice"
        Value new_v = DiffValueCollector::get_new_value(name_diff);   // "Bob"
        
        std::cout << "Name changed from " << old_v.as_string() 
                  << " to " << new_v.as_string() << std::endl;
    }
}

// Convenience function for one-liner usage
Value diff_tree = diff_as_value(old_val, new_val);
```

**Special Keys in Diff Nodes:**

| Key | Description | Value Type |
|-----|-------------|------------|
| `_diff_type` | Type of change | `uint8_t` (0=Add, 1=Remove, 2=Change) |
| `_old` | Previous value | Present for Remove and Change |
| `_new` | New value | Present for Add and Change |

> **Tip:** Access key names via `diff_keys::TYPE`, `diff_keys::OLD`, `diff_keys::NEW`.

**Static Helper Methods:**

| Method | Return Type | Description |
|--------|-------------|-------------|
| `DiffValueCollector::is_diff_node(val)` | `bool` | Check if a Value is a diff leaf node |
| `DiffValueCollector::get_diff_type(val)` | `DiffEntry::Type` | Get the diff type enum |
| `DiffValueCollector::get_old_value(val)` | `Value` | Get the old value from a diff node |
| `DiffValueCollector::get_new_value(val)` | `Value` | Get the new value from a diff node |

### 5.4 DiffNodeView (Optimized Access)

`DiffNodeView` is a lightweight accessor that parses a diff node once and provides O(1) access to all fields, avoiding repeated hash lookups. Use this when you need to access the same diff node multiple times.

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

// Get a diff node from DiffValueCollector result
Value diff_tree = diff_as_value(old_val, new_val);
Value name_diff = diff_tree.at("user").at("name");

// Parse once, access multiple times (O(1) after parse)
DiffNodeView view;
if (view.parse(name_diff)) {
    // Access type directly (no hash lookup)
    switch (view.type) {
        case DiffEntry::Type::Add:
            std::cout << "Added: " << view.new_value->as_string() << std::endl;
            break;
        case DiffEntry::Type::Remove:
            std::cout << "Removed: " << view.old_value->as_string() << std::endl;
            break;
        case DiffEntry::Type::Change:
            std::cout << "Changed: " << view.old_value->as_string() 
                      << " -> " << view.new_value->as_string() << std::endl;
            break;
    }
    
    // Convenience accessors
    const Value& meaningful = view.value();  // Returns appropriate value based on type
}

// Batch processing example - parse once per node
void process_diff_tree(const Value& node) {
    DiffNodeView view;
    if (view.parse(node)) {
        // This is a diff leaf node
        handle_change(view.type, view.old_value, view.new_value);
    } else if (auto* m = node.get_if<ValueMap>()) {
        // This is an intermediate node, recurse
        for (const auto& [key, child_box] : *m) {
            process_diff_tree(*child_box);
        }
    }
}
```

**DiffNodeView Members:**

| Member | Type | Description |
|--------|------|-------------|
| `type` | `DiffEntry::Type` | The type of change (Add/Remove/Change) |
| `old_value` | `const Value*` | Pointer to old value (nullptr if not present) |
| `new_value` | `const Value*` | Pointer to new value (nullptr if not present) |

**DiffNodeView Methods:**

| Method | Return Type | Description |
|--------|-------------|-------------|
| `parse(val)` | `bool` | Parse a Value into this view. Returns true if valid diff node. |
| `has_old()` | `bool` | Check if old_value is available |
| `has_new()` | `bool` | Check if new_value is available |
| `get_old()` | `const Value&` | Get old value (throws if not available) |
| `get_new()` | `const Value&` | Get new value (throws if not available) |
| `value()` | `const Value&` | Get meaningful value based on type |

> **Performance Tip:** Use `DiffNodeView` when accessing the same diff node multiple times. A single `parse()` call performs 3 hash lookups, but subsequent access to `type`, `old_value`, and `new_value` is O(1).

### 5.5 Quick Difference Check

For fast detection without collecting details:

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

// Quick check if any differences exist (faster than full diff)
if (has_any_difference(old_val, new_val)) {
    // Values are different
}

// Non-recursive check (only top-level)
if (has_any_difference(old_val, new_val, false)) {
    // Top-level values differ
}
```

### 5.6 Choosing Between Collectors

| Collector | Output | Best For |
|-----------|--------|----------|
| `DiffEntryCollector` | `std::vector<DiffEntry>` | Flat iteration over all changes |
| `DiffValueCollector` | `Value` tree | Hierarchical traversal, path-based access |

Both collectors use zero-copy internally with `ValueBox` for optimal performance

---

## 6. Shared State (Cross-Process)

### 6.1 SharedState

`SharedState` enables cross-process state synchronization:

```cpp
#include <lager_ext/shared_state.h>
using namespace lager_ext;

// Publisher side
SharedStatePublisher publisher("my_app_state");
publisher.set(create_sample_data());
publisher.update({"config", "theme"}, "light");

// Subscriber side (different process)
SharedStateSubscriber subscriber("my_app_state");
Value current = subscriber.get();

// Watch for changes
subscriber.watch([](const ValueDiff& diff) {
    for (const auto& change : diff.modified) {
        std::cout << "Changed: " << path_to_string(change.path) << std::endl;
    }
});
```

### 6.2 Memory Region Operations

```cpp
#include <lager_ext/shared_value.h>
using namespace lager_ext;

// Check if a memory region is initialized
bool ready = is_memory_region_initialized("my_region");

// Get a handle to a shared memory region
SharedMemoryHandle handle = get_shared_memory_region("my_region");

// Write Value to memory region
write_to_memory_region(handle, my_value);

// Read Value from memory region
Value value = read_from_memory_region(handle);

// Release the handle when done
release_memory_region(handle);
```

---

## 7. Header Reference

| Header | Description |
|--------|-------------|
| `<lager_ext/value.h>` | Core `Value` type, type aliases, comparison operators |
| `<lager_ext/builders.h>` | Builder classes for O(n) container construction |
| `<lager_ext/serialization.h>` | Binary and JSON serialization |
| `<lager_ext/path.h>` | **Unified Path API** - single entry point for all path operations |
| `<lager_ext/path_utils.h>` | Path traversal and manipulation |
| `<lager_ext/lager_lens.h>` | PathLens and static path lens integration |
| `<lager_ext/static_path.h>` | Compile-time static path lens (C++20 NTTP) |
| `<lager_ext/string_path.h>` | RFC 6901 JSON Pointer parsing |
| `<lager_ext/value_diff.h>` | Value difference detection |
| `<lager_ext/shared_state.h>` | Cross-process shared state |
| `<lager_ext/shared_value.h>` | Low-level shared memory operations |
| `<lager_ext/concepts.h>` | C++20 concepts for type constraints |
| `<lager_ext/editor_engine.h>` | Scene-like editor state management |
| `<lager_ext/delta_undo.h>` | Delta-based undo/redo system |
| `<lager_ext/multi_store.h>` | Multi-document state management |

---

## 8. Usage Examples

### 8.1 Basic Usage

```cpp
#include <lager_ext/value.h>
#include <lager_ext/builders.h>
#include <lager_ext/serialization.h>
using namespace lager_ext;

int main() {
    // Build a complex structure using builders (O(n) construction)
    Value state = MapBuilder()
        .set("config", MapBuilder()
            .set("debug", true)
            .set("timeout", 30)
            .finish())
        .set("users", VectorBuilder()
            .push_back(MapBuilder()
                .set("id", 1)
                .set("name", "Alice")
                .finish())
            .push_back(MapBuilder()
                .set("id", 2)
                .set("name", "Bob")
                .finish())
            .finish())
        .finish();

    // Read nested value using path
    Value name = state.at("users").at(0).at("name");
    std::cout << name.as_string() << std::endl;  // "Alice"

    // Update nested value (immutable - returns new state)
    Value new_state = set_at_path(state, 
        {"users", size_t(0), "name"}, "Charlie");

    // Original unchanged
    assert(state.at("users").at(0).at("name").as_string() == "Alice");
    assert(new_state.at("users").at(0).at("name").as_string() == "Charlie");

    // Serialize to JSON
    std::string json = to_json(state, false);
    std::cout << json << std::endl;

    // Serialize to binary
    ByteBuffer buffer = serialize(state);
    Value restored = deserialize(buffer);

    return 0;
}
```

### 8.2 Working with Math Types

```cpp
#include <lager_ext/value.h>
#include <lager_ext/builders.h>
using namespace lager_ext;

int main() {
    // Create a transform component
    Value transform = MapBuilder()
        .set("position", Value::vec3(0.0f, 0.0f, 0.0f))
        .set("rotation", Value::vec4(0.0f, 0.0f, 0.0f, 1.0f))  // quaternion
        .set("scale", Value::vec3(1.0f, 1.0f, 1.0f))
        .finish();

    // Read position
    Vec3 pos = transform.at("position").as_vec3();
    std::cout << "Position: " << pos[0] << ", " << pos[1] << ", " << pos[2] << std::endl;

    // Update position
    Value moved = transform.set("position", Value::vec3(10.0f, 0.0f, 5.0f));

    // Check type
    if (moved.at("rotation").is_vec4()) {
        Vec4 rot = moved.at("rotation").as_vec4();
        // ... use rotation
    }

    return 0;
}
```

### 8.3 Thread Safety

```cpp
#include <lager_ext/value.h>
#include <lager_ext/builders.h>
#include <thread>
using namespace lager_ext;

int main() {
    // For single-threaded code, use Value (default, fastest)
    Value local_state = MapBuilder()
        .set("counter", 0)
        .finish();

    // For multi-threaded code, use SyncValue
    SyncValue shared_state = SyncMapBuilder()
        .set("counter", 0)
        .finish();

    // Safe to read from multiple threads
    std::thread t1([&shared_state]() {
        SyncValue copy = shared_state;  // atomic refcount
        // ... read copy safely
    });

    std::thread t2([&shared_state]() {
        SyncValue copy = shared_state;  // atomic refcount
        // ... read copy safely
    });

    t1.join();
    t2.join();

    return 0;
}
```

---

## Notes

- All `Value` operations are **immutable** - modifications return new values.
- Use `Value` (alias for `UnsafeValue`) for single-threaded performance (10-30% faster).
- Use `SyncValue` (alias for `ThreadSafeValue`) when sharing values across threads.
- Use `SharedState` for cross-process state synchronization.
- Builders are essential for efficient O(n) construction of containers.
- C++20 features are used: concepts, `<=>`, `std::span`, `source_location`.
- Large matrices (Mat3, Mat4x3, Mat4) are boxed to keep `Value` variant size compact (~48 bytes).
- Path operations support both compile-time (`static_path_lens`) and runtime (`PathLens`) access patterns.
- Binary serialization uses native little-endian byte order (optimized for x86/x64).
