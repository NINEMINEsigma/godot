/**************************************************************************/
/*  editor_mcp_capture_tools.cpp                                          */
/**************************************************************************/

#include "editor_mcp_capture_tools.h"

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"
#include "core/io/image.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor_mcp_context.h"
#include "editor_mcp_tool_registry.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

#ifdef WINDOWS_ENABLED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000003
#endif
#endif

namespace {

Dictionary object_schema(const Dictionary &p_properties, const Array &p_required = Array()) {
	Dictionary schema;
	schema["type"] = "object";
	schema["properties"] = p_properties;
	schema["required"] = p_required;
	schema["additionalProperties"] = false;
	return schema;
}

Dictionary save_path_property() {
	Dictionary property;
	property["type"] = "string";
	property["description"] = "Optional absolute file path. If provided, the image is saved to this path as a PNG file instead of being returned as a base64-encoded image content block.";
	return property;
}

String image_to_base64_png(const Ref<Image> &p_image) {
	if (p_image.is_null() || p_image->is_empty()) {
		return String();
	}
	Vector<uint8_t> png_buffer = p_image->save_png_to_buffer();
	if (png_buffer.is_empty()) {
		return String();
	}
	return CryptoCore::b64_encode_str(png_buffer.ptr(), png_buffer.size());
}

Dictionary image_result(const Ref<Image> &p_image, const String &p_source_label, const String &p_save_path = String()) {
	if (p_image.is_null() || p_image->is_empty()) {
		return EditorMCPTool::error_result("Failed to capture " + p_source_label + ". The viewport may not be rendering yet.");
	}

	if (!p_save_path.is_empty()) {
		Error save_error = p_image->save_png(p_save_path);
		if (save_error != OK) {
			return EditorMCPTool::error_result("Failed to save " + p_source_label + " image to: " + p_save_path + " (error " + itos(save_error) + ")");
		}
		Dictionary result;
		result["saved"] = true;
		result["path"] = p_save_path;
		result["source"] = p_source_label;
		result["width"] = p_image->get_width();
		result["height"] = p_image->get_height();

		Dictionary text_item;
		text_item["type"] = "text";
		text_item["text"] = "Image saved to " + p_save_path + " (" + itos(p_image->get_width()) + "x" + itos(p_image->get_height()) + ")";
		Array content;
		content.push_back(text_item);
		result["content"] = content;
		return result;
	}

	String base64 = image_to_base64_png(p_image);
	if (base64.is_empty()) {
		return EditorMCPTool::error_result("Failed to encode " + p_source_label + " image to PNG.");
	}

	Dictionary image_data;
	image_data["format"] = "png";
	image_data["base64"] = base64;
	image_data["width"] = p_image->get_width();
	image_data["height"] = p_image->get_height();

	Dictionary content_item;
	content_item["type"] = "image";
	content_item["data"] = base64;
	content_item["mimeType"] = "image/png";

	Array content;
	content.push_back(content_item);

	Dictionary result;
	result["content"] = content;
	result["image"] = image_data;
	result["source"] = p_source_label;
	return result;
}

#ifdef WINDOWS_ENABLED
// Capture a native window via PrintWindow — works even if the window is on
// another virtual desktop or occluded by other windows.
Ref<Image> capture_native_window(HWND p_hwnd) {
	if (!p_hwnd || !IsWindow(p_hwnd)) {
		return Ref<Image>();
	}

	RECT rc;
	if (!GetWindowRect(p_hwnd, &rc)) {
		return Ref<Image>();
	}
	int width = rc.right - rc.left;
	int height = rc.bottom - rc.top;
	if (width <= 0 || height <= 0) {
		return Ref<Image>();
	}

	HDC hdc_screen = GetDC(nullptr);
	if (!hdc_screen) {
		return Ref<Image>();
	}

	HDC hdc_mem = CreateCompatibleDC(hdc_screen);
	if (!hdc_mem) {
		ReleaseDC(nullptr, hdc_screen);
		return Ref<Image>();
	}

	HBITMAP hbmp = CreateCompatibleBitmap(hdc_screen, width, height);
	if (!hbmp) {
		DeleteDC(hdc_mem);
		ReleaseDC(nullptr, hdc_screen);
		return Ref<Image>();
	}

	HBITMAP hbmp_old = (HBITMAP)SelectObject(hdc_mem, hbmp);

	// PrintWindow with PW_RENDERFULLCONTENT (3) captures the full window
	// content including client area, even when the window is not on the
	// current virtual desktop or is occluded.
	BOOL ok = PrintWindow(p_hwnd, hdc_mem, PW_RENDERFULLCONTENT);
	if (!ok) {
		// Fallback: try without PW_RENDERFULLCONTENT (some older Windows).
		ok = PrintWindow(p_hwnd, hdc_mem, 0);
	}

	Ref<Image> image;
	if (ok) {
		BITMAPINFO bi = {};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = width;
		bi.bmiHeader.biHeight = -height; // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		Vector<uint8_t> data;
		data.resize(width * height * 4);
		if (GetDIBits(hdc_mem, hbmp, 0, height, data.ptrw(), &bi, DIB_RGB_COLORS)) {
			// Swap B and R channels.
			uint8_t *wr = data.ptrw();
			for (int i = 0; i < width * height; i++) {
				SWAP(wr[i * 4 + 0], wr[i * 4 + 2]);
			}
			image = Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, data);
		}
	}

