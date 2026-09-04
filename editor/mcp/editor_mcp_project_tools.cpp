/**************************************************************************/
/*  editor_mcp_project_tools.cpp                                          */
/**************************************************************************/

#include "editor_mcp_project_tools.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "editor/editor_node.h"
#include "editor/run/editor_run_bar.h"
#include "editor_mcp_context.h"
#include "editor_mcp_tool_registry.h"

namespace {

Dictionary empty_schema() {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = Dictionary();
	schema["additionalProperties"] = false;
	return schema;
}

Dictionary project_run_state() {
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	Dictionary state;
	state["playing"] = run_bar && run_bar->is_playing();
	state["playing_scene"] = run_bar ? run_bar->get_playing_scene() : String();
	state["process_id"] = run_bar ? int64_t(run_bar->get_current_process()) : int64_t(0);
	state["project_name"] = GLOBAL_GET("application/config/name");
	state["project_path"] = ProjectSettings::get_singleton()->get_resource_path();
	state["main_scene"] = GLOBAL_GET("application/run/main_scene");
	return state;
}

class ProjectGetRunStateTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.project.get_run_state"; }
	String get_description() const override { return "Returns whether the project is running, the running scene, process ID, and main scene."; }
	Dictionary get_input_schema() const override { return empty_schema(); }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		return json_result(project_run_state());
	}
};

class ProjectRunTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.project.run"; }
	String get_description() const override { return "Runs the main, current, or a custom project scene through the Godot editor. This executes project code."; }
	Dictionary get_input_schema() const override {
		Dictionary mode;
		mode["type"] = "string";
		Array modes;
		modes.push_back("main");
		modes.push_back("current");
		modes.push_back("custom");
		mode["enum"] = modes;
		mode["default"] = "main";
		Dictionary scene_path;
		scene_path["type"] = "string";
		scene_path["description"] = "Required res:// PackedScene path when mode is custom.";
		Dictionary arguments;
		arguments["type"] = "array";
		Dictionary argument_item;
		argument_item["type"] = "string";
		arguments["items"] = argument_item;
		arguments["description"] = "Optional command-line arguments passed to the running project.";
		Dictionary properties;
		properties["mode"] = mode;
		properties["scene_path"] = scene_path;
		properties["arguments"] = arguments;
		Dictionary schema;
		schema["type"] = "object";
		schema["properties"] = properties;
		schema["additionalProperties"] = false;
		return schema;
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::DANGEROUS; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		EditorRunBar *run_bar = EditorRunBar::get_singleton();
		if (!run_bar) {
			return error_result("The Godot project run bar is not available.");
		}

		String mode = p_arguments.get("mode", String("main"));
		Vector<String> play_arguments;
		Array input_arguments = p_arguments.get("arguments", Array());
		for (int i = 0; i < input_arguments.size(); i++) {
			if (input_arguments[i].get_type() != Variant::STRING) {
				return error_result("Every project run argument must be a string.");
			}
			play_arguments.push_back(String(input_arguments[i]));
		}

		if (mode == "main") {
			String main_scene = GLOBAL_GET("application/run/main_scene");
			if (main_scene.is_empty()) {
				return error_result("The project does not have a main scene configured.");
			}
			run_bar->play_main_scene(false, play_arguments);
		} else if (mode == "current") {
			Node *root = p_context.get_editor_node() ? p_context.get_editor_node()->get_edited_scene() : nullptr;
			if (!root || root->get_scene_file_path().is_empty()) {
				return error_result("The current edited scene must be saved before it can run.");
			}
			run_bar->play_current_scene(false, play_arguments);
		} else if (mode == "custom") {
			String scene_path = p_arguments.get("scene_path", String());
			if (!scene_path.begins_with("res://") || scene_path.contains("..") || ResourceLoader::get_resource_type(scene_path) != "PackedScene") {
				return error_result("scene_path must reference an existing res:// PackedScene.");
			}
			run_bar->play_custom_scene(scene_path, play_arguments);
		} else {
			return error_result("Unknown run mode. Expected main, current, or custom.");
		}

		Dictionary response = project_run_state();
		response["requested"] = true;
		response["mode"] = mode;
		return json_result(response);
	}
};

class ProjectStopTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.project.stop"; }
	String get_description() const override { return "Stops the project process currently running through the Godot editor."; }
	Dictionary get_input_schema() const override { return empty_schema(); }
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::DANGEROUS; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		EditorRunBar *run_bar = EditorRunBar::get_singleton();
		if (!run_bar) {
			return error_result("The Godot project run bar is not available.");
		}
		bool was_playing = run_bar->is_playing();
		if (was_playing) {
			run_bar->stop_playing();
		}
		Dictionary response = project_run_state();
		response["stopped"] = was_playing;
		return json_result(response);
	}
};

} // namespace

void register_editor_mcp_project_tools(EditorMCPToolRegistry &p_registry) {
	p_registry.register_tool(memnew(ProjectGetRunStateTool));
	p_registry.register_tool(memnew(ProjectRunTool));
	p_registry.register_tool(memnew(ProjectStopTool));
}
