// string_path.h - String-based path parsing (RFC 6901 JSON Pointer style)

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/value.h>
#include <lager_ext/lager_lens.h>
#include <string_view>

namespace lager_ext {

[[nodiscard]] LAGER_EXT_API Path parse_string_path(std::string_view path_str);
[[nodiscard]] LAGER_EXT_API std::string path_to_string_path(const Path& path);
[[nodiscard]] LAGER_EXT_API LagerValueLens string_path_lens(std::string_view path_str);
[[nodiscard]] LAGER_EXT_API Value get_by_path(const Value& data, std::string_view path_str);
[[nodiscard]] LAGER_EXT_API Value set_by_path(const Value& data, std::string_view path_str, Value new_value);

template<typename Fn>
[[nodiscard]] Value over_by_path(const Value& data, std::string_view path_str, Fn&& fn)
{
    auto lens = string_path_lens(path_str);
    return lager::over(lens, data, std::forward<Fn>(fn));
}

} // namespace lager_ext
