// string_path.h - String-based path parsing (RFC 6901 JSON Pointer style)

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/value.h>
#include <string_view>

namespace lager_ext {

/// @brief Parse a JSON Pointer string into a Path
/// @param path_str JSON Pointer format string (e.g., "/users/0/name")
/// @return Parsed Path object
/// @example Path p = parse_string_path("/users/0/name");
[[nodiscard]] LAGER_EXT_API Path parse_string_path(std::string_view path_str);

/// @brief Convert a Path back to JSON Pointer string
/// @param path The Path to convert
/// @return JSON Pointer format string
/// @example std::string s = path_to_string_path(path); // "/users/0/name"
[[nodiscard]] LAGER_EXT_API std::string path_to_string_path(const Path& path);

} // namespace lager_ext