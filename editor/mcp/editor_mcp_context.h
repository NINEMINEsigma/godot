/**************************************************************************/
/*  editor_mcp_context.h                                                  */
/**************************************************************************/

#ifndef EDITOR_MCP_CONTEXT_H
#define EDITOR_MCP_CONTEXT_H

#include "core/string/ustring.h"

class EditorNode;

class EditorMCPContext {
	EditorNode *editor_node = nullptr;
	String permission_mode = "restricted";

public:
	explicit EditorMCPContext(EditorNode *p_editor_node, const String &p_permission_mode = "restricted") :
			editor_node(p_editor_node), permission_mode(p_permission_mode) {}

	EditorNode *get_editor_node() const { return editor_node; }
	const String &get_permission_mode() const { return permission_mode; }

	bool allows_write() const {
		return permission_mode == "developer" || permission_mode == "full_access";
	}
	bool allows_full_access() const {
		return permission_mode == "full_access";
	}
};

#endif // EDITOR_MCP_CONTEXT_H
