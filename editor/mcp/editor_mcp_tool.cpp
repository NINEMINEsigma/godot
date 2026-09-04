/**************************************************************************/
/*  editor_mcp_tool.cpp                                                   */
/**************************************************************************/

#include "editor_mcp_tool.h"

#include "core/io/json.h"

Dictionary EditorMCPTool::get_mcp_descriptor() const {
	Dictionary descriptor;
	descriptor["name"] = get_name();
	descriptor["description"] = get_description();
	descriptor["inputSchema"] = get_input_schema();
	return descriptor;
}

Dictionary EditorMCPTool::text_result(const String &p_text) {
	Dictionary item;
	item["type"] = "text";
	item["text"] = p_text;
	Array content;
	content.push_back(item);
	Dictionary result;
	result["content"] = content;
	return result;
}

Dictionary EditorMCPTool::json_result(const Variant &p_value) {
	return text_result(JSON::stringify(p_value));
}

Dictionary EditorMCPTool::error_result(const String &p_message) {
	Dictionary result = text_result(p_message);
	result["isError"] = true;
	return result;
}
