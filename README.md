# Godot

## 编译

Windows最快满足编译条件

```bash
scons platform=windows target=editor arch=x86_64 d3d12=no angle=no -j4
```

然后即可被拉起

```bash
.\bin\godot.windows.editor.x86_64.console.exe --editor --path "E:/Users/LiuBai/Documents/Godot Projects/gd-test" --mcp --mcp-permission full_access
```

## Godot 内置 MCP Server 使用文档

这是一个改造的Godot, 并内置了可直接网络通讯的MCP服务

### 概述

Godot Editor 内置 MCP (Model Context Protocol) Server，允许 AI 客户端直接连接并操作当前打开的 Godot 编辑器项目，无需安装外部 Node.js 或 Python MCP 桥接程序。

#### 主要特性

- 直接编译进 Godot Editor，无需项目插件
- 通过 HTTP MCP 协议连接，支持 Cursor、Claude Desktop 等 AI 客户端
- 场景操作、文件系统、资源查询、项目运行控制
- Bearer Token 认证
- 三档权限模式（只读 / 开发 / 完全访问）
- 响应自动UTF-8 编码，支持中文

---

### MCP 客户端配置

#### 1. 启动 Godot Editor

```powershell
.\bin\godot.windows.editor.x86_64.exe `
    --editor `
    --path "D:/godot_projects/your_project" `
    --mcp `
    --mcp-port 8765 `
    --mcp-token "your-secret-token" `
    --mcp-permission developer
```

启动参数说明：

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--mcp` | 启用内置 MCP Server | 必须 |
| `--mcp-port` | HTTP 监听端口 | 8765 |
| `--mcp-bind` | 绑定地址 | 127.0.0.1 |
| `--mcp-token` | Bearer Token 认证密钥 | 无（不推荐不设） |
| `--mcp-permission` | 权限模式：restricted / developer / full_access | restricted |

#### 2. 确认服务运行

启动后控制台将显示：

```
Built-in Godot MCP server listening on http://127.0.0.1:8765/mcp
```

检查端口监听状态：

```powershell
Get-NetTCPConnection -LocalPort 8765 -State Listen
```

#### 3. 配置 AI 客户端

##### Cursor (`settings.json`)

编辑 `%APPDATA%/Cursor/settingssetting.json`，添加：

```json
{
  "mcp": {
    "servers": {
      "godot-local": {
        "command": "godot-mcp",
        "url": "http://127.0.0.1:8765/mcp",
        "env": {
          "MCP_TOKEN": "your-secret-token"
        }
      }
    }
  }
}
```

##### Claude Desktop (`claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "godot": {
      "url": "http://127.0.0.1:8765/mcp",
      "headers": {
        "Authorization": "Bearer your-secret-token"
      }
    }
  }
}
```

---

### 权限模式说明

| 模式 | 可用工具 | 适用场景 |
|------|---------|---------|
| `restricted` | 只读工具（场景树获取、文件读取、资源查询） | AI 分析项目结构 |
| `developer` | 写操作（创建节点、修改属性、写文件、创建目录） | AI 辅助开发 |
| `full_access` | 所有工具（含删除文件、运行/停止游戏） | AI 完全控制 |

#### 权限检查逻辑

系统按工具风险等级控制访问：

```
等级 0 (READ)：场景读取、文件读取、资源查询
等级 1 (WRITE)：节点创建、属性修改、文件写入、目录创建
等级 2 (DANGEROUS)：删除文件、运行/停止游戏进程
```

开发者模式自动允许 0+1 级，完全访问模式允许全部。

---

### 全部接口参考

#### 1. 场景操作 (7 个)

##### `godot.editor.get_state`

获取编辑器和项目当前状态。

**参数：**

```json
{}
```

**返回：**

```json
{
  "project_name": "my_project",
  "project_path": "D:/projects/my_project",
  "main_scene": "res://main.tscn",
  "mcp": {
    "listening": true,
    "port": 8765
  }
}
```

---

##### `godot.scene.get_tree`

获取当前打开场景的完整节点树（从根节点开始）。

**参数：**

```json
{
  "max_depth": 10,
  "max_nodes": 500
}
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `max_depth` | int | 10 | 最大递归深度 |
| `max_nodes` | int | 500 | 最大返回节点数 |

**返回：**

```json
{
  "root": {
    "name": "Main",
    "type": "Node3D",
    "path": ".",
    "children": [
      {
        "name": "Player",
        "type": "CharacterBody3D",
        "path": "Player",
        "children": []
      }
    ]
  }
}
```

---

##### `godot.scene.get_node`

