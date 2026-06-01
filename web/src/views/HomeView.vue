<template>
  <div class="app-layout">
    <!-- Header -->
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
      </div>
      <div class="header-right">
        <router-link to="/shares" class="nav-link">My Shares</router-link>
        <span class="user">{{ userStore.username }}</span>
        <button class="btn-logout" @click="handleLogout">Logout</button>
      </div>
    </header>

    <!-- Toolbar -->
    <div class="toolbar">
      <div class="breadcrumb">
        <router-link to="/folder/root">ROOT</router-link>
        <template v-for="bc in breadcrumb" :key="bc.id">
          <span class="sep">/</span>
          <router-link :to="`/folder/${bc.id}`">{{ bc.name }}</router-link>
        </template>
      </div>
      <div class="actions">
        <button class="btn" @click="showNewFolder = true">New Folder</button>
        <label class="btn btn-upload">
          Upload
          <input type="file" multiple @change="handleUpload" style="display:none" />
        </label>
      </div>
    </div>

    <!-- New folder dialog -->
    <div v-if="showNewFolder" class="dialog-overlay" @click.self="showNewFolder = false">
      <div class="dialog">
        <h3>New Folder</h3>
        <input v-model="newFolderName" placeholder="Folder name" @keyup.enter="createNewFolder" autofocus />
        <div class="dialog-actions">
          <button class="btn" @click="showNewFolder = false">Cancel</button>
          <button class="btn btn-primary" @click="createNewFolder">Create</button>
        </div>
      </div>
    </div>

    <!-- Upload progress -->
    <div v-if="uploading" class="upload-bar">
      <div class="upload-progress" :style="{ width: uploadProgress + '%' }"></div>
      <span>{{ uploadProgress }}%</span>
    </div>

    <!-- Content -->
    <div class="content">
      <div v-if="loading" class="loading">Loading...</div>

      <table v-else class="file-table">
        <thead>
          <tr>
            <th>Name</th>
            <th>Size</th>
            <th>Creator</th>
            <th>Date</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="folder in folders" :key="folder.id" class="row-folder">
            <td>
              <router-link :to="`/folder/${folder.id}`" class="folder-link">
                <span class="icon">&#128193;</span> {{ folder.name }}
              </router-link>
            </td>
            <td>-</td>
            <td>{{ folder.creator }}</td>
            <td>{{ folder.created_at }}</td>
            <td class="actions-cell">
              <button class="btn-sm" @click="renameFolderPrompt(folder)">Rename</button>
              <button class="btn-sm btn-danger" @click="removeFolder(folder.id)">Delete</button>
            </td>
          </tr>
          <tr v-for="file in files" :key="file.id">
            <td>
              <span class="icon">&#128196;</span> {{ file.name }}
            </td>
            <td>{{ formatSize(file.size) }}</td>
            <td>{{ file.creator }}</td>
            <td>{{ file.created_at }}</td>
            <td class="actions-cell">
              <button class="btn-sm" @click="download(file.id, file.name)">Download</button>
              <button class="btn-sm" @click="shareFile(file.id)">Share</button>
              <button class="btn-sm" @click="renameFilePrompt(file)">Rename</button>
              <button class="btn-sm btn-danger" @click="removeFile(file.id)">Delete</button>
            </td>
          </tr>
          <tr v-if="!folders.length && !files.length">
            <td colspan="5" class="empty">This folder is empty</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getFolder, createFolder, renameFolder, deleteFolder, uploadFile, downloadFile, renameFile, deleteFile, createShare } from '../api'

const route = useRoute()
const router = useRouter()
const userStore = useUserStore()

const loading = ref(true)
const folders = ref<any[]>([])
const files = ref<any[]>([])
const breadcrumb = ref<any[]>([])

const showNewFolder = ref(false)
const newFolderName = ref('')

const uploading = ref(false)
const uploadProgress = ref(0)

const currentFolderId = ref('root')

async function loadFolder(id: string) {
  loading.value = true
  currentFolderId.value = id
  try {
    const res = await getFolder(id)
    folders.value = res.data.folders
    files.value = res.data.files
    breadcrumb.value = res.data.breadcrumb
  } catch (e: any) {
    if (e.response?.status === 401) router.push('/login')
  } finally {
    loading.value = false
  }
}

watch(() => route.params.id, (id) => {
  loadFolder((id as string) || 'root')
}, { immediate: true })

onMounted(() => userStore.fetchUser())

async function handleLogout() {
  await userStore.logout()
  router.push('/login')
}

async function createNewFolder() {
  if (!newFolderName.value.trim()) return
  try {
    await createFolder(newFolderName.value.trim(), currentFolderId.value)
    newFolderName.value = ''
    showNewFolder.value = false
    loadFolder(currentFolderId.value)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Failed')
  }
}

async function handleUpload(e: Event) {
  const input = e.target as HTMLInputElement
  if (!input.files?.length) return
  uploading.value = true
  uploadProgress.value = 0
  try {
    for (let i = 0; i < input.files.length; i++) {
      await uploadFile(currentFolderId.value, input.files[i], (p) => {
        uploadProgress.value = p
      })
    }
    loadFolder(currentFolderId.value)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Upload failed')
  } finally {
    uploading.value = false
    input.value = ''
  }
}

