// value.cpp - Value type utilities and serialization

#include <lager_ext/value.h>
#include <lager_ext/builders.h>
#include <lager_ext/serialization.h>
#include <immer/map_transient.hpp>
#include <immer/vector_transient.hpp>
#include <immer/array_transient.hpp>
#include <immer/table_transient.hpp>

#include <cstring>  // for std::memcpy
#include <stdexcept>  // for std::runtime_error
#include <sstream>    // for std::ostringstream
#include <iomanip>    // for std::setprecision

namespace lager_ext {

namespace {

template<std::size_t N>
std::string format_float_array(const std::array<float, N>& arr, const char* name) {
    std::ostringstream oss;
    oss << name << "(";
    oss << std::setprecision(4);
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) oss << ", ";
        oss << arr[i];
    }
    oss << ")";
    return oss.str();
}

} // anonymous namespace

std::string value_to_string(const Value& val)
{
    return std::visit([](const auto& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + arg + "\"";
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return std::to_string(static_cast<int>(arg)) + "i8";
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return std::to_string(arg) + "i16";
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::to_string(arg) + "L";
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return std::to_string(static_cast<unsigned>(arg)) + "u8";
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            return std::to_string(arg) + "u16";
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            return std::to_string(arg) + "u";
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return std::to_string(arg) + "uL";
        } else if constexpr (std::is_same_v<T, float>) {
            return std::to_string(arg) + "f";
        } else if constexpr (std::is_same_v<T, double>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, Vec2>) {
            return format_float_array(arg, "vec2");
        } else if constexpr (std::is_same_v<T, Vec3>) {
            return format_float_array(arg, "vec3");
        } else if constexpr (std::is_same_v<T, Vec4>) {
            return format_float_array(arg, "vec4");
        } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
            return format_float_array(arg.get(), "mat3");
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
            return format_float_array(arg.get(), "mat4x3");
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
            return format_float_array(arg.get(), "mat4");
        } else if constexpr (std::is_same_v<T, ValueMap>) {
            return "{map:" + std::to_string(arg.size()) + "}";
        } else if constexpr (std::is_same_v<T, ValueVector>) {
            return "[vector:" + std::to_string(arg.size()) + "]";
        } else if constexpr (std::is_same_v<T, ValueArray>) {
            return "[array:" + std::to_string(arg.size()) + "]";
        } else if constexpr (std::is_same_v<T, ValueTable>) {
            return "<table:" + std::to_string(arg.size()) + ">";
        } else {
            return "null";
        }
    }, val.data);
}

void print_value(const Value& val, const std::string& prefix, std::size_t depth)
{
    std::visit(
        [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, std::string>) {
                std::cout << std::string(depth * 2, ' ') << prefix << arg << "\n";
            } else if constexpr (std::is_same_v<T, bool>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << (arg ? "true" : "false") << "\n";
            } else if constexpr (std::is_same_v<T, int8_t>) {
                std::cout << std::string(depth * 2, ' ') << prefix << static_cast<int>(arg) << "\n";
            } else if constexpr (std::is_same_v<T, uint8_t>) {
                std::cout << std::string(depth * 2, ' ') << prefix << static_cast<unsigned>(arg) << "\n";
            } else if constexpr (std::is_same_v<T, int16_t> ||
                                 std::is_same_v<T, int32_t> ||
                                 std::is_same_v<T, int64_t> ||
                                 std::is_same_v<T, uint16_t> ||
                                 std::is_same_v<T, uint32_t> ||
                                 std::is_same_v<T, uint64_t> ||
                                 std::is_same_v<T, float> ||
                                 std::is_same_v<T, double>) {
                std::cout << std::string(depth * 2, ' ') << prefix << arg << "\n";
            } else if constexpr (std::is_same_v<T, Vec2>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg, "vec2") << "\n";
            } else if constexpr (std::is_same_v<T, Vec3>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg, "vec3") << "\n";
            } else if constexpr (std::is_same_v<T, Vec4>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg, "vec4") << "\n";
            } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg.get(), "mat3") << "\n";
            } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg.get(), "mat4x3") << "\n";
            } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
                std::cout << std::string(depth * 2, ' ') << prefix
                          << format_float_array(arg.get(), "mat4") << "\n";
            } else if constexpr (std::is_same_v<T, ValueMap>) {
                for (const auto& [k, v] : arg) {
                    std::cout << std::string(depth * 2, ' ') << prefix << k << ":\n";
                    print_value(*v, "", depth + 1);
                }
            } else if constexpr (std::is_same_v<T, ValueVector>) {
                for (std::size_t i = 0; i < arg.size(); ++i) {
                    std::cout << std::string(depth * 2, ' ') << prefix << "["
                              << i << "]:\n";
                    print_value(*arg[i], "", depth + 1);
                }
            } else if constexpr (std::is_same_v<T, ValueArray>) {
                for (std::size_t i = 0; i < arg.size(); ++i) {
                    std::cout << std::string(depth * 2, ' ') << prefix << "("
                              << i << "):\n";
                    print_value(*arg[i], "", depth + 1);
                }
            } else if constexpr (std::is_same_v<T, ValueTable>) {
                for (const auto& entry : arg) {
                    std::cout << std::string(depth * 2, ' ') << prefix << "<"
                              << entry.id << ">:\n";
                    print_value(*entry.value, "", depth + 1);
                }
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                std::cout << std::string(depth * 2, ' ') << prefix << "null\n";
            }
        },
        val.data);
}

