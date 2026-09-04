/**************************************************************************/
/*  editor_mcp_server.cpp                                                 */
/**************************************************************************/

#include "editor_mcp_server.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "core/variant/variant_utility.h"
#include "editor/editor_node.h"
#include "editor_mcp_context.h"
#include "editor_mcp_tool_registry.h"

EditorMCPServer *EditorMCPServer::singleton = nullptr;

EditorMCPServer::EditorMCPServer() {
	tool_registry = memnew(EditorMCPToolRegistry);
}

EditorMCPServer::~EditorMCPServer() {
	stop();
	memdelete(tool_registry);
	tool_registry = nullptr;
}

void EditorMCPServer::start_from_command_line() {
	if (singleton != nullptr) {
		return;
	}

	List<String> args = OS::get_singleton()->get_cmdline_args();
	bool enabled = false;
	int requested_port = 8765;
	String requested_bind = "127.0.0.1";
	String requested_permission = "restricted";
	String requested_token;

	for (const String &arg : args) {
		if (arg == "--mcp") {
			enabled = true;
		}
	}

	for (const List<String>::Element *E = args.front(); E; E = E->next()) {
		if (E->get() == "--mcp-port" && E->next()) {
			requested_port = E->next()->get().to_int();
			E = E->next();
		} else if (E->get() == "--mcp-bind" && E->next()) {
			requested_bind = E->next()->get();
			E = E->next();
		} else if (E->get() == "--mcp-permission" && E->next()) {
			requested_permission = E->next()->get();
			E = E->next();
		} else if (E->get() == "--mcp-token" && E->next()) {
			requested_token = E->next()->get();
			E = E->next();
		}
	}

	if (!enabled) {
		return;
	}

	singleton = memnew(EditorMCPServer);
	singleton->permission_mode = requested_permission;
	singleton->authentication_token = requested_token;
	if (requested_permission != "restricted" && requested_permission != "developer" && requested_permission != "full_access") {
		ERR_PRINT("Invalid MCP permission mode. Expected restricted, developer, or full_access.");
		memdelete(singleton);
		singleton = nullptr;
		return;
	}
	if (!singleton->start(requested_bind, requested_port)) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

void EditorMCPServer::poll_singleton() {
	if (singleton != nullptr) {
		singleton->poll();
	}
}

void EditorMCPServer::stop_singleton() {
	if (singleton != nullptr) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

bool EditorMCPServer::start(const String &p_bind_address, int p_port) {
	if (server.is_valid()) {
		return true;
	}
	if (p_bind_address != "127.0.0.1" && p_bind_address != "localhost" && p_bind_address != "::1" && authentication_token.is_empty()) {
		ERR_PRINT("A non-loopback MCP bind address requires --mcp-token.");
		return false;
	}
	if (p_port < 1 || p_port > 65535) {
		ERR_PRINT("MCP port must be between 1 and 65535.");
		return false;
	}

	bind_address = p_bind_address;
	port = p_port;
	server.instantiate();

	IPAddress address;
	if (bind_address.is_empty() || bind_address == "0.0.0.0") {
		address = IPAddress();
	} else {
		address = IP::get_singleton()->resolve_hostname(bind_address);
	}

	Error err = server->listen(port, address);
	if (err != OK) {
		ERR_PRINT("Unable to start the built-in MCP HTTP server on " + bind_address + ":" + itos(port));
		server.unref();
		return false;
	}

	print_line("Built-in Godot MCP server listening on http://" + bind_address + ":" + itos(port) + "/mcp");
	return true;
}

void EditorMCPServer::stop() {
	connections.clear();
	sessions.clear();
	if (server.is_valid()) {
		server->stop();
		server.unref();
	}
}

bool EditorMCPServer::is_running() const {
	return server.is_valid();
}

void EditorMCPServer::_accept_connections() {
	while (server.is_valid() && server->is_connection_available()) {
		Connection connection;
		connection.peer = server->take_connection();
		connection.accepted_at_msec = OS::get_singleton()->get_ticks_msec();
		connections.push_back(connection);
	}
}

void EditorMCPServer::poll() {
	if (!server.is_valid() || is_polling) {
		return;
	}

	is_polling = true;
	_accept_connections();
	for (int i = connections.size() - 1; i >= 0; i--) {
		if (_process_connection(connections.write[i])) {
			connections.remove_at(i);
		}
	}
	is_polling = false;
}

String EditorMCPServer::_get_header_value(const String &p_headers, const String &p_name) const {
	PackedStringArray lines = p_headers.split("\r\n");
	String prefix = p_name.to_lower() + ":";
	for (const String &line : lines) {
		String lower = line.to_lower();
		if (lower.begins_with(prefix)) {
			return line.substr(p_name.length() + 1).strip_edges();
		}
	}
	return String();
}

String EditorMCPServer::_create_session_id() {
	session_counter++;
	String entropy = itos(OS::get_singleton()->get_process_id()) + ":" +
			String::num_uint64(OS::get_singleton()->get_ticks_usec()) + ":" +
			String::num_uint64(session_counter) + ":" +
			String::num_uint64(Math::rand());
	return entropy.sha256_text();
}

bool EditorMCPServer::_process_connection(Connection &r_connection) {
	if (!r_connection.peer.is_valid()) {
		return true;
	}

	if (r_connection.response_sent) {
		if (OS::get_singleton()->get_ticks_msec() < r_connection.close_after_msec) {
			return false;
		}
		r_connection.peer->disconnect_from_host();
		return true;
	}

	if (r_connection.peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return true;
	}
	if (OS::get_singleton()->get_ticks_msec() - r_connection.accepted_at_msec > 10000) {
		_send_http_response(r_connection, 408, "Request Timeout", "Request was not completed within 10 seconds.", "text/plain");
		return false;
	}

	int available = r_connection.peer->get_available_bytes();
	if (available > 0) {
		r_connection.buffer += r_connection.peer->get_utf8_string(available);
	}
	if (r_connection.buffer.to_utf8_buffer().size() > 4 * 1024 * 1024) {
		_send_http_response(r_connection, 413, "Content Too Large", "MCP requests are limited to 4 MiB.", "text/plain");
		return false;
	}

	int header_end = r_connection.buffer.find("\r\n\r\n");
	if (header_end < 0) {
		if (r_connection.buffer.length() > 64 * 1024) {
			_send_http_response(r_connection, 431, "Request Header Fields Too Large", "HTTP headers are limited to 64 KiB.", "text/plain");
		}
		return false;
	}

	String headers = r_connection.buffer.substr(0, header_end);
	PackedStringArray request_lines = headers.split("\r\n");
	PackedStringArray request_line = request_lines[0].split(" ", false);
	if (request_line.size() < 2) {
		_send_http_response(r_connection, 400, "Bad Request", "Malformed HTTP request line.", "text/plain");
		return false;
	}
	String http_method = request_line[0].to_upper();
	String request_path = request_line[1].get_slice("?", 0);
	if (request_path != "/mcp") {
		_send_http_response(r_connection, 404, "Not Found", "The built-in MCP endpoint is /mcp.", "text/plain");
		return false;
	}
	if (http_method != "POST" && http_method != "DELETE") {
		_send_http_response(r_connection, 405, "Method Not Allowed", "The MCP endpoint accepts HTTP POST and DELETE requests.", "text/plain", "Allow: POST, DELETE\r\n");
		return false;
	}

	if (!authentication_token.is_empty()) {
		String authorization = _get_header_value(headers, "Authorization");
		if (authorization != "Bearer " + authentication_token) {
			_send_http_response(r_connection, 401, "Unauthorized", "A valid MCP bearer token is required.", "text/plain", "WWW-Authenticate: Bearer\r\n");
			return false;
		}
	}

	String session_id = _get_header_value(headers, "Mcp-Session-Id");
	if (!session_id.is_empty() && !sessions.has(session_id)) {
		_send_http_response(r_connection, 404, "Not Found", "The MCP session is unknown or has expired.", "text/plain");
		return false;
	}
	if (http_method == "DELETE") {
		if (session_id.is_empty()) {
			_send_http_response(r_connection, 400, "Bad Request", "Mcp-Session-Id is required to terminate a session.", "text/plain");
			return false;
		}
		sessions.erase(session_id);
		_send_http_response(r_connection, 200, "OK");
		return false;
	}

	String protocol_version = _get_header_value(headers, "MCP-Protocol-Version");
	if (!protocol_version.is_empty() && protocol_version != "2024-11-05" && protocol_version != "2025-03-26") {
		_send_http_response(r_connection, 400, "Bad Request", "Unsupported MCP-Protocol-Version: " + protocol_version, "text/plain");
		return false;
	}

	String content_length_header = _get_header_value(headers, "Content-Length");
	if (content_length_header.is_empty()) {
		_send_http_response(r_connection, 411, "Length Required", "Content-Length is required.", "text/plain");
		return false;
	}
	int content_length = content_length_header.to_int();
	if (content_length < 0 || content_length > 4 * 1024 * 1024) {
		_send_http_response(r_connection, 413, "Content Too Large", "MCP requests are limited to 4 MiB.", "text/plain");
		return false;
	}
	int body_start = header_end + 4;
	String available_body = r_connection.buffer.substr(body_start);
	if (available_body.to_utf8_buffer().size() < content_length) {
		return false;
	}

	Variant parsed = JSON::parse_string(available_body);
	Dictionary response;
	if (parsed.get_type() != Variant::DICTIONARY) {
		response["code"] = -32700;
		response["message"] = "Invalid JSON-RPC request.";
		_send_json_response(r_connection, Variant(), response, true);
		return false;
	}

	Dictionary request = parsed;
	if (request.get("jsonrpc", String()) != "2.0" || !request.has("method")) {
		response["code"] = -32600;
		response["message"] = "Invalid JSON-RPC 2.0 request.";
		_send_json_response(r_connection, request.get("id", Variant()), response, true);
		return false;
	}

	if (request.has("id")) {
		response = _handle_request(request);
		if (request.get("method", String()) == "initialize" && !response.has("code")) {
			String new_session_id = _create_session_id();
			sessions.insert(new_session_id);
			Dictionary rpc_response;
			rpc_response["jsonrpc"] = "2.0";
			rpc_response["id"] = request["id"];
			rpc_response["result"] = response;
			_send_http_response(r_connection, 200, "OK", JSON::stringify(rpc_response), "application/json", "Mcp-Session-Id: " + new_session_id + "\r\n");
		} else {
			_send_json_response(r_connection, request["id"], response, response.has("code"));
		}
	} else {
		_handle_request(request);
		_send_http_response(r_connection, 202, "Accepted");
	}
	return false;
}

void EditorMCPServer::_send_http_response(Connection &r_connection, int p_status, const String &p_status_text, const String &p_body, const String &p_content_type, const String &p_extra_headers) {
	PackedByteArray body_data = p_body.to_utf8_buffer();
	String http_response = "HTTP/1.1 " + itos(p_status) + " " + p_status_text + "\r\n";
	http_response += "Content-Type: " + p_content_type + "\r\n";
	http_response += "Content-Length: " + itos(body_data.size()) + "\r\n";
	http_response += "Cache-Control: no-store\r\n";
	http_response += p_extra_headers;
	http_response += "Connection: close\r\n\r\n";
	http_response += p_body;
	PackedByteArray response_data = http_response.to_utf8_buffer();
	Error error = r_connection.peer->put_data(response_data.ptr(), response_data.size());
	if (error != OK) {
		WARN_PRINT("Failed to write an MCP HTTP response. Error code: " + itos(error));
	}
	r_connection.response_sent = true;
	r_connection.close_after_msec = OS::get_singleton()->get_ticks_msec() + 100;
}

void EditorMCPServer::_send_json_response(Connection &r_connection, const Variant &p_id, const Dictionary &p_result, bool p_is_error) {
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = p_id;
	if (p_is_error) {
		response["error"] = p_result;
	} else {
		response["result"] = p_result;
	}
	_send_http_response(r_connection, 200, "OK", JSON::stringify(response));
}

Dictionary EditorMCPServer::_handle_request(const Dictionary &p_request) {
	String method = p_request.get("method", String());
	Dictionary params = p_request.get("params", Dictionary());

	if (method == "initialize") {
		Dictionary tool_capabilities;
		tool_capabilities["listChanged"] = false;
		Dictionary capabilities;
		capabilities["tools"] = tool_capabilities;

		Dictionary server_info;
		server_info["name"] = "godot-inbuilt-mcp";
		server_info["version"] = "0.2.0";

		String requested_version = params.get("protocolVersion", String());
		String selected_version = requested_version == "2024-11-05" ? "2024-11-05" : "2025-03-26";
		Dictionary result;
		result["protocolVersion"] = selected_version;
		result["capabilities"] = capabilities;
		result["serverInfo"] = server_info;
		return result;
	}
	if (method == "ping" || method == "notifications/initialized" || method == "notifications/cancelled") {
		return Dictionary();
	}
	if (method == "tools/list") {
		return _tool_list();
	}
	if (method == "tools/call") {
		return _tool_call(params);
	}

	Dictionary error;
	error["code"] = -32601;
	error["message"] = "Method not found: " + method;
	return error;
}

Dictionary EditorMCPServer::_tool_list() const {
	Dictionary result;
	result["tools"] = tool_registry->list_tools();
	return result;
}

Dictionary EditorMCPServer::_tool_call(const Dictionary &p_params) {
	StringName name = p_params.get("name", StringName());
	Dictionary arguments = p_params.get("arguments", Dictionary());
	EditorMCPContext context(EditorNode::get_singleton(), permission_mode);
	return tool_registry->call_tool(name, arguments, context);
}
