/**************************************************************************/
/*  editor_mcp_variant_codec.cpp                                          */
/**************************************************************************/

#include "editor_mcp_variant_codec.h"

#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/object/object.h"
#include "scene/main/node.h"

Variant EditorMCPVariantCodec::encode(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value;
		if (!object) {
			return Variant();
		}

		Dictionary encoded;
		Ref<Resource> resource = Object::cast_to<Resource>(object);
		if (resource.is_valid()) {
			encoded["$type"] = "Resource";
			encoded["class"] = resource->get_class();
			encoded["path"] = resource->get_path();
			return encoded;
		}

		encoded["$type"] = "Object";
		encoded["class"] = object->get_class();
		encoded["instance_id"] = int64_t(object->get_instance_id());
		Node *node = Object::cast_to<Node>(object);
		if (node && node->is_inside_tree()) {
			encoded["node_path"] = String(node->get_path());
		}
		return encoded;
	}

	return JSON::from_native(p_value, false);
}

bool EditorMCPVariantCodec::decode(const Variant &p_value, Variant::Type p_expected_type, Variant &r_value, String &r_error) {
	if (p_expected_type == Variant::OBJECT) {
		if (p_value.get_type() == Variant::NIL) {
			r_value = Variant();
			return true;
		}
		if (p_value.get_type() != Variant::DICTIONARY) {
			r_error = "Object properties require a Resource descriptor or null.";
			return false;
		}
		Dictionary descriptor = p_value;
		if (descriptor.get("$type", String()) != "Resource") {
			r_error = "Only Resource object properties can currently be assigned through MCP.";
			return false;
		}
		String path = descriptor.get("path", String());
		if (!path.begins_with("res://") && !path.begins_with("uid://")) {
			r_error = "Resource paths must use res:// or uid://.";
			return false;
		}
		Ref<Resource> resource = ResourceLoader::load(path);
		if (resource.is_null()) {
			r_error = "Unable to load resource: " + path;
			return false;
		}
		r_value = resource;
		return true;
	}

	Variant native_value = JSON::to_native(p_value, false);
	if (native_value.get_type() == p_expected_type || p_expected_type == Variant::NIL) {
		r_value = native_value;
		return true;
	}
	if (!Variant::can_convert_strict(native_value.get_type(), p_expected_type)) {
		r_error = vformat("Expected %s but received %s.", Variant::get_type_name(p_expected_type), Variant::get_type_name(native_value.get_type()));
		return false;
	}

	Callable::CallError call_error;
	const Variant *arguments[1] = { &native_value };
	Variant::construct(p_expected_type, r_value, arguments, 1, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		r_error = vformat("Unable to convert %s to %s.", Variant::get_type_name(native_value.get_type()), Variant::get_type_name(p_expected_type));
		return false;
	}
	return true;
}