std::string path_to_string(const Path& path)
{
    std::string result;
    for (const auto& elem : path) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                result += "." + v;
            } else {
                result += "[" + std::to_string(v) + "]";
            }
        }, elem);
    }
    return result.empty() ? "/" : result;
}

Value create_sample_data()
{
    // Create user 1 using Builder API for O(n) construction
    Value user1 = MapBuilder()
        .set("name", Value{std::string{"Alice"}})
        .set("age", Value{25})
        .finish();

    // Create user 2 using Builder API
    Value user2 = MapBuilder()
        .set("name", Value{std::string{"Bob"}})
        .set("age", Value{30})
        .finish();

    // Create users array using Builder API
    Value users = VectorBuilder()
        .push_back(user1)
        .push_back(user2)
        .finish();

    // Create config using Builder API
    Value config = MapBuilder()
        .set("version", Value{1})
        .set("theme", Value{std::string{"dark"}})
        .finish();

    // Create root using Builder API
    return MapBuilder()
        .set("users", users)
        .set("config", config)
        .finish();
}

namespace {

// Type tags for binary format
enum class TypeTag : uint8_t {
    Null   = 0x00,
    Int32  = 0x01,  // int32_t (renamed from Int for clarity)
    Float  = 0x02,
    Double = 0x03,
    Bool   = 0x04,
    String = 0x05,
    Map    = 0x06,
    Vector = 0x07,
    Array  = 0x08,
    Table  = 0x09,
    Int64  = 0x0A,  // int64_t
    // New integer types (0x0B - 0x0F)
    Int8   = 0x0B,  // int8_t
    Int16  = 0x0C,  // int16_t
    UInt8  = 0x0D,  // uint8_t
    UInt16 = 0x0E,  // uint16_t
    UInt32 = 0x0F,  // uint32_t
    // Math types (0x10 - 0x15)
    Vec2   = 0x10,
    Vec3   = 0x11,
    Vec4   = 0x12,
    Mat3   = 0x13,
    Mat4x3 = 0x14,
    Mat4   = 0x15,  // 4x4 matrix
    // Extended integer types (0x16)
    UInt64 = 0x16,  // uint64_t
};

// Helper: write bytes to buffer
// OPTIMIZATION: Use memcpy batch writes instead of per-byte push_back
// This exploits native little-endian representation on x86/x64 architectures
class ByteWriter {
public:
    ByteBuffer buffer;

    // Single byte - no optimization needed
    void write_u8(uint8_t v) {
        buffer.push_back(v);
    }

    // 16-bit write with memcpy
    void write_u16(uint16_t v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    void write_i16(int16_t v) {
        write_u16(static_cast<uint16_t>(v));
    }

    // 32-bit write with memcpy (little-endian native on x86/x64)
    void write_u32(uint32_t v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    void write_i32(int32_t v) {
        write_u32(static_cast<uint32_t>(v));
    }

    // 32-bit float with memcpy
    void write_f32(float v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    // 64-bit double with memcpy
    void write_f64(double v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    // 64-bit integer with memcpy
    void write_i64(int64_t v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    void write_u64(uint64_t v) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(v));
        std::memcpy(buffer.data() + old_size, &v, sizeof(v));
    }

    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + s.size());
        std::memcpy(buffer.data() + old_size, s.data(), s.size());
    }

    // OPTIMIZATION: Batch write entire float array with single memcpy
    template<std::size_t N>
    void write_float_array(const std::array<float, N>& arr) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(arr));
        std::memcpy(buffer.data() + old_size, arr.data(), sizeof(arr));
    }

    // OPTIMIZATION: Batch write entire double array with single memcpy
    template<std::size_t N>
    void write_double_array(const std::array<double, N>& arr) {
        std::size_t old_size = buffer.size();
        buffer.resize(old_size + sizeof(arr));
        std::memcpy(buffer.data() + old_size, arr.data(), sizeof(arr));
    }
};

