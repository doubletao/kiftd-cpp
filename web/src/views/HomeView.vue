<template>
  <div class="app-layout"
       @dragenter.prevent="onDragEnter"
       @dragover.prevent="onDragOver"
       @dragleave.prevent="onDragLeave"
       @drop.prevent="onDrop">
    <!-- Header -->
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
      </div>
      <div class="header-right">
        <router-link to="/play-history" class="nav-link">Play History</router-link>
        <router-link to="/transcode-tasks" class="nav-link">Transcode Tasks</router-link>
        <router-link to="/shares" class="nav-link">My Shares</router-link>
        <router-link v-if="userStore.isAdmin" to="/admin" class="nav-link">Admin</router-link>
        <router-link to="/settings" class="nav-link">Settings</router-link>
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
        <label class="btn btn-upload">
          Upload Folder
          <input type="file" webkitdirectory @change="handleUpload" style="display:none" />
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
      <span class="upload-status">{{ uploadStatus || uploadProgress + '%' }}</span>
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
              <span v-if="playHistoryRecord && playHistoryRecord.file_id === file.id" class="resume-badge">Resume</span>
            </td>
            <td>{{ formatSize(file.size) }}</td>
            <td>{{ file.creator }}</td>
            <td>{{ file.created_at }}</td>
            <td class="actions-cell">
              <button v-if="canPreview(file.name)" class="btn-sm" @click="openPreview(file.id, file.name)">Preview</button>
              <button v-if="canPlayDirect(file.name)" class="btn-sm btn-play" @click="openVideoPreview(file.id, file.name, false)">Play</button>
              <button v-if="canTranscode(file.name) && getTranscodeStatusForFile(file.id) === 'none'" class="btn-sm btn-play" @click="openVideoPreview(file.id, file.name, false)">Play</button>
              <button v-if="canTranscode(file.name) && getTranscodeStatusForFile(file.id) === 'none'" class="btn-sm btn-transcode" @click="openTranscodeDialog(file)">Transcode</button>
              <button v-if="getTranscodeStatusForFile(file.id) === 'pending'" class="btn-sm btn-cancel" @click="cancelTranscode(file.id)">Queued ✕</button>
              <button v-if="getTranscodeStatusForFile(file.id) === 'transcoding'" class="btn-sm btn-cancel" @click="cancelTranscode(file.id)">Transcoding ✕</button>
              <template v-if="getTranscodeStatusForFile(file.id) === 'done'">
                <button class="btn-sm btn-play" @click="openVideoPreview(file.id, file.name, true)">Play</button>
                <button class="btn-sm btn-danger" @click="removeTranscode(file.id)">Del Cache</button>
              </template>
              <button v-if="getTranscodeStatusForFile(file.id) === 'failed'" class="btn-sm btn-danger" @click="openTranscodeDialog(file)">Failed</button>
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

    <!-- File Preview (images/text/audio only) -->
    <FilePreview
      :visible="showPreview"
      :file-id="previewFileId"
      :file-name="previewFileName"
      :image-files="imageFiles"
      @close="showPreview = false"
      @navigate="onPreviewNavigate"
    />

    <!-- Transcode Dialog -->
    <TranscodeDialog
      :visible="showTranscodeDialog"
      :file-id="transcodeFileId"
      :file-name="transcodeFileName"
      :presets="transcodePresets"
      :profile-name="transcodeProfileName"
      @close="showTranscodeDialog = false"
      @submit="handleTranscodeSubmit"
    />

    <!-- Drop overlay -->
    <div v-if="dragging" class="drop-overlay">
      <div class="drop-hint">
        <span class="drop-icon">&#128229;</span>
        <span>Drop files or folders here to upload</span>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getFolder, createFolder, renameFolder, deleteFolder, uploadFile, downloadFile, renameFile, deleteFile, createShare, getTranscodeConfig, submitTranscode, getTranscodeStatus, deleteTranscode, getPlayHistory, updatePlayHistory } from '../api'
