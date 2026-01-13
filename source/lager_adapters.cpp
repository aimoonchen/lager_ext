// lager_adapters.cpp
// Implementation of Lager library integration adapters

#include <lager_ext/lager_adapters.h>
#include <lager_ext/value_diff.h>

#include <iostream>

namespace lager_ext {

auto value_diff_middleware(bool recursive) {
    return value_middleware({.enable_diff_logging = true,
                             .enable_deep_diff = recursive,
                             .on_change = [recursive](const Value& old_state, const Value& new_state) {
                                 // Check if there are actual changes using fast path comparison
                                 if (&old_state.data == &new_state.data) {
                                     return; // Same object, no changes
                                 }

                                 if (!has_any_difference(old_state, new_state, recursive)) {
                                     return; // No structural changes
                                 }

                                 // Compute and log the diff
                                 DiffEntryCollector collector;
                                 collector.diff(old_state, new_state, recursive);

                                 if (collector.has_changes()) {
                                     std::cout << "[value_diff_middleware] State changes detected:\n";
                                     collector.print_diffs();
                                 }
                             }});
}

} // namespace lager_ext
