// editor_engine.cpp
// Implementation of Editor-Engine Cross-Process State Management

#include <lager_ext/builders.h>
#include <lager_ext/editor_engine.h>
#include <lager_ext/path_utils.h>

#include <iostream>
#include <sstream>

namespace lager_ext {

// ============================================================
// Helper: Parse property path (e.g., "position.x" -> ["position", "x"])
// ============================================================
static Path parse_property_path(const std::string& path_str) {
    Path result;
    std::istringstream ss(path_str);
    std::string segment;
    while (std::getline(ss, segment, '.')) {
        if (!segment.empty()) {
            result.push_back(segment);
        }
    }
    return result;
}

// Use shared path_core.h functions instead of duplicating code
// get_value_at_path -> get_at_path
// set_value_at_path -> set_at_path

// ============================================================
// Editor Reducer Implementation
// ============================================================

// Helper: Save current state to undo stack (only for user actions)
static void push_undo_state(EditorModel& model) {
    model.undo_stack = model.undo_stack.push_back(model.scene);
    if (model.undo_stack.size() > EditorModel::max_history) {
        model.undo_stack = model.undo_stack.drop(1); // O(1) drop from front
    }
    model.redo_stack = immer::flex_vector<SceneState>{}; // Clear redo stack
}

EditorModel editor_update(EditorModel model, EditorAction action) {
    // Check if this action should be recorded to undo history
    const bool record_undo = should_record_undo(action);

    return std::visit(
        [&model, record_undo](auto&& act) -> EditorModel {
            using T = std::decay_t<decltype(act)>;

            // ============================================================
            // Control Actions (Undo/Redo/ClearHistory)
            // ============================================================

            if constexpr (std::is_same_v<T, actions::Undo>) {
                if (model.undo_stack.empty()) {
                    return model;
                }

                // Save current state for redo - flex_vector has O(1) push_back
                model.redo_stack = model.redo_stack.push_back(model.scene);

                // Restore previous state - flex_vector has O(1) back() and take()
                model.scene = model.undo_stack.back();
                model.undo_stack = model.undo_stack.take(model.undo_stack.size() - 1);
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::Redo>) {
                if (model.redo_stack.empty()) {
                    return model;
                }

                // Save current state for undo
                model.undo_stack = model.undo_stack.push_back(model.scene);

                // Restore next state
                model.scene = model.redo_stack.back();
                model.redo_stack = model.redo_stack.take(model.redo_stack.size() - 1);
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::ClearHistory>) {
                // Clear undo/redo history (e.g., after loading a new scene)
                model.undo_stack = immer::flex_vector<SceneState>{};
                model.redo_stack = immer::flex_vector<SceneState>{};
                return model;
            }

            // ============================================================
            // System Actions (NOT recorded to undo history)
            // ============================================================

            else if constexpr (std::is_same_v<T, actions::SelectObject>) {
                // Select object for editing - SystemAction, no undo
                const auto& payload = act.payload;
                if (model.scene.objects.find(payload.object_id) != nullptr) {
                    model.scene.selected_id = payload.object_id;
                }
                return model;
            } else if constexpr (std::is_same_v<T, actions::SyncFromEngine>) {
                // Full sync from engine (replaces current state) - clears history
                const auto& payload = act.payload;
                model.scene = payload.new_state;
                model.undo_stack = immer::flex_vector<SceneState>{};
                model.redo_stack = immer::flex_vector<SceneState>{};
                model.dirty = false;
                return model;
            } else if constexpr (std::is_same_v<T, actions::LoadObjects>) {
                // Batch load objects - SystemAction, no undo (used for incremental loading)
                const auto& payload = act.payload;

                // Use transient builder for O(n) batch insertion
                auto builder = model.scene.objects.transient();
                for (const auto& obj : payload.objects) {
                    builder.set(obj.id, obj);
                }
                model.scene.objects = std::move(builder).persistent();
                model.scene.version++;
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::SetLoadingState>) {
                // Set loading state - SystemAction, no undo (UI state only)
                // Note: In a full implementation, you might have a separate loading_state field
                // For now, this is a placeholder that doesn't modify scene state
                return model;
            }

            // ============================================================
            // User Actions (recorded to undo history)
            // ============================================================

            else if constexpr (std::is_same_v<T, actions::SetProperty>) {
                // Modify property of selected object - UserAction
                const auto& payload = act.payload;

                if (model.scene.selected_id.empty()) {
                    return model;
                }

                const SceneObject* obj_ptr = model.scene.objects.find(model.scene.selected_id);
                if (obj_ptr == nullptr) {
                    return model;
                }

                // Record undo state (this is a UserAction)
                if (record_undo) {
                    push_undo_state(model);
                }

                // Update property using immer::map::set() for immutable update
                Path path = parse_property_path(payload.property_path);
                SceneObject updated_obj = *obj_ptr;
                updated_obj.data = set_at_path(updated_obj.data, path, payload.new_value);
                model.scene.objects = model.scene.objects.set(model.scene.selected_id, updated_obj);
                model.scene.version++;
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::SetProperties>) {
                // Batch update multiple properties - UserAction
                const auto& payload = act.payload;

                if (model.scene.selected_id.empty()) {
                    return model;
                }

                const SceneObject* obj_ptr = model.scene.objects.find(model.scene.selected_id);
                if (obj_ptr == nullptr) {
                    return model;
                }

                // Record undo state (this is a UserAction)
                if (record_undo) {
                    push_undo_state(model);
                }

                // Update all properties
                SceneObject updated_obj = *obj_ptr;
                for (const auto& [path_str, value] : payload.updates) {
                    Path path = parse_property_path(path_str);
                    updated_obj.data = set_at_path(updated_obj.data, path, value);
                }
                model.scene.objects = model.scene.objects.set(model.scene.selected_id, updated_obj);
                model.scene.version++;
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::AddObject>) {
                // Add a new object - UserAction
                const auto& payload = act.payload;

                // Record undo state (this is a UserAction)
                if (record_undo) {
                    push_undo_state(model);
                }

                // Add the object using immer::map::set()
                model.scene.objects = model.scene.objects.set(payload.object.id, payload.object);

                // Add to parent's children list
                if (!payload.parent_id.empty()) {
                    const SceneObject* parent_ptr = model.scene.objects.find(payload.parent_id);
                    if (parent_ptr != nullptr) {
                        SceneObject updated_parent = *parent_ptr;
                        updated_parent.children.push_back(payload.object.id);
                        model.scene.objects = model.scene.objects.set(payload.parent_id, updated_parent);
                    }
                }

                model.scene.version++;
                model.dirty = true;

                return model;
            } else if constexpr (std::is_same_v<T, actions::RemoveObject>) {
                // Remove an object - UserAction
                const auto& payload = act.payload;

                const SceneObject* obj_ptr = model.scene.objects.find(payload.object_id);
                if (obj_ptr == nullptr) {
                    return model;
                }

                // Record undo state (this is a UserAction)
                if (record_undo) {
                    push_undo_state(model);
                }

                // Remove from parent's children list (immutably)
                for (const auto& [id, obj] : model.scene.objects) {
                    auto child_it = std::find(obj.children.begin(), obj.children.end(), payload.object_id);
                    if (child_it != obj.children.end()) {
                        SceneObject updated_parent = obj;
                        updated_parent.children.erase(std::find(updated_parent.children.begin(),
                                                                updated_parent.children.end(), payload.object_id));
                        model.scene.objects = model.scene.objects.set(id, updated_parent);
                        break;
                    }
                }

                // Remove the object using immer::map::erase()
                model.scene.objects = model.scene.objects.erase(payload.object_id);

                // Clear selection if removed object was selected
                if (model.scene.selected_id == payload.object_id) {
                    model.scene.selected_id.clear();
                }

                model.scene.version++;
                model.dirty = true;

                return model;
            }

            return model;
        },
        action);
}

// ============================================================
// EngineSimulator Implementation
// ============================================================

struct EngineSimulator::Impl {
    SceneState scene;
    std::vector<EngineCallback> callbacks;