function download(id: string, name: string) {
  downloadFile(id, name)
}

async function shareFile(fileId: string) {
  try {
    const res = await createShare(fileId)
    const shareUrl = `${window.location.origin}/s/${res.data.id}`
    await navigator.clipboard.writeText(shareUrl)
    alert(`Share link copied to clipboard:\n${shareUrl}`)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Share failed')
  }
}

async function renameFolderPrompt(folder: any) {
  const name = prompt('New name:', folder.name)
  if (name && name !== folder.name) {
    await renameFolder(folder.id, name)
    loadFolder(currentFolderId.value)
  }
}

async function removeFolder(id: string) {
  if (!confirm('Delete this folder and all its contents?')) return
  await deleteFolder(id)
  loadFolder(currentFolderId.value)
}

async function renameFilePrompt(file: any) {
  const name = prompt('New name:', file.name)
  if (name && name !== file.name) {
    await renameFile(file.id, name)
    loadFolder(currentFolderId.value)
  }
}

async function removeFile(id: string) {
  if (!confirm('Delete this file?')) return
  await deleteFile(id)
  loadFolder(currentFolderId.value)
}

function formatSize(bytes: number): string {
  if (bytes < 1024) return bytes + ' B'
  if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB'
  if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + ' MB'
  return (bytes / 1073741824).toFixed(1) + ' GB'
}
</script>

<style scoped>
.app-layout {
  min-height: 100vh;
  background: #f5f5f5;
}
.header {
  background: #fff;
  padding: 0 1.5rem;
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  box-shadow: 0 1px 3px rgba(0,0,0,0.1);
  position: sticky;
  top: 0;
  z-index: 10;
}
.logo {
  font-size: 1.3rem;
  font-weight: 700;
  color: #667eea;
  text-decoration: none;
}
.header-right {
  display: flex;
  align-items: center;
  gap: 1rem;
}
.nav-link {
  color: #667eea;
  text-decoration: none;
  font-size: 0.9rem;
}
.user {
  font-size: 0.9rem;
  color: #666;
}
.btn-logout {
  background: none;
  border: 1px solid #ddd;
  padding: 0.3rem 0.8rem;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.85rem;
}
.toolbar {
  padding: 1rem 1.5rem;
  display: flex;
  justify-content: space-between;
  align-items: center;
}
.breadcrumb {
  font-size: 0.9rem;
}
.breadcrumb a {
  color: #667eea;
  text-decoration: none;
}
.sep {
  margin: 0 0.3rem;
  color: #999;
}
.actions {
  display: flex;
  gap: 0.5rem;
}
.btn {
  padding: 0.5rem 1rem;
  background: #667eea;
  color: white;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.9rem;
}
.btn:hover {
  background: #5a6fd6;
}
.btn-upload {
  cursor: pointer;
}
.content {
  padding: 0 1.5rem 2rem;
}
.file-table {
  width: 100%;
  background: white;
  border-radius: 8px;
  overflow: hidden;
  box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  border-collapse: collapse;
}
.file-table th {
  text-align: left;
  padding: 0.75rem 1rem;
  background: #fafafa;
  border-bottom: 1px solid #eee;
  font-size: 0.85rem;
  color: #666;
}
.file-table td {
  padding: 0.65rem 1rem;
  border-bottom: 1px solid #f0f0f0;
  font-size: 0.9rem;
}
.file-table tr:hover {
  background: #f8f9ff;
}
.folder-link {
  color: #333;
  text-decoration: none;
  font-weight: 500;
}
.folder-link:hover {
  color: #667eea;
}
.icon {
  margin-right: 0.3rem;
}
.actions-cell {
  display: flex;
  gap: 0.3rem;
}
.btn-sm {
  padding: 0.25rem 0.6rem;
  background: #f0f0f0;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.8rem;
}
.btn-sm:hover {
  background: #e0e0e0;
}
.btn-danger {
  color: #e74c3c;
}
.btn-danger:hover {
  background: #fde8e8;
}
.empty {
  text-align: center;
  color: #999;
  padding: 2rem !important;
}
.loading {
  text-align: center;
  padding: 3rem;
  color: #999;
}
.dialog-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.4);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 20;
}
.dialog {
  background: white;
  padding: 1.5rem;
  border-radius: 10px;
  width: 360px;
  box-shadow: 0 10px 40px rgba(0,0,0,0.2);
}
.dialog h3 {
  margin-bottom: 1rem;
}
.dialog input {
  width: 100%;
  padding: 0.6rem 0.8rem;
  border: 1px solid #ddd;
  border-radius: 6px;
  font-size: 0.95rem;
  margin-bottom: 1rem;
}
.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 0.5rem;
}
.upload-bar {
  position: fixed;
  top: 56px;
  left: 0;
  right: 0;
  height: 3px;
  background: #eee;
  z-index: 15;
}
.upload-progress {
  height: 100%;
  background: #667eea;
  transition: width 0.2s;
}
</style>