获取指定节点的详细信息和编辑器可见属性。

**参数：**

```json
{
  "node_path": "Player/Camera3D",
  "include_properties": true
}
```

**返回：**（属性示例）

```json
{
  "name": "Camera3D",
  "type": "Camera3D",
  "path": "Player/Camera3D",
  "properties": [
    { "name": "position", "type": "Vector3", "value": {"args":[0,1.6,0]} },
    { "name": "fov", "type": "float", "value": 75 }
  ]
}
```

---

##### `godot.scene.create_node`

在指定父节点下创建新节点。

**参数：**

```json
{
  "parent_path": ".",
  "type": "CharacterBody3D",
  "name": "Enemy"
}
```

| 参数 | 类型 | 必须 | 说明 |
|------|------|------|------|
| `parent_path` | string | ✓ | 父节点场景相对路径，`"."` 表示根节点 |
| `type` | string | ✓ | Godot 节点类名 |
| `name` | string | × | 节点名称，默认与 type 相同 |

**返回：**

```json
{
  "created": true,
  "node": {
    "name": "Enemy",
    "type": "CharacterBody3D",
    "path": "Enemy"
  }
}
```

> 操作已加入编辑器撤销历史，`Ctrl+Z` 可撤销。

---

##### `godot.scene.set_property`

修改节点编辑器可见属性。

**参数：**

```json
{
  "node_path": "Player/Camera3D",
  "property": "fov",
  "value": 90
}
```

**返回：**

```json
{
  "updated": true,
  "property": "fov",
  "value": 90
}
```

支持所有 Godot Variant 类型（Vector2/3、Color、Transform3D 等）。

> 操作已加入编辑器撤销历史。

---

##### `godot.scene.delete_node`

删除指定节点。

**参数：**

```json
{
  "node_path": "Enemy"
}
```

**返回：**

```json
{
  "deleted": true
}
```

> 操作已加入编辑器撤销历史。

---

##### `godot.scene.save`

保存当前打开的场景文件。

**参数：**

```json
{}
```

**返回：**

```json
{
  "saved": true,
  "scene_path": "res://main.tscn"
}
```

---

#### 2. 文件系统 (5 个)

##### `godot.filesystem.list`

列出项目目录内容。

**参数：**

```json
{
  "path": "res://scripts"
}
```

**返回：**

```json
{
  "entries": [
    { "name": "player.gd", "is_directory": false },
    { "name": "enemies", "is_directory": true }
  ]
}
```

---

##### `godot.filesystem.read_text`

读取项目内的 UTF-8 文本文件（最大 4 MiB）。

**参数：**

```json
{
  "path": "res://scripts/player.gd"
}
```

**返回：**

```json
{
  "content": "extends CharacterBody3D\n\nfunc _ready():\n\tprint(\"Hello\")"
}
```

---

##### `godot.filesystem.write_text`

写入 UTF-8 文本文件。

**参数：**