// Helper: read bytes from buffer
// OPTIMIZATION: Use memcpy batch reads instead of per-byte operations
// This exploits native little-endian representation on x86/x64 architectures
class ByteReader {
public:
    const uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    ByteReader(const uint8_t* d, std::size_t s) : data(d), size(s) {}

    bool has_bytes(std::size_t n) const {
        return pos + n <= size;
    }

    uint8_t read_u8() {
        if (!has_bytes(1)) throw std::runtime_error("Unexpected end of buffer");
        return data[pos++];
    }

    // 16-bit read with memcpy
    uint16_t read_u16() {
        if (!has_bytes(sizeof(uint16_t))) throw std::runtime_error("Unexpected end of buffer");
        uint16_t v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    int16_t read_i16() {
        return static_cast<int16_t>(read_u16());
    }

    // 32-bit read with memcpy
    uint32_t read_u32() {
        if (!has_bytes(sizeof(uint32_t))) throw std::runtime_error("Unexpected end of buffer");
        uint32_t v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    int32_t read_i32() {
        return static_cast<int32_t>(read_u32());
    }

    // 32-bit float with memcpy
    float read_f32() {
        if (!has_bytes(sizeof(float))) throw std::runtime_error("Unexpected end of buffer");
        float v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    // 64-bit double with memcpy
    double read_f64() {
        if (!has_bytes(sizeof(double))) throw std::runtime_error("Unexpected end of buffer");
        double v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    // 64-bit integer with memcpy
    int64_t read_i64() {
        if (!has_bytes(sizeof(int64_t))) throw std::runtime_error("Unexpected end of buffer");
        int64_t v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    uint64_t read_u64() {
        if (!has_bytes(sizeof(uint64_t))) throw std::runtime_error("Unexpected end of buffer");
        uint64_t v;
        std::memcpy(&v, data + pos, sizeof(v));
        pos += sizeof(v);
        return v;
    }

    std::string read_string() {
        uint32_t len = read_u32();
        if (!has_bytes(len)) throw std::runtime_error("Unexpected end of buffer");
        std::string s(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        return s;
    }

    // OPTIMIZATION: Batch read entire float array with single memcpy
    template<std::size_t N>
    std::array<float, N> read_float_array() {
        constexpr std::size_t byte_size = N * sizeof(float);
        if (!has_bytes(byte_size)) throw std::runtime_error("Unexpected end of buffer");
        std::array<float, N> arr;
        std::memcpy(arr.data(), data + pos, byte_size);
        pos += byte_size;
        return arr;
    }

    // OPTIMIZATION: Batch read entire double array with single memcpy
    template<std::size_t N>
    std::array<double, N> read_double_array() {
        constexpr std::size_t byte_size = N * sizeof(double);
        if (!has_bytes(byte_size)) throw std::runtime_error("Unexpected end of buffer");
        std::array<double, N> arr;
        std::memcpy(arr.data(), data + pos, byte_size);
        pos += byte_size;
        return arr;
    }
};

// Forward declarations
void serialize_value(ByteWriter& w, const Value& val);
Value deserialize_value(ByteReader& r);

void serialize_value(ByteWriter& w, const Value& val) {
    std::visit([&w](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Null));
        } else if constexpr (std::is_same_v<T, int8_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int8));
            w.write_u8(static_cast<uint8_t>(arg));
        } else if constexpr (std::is_same_v<T, int16_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int16));
            w.write_u8(static_cast<uint8_t>(arg & 0xFF));
            w.write_u8(static_cast<uint8_t>((arg >> 8) & 0xFF));
        } else if constexpr (std::is_same_v<T, int32_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int32));
            w.write_i32(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int64));
            w.write_i64(arg);
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt8));
            w.write_u8(arg);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt16));
            w.write_u8(static_cast<uint8_t>(arg & 0xFF));
            w.write_u8(static_cast<uint8_t>((arg >> 8) & 0xFF));
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt32));
            w.write_u32(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt64));
            w.write_i64(static_cast<int64_t>(arg));  // reuse i64 write
        } else if constexpr (std::is_same_v<T, float>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Float));
            w.write_f32(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Double));
            w.write_f64(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Bool));
            w.write_u8(arg ? 0x01 : 0x00);
        } else if constexpr (std::is_same_v<T, std::string>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::String));
            w.write_string(arg);
        } else if constexpr (std::is_same_v<T, ValueMap>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Map));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& [k, v] : arg) {
                w.write_string(k);
                serialize_value(w, *v);
            }
        } else if constexpr (std::is_same_v<T, ValueVector>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vector));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& v : arg) {
                serialize_value(w, *v);
            }
        } else if constexpr (std::is_same_v<T, ValueArray>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Array));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (std::size_t i = 0; i < arg.size(); ++i) {
                serialize_value(w, *arg[i]);
            }
        } else if constexpr (std::is_same_v<T, ValueTable>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Table));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& entry : arg) {
                w.write_string(entry.id);
                serialize_value(w, *entry.value);
            }
        } else if constexpr (std::is_same_v<T, Vec2>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec2));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Vec3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec3));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Vec4>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec4));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat3));
            w.write_float_array(arg.get());
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat4x3));
            w.write_float_array(arg.get());
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat4));
            w.write_float_array(arg.get());
        }
    }, val.data);
}

