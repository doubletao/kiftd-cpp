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

    <!-- Choice overlay for non-MP4 without cache -->
    <div v-if="showChoiceOverlay" class="choice-overlay">
      <div class="choice-box">
        <div class="choice-file-name">{{ currentFileName }}</div>
        <p class="choice-hint">This video has not been transcoded. Choose a play mode:</p>
        <div class="choice-actions">
          <button class="btn-choice btn-play" @click="startLivePlay">
            &#9654; Play
          </button>
          <button class="btn-choice btn-cache" @click="startCache">
            &#9881; Cache
          </button>
        </div>
        <div class="choice-desc">
          <div class="desc-item"><strong>Play</strong>: Stream directly, start watching immediately</div>
          <div class="desc-item"><strong>Cache</strong>: Transcode to local cache for smoother playback</div>
        </div>
      </div>
    </div>

    <!-- Video player -->
    <div v-show="!showChoiceOverlay" class="video-container">
      <video ref="videoRef" :src="videoUrl" controls autoplay
             @loadedmetadata="onLoadedMetadata"
             @timeupdate="onTimeUpdate"
             @ended="onEnded"></video>
    </div>

    <!-- Bottom bar -->
    <div v-show="!showChoiceOverlay" class="bottom-bar">
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
        <span v-if="playMode === 'live'" class="live-tag">LIVE</span>
      </div>
    </div>

    <!-- Transcode confirm dialog (for Cache action) -->
    <TranscodeDialog
      :visible="showTranscodeDialog"
      :file-id="transcodeDialogFileId"
      :file-name="transcodeDialogFileName"
      :presets="transcodePresets"
      :profile-name="transcodeProfileName"
      @close="showTranscodeDialog = false"
      @submit="handleTranscodeSubmit"
    />
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import Hls from 'hls.js'
import { getFolder, getTranscodeConfig, getTranscodeStatus, submitTranscode, getPlayHistory, updatePlayHistory, deletePlayHistory, getTranscodeStreamUrl, getPreviewUrl, getLivePlaylistUrl } from '../api'
import TranscodeDialog from '../components/TranscodeDialog.vue'

const route = useRoute()
const router = useRouter()

const videoExts = ['mkv','avi','flv','wmv','mov','ts','m4v','rmvb','rm','3gp','f4v','vob']
const directPlayExts = ['mp4']
const allVideoExts = [...videoExts, ...directPlayExts]

interface VideoFile {
  id: string
  name: string
  transcoded: boolean
  canPlayDirect: boolean
}

// Play mode: 'direct' = MP4, 'cached' = transcoded cache, 'live' = 边转边播
type PlayMode = 'direct' | 'cached' | 'live' | null

let hlsInstance: Hls | null = null
let currentLiveFileId: string | null = null  // saved for cleanup on unmount

const folderId = computed(() => route.params.folderId as string)
const fileId = computed(() => route.params.fileId as string)

const videoRef = ref<HTMLVideoElement | null>(null)
const videoFiles = ref<VideoFile[]>([])
const transcodeStatuses = ref<Record<string, string>>({})
const playProgressThreshold = ref(90)
const autoTranscodeNext = ref(false)
const transcodeEnabled = ref(false)
const playMode = ref<PlayMode>(null)

// Skip controls (session only)
const skipIntroVal = ref(0)
const skipOutroVal = ref(0)

// Playback state
const currentTime = ref(0)
const duration = ref(0)
let lastProgressEmit = 0
let liveTimeOffset = 0  // offset for live mode: actual_time = hls_time + offset

// Play history
interface PlayHistoryItem {
  folder_id: string; file_id: string; position: number; duration: number
  preset?: string; audio_index?: number; subtitle_index?: number; external_subtitle_path?: string
}
const playHistoryRecord = ref<PlayHistoryItem | null>(null)

// Transcode dialog state
const showTranscodeDialog = ref(false)
const transcodeDialogFileId = ref('')
const transcodeDialogFileName = ref('')
const transcodePresets = ref<Record<string, { resolution: number; crf: number; preset: string }>>({})
const transcodeProfileName = ref('')

const currentIndex = computed(() => videoFiles.value.findIndex(f => f.id === fileId.value))
const currentFileName = computed(() => {
  const f = videoFiles.value.find(f => f.id === fileId.value)
  return f ? f.name : ''
})

const currentFile = computed(() => videoFiles.value.find(f => f.id === fileId.value))

// Show choice overlay when: non-MP4, no cache, and user hasn't chosen live mode yet
const showChoiceOverlay = computed(() => {
  const f = currentFile.value
  if (!f) return false
  return !f.canPlayDirect && !f.transcoded && playMode.value !== 'live'
})

function getExt(name: string): string {
  const dot = name.lastIndexOf('.')
  return dot >= 0 ? name.substring(dot + 1).toLowerCase() : ''
}