```json
{
  "path": "res://scripts/new_script.gd",
  "content": "extends Node\n\nfunc _ready():\n\tpass\n",
  "overwrite": false
}
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `path` | string | ✓ | 文件路径 |
| `content` | string | ✓ | UTF-8 文本内容 |
| `overwrite` | bool | false | 允许覆盖已存在文件 |

**返回：**

```json
{
  "written": true,
  "size_bytes": 42
}
```

---

##### `godot.filesystem.make_directory`

创建目录（递归）。

**参数：**

```json
{
  "path": "res://generated/scripts"
}
```

---

##### `godot.filesystem.delete`

永久删除项目内一个文件。

**参数：**

```json
{
  "path": "res://generated/old_script.gd"
}
```

> ⚠️ 此操作无法通过 `Ctrl+Z` 撤销。

---

#### 3. 资源查询 (1 个)

##### `godot.resource.get_info`

加载并检查项目资源。

**参数：**

```json
{
  "path": "res://materials/metal.tres",
  "include_properties": true
}
```

**返回：**

```json
{
  "path": "res://materials/metal.tres",
  "class": "StandardMaterial3D",
  "cached": true,
  "properties": [
    { "name": "albedo_color", "type": "Color", "value": {"args":[0.8,0.8,0.8,1]} },
    { "name": "metallic", "type": "float", "value": 0.9 }
  ]
}
```

支持的资源类型：
- PackedScene (.tscn, .scn)
- StandardMaterial3D (.tres)
- Mesh (.tres)
- Texture2D (.png, .jpg, .webp 等)
- Script (.gd, .cs)
- 任意 Godot 可加载资源

---

#### 4. 项目运行 (3 个)

##### `godot.project.get_run_state`

获取项目运行状态。

**返回：**

```json
{
  "playing": true,
  "playing_scene": "res://main.tscn",
  "process_id": 12345,
  "main_scene": "res://main.tscn"
}
```

---

##### `godot.project.run`

启动项目。

**参数：**

```json
{
  "mode": "current"
}
```

| mode | 说明 |
|------|------|
| `"main"` | 运行主场景 (`project.godot` 中的 `main_scene`) |
| `"current"` | 运行当前在编辑器中打开的场景 |
| `"custom"` | 运行指定场景 |

**custom 模式额外参数：**

```json
{
  "mode": "custom",
  "scene_path": "res://levels/level_01.tscn"
}
```

**带参数运行：**

```json
{
  "mode": "main",
  "arguments": ["--level", "3", "--debug"]
}
```

**返回：**

```json
{
  "requested": true,
  "mode": "current",
  "playing": true,
  "playing_scene": "res://main.tscn",
  "process_id": 12345
}
```

> 需要 `full_access` 权限。

---

##### `godot.project.stop`

停止当前运行的游戏进程。

**返回：**

```json
{
  "stopped": true,
  "playing": false
}
```

> 需要 `full_access` 权限。

---

#### 5. 截图捕获 (2 个)

##### `godot.editor.capture_viewport`

通过 Godot 的 Viewport 纹理直接捕获编辑器视口截图（2D 场景视图或 3D 编辑器视口）。不走屏幕截图，即使编辑器窗口在其他虚拟桌面或被遮挡也能正常工作。

**参数：**

```json
{
  "target": "3d",
  "viewport_index": 0,
  "save_path": "D:/screenshots/viewport.png"
}
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `target` | string | `"3d"` | 截取目标：`"2d"` 为 2D 场景视图，`"3d"` 为 3D 编辑器视口 |
| `viewport_index` | integer | 0 | 3D 视口索引（0-3），仅 `target` 为 `"3d"` 时有效 |
| `save_path` | string | 无 | 可选绝对文件路径。传入则将图片保存为 PNG 到该路径，不返回 base64 图片块 |

**不传 `save_path` 的返回（多模态图片内容）：**

```json
{
  "content": [
    {
      "type": "image",
      "data": "iVBORw0KGgo...",
      "mimeType": "image/png"
    }
  ],
  "image": {
    "format": "png",
    "base64": "iVBORw0KGgo...",
    "width": 1284,
    "height": 747
  },
  "source": "3D editor viewport 0"
}
```

**传 `save_path` 的返回（文本内容）：**

```json
{
  "content": [
    {
      "type": "text",
      "text": "Image saved to D:/screenshots/viewport.png (1284x747)"
    }
  ],
  "saved": true,
  "path": "D:/screenshots/viewport.png",
  "source": "3D editor viewport 0",
  "width": 1284,
  "height": 747
}
```

---

##### `godot.editor.capture_window`

捕获编辑器窗口或运行中的游戏窗口截图。

- **`"editor"` 模式**：通过 Godot 的 Viewport 纹理获取编辑器主窗口渲染结果，不走屏幕截图，窗口在其他虚拟桌面或被遮挡也能正常工作。
- **`"game"` 模式**：在 Windows 上使用 `PrintWindow` API 捕获运行中的游戏窗口原生内容，即使游戏窗口在其他虚拟桌面也能工作。其他平台回退到屏幕矩形截取。
- **`"screen_rect"` 模式**：截取屏幕上任意矩形区域（通过操作系统屏幕截图）。

**参数：**

```json
{
  "target": "editor",
  "save_path": "D:/screenshots/editor.png"
}
```

| 参数 | 类型 | 默认 | 说明 |
|------|------|------|------|
| `target` | string | `"editor"` | 截取目标：`"editor"` 编辑器窗口、`"game"` 游戏窗口、`"screen_rect"` 屏幕矩形 |
| `x` | integer | 0 | 屏幕 X 坐标（仅 `screen_rect`） |
| `y` | integer | 0 | 屏幕 Y 坐标（仅 `screen_rect`） |
| `width` | integer | 0 | 截取宽度（仅 `screen_rect`） |
| `height` | integer | 0 | 截取高度（仅 `screen_rect`） |
| `save_path` | string | 无 | 可选绝对文件路径。传入则将图片保存为 PNG 到该路径，不返回 base64 图片块 |

**`save_path` 模式返回：**

```json
{
  "content": [
    {
      "type": "text",
      "text": "Image saved to D:/screenshots/editor.png (1858x1141)"
    }
  ],
  "saved": true,
  "path": "D:/screenshots/editor.png",
  "source": "editor window",
  "width": 1858,
  "height": 1141
}
```

