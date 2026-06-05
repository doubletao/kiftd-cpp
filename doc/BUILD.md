# Build Guide

## 环境要求

| 工具 | 版本 | 用途 |
|------|------|------|
| CMake | 3.20+ | 构建系统 |
| C++ 编译器 | MSVC 2019+ / GCC 9+ / Clang 10+ | 后端编译 |
| Node.js | 18+ | 前端构建 |
| Git | 任意 | 克隆仓库 |

> C++ 依赖已全部内置在 `third_party/` 中，编译后端**无需联网下载**。

## 一键构建 (Windows)

```bash
build.bat          # 构建前端+后端，输出到 bin/
package.bat        # 构建+打包 zip 发行包
```

## 手动构建

### 后端

```bash
cmake -B build -S .
cmake --build build --config Release
```

产物：`build/Release/kiftd.exe`（MSVC）或 `build/kiftd`（GCC/Clang）

### 前端

```bash
cd web
npm install        # 首次构建需安装依赖
npm run build
```

产物：`web/dist/`

### 开发模式

```bash
# 前端热重载
cd web
npm run dev        # 默认 http://localhost:5173

# 后端
cmake -B build -S .
cmake --build build
./build/kiftd -w web/dist
```

## 编译说明

- MSVC 需要 `/bigobj` 标志（已在 CMakeLists.txt 中配置）
- SQLite3 使用线程安全模式编译 (`SQLITE_THREADSAFE=1`)
- Asio 使用 standalone 模式，不依赖 Boost
- Windows 平台额外链接 `ws2_32`（Winsock）

## 发布打包

`package.bat` 会自动完成：
1. 构建前端 → `web/dist/`
2. 构建后端 → `build/Release/kiftd.exe`
3. 组装发布目录 → `kiftd-cpp-release/`
4. 打包为 `kiftd-cpp-v1.0.0-win64.zip`

发布包内容：
```
kiftd-cpp-release/
├── kiftd.exe
├── config.json
├── README.txt
├── data/
│   └── files/
└── web/
    └── dist/
```