    void fire_event(const std::string& event, const Value& data) {
        for (auto& cb : callbacks) {
            if (cb) {
                cb(event, data);
            }
        }
    }
};

EngineSimulator::EngineSimulator() : impl_(std::make_unique<Impl>()) {}
EngineSimulator::~EngineSimulator() = default;

void EngineSimulator::initialize_sample_scene() {
    // Create a sample scene with a few objects

    // ===== Transform component metadata =====
    UIMeta transform_meta;
    transform_meta.type_name = "Transform";
    transform_meta.icon_name = "transform_icon";
    transform_meta.properties = {
        {"position.x", "Position X", "X coordinate", "Transform", WidgetType::DoubleSpinBox,
         NumericRange{-1000.0, 1000.0, 0.1}, std::nullopt, false, true, 0},
        {"position.y", "Position Y", "Y coordinate", "Transform", WidgetType::DoubleSpinBox,
         NumericRange{-1000.0, 1000.0, 0.1}, std::nullopt, false, true, 1},
        {"position.z", "Position Z", "Z coordinate", "Transform", WidgetType::DoubleSpinBox,
         NumericRange{-1000.0, 1000.0, 0.1}, std::nullopt, false, true, 2},
        {"rotation.x", "Rotation X", "X rotation in degrees", "Transform", WidgetType::Slider,
         NumericRange{-180.0, 180.0, 1.0}, std::nullopt, false, true, 3},
        {"rotation.y", "Rotation Y", "Y rotation in degrees", "Transform", WidgetType::Slider,
         NumericRange{-180.0, 180.0, 1.0}, std::nullopt, false, true, 4},
        {"rotation.z", "Rotation Z", "Z rotation in degrees", "Transform", WidgetType::Slider,
         NumericRange{-180.0, 180.0, 1.0}, std::nullopt, false, true, 5},
        {"scale.x", "Scale X", "X scale factor", "Transform", WidgetType::DoubleSpinBox, NumericRange{0.01, 100.0, 0.1},
         std::nullopt, false, true, 6},
        {"scale.y", "Scale Y", "Y scale factor", "Transform", WidgetType::DoubleSpinBox, NumericRange{0.01, 100.0, 0.1},
         std::nullopt, false, true, 7},
        {"scale.z", "Scale Z", "Z scale factor", "Transform", WidgetType::DoubleSpinBox, NumericRange{0.01, 100.0, 0.1},
         std::nullopt, false, true, 8},
    };

    // ===== Light component metadata =====
    UIMeta light_meta;
    light_meta.type_name = "Light";
    light_meta.icon_name = "light_icon";
    light_meta.properties = {
        {"name", "Name", "Object name", "General", WidgetType::LineEdit, std::nullopt, std::nullopt, false, true, 0},
        {"type", "Light Type", "Type of light source", "Light", WidgetType::ComboBox, std::nullopt,
         ComboOptions{{"Point", "Directional", "Spot"}, 0}, false, true, 1},
        {"color", "Color", "Light color", "Light", WidgetType::ColorPicker, std::nullopt, std::nullopt, false, true, 2},
        {"intensity", "Intensity", "Light intensity", "Light", WidgetType::Slider, NumericRange{0.0, 10.0, 0.1},
         std::nullopt, false, true, 3},
        {"enabled", "Enabled", "Is light enabled", "Light", WidgetType::CheckBox, std::nullopt, std::nullopt, false,
         true, 4},
    };

    // ===== Mesh component metadata =====
    UIMeta mesh_meta;
    mesh_meta.type_name = "MeshRenderer";
    mesh_meta.icon_name = "mesh_icon";
    mesh_meta.properties = {
        {"name", "Name", "Object name", "General", WidgetType::LineEdit, std::nullopt, std::nullopt, false, true, 0},
        {"mesh_path", "Mesh", "Path to mesh file", "Mesh", WidgetType::FileSelector, std::nullopt, std::nullopt, false,
         true, 1},
        {"material", "Material", "Material name", "Mesh", WidgetType::LineEdit, std::nullopt, std::nullopt, false, true,
         2},
        {"visible", "Visible", "Is mesh visible", "Mesh", WidgetType::CheckBox, std::nullopt, std::nullopt, false, true,
         3},
        {"cast_shadows", "Cast Shadows", "Does mesh cast shadows", "Mesh", WidgetType::CheckBox, std::nullopt,
         std::nullopt, false, true, 4},
    };

    // ===== Create Root object =====
    SceneObject root;
    root.id = "root";
    root.type = "Transform";
    root.meta = transform_meta;
    root.children = {"camera_main", "light_sun", "cube_1"};

    // Create root data using Builder API for O(n) construction
    Value position = MapBuilder().set("x", Value{0.0}).set("y", Value{0.0}).set("z", Value{0.0}).finish();

    Value rotation = MapBuilder().set("x", Value{0.0}).set("y", Value{0.0}).set("z", Value{0.0}).finish();

    Value scale = MapBuilder().set("x", Value{1.0}).set("y", Value{1.0}).set("z", Value{1.0}).finish();

    root.data = MapBuilder().set("position", position).set("rotation", rotation).set("scale", scale).finish();

    // ===== Create Light object using Builder API =====
    SceneObject light;
    light.id = "light_sun";
    light.type = "Light";
    light.meta = light_meta;

    light.data = MapBuilder()
                     .set("name", Value{std::string{"Sun Light"}})
                     .set("type", Value{std::string{"Directional"}})
                     .set("color", Value{std::string{"#FFFFCC"}})
                     .set("intensity", Value{1.5})
                     .set("enabled", Value{true})
                     .finish();

    // ===== Create Mesh object using Builder API =====
    SceneObject cube;
    cube.id = "cube_1";
    cube.type = "MeshRenderer";
    cube.meta = mesh_meta;

    cube.data = MapBuilder()
                    .set("name", Value{std::string{"Main Cube"}})
                    .set("mesh_path", Value{std::string{"/meshes/cube.fbx"}})
                    .set("material", Value{std::string{"default_material"}})
                    .set("visible", Value{true})
                    .set("cast_shadows", Value{true})
                    .finish();

    // ===== Create Camera object using Builder API =====
    SceneObject camera;
    camera.id = "camera_main";
    camera.type = "Transform";
    camera.meta = transform_meta;

    Value cam_position = MapBuilder().set("x", Value{0.0}).set("y", Value{5.0}).set("z", Value{-10.0}).finish();

    Value cam_rotation = MapBuilder().set("x", Value{15.0}).set("y", Value{0.0}).set("z", Value{0.0}).finish();

    Value cam_scale = MapBuilder().set("x", Value{1.0}).set("y", Value{1.0}).set("z", Value{1.0}).finish();

    camera.data =
        MapBuilder().set("position", cam_position).set("rotation", cam_rotation).set("scale", cam_scale).finish();

    // ===== Build scene using Builder API for O(n) construction =====
    using SceneMapBuilder = typename immer::map<std::string, SceneObject>::transient_type;
    SceneMapBuilder scene_builder = impl_->scene.objects.transient();
    scene_builder.set("root", root);
    scene_builder.set("light_sun", light);
    scene_builder.set("cube_1", cube);
    scene_builder.set("camera_main", camera);
    impl_->scene.objects = scene_builder.persistent();
    impl_->scene.root_id = "root";
    impl_->scene.version = 1;
}

SceneState EngineSimulator::get_initial_state() const {
    return impl_->scene;
}

void EngineSimulator::apply_diff(const DiffResult& diff) {
    std::cout << "[Engine] Applying diff with " << diff.added.size() << " additions, " << diff.removed.size()
              << " removals, " << diff.modified.size() << " modifications\n";

    // In a real implementation, we would:
    // 1. Parse the paths to find which objects are affected
    // 2. Update the corresponding runtime objects
    // 3. Trigger necessary updates (e.g., re-render)

    for (const auto& mod : diff.modified) {
        std::cout << "  Modified: " << mod.path.to_dot_notation() << " = " << value_to_string(mod.new_value) << "\n";
    }

    impl_->fire_event("diff_applied", Value{});
}

void EngineSimulator::apply_full_state(const Value& state) {
    std::cout << "[Engine] Applying full state update\n";
    impl_->fire_event("state_updated", state);
}

Value EngineSimulator::get_state_as_value() const {
    // Container Boxing: Convert scene to Value with BoxedValueMap
    ValueMap objects_map;
    for (const auto& [id, obj] : impl_->scene.objects) {
        objects_map = objects_map.set(id, obj.data);
    }

    ValueMap scene_value;
    scene_value = scene_value.set("objects", Value{BoxedValueMap{objects_map}});
    scene_value = scene_value.set("root_id", Value{impl_->scene.root_id});
    scene_value = scene_value.set("version", Value{static_cast<int>(impl_->scene.version)});

    return Value{BoxedValueMap{scene_value}};
}

void EngineSimulator::on_event(EngineCallback callback) {
    impl_->callbacks.push_back(std::move(callback));
}

void EngineSimulator::print_state() const {
    std::cout << "\n=== Engine Scene State ===\n";
    std::cout << "Root: " << impl_->scene.root_id << "\n";
    std::cout << "Version: " << impl_->scene.version << "\n";
    std::cout << "Objects:\n";

    for (const auto& [id, obj] : impl_->scene.objects) {
        std::cout << "  [" << id << "] Type: " << obj.type << "\n";
        std::cout << "    Data:\n";
        print_value(obj.data, "      ", 3);
    }
}

// ============================================================
// EditorController Implementation
// ============================================================

struct EditorController::Impl {
    EditorModel model;
    EditorEffects effects;
    std::vector<WatchCallback> watchers;
    Value previous_state_value; // For diff calculation

