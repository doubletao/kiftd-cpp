<template>
  <div class="play-page">
    <!-- Top bar -->
    <div class="top-bar">
      <button class="btn-back" @click="goBack">&larr; Back</button>
      <span class="file-name">{{ currentFileName }}</span>
      <div class="episode-nav">
        <button class="btn-ep" :disabled="currentIndex <= 0" @click="switchEpisode(-1)">&lsaquo; Prev</button>
        <span class="ep-counter">{{ currentIndex + 1 }} / {{ videoFiles.length }}</span>
        <button class="btn-ep" :disabled="currentIndex >= videoFiles.length - 1" @click="switchEpisode(1)">Next &rsaquo;</button>
      </div>
    </div>

    <!-- Video player -->
    <div class="video-container">
      <video ref="videoRef" :src="videoUrl" controls autoplay
             @loadedmetadata="onLoadedMetadata"
             @timeupdate="onTimeUpdate"
             @ended="onEnded"></video>
    </div>

    <!-- Bottom bar -->
    <div class="bottom-bar">
      <div class="skip-controls">
        <span class="skip-label">Skip Intro:</span>
        <button class="skip-btn" @click="adjustSkip('intro', -5)">-5s</button>
        <span class="skip-value">{{ skipIntroVal }}s</span>
        <button class="skip-btn" @click="adjustSkip('intro', 5)">+5s</button>
        <span class="skip-sep">|</span>
        <span class="skip-label">Skip Outro:</span>
        <button class="skip-btn" @click="adjustSkip('outro', -5)">-5s</button>
        <span class="skip-value">{{ skipOutroVal }}s</span>
        <button class="skip-btn" @click="adjustSkip('outro', 5)">+5s</button>
      </div>
      <div class="progress-info" v-if="videoRef">
        <span>{{ formatTime(currentTime) }} / {{ formatTime(duration) }}</span>
      </div>
    </div>

    <!-- Transcode confirm dialog -->
    <div v-if="showTranscodeConfirm" class="modal-overlay" @click.self="showTranscodeConfirm = false">
      <div class="modal-box">
        <p>{{ transcodeConfirmMsg }}</p>
        <div class="modal-actions">
          <button v-if="transcodeConfirmCanSubmit" class="btn-primary" @click="confirmTranscode">Transcode Now</button>
          <button class="btn-secondary" @click="showTranscodeConfirm = false">Later</button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { getFolder, getTranscodeConfig, getTranscodeStatus, submitTranscode, getPlayHistory, updatePlayHistory, deletePlayHistory, getTranscodeStreamUrl, getPreviewUrl } from '../api'

const route = useRoute()
const router = useRouter()

const videoExts = ['mkv','avi','flv','wmv','mov','ts','m4v','rmvb','rm','3gp','f4v','vob']
const directPlayExts = ['mp4']
const allVideoExts = [...videoExts, ...directPlayExts]

interface VideoFile {
  id: string
  name: string
  transcoded: boolean
}

const folderId = computed(() => route.params.folderId as string)
const fileId = computed(() => route.params.fileId as string)

const videoRef = ref<HTMLVideoElement | null>(null)
const videoFiles = ref<VideoFile[]>([])
const transcodeStatuses = ref<Record<string, string>>({})
const playProgressThreshold = ref(90)
const autoTranscodeNext = ref(false)
const transcodeEnabled = ref(false)

// Skip controls (session only)
const skipIntroVal = ref(0)
const skipOutroVal = ref(0)

// Playback state
const currentTime = ref(0)
const duration = ref(0)
let lastProgressEmit = 0

// Transcode confirm dialog
const showTranscodeConfirm = ref(false)
const transcodeConfirmMsg = ref('')
const transcodeConfirmCanSubmit = ref(false)
let pendingSwitchIndex = -1

// Play history
interface PlayHistoryItem {
  folder_id: string; file_id: string; position: number; duration: number
  preset?: string; audio_index?: number; subtitle_index?: number; external_subtitle_path?: string
}
const playHistoryRecord = ref<PlayHistoryItem | null>(null)