	SelectObject(hdc_mem, hbmp_old);
	DeleteObject(hbmp);
	DeleteDC(hdc_mem);
	ReleaseDC(nullptr, hdc_screen);
	return image;
}
#endif // WINDOWS_ENABLED

// ---------------------------------------------------------------------------
// Tool 1: Capture an editor viewport (2D scene view or 3D editor viewport).
// Uses Godot's Viewport texture — no screen capture involved.
// ---------------------------------------------------------------------------
class CaptureViewportTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.editor.capture_viewport"; }
	String get_description() const override {
		return "Captures a screenshot of a Godot editor viewport (2D scene view or 3D editor viewport) and returns it as a base64-encoded PNG image. If save_path is provided, saves the image to that absolute file path instead.";
	}
	Dictionary get_input_schema() const override {
		Dictionary target;
		target["type"] = "string";
		Array targets;
		targets.push_back("2d");
		targets.push_back("3d");
		target["enum"] = targets;
		target["default"] = "3d";
		target["description"] = "Which editor viewport to capture: '2d' for the 2D scene view, '3d' for the 3D editor viewport.";

		Dictionary viewport_index;
		viewport_index["type"] = "integer";
		viewport_index["minimum"] = 0;
		viewport_index["maximum"] = 3;
		viewport_index["default"] = 0;
		viewport_index["description"] = "3D viewport index (0-3). Ignored for 2D captures.";

		Dictionary properties;
		properties["target"] = target;
		properties["viewport_index"] = viewport_index;
		properties["save_path"] = save_path_property();
		return object_schema(properties);
	}

	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String target = p_arguments.get("target", String("3d"));
		int viewport_index = CLAMP(int(p_arguments.get("viewport_index", 0)), 0, 3);
		String save_path = p_arguments.get("save_path", String());

		EditorInterface *editor_interface = EditorInterface::get_singleton();
		if (!editor_interface) {
			return error_result("The Godot editor interface is not available.");
		}

		Ref<Image> image;
		String source_label;

		if (target == "2d") {
			SubViewport *viewport_2d = editor_interface->get_editor_viewport_2d();
			if (!viewport_2d) {
				return error_result("The 2D editor viewport is not available.");
			}
			Ref<ViewportTexture> texture = viewport_2d->get_texture();
			if (texture.is_null()) {
				return error_result("Failed to get the 2D viewport texture.");
			}
			image = texture->get_image();
			source_label = "2D editor viewport";
		} else if (target == "3d") {
			SubViewport *viewport_3d = editor_interface->get_editor_viewport_3d(viewport_index);
			if (!viewport_3d) {
				return error_result("The 3D editor viewport at index " + itos(viewport_index) + " is not available.");
			}
			Ref<ViewportTexture> texture = viewport_3d->get_texture();
			if (texture.is_null()) {
				return error_result("Failed to get the 3D viewport texture.");
			}
			image = texture->get_image();
			source_label = "3D editor viewport " + itos(viewport_index);
		} else {
			return error_result("Unknown target. Expected '2d' or '3d'.");
		}

		return image_result(image, source_label, save_path);
	}
};