import FilePreview from '../components/FilePreview.vue'
import TranscodeDialog from '../components/TranscodeDialog.vue'

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
const uploadStatus = ref('')

const showPreview = ref(false)
const previewFileId = ref('')
const previewFileName = ref('')

// Transcode
const transcodeEnabled = ref(false)
const transcodePresets = ref<Record<string, { resolution: number; crf: number; preset: string }>>({})
const transcodeProfileName = ref('')
const transcodeStatuses = ref<Record<string, string>>({})  // file_id -> status
const showTranscodeDialog = ref(false)
const transcodeFileId = ref('')
const transcodeFileName = ref('')
let pollTimer: ReturnType<typeof setInterval> | null = null
let configLoaded = false

// Play history
interface PlayHistoryItem {
  folder_id: string; file_id: string; position: number; duration: number
  preset?: string; audio_index?: number; subtitle_index?: number; external_subtitle_path?: string
}
const playHistoryRecord = ref<PlayHistoryItem | null>(null)

const imageExts = ['png','jpg','jpeg','gif','svg','ico','bmp','webp']
const videoExts = ['mkv','avi','flv','wmv','mov','ts','m4v','rmvb','rm','3gp','f4v','vob']
const directPlayExts = ['mp4']

const imageFiles = computed(() =>
  files.value.filter(f => {
    const dot = f.name.lastIndexOf('.')
    return dot >= 0 && imageExts.includes(f.name.substring(dot + 1).toLowerCase())
  })
)

const currentFolderId = ref('root')

// Drag & drop
const dragging = ref(false)
let dragCounter = 0

function onDragEnter() {
  dragCounter++
  dragging.value = true
}

function onDragOver(e: DragEvent) {
  if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy'
}

function onDragLeave() {
  dragCounter--
  if (dragCounter <= 0) {
    dragging.value = false
    dragCounter = 0
  }
}

interface FileEntry {
  file: File
  path: string
}

async function onDrop(e: DragEvent) {
  dragging.value = false
  dragCounter = 0

  const items = e.dataTransfer?.items
  if (!items?.length) return

  const fileList: File[] = []
  const folderEntries: FileEntry[] = []
  let hasFolder = false

  for (let i = 0; i < items.length; i++) {
    const entry = items[i].webkitGetAsEntry?.()
    if (!entry) continue

    if (entry.isFile) {
      fileList.push(items[i].getAsFile()!)
    } else if (entry.isDirectory) {
      hasFolder = true
      const entries = await readEntryRecursive(entry, '')
      folderEntries.push(...entries)
    }
  }

  if (hasFolder && folderEntries.length > 0) {
    await uploadFolderEntries(folderEntries)
  }

  if (fileList.length > 0) {
    await uploadFiles(fileList)
  }
}

function readEntryRecursive(entry: FileSystemEntry, basePath: string): Promise<FileEntry[]> {
  return new Promise((resolve) => {
    if (entry.isFile) {
      (entry as FileSystemFileEntry).file((file) => {
        const path = basePath ? `${basePath}/${entry.name}` : entry.name
        resolve([{ file, path }])
      })
    } else if (entry.isDirectory) {
      const dirReader = (entry as FileSystemDirectoryEntry).createReader()
      const allEntries: FileEntry[] = []
      const dirPath = basePath ? `${basePath}/${entry.name}` : entry.name

      function readBatch() {
        dirReader.readEntries(async (entries) => {
          if (entries.length === 0) {
            resolve(allEntries)
            return
          }
          for (const childEntry of entries) {
            const childEntries = await readEntryRecursive(childEntry, dirPath)
            allEntries.push(...childEntries)
          }
          readBatch()
        })
      }
      readBatch()
    } else {
      resolve([])
    }
  })
}

interface FolderNode {
  name: string
  path: string
  children: Map<string, FolderNode>
  files: FileEntry[]
}

