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
  - [4. Lens-Based Path System](#4-lens-based-path-system)
    - [4.1 Architecture Overview](#41-architecture-overview)
    - [4.2 Core Types](#42-core-types)
    - [4.3 Primitive Lenses](#43-primitive-lenses)
    - [4.4 PathLens (Runtime Paths)](#44-pathlens-runtime-paths)
    - [4.5 StaticPath (Compile-Time Paths)](#45-staticpath-compile-time-paths)
    - [4.6 Unified API (`path::` namespace)](#46-unified-api-path-namespace)
    - [4.7 ZoomedValue (Focused View)](#47-zoomedvalue-focused-view)
    - [4.8 PathWatcher (Change Detection)](#48-pathwatcher-change-detection)
    - [4.9 String Path Parsing (RFC 6901)](#49-string-path-parsing-rfc-6901)
    - [4.10 Core Path Engine (`path_core.h`)](#410-core-path-engine-path_coreh)
    - [4.11 Performance Considerations](#411-performance-considerations)
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
  - [7. Lager Library Integration](#7-lager-library-integration)
    - [7.1 Overview](#71-overview)
    - [7.2 zoom\_value() - Cursor/Reader Adapters](#72-zoom_value---cursorreader-adapters)
    - [7.3 value\_middleware() - Store Middleware](#73-value_middleware---store-middleware)
    - [7.4 watch\_path() - Path-based Subscriptions](#74-watch_path---path-based-subscriptions)
    - [7.5 PathWatcher vs watch\_path()](#75-pathwatcher-vs-watch_path)
  - [8. IPC (Inter-Process Communication)](#8-ipc-inter-process-communication)
    - [8.1 Overview](#81-overview)
    - [8.2 Channel (Unidirectional)](#82-channel-unidirectional)
      - [Factory Methods](#factory-methods)
      - [Producer Operations](#producer-operations)
      - [Consumer Operations](#consumer-operations)
      - [Properties](#properties)
    - [8.3 ChannelPair (Bidirectional)](#83-channelpair-bidirectional)
      - [Factory Methods](#factory-methods-1)
      - [Operations](#operations)
      - [Properties](#properties-1)
    - [8.4 Message Structure](#84-message-structure)
    - [8.5 Performance Characteristics](#85-performance-characteristics)
    - [8.6 Best Practices](#86-best-practices)
      - [Thread Safety Rules](#thread-safety-rules)
      - [Error Handling](#error-handling)
      - [Graceful Shutdown](#graceful-shutdown)
      - [Choosing Between APIs](#choosing-between-apis)
  - [9. Header Reference](#9-header-reference)
  - [10. Usage Examples](#10-usage-examples)
    - [10.1 Basic Usage](#101-basic-usage)
    - [10.2 Working with Math Types](#102-working-with-math-types)
    - [10.3 Thread Safety](#103-thread-safety)
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
// Generic template access (recommended for most types)
int i = val.as<int>(0);             // Returns 0 if not an int
double d = val.as<double>(0.0);
float f = val.as<float>(0.0f);
bool b = val.as<bool>(false);
int64_t l = val.as<int64_t>(0);

// Math types (via template)
Vec2 v2 = val.as<Vec2>();
Vec3 v3 = val.as<Vec3>();

// Special accessor functions (cannot be replaced by as<T>)
std::string s = val.as_string("");      // Supports move optimization
std::string_view sv = val.as_string_view(); // Zero-copy string view
double n = val.as_number(0.0);          // Heterogeneous numeric conversion
Mat3 m3 = val.as_mat3();                // Unboxing from immer::box
Mat4x3 m4x3 = val.as_mat4x3();          // Unboxing from immer::box

// Low-level pointer access
auto ptr = val.get_if<std::string>();   // Returns const T* or nullptr

// Element access
Value name = obj.at("name");           // Map key access, returns null if not found
Value first = vec.at(0);               // Vector index access

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

| Single-Threaded Builder | Output Type | Thread-Safe Variant |
|-------------------------|-------------|---------------------|
| `MapBuilder` | `Value` containing `ValueMap` | `SyncMapBuilder` |
| `VectorBuilder` | `Value` containing `ValueVector` | `SyncVectorBuilder` |
| `ArrayBuilder` | `Value` containing `ValueArray` | `SyncArrayBuilder` |
| `TableBuilder` | `Value` containing `ValueTable` | `SyncTableBuilder` |

> **Note:** Use the single-threaded builders (`MapBuilder`, etc.) with `Value` for best performance. Use `Sync*` variants with `SyncValue` when thread safety is required.

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
    return Value{v.as<int>(0) + 1};
});

// Upsert (update or insert)
builder.upsert("items", [](const Value& current) {
    if (current.is_null()) {
        return Value::vector({});
    }
    return current;
});

// Set at nested path with auto-vivification
builder.set_at_path({"users", size_t(0), "name"}, "Alice");

// Update at nested path
builder.update_at_path({"users", size_t(0), "age"}, [](const Value& v) {
    return Value{v.as<int>(0) + 1};
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
    return Value{v.as<int>(0) * 2};
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
    return player.set("hp", player.at("hp").as<int>(0) - 10);
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

## 4. Lens-Based Path System

The path system provides **lens-based access** to deeply nested values in `Value` trees. Built on top of `lager::lenses::getset`, it bridges compile-time type safety with runtime flexibility.

### 4.1 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                       User API Layer                            │
│  path::get/set/over    PathLens    StaticPath<"/a/b">          │
├─────────────────────────────────────────────────────────────────┤
│                      Lens Layer                                 │
│  key_lens(key)    index_lens(idx)    static_path_lens(...)     │
├─────────────────────────────────────────────────────────────────┤
│                    lager Foundation                             │
│  lager::lenses::getset    lager::view/set/over    zug::comp    │
└─────────────────────────────────────────────────────────────────┘
```

**Key Design Decisions:**

| Aspect | Our Lens | `lager::lenses::at` |
|--------|----------|---------------------|
| Return type | `Value` (null if missing) | `optional<Value>` |
| Multi-level path | Native support | Manual nesting |
| String paths | `Path{"/a/b"}` constructor | Not supported |
| Compile-time paths | `StaticPath<"/a/b">` | Not supported |

### 4.2 Core Types

```cpp
#include <lager_ext/path.h>
using namespace lager_ext;
using namespace std::string_view_literals;

// PathElement: string_view key or numeric index (compact: ~24 bytes)
using PathElement = std::variant<std::string_view, std::size_t>;

// PathView: Zero-allocation path (non-owning, for static/literal paths)
PathView pv = {{"users"sv, size_t(0), "name"sv}};

// Path: Owning path (for dynamic/runtime paths, implicit conversion to PathView)
Path p;
p.push_back(get_user_input());  // Copies into internal storage
p.push_back(0);
auto val = get_at_path(root, p);  // Path implicitly converts to PathView

// Path from JSON Pointer string (RFC 6901)
Path from_str{"/users/0/name"};           // Parse from string_view
Path from_move{std::move(json_pointer)};  // Zero-copy from rvalue string

// Convert Path back to JSON Pointer string
std::string str = from_str.to_string_path();  // "/users/0/name"
```

**Path Types Comparison:**

| Type | Ownership | Best For | Memory |
|------|-----------|----------|--------|
| `PathView` | Non-owning | Static/literal paths | Zero allocation |
| `Path` | Owning | Dynamic/runtime paths | Single contiguous buffer |

**Design Notes:**
- `PathElement` uses `std::string_view` (16 bytes) instead of `std::string` (32 bytes) for compact size
- `PathView` is a non-owning span - perfect for string literals and compile-time paths
- `Path` stores all keys in a contiguous buffer, avoiding per-key heap allocations
- All path functions accept `PathView`; `Path` implicitly converts to `PathView`
- `Path` can be constructed directly from JSON Pointer strings (`Path{"/a/b/c"}`)
- `Path` and `PathView` both have `to_string_path()` for serialization back to JSON Pointer format

### 4.3 Primitive Lenses

The building blocks for all path access:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// key_lens: access map key
auto name_lens = key_lens("name");
Value name = lager::view(name_lens, user);           // Get
Value updated = lager::set(name_lens, user, "Bob");  // Set

// index_lens: access vector index
auto first_lens = index_lens(0);
Value first = lager::view(first_lens, items);

// Composition with zug::comp
auto deep_lens = zug::comp(
    key_lens("users"),
    index_lens(0),
    key_lens("name")
);
Value name = lager::view(deep_lens, root);
```

### 4.4 PathLens (Runtime Paths)

`PathLens` wraps a runtime `Path` and implements the **lager lens protocol**, enabling direct use with `lager::view/set/over`.

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// ========== Construction ==========

// From Path vector
PathLens lens1(Path{"users", size_t(0), "name"});

// From string path (JSON Pointer) - Path constructor parses the string
PathLens lens2(Path{"/users/0/name"});

// Fluent builder with / operator
PathLens lens3 = PathLens() / "users" / 0 / "name";

// Global root constant
PathLens from_root = root / "config" / "theme";

// ========== Operations ==========

// Direct methods (recommended)
Value name = lens1.get(state);
Value updated = lens1.set(state, Value{"Alice"});
Value incremented = lens1.over(state, [](Value v) {
    return Value{v.as<int>(0) + 1};
});

// Lager integration (also works!)
Value name2 = lager::view(lens1, state);
Value updated2 = lager::set(lens1, state, Value{"Alice"});
Value inc2 = lager::over(lens1, state, [](Value v) {
    return Value{v.as<int>(0) + 1};
});

// ========== Inspection ==========

lens1.empty();           // true if root path
lens1.depth();           // number of elements
lens1.path();            // const Path&
lens1.to_string();       // ".users[0].name"
lens1.parent();          // PathLens to parent
lens1.concat(other);     // combine two paths
```

### 4.5 StaticPath (Compile-Time Paths)

For paths known at compile time, `StaticPath` provides **zero runtime overhead**:

```cpp
#include <lager_ext/static_path.h>
using namespace lager_ext;

// ========== Define Path Types ==========

// JSON Pointer syntax (C++20 NTTP) - Recommended
using UserNamePath = StaticPath<"/users/0/name">;
using ConfigTheme = StaticPath<"/config/theme">;

// Segment syntax (more flexible, for advanced use cases)
using UserNamePath2 = SegmentPath<K<"users">, I<0>, K<"name">>;

// ========== Use Directly ==========

Value name = UserNamePath::get(state);
Value updated = UserNamePath::set(state, Value{"Alice"});

// Compile-time depth
constexpr auto depth = UserNamePath::depth;  // 3

// Convert to runtime Path
Path runtime = UserNamePath::to_runtime_path();

// ========== Schema Definition Pattern ==========

namespace schema {
    // Define all paths for your data model
    using Title = StaticPath<"/title">;
    using WindowWidth = StaticPath<"/window/width">;
    using WindowHeight = StaticPath<"/window/height">;
    
    template<std::size_t N>
    using UserName = SegmentPath<K<"users">, I<N>, K<"name">>;
    
    template<std::size_t N>
    using UserAge = SegmentPath<K<"users">, I<N>, K<"age">>;
}

// Type-safe access
Value title = schema::Title::get(state);
Value user0_name = schema::UserName<0>::get(state);
Value user5_age = schema::UserAge<5>::get(state);
```

**Path Composition:**

```cpp
// Extend existing path
using UsersPath = StaticPath<"/users">;
using FirstUser = ExtendPathT<UsersPath, I<0>>;
using FirstUserName = ExtendPathT<FirstUser, K<"name">>;

// Concatenate two paths
using FullPath = ConcatPathT<
    StaticPath<"/users">,
    StaticPath<"/0/name">
>;
```

### 4.6 Unified API (`path::` namespace)

A single entry point for all path operations:

```cpp
#include <lager_ext/path.h>
using namespace lager_ext;

// ========== Create Lenses ==========

// Compile-time (recommended for fixed paths)
auto lens1 = path::lens<"/users/0/name">();

// Runtime string
auto lens2 = path::lens("/users/" + std::to_string(id) + "/name");

// Variadic elements
auto lens3 = path::lens("users", 0, "name");

// ========== Direct Access ==========

// Get
Value name = path::get(state, "/users/0/name");
Value name = path::get(state, "users", 0, "name");

// Set
Value updated = path::set(state, "/users/0/name", Value{"Alice"});

// Over (transform)
Value inc = path::over(state, "/counter", [](const Value& v) {
    return Value{v.as<int>(0) + 1};
});

// ========== Builder Style ==========

auto p = path::builder() / "users" / 0 / "name";
Value name = p.get(state);
Value updated = p.set(state, Value{"Alice"});

// ========== Safe Access ==========

PathAccessResult result = path::safe_get(state, {"users", size_t(0), "name"});
if (result) {
    use(result.value);
} else {
    log_error(result.error_message);
    // result.error_code, result.failed_at_index available
}

// ========== Utilities ==========

Path elements = path::parse("/users/0/name");
std::string dot_notation = path::to_string(elements);      // ".users[0].name"
std::string json_pointer = path::to_json_pointer(elements); // "/users/0/name"

// Cache management
path::clear_cache();
auto stats = path::cache_stats();  // hits, misses, hit_rate
```

### 4.7 ZoomedValue (Focused View)

A lightweight wrapper for navigating within a `Value` tree:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

Value state = /* ... */;

// Create zoomed view
ZoomedValue users = zoom(state) / "users";
ZoomedValue first_user = users / 0;

// Read values
Value name = (first_user / "name").get();
Value age = (first_user / "age").get();

// Write values (returns new root)
Value new_state = (first_user / "name").set(Value{"Alice"});

// Continue working with updated state
ZoomedValue updated_user = first_user.with_root(new_state);
Value new_name = (updated_user / "name").get();  // "Alice"

// Inspection
first_user.depth();    // 2
first_user.at_root();  // false
first_user.parent();   // ZoomedValue at /users
first_user.to_lens();  // PathLens{"/users/0"}
```

### 4.8 PathWatcher (Change Detection)

Monitor specific paths for changes between state snapshots. Optimized with **trie-based indexing** and **structural sharing pruning** for high performance.

> **Note:** `PathWatcher` is a standalone module in `<lager_ext/path_watcher.h>`. It is **independent of lager's cursor.watch()** system and does not require a lager store. This makes it suitable for:
> - Runtime-determined paths (e.g., from configuration or user input)
> - Cross-process state synchronization (`SharedState`)
> - Comparing arbitrary `Value` trees without a lager store

**Performance Optimizations:**
1. **Fast path equality**: Skips check entirely if states are identical
2. **Trie structure**: Shared path prefixes are only traversed once
3. **Structural sharing**: Uses immer's pointer identity for early pruning of unchanged subtrees

```cpp
#include <lager_ext/path_watcher.h>
using namespace lager_ext;

PathWatcher watcher;

// Register paths to watch
watcher.watch("/users/0/name", [](const Value& old_v, const Value& new_v) {
    std::cout << "Name: " << old_v.as_string() << " -> " << new_v.as_string() << "\n";
});

watcher.watch("/users/0/age", [](const Value& old_v, const Value& new_v) {
    std::cout << "Age changed\n";
});

watcher.watch("/config/theme", [](const Value& old_v, const Value& new_v) {
    update_ui_theme(new_v.as_string());
});

// Check for changes (call after state update)
Value old_state = /* previous */;
Value new_state = /* current */;

std::size_t triggered = watcher.check(old_state, new_state);
std::cout << "Triggered " << triggered << " callbacks\n";

// Management
watcher.unwatch("/config/theme");
watcher.clear();
watcher.size();   // number of watched paths
watcher.empty();  // true if no watches

// ========== Performance Statistics ==========

// Get statistics for monitoring and tuning
const auto& stats = watcher.stats();

std::cout << "Total checks: " << stats.total_checks << "\n";
std::cout << "Skipped (equal): " << stats.skipped_equal << "\n";
std::cout << "Trie nodes visited: " << stats.nodes_visited << "\n";
std::cout << "Nodes pruned: " << stats.nodes_pruned << "\n";
std::cout << "Callbacks triggered: " << stats.callbacks_triggered << "\n";

// Reset statistics
watcher.reset_stats();
```

**PathWatcher API Reference:**

| Method | Description |
|--------|-------------|
| `watch(path, callback)` | Register a callback for path changes |
| `unwatch(path)` | Remove all callbacks for a path |
| `clear()` | Remove all watches |
| `check(old, new)` | Compare states and trigger callbacks |
| `size()` | Number of registered watches |
| `empty()` | Check if no watches registered |
| `stats()` | Get performance statistics |
| `reset_stats()` | Reset statistics counters |

**Stats Structure:**

| Field | Description |
|-------|-------------|
| `total_checks` | Total number of `check()` calls |
| `skipped_equal` | Checks skipped due to equal states |
| `nodes_visited` | Trie nodes visited during checks |
| `nodes_pruned` | Subtrees pruned via structural sharing |
| `callbacks_triggered` | Total callbacks invoked |

> **Performance Tip:** When watching many paths with shared prefixes (e.g., `/users/0/name`, `/users/0/age`, `/users/0/email`), PathWatcher's trie structure ensures `/users/0` is only traversed once. Combined with structural sharing pruning, unchanged subtrees are skipped entirely.

### 4.9 String Path Parsing (RFC 6901)

`Path` class has built-in support for JSON Pointer (RFC 6901) parsing and serialization:

```cpp
#include <lager_ext/path.h>
using namespace lager_ext;

// ========== Parsing (String -> Path) ==========

// From string literal (zero-copy, optimal!)
Path path{"/users/0/name"};
// Result: elements = {"users", size_t(0), "name"}
// Note: String literals have static storage, no copy needed!

// From rvalue string (zero-copy, takes ownership)
std::string json_ptr = "/users/0/name";
Path path2{std::move(json_ptr)};  // No allocation, reuses buffer

// From string_view (copies the string)
std::string_view sv = get_path_from_somewhere();
Path path3{sv};  // Copies because sv may be temporary

// ========== Serialization (Path -> String) ==========

// Convert back to JSON Pointer string
std::string str = path.to_string_path();
// Result: "/users/0/name"

// PathView also supports serialization
PathView pv = path;
std::string str2 = pv.to_string_path();

// ========== Escape Sequences (RFC 6901) ==========
// ~0 = literal ~
// ~1 = literal /

Path escaped{"/config/theme~0mode"};        // key: "theme~mode"
Path with_slash{"/users/0/profile/tags~1skills"};  // key: "tags/skills"

// Round-trip preserves escaping
std::string round_trip = escaped.to_string_path();
// Result: "/config/theme~0mode"
```

**Path Construction Methods:**

| Method | Heap Allocation | String Copy | Best For |
|--------|-----------------|-------------|----------|
| `Path{"/a/b/c"}` | **No** | **Zero-copy** | String literals (optimal!) |
| `Path{std::string&&}` | **No** | **Zero-copy** | Dynamically built strings |
| `Path{std::string_view}` | Yes | Full copy | Temporary/dynamic string_view |

> **How It Works:** The compiler distinguishes between string literals (`const char[N]`) and `std::string_view`. When you write `Path{"/users/0/name"}`, the template `Path(const char (&)[N])` matches, which creates string_views pointing directly to the literal's static storage - zero allocation!

> **When to use each constructor:**
> ```cpp
> // ✅ String literal - zero-copy (uses const char[N] template)
> Path p1{"/config/theme"};
> 
> // ✅ Dynamic string you no longer need - zero-copy (takes ownership)
> std::string dynamic = build_path();
> Path p2{std::move(dynamic)};  // dynamic is now empty
> 
> // ✅ String view from external source - copies (safe)
> std::string_view external = get_path();
> Path p3{external};  // Copies because external may be temporary
> 
> // ✅ Dynamic path building - use push_back()
> Path p4;
> p4.push_back("users");
> p4.push_back(get_user_index());
> p4.push_back("name");
> ```

### 4.10 Core Path Engine (`path_core.h`)

The low-level path traversal engine used by all higher-level path abstractions.

**Key Optimizations (C++20):**
- **Zero-allocation traversal**: All functions accept `PathView`, avoiding heap allocations for literal paths
- **Transparent hashing**: Uses `is_transparent` in `TransparentStringHash` to query `immer::map` with `string_view` without creating temporary `std::string`
- **Branch prediction hints**: `[[unlikely]]` on null checks for early exit optimization
- **Inline functions**: Core traversal is inlined for maximum performance

```cpp
#include <lager_ext/path_core.h>
using namespace lager_ext;
using namespace std::string_view_literals;

// ========== Zero-Allocation Core Operations ==========

// Get value at path (returns null Value if path doesn't exist)
// Uses string_view literals for zero heap allocation
Value val = get_at_path(state, {{"users"sv, size_t(0), "name"sv}});

// Set value at path (strict mode - fails silently if path doesn't exist)
Value updated = set_at_path(state, {{"users"sv, size_t(0), "name"sv}}, Value{"Alice"});

// Set with auto-vivification (creates intermediate maps/vectors as needed)
Value new_state = set_at_path_vivify(Value{}, {{"a"sv, "b"sv, "c"sv}}, Value{123});
// Result: {"a": {"b": {"c": 123}}}

// Erase at path (for maps: removes key, for vectors: sets to null)
Value without_key = erase_at_path(state, {{"users"sv, size_t(0), "email"sv}});

// ========== Path Validation ==========

// Check if a path exists
bool exists = is_valid_path(state, {{"users"sv, size_t(0), "name"sv}});

// Get how deep a path can be traversed (0 to path.size())
std::size_t depth = valid_path_depth(state, {{"users"sv, size_t(99), "name"sv}});
// Returns 1 if users[99] doesn't exist

// ========== Dynamic Path Example ==========

// When keys come from runtime, use Path (owns its strings)
Path dynamic_path;
dynamic_path.push_back(user_input_key);  // Copied into internal buffer
dynamic_path.push_back(0);
Value val = get_at_path(state, dynamic_path);  // Implicit PathView conversion
```

**Core API Reference:**

| Function | Description |
|----------|-------------|
| `get_at_path(root, path)` | Get value at path, returns null if not found |
| `set_at_path(root, path, value)` | Set value at path (strict mode) |
| `set_at_path_vivify(root, path, value)` | Set value with auto-creation of intermediate nodes |
| `erase_at_path(root, path)` | Erase value at path |
| `is_valid_path(root, path)` | Check if entire path can be traversed |
| `valid_path_depth(root, path)` | Get number of path elements that exist |

> **Note:** All functions accept `PathView` as the path parameter. You can pass:
> - An initializer list with `string_view` literals: `{{"users"sv, 0, "name"sv}}`
> - A `Path` object (implicit conversion to `PathView`)
> - Any container with `data()` and `size()` returning `PathElement*` and `size_t`

### 4.11 Performance Considerations

| API | Use Case | Overhead |
|-----|----------|----------|
| `StaticPath<"/a/b">` | Fixed paths in code | **Zero** (compile-time) |
| `PathLens` + cache | Repeated runtime paths | Low (LRU cache hit) |
| `PathLens` no cache | One-off access | Medium (lens construction) |
| `get_at_path()` | Simple traversal | Low (no lens) |

**Best Practices:**

```cpp
// ✅ Good: Compile-time path for fixed access
using NamePath = StaticPath<"/users/0/name">;
Value name = NamePath::get(state);

// ✅ Good: Reuse PathLens for repeated access
PathLens user_lens = root / "users" / selected_id;
Value name = (user_lens / "name").get(state);
Value age = (user_lens / "age").get(state);

// ✅ Good: Direct access for one-off use
Value theme = path::get(state, "/config/theme");

// ❌ Avoid: Recreating lens in tight loop
for (int i = 0; i < 1000; ++i) {
    auto lens = PathLens() / "items" / i / "value";  // Recreated each iteration!
    process(lens.get(state));
}

// ✅ Better: Build path once, reuse
PathLens items = root / "items";
for (int i = 0; i < 1000; ++i) {
    auto lens = items / i / "value";
    process(lens.get(state));
}
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
                std::cout << "Added at " << entry.path.to_dot_notation() 
                          << ": " << to_json(entry.get_new(), true) << std::endl;
                break;
            case DiffEntry::Type::Remove:
                std::cout << "Removed at " << entry.path.to_dot_notation() 
                          << ": " << to_json(entry.get_old(), true) << std::endl;
                break;
            case DiffEntry::Type::Change:
                std::cout << "Changed at " << entry.path.to_dot_notation()
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
        std::cout << "Changed: " << change.path.to_dot_notation() << std::endl;
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

## 7. Lager Library Integration

The `lager_adapters.h` header provides seamless integration with the [lager](https://github.com/arximboldi/lager) library for reactive state management.

### 7.1 Overview

```cpp
#include <lager_ext/lager_adapters.h>
using namespace lager_ext;
```

This module bridges `lager_ext`'s `Value`-based API with lager's strongly-typed cursor/reader/store ecosystem:

| Function | Purpose |
|----------|---------|
| `zoom_value()` | Zoom a `lager::cursor<Value>` or `lager::reader<Value>` to a sub-path |
| `value_middleware()` | Store middleware for intercepting Value state changes |
| `value_diff_middleware()` | Convenience middleware that logs all state diffs |
| `watch_path()` | Watch a specific path for changes (reactive) |

### 7.2 zoom_value() - Cursor/Reader Adapters

Zoom a lager cursor or reader to a sub-path using `PathLens`, `Path`, or variadic elements:

```cpp
#include <lager_ext/lager_adapters.h>
using namespace lager_ext;

lager::cursor<Value> cursor = /* from lager store */;

// ========== Using PathLens ==========
auto name_cursor = zoom_value(cursor, root / "users" / 0 / "name");
Value name = name_cursor.get();
name_cursor.set(Value{"Alice"});

// ========== Using Path ==========
Path path{"users", size_t(0), "name"};
auto name_cursor2 = zoom_value(cursor, path);

// ========== Using variadic elements ==========
auto name_cursor3 = zoom_value(cursor, "users", 0, "name");

// ========== Works with readers too ==========
lager::reader<Value> reader = cursor;  // implicit conversion
auto name_reader = zoom_value(reader, "users" / 0 / "name");
Value name = name_reader.get();  // read-only
```

**API Reference:**

| Overload | Description |
|----------|-------------|
| `zoom_value(reader, PathLens)` | Zoom using a PathLens |
| `zoom_value(reader, Path)` | Zoom using a Path |
| `zoom_value(reader, elements...)` | Zoom using variadic path elements |

> **Note:** `zoom_value()` works with any lager reader or cursor type that has `value_type = Value`.

### 7.3 value_middleware() - Store Middleware

Create middleware that intercepts state changes in a lager store:

```cpp
#include <lager_ext/lager_adapters.h>
using namespace lager_ext;

// ========== Basic Usage ==========
auto store = lager::make_store<MyAction>(
    initial_state,
    lager::with_manual_event_loop{},
    value_middleware({
        .on_change = [](const Value& old_state, const Value& new_state) {
            std::cout << "State changed!\n";
            // Handle state change...
        }
    })
);

// ========== With Diff Logging ==========
auto store = lager::make_store<MyAction>(
    initial_state,
    lager::with_manual_event_loop{},
    value_middleware({
        .enable_diff_logging = true,
        .enable_deep_diff = true,
        .on_change = [](const Value& old_s, const Value& new_s) {
            // Custom handling
        }
    })
);

// ========== Convenience: Diff-Logging Middleware ==========
// Logs all state changes with detailed diff output
auto store = lager::make_store<MyAction>(
    initial_state,
    lager::with_manual_event_loop{},
    value_diff_middleware(/*recursive=*/true)
);
```

**ValueMiddlewareConfig Options:**

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_diff_logging` | `bool` | `false` | Log diffs to console |
| `enable_deep_diff` | `bool` | `true` | Use recursive diff |
| `on_change` | `function<void(Value, Value)>` | `nullptr` | Callback on state change |

### 7.4 watch_path() - Path-based Subscriptions

Watch a specific path in a lager reader/cursor for changes. The callback is triggered automatically by lager's event loop when the value at the path changes.

```cpp
#include <lager_ext/lager_adapters.h>
using namespace lager_ext;

lager::reader<Value> reader = /* from store */;

// ========== Watch with Path ==========
auto conn = watch_path(reader, Path{"users", size_t(0), "status"}, 
    [](const Value& new_status) {
        std::cout << "Status changed to: " << new_status.as_string() << "\n";
    }
);

// ========== Watch with PathLens ==========
auto conn2 = watch_path(reader, root / "config" / "theme", 
    [](const Value& theme) {
        update_ui_theme(theme.as_string());
    }
);

// ========== Disconnect when done ==========
conn.disconnect();
```

### 7.5 PathWatcher vs watch_path()

Both mechanisms monitor paths for changes, but they serve different use cases:

| Feature | `PathWatcher` | `watch_path()` |
|---------|---------------|----------------|
| **Trigger Mode** | Manual (`check(old, new)`) | Automatic (lager event loop) |
| **Requires lager** | No | Yes |
| **Multiple paths** | Optimized (Trie structure) | One subscription per path |
| **Structural sharing** | Explicit pruning | Relies on lager |
| **Statistics** | Built-in | None |
| **Best for** | Many paths, no lager store | Few paths, existing lager store |

**When to use which:**

```cpp
// ✅ Use watch_path() when:
// - You already have a lager store
// - You're watching a few paths
// - You want automatic reactive updates
auto conn = watch_path(store, "users" / 0 / "name", on_name_change);

// ✅ Use PathWatcher when:
// - You're watching many paths with shared prefixes
// - You don't have a lager store (e.g., cross-process sync)
// - You need performance statistics
PathWatcher watcher;
watcher.watch("/users/0/name", callback1);
watcher.watch("/users/0/age", callback2);
watcher.watch("/users/0/email", callback3);
// ... manually call watcher.check(old_state, new_state) after updates
```

---

## 8. IPC (Inter-Process Communication)

The IPC module provides high-performance, lock-free cross-process communication using shared memory. It is designed for scenarios requiring sub-microsecond latency, such as game engine ↔ editor communication.

> **Note:** This module requires building with `-DLAGER_EXT_ENABLE_IPC=ON` (Windows only).

### 8.1 Overview

```cpp
#include <lager_ext/ipc.h>
using namespace lager_ext::ipc;
```

The IPC module provides two main classes:

| Class | Description | Use Case |
|-------|-------------|----------|
| `Channel` | Unidirectional lock-free channel | One-way data streaming |
| `ChannelPair` | Bidirectional channel pair | Request/reply patterns |

**Key Features:**
- **Lock-free SPSC ring buffer** - Single Producer Single Consumer architecture
- **~100-600 ns round-trip latency** - Orders of magnitude faster than Windows messaging
- **~2-4 million messages/second** throughput
- **Zero system calls** in the hot path
- **Cache-line aligned** indices to prevent false sharing

### 8.2 Channel (Unidirectional)

`Channel` represents a one-way communication channel. One process creates the channel as a **producer** (sender), and another process attaches as a **consumer** (receiver).

#### Factory Methods

```cpp
/// Create channel as producer (creates shared memory)
static std::unique_ptr<Channel> createProducer(
    const std::string& name,
    size_t capacity = DEFAULT_CAPACITY  // 4096 messages
);

/// Attach to channel as consumer
static std::unique_ptr<Channel> createConsumer(const std::string& name);
```

**Example:**

```cpp
// Process A (Producer)
auto sender = Channel::createProducer("MyChannel", 8192);
if (!sender) {
    std::cerr << "Failed to create channel\n";
    return;
}

// Process B (Consumer)
auto receiver = Channel::createConsumer("MyChannel");
if (!receiver) {
    std::cerr << "Failed to attach to channel\n";
    return;
}
```

#### Producer Operations

```cpp
/// Send a Value (automatically serialized)
bool send(uint32_t msgId, const Value& data = {});

/// Send raw bytes (no serialization overhead)
bool sendRaw(uint32_t msgId, const void* data, size_t size);

/// Check if queue has space
bool canSend() const;

/// Get number of pending messages
size_t pendingCount() const;
```

**Example:**

```cpp
// Send structured data
Value playerState = MapBuilder()
    .set("x", 100.5f)
    .set("y", 200.0f)
    .set("health", 100)
    .finish();
sender->send(MSG_PLAYER_UPDATE, playerState);

// Send raw bytes (faster, no serialization)
struct Position { float x, y; };
Position pos{100.5f, 200.0f};
sender->sendRaw(MSG_POSITION, &pos, sizeof(pos));
```

#### Consumer Operations

```cpp
/// Received message structure
struct ReceivedMessage {
    uint32_t msgId;
    Value data;
    uint64_t timestamp;
};

/// Non-blocking receive (returns std::nullopt if empty)
std::optional<ReceivedMessage> tryReceive();

/// Blocking receive with timeout
std::optional<ReceivedMessage> receive(
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()
);

/// Raw bytes receive (no deserialization)
/// Returns: data size, 0 if empty, -1 if buffer too small
int tryReceiveRaw(uint32_t& outMsgId, void* outData, size_t maxSize);
```

**Example:**

```cpp
// Non-blocking polling
while (auto msg = receiver->tryReceive()) {
    switch (msg->msgId) {
        case MSG_PLAYER_UPDATE:
            handlePlayerUpdate(msg->data);
            break;
        case MSG_GAME_EVENT:
            handleGameEvent(msg->data);
            break;
    }
}

// Blocking receive with timeout
if (auto msg = receiver->receive(std::chrono::milliseconds(100))) {
    process(msg->data);
} else {
    std::cout << "Timeout waiting for message\n";
}
```

#### Properties

```cpp
const std::string& name() const;      // Channel name
bool isProducer() const;              // true if producer side
size_t capacity() const;              // Queue capacity
const std::string& lastError() const; // Last error message
```

### 8.3 ChannelPair (Bidirectional)

`ChannelPair` provides bidirectional communication by combining two `Channel` instances. It supports both asynchronous and synchronous (request/reply) patterns.

#### Factory Methods

```cpp
/// Create as endpoint A (creates both channels)
static std::unique_ptr<ChannelPair> createEndpointA(
    const std::string& name,
    size_t capacity = DEFAULT_CAPACITY
);

/// Attach as endpoint B
static std::unique_ptr<ChannelPair> createEndpointB(const std::string& name);
```

**Example:**

```cpp
// Game Engine (Endpoint A)
auto enginePair = ChannelPair::createEndpointA("EngineEditor", 4096);

// Editor (Endpoint B)
auto editorPair = ChannelPair::createEndpointB("EngineEditor");
```

#### Operations

```cpp
/// Send to the other endpoint
bool send(uint32_t msgId, const Value& data = {});

/// Non-blocking receive
std::optional<Channel::ReceivedMessage> tryReceive();

/// Blocking receive
std::optional<Channel::ReceivedMessage> receive(
    std::chrono::milliseconds timeout = std::chrono::milliseconds::max()
);

/// Synchronous request/reply (like SendMessage)
std::optional<Value> sendAndWaitReply(
    uint32_t msgId,
    const Value& data,
    std::chrono::milliseconds timeout = std::chrono::seconds(30)
);
```

**Example - Asynchronous:**

```cpp
// Engine sends updates
enginePair->send(MSG_SCENE_UPDATE, sceneData);

// Editor receives and processes
while (auto msg = editorPair->tryReceive()) {
    if (msg->msgId == MSG_SCENE_UPDATE) {
        updateSceneView(msg->data);
    }
}
```

**Example - Synchronous Request/Reply:**

```cpp
// Editor requests entity data from engine
Value request = MapBuilder()
    .set("entityId", 42)
    .finish();

if (auto reply = editorPair->sendAndWaitReply(MSG_GET_ENTITY, request)) {
    std::string name = reply->at("name").as_string();
    Vec3 pos = reply->at("position").as<Vec3>();
}
```

#### Properties

```cpp
const std::string& name() const;
bool isEndpointA() const;
const std::string& lastError() const;
```

### 8.4 Message Structure

Messages are stored in a fixed-size 256-byte structure optimized for cache efficiency:

```cpp
struct Message {
    uint32_t msgId;     // Message type identifier
    uint32_t dataSize;  // Data size in bytes
    uint64_t timestamp; // Optional timestamp

    static constexpr size_t INLINE_SIZE = 240;
    uint8_t inlineData[INLINE_SIZE];  // Inline storage for small data
};

static_assert(sizeof(Message) == 256);
```

- **Inline data (≤240 bytes):** Stored directly in the message, no extra allocation
- **Large data:** Must be serialized to fit within 240 bytes, or use raw byte API

### 8.5 Performance Characteristics

Benchmark results comparing IPC Channel to Windows native messaging (10,000 iterations):

| Method | Average Latency | Min Latency | Throughput |
|--------|-----------------|-------------|------------|
| **IPC Channel** | **~0.4-1.2 µs** | **~0.2 µs** | **~2-4M/sec** |
| PostMessage + Reply | ~19 µs | ~11 µs | ~50K/sec |
| SendMessage | ~237 µs | ~4 µs | ~4K/sec |
| WM_COPYDATA | ~180 µs | ~9 µs | ~5K/sec |

**Performance advantage:** 50-400x faster than Windows messaging APIs.

**Why so fast?**
1. **No kernel transitions** - Pure user-mode shared memory operations
2. **Lock-free design** - Uses `std::atomic` with release/acquire semantics
3. **Cache-optimized** - 256-byte messages align to cache lines
4. **False sharing prevention** - Producer/consumer indices on separate cache lines

### 8.6 Best Practices

#### Thread Safety Rules

```cpp
// ❌ WRONG: Multiple producers
std::thread t1([&]{ channel->send(1, data1); });
std::thread t2([&]{ channel->send(2, data2); });  // Data corruption!

// ✅ CORRECT: Single producer, single consumer
// Producer process/thread
channel->send(1, data);

// Consumer process/thread (different from producer)
channel->tryReceive();
```

#### Error Handling

```cpp
auto channel = Channel::createProducer("MyChannel");
if (!channel) {
    std::cerr << "Create failed: " << Channel::lastError() << "\n";
    return;
}

if (!channel->send(msgId, data)) {
    // Queue is full - back off or drop message
    std::cerr << "Queue full, pending: " << channel->pendingCount() << "\n";
}
```

#### Graceful Shutdown

```cpp
// Define shutdown message ID
constexpr uint32_t MSG_SHUTDOWN = 0xFFFFFFFF;

// Producer signals shutdown
sender->send(MSG_SHUTDOWN);

// Consumer handles shutdown
while (auto msg = receiver->tryReceive()) {
    if (msg->msgId == MSG_SHUTDOWN) {
        break;
    }
    process(msg->data);
}
```

#### Choosing Between APIs

| Scenario | Recommended API |
|----------|-----------------|
| One-way data streaming | `Channel` |
| Bidirectional communication | `ChannelPair` |
| High-frequency small updates | `sendRaw()` / `tryReceiveRaw()` |
| Complex structured data | `send()` with `Value` |
| Request/reply pattern | `ChannelPair::sendAndWaitReply()` |

---

## 9. Header Reference

| Header | Description |
|--------|-------------|
| `<lager_ext/value.h>` | Core `Value` type, type aliases, comparison operators |
| `<lager_ext/builders.h>` | Builder classes for O(n) container construction |
| `<lager_ext/serialization.h>` | Binary and JSON serialization |
| `<lager_ext/path.h>` | Core `PathView` (zero-allocation) and `Path` (owning) types |
| `<lager_ext/path_utils.h>` | Path traversal utilities (`get_at_path`, `set_at_path`, etc.) |
| `<lager_ext/path_watcher.h>` | Path change detection with trie-based optimization |
| `<lager_ext/lager_lens.h>` | PathLens, ZoomedValue and lager lens integration |
| `<lager_ext/lager_adapters.h>` | **Lager integration** - `zoom_value()`, middleware, `watch_path()` |
| `<lager_ext/static_path.h>` | Compile-time static path lens (C++20 NTTP) |
| `<lager_ext/value_diff.h>` | Value difference detection (`DiffEntryCollector`, `DiffValueCollector`) |
| `<lager_ext/shared_state.h>` | Cross-process shared state |
| `<lager_ext/shared_value.h>` | Low-level shared memory operations |
| `<lager_ext/ipc.h>` | **IPC module** - Lock-free cross-process `Channel` and `ChannelPair` |
| `<lager_ext/concepts.h>` | C++20 concepts and math type aliases (`Vec2`, `Vec3`, etc.) |
| `<lager_ext/editor_engine.h>` | Scene-like editor state management |
| `<lager_ext/delta_undo.h>` | Delta-based undo/redo system |
| `<lager_ext/multi_store.h>` | Multi-document state management |

---

## 10. Usage Examples

### 10.1 Basic Usage

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

### 10.2 Working with Math Types

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
    Vec3 pos = transform.at("position").as<Vec3>();
    std::cout << "Position: " << pos[0] << ", " << pos[1] << ", " << pos[2] << std::endl;

    // Update position
    Value moved = transform.set("position", Value::vec3(10.0f, 0.0f, 5.0f));

    // Check type
    if (moved.at("rotation").is_vec4()) {
        Vec4 rot = moved.at("rotation").as<Vec4>();
        // ... use rotation
    }

    return 0;
}
```

### 10.3 Thread Safety

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
