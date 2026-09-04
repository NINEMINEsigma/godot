/**************************************************************************/
/*  editor_mcp_resource_tools.cpp                                         */
/**************************************************************************/

#include "editor_mcp_resource_tools.h"

#include "core/io/file_access.h"
#include "core/io/resource.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "editor_mcp_context.h"
#include "editor_mcp_tool_registry.h"
#include "editor_mcp_variant_codec.h"

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

Dictionary resource_schema() {
	Dictionary properties;
	properties["path"] = string_property("A res:// or uid:// resource path.");
	properties["include_properties"] = bool_property("Include editor-visible resource properties.", true);
	Array required;
	required.push_back("path");
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = properties;
	schema["required"] = required;
	schema["additionalProperties"] = false;
	return schema;
}

bool validate_resource_path(const String &p_input, String &r_path, String &r_error) {
	String path = p_input.strip_edges().replace("\\", "/");
	if (path.begins_with("uid://")) {
		r_path = path;
		return true;
	}
	if (!path.begins_with("res://")) {
		if (path.is_absolute_path() || path.contains(":") || path.is_empty()) {
			r_error = "Only project-relative, res://, or uid:// resource paths are allowed.";
			return false;
		}
		path = "res://" + path.trim_prefix("./");
	}
	if (path.contains("::")) {
		r_error = "Built-in subresource paths are not accepted by this tool.";
		return false;
	}
	PackedStringArray segments = path.trim_prefix("res://").split("/", false);
	for (const String &segment : segments) {
		if (segment == "..") {
			r_error = "Resource paths cannot contain '..'.";
			return false;
		}
	}
	if (path == "res://.godot" || path.begins_with("res://.godot/")) {
		r_error = "The internal res://.godot directory is not accessible through MCP resource tools.";
		return false;
	}
	r_path = path;
	return true;
}

class ResourceGetInfoTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.resource.get_info"; }
	String get_description() const override { return "Loads a project resource and returns its type, identity, and optionally editor-visible properties."; }
	Dictionary get_input_schema() const override { return resource_schema(); }
	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String path;
		String error;
		if (!validate_resource_path(p_arguments.get("path", String()), path, error)) {
			return error_result(error);
		}

		String resolved_path = path;
		if (path.begins_with("uid://")) {
			ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id(path);
			if (!ResourceUID::get_singleton()->has_id(uid)) {
				return error_result("Unknown resource UID: " + path);
			}
			resolved_path = ResourceUID::get_singleton()->get_id_path(uid);
		}
		if (!ResourceLoader::exists(resolved_path)) {
			return error_result("Resource does not exist: " + resolved_path);
		}

		Ref<Resource> resource = ResourceLoader::load(resolved_path);
		if (resource.is_null()) {
			return error_result("Unable to load resource: " + resolved_path);
		}

		Dictionary response;
		response["requested_path"] = path;
		response["path"] = resource->get_path();
		response["class"] = resource->get_class();
		response["resource_name"] = resource->get_name();
		response["resource_scene_unique_id"] = resource->get_scene_unique_id();
		response["cached"] = ResourceCache::has(resource->get_path());

		if (bool(p_arguments.get("include_properties", true))) {
			Array properties;
			List<PropertyInfo> property_list;
			resource->get_property_list(&property_list);
			for (const PropertyInfo &property : property_list) {
				if (!(property.usage & PROPERTY_USAGE_EDITOR) || String(property.name).begins_with("_")) {
					continue;
				}
				bool valid = false;
				Variant value = resource->get(property.name, &valid);
				if (!valid) {
					continue;
				}
				Dictionary entry;
				entry["name"] = String(property.name);
				entry["type"] = Variant::get_type_name(property.type);
				entry["hint"] = property.hint;
				entry["hint_string"] = property.hint_string;
				entry["value"] = EditorMCPVariantCodec::encode(value);
				properties.push_back(entry);
				if (properties.size() >= 1000) {
					break;
				}
			}
			response["properties"] = properties;
			response["properties_truncated"] = properties.size() >= 1000;
		}
		return json_result(response);
	}
};

} // namespace

void register_editor_mcp_resource_tools(EditorMCPToolRegistry &p_registry) {
	p_registry.register_tool(memnew(ResourceGetInfoTool));
}