function buildFolderTree(entries: FileEntry[]): FolderNode {
  const root: FolderNode = { name: '', path: '', children: new Map(), files: [] }

  for (const entry of entries) {
    const parts = entry.path.split('/')
    let current = root

    for (let i = 0; i < parts.length - 1; i++) {
      const part = parts[i]
      if (!current.children.has(part)) {
        const childPath = parts.slice(0, i + 1).join('/')
        current.children.set(part, {
          name: part,
          path: childPath,
          children: new Map(),
          files: []
        })
      }
      current = current.children.get(part)!
    }

    current.files.push(entry)
  }

  return root
}

async function createFoldersRecursively(
  node: FolderNode,
  parentId: string,
  folderMap: Map<string, string>
): Promise<void> {
  for (const [name, child] of node.children) {
    try {
      const res = await createFolder(name, parentId)
      const folderId = res.data.id
      folderMap.set(child.path, folderId)
      await createFoldersRecursively(child, folderId, folderMap)
    } catch (e: any) {
      console.error(`Failed to create folder ${name}:`, e)
    }
  }
}

async function uploadFolderEntries(entries: FileEntry[]) {
  if (!entries.length) return

  uploading.value = true
  uploadProgress.value = 0
  uploadStatus.value = 'Creating folders...'

  try {
    const tree = buildFolderTree(entries)
    const folderMap = new Map<string, string>()

    await createFoldersRecursively(tree, currentFolderId.value, folderMap)

    const totalFiles = entries.length
    let uploadedFiles = 0

    for (const entry of entries) {
      const parts = entry.path.split('/')
      const filePath = parts.slice(0, -1).join('/')
      const targetFolderId = filePath ? (folderMap.get(filePath) || currentFolderId.value) : currentFolderId.value

      uploadStatus.value = `Uploading ${entry.file.name}...`
      await uploadFile(targetFolderId, entry.file, (p) => {
        const fileProgress = p / 100
        uploadProgress.value = Math.round(((uploadedFiles + fileProgress) / totalFiles) * 100)
      })
      uploadedFiles++
    }

    uploadStatus.value = 'Upload complete!'
    loadFolder(currentFolderId.value)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Folder upload failed')
  } finally {
    uploading.value = false
    uploadStatus.value = ''
  }
}

async function loadTranscodeConfig() {
  if (configLoaded) return
  configLoaded = true
  try {
    const res = await getTranscodeConfig()
    transcodeEnabled.value = res.data.enabled
    if (res.data.presets) {
      transcodePresets.value = res.data.presets
    }
    if (res.data.profile && res.data.profiles) {
      const p = res.data.profiles[res.data.profile]
      transcodeProfileName.value = p?.name || res.data.profile
    }
  } catch {
    // ignore — transcode not available
  }
}

async function loadFolder(id: string) {
  loading.value = true
  currentFolderId.value = id
  playHistoryRecord.value = null
  try {
    await loadTranscodeConfig()
    const res = await getFolder(id)
    folders.value = res.data.folders
    files.value = res.data.files
    breadcrumb.value = res.data.breadcrumb
    await loadPlayHistory(id)
  } catch (e: any) {
    if (e.response?.status === 401) router.push('/login')
  } finally {
    loading.value = false
  }
}

