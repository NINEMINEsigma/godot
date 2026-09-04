/**************************************************************************/
/*  editor_mcp_variant_codec.h                                            */
/**************************************************************************/

#ifndef EDITOR_MCP_VARIANT_CODEC_H
#define EDITOR_MCP_VARIANT_CODEC_H

#include "core/variant/variant.h"

class EditorMCPVariantCodec {
public:
	// Encodes Godot Variant values into JSON-compatible MCP values. Built-in
	// math types use Godot's lossless JSON native representation.
	static Variant encode(const Variant &p_value);

	// Decodes a JSON-compatible MCP value and converts it to the requested
	// Godot Variant type. Resource properties additionally accept
	// { "$type": "Resource", "path": "res://..." }.
	static bool decode(const Variant &p_value, Variant::Type p_expected_type, Variant &r_value, String &r_error);
};

#endif // EDITOR_MCP_VARIANT_CODEC_H
