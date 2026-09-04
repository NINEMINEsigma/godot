/**************************************************************************/
/*  editor_mcp_tool_registry.cpp                                          */
/**************************************************************************/

#include "editor_mcp_tool_registry.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor_mcp_context.h"
#include "editor_mcp_capture_tools.h"
#include "editor_mcp_filesystem_tools.h"
#include "editor_mcp_project_tools.h"
#include "editor_mcp_resource_tools.h"
#include "editor_mcp_variant_codec.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"

namespace {

Dictionary empty_object_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = Dictionary();
	schema["additionalProperties"] = false;
	return schema;
}

Dictionary object_schema(const Dictionary &p_properties, const Array &p_required = Array()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

Dictionary string_property(const String &p_description) {
	Dictionary property;
	property["type"] = "string";
	property["description"] = p_description;
	return property;
}

Node *resolve_scene_node(EditorMCPContext &p_context, const String &p_path, String &r_error) {
	EditorNode *editor = p_context.get_editor_node();
	Node *root = editor ? editor->get_edited_scene() : nullptr;
	if (!root) {
		r_error = "No scene is currently open in the editor.";
		return nullptr;
	}
	if (p_path.is_empty() || p_path == ".") {
		return root;
	}
	NodePath path(p_path);
	if (path.is_absolute() || p_path.contains("..")) {
		r_error = "node_path must be relative to the edited scene root and cannot contain '..'.";
		return nullptr;
	}
	Node *node = root->get_node_or_null(path);
	if (!node) {
		r_error = "Scene node not found: " + p_path;
	}
	return node;
}

class EditorStateTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.editor.get_state"; }
	String get_description() const override { return "Returns the current Godot editor and project state."; }
	Dictionary get_input_schema() const override { return empty_object_schema(); }

	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		Dictionary state;
		state["editor"] = "Godot";
		state["mcp"] = true;
		state["project_path"] = ProjectSettings::get_singleton()->get_resource_path();
		state["permission_mode"] = p_context.get_permission_mode();

		EditorNode *editor = p_context.get_editor_node();
		Node *root = editor && editor->get_tree() ? editor->get_tree()->get_edited_scene_root() : nullptr;
		state["scene_open"] = root != nullptr;
		state["scene_path"] = root ? root->get_scene_file_path() : String();
		return json_result(state);
	}
};

class SceneTreeTool : public EditorMCPTool {
	Dictionary _serialize_node(Node *p_scene_root, Node *p_node, int p_depth, int p_max_depth, int p_max_nodes, int &r_count, bool &r_truncated) const {
		Dictionary data;
		data["name"] = String(p_node->get_name());
		data["type"] = p_node->get_class();
		data["path"] = p_node == p_scene_root ? String(".") : String(p_scene_root->get_path_to(p_node));
		data["child_count"] = p_node->get_child_count();

		Array children;
		if (p_depth < p_max_depth) {
			for (int i = 0; i < p_node->get_child_count(); i++) {
				if (r_count >= p_max_nodes) {
					r_truncated = true;
					break;
				}
				Node *child = p_node->get_child(i);
				r_count++;
				children.push_back(_serialize_node(p_scene_root, child, p_depth + 1, p_max_depth, p_max_nodes, r_count, r_truncated));
			}
		} else if (p_node->get_child_count() > 0) {
			r_truncated = true;
		}
		data["children"] = children;
		return data;
	}

public:
	StringName get_name() const override { return "godot.scene.get_tree"; }
	String get_description() const override { return "Returns the current edited scene tree with node names, types, paths, and children."; }
	Dictionary get_input_schema() const override {
		Dictionary depth;
		depth["type"] = "integer";
		depth["minimum"] = 0;
		depth["maximum"] = 64;
		depth["default"] = 32;
		depth["description"] = "Maximum recursion depth.";

		Dictionary nodes;
		nodes["type"] = "integer";
		nodes["minimum"] = 1;
		nodes["maximum"] = 10000;
		nodes["default"] = 2000;
		nodes["description"] = "Maximum number of nodes returned.";

		Dictionary properties;
		properties["max_depth"] = depth;
		properties["max_nodes"] = nodes;

		Dictionary schema;
		schema["type"] = "object";
		schema["properties"] = properties;
		schema["additionalProperties"] = false;
		return schema;
	}

	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		EditorNode *editor = p_context.get_editor_node();
		if (!editor) {
			return error_result("The Godot editor is not available.");
		}
		Node *root = editor->get_tree() ? editor->get_tree()->get_edited_scene_root() : nullptr;
		if (!root) {
			return error_result("No scene is currently open in the editor.");
		}

		int max_depth = CLAMP(int(p_arguments.get("max_depth", 32)), 0, 64);
		int max_nodes = CLAMP(int(p_arguments.get("max_nodes", 2000)), 1, 10000);
		int count = 1;
		bool truncated = false;

		Dictionary response;
		response["scene_path"] = root->get_scene_file_path();
		response["root"] = _serialize_node(root, root, 0, max_depth, max_nodes, count, truncated);
		response["node_count"] = count;
		response["truncated"] = truncated;
		return json_result(response);
	}
};

class SceneGetNodeTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.scene.get_node"; }
	String get_description() const override { return "Returns details and editor-visible properties for one node in the current scene."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["node_path"] = string_property("Scene-relative node path. Use '.' for the scene root.");
		Array required;
		required.push_back("node_path");
		return object_schema(properties, required);
	}
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path = p_arguments.get("node_path", String());
		String error;
		Node *node = resolve_scene_node(p_context, path, error);
		if (!node) {
			return error_result(error);
		}

		Dictionary response;
		response["name"] = String(node->get_name());
		response["type"] = node->get_class();
		response["path"] = path.is_empty() ? String(".") : path;
		response["child_count"] = node->get_child_count();

		Array property_entries;
		List<PropertyInfo> property_list;
		node->get_property_list(&property_list);
		for (const PropertyInfo &property : property_list) {
			if (!(property.usage & PROPERTY_USAGE_EDITOR) || String(property.name).begins_with("_")) {
				continue;
			}
			bool valid = false;
			Variant value = node->get(property.name, &valid);
			if (!valid) {
				continue;
			}
			Dictionary entry;
			entry["name"] = String(property.name);
			entry["type"] = Variant::get_type_name(property.type);
			entry["hint"] = property.hint;
			entry["hint_string"] = property.hint_string;
			entry["value"] = EditorMCPVariantCodec::encode(value);
			property_entries.push_back(entry);
		}
		response["properties"] = property_entries;
		return json_result(response);
	}
};

class SceneCreateNodeTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.scene.create_node"; }
	String get_description() const override { return "Creates a Godot Node under a parent in the current scene. The action supports editor undo."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["parent_path"] = string_property("Scene-relative parent path. Use '.' for the scene root.");
		properties["type"] = string_property("Godot node class name, for example Node3D or Camera3D.");
		properties["name"] = string_property("Optional node name. Defaults to the class name.");
		Array required;
		required.push_back("parent_path");
		required.push_back("type");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String parent_path = p_arguments.get("parent_path", String());
		String type = p_arguments.get("type", String());
		String requested_name = p_arguments.get("name", type);
		String error;
		Node *parent = resolve_scene_node(p_context, parent_path, error);
		if (!parent) {
			return error_result(error);
		}
		if (!ClassDB::class_exists(type) || !ClassDB::can_instantiate(type)) {
			return error_result("Unknown or non-instantiable Godot class: " + type);
		}
		Object *object = ClassDB::instantiate(type);
		Node *node = Object::cast_to<Node>(object);
		if (!node) {
			memdelete(object);
			return error_result("The requested class does not inherit Node: " + type);
		}
		node->set_name(requested_name);

		Node *scene_root = p_context.get_editor_node()->get_edited_scene();
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action("MCP: Create Node");
		undo_redo->add_do_method(parent, "add_child", node, true);
		undo_redo->add_do_method(node, "set_owner", scene_root);
		undo_redo->add_do_reference(node);
		undo_redo->add_undo_method(parent, "remove_child", node);
		undo_redo->commit_action();

		Dictionary response;
		response["created"] = true;
		response["name"] = String(node->get_name());
		response["type"] = node->get_class();
		response["path"] = String(scene_root->get_path_to(node));
		return json_result(response);
	}
};

class SceneSetPropertyTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.scene.set_property"; }
	String get_description() const override { return "Sets one node property with lossless Godot Variant decoding. Math types use the JSON native representation returned by get_node. The action supports editor undo."; }
	Dictionary get_input_schema() const override {
		Dictionary value;
		value["description"] = "New value. For Vector, Transform, Color, NodePath, and other Godot types, use the lossless JSON native representation returned by godot.scene.get_node.";
		Dictionary properties;
		properties["node_path"] = string_property("Scene-relative node path. Use '.' for the scene root.");
		properties["property"] = string_property("Godot property name.");
		properties["value"] = value;
		Array required;
		required.push_back("node_path");
		required.push_back("property");
		required.push_back("value");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path = p_arguments.get("node_path", String());
		StringName property = p_arguments.get("property", StringName());
		if (!p_arguments.has("value")) {
			return error_result("Missing required argument: value");
		}
		String error;
		Node *node = resolve_scene_node(p_context, path, error);
		if (!node) {
			return error_result(error);
		}
		bool valid = false;
		Variant old_value = node->get(property, &valid);
		if (!valid) {
			return error_result("Property not found: " + String(property));
		}

		Variant::Type expected_type = old_value.get_type();
		List<PropertyInfo> property_list;
		node->get_property_list(&property_list);
		for (const PropertyInfo &property_info : property_list) {
			if (property_info.name == property) {
				expected_type = property_info.type;
				break;
			}
		}

		Variant new_value;
		String conversion_error;
		if (!EditorMCPVariantCodec::decode(p_arguments["value"], expected_type, new_value, conversion_error)) {
			return error_result("Invalid value for property '" + String(property) + "': " + conversion_error);
		}

		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action("MCP: Set Node Property");
		undo_redo->add_do_property(node, property, new_value);
		undo_redo->add_undo_property(node, property, old_value);
		undo_redo->commit_action();

		Dictionary response;
		response["updated"] = true;
		response["node_path"] = path;
		response["property"] = String(property);
		response["value"] = EditorMCPVariantCodec::encode(new_value);
		return json_result(response);
	}
};

class SceneDeleteNodeTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.scene.delete_node"; }
	String get_description() const override { return "Removes a node from the current scene. The action supports editor undo."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["node_path"] = string_property("Scene-relative node path. The scene root cannot be deleted.");
		Array required;
		required.push_back("node_path");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path = p_arguments.get("node_path", String());
		if (path.is_empty() || path == ".") {
			return error_result("The edited scene root cannot be deleted.");
		}
		String error;
		Node *node = resolve_scene_node(p_context, path, error);
		if (!node) {
			return error_result(error);
		}
		Node *parent = node->get_parent();
		Node *owner = node->get_owner();
		int index = node->get_index(false);

		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action("MCP: Delete Node");
		undo_redo->add_do_method(parent, "remove_child", node);
		undo_redo->add_undo_method(parent, "add_child", node, true);
		undo_redo->add_undo_method(parent, "move_child", node, index);
		undo_redo->add_undo_method(node, "set_owner", owner);
		undo_redo->add_undo_reference(node);
		undo_redo->commit_action();

		Dictionary response;
		response["deleted"] = true;
		response["node_path"] = path;
		return json_result(response);
	}
};

class SceneSaveTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.scene.save"; }
	String get_description() const override { return "Saves the current edited scene to its existing file path."; }
	Dictionary get_input_schema() const override { return empty_object_schema(); }
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		EditorNode *editor = p_context.get_editor_node();
		Node *root = editor ? editor->get_edited_scene() : nullptr;
		if (!root) {
			return error_result("No scene is currently open in the editor.");
		}
		if (root->get_scene_file_path().is_empty()) {
			return error_result("The current scene has no file path. Save it manually once before using this tool.");
		}
		Error error = EditorInterface::get_singleton()->save_scene();
		if (error != OK) {
			return error_result("Godot failed to save the current scene. Error code: " + itos(error));
		}
		Dictionary response;
		response["saved"] = true;
		response["scene_path"] = root->get_scene_file_path();
		return json_result(response);
	}
};

} // namespace

EditorMCPToolRegistry::EditorMCPToolRegistry() {
	register_tool(memnew(EditorStateTool));
	register_tool(memnew(SceneTreeTool));
	register_tool(memnew(SceneGetNodeTool));
	register_tool(memnew(SceneCreateNodeTool));
	register_tool(memnew(SceneSetPropertyTool));
	register_tool(memnew(SceneDeleteNodeTool));
	register_tool(memnew(SceneSaveTool));
	register_editor_mcp_filesystem_tools(*this);
	register_editor_mcp_resource_tools(*this);
	register_editor_mcp_project_tools(*this);
	register_editor_mcp_capture_tools(*this);
}

EditorMCPToolRegistry::~EditorMCPToolRegistry() {
	for (const KeyValue<StringName, EditorMCPTool *> &entry : tools) {
		memdelete(entry.value);
	}
	tools.clear();
}

void EditorMCPToolRegistry::register_tool(EditorMCPTool *p_tool) {
	ERR_FAIL_NULL(p_tool);
	const StringName name = p_tool->get_name();
	if (tools.has(name)) {
		memdelete(tools[name]);
	}
	tools.insert(name, p_tool);
}

void EditorMCPToolRegistry::unregister_tool(const StringName &p_name) {
	EditorMCPTool **tool = tools.getptr(p_name);
	if (!tool) {
		return;
	}
	memdelete(*tool);
	tools.erase(p_name);
}

Array EditorMCPToolRegistry::list_tools() const {
	Array result;
	for (const KeyValue<StringName, EditorMCPTool *> &entry : tools) {
		result.push_back(entry.value->get_mcp_descriptor());
	}
	return result;
}

Dictionary EditorMCPToolRegistry::call_tool(const StringName &p_name, const Dictionary &p_arguments, EditorMCPContext &p_context) const {
	EditorMCPTool *const *tool = tools.getptr(p_name);
	if (!tool) {
		Dictionary item;
		item["type"] = "text";
		item["text"] = "Unknown MCP tool: " + String(p_name);
		Array content;
		content.push_back(item);
		Dictionary result;
		result["content"] = content;
		result["isError"] = true;
		return result;
	}

	EditorMCPToolRisk risk = (*tool)->get_risk();
	if (risk == EditorMCPToolRisk::WRITE && !p_context.allows_write()) {
		return EditorMCPTool::error_result("This tool requires developer permission mode.");
	}
	if (risk == EditorMCPToolRisk::DANGEROUS && !p_context.allows_full_access()) {
		return EditorMCPTool::error_result("This tool requires full_access permission mode.");
	}
	return (*tool)->execute(p_arguments, p_context);
}