Value deserialize_value(ByteReader& r) {
    TypeTag tag = static_cast<TypeTag>(r.read_u8());

    switch (tag) {
        case TypeTag::Null:
            return Value{};

        // Integer types
        case TypeTag::Int8:
            return Value{static_cast<int8_t>(r.read_u8())};

        case TypeTag::Int16: {
            uint8_t lo = r.read_u8();
            uint8_t hi = r.read_u8();
            return Value{static_cast<int16_t>(lo | (hi << 8))};
        }

        case TypeTag::Int32:
            return Value{r.read_i32()};

        case TypeTag::Int64:
            return Value{r.read_i64()};

        case TypeTag::UInt8:
            return Value{r.read_u8()};

        case TypeTag::UInt16: {
            uint8_t lo = r.read_u8();
            uint8_t hi = r.read_u8();
            return Value{static_cast<uint16_t>(lo | (hi << 8))};
        }

        case TypeTag::UInt32:
            return Value{r.read_u32()};

        case TypeTag::UInt64:
            return Value{static_cast<uint64_t>(r.read_i64())};

        // Floating-point types
        case TypeTag::Float:
            return Value{r.read_f32()};

        case TypeTag::Double:
            return Value{r.read_f64()};

        case TypeTag::Bool:
            return Value{r.read_u8() != 0};

        case TypeTag::String:
            return Value{r.read_string()};

        // Container types
        case TypeTag::Map: {
            uint32_t count = r.read_u32();
            auto transient = ValueMap{}.transient();
            for (uint32_t i = 0; i < count; ++i) {
                std::string key = r.read_string();
                Value val = deserialize_value(r);
                transient.set(std::move(key), ValueBox{std::move(val)});
            }
            return Value{transient.persistent()};
        }

        case TypeTag::Vector: {
            uint32_t count = r.read_u32();
            auto transient = ValueVector{}.transient();
            for (uint32_t i = 0; i < count; ++i) {
                Value val = deserialize_value(r);
                transient.push_back(ValueBox{std::move(val)});
            }
            return Value{transient.persistent()};
        }

        case TypeTag::Array: {
            // Deserialize as immer::array to preserve type information
            // Note: immer::array's transient may not work with custom MemoryPolicy,
            // so we use std::vector + range constructor for O(n) construction.
            uint32_t count = r.read_u32();
            std::vector<ValueBox> temp;
            temp.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                Value val = deserialize_value(r);
                temp.emplace_back(std::move(val));
            }
            // Construct immer::array using move iterators for efficiency
            return Value{ValueArray(std::make_move_iterator(temp.begin()),
                                    std::make_move_iterator(temp.end()))};
        }

        case TypeTag::Table: {
            uint32_t count = r.read_u32();
            auto transient = ValueTable{}.transient();
            for (uint32_t i = 0; i < count; ++i) {
                std::string id = r.read_string();
                Value val = deserialize_value(r);
                transient.insert(TableEntry{std::move(id), ValueBox{std::move(val)}});
            }
            return Value{transient.persistent()};
        }

        // Math types
        case TypeTag::Vec2:
            return Value{r.read_float_array<2>()};

        case TypeTag::Vec3:
            return Value{r.read_float_array<3>()};

        case TypeTag::Vec4:
            return Value{r.read_float_array<4>()};

        case TypeTag::Mat3:
            return Value{Value::boxed_mat3{r.read_float_array<9>()}};

        case TypeTag::Mat4x3:
            return Value{Value::boxed_mat4x3{r.read_float_array<12>()}};

        case TypeTag::Mat4:
            return Value{Value::boxed_mat4{r.read_float_array<16>()}};

        default:
            throw std::runtime_error("Unknown type tag: " + std::to_string(static_cast<int>(tag)));
    }
}

