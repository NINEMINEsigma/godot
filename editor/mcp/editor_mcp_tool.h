/**************************************************************************/
/*  editor_mcp_tool.h                                                     */
/**************************************************************************/

#ifndef EDITOR_MCP_TOOL_H
#define EDITOR_MCP_TOOL_H

#include "core/string/string_name.h"
#include "core/variant/dictionary.h"

class EditorMCPContext;

enum class EditorMCPToolRisk {
	READ,
	WRITE,
	DANGEROUS,
};

class EditorMCPTool {
public:
	virtual ~EditorMCPTool() = default;

	virtual StringName get_name() const = 0;
	virtual String get_description() const = 0;
	virtual Dictionary get_input_schema() const = 0;
	virtual EditorMCPToolRisk get_risk() const { return EditorMCPToolRisk::READ; }
	virtual Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) = 0;

	Dictionary get_mcp_descriptor() const;
	static Dictionary text_result(const String &p_text);
	static Dictionary json_result(const Variant &p_value);
	static Dictionary error_result(const String &p_message);
};

#endif // EDITOR_MCP_TOOL_H