    void notify_watchers() {
        for (auto& watcher : watchers) {
            if (watcher) {
                watcher(model);
            }
        }
    }

    void check_and_notify_changes() {
        if (!model.dirty)
            return;

        // Get current state as Value for diff
        Value current_state_value = scene_to_value(model.scene);

        // Calculate diff
        if (effects.on_state_changed) {
            DiffResult diff = collect_diff(previous_state_value, current_state_value);
            if (!diff.empty()) {
                effects.on_state_changed(diff);
            }
        }

        previous_state_value = current_state_value;
        model.dirty = false;
    }

    static Value scene_to_value(const SceneState& scene) {
        // Container Boxing: wrap maps in BoxedValueMap
        ValueMap objects_map;
        for (const auto& [id, obj] : scene.objects) {
            objects_map = objects_map.set(id, obj.data);
        }

        ValueMap scene_map;
        scene_map = scene_map.set("objects", Value{BoxedValueMap{objects_map}});
        scene_map = scene_map.set("selected_id", Value{scene.selected_id});
        scene_map = scene_map.set("version", Value{static_cast<int>(scene.version)});

        return Value{BoxedValueMap{scene_map}};
    }
};

EditorController::EditorController() : impl_(std::make_unique<Impl>()) {}
EditorController::~EditorController() = default;

void EditorController::initialize(const SceneState& initial_state) {
    impl_->model.scene = initial_state;
    impl_->model.undo_stack = immer::flex_vector<SceneState>{}; // Clear
    impl_->model.redo_stack = immer::flex_vector<SceneState>{}; // Clear
    impl_->model.dirty = false;
    impl_->previous_state_value = Impl::scene_to_value(initial_state);
}

void EditorController::dispatch(EditorAction action) {
    // Track selection changes
    std::string old_selection = impl_->model.scene.selected_id;

    // Apply action through reducer
    impl_->model = editor_update(impl_->model, action);

    // Notify selection change
    if (impl_->model.scene.selected_id != old_selection) {
        if (impl_->effects.on_selection_changed) {
            impl_->effects.on_selection_changed(impl_->model.scene.selected_id);
        }
    }

    // Check and notify state changes
    impl_->check_and_notify_changes();

    // Notify watchers
    impl_->notify_watchers();
}

const EditorModel& EditorController::get_model() const {
    return impl_->model;
}

const SceneObject* EditorController::get_selected_object() const {
    if (impl_->model.scene.selected_id.empty()) {
        return nullptr;
    }

    // immer::map::find() returns const T* directly, not an iterator
    return impl_->model.scene.objects.find(impl_->model.scene.selected_id);
}

Value EditorController::get_property(const std::string& path) const {
    const SceneObject* obj = get_selected_object();
    if (!obj)
        return Value{}; // null Value indicates no object selected

    Path parsed_path = parse_property_path(path);
    return get_at_path(obj->data, parsed_path);
}

void EditorController::set_property(const std::string& path, Value value) {
    // SetProperty is a UserAction - will be recorded to undo history
    dispatch(actions::SetProperty{payloads::SetProperty{path, std::move(value)}});
}

bool EditorController::can_undo() const {
    return !impl_->model.undo_stack.empty();
}

bool EditorController::can_redo() const {
    return !impl_->model.redo_stack.empty();
}

void EditorController::undo() {
    dispatch(actions::Undo{});
}

void EditorController::redo() {
    dispatch(actions::Redo{});
}

void EditorController::set_effects(EditorEffects effects) {
    impl_->effects = std::move(effects);
}

void EditorController::step() {
    // For manual event loop - process any pending operations
    impl_->check_and_notify_changes();
}

std::function<void()> EditorController::watch(WatchCallback callback) {
    impl_->watchers.push_back(callback);
    size_t index = impl_->watchers.size() - 1;

    // Return unsubscribe function
    return [this, index]() {
        if (index < impl_->watchers.size()) {
            impl_->watchers[index] = nullptr;
        }
    };
}

// ============================================================
// Qt UI Binding Helpers
// ============================================================

std::vector<PropertyBinding> generate_property_bindings(EditorController& controller, const SceneObject& object) {
    std::vector<PropertyBinding> bindings;

    for (const auto& prop : object.meta.properties) {
        PropertyBinding binding;
        binding.property_path = prop.name;
        binding.meta = prop;

        // Create getter closure
        std::string path = prop.name;
        binding.getter = [&controller, path]() -> Value { return controller.get_property(path); };

        // Create setter closure
        binding.setter = [&controller, path](Value value) { controller.set_property(path, std::move(value)); };

        bindings.push_back(std::move(binding));
    }

    return bindings;
}

} // namespace lager_ext