async function loadPlayHistory(folderId: string) {
  try {
    const res = await getPlayHistory()
    const record = res.data.find((r: any) => r.folder_id === folderId)
    if (record) {
      playHistoryRecord.value = record
    }
  } catch {
    // ignore
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
  const fileList = Array.from(input.files)

  const hasRelativePath = fileList.some(f => (f as any).webkitRelativePath)
  if (hasRelativePath) {
    const entries: FileEntry[] = fileList.map(f => ({
      file: f,
      path: (f as any).webkitRelativePath || f.name
    }))
    await uploadFolderEntries(entries)
  } else {
    await uploadFiles(fileList)
  }
  input.value = ''
}

async function uploadFiles(fileList: File[]) {
  if (!fileList.length) return
  uploading.value = true
  uploadProgress.value = 0
  try {
    for (let i = 0; i < fileList.length; i++) {
      await uploadFile(currentFolderId.value, fileList[i], (p) => {
        uploadProgress.value = p
      })
    }
    loadFolder(currentFolderId.value)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Upload failed')
  } finally {
    uploading.value = false
  }
}

function download(id: string, name: string) {
  downloadFile(id, name)
}

const previewExts = ['png','jpg','jpeg','gif','svg','ico','bmp','webp','mp3','wav','ogg','flac','aac','txt','text','json','js','css','html','htm','xml','md','csv','log','ini','conf','yml','yaml','sh','bat','py','java','c','cpp','h','hpp']

function canPreview(name: string): boolean {
  const dot = name.lastIndexOf('.')
  if (dot < 0) return false
  return previewExts.includes(name.substring(dot + 1).toLowerCase())
}

function openPreview(id: string, name: string) {
  previewFileId.value = id
  previewFileName.value = name
  showPreview.value = true
}

function onPreviewNavigate(id: string, name: string) {
  previewFileId.value = id
  previewFileName.value = name
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

function canTranscode(name: string): boolean {
  if (!transcodeEnabled.value) return false
  const dot = name.lastIndexOf('.')
  if (dot < 0) return false
  return videoExts.includes(name.substring(dot + 1).toLowerCase())
}

function canPlayDirect(name: string): boolean {
  const dot = name.lastIndexOf('.')
  if (dot < 0) return false
  return directPlayExts.includes(name.substring(dot + 1).toLowerCase())
}

function getTranscodeStatusForFile(fileId: string): string {
  return transcodeStatuses.value[fileId] || 'none'
}

function openTranscodeDialog(file: any) {
  transcodeFileId.value = file.id
  transcodeFileName.value = file.name
  showTranscodeDialog.value = true
}

function openVideoPreview(fileId: string, fileName: string, transcoded: boolean) {
  // Navigate to dedicated play page
  let t = ''
  if (playHistoryRecord.value && playHistoryRecord.value.file_id === fileId && playHistoryRecord.value.position > 0) {
    t = `?t=${Math.floor(playHistoryRecord.value.position)}`
  }
  router.push(`/play/${currentFolderId.value}/${fileId}${t}`)
}

async function handleTranscodeSubmit(preset: string, audioIndex: number, subtitleIndex: number, externalSubtitlePath: string) {
  try {
    await submitTranscode(transcodeFileId.value, preset, audioIndex, subtitleIndex, externalSubtitlePath)
    transcodeStatuses.value[transcodeFileId.value] = 'pending'
    showTranscodeDialog.value = false
    startPolling()
    // Save transcode params as folder default for auto-transcode
    try {
      await updatePlayHistory(currentFolderId.value, transcodeFileId.value, 0, 0, preset, audioIndex, subtitleIndex, externalSubtitlePath)
      // Update local record so subsequent auto-transcode uses these params
      playHistoryRecord.value = {
        folder_id: currentFolderId.value,
        file_id: transcodeFileId.value,
        position: 0, duration: 0,
        preset, audio_index: audioIndex, subtitle_index: subtitleIndex,
        external_subtitle_path: externalSubtitlePath
      }
    } catch { /* ignore */ }
  } catch (e: any) {
    alert(e.response?.data?.error || 'Transcode submit failed')
  }
}

async function removeTranscode(fileId: string) {
  try {
    await deleteTranscode(fileId)
    delete transcodeStatuses.value[fileId]
    transcodeStatuses.value = { ...transcodeStatuses.value }
  } catch (e: any) {
    alert(e.response?.data?.error || 'Delete failed')
  }
}

async function cancelTranscode(fileId: string) {
  if (!confirm('Cancel this transcode task?')) return
  try {
    await deleteTranscode(fileId)
    delete transcodeStatuses.value[fileId]
    transcodeStatuses.value = { ...transcodeStatuses.value }
  } catch (e: any) {
    alert(e.response?.data?.error || 'Cancel failed')
  }
}

function startPolling() {
  if (pollTimer) return
  pollTimer = setInterval(async () => {
    const activeIds = Object.entries(transcodeStatuses.value)
      .filter(([_, s]) => s === 'pending' || s === 'transcoding')
      .map(([id]) => id)

    if (activeIds.length === 0) {
      stopPolling()
      return
    }

    for (const id of activeIds) {
      try {
        const res = await getTranscodeStatus(id)
        const status = res.data.status || 'none'
        transcodeStatuses.value[id] = status
      } catch {
        // ignore
      }
    }
    transcodeStatuses.value = { ...transcodeStatuses.value }
  }, 3000)
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// Load transcode statuses when folder changes
watch(() => files.value, async (newFiles) => {
  if (!transcodeEnabled.value || !newFiles.length) return
  for (const f of newFiles) {
    if (videoExts.includes(getExt(f.name))) {
      try {
        const res = await getTranscodeStatus(f.id)
        const status = res.data.status || 'none'
        if (status !== 'none') {
          transcodeStatuses.value[f.id] = status
        }
      } catch { /* ignore */ }
    }
  }
  const hasActive = Object.values(transcodeStatuses.value).some(s => s === 'pending' || s === 'transcoding')
  if (hasActive) startPolling()
}, { immediate: false })

function getExt(name: string): string {
  const dot = name.lastIndexOf('.')
  return dot >= 0 ? name.substring(dot + 1).toLowerCase() : ''
}

const allVideoExts = [...videoExts, ...directPlayExts]
const videoFiles = computed(() =>
  files.value
    .filter(f => {
      const dot = f.name.lastIndexOf('.')
      return dot >= 0 && allVideoExts.includes(f.name.substring(dot + 1).toLowerCase())
    })
    .map(f => ({
      id: f.id,
      name: f.name,
      transcoded: getTranscodeStatusForFile(f.id) === 'done' || directPlayExts.includes(getExt(f.name))
    }))
)

onUnmounted(() => stopPolling())
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
.btn-play {
  background: #e8f5e9;
  color: #2e7d32;
}
.btn-play:hover {
  background: #c8e6c9;
}
.btn-transcode {
  background: #e3f2fd;
  color: #1565c0;
}
.btn-transcode:hover {
  background: #bbdefb;
}
.btn-transcoding {
  background: #fff3e0;
  color: #e65100;
}
.btn-cancel {
  background: #ffebee;
  color: #c62828;
  cursor: pointer;
}
.btn-cancel:hover {
  background: #ffcdd2;
}
.resume-badge {
  display: inline-block;
  padding: 0.1rem 0.4rem;
  background: #667eea;
  color: white;
  border-radius: 4px;
  font-size: 0.7rem;
  font-weight: 600;
  margin-left: 0.4rem;
  vertical-align: middle;
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
  height: 24px;
  background: #eee;
  z-index: 15;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.75rem;
  color: #666;
}
.upload-progress {
  position: absolute;
  left: 0;
  top: 0;
  height: 100%;
  background: #667eea;
  transition: width 0.2s;
  opacity: 0.3;
}
.upload-status {
  position: relative;
  z-index: 1;
}
.drop-overlay {
  position: fixed;
  inset: 0;
  background: rgba(102, 126, 234, 0.15);
  border: 3px dashed #667eea;
  z-index: 90;
  display: flex;
  justify-content: center;
  align-items: center;
  pointer-events: none;
}
.drop-hint {
  background: white;
  padding: 2rem 3rem;
  border-radius: 12px;
  box-shadow: 0 8px 30px rgba(0,0,0,0.15);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0.75rem;
  font-size: 1.1rem;
  color: #667eea;
  font-weight: 500;
}
.drop-icon {
  font-size: 2.5rem;
}
</style>