> 两个截图工具均为只读操作，`restricted` 权限即可调用。

---

### 调用示例

#### PowerShell

```powershell
$headers = @{
    Authorization = "Bearer your-secret-token"
}

# 获取场景树
$body = @{
    jsonrpc = "2.0"
    id = 1
    method = "tools/call"
    params = @{
        name = "godot.scene.get_tree"
        arguments = @{}
    }
} | ConvertTo-Json -Depth 10

$response = Invoke-RestMethod `
    -Uri "http://127.0.0.1:8765/mcp" `
    -Method Post `
    -ContentType "application/json" `
    -Headers $headers `
    -Body $body

$result = $response.result.content[0].text | ConvertFrom-Json
$result
```

### Python

```python
import requests

MCP_URL = "http://127.0.0.1:8765/mcp"
TOKEN = "your-secret-token"

headers = {
    "Authorization": f"Bearer {TOKEN}",
    "Content-Type": "application/json"
}

def call_tool(name, arguments=None):
    payload = {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": name,
            "arguments": arguments or {}
        }
    }
    response = requests.post(MCP_URL, headers=headers, json=payload)
    response.raise_for_status()
    return response.json()["result"]

# 获取场景树
result = call_tool("godot.scene.get_tree")
print(result)

# 创建节点
result = call_tool("godot.scene.create_node", {
    "parent_path": ".",
    "type": "Sprite3D",
    "name": "MySprite"
})
print(result)
```

#### JavaScript / Node.js

```javascript
const MCP_URL = "http://127.0.0.1:8765/mcp";
const TOKEN = "your-secret-token";

async function callTool(name, arguments = {}) {
  const response = await fetch(MCP_URL, {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${TOKEN}`,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({
      jsonrpc: "2.0",
      id: 1,
      method: "tools/call",
      params: { name, arguments }
    })
  });

  const data = await response.json();
  return data.result;
}

// 使用示例
const tree = await callTool("godot.scene.get_tree", {});
console.log(tree);
```

---

### 通信协议说明

#### HTTP MCP Streamable HTTP

本实现采用 MCP Streamable HTTP 协议：

- 端点：`POST /mcp`
- 内容类型：`application/json`
- 请求体：JSON-RPC 2.0
- 响应体：JSON-RPC 2.0
- 会话：`Mcp-Session-Id` 响应头

#### JSON-RPC 请求结构

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "godot.scene.create_node",
    "arguments": {
      "parent_path": ".",
      "type": "Node3D"
    }
  }
}
```

#### JSON-RPC 响应结构

**成功：**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "{\"path\":\"...\"}"
      }
    ]
  }
}
```

**错误：**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32600,
    "message": "Invalid Request"
  }
}
```

#### MCP Session

1. 客户端调用 `initialize` 方法
2. 服务器返回 `Mcp-Session-Id` 响应头
3. 客户端在后续请求中携带该 Headers：
   ```
   Mcp-Session-Id: <session-id>
   ```
4. 调用 `notifications/cancelled` 或断开连接释放 Session

---

### HTTP Status Codes

| 状态码 | 含义 |
|--------|------|
| 200 | 请求成功 |
| 202 | 通知已接受（无需响应体） |
| 400 | 请求格式错误 |
| 401 | Token 无效或未提供 |
| 404 | 端点不存在或资源未找到 |
| 405 | HTTP 方法不允许 |

---

### 错误处理

#### JSON-RPC 错误代码

| 代码 | 含义 |
|------|------|
| -32700 | JSON 解析失败 |
| -32600 | 无效 JSON-RPC 请求 |
| -32601 | 方法不存在 |
| -32602 | 参数无效 |
| -32603 | 内部错误 |
| -32000 | 服务器内部错误 |

#### 工具执行错误

工具执行失败时，响应 `result.isError` 为 `true`，`content[0].text` 包含错误提示：

```json
{
  "result": {
    "isError": true,
    "content": [
      {
        "type": "text",
        "text": "File does not exist: res://nonexistent.tscn"
      }
    ]
  }
}
```

---

### 服务器版本

当前版本：`0.2.0`

#### 版本历史

- **0.1.0** — 初始 HTTP MCP 实现
- **0.2.0** — 添加 Session 管理、Token 认证、UTF-8 支持、文件系统、资源查询、项目运行工具
- **0.3.0** — 添加截图捕获工具（编辑器视口、编辑器窗口、游戏窗口、屏幕矩形），支持 `save_path` 保存到文件