const currentIndex = computed(() => videoFiles.value.findIndex(f => f.id === fileId.value))
const currentFileName = computed(() => {
  const f = videoFiles.value.find(f => f.id === fileId.value)
  return f ? f.name : ''
})

function getExt(name: string): string {
  const dot = name.lastIndexOf('.')
  return dot >= 0 ? name.substring(dot + 1).toLowerCase() : ''
}

const videoUrl = computed(() => {
  const f = videoFiles.value.find(f => f.id === fileId.value)
  if (!f) return ''
  return f.transcoded ? getTranscodeStreamUrl(f.id) : getPreviewUrl(f.id)
})

function formatTime(seconds: number): string {
  if (!seconds || !isFinite(seconds)) return '0:00'
  const m = Math.floor(seconds / 60)
  const s = Math.floor(seconds % 60)
  return `${m}:${s.toString().padStart(2, '0')}`
}

async function loadFolder() {
  try {
    // Load transcode config
    const cfgRes = await getTranscodeConfig()
    const cfg = cfgRes.data
    transcodeEnabled.value = cfg.enabled || false
    playProgressThreshold.value = cfg.play_progress_threshold || 90
    autoTranscodeNext.value = cfg.auto_transcode_next || false

    // Load folder files
    const res = await getFolder(folderId.value)
    const files: { id: string; name: string }[] = res.data.files

    // Filter video files and get transcode status
    const vids: VideoFile[] = []
    for (const f of files) {
      const ext = getExt(f.name)
      if (!allVideoExts.includes(ext)) continue
      let transcoded = directPlayExts.includes(ext)
      if (!transcoded && transcodeEnabled.value) {
        try {
          const sRes = await getTranscodeStatus(f.id)
          const status = sRes.data.status || 'none'
          transcodeStatuses.value[f.id] = status
          transcoded = status === 'done'
        } catch { /* ignore */ }
      }
      vids.push({ id: f.id, name: f.name, transcoded })
    }
    // Sort by name
    vids.sort((a, b) => a.name.localeCompare(b.name))
    videoFiles.value = vids

    // Load play history
    try {
      const hRes = await getPlayHistory()
      const record = hRes.data.find((r: any) => r.folder_id === folderId.value)
      if (record) playHistoryRecord.value = record
    } catch { /* ignore */ }

    // Auto-transcode next episode
    autoTranscodeNextEpisode(fileId.value)
  } catch (e: any) {
    if (e.response?.status === 401) router.push('/login')
  }
}

function onLoadedMetadata() {
  if (!videoRef.value) return
  // Resume from query param or play history
  const queryTime = parseFloat(route.query.t as string)
  if (queryTime > 0) {
    videoRef.value.currentTime = queryTime
  } else if (playHistoryRecord.value && playHistoryRecord.value.file_id === fileId.value && playHistoryRecord.value.position > 0) {
    videoRef.value.currentTime = playHistoryRecord.value.position
  }
  // Skip intro
  if (skipIntroVal.value > 0 && videoRef.value.currentTime < skipIntroVal.value) {
    videoRef.value.currentTime = skipIntroVal.value
  }
}

function onTimeUpdate() {
  if (!videoRef.value) return
  currentTime.value = videoRef.value.currentTime
  duration.value = videoRef.value.duration

  // Save progress every 5s (skip if duration is not yet available)
  const now = Date.now()
  if (now - lastProgressEmit >= 5000 && videoRef.value.duration > 0) {
    lastProgressEmit = now
    updatePlayHistory(folderId.value, fileId.value, videoRef.value.currentTime, videoRef.value.duration).catch(() => {})
  }

  // Skip outro
  if (skipOutroVal.value > 0 && videoRef.value.duration > 0 && (videoRef.value.duration - videoRef.value.currentTime) <= skipOutroVal.value) {
    onEnded()
  }
}

