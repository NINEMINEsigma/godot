/**************************************************************************/
/*  editor_mcp_filesystem_tools.cpp                                       */
/**************************************************************************/

#include "editor_mcp_filesystem_tools.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "editor/file_system/editor_file_system.h"
#include "editor_mcp_context.h"
#include "editor_mcp_tool_registry.h"

namespace {

Dictionary string_property(const String &p_description) {
	Dictionary property;
	property["type"] = "string";
	property["description"] = p_description;
	return property;
}

Dictionary bool_property(const String &p_description, bool p_default) {
	Dictionary property;
	property["type"] = "boolean";
	property["description"] = p_description;
	property["default"] = p_default;
	return property;
}

Dictionary object_schema(const Dictionary &p_properties, const Array &p_required) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

bool normalize_project_path(const String &p_input, String &r_path, String &r_error, bool p_allow_project_root = true) {
	String path = p_input.strip_edges().replace("\\", "/");
	if (path.is_empty() || path == "." || path == "res://") {
		if (!p_allow_project_root) {
			r_error = "A file path inside the project is required.";
			return false;
		}
		r_path = "res://";
		return true;
	}
	if (path.contains("::")) {
		r_error = "Subresource paths are not valid filesystem paths.";
		return false;
	}
	if (!path.begins_with("res://")) {
		if (path.is_absolute_path() || path.contains(":")) {
			r_error = "Only project-relative or res:// paths are allowed.";
			return false;
		}
		path = "res://" + path.trim_prefix("./");
	}

	String relative = path.trim_prefix("res://");
	PackedStringArray segments = relative.split("/", false);
	for (const String &segment : segments) {
		if (segment == "..") {
			r_error = "Project paths cannot contain '..'.";
			return false;
		}
	}
	path = "res://" + relative.simplify_path().trim_prefix("/");
	if (path == "res://.godot" || path.begins_with("res://.godot/")) {
		r_error = "The internal res://.godot directory is not accessible through MCP filesystem tools.";
		return false;
	}
	r_path = path;
	return true;
}

class FilesystemListTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.filesystem.list"; }
	String get_description() const override { return "Lists files and directories inside the current Godot project."; }
	Dictionary get_input_schema() const override {
		Dictionary recursive;
		recursive["type"] = "boolean";
		recursive["default"] = false;
		recursive["description"] = "Reserved for future recursive listing. Currently must be false.";
		Dictionary properties;
		properties["path"] = string_property("Project-relative or res:// directory path.");
		properties["recursive"] = recursive;
		Array required;
		required.push_back("path");
		return object_schema(properties, required);
	}
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		if (bool(p_arguments.get("recursive", false))) {
			return error_result("Recursive listing is not enabled yet. List one directory at a time.");
		}
		String path;
		String error;
		if (!normalize_project_path(p_arguments.get("path", String()), path, error)) {
			return error_result(error);
		}
		Error open_error = OK;
		Ref<DirAccess> directory = DirAccess::open(path, &open_error);
		if (directory.is_null()) {
			return error_result("Unable to open project directory: " + path);
		}

		Array entries;
		directory->list_dir_begin();
		for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
			if (name == "." || name == ".." || (path == "res://" && name == ".godot")) {
				continue;
			}
			Dictionary entry;
			entry["name"] = name;
			entry["path"] = path.path_join(name);
			entry["is_directory"] = directory->current_is_dir();
			entries.push_back(entry);
			if (entries.size() >= 10000) {
				break;
			}
		}
		directory->list_dir_end();

		Dictionary response;
		response["path"] = path;
		response["entries"] = entries;
		response["truncated"] = entries.size() >= 10000;
		return json_result(response);
	}
};

class FilesystemReadTextTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.filesystem.read_text"; }
	String get_description() const override { return "Reads a UTF-8 text file inside the current Godot project."; }
	Dictionary get_input_schema() const override {
		Dictionary max_bytes;
		max_bytes["type"] = "integer";
		max_bytes["minimum"] = 1;
		max_bytes["maximum"] = 4194304;
		max_bytes["default"] = 1048576;
		Dictionary properties;
		properties["path"] = string_property("Project-relative or res:// file path.");
		properties["max_bytes"] = max_bytes;
		Array required;
		required.push_back("path");
		return object_schema(properties, required);
	}
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path;
		String error;
		if (!normalize_project_path(p_arguments.get("path", String()), path, error, false)) {
			return error_result(error);
		}
		int64_t max_bytes = CLAMP(int64_t(p_arguments.get("max_bytes", 1048576)), int64_t(1), int64_t(4194304));
		Error open_error = OK;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ, &open_error);
		if (file.is_null()) {
			return error_result("Unable to read file: " + path + " (error " + itos(open_error) + ")");
		}
		int64_t length = file->get_length();
		if (length > max_bytes) {
			return error_result("File size " + itos(length) + " exceeds max_bytes " + itos(max_bytes) + ".");
		}
		PackedByteArray bytes = file->get_buffer(length);
		String text = String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
		Dictionary response;
		response["path"] = path;
		response["size_bytes"] = length;
		response["text"] = text;
		return json_result(response);
	}
};

class FilesystemWriteTextTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.filesystem.write_text"; }
	String get_description() const override { return "Writes a UTF-8 text file inside the current project. Filesystem writes are not part of scene undo history."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["path"] = string_property("Project-relative or res:// file path.");
		properties["text"] = string_property("Complete UTF-8 file contents.");
		properties["overwrite"] = bool_property("Allow replacement when the file already exists.", false);
		Array required;
		required.push_back("path");
		required.push_back("text");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path;
		String error;
		if (!normalize_project_path(p_arguments.get("path", String()), path, error, false)) {
			return error_result(error);
		}
		String text = p_arguments.get("text", String());
		if (text.to_utf8_buffer().size() > 4 * 1024 * 1024) {
			return error_result("Text writes are limited to 4 MiB.");
		}
		if (FileAccess::exists(path) && !bool(p_arguments.get("overwrite", false))) {
			return error_result("File already exists. Set overwrite to true to replace it: " + path);
		}
		String parent = path.get_base_dir();
		if (!DirAccess::dir_exists_absolute(ProjectSettings::get_singleton()->globalize_path(parent))) {
			return error_result("Parent directory does not exist: " + parent);
		}
		Error open_error = OK;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &open_error);
		if (file.is_null()) {
			return error_result("Unable to write file: " + path + " (error " + itos(open_error) + ")");
		}
		file->store_string(text);
		if (file->get_error() != OK) {
			return error_result("An error occurred while writing: " + path);
		}
		file->close();
		EditorFileSystem::get_singleton()->scan_changes();
		Dictionary response;
		response["written"] = true;
		response["path"] = path;
		response["size_bytes"] = text.to_utf8_buffer().size();
		return json_result(response);
	}
};

class FilesystemMakeDirectoryTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.filesystem.make_directory"; }
	String get_description() const override { return "Creates a directory, including missing parents, inside the current Godot project."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["path"] = string_property("Project-relative or res:// directory path.");
		Array required;
		required.push_back("path");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::WRITE; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path;
		String error;
		if (!normalize_project_path(p_arguments.get("path", String()), path, error, false)) {
			return error_result(error);
		}
		String absolute = ProjectSettings::get_singleton()->globalize_path(path);
		if (DirAccess::dir_exists_absolute(absolute)) {
			Dictionary response;
			response["created"] = false;
			response["already_exists"] = true;
			response["path"] = path;
			return json_result(response);
		}
		Error create_error = DirAccess::make_dir_recursive_absolute(absolute);
		if (create_error != OK) {
			return error_result("Unable to create directory: " + path + " (error " + itos(create_error) + ")");
		}
		EditorFileSystem::get_singleton()->scan_changes();
		Dictionary response;
		response["created"] = true;
		response["path"] = path;
		return json_result(response);
	}
};

class FilesystemDeleteTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.filesystem.delete"; }
	String get_description() const override { return "Permanently deletes one file inside the current project. This operation cannot be undone."; }
	Dictionary get_input_schema() const override {
		Dictionary properties;
		properties["path"] = string_property("Project-relative or res:// file path. Directories are rejected.");
		Array required;
		required.push_back("path");
		return object_schema(properties, required);
	}
	EditorMCPToolRisk get_risk() const override { return EditorMCPToolRisk::DANGEROUS; }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path;
		String error;
		if (!normalize_project_path(p_arguments.get("path", String()), path, error, false)) {
			return error_result(error);
		}
		String absolute = ProjectSettings::get_singleton()->globalize_path(path);
		if (!FileAccess::exists(absolute)) {
			return error_result("File does not exist: " + path);
		}
		Error delete_error = DirAccess::remove_absolute(absolute);
		if (delete_error != OK) {
			return error_result("Unable to delete file: " + path + " (error " + itos(delete_error) + ")");
		}
		EditorFileSystem::get_singleton()->scan_changes();
		Dictionary response;
		response["deleted"] = true;
		response["path"] = path;
		return json_result(response);
	}
};

} // namespace

void register_editor_mcp_filesystem_tools(EditorMCPToolRegistry &p_registry) {
	p_registry.register_tool(memnew(FilesystemListTool));
	p_registry.register_tool(memnew(FilesystemReadTextTool));
	p_registry.register_tool(memnew(FilesystemWriteTextTool));
	p_registry.register_tool(memnew(FilesystemMakeDirectoryTool));
	p_registry.register_tool(memnew(FilesystemDeleteTool));
}
