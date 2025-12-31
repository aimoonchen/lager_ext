# lager_ext API Guide

This document provides an overview of the public APIs in the `lager_ext` library.

## Table of Contents

- [lager\_ext API Guide](#lager_ext-api-guide)
  - [Table of Contents](#table-of-contents)
  - [1. Value Types](#1-value-types)
    - [1.1 Type Aliases](#11-type-aliases)
    - [1.2 Construction](#12-construction)
    - [1.3 Accessors](#13-accessors)
    - [1.4 Modification](#14-modification)
    - [1.5 Comparison](#15-comparison)
  - [2. Path Operations](#2-path-operations)
    - [2.1 PathLens](#21-pathlens)
    - [2.2 Static Path Lens](#22-static-path-lens)
    - [2.3 String Path Operations](#23-string-path-operations)
  - [3. Diff Operations](#3-diff-operations)
    - [3.1 DiffCollector](#31-diffcollector)
    - [3.2 RecursiveDiffCollector](#32-recursivediffcollector)
  - [4. Shared Memory](#4-shared-memory)
    - [4.1 SharedValue](#41-sharedvalue)
    - [4.2 Memory Region Operations](#42-memory-region-operations)
  - [Quick Reference](#quick-reference)
    - [Include Headers](#include-headers)
    - [Typical Usage Pattern](#typical-usage-pattern)
  - [Notes](#notes)

---

## 1. Value Types

### 1.1 Type Aliases

```cpp
#include <lager_ext/value.h>

// Default single-threaded Value (recommended for most use cases)
using Value = lager_ext::UnsafeValue;

// Thread-safe Value for multi-threaded scenarios
using SyncValue = lager_ext::SyncValue;
```

**Underlying primitive types:**

| Type Alias      | Description                         |
|-----------------|-------------------------------------|
| `value_null`    | Null type (`std::monostate`)        |
| `value_bool`    | Boolean                             |
| `value_int`     | 64-bit signed integer (`int64_t`)   |
| `value_double`  | 64-bit floating point (`double`)    |
| `value_string`  | Immutable string (`immer::box<std::string>`) |
| `value_vector`  | Immutable vector of `Value`         |
| `value_map`     | Immutable map (`std::string` → `Value`) |

### 1.2 Construction

```cpp
#include <lager_ext/value.h>
using namespace lager_ext;

// Null
Value null_val;                        // Default is null
Value null_val2 = Value::null_value(); // Explicit null

// Boolean
Value bool_val(true);

// Integer
Value int_val(42);
Value int_val2(int64_t{100});

// Double
Value double_val(3.14);

// String
Value str_val("hello");
Value str_val2(std::string("world"));

// Vector (using builder)
Value vec = Value::vector_builder()
    .push_back(1)
    .push_back("two")
    .push_back(3.0)
    .build();

// Map (using builder)
Value obj = Value::map_builder()
    .set("name", "Alice")
    .set("age", 30)
    .set("active", true)
    .build();

// Nested structures
Value nested = Value::map_builder()
    .set("users", Value::vector_builder()
        .push_back(Value::map_builder()
            .set("id", 1)
            .set("name", "Bob")
            .build())
        .build())
    .build();
```

### 1.3 Accessors

```cpp
// Type checking
val.is_null();
val.is_bool();
val.is_int();
val.is_double();
val.is_string();
val.is_vector();
val.is_map();

// Direct access (throws if type mismatch)
bool b         = val.as_bool();
int64_t i      = val.as_int();
double d       = val.as_double();
std::string s  = val.as_string();
value_vector v = val.as_vector();
value_map m    = val.as_map();

// Safe access with default value
bool b         = val.get_bool(false);
int64_t i      = val.get_int(0);
double d       = val.get_double(0.0);
std::string s  = val.get_string("");

// Map key access
Value name = obj["name"];              // Returns null if key not found
Value name = obj.at("name");           // Same as operator[]

// Vector index access
Value first = vec[0];                  // Returns null if out of bounds
Value first = vec.at(0);               // Same as operator[]

// Size operations
size_t sz = val.size();                // Vector/Map size, 0 for others
bool empty = val.empty();              // True if size == 0 or null

// Check if key exists in map
bool has = obj.contains("name");
```

### 1.4 Modification

All modifications return a **new** `Value` (immutable semantics):

```cpp
// Set a key in a map
Value updated_obj = obj.set("email", "alice@example.com");

// Nested set using path
Value updated = val.set_at_path({"users", 0, "name"}, "Charlie");

// Push to vector (returns new vector)
Value new_vec = val.push_back(42);

// Erase from map
Value without_email = obj.erase("email");

// Update value at key with a function
Value updated = obj.update("counter", [](const Value& v) {
    return Value(v.get_int(0) + 1);
});

// Vivify: Create intermediate structures as needed
Value result = set_at_path_vivify(Value(), {"a", "b", "c"}, 123);
// Result: {"a": {"b": {"c": 123}}}
```

### 1.5 Comparison

```cpp
Value a(42);
Value b(42);

a == b;   // true
a != b;   // false
a < b;    // Comparison operators available
```

---

## 2. Path Operations

### 2.1 PathLens

`PathLens` provides type-erased access to nested values:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// Create a path lens for nested access
PathLens path = make_path_lens({"users", 0, "name"});

// Read value at path
Value name = path.view(root);

// Set value at path (returns new root)
Value new_root = path.set(root, "NewName");

// Update value at path
Value updated = path.over(root, [](const Value& v) {
    return Value(v.get_string("") + "_modified");
});
```

### 2.2 Static Path Lens

Compile-time path composition using `lager::lenses`:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// Method 1: Variadic arguments (simple and recommended)
auto lens = static_path_lens("users", 0, "name");

// Method 2: Explicit lens composition (full control)
auto lens = zug::comp(
    key_lens("users"),
    index_lens(0),
    key_lens("name")
);

// Use with lager::view / lager::set / lager::over
Value name = lager::view(lens, root);
Value new_root = lager::set(lens, root, "Alice");
```

**Static Path with JSON Pointer Syntax (C++20):**

For compile-time string paths using JSON Pointer syntax, use `StaticPath` from `static_path.h`:

```cpp
#include <lager_ext/static_path.h>
using namespace lager_ext::static_path;

// Define path using JSON Pointer syntax (C++20 NTTP)
using UserNamePath = JsonPointerPath<"/users/0/name">;

// Use static methods directly
Value name = UserNamePath::get(root);
Value new_root = UserNamePath::set(root, Value{"Alice"});

// Or get a lens object for lager compatibility
auto lens = UserNamePath::to_lens();
Value name = lens.get(root);
```

### 2.3 String Path Operations

Parse RFC 6901 JSON Pointer style paths:

```cpp
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

// Parse a path string (supports "/" separator and "~" escape)
std::vector<PathElement> path = parse_string_path("/users/0/name");
// Result: {"users", 0, "name"}

// Access using string path
PathLens lens = make_path_lens_from_string("/users/0/name");
Value name = lens.view(root);

// Set at path with auto-vivification
Value result = set_at_path_vivify(root, "/a/b/c", 42);
```

**Path Element Types:**

| Type         | Description                        | Example      |
|--------------|------------------------------------|--------------|
| String key   | Map key access                     | `"name"`     |
| Integer index| Vector index access                | `0`, `1`     |

---

## 3. Diff Operations

### 3.1 DiffCollector

Collects shallow differences between two values:

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

Value old_val = /* ... */;
Value new_val = /* ... */;

DiffCollector diff;
diff.collect(old_val, new_val);

for (const auto& entry : diff.result()) {
    // entry.path   - Path to the changed element
    // entry.from   - Old value (null if added)
    // entry.to     - New value (null if removed)
    // entry.type   - DiffType::Add / Modify / Remove
}
```

### 3.2 RecursiveDiffCollector

Collects differences recursively, including nested changes:

```cpp
#include <lager_ext/value_diff.h>
using namespace lager_ext;

RecursiveDiffCollector diff;
diff.collect(old_val, new_val);

// Same interface as DiffCollector
for (const auto& entry : diff.result()) {
    // Process recursive diffs
}
```

**DiffEntry Structure:**

```cpp
struct DiffEntry {
    std::vector<PathElement> path;  // Path to changed element
    Value from;                     // Previous value
    Value to;                       // New value
    DiffType type;                  // Add, Modify, or Remove
};
```

---

## 4. Shared Memory

### 4.1 SharedValue

`SharedValue` enables zero-copy sharing of immutable data across processes:

```cpp
#include <lager_ext/shared_value.h>
using namespace lager_ext;

// Convert Value to SharedValue for cross-process sharing
SharedValue shared = to_shared_value(my_value);

// Convert back to Value
Value restored = to_value(shared);
```

### 4.2 Memory Region Operations

```cpp
#include <lager_ext/shared_value.h>
using namespace lager_ext;

// Check if a memory region is initialized
bool ready = is_memory_region_initialized("my_region");

// Get a handle to a shared memory region
SharedMemoryHandle handle = get_shared_memory_region("my_region");

// Write SharedValue to memory region
write_to_memory_region(handle, shared_value);

// Read SharedValue from memory region
SharedValue value = read_from_memory_region(handle);

// Release the handle when done
release_memory_region(handle);
```

**SharedValue Types:**

| Type              | Description                       |
|-------------------|-----------------------------------|
| `SharedValue`     | Cross-process immutable value     |
| `SharedString`    | Shared memory string              |
| `SharedVector`    | Shared memory vector              |
| `SharedMap`       | Shared memory map                 |

---

## Quick Reference

### Include Headers

```cpp
#include <lager_ext/value.h>        // Value, UnsafeValue, SyncValue
#include <lager_ext/lager_lens.h>   // PathLens, path operations
#include <lager_ext/value_diff.h>   // DiffCollector, RecursiveDiffCollector
#include <lager_ext/shared_value.h> // SharedValue, memory regions
```

### Typical Usage Pattern

```cpp
#include <lager_ext/value.h>
#include <lager_ext/lager_lens.h>
using namespace lager_ext;

int main() {
    // Build a complex structure
    Value state = Value::map_builder()
        .set("config", Value::map_builder()
            .set("debug", true)
            .set("timeout", 30)
            .build())
        .set("users", Value::vector_builder()
            .push_back(Value::map_builder()
                .set("id", 1)
                .set("name", "Alice")
                .build())
            .build())
        .build();

    // Read nested value
    Value name = state["users"][0]["name"];
    std::cout << name.get_string("") << std::endl;  // "Alice"

    // Update nested value (immutable)
    Value new_state = state.set_at_path({"users", 0, "name"}, "Bob");

    // Original unchanged
    assert(state["users"][0]["name"].get_string("") == "Alice");
    assert(new_state["users"][0]["name"].get_string("") == "Bob");

    return 0;
}
```

---

## Notes

- All `Value` operations are **immutable** - modifications return new values.
- Use `UnsafeValue` (default `Value`) for single-threaded performance.
- Use `SyncValue` when sharing across threads.
- Use `SharedValue` for cross-process shared memory scenarios.
- Path operations support both compile-time (`static_path_lens`) and runtime (`PathLens`) access patterns.
