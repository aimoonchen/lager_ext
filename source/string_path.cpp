// string_path.cpp - String-based path parsing API

#include <lager_ext/string_path.h>
#include <cctype>
#include <iostream>
#include <ranges>

namespace lager_ext {

namespace {

// Unescape: ~1 -> /, ~0 -> ~
std::string unescape_segment(std::string_view segment)
{
    std::string result;
    result.reserve(segment.size());

    for (size_t i = 0; i < segment.size(); ++i) {
        if (segment[i] == '~' && i + 1 < segment.size()) {
            if (segment[i + 1] == '1') {
                result += '/';
                ++i;
                continue;
            } else if (segment[i + 1] == '0') {
                result += '~';
                ++i;
                continue;
            }
        }
        result += segment[i];
    }

    return result;
}

bool is_array_index(const std::string& s)
{
    if (s.empty() || s == "-") {  // "-" is special case for "end of array"
        return false;
    }
    return std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c); });
}

} // anonymous namespace

Path parse_string_path(std::string_view path_str)
{
    // Empty path refers to root
    if (path_str.empty()) {
        return Path{};
    }

    // Path must start with '/'
    if (path_str[0] != '/') {
        std::cerr << "[parse_string_path] Invalid path, must start with '/': "
                  << path_str << "\n";
        return Path{};
    }

    Path result;

    // Skip leading '/'
    path_str = path_str.substr(1);

    // Split by '/'
    while (!path_str.empty()) {
        auto pos = path_str.find('/');
        std::string_view segment = (pos == std::string_view::npos)
                                    ? path_str
                                    : path_str.substr(0, pos);

        std::string unescaped = unescape_segment(segment);

        if (is_array_index(unescaped)) {
            result.push_back(static_cast<std::size_t>(std::stoull(unescaped)));
        } else {
            result.push_back(std::move(unescaped));
        }

        if (pos == std::string_view::npos) {
            break;
        }
        path_str = path_str.substr(pos + 1);
    }

    return result;
}

std::string path_to_string_path(const Path& path)
{
    if (path.empty()) {
        return "";  // Root reference
    }

    std::string result;
    for (const auto& elem : path) {
        result += '/';
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string_view>) {
                // Escape: ~ -> ~0, / -> ~1
                for (char c : v) {
                    if (c == '~') {
                        result += "~0";
                    } else if (c == '/') {
                        result += "~1";
                    } else {
                        result += c;
                    }
                }
            } else {
                // size_t index
                result += std::to_string(v);
            }
        }, elem);
    }
    return result;
}

} // namespace lager_ext