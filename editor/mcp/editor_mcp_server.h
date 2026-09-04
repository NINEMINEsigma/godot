/**************************************************************************/
/*  editor_mcp_server.h                                                   */
/**************************************************************************/

#ifndef EDITOR_MCP_SERVER_H
#define EDITOR_MCP_SERVER_H

#include "core/io/ip.h"
#include "core/io/tcp_server.h"
#include "core/io/stream_peer_tcp.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

class EditorMCPToolRegistry;

class EditorMCPServer {
public:
	static void start_from_command_line();
	static void poll_singleton();
	static void stop_singleton();

	bool start(const String &p_bind_address, int p_port);
	void stop();
	void poll();
	bool is_running() const;

	EditorMCPServer();
	~EditorMCPServer();

private:
	struct Connection {
		Ref<StreamPeerTCP> peer;
		String buffer;
		bool response_sent = false;
		uint64_t close_after_msec = 0;
		uint64_t accepted_at_msec = 0;
	};

	static EditorMCPServer *singleton;
	Ref<TCPServer> server;
	Vector<Connection> connections;
	String bind_address;
	String permission_mode = "restricted";
	String authentication_token;
	HashSet<String> sessions;
	uint64_t session_counter = 0;
	int port = 8765;
	bool is_polling = false;
	EditorMCPToolRegistry *tool_registry = nullptr;

	void _accept_connections();
	bool _process_connection(Connection &r_connection);
	void _send_json_response(Connection &r_connection, const Variant &p_id, const Dictionary &p_result, bool p_is_error = false);
	void _send_http_response(Connection &r_connection, int p_status, const String &p_status_text, const String &p_body = String(), const String &p_content_type = "application/json", const String &p_extra_headers = String());
	String _create_session_id();
	Dictionary _handle_request(const Dictionary &p_request);
	Dictionary _tool_list() const;
	Dictionary _tool_call(const Dictionary &p_params);
	String _get_header_value(const String &p_headers, const String &p_name) const;

	EditorMCPServer(const EditorMCPServer &) = delete;
	EditorMCPServer &operator=(const EditorMCPServer &) = delete;
};

#endif // EDITOR_MCP_SERVER_H