function onEnded() {
  // Check if watched enough
  const pct = duration.value > 0 ? (currentTime.value / duration.value) * 100 : 100
  if (pct >= playProgressThreshold.value) {
    // Mark as complete
    updatePlayHistory(folderId.value, fileId.value, 1, 1).catch(() => {})

    // Last episode? Delete history
    if (currentIndex.value >= videoFiles.value.length - 1) {
      deletePlayHistory(folderId.value).catch(() => {})
      return
    }

    // Switch to next
    switchEpisode(1)
  }
}

function switchEpisode(direction: number) {
  const newIdx = currentIndex.value + direction
  if (newIdx < 0 || newIdx >= videoFiles.value.length) return

  const target = videoFiles.value[newIdx]

  // Already transcoded or direct play? Navigate
  if (target.transcoded) {
    navigateToFile(target.id)
    return
  }

  // Check transcode status
  const status = transcodeStatuses.value[target.id] || 'none'
  if (status === 'transcoding' || status === 'pending') {
    transcodeConfirmMsg.value = `"${target.name}" is being transcoded, please wait.`
    transcodeConfirmCanSubmit.value = false
    showTranscodeConfirm.value = true
    return
  }

  // Not transcoded - ask user
  pendingSwitchIndex = newIdx
  transcodeConfirmMsg.value = `"${target.name}" has not been transcoded yet. Transcode now?`
  transcodeConfirmCanSubmit.value = true
  showTranscodeConfirm.value = true
}

async function confirmTranscode() {
  showTranscodeConfirm.value = false
  if (pendingSwitchIndex < 0) return

  const target = videoFiles.value[pendingSwitchIndex]
  if (!target) return

  // Use saved params from play history
  const rec = playHistoryRecord.value
  const preset = rec?.preset || 'fast'
  const audioIdx = rec?.audio_index ?? 0
  const subtitleIdx = rec?.subtitle_index ?? -1
  const extSubPath = rec?.external_subtitle_path || ''

  try {
    await submitTranscode(target.id, preset, audioIdx, subtitleIdx, extSubPath)
    transcodeStatuses.value[target.id] = 'pending'
  } catch { /* ignore */ }

  pendingSwitchIndex = -1
}

function navigateToFile(id: string) {
  // Save current progress before switching
  if (videoRef.value && videoRef.value.duration > 0) {
    updatePlayHistory(folderId.value, fileId.value, videoRef.value.currentTime, videoRef.value.duration).catch(() => {})
  }
  router.push(`/play/${folderId.value}/${id}`)
}

async function autoTranscodeNextEpisode(currentFileId: string) {
  if (!autoTranscodeNext.value || !transcodeEnabled.value) return
  const idx = videoFiles.value.findIndex(f => f.id === currentFileId)
  if (idx < 0 || idx >= videoFiles.value.length - 1) return
  const next = videoFiles.value[idx + 1]
  if (!next || directPlayExts.includes(getExt(next.name))) return
  const status = transcodeStatuses.value[next.id] || 'none'
  if (status !== 'none') return

  const rec = playHistoryRecord.value
  const preset = rec?.preset || 'fast'
  const audioIdx = rec?.audio_index ?? 0
  const subtitleIdx = rec?.subtitle_index ?? -1
  const extSubPath = rec?.external_subtitle_path || ''
  try {
    await submitTranscode(next.id, preset, audioIdx, subtitleIdx, extSubPath)
    transcodeStatuses.value[next.id] = 'pending'
  } catch { /* ignore */ }
}

function adjustSkip(type: 'intro' | 'outro', delta: number) {
  if (type === 'intro') {
    skipIntroVal.value = Math.max(0, skipIntroVal.value + delta)
  } else {
    skipOutroVal.value = Math.max(0, skipOutroVal.value + delta)
  }
}

function goBack() {
  router.push(`/folder/${folderId.value}`)
}

