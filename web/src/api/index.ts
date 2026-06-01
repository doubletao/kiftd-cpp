import axios from 'axios'

const api = axios.create({
  baseURL: '/api',
  timeout: 30000
})

// Auth
export const login = (username: string, password: string) =>
  api.post('/auth/login', { username, password })

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

// Shares
export const createShare = (fileId: string, expireAt?: string) =>
  api.post('/shares', { file_id: fileId, expire_at: expireAt || null })

export const getMyShares = () => api.get('/shares/mine')

export const deleteShare = (id: string) => api.delete(`/shares/${id}`)