// ---------------------------------------------------------------------------
// Tool 2: Capture a window.
//   - "editor": captures the main editor window via Godot Viewport texture
//     (no screen capture, works regardless of virtual desktop).
//   - "game": captures the running game window via native window capture
//     (PrintWindow on Windows, screen rect on other platforms).
//   - "screen_rect": captures a screen rectangle via DisplayServer.
// ---------------------------------------------------------------------------
class CaptureWindowTool : public EditorMCPTool {
public:
	StringName get_name() const override { return "godot.editor.capture_window"; }
	String get_description() const override {
		return "Captures a screenshot of a Godot editor window or the running game window. The editor window is captured via Godot's own viewport texture (works even on other virtual desktops). The game window is captured via the OS native window capture. Returns a base64-encoded PNG image, or saves to a file if save_path is provided.";
	}
	Dictionary get_input_schema() const override {
		Dictionary target;
		target["type"] = "string";
		Array targets;
		targets.push_back("editor");
		targets.push_back("game");
		targets.push_back("screen_rect");
		target["enum"] = targets;
		target["default"] = "editor";
		target["description"] = "Which window to capture: 'editor' for the main editor window (via viewport texture), 'game' for the running game window (via native window capture), 'screen_rect' for a custom screen rectangle.";

		Dictionary x;
		x["type"] = "integer";
		x["description"] = "Screen X coordinate (screen_rect only).";

		Dictionary y;
		y["type"] = "integer";
		y["description"] = "Screen Y coordinate (screen_rect only).";

		Dictionary width;
		width["type"] = "integer";
		width["minimum"] = 1;
		width["description"] = "Capture width in screen pixels (screen_rect only).";

		Dictionary height;
		height["type"] = "integer";
		height["minimum"] = 1;
		height["description"] = "Capture height in screen pixels (screen_rect only).";

		Dictionary properties;
		properties["target"] = target;
		properties["x"] = x;
		properties["y"] = y;
		properties["width"] = width;
		properties["height"] = height;
		properties["save_path"] = save_path_property();
		return object_schema(properties);
	}

	Dictionary execute(const Dictionary &p_arguments, EditorMCPContext &p_context) override {
		String target = p_arguments.get("target", String("editor"));
		String save_path = p_arguments.get("save_path", String());

		DisplayServer *ds = DisplayServer::get_singleton();
		if (!ds) {
			return error_result("The display server is not available.");
		}

		if (target == "editor") {
			// Capture the editor main window via Godot's Viewport texture.
			// This does NOT use screen capture — it reads the rendered frame
			// directly from the GPU, so it works even if the editor window
			// is on another virtual desktop or is occluded.
			EditorNode *editor = p_context.get_editor_node();
			if (!editor) {
				return error_result("The Godot editor is not available.");
			}
			Window *window = editor->get_window();
			if (!window) {
				return error_result("The editor window is not available.");
			}
			Ref<ViewportTexture> texture = window->get_texture();
			if (texture.is_null()) {
				return error_result("Failed to get the editor window viewport texture.");
			}
			Ref<Image> image = texture->get_image();
			return image_result(image, "editor window", save_path);
		}

		if (target == "game") {
			// Find the game window (any non-main window with a valid size).
			Vector<DisplayServerEnums::WindowID> windows = ds->get_window_list();
			DisplayServerEnums::WindowID game_window_id = DisplayServerEnums::INVALID_WINDOW_ID;

			for (const DisplayServerEnums::WindowID &wid : windows) {
				if (wid == DisplayServerEnums::MAIN_WINDOW_ID) {
					continue;
				}
				Size2i wsize = ds->window_get_size(wid);
				if (wsize.x > 0 && wsize.y > 0) {
					game_window_id = wid;
					break;
				}
			}

			if (game_window_id == DisplayServerEnums::INVALID_WINDOW_ID) {
				return error_result("No game window is currently running.");
			}

#ifdef WINDOWS_ENABLED
			// Use PrintWindow to capture the native window — works even
			// when the game window is on another virtual desktop.
			int64_t handle = ds->window_get_native_handle(DisplayServerEnums::WINDOW_HANDLE, game_window_id);
			HWND hwnd = (HWND)handle;
			Ref<Image> image = capture_native_window(hwnd);
			return image_result(image, "game window", save_path);
#else
			// Fallback for non-Windows platforms: use screen rect capture.
			Rect2i capture_rect = Rect2i(
					ds->window_get_position_with_decorations(game_window_id),
					ds->window_get_size_with_decorations(game_window_id));
			Ref<Image> image = ds->screen_get_image_rect(capture_rect);
			return image_result(image, "game window", save_path);
#endif
		}

		if (target == "screen_rect") {
			int x = int(p_arguments.get("x", 0));
			int y = int(p_arguments.get("y", 0));
			int width = int(p_arguments.get("width", 0));
			int height = int(p_arguments.get("height", 0));
			if (width <= 0 || height <= 0) {
				return error_result("width and height must be positive integers for screen_rect target.");
			}
			Rect2i capture_rect = Rect2i(Point2i(x, y), Size2i(width, height));
			Ref<Image> image = ds->screen_get_image_rect(capture_rect);
			return image_result(image, "screen rectangle", save_path);
		}

		return error_result("Unknown target. Expected 'editor', 'game', or 'screen_rect'.");
	}
};

} // namespace

void register_editor_mcp_capture_tools(EditorMCPToolRegistry &p_registry) {
	p_registry.register_tool(memnew(CaptureViewportTool));
	p_registry.register_tool(memnew(CaptureWindowTool));
}
