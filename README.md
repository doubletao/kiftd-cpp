# kiftd-cpp

**个人轻量网盘 · 在线播放优化**

基于 C++17 + SQLite 的轻量文件服务器，灵感来自 [kiftd](https://github.com/KOHGYLW/kiftd)。在原版网盘功能基础上，大幅优化了在线视频播放体验，更适合个人小型网盘、家庭影音库等场景。

> 📖 [English](README.en.md)

## 为什么选择 kiftd-cpp

### 对比 Jellyfin — 低配置也能流畅看剧

Jellyfin 采用**实时转码**方案，播放时边转边播，对 CPU/GPU 性能要求较高，低配服务器（NAS、旧电脑）很容易卡顿。

kiftd-cpp 采用**转码缓存**方案：提前将视频转码为浏览器友好的 MP4 (H.264)，播放时直接读取缓存文件，**零实时计算开销**。

| | Jellyfin (实时转码) | kiftd-cpp (转码缓存) |
|--|---|---|
| 播放时 CPU 占用 | 高（持续编码） | 几乎为零 |
| 低配设备体验 | 卡顿、缓冲 | 流畅播放 |
| 硬盘空间 | 不占用额外空间 | 缓存占用空间（看完可删） |
| 硬件加速 | 需要 GPU 才能流畅 | 可选，无 GPU 也能用（后台慢慢转） |

- **空间换时间** — 后台预转码，播放时零开销，低配平台也能顺畅播放
- **缓存可控** — 看完随手删，不会长期占用空间
- **自动缓存下一集** — 播放当前集时，下一集自动开始转码，切换无缝衔接
- **硬件加速可选** — 支持 CPU / NVIDIA NVENC / Intel QSV / AMD AMF 四种方案，有独显时转码更快，无 GPU 也完全可用

### 对比 kiftd — 在线播放更好用

kiftd 是优秀的网盘工具，但在线播放功能较为基础。kiftd-cpp 在保留完整网盘功能的同时，针对"看剧"场景做了深度优化：

- **播放历史** — 同一文件夹下自动记录每集播放位置，刷剧随时中断随时续看
- **上一集 / 下一集** — 播放器内一键切换，不用返回文件列表
- **跳过片头片尾** — 可配置跳过时长，每集自动跳过，省去手动拖进度条
- **独立播放页面** — 沉浸式播放体验，支持键盘快捷键（空格暂停、方向键快进快退、F 全屏）
- **转码任务管理** — 可视化查看转码队列，支持取消、调整优先级

### 部署极简

- **C++ 原生** — 不依赖 Java 运行时，单个 exe 直接运行
- **唯一前置** — Windows 只需安装 [VC++ 可再发行组件包](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist)（大多数电脑已有）
- **SQLite 数据库** — 零配置，数据就是一个文件，备份迁移就是复制文件夹
- **无外部依赖** — 不需要 Redis、MySQL、Docker，开箱即用

## 功能一览

- 文件管理 — 上传/下载/文件夹树/重命名/删除/预览（文本+图片）
- 视频播放 — 内置播放器，键盘快捷键，上下集切换，播放进度追踪
- 视频转码 — FFmpeg 集成，四种转码方案 (CPU/NVENC/QSV/AMF)，队列管理，可配置质量预设
- 播放历史 — 自动记录进度，自动播放下一集，跳过片头片尾
- 文件分享 — 生成公开下载链接，无需登录即可下载
- 安全机制 — 双重 SHA256 密码哈希，登录失败锁定，Cookie Session

## 截图

### 文件浏览器

![文件浏览器](doc/sample_img/main.png)

### 视频播放器

![视频播放器](doc/sample_img/player.png)

### 播放历史

![播放历史](doc/sample_img/playhistory.png)

### 分享管理

![分享管理](doc/sample_img/sharelist.png)

### 视频转码

![视频转码](doc/sample_img/trandcoding.png)

### 转码任务管理

![转码任务管理](doc/sample_img/transcodetask.png)

## 快速开始

### 环境要求

- CMake 3.20+
- C++17 编译器 (MSVC 2019+ / GCC 9+ / Clang 10+)
- Node.js 18+（构建前端）
- FFmpeg（可选，用于视频转码）

### 编译

```bash
# 后端（无需联网，依赖已在 third_party/ 中）
cmake -B build -S .
cmake --build build --config Release

# 前端
cd web
npm install
npm run build
cd ..
```

Windows 一键构建：

```bash
build.bat          # 编译
package.bat         # 编译 + 打包 zip 发行包
```

### 运行

```bash
# 默认端口 8080
./build/kiftd

# 自定义选项
./build/kiftd -p 9090 -d /path/to/data -w /path/to/web/dist
```

打开 http://localhost:8080，默认账号：`admin` / `admin`

## 配置

详见 [doc/CONFIG.md](doc/CONFIG.md)，关键配置项在 `data/config.json` 中：

```json
{
  "port": 8081,
  "accounts": [{ "username": "admin", "password": "admin" }],
  "ffmpeg_path": "",
  "transcode_concurrency": 2,
  "transcode_profile": "cpu",
  "auto_transcode_next": false,
  "play_progress_threshold": 90
}
```

## 文档

| 文档 | 说明 |
|------|------|
| [架构设计](doc/ARCHITECTURE.md) | 系统架构与模块设计 |
| [编译指南](doc/BUILD.md) | 详细编译说明 |
| [配置说明](doc/CONFIG.md) | 全部配置项 |
| [依赖清单](doc/DEPENDENCIES.md) | 三方库列表 |
| [API 文档](doc/API.md) | REST API 接口 |

## 技术栈

**后端：** C++17 / Crow / SQLite3 / Asio / nlohmann-json

**前端：** Vue 3 / TypeScript / Vite / Vue Router / Pinia / Axios

## API 概览

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/login` | 登录 |
| GET | `/api/folders/:id` | 文件夹内容 |
| POST | `/api/files/upload` | 上传文件 |
| GET | `/api/files/:id/download` | 下载文件 |
| POST | `/api/shares` | 创建分享链接 |
| GET | `/s/:share_id` | 公开下载（无需认证） |
| POST | `/api/transcode/:fileId` | 提交转码任务 |

完整 API 文档：[doc/API.md](doc/API.md)

## 数据迁移

复制整个 `data/` 目录即可：

```bash
cp -r data/ /new/server/data/
./build/kiftd -d /new/server/data
```

## License

MIT