std::size_t calc_serialized_size(const Value& val) {
    std::size_t size = 1; // type tag

    std::visit([&size](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            // no extra data
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
            size += 1;
        } else if constexpr (std::is_same_v<T, int16_t> || std::is_same_v<T, uint16_t>) {
            size += 2;
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>) {
            size += 4;
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
            size += 8;
        } else if constexpr (std::is_same_v<T, float>) {
            size += 4;
        } else if constexpr (std::is_same_v<T, double>) {
            size += 8;
        } else if constexpr (std::is_same_v<T, bool>) {
            size += 1;
        } else if constexpr (std::is_same_v<T, std::string>) {
            size += 4 + arg.size();
        } else if constexpr (std::is_same_v<T, ValueMap>) {
            size += 4; // count
            for (const auto& [k, v] : arg) {
                size += 4 + k.size(); // key string
                size += calc_serialized_size(*v);
            }
        } else if constexpr (std::is_same_v<T, ValueVector>) {
            size += 4; // count
            for (const auto& v : arg) {
                size += calc_serialized_size(*v);
            }
        } else if constexpr (std::is_same_v<T, ValueArray>) {
            size += 4; // count
            for (std::size_t i = 0; i < arg.size(); ++i) {
                size += calc_serialized_size(*arg[i]);
            }
        } else if constexpr (std::is_same_v<T, ValueTable>) {
            size += 4; // count
            for (const auto& entry : arg) {
                size += 4 + entry.id.size(); // id string
                size += calc_serialized_size(*entry.value);
            }
        } else if constexpr (std::is_same_v<T, Vec2>) {
            size += 2 * sizeof(float);  // 2 floats
        } else if constexpr (std::is_same_v<T, Vec3>) {
            size += 3 * sizeof(float);  // 3 floats
        } else if constexpr (std::is_same_v<T, Vec4>) {
            size += 4 * sizeof(float);  // 4 floats
        } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
            size += 9 * sizeof(float);  // 9 floats
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
            size += 12 * sizeof(float); // 12 floats
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
            size += 16 * sizeof(float); // 16 floats
        }
    }, val.data);

    return size;
}

} // anonymous namespace

ByteBuffer serialize(const Value& val) {
    ByteWriter w;
    w.buffer.reserve(calc_serialized_size(val));
    serialize_value(w, val);
    return std::move(w.buffer);
}

Value deserialize(const ByteBuffer& buffer) {
    return deserialize(buffer.data(), buffer.size());
}

Value deserialize(const uint8_t* data, std::size_t size) {
    if (size == 0) {
        return Value{};
    }
    ByteReader r(data, size);
    return deserialize_value(r);
}

std::size_t serialized_size(const Value& val) {
    return calc_serialized_size(val);
}

// Helper class to write directly to a pre-allocated buffer
// OPTIMIZATION: Use memcpy batch writes instead of per-byte operations
namespace {
class DirectByteWriter {
public:
    uint8_t* buffer;
    std::size_t capacity;
    std::size_t pos = 0;

    DirectByteWriter(uint8_t* buf, std::size_t cap) : buffer(buf), capacity(cap) {}

    void write_u8(uint8_t v) {
        if (pos >= capacity) throw std::runtime_error("Buffer overflow");
        buffer[pos++] = v;
    }

