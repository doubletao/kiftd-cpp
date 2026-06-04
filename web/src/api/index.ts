import axios from 'axios'
import { sha256 } from '../utils/sha256'

const api = axios.create({
  baseURL: '/api',
  timeout: 30000
})

// Auth
export async function login(username: string, password: string) {
  const hashed = await sha256(password)
  return api.post('/auth/login', { username, password: hashed })
}

export const logout = () => api.post('/auth/logout')

export const getMe = () => api.get('/auth/me')

// Folders
export const getFolder = (id: string) => api.get(`/folders/${id}`)

export const createFolder = (name: string, parentId: string) =>
  api.post('/folders', { name, parent_id: parentId })

export const renameFolder = (id: string, name: string) =>
  api.put(`/folders/${id}`, { name })

export const deleteFolder = (id: string) => api.delete(`/folders/${id}`)

// Files
export const uploadFile = (folderId: string, file: File, onProgress?: (p: number) => void) => {
  const form = new FormData()
  form.append('folder_id', folderId)
  form.append('file', file)
  return api.post('/files/upload', form, {
    headers: { 'Content-Type': 'multipart/form-data' },
    onUploadProgress: onProgress ? (e) => onProgress(Math.round((e.loaded / (e.total || 1)) * 100)) : undefined
  })
}

export const downloadFile = (id: string, name: string) => {
  const a = document.createElement('a')
  a.href = `/api/files/${id}/download`
  a.download = name
  a.click()
}

export const renameFile = (id: string, name: string) =>
  api.put(`/files/${id}`, { name })

export const deleteFile = (id: string) => api.delete(`/files/${id}`)

export const getPreviewUrl = (id: string) => `/api/files/${id}/preview`

// Shares
export const createShare = (fileId: string, expireAt?: string) =>
  api.post('/shares', { file_id: fileId, expire_at: expireAt || null })

export const getMyShares = () => api.get('/shares/mine')

export const deleteShare = (id: string) => api.delete(`/shares/${id}`)

// Transcode
export const getTranscodeConfig = () => api.get('/config/transcode')

export const probeFile = (id: string) => api.post(`/files/${id}/probe`)

export const submitTranscode = (id: string, preset: string, audioIndex: number, subtitleIndex?: number, externalSubtitlePath?: string) =>
  api.post(`/files/${id}/transcode`, { preset, audio_index: audioIndex, subtitle_index: subtitleIndex ?? -1, external_subtitle_path: externalSubtitlePath || '' })

export const getTranscodeStatus = (id: string) => api.get(`/files/${id}/transcode/status`)

export const deleteTranscode = (id: string) => api.delete(`/files/${id}/transcode`)

export const getTranscodeStreamUrl = (id: string) => `/api/files/${id}/transcode/stream`

export const getTranscodeTasks = () => api.get('/transcode/tasks')

export const reorderTranscodeTask = (fileId: string, direction: number) =>
  api.put('/transcode/tasks/reorder', { file_id: fileId, direction })

// Play History
export const getPlayHistory = () => api.get('/play-history')

export const updatePlayHistory = (folderId: string, fileId: string, position: number, duration: number,
  preset?: string, audioIndex?: number, subtitleIndex?: number, externalSubtitlePath?: string) =>
  api.put('/play-history', {
    folder_id: folderId, file_id: fileId, position, duration,
    ...(preset !== undefined ? { preset, audio_index: audioIndex ?? 0, subtitle_index: subtitleIndex ?? -1, external_subtitle_path: externalSubtitlePath ?? '' } : {})
  })

export const deletePlayHistory = (folderId: string) => api.delete(`/play-history/${folderId}`)
