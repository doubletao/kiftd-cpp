# Dependencies

所有 C++ 依赖均已内置在 `third_party/` 目录中，**编译无需联网**。

## C++ 依赖

| 库 | 类型 | 版本 | 用途 |
|----|------|------|------|
| [Crow](https://github.com/CrowCpp/Crow) | Header-only | - | HTTP Web 框架 |
| [Asio](https://think-async.com/Asio/) | Header-only | standalone | 异步 I/O（Crow 底层） |
| [nlohmann/json](https://github.com/nlohmann/json) | Header-only | - | JSON 解析 |
| [SQLite3](https://www.sqlite.org/) | Static lib | amalgamation | 嵌入式数据库 |

### 依赖关系

```
Crow
├── Asio (网络 I/O)
└── nlohmann/json (JSON 处理)

SQLite3 (独立使用)
```

### 编译方式

- **Crow**: Header-only，通过 include path 引入
- **Asio**: Header-only standalone 模式，定义 `ASIO_STANDALONE` 和 `_WIN32_WINNT=0x0601`
- **nlohmann/json**: Header-only，单头文件 `nlohmann/json.hpp`
- **SQLite3**: 编译为静态库，启用 `SQLITE_THREADSAFE=1`

## 前端依赖

| 包 | 版本 | 用途 |
|----|------|------|
| vue | ^3.4.0 | 前端框架 |
| vue-router | ^4.3.0 | 路由管理 |
| pinia | ^2.1.0 | 状态管理 |
| axios | ^1.7.0 | HTTP 客户端 |

### 开发依赖

| 包 | 版本 | 用途 |
|----|------|------|
| vite | ^5.4.0 | 构建工具 |
| @vitejs/plugin-vue | ^5.0.0 | Vue 插件 |
| typescript | ^5.5.0 | 类型检查 |

## 可选依赖

| 工具 | 用途 | 是否必须 |
|------|------|---------|
| FFmpeg | 视频转码 | 否（不配置则禁用转码功能） |

配置 FFmpeg 路径后可启用视频转码，支持 CPU 和多种硬件加速（NVENC/QSV/AMF）。

## 系统依赖

| 平台 | 额外依赖 |
|------|---------|
| Windows | ws2_32 (Winsock，自动链接) |
| Linux | pthread (通常已内置) |
| macOS | 无额外依赖 |