    // 16-bit write with memcpy
    void write_u16(uint16_t v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    void write_i16(int16_t v) {
        write_u16(static_cast<uint16_t>(v));
    }

    // 32-bit write with memcpy
    void write_u32(uint32_t v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    void write_i32(int32_t v) {
        write_u32(static_cast<uint32_t>(v));
    }

    // 32-bit float with memcpy
    void write_f32(float v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    // 64-bit double with memcpy
    void write_f64(double v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    // 64-bit integer with memcpy
    void write_i64(int64_t v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    void write_u64(uint64_t v) {
        if (pos + sizeof(v) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, &v, sizeof(v));
        pos += sizeof(v);
    }

    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        if (pos + s.size() > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, s.data(), s.size());
        pos += s.size();
    }

    // OPTIMIZATION: Batch write entire float array with single memcpy
    template<std::size_t N>
    void write_float_array(const std::array<float, N>& arr) {
        if (pos + sizeof(arr) > capacity) throw std::runtime_error("Buffer overflow");
        std::memcpy(buffer + pos, arr.data(), sizeof(arr));
        pos += sizeof(arr);
    }
};

void serialize_value_direct(DirectByteWriter& w, const Value& val);

void serialize_value_direct(DirectByteWriter& w, const Value& val) {
    std::visit([&w](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Null));
        } else if constexpr (std::is_same_v<T, int8_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int8));
            w.write_u8(static_cast<uint8_t>(arg));
        } else if constexpr (std::is_same_v<T, int16_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int16));
            w.write_u8(static_cast<uint8_t>(arg & 0xFF));
            w.write_u8(static_cast<uint8_t>((arg >> 8) & 0xFF));
        } else if constexpr (std::is_same_v<T, int32_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int32));
            w.write_i32(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Int64));
            w.write_i64(arg);
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt8));
            w.write_u8(arg);
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt16));
            w.write_u8(static_cast<uint8_t>(arg & 0xFF));
            w.write_u8(static_cast<uint8_t>((arg >> 8) & 0xFF));
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt32));
            w.write_u32(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::UInt64));
            w.write_i64(static_cast<int64_t>(arg));
        } else if constexpr (std::is_same_v<T, float>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Float));
            w.write_f32(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Double));
            w.write_f64(arg);
        } else if constexpr (std::is_same_v<T, bool>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Bool));
            w.write_u8(arg ? 0x01 : 0x00);
        } else if constexpr (std::is_same_v<T, std::string>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::String));
            w.write_string(arg);
        } else if constexpr (std::is_same_v<T, ValueMap>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Map));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& [k, v] : arg) {
                w.write_string(k);
                serialize_value_direct(w, *v);
            }
        } else if constexpr (std::is_same_v<T, ValueVector>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vector));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& v : arg) {
                serialize_value_direct(w, *v);
            }
        } else if constexpr (std::is_same_v<T, ValueArray>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Array));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (std::size_t i = 0; i < arg.size(); ++i) {
                serialize_value_direct(w, *arg[i]);
            }
        } else if constexpr (std::is_same_v<T, ValueTable>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Table));
            w.write_u32(static_cast<uint32_t>(arg.size()));
            for (const auto& entry : arg) {
                w.write_string(entry.id);
                serialize_value_direct(w, *entry.value);
            }
        } else if constexpr (std::is_same_v<T, Vec2>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec2));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Vec3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec3));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Vec4>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Vec4));
            w.write_float_array(arg);
        } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat3));
            w.write_float_array(arg.get());
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat4x3));
            w.write_float_array(arg.get());
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
            w.write_u8(static_cast<uint8_t>(TypeTag::Mat4));
            w.write_float_array(arg.get());
        }
    }, val.data);
}
} // anonymous namespace

std::size_t serialize_to(const Value& val, uint8_t* buffer, std::size_t buffer_size) {
    std::size_t required = calc_serialized_size(val);
    if (required > buffer_size) {
        throw std::runtime_error("Buffer too small: need " + std::to_string(required) +
                                 " bytes, got " + std::to_string(buffer_size));
    }
    DirectByteWriter w(buffer, buffer_size);
    serialize_value_direct(w, val);
    return w.pos;
}

// ============================================================
// JSON Serialization / Deserialization Implementation
// ============================================================

namespace {

// JSON escape special characters in strings
// OPTIMIZATION: Use string::reserve + append instead of ostringstream for better performance
std::string json_escape_string(const std::string& s) {
    std::string result;
    // Pre-allocate with some extra space for potential escapes
    result.reserve(s.size() + 16);

    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Control characters as \uXXXX
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned int>(static_cast<unsigned char>(c)));
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// Forward declaration
void to_json_impl(const Value& val, std::ostringstream& oss, bool compact, int indent_level);

// Write float array as JSON array
template<std::size_t N>
void write_float_array_json(const std::array<float, N>& arr, std::ostringstream& oss) {
    oss << "[";
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) oss << ",";
        oss << std::setprecision(7) << arr[i];
    }
    oss << "]";
}