const videoUrl = computed(() => {
  const f = currentFile.value
  if (!f) return ''
  if (f.canPlayDirect) return getPreviewUrl(f.id)
  if (f.transcoded) return getTranscodeStreamUrl(f.id)
  // For live mode, hls.js handles the source directly
  return ''
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

    // Load presets for TranscodeDialog
    if (cfg.presets) transcodePresets.value = cfg.presets
    if (cfg.profile) transcodeProfileName.value = cfg.profile

    // Load folder files
    const res = await getFolder(folderId.value)
    const files: { id: string; name: string }[] = res.data.files

    // Filter video files
    const vids: VideoFile[] = []
    const needTranscodeCheck: string[] = []
    for (const f of files) {
      const ext = getExt(f.name)
      if (!allVideoExts.includes(ext)) continue
      const canPlayDirect = directPlayExts.includes(ext)
      vids.push({ id: f.id, name: f.name, transcoded: canPlayDirect, canPlayDirect })
      if (!canPlayDirect && transcodeEnabled.value) {
        needTranscodeCheck.push(f.id)
      }
    }
    // Sort by name
    vids.sort((a, b) => a.name.localeCompare(b.name))

    // Batch fetch transcode statuses in parallel
    if (needTranscodeCheck.length > 0) {
      const statusResults = await Promise.allSettled(
        needTranscodeCheck.map(id => getTranscodeStatus(id))
      )
      for (let i = 0; i < needTranscodeCheck.length; i++) {
        const id = needTranscodeCheck[i]
        const result = statusResults[i]
        if (result.status === 'fulfilled') {
          const status = result.value.data.status || 'none'
          transcodeStatuses.value[id] = status
          const v = vids.find(v => v.id === id)
          if (v) v.transcoded = status === 'done'
        }
      }
    }
    videoFiles.value = vids

    // Load play history
    try {
      const hRes = await getPlayHistory()
      const record = hRes.data.find((r: any) => r.folder_id === folderId.value)
      if (record) playHistoryRecord.value = record
    } catch { /* ignore */ }

    // Determine initial play mode
    const current = vids.find(f => f.id === fileId.value)
    if (current) {
      if (current.canPlayDirect) {
        playMode.value = 'direct'
      } else if (current.transcoded) {
        playMode.value = 'cached'
      } else {
        playMode.value = null  // show choice overlay
      }
    }

    // Auto-transcode next episode (only for cached/direct mode, not live)
    if (playMode.value !== null && playMode.value !== 'live') {
      autoTranscodeNextEpisode(fileId.value)
    }
  } catch (e: any) {
    if (e.response?.status === 401) router.push('/login')
  }
}

async function startLivePlay(position?: number) {
  const f = currentFile.value
  if (!f) return

  // Destroy existing hls instance before creating new one
  if (hlsInstance) {
    hlsInstance.destroy()
    hlsInstance = null
  }

  playMode.value = 'live'
  currentLiveFileId = f.id

  // Wait for DOM update, then attach hls.js
  await nextTick()
  const video = videoRef.value
  if (!video) return

  // Calculate start segment from position or play history
  const rec = playHistoryRecord.value
  const resumePos = position ?? rec?.position ?? 0
  const startSegment = Math.floor(resumePos / 4)
  liveTimeOffset = startSegment * 4
  const playlistUrl = getLivePlaylistUrl(f.id, startSegment)
  console.log('[Live] starting from segment', startSegment, '(position', resumePos, 's, offset', liveTimeOffset, 's)')

  if (Hls.isSupported()) {
    hlsInstance = new Hls({
      liveSyncDuration: 3,
      liveMaxLatencyDuration: 10,
      maxBufferLength: 10,
      maxMaxBufferLength: 30,
    })
    hlsInstance.loadSource(playlistUrl)
    hlsInstance.attachMedia(video)
    hlsInstance.on(Hls.Events.MANIFEST_PARSED, () => {
      video.play().catch(() => {})
    })
    hlsInstance.on(Hls.Events.ERROR, (_event, data) => {
      if (data.fatal) {
        console.error('[HLS] fatal error:', data.type, data.details)
      }
    })
  } else if (video.canPlayType('application/vnd.apple.mpegurl')) {
    video.src = playlistUrl
    video.addEventListener('loadedmetadata', () => {
      video.play().catch(() => {})
    })
  }
}

function startCache() {
  const f = currentFile.value
  if (!f) return
  transcodeDialogFileId.value = f.id
  transcodeDialogFileName.value = f.name
  showTranscodeDialog.value = true
}

async function handleTranscodeSubmit(preset: string, audioIndex: number, subtitleIndex: number, externalSubtitlePath: string) {
  showTranscodeDialog.value = false
  const f = currentFile.value
  if (!f) return

  try {
    await submitTranscode(f.id, preset, audioIndex, subtitleIndex, externalSubtitlePath)
    transcodeStatuses.value[f.id] = 'pending'
    // Update play history with transcode params
    try {
      await updatePlayHistory(folderId.value, f.id, 0, 0, preset, audioIndex, subtitleIndex, externalSubtitlePath)
      playHistoryRecord.value = {
        folder_id: folderId.value,
        file_id: f.id,
        position: 0, duration: 0,
        preset, audio_index: audioIndex, subtitle_index: subtitleIndex,
        external_subtitle_path: externalSubtitlePath
      }
    } catch { /* ignore */ }
  } catch (e: any) {
    alert(e.response?.data?.error || 'Transcode submit failed')
  }
}

