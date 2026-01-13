// lager_adapters.h - Adapters for seamless integration with lager library
//
// This header provides adapters that bridge lager_ext's Value-based API
// with lager's strongly-typed cursor/reader/store ecosystem.
//
// Features:
// 1. zoom_value() - Zoom adapters for lager::reader<Value> and lager::cursor<Value>
// 2. value_middleware - Store middleware for Value-based state management
// 3. watch_path() - Watch specific paths for changes
//
// Example usage:
//   // Zoom a lager cursor to a Value path
//   auto user_cursor = zoom_value(cursor, "users" / 0 / "name");
//
//   // Use middleware with lager store
//   auto store = lager::make_store<Action>(init_state, loop, value_middleware());
//
//   // Watch a specific path for changes
//   auto conn = watch_path(reader, {"users", 0, "status"}, [](const Value& v) {
//       std::cout << "Status changed: " << v << "\n";
//   });

#pragma once

#include <lager_ext/api.h>
#include <lager_ext/lager_lens.h>
#include <lager_ext/path.h>
#include <lager_ext/value.h>

#include <lager/cursor.hpp>
#include <lager/lens.hpp>
#include <lager/lenses.hpp>
#include <lager/reader.hpp>
#include <lager/state.hpp>
#include <lager/watch.hpp>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace lager_ext {

// ============================================================
// Part 1: zoom_value() - Zoom adapters for lager::reader/cursor
//
// These functions allow zooming into a lager::reader<Value> or
// lager::cursor<Value> using a PathLens or Path, returning a
// new reader/cursor focused on that sub-path.
//
// Unlike regular zoom() which requires a lens returning the same
// type, zoom_value() always works with Value->Value lenses.
// ============================================================

/// @brief Zoom a lager::reader<Value> to a sub-path
/// @param reader The source reader containing a Value
/// @param lens PathLens specifying the path to zoom to
/// @return A new reader<Value> focused on the specified path
///
/// Example:
///   lager::reader<Value> state_reader = /* ... */;
///   auto user_reader = zoom_value(state_reader, root / "users" / 0);
template <typename ReaderT>
    requires std::is_same_v<typename ReaderT::value_type, Value>
[[nodiscard]] auto zoom_value(ReaderT reader, const PathLens& lens) {
    return reader.zoom(lens.to_lens());
}

/// @brief Zoom a lager::reader<Value> using a Path
template <typename ReaderT>
    requires std::is_same_v<typename ReaderT::value_type, Value>
[[nodiscard]] auto zoom_value(ReaderT reader, const Path& path) {
    return reader.zoom(lager_path_lens(path));
}

/// @brief Zoom using variadic path elements (e.g., zoom_value(r, "users", 0, "name"))
template <typename ReaderT, PathElementType... Elements>
    requires std::is_same_v<typename ReaderT::value_type, Value>
[[nodiscard]] auto zoom_value(ReaderT reader, Elements&&... elements) {
    return zoom_value(std::move(reader), make_path(std::forward<Elements>(elements)...));
}

// ============================================================
// Part 2: value_middleware - Store middleware
//
// Middleware that integrates with lager::make_store to provide:
// - Value change diffing
// - Automatic path-based subscriptions
// - Debug logging of Value state changes
// ============================================================

/// @brief Configuration for value_middleware
struct ValueMiddlewareConfig {
    bool enable_diff_logging = false; ///< Log all state diffs to console
    bool enable_deep_diff = true;     ///< Use recursive diff (vs shallow)
    std::function<void(const Value& old_state, const Value& new_state)> on_change;
};

namespace detail {

/// Internal: Creates the actual middleware enhancer
template <typename Config>
auto make_value_middleware_impl(Config config) {
    return [config = std::move(config)](auto next) {
        return [config, next](auto action, auto&& model, auto&& reducer, auto&& loop, auto&& deps, auto&& tags) {
            // Wrap the reducer to intercept state changes
            auto wrapped_reducer = [original_reducer = std::forward<decltype(reducer)>(reducer), config](auto&& state,
                                                                                                         auto&& act) {
                auto old_state = state;
                auto result = original_reducer(std::forward<decltype(state)>(state), std::forward<decltype(act)>(act));

                // If the state is a Value and we have a callback, notify
                if constexpr (std::is_same_v<std::decay_t<decltype(state)>, Value>) {
                    if (config.on_change) {
                        // Extract new state from result (handles both Model and pair<Model,
                        // Effect>)
                        if constexpr (requires { result.first; }) {
                            config.on_change(old_state, result.first);
                        } else {
                            config.on_change(old_state, result);
                        }
                    }
                }

                return result;
            };

            return next(action, std::forward<decltype(model)>(model), std::move(wrapped_reducer),
                        std::forward<decltype(loop)>(loop), std::forward<decltype(deps)>(deps),
                        std::forward<decltype(tags)>(tags));
        };
    };
}

} // namespace detail

/// @brief Create a middleware for Value-based stores
/// @param config Configuration options
/// @return A store enhancer that can be passed to lager::make_store
///
/// Example:
///   auto store = lager::make_store<MyAction>(
///       init_state,
///       loop,
///       value_middleware({
///           .on_change = [](const Value& old_s, const Value& new_s) {
///               std::cout << "State changed!\n";
///           }
///       })
///   );
[[nodiscard]] inline auto value_middleware(ValueMiddlewareConfig config = {}) {
    return detail::make_value_middleware_impl(std::move(config));
}

/// @brief Create a diff-logging middleware (convenience)
/// @param recursive Whether to use recursive diff
/// @return A store enhancer that logs all state changes
[[nodiscard]] LAGER_EXT_API auto value_diff_middleware(bool recursive = true);

// ============================================================
// Part 3: Watch Adapter for Path-based Subscriptions
// ============================================================

/// @brief Watch a specific path in a lager reader/cursor for changes
///
/// Unlike lager::watch which watches the entire value, this watches
/// only the value at a specific path and only triggers when that
/// value changes.
///
/// @param watchable A lager::reader<Value> or lager::cursor<Value>
/// @param path The path to watch
/// @param callback Called with the new value when the path's value changes
/// @return A watch connection that can be used to unwatch
template <typename Watchable, typename Callback>
    requires std::is_same_v<typename Watchable::value_type, Value>
auto watch_path(Watchable& watchable, const Path& path, Callback&& callback) {
    // Create a zoomed reader/cursor that only tracks the specific path
    auto zoomed = zoom_value(watchable, path);

    // Watch the zoomed value
    return lager::watch(zoomed, std::forward<Callback>(callback));
}

/// @brief Watch a path using PathLens
template <typename Watchable, typename Callback>
    requires std::is_same_v<typename Watchable::value_type, Value>
auto watch_path(Watchable& watchable, const PathLens& lens, Callback&& callback) {
    return watch_path(watchable, lens.path(), std::forward<Callback>(callback));
}

/// @brief Watch a path using variadic elements
template <typename Watchable, typename Callback, PathElementType... Elements>
    requires std::is_same_v<typename Watchable::value_type, Value>
auto watch_path(Watchable& watchable, Callback&& callback, Elements&&... elements) {
    return watch_path(watchable, make_path(std::forward<Elements>(elements)...).path(),
                      std::forward<Callback>(callback));
}

} // namespace lager_ext