void to_json_impl(const Value& val, std::ostringstream& oss, bool compact, int indent_level) {
    const std::string indent = compact ? "" : std::string(indent_level * 2, ' ');
    const std::string child_indent = compact ? "" : std::string((indent_level + 1) * 2, ' ');
    const std::string newline = compact ? "" : "\n";
    const std::string space_after_colon = compact ? "" : " ";

    std::visit([&](const auto& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            oss << "null";
        } else if constexpr (std::is_same_v<T, bool>) {
            oss << (arg ? "true" : "false");
        } else if constexpr (std::is_same_v<T, int8_t>) {
            oss << static_cast<int>(arg);
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            oss << static_cast<unsigned>(arg);
        } else if constexpr (std::is_same_v<T, int16_t> ||
                           std::is_same_v<T, int32_t> ||
                           std::is_same_v<T, int64_t> ||
                           std::is_same_v<T, uint16_t> ||
                           std::is_same_v<T, uint32_t> ||
                           std::is_same_v<T, uint64_t>) {
            oss << arg;
        } else if constexpr (std::is_same_v<T, float>) {
            oss << std::setprecision(7) << arg;
        } else if constexpr (std::is_same_v<T, double>) {
            oss << std::setprecision(15) << arg;
        } else if constexpr (std::is_same_v<T, std::string>) {
            oss << "\"" << json_escape_string(arg) << "\"";
        } else if constexpr (std::is_same_v<T, Vec2>) {
            write_float_array_json(arg, oss);
        } else if constexpr (std::is_same_v<T, Vec3>) {
            write_float_array_json(arg, oss);
        } else if constexpr (std::is_same_v<T, Vec4>) {
            write_float_array_json(arg, oss);
        } else if constexpr (std::is_same_v<T, Value::boxed_mat3>) {
            write_float_array_json(arg.get(), oss);
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4x3>) {
            write_float_array_json(arg.get(), oss);
        } else if constexpr (std::is_same_v<T, Value::boxed_mat4>) {
            write_float_array_json(arg.get(), oss);
        } else if constexpr (std::is_same_v<T, ValueMap>) {
            if (arg.size() == 0) {
                oss << "{}";
            } else {
                oss << "{" << newline;
                bool first = true;
                for (const auto& [k, v] : arg) {
                    if (!first) oss << "," << newline;
                    first = false;
                    oss << child_indent << "\"" << json_escape_string(k) << "\":" << space_after_colon;
                    to_json_impl(*v, oss, compact, indent_level + 1);
                }
                oss << newline << indent << "}";
            }
        } else if constexpr (std::is_same_v<T, ValueVector>) {
            if (arg.size() == 0) {
                oss << "[]";
            } else {
                oss << "[" << newline;
                bool first = true;
                for (const auto& v : arg) {
                    if (!first) oss << "," << newline;
                    first = false;
                    oss << child_indent;
                    to_json_impl(*v, oss, compact, indent_level + 1);
                }
                oss << newline << indent << "]";
            }
        } else if constexpr (std::is_same_v<T, ValueArray>) {
            if (arg.size() == 0) {
                oss << "[]";
            } else {
                oss << "[" << newline;
                for (std::size_t i = 0; i < arg.size(); ++i) {
                    if (i > 0) oss << "," << newline;
                    oss << child_indent;
                    to_json_impl(*arg[i], oss, compact, indent_level + 1);
                }
                oss << newline << indent << "]";
            }
        } else if constexpr (std::is_same_v<T, ValueTable>) {
            if (arg.size() == 0) {
                oss << "{}";
            } else {
                oss << "{" << newline;
                bool first = true;
                for (const auto& entry : arg) {
                    if (!first) oss << "," << newline;
                    first = false;
                    oss << child_indent << "\"" << json_escape_string(entry.id) << "\":" << space_after_colon;
                    to_json_impl(*entry.value, oss, compact, indent_level + 1);
                }
                oss << newline << indent << "}";
            }
        }
    }, val.data);
}

// ============================================================
// Simple JSON Parser
// ============================================================

class JsonParser {
public:
    JsonParser(const std::string& json) : json_(json), pos_(0) {}

    Value parse(std::string* error_out) {
        try {
            skip_whitespace();
            if (pos_ >= json_.size()) {
                if (error_out) *error_out = "Empty JSON input";
                return Value{};
            }
            return parse_value();
        } catch (const std::exception& e) {
            if (error_out) *error_out = e.what();
            return Value{};
        }
    }

private:
    const std::string& json_;
    std::size_t pos_;

    char peek() const {
        return pos_ < json_.size() ? json_[pos_] : '\0';
    }

    char consume() {
        return pos_ < json_.size() ? json_[pos_++] : '\0';
    }

    void skip_whitespace() {
        while (pos_ < json_.size() && std::isspace(static_cast<unsigned char>(json_[pos_]))) {
            ++pos_;
        }
    }

    void expect(char c) {
        skip_whitespace();
        if (consume() != c) {
            throw std::runtime_error(std::string("Expected '") + c + "' at position " + std::to_string(pos_));
        }
    }

    Value parse_value() {
        skip_whitespace();
        char c = peek();

        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();

        throw std::runtime_error("Unexpected character '" + std::string(1, c) + "' at position " + std::to_string(pos_));
    }

    Value parse_object() {
        expect('{');
        skip_whitespace();

        if (peek() == '}') {
            consume();
            return Value{ValueMap{}};
        }

        auto transient = ValueMap{}.transient();

        while (true) {
            skip_whitespace();
            std::string key = parse_string_raw();
            expect(':');
            Value val = parse_value();
            transient.set(std::move(key), ValueBox{std::move(val)});

            skip_whitespace();
            char c = peek();
            if (c == '}') {
                consume();
                break;
            }
            if (c != ',') {
                throw std::runtime_error("Expected ',' or '}' in object at position " + std::to_string(pos_));
            }
            consume();
        }

        return Value{transient.persistent()};
    }

