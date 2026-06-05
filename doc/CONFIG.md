# Configuration

配置文件位于 `data/config.json`，首次运行时可手动创建或使用默认值。

## 完整配置示例

```json
{
  "port": 8081,
  "data_dir": "data",
  "accounts": [
    { "username": "admin", "password": "admin" }
  ],
  "max_attempts": 5,
  "lockout_seconds": 60,
  "ffmpeg_path": "",
  "transcode_dir": "data/transcode",
  "transcode_concurrency": 2,
  "transcode_presets": {
    "fast":   { "resolution": 480, "crf": 30, "preset": "veryfast" },
    "medium": { "resolution": 720, "crf": 27, "preset": "fast" },
    "high":   { "resolution": 0,   "crf": 25, "preset": "fast" }
  },
  "transcode_profile": "cpu",
  "transcode_profiles": {
    "cpu":   { "name": "CPU (libx264)",  "command": "..." },
    "nvenc": { "name": "NVIDIA NVENC",   "command": "..." },
    "qsv":   { "name": "Intel QSV",     "command": "..." },
    "amf":   { "name": "AMD AMF",       "command": "..." }
  },
  "play_progress_threshold": 90,
  "auto_transcode_next": false
}
```

## 配置项说明

### 基础配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `port` | int | 8081 | 服务端口 |
| `data_dir` | string | "data" | 数据目录路径 |
| `accounts` | array | [{"admin","admin"}] | 初始管理员账号列表 |
| `max_attempts` | int | 5 | 登录失败最大尝试次数 |
| `lockout_seconds` | int | 60 | 登录锁定时长（秒） |

### 转码配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `ffmpeg_path` | string | "" | FFmpeg 可执行文件路径，为空则禁用转码 |
| `transcode_dir` | string | "data/transcode" | 转码缓存目录 |
| `transcode_concurrency` | int | 2 | 同时转码任务数 |
| `transcode_profile` | string | "cpu" | 当前使用的转码方案 |
| `transcode_presets` | object | 见上表 | 转码质量预设 |
| `transcode_profiles` | object | 见上表 | 转码命令模板 |

### 转码预设 (transcode_presets)

| 预设 | 分辨率 | CRF | Preset | 说明 |
|------|--------|-----|--------|------|
| fast | 480p | 30 | veryfast | 快速转码，体积小 |
| medium | 720p | 27 | fast | 平衡质量和速度 |
| high | 原始 | 25 | fast | 高质量，保留原始分辨率 |

- `resolution`: 0 表示保持原始分辨率
- `crf`: 越小质量越高，文件越大（范围 0-51）
- `preset`: 编码速度，影响压缩效率

### 硬件加速方案 (transcode_profiles)

| 方案 | 编码器 | 适用硬件 |
|------|--------|---------|
| cpu | libx264 | 通用 CPU |
| nvenc | h264_nvenc | NVIDIA GPU |
| qsv | h264_qsv | Intel 核显/独显 |
| amf | h264_amf | AMD GPU |

### 播放配置

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `play_progress_threshold` | int | 90 | 播放进度达到此百分比视为"已看完" |
| `auto_transcode_next` | bool | false | 看完一集后自动转码下一集 |

## 命令行参数

```
kiftd [options]
  -p <port>       覆盖配置中的端口
  -d <dir>        覆盖数据目录
  -w <dir>        覆盖前端资源目录
  -h              显示帮助
```

命令行参数优先级高于配置文件。
