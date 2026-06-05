# API Reference

所有 API 路径以 `/api/` 为前缀。认证使用 Cookie Session。

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

### 登出

```
POST /api/auth/logout
```

需要认证。

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

根文件夹 ID 为空字符串 `""`。

---

## 文件接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/files/upload` | 上传文件（multipart） |
| GET | `/api/files/:id/download` | 下载文件 |
| GET | `/api/files/:id/preview` | 预览文件（文本/图片） |
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

---

## 转码接口

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/transcode/:fileId` | 提交转码任务 |
| GET | `/api/transcode/tasks` | 获取转码队列 |
| DELETE | `/api/transcode/tasks/:taskId` | 取消转码任务 |
| PUT | `/api/transcode/tasks/:taskId/priority` | 调整队列优先级 |

---

## 播放记录接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/play-history` | 获取播放记录列表 |
| PUT | `/api/play-history/:fileId` | 更新播放进度 |
| DELETE | `/api/play-history/:fileId` | 删除播放记录 |

---

## 错误响应

所有接口在出错时返回统一格式：

```json
{ "error": "错误描述" }
```

常见状态码：
- `200` 成功
- `400` 请求参数错误
- `401` 未认证
- `403` 无权限
- `404` 资源不存在
- `500` 服务器内部错误
