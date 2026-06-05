# Architecture

kiftd-cpp 采用前后端分离架构，后端为 C++ HTTP 服务，前端为 Vue 3 SPA。

## 整体架构

```
┌─────────────────────────────────────────────────┐
│                   Browser                        │
│              Vue 3 SPA (Vite)                    │
└──────────────────────┬──────────────────────────┘
                       │ HTTP
┌──────────────────────▼──────────────────────────┐
│              Crow HTTP Server                    │
│         (multithreaded, Asio)                    │
├──────────┬──────────┬──────────┬────────────────┤
│  Auth    │  Folder  │  File    │  Share         │
│Controller│Controller│Controller│ Controller     │
├──────────┴──────────┴──────────┴────────────────┤
│               Core Modules                      │
│  ┌─────────┐ ┌──────────┐ ┌──────────────┐     │
│  │  Auth   │ │Database  │ │  FileStore   │     │
│  │(SHA256) │ │(SQLite3) │ │ (UUID.bin)   │     │
│  └─────────┘ └──────────┘ └──────────────┘     │
│  ┌──────────────────┐ ┌──────────────────┐     │
│  │TranscodeManager  │ │PlayHistory       │     │
│  │(FFmpeg queue)    │ │(progress track)  │     │
│  └──────────────────┘ └──────────────────┘     │
├─────────────────────────────────────────────────┤
│              Config (singleton)                  │
│        data/config.json + defaults               │
└─────────────────────────────────────────────────┘
```

## 后端模块

### Config
- 全局单例，从 `data/config.json` 加载配置
- 支持命令行参数覆盖 (`-p`, `-d`, `-w`)
- 未配置项自动使用默认值

### Database
- SQLite3 封装层，负责 schema 初始化和所有 CRUD 操作
- 表结构：`users`、`folders`、`files`、`shares`、`play_history`
- 文件夹采用树形结构，通过 `parent_id` 关联

### Auth
- 密码双重哈希：前端 SHA256 → 后端 SHA256 + salt
- Cookie-based session（Crow CookieParser）
- 登录失败锁定机制（可配置最大尝试次数和锁定时长）

### FileStore
- 物理文件 I/O，文件以 `UUID.bin` 存储在 `data/files/`
- 文件名与存储路径解耦，支持中文文件名无乱码

### TranscodeManager
- FFmpeg 转码队列管理
- 支持多种硬件加速：CPU (libx264)、NVIDIA NVENC、Intel QSV、AMD AMF
- 可配置并发数、CRF、分辨率、编码 preset
- 转码缓存使用 UUID 路径，避免中文路径问题

### PlayHistory
- 播放进度追踪，记录每个文件的播放位置
- 可配置"已看完"阈值（默认 90%）
- 支持自动播放下一集、跳过片头片尾

## Controllers

| Controller | 职责 |
|-----------|------|
| `auth_controller` | 登录/登出/会话检查 |
| `folder_controller` | 文件夹 CRUD（树形结构） |
| `file_controller` | 文件上传/下载/预览/重命名/删除 |
| `share_controller` | 公开分享链接管理 |
| `transcode_controller` | 转码任务管理（查看/取消/调整队列） |
| `play_history_controller` | 播放记录查询/更新 |

## 前端架构

```
web/src/
├── api/            # Axios 请求封装
├── components/     # 可复用组件
├── router/         # Vue Router 路由定义
├── stores/         # Pinia 状态管理
├── utils/          # 工具函数
└── views/          # 页面视图
    ├── LoginView.vue        # 登录页
    ├── HomeView.vue         # 文件浏览器
    ├── SharesView.vue       # 分享管理
    ├── PlayView.vue         # 视频播放器
    ├── PlayHistoryView.vue  # 播放记录
    └── TranscodeTasksView.vue # 转码任务管理
```

## 数据目录

```
data/
├── config.json       # 服务器配置
├── kiftd.db          # SQLite 数据库（自动创建）
├── files/            # 上传文件存储（UUID.bin）
└── transcode/        # 转码缓存
```

迁移时只需复制整个 `data/` 目录。