    Value parse_array() {
        expect('[');
        skip_whitespace();

        if (peek() == ']') {
            consume();
            return Value{ValueVector{}};
        }

        auto transient = ValueVector{}.transient();

        while (true) {
            Value val = parse_value();
            transient.push_back(ValueBox{std::move(val)});

            skip_whitespace();
            char c = peek();
            if (c == ']') {
                consume();
                break;
            }
            if (c != ',') {
                throw std::runtime_error("Expected ',' or ']' in array at position " + std::to_string(pos_));
            }
            consume();
        }

        return Value{transient.persistent()};
    }

    std::string parse_string_raw() {
        expect('"');
        std::string result;

        while (pos_ < json_.size()) {
            char c = consume();
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (pos_ >= json_.size()) {
                    throw std::runtime_error("Unexpected end of string escape");
                }
                char escaped = consume();
                switch (escaped) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // Parse \uXXXX
                        if (pos_ + 4 > json_.size()) {
                            throw std::runtime_error("Invalid unicode escape");
                        }
                        std::string hex = json_.substr(pos_, 4);
                        pos_ += 4;
                        int codepoint = std::stoi(hex, nullptr, 16);
                        if (codepoint < 0x80) {
                            result += static_cast<char>(codepoint);
                        } else if (codepoint < 0x800) {
                            result += static_cast<char>(0xC0 | (codepoint >> 6));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (codepoint >> 12));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        throw std::runtime_error("Invalid escape sequence: \\" + std::string(1, escaped));
                }
            } else {
                result += c;
            }
        }

        throw std::runtime_error("Unterminated string");
    }

    Value parse_string() {
        return Value{parse_string_raw()};
    }

    Value parse_number() {
        std::size_t start = pos_;
        bool has_decimal = false;
        bool has_exponent = false;

        if (peek() == '-') consume();

        while (pos_ < json_.size()) {
            char c = peek();
            if (std::isdigit(static_cast<unsigned char>(c))) {
                consume();
            } else if (c == '.' && !has_decimal && !has_exponent) {
                has_decimal = true;
                consume();
            } else if ((c == 'e' || c == 'E') && !has_exponent) {
                has_exponent = true;
                consume();
                if (peek() == '+' || peek() == '-') consume();
            } else {
                break;
            }
        }

        std::string num_str = json_.substr(start, pos_ - start);

        if (has_decimal || has_exponent) {
            return Value{std::stod(num_str)};
        } else {
            try {
                int64_t val = std::stoll(num_str);
                // Use int if it fits, otherwise int64_t
                if (val >= INT_MIN && val <= INT_MAX) {
                    return Value{static_cast<int>(val)};
                }
                return Value{val};
            } catch (...) {
                return Value{std::stod(num_str)};
            }
        }
    }

    Value parse_bool() {
        if (json_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value{true};
        }
        if (json_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value{false};
        }
        throw std::runtime_error("Expected 'true' or 'false' at position " + std::to_string(pos_));
    }

    Value parse_null() {
        if (json_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value{};
        }
        throw std::runtime_error("Expected 'null' at position " + std::to_string(pos_));
    }
};

} // anonymous namespace

std::string to_json(const Value& val, bool compact) {
    std::ostringstream oss;
    to_json_impl(val, oss, compact, 0);
    return oss.str();
}

Value from_json(const std::string& json_str, std::string* error_out) {
    JsonParser parser(json_str);
    return parser.parse(error_out);
}

// Note: path_to_string_path() and parse_string_path() are implemented
// in string_path.cpp to avoid duplicate definitions.

// ============================================================
// Explicit Template Instantiations
//
// These instantiations generate the actual code for the templated classes
// that are declared with 'extern template' in value.h.
// This centralizes instantiation in a single translation unit, reducing:
//   - Compile time (no redundant instantiations across TUs)
//   - Object file size (no duplicate symbols)
//   - Link time (fewer symbols to deduplicate)
// ============================================================

// Explicit instantiation for unsafe_memory_policy (Value, UnsafeValue)
template struct BasicValue<unsafe_memory_policy>;
template struct BasicTableEntry<unsafe_memory_policy>;
template class BasicMapBuilder<unsafe_memory_policy>;
template class BasicVectorBuilder<unsafe_memory_policy>;
template class BasicArrayBuilder<unsafe_memory_policy>;
template class BasicTableBuilder<unsafe_memory_policy>;

// Explicit instantiation for thread_safe_memory_policy (SyncValue, ThreadSafeValue)
template struct BasicValue<thread_safe_memory_policy>;
template struct BasicTableEntry<thread_safe_memory_policy>;
template class BasicMapBuilder<thread_safe_memory_policy>;
template class BasicVectorBuilder<thread_safe_memory_policy>;
template class BasicArrayBuilder<thread_safe_memory_policy>;
template class BasicTableBuilder<thread_safe_memory_policy>;

} // namespace lager_ext