function onKeyDown(e: KeyboardEvent) {
  if (!videoRef.value) return
  switch (e.key) {
    case 'ArrowLeft':
      videoRef.value.currentTime = Math.max(0, videoRef.value.currentTime - 5)
      e.preventDefault()
      break
    case 'ArrowRight':
      videoRef.value.currentTime = Math.min(videoRef.value.duration, videoRef.value.currentTime + 5)
      e.preventDefault()
      break
    case 'ArrowUp':
      videoRef.value.volume = Math.min(1, videoRef.value.volume + 0.1)
      e.preventDefault()
      break
    case 'ArrowDown':
      videoRef.value.volume = Math.max(0, videoRef.value.volume - 0.1)
      e.preventDefault()
      break
    case ' ':
      if (videoRef.value.paused) videoRef.value.play()
      else videoRef.value.pause()
      e.preventDefault()
      break
    case 'Escape':
      goBack()
      break
  }
}

onMounted(() => {
  loadFolder()
  document.addEventListener('keydown', onKeyDown)
})

onUnmounted(() => {
  document.removeEventListener('keydown', onKeyDown)
  // Save progress on leave
  if (videoRef.value && videoRef.value.duration > 0) {
    updatePlayHistory(folderId.value, fileId.value, videoRef.value.currentTime, videoRef.value.duration).catch(() => {})
  }
})

// Reload when navigating between episodes
watch(() => route.params.fileId, (newId, oldId) => {
  if (newId && newId !== oldId) {
    lastProgressEmit = 0
    currentTime.value = 0
    duration.value = 0
    loadFolder()
  }
})
</script>

<style scoped>
.play-page {
  min-height: 100vh;
  background: #000;
  color: #fff;
  display: flex;
  flex-direction: column;
}
.top-bar {
  display: flex;
  align-items: center;
  padding: 0.5rem 1rem;
  background: #111;
  gap: 1rem;
  flex-shrink: 0;
}
.btn-back {
  background: none;
  border: 1px solid #555;
  color: #fff;
  padding: 0.3rem 0.8rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}
.btn-back:hover { background: #333; }
.file-name {
  flex: 1;
  font-size: 0.95rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.episode-nav {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}
.btn-ep {
  background: none;
  border: 1px solid #555;
  color: #fff;
  padding: 0.3rem 0.8rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}
.btn-ep:hover:not(:disabled) { background: #333; }
.btn-ep:disabled { opacity: 0.4; cursor: default; }
.ep-counter {
  font-size: 0.85rem;
  color: #aaa;
  min-width: 50px;
  text-align: center;
}
.video-container {
  flex: 1;
  display: flex;
  justify-content: center;
  align-items: center;
  background: #000;
  min-height: 0;
}
.video-container video {
  max-width: 100%;
  max-height: 100%;
  width: 100%;
}
.bottom-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.5rem 1rem;
  background: #111;
  flex-shrink: 0;
}
.skip-controls {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  font-size: 0.85rem;
}
.skip-label {
  font-weight: 500;
  margin-left: 0.3rem;
}
.skip-btn {
  background: #333;
  border: 1px solid #555;
  border-radius: 4px;
  padding: 0.15rem 0.5rem;
  cursor: pointer;
  font-size: 0.8rem;
  color: #fff;
}
.skip-btn:hover { background: #444; }
.skip-value {
  min-width: 2.5rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
}
.skip-sep {
  margin: 0 0.3rem;
  color: #555;
}
.progress-info {
  font-size: 0.85rem;
  color: #aaa;
  font-variant-numeric: tabular-nums;
}
.modal-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0,0,0,0.6);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 100;
}
.modal-box {
  background: #222;
  border-radius: 8px;
  padding: 1.5rem;
  min-width: 300px;
  max-width: 400px;
}
.modal-box p {
  margin: 0 0 1rem;
  font-size: 0.95rem;
  line-height: 1.5;
}
.modal-actions {
  display: flex;
  gap: 0.5rem;
  justify-content: flex-end;
}
.btn-primary {
  background: #4a9eff;
  border: none;
  color: #fff;
  padding: 0.4rem 1rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}
.btn-primary:hover { background: #3a8eef; }
.btn-secondary {
  background: #444;
  border: none;
  color: #fff;
  padding: 0.4rem 1rem;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
}
.btn-secondary:hover { background: #555; }
</style>
