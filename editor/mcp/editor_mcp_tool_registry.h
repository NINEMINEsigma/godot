/**************************************************************************/
/*  editor_mcp_tool_registry.h                                            */
/**************************************************************************/

#ifndef EDITOR_MCP_TOOL_REGISTRY_H
#define EDITOR_MCP_TOOL_REGISTRY_H

#include "core/templates/hash_map.h"
#include "editor_mcp_tool.h"

class EditorMCPContext;

class EditorMCPToolRegistry {
	HashMap<StringName, EditorMCPTool *> tools;

public:
	EditorMCPToolRegistry();
	~EditorMCPToolRegistry();

	void register_tool(EditorMCPTool *p_tool);
	void unregister_tool(const StringName &p_name);
	Array list_tools() const;
	Dictionary call_tool(const StringName &p_name, const Dictionary &p_arguments, EditorMCPContext &p_context) const;

	EditorMCPToolRegistry(const EditorMCPToolRegistry &) = delete;
	EditorMCPToolRegistry &operator=(const EditorMCPToolRegistry &) = delete;
};

#endif // EDITOR_MCP_TOOL_REGISTRY_H
