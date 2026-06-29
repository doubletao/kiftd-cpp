# API Reference

所有 API 路径以 `/api/` 为前缀。认证使用 Cookie Session（HMAC 签名）。

## 认证接口

### 登录

```
POST /api/auth/login
```

请求体：
```json
{ "username": "admin", "password": "<sha256_hex>" }
```

> 密码需先在前端做 SHA256 哈希后再发送，后端会进行第二次 SHA256 + salt 存储。

响应：
```json
{ "ok": true }
```

设置签名 Cookie：`kiftd_user=<username>|<expires>|<signature>`

### 登出

```
POST /api/auth/logout
```

清除 Cookie。

### 检查会话

```
GET /api/auth/me
```

返回当前登录用户信息。未登录返回 401。

---

## 文件夹接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/folders/:id` | 获取文件夹内容（子文件夹+文件列表） |
| POST | `/api/folders` | 创建文件夹 |
| PUT | `/api/folders/:id` | 重命名文件夹 |
| DELETE | `/api/folders/:id` | 删除文件夹（递归） |

根文件夹 ID 为 `root` 或空字符串。

### GET /api/folders/:id

支持分页查询参数：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `page` | int | 1 | 页码 |
| `page_size` | int | 100 | 每页数量（最大 1000） |

响应示例：
```json
{
  "folder": { "id": "xxx", "name": "My Folder" },
  "breadcrumb": [
    { "id": "", "name": "ROOT" },
    { "id": "xxx", "name": "My Folder" }
  ],
  "folders": [ ... ],
  "files": [ ... ],
  "pagination": {
    "page": 1,
    "page_size": 100,
    "total_files": 250,
    "total_pages": 3
  }
}
```

---

## 文件接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/files/upload` | 上传文件（multipart） |
| GET | `/api/files/:id/download` | 下载文件 |
| GET | `/api/files/:id/preview` | 预览文件（文本/图片/视频） |
| PUT | `/api/files/:id` | 重命名文件 |
| DELETE | `/api/files/:id` | 删除文件 |

---

## 分享接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/shares` | 创建分享链接 |
| GET | `/api/shares/mine` | 获取我的分享列表 |
| DELETE | `/api/shares/:id` | 删除分享 |
| GET | `/s/:share_id` | 公开下载（无需认证） |

分享链接支持过期时间设置（ISO 8601 格式），过期后返回 410 Gone。

---

## 转码接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/config/transcode` | 获取转码配置 |
| GET | `/api/files/:id/probe` | 探测文件媒体信息 |
| POST | `/api/files/:id/transcode` | 提交转码任务 |
| GET | `/api/files/:id/transcode/status` | 获取转码状态 |
| DELETE | `/api/files/:id/transcode` | 取消转码 |
| GET | `/api/files/:id/transcode/stream` | 流式播放转码文件 |
| GET | `/api/transcode/tasks` | 获取转码队列 |
| POST | `/api/transcode/tasks/reorder` | 调整队列优先级 |

### 实时转码（HLS）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/files/:id/live/start` | 启动实时转码会话 |
| GET | `/api/files/:id/live.m3u8` | 获取 HLS 播放列表 |
| GET | `/api/files/:id/live/segment/init.mp4` | 获取初始化片段 |
| GET | `/api/files/:id/live/segment/:n.mp4` | 获取视频片段 |

---

## 播放记录接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/play-history` | 获取播放记录列表 |
| PUT | `/api/play-history` | 更新播放进度（body 中传 folder_id, file_id, position, duration） |
| DELETE | `/api/play-history/:folderId` | 删除播放记录 |

---

## 用户管理接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/users` | 获取所有用户（管理员） |
| POST | `/api/users` | 创建用户（管理员） |
| DELETE | `/api/users/:username` | 删除用户（管理员） |
| PUT | `/api/users/:username/password` | 重置密码（管理员） |
| PUT | `/api/users/me/password` | 修改自己的密码 |
| DELETE | `/api/users/me` | 删除自己的账户 |

---

## 错误响应

所有接口在出错时返回统一格式：

```json
{ "error": "错误描述" }
```

常见状态码：
- `200` 成功
- `201` 创建成功
- `206` 部分内容（Range 请求）
- `400` 请求参数错误
- `401` 未认证
- `403` 无权限
- `404` 资源不存在
- `410` 分享链接已过期
- `416` Range 请求超出文件范围
- `429` 登录尝试次数过多
- `500` 服务器内部错误