function onLoadedMetadata() {
  if (!videoRef.value) return
  // In live mode, m3u8 already starts from resume position - don't seek
  if (playMode.value === 'live') return
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
  const rawTime = videoRef.value.currentTime
  const rawDuration = videoRef.value.duration

  if (playMode.value === 'live') {
    currentTime.value = rawTime + liveTimeOffset
    duration.value = rawDuration + liveTimeOffset
  } else {
    currentTime.value = rawTime
    duration.value = rawDuration
  }

  // Save progress every 5s (skip if duration is not yet available)
  const now = Date.now()
  if (now - lastProgressEmit >= 5000 && rawDuration > 0) {
    lastProgressEmit = now
    const saveTime = playMode.value === 'live' ? rawTime + liveTimeOffset : rawTime
    const saveDur = playMode.value === 'live' ? rawDuration + liveTimeOffset : rawDuration
    updatePlayHistory(folderId.value, fileId.value, saveTime, saveDur).catch(() => {})
  }

  // Skip outro
  if (skipOutroVal.value > 0 && rawDuration > 0 && (rawDuration - rawTime) <= skipOutroVal.value) {
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

  // Save current progress before switching
  if (videoRef.value && videoRef.value.duration > 0) {
    const raw = videoRef.value.currentTime
    const rawDur = videoRef.value.duration
    const saveTime = playMode.value === 'live' ? raw + liveTimeOffset : raw
    const saveDur = playMode.value === 'live' ? rawDur + liveTimeOffset : rawDur
    updatePlayHistory(folderId.value, fileId.value, saveTime, saveDur).catch(() => {})
  }

  // Navigate - the loadFolder will determine play mode for the new file
  navigateToFile(target.id)
}

function navigateToFile(id: string) {
  router.push(`/play/${folderId.value}/${id}`)
}

async function autoTranscodeNextEpisode(currentFileId: string) {
  if (!autoTranscodeNext.value || !transcodeEnabled.value) return
  const idx = videoFiles.value.findIndex(f => f.id === currentFileId)
  if (idx < 0 || idx >= videoFiles.value.length - 1) return
  const next = videoFiles.value[idx + 1]
  if (!next || next.canPlayDirect) return
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
    const raw = videoRef.value.currentTime
    const rawDur = videoRef.value.duration
    const saveTime = playMode.value === 'live' ? raw + liveTimeOffset : raw
    const saveDur = playMode.value === 'live' ? rawDur + liveTimeOffset : rawDur
    updatePlayHistory(folderId.value, fileId.value, saveTime, saveDur).catch(() => {})
  }
  // Destroy hls instance
  if (hlsInstance) {
    hlsInstance.destroy()
    hlsInstance = null
  }
})

// Reload when navigating between episodes
watch(() => route.params.fileId, (newId, oldId) => {
  if (newId && newId !== oldId) {
    // Clean up previous hls instance
    if (hlsInstance) {
      hlsInstance.destroy()
      hlsInstance = null
    }
    currentLiveFileId = null
    lastProgressEmit = 0
    currentTime.value = 0
    duration.value = 0
    playMode.value = null
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
  display: flex;
  align-items: center;
  gap: 0.5rem;
}
.live-tag {
  background: #e74c3c;
  color: #fff;
  padding: 0.1rem 0.4rem;
  border-radius: 3px;
  font-size: 0.7rem;
  font-weight: 600;
  letter-spacing: 0.5px;
}

/* Choice overlay */
.choice-overlay {
  flex: 1;
  display: flex;
  justify-content: center;
  align-items: center;
  background: #0a0a0a;
}
.choice-box {
  background: #1a1a1a;
  border-radius: 12px;
  padding: 2rem 2.5rem;
  max-width: 420px;
  text-align: center;
  border: 1px solid #333;
}
.choice-file-name {
  font-size: 1rem;
  font-weight: 600;
  color: #eee;
  margin-bottom: 0.5rem;
  word-break: break-all;
}
.choice-hint {
  font-size: 0.9rem;
  color: #999;
  margin-bottom: 1.5rem;
}
.choice-actions {
  display: flex;
  gap: 1rem;
  justify-content: center;
  margin-bottom: 1.5rem;
}
.btn-choice {
  padding: 0.7rem 2rem;
  border: none;
  border-radius: 8px;
  cursor: pointer;
  font-size: 1rem;
  font-weight: 600;
  transition: background 0.2s;
}
.btn-play {
  background: #4a9eff;
  color: #fff;
}
.btn-play:hover { background: #3a8eef; }
.btn-cache {
  background: #555;
  color: #fff;
}
.btn-cache:hover { background: #666; }
.choice-desc {
  text-align: left;
  font-size: 0.8rem;
  color: #777;
  line-height: 1.6;
}
.desc-item {
  margin-bottom: 0.3rem;
}
.desc-item strong {
  color: #aaa;
}
</style>
