<template>
  <div v-if="visible" class="preview-overlay" @click.self="close">
    <div class="preview-dialog">
      <div class="preview-header">
        <span class="preview-title">{{ currentName }}</span>
        <button class="preview-close" @click="close">&times;</button>
      </div>
      <div class="preview-body">
        <!-- Image -->
        <div v-if="isImage" class="preview-image-wrap" ref="imageWrapRef"
             @wheel.prevent="onWheel"
             @mousedown="onDragStart"
             @mousemove="onDragMove"
             @mouseup="onDragEnd"
             @mouseleave="onDragEnd">
          <img :src="previewUrl" class="preview-image"
               :style="{ transform: `scale(${scale}) translate(${panX}px, ${panY}px)`, cursor: scale > 1 ? 'grab' : 'default' }"
               @load="resetTransform" />
          <!-- Zoom controls -->
          <div class="zoom-controls">
            <button class="zoom-btn" @click="zoomIn" title="Zoom In">+</button>
            <span class="zoom-level">{{ Math.round(scale * 100) }}%</span>
            <button class="zoom-btn" @click="zoomOut" title="Zoom Out">&minus;</button>
            <button class="zoom-btn" @click="resetTransform" title="Reset">1:1</button>
          </div>
          <!-- Nav arrows -->
          <button v-if="imageFiles.length > 1" class="nav-arrow nav-prev" @click="prevImage" title="Previous">&lsaquo;</button>
          <button v-if="imageFiles.length > 1" class="nav-arrow nav-next" @click="nextImage" title="Next">&rsaquo;</button>
          <!-- Image counter -->
          <div v-if="imageFiles.length > 1" class="image-counter">{{ currentIndex + 1 }} / {{ imageFiles.length }}</div>
        </div>
        <!-- Audio -->
        <div v-else-if="isAudio" class="preview-audio">
          <audio :src="previewUrl" controls autoplay></audio>
        </div>
        <!-- Video -->
        <div v-else-if="isVideo" class="preview-video">
          <div class="video-toolbar">
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
          <video ref="videoRef" :src="videoUrl" controls autoplay
                 @loadedmetadata="onVideoLoaded"
                 @timeupdate="onTimeUpdate"
                 @ended="onVideoEnded"></video>
        </div>
        <!-- Text -->
        <pre v-else-if="isText" class="preview-text">{{ textContent }}</pre>
        <!-- Unsupported -->
        <div v-else class="preview-unsupported">
          This file type is not supported for preview.
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { getPreviewUrl, getTranscodeStreamUrl } from '../api'

interface ImageFile {
  id: string
  name: string
}

interface VideoFile {
  id: string
  name: string
  transcoded: boolean
}

const props = defineProps<{
  visible: boolean
  fileId: string
  fileName: string
  imageFiles?: ImageFile[]
  transcoded?: boolean
  videoFiles?: VideoFile[]
  folderId?: string
  progressThreshold?: number
  initialTime?: number
}>()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'navigate', id: string, name: string): void
  (e: 'playNext', fileId: string, fileName: string, transcoded: boolean): void
  (e: 'progressUpdate', folderId: string, fileId: string, position: number, duration: number): void
}>()

const textContent = ref('')

// Video playback state
const videoRef = ref<HTMLVideoElement | null>(null)
const skipIntroVal = ref(0)
const skipOutroVal = ref(0)
let lastProgressEmit = 0

// Zoom & Pan state
const scale = ref(1)
const panX = ref(0)
const panY = ref(0)
let isDragging = false
let dragStartX = 0
let dragStartY = 0
let panStartX = 0
let panStartY = 0

const imageWrapRef = ref<HTMLElement | null>(null)

// Navigation
const imageFiles = computed(() => props.imageFiles || [])
const currentIndex = computed(() => {
  const idx = imageFiles.value.findIndex(f => f.id === props.fileId)
  return idx >= 0 ? idx : 0
})
const currentName = computed(() => {
  if (isImage.value && imageFiles.value.length > 0) {
    return imageFiles.value[currentIndex.value]?.name ?? props.fileName
  }
  return props.fileName
})

const ext = computed(() => {
  const dot = props.fileName.lastIndexOf('.')
  return dot >= 0 ? props.fileName.substring(dot + 1).toLowerCase() : ''
})

const isImage = computed(() => ['png', 'jpg', 'jpeg', 'gif', 'svg', 'ico', 'bmp', 'webp'].includes(ext.value))
const isAudio = computed(() => ['mp3', 'wav', 'ogg', 'flac', 'aac'].includes(ext.value))
const isVideo = computed(() => props.transcoded || ['mp4'].includes(ext.value))
const isText = computed(() => ['txt', 'text', 'json', 'js', 'css', 'html', 'htm', 'xml', 'md', 'csv', 'log', 'ini', 'conf', 'yml', 'yaml', 'sh', 'bat', 'py', 'java', 'c', 'cpp', 'h', 'hpp'].includes(ext.value))

const previewUrl = computed(() => getPreviewUrl(props.fileId))
const videoUrl = computed(() => props.transcoded ? getTranscodeStreamUrl(props.fileId) : getPreviewUrl(props.fileId))

// Zoom methods
function zoomIn() {
  scale.value = Math.min(scale.value * 1.25, 10)
}

function zoomOut() {
  scale.value = Math.max(scale.value / 1.25, 0.1)
  if (scale.value <= 1) {
    panX.value = 0
    panY.value = 0
  }
}

function resetTransform() {
  scale.value = 1
  panX.value = 0
  panY.value = 0
}

function onWheel(e: WheelEvent) {
  if (e.deltaY < 0) {
    zoomIn()
  } else {
    zoomOut()
  }
}

// Drag to pan
function onDragStart(e: MouseEvent) {
  if (scale.value <= 1) return
  isDragging = true
  dragStartX = e.clientX
  dragStartY = e.clientY
  panStartX = panX.value
  panStartY = panY.value
  ;(e.target as HTMLElement).style.cursor = 'grabbing'
}

function onDragMove(e: MouseEvent) {
  if (!isDragging) return
  panX.value = panStartX + (e.clientX - dragStartX) / scale.value
  panY.value = panStartY + (e.clientY - dragStartY) / scale.value
}

function onDragEnd(e?: MouseEvent) {
  if (!isDragging) return
  isDragging = false
  if (e && imageWrapRef.value) {
    const img = imageWrapRef.value.querySelector('.preview-image') as HTMLElement
    if (img) img.style.cursor = scale.value > 1 ? 'grab' : 'default'
  }
}

// Navigation
function prevImage() {
  if (imageFiles.value.length <= 1) return
  const idx = currentIndex.value > 0 ? currentIndex.value - 1 : imageFiles.value.length - 1
  const file = imageFiles.value[idx]
  emit('navigate', file.id, file.name)
}

function nextImage() {
  if (imageFiles.value.length <= 1) return
  const idx = currentIndex.value < imageFiles.value.length - 1 ? currentIndex.value + 1 : 0
  const file = imageFiles.value[idx]
  emit('navigate', file.id, file.name)
}

// Keyboard support
function onKeyDown(e: KeyboardEvent) {
  if (!props.visible) return
  if (e.key === 'Escape') close()
  if (isImage.value) {
    if (e.key === 'ArrowLeft') prevImage()
    if (e.key === 'ArrowRight') nextImage()
    if (e.key === '+' || e.key === '=') zoomIn()
    if (e.key === '-') zoomOut()
    if (e.key === '0') resetTransform()
  }
}

// Video playback methods
function onVideoLoaded() {
  if (videoRef.value && props.initialTime && props.initialTime > 0) {
    videoRef.value.currentTime = props.initialTime
  }
  if (videoRef.value && skipIntroVal.value > 0 && videoRef.value.currentTime < skipIntroVal.value) {
    videoRef.value.currentTime = skipIntroVal.value
  }
}

function onTimeUpdate() {
  if (!videoRef.value || !props.folderId) return
  const video = videoRef.value
  const now = Date.now()
  if (now - lastProgressEmit >= 5000) {
    lastProgressEmit = now
    emit('progressUpdate', props.folderId, props.fileId, video.currentTime, video.duration)
  }
  // Skip outro
  if (skipOutroVal.value > 0 && video.duration > 0 && (video.duration - video.currentTime) <= skipOutroVal.value) {
    playNextEpisode()
  }
}

function onVideoEnded() {
  playNextEpisode()
}

function playNextEpisode() {
  if (!props.videoFiles || props.videoFiles.length === 0) return
  const idx = props.videoFiles.findIndex(f => f.id === props.fileId)
  if (idx < 0 || idx >= props.videoFiles.length - 1) return
  const next = props.videoFiles[idx + 1]
  emit('playNext', next.id, next.name, next.transcoded)
}

function adjustSkip(type: 'intro' | 'outro', delta: number) {
  if (type === 'intro') {
    skipIntroVal.value = Math.max(0, skipIntroVal.value + delta)
  } else {
    skipOutroVal.value = Math.max(0, skipOutroVal.value + delta)
  }
}

watch(() => props.visible, async (val) => {
  if (val) {
    document.addEventListener('keydown', onKeyDown)
    if (isText.value && props.fileId) {
      try {
        const res = await fetch(previewUrl.value, { credentials: 'same-origin' })
        textContent.value = await res.text()
      } catch {
        textContent.value = 'Failed to load file content.'
      }
    }
  } else {
    document.removeEventListener('keydown', onKeyDown)
    textContent.value = ''
    resetTransform()
  }
})

// Reset transform on image navigation, reset video state on file change
watch(() => props.fileId, () => {
  resetTransform()
  lastProgressEmit = 0
  // Don't reset skip values - they are session-wide preferences
})

function close() {
  emit('close')
}
</script>

<style scoped>
.preview-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.6);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 100;
}
.preview-dialog {
  background: #fff;
  border-radius: 10px;
  width: 80vw;
  max-width: 900px;
  max-height: 85vh;
  display: flex;
  flex-direction: column;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.3);
  overflow: hidden;
}
.preview-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  border-bottom: 1px solid #eee;
  flex-shrink: 0;
}
.preview-title {
  font-size: 0.95rem;
  font-weight: 500;
  color: #333;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.preview-close {
  background: none;
  border: none;
  font-size: 1.5rem;
  cursor: pointer;
  color: #999;
  line-height: 1;
  padding: 0 0.25rem;
}
.preview-close:hover {
  color: #333;
}
.preview-body {
  flex: 1;
  overflow: auto;
  display: flex;
  justify-content: center;
  align-items: center;
  padding: 0;
}
.preview-image-wrap {
  position: relative;
  width: 100%;
  height: 100%;
  display: flex;
  justify-content: center;
  align-items: center;
  overflow: hidden;
  min-height: 400px;
}
.preview-image {
  max-width: 100%;
  max-height: 70vh;
  object-fit: contain;
  transition: transform 0.1s ease;
  user-select: none;
  -webkit-user-drag: none;
}
.zoom-controls {
  position: absolute;
  bottom: 1rem;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 0.4rem;
  background: rgba(0, 0, 0, 0.6);
  padding: 0.3rem 0.6rem;
  border-radius: 6px;
  z-index: 5;
}
.zoom-btn {
  background: none;
  border: none;
  color: #fff;
  font-size: 1.1rem;
  cursor: pointer;
  width: 28px;
  height: 28px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 4px;
}
.zoom-btn:hover {
  background: rgba(255, 255, 255, 0.2);
}
.zoom-level {
  color: #fff;
  font-size: 0.8rem;
  min-width: 40px;
  text-align: center;
}
.nav-arrow {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  background: rgba(0, 0, 0, 0.4);
  border: none;
  color: #fff;
  font-size: 2.5rem;
  cursor: pointer;
  width: 44px;
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 5;
  border-radius: 4px;
  transition: background 0.2s;
}
.nav-arrow:hover {
  background: rgba(0, 0, 0, 0.7);
}
.nav-prev {
  left: 0.5rem;
}
.nav-next {
  right: 0.5rem;
}
.image-counter {
  position: absolute;
  top: 0.5rem;
  left: 50%;
  transform: translateX(-50%);
  background: rgba(0, 0, 0, 0.5);
  color: #fff;
  font-size: 0.8rem;
  padding: 0.2rem 0.7rem;
  border-radius: 10px;
  z-index: 5;
}
.preview-audio {
  width: 100%;
  text-align: center;
  padding: 2rem 0;
}
.preview-audio audio {
  width: 80%;
  min-width: 300px;
}
.preview-video {
  width: 100%;
  text-align: center;
  padding: 1rem 0;
}
.preview-video video {
  max-width: 100%;
  max-height: 70vh;
  background: #000;
}
.video-toolbar {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 0.4rem;
  padding: 0.5rem 0;
  font-size: 0.85rem;
  color: #555;
}
.skip-label {
  font-weight: 500;
  margin-left: 0.3rem;
}
.skip-btn {
  background: #f0f0f0;
  border: 1px solid #ddd;
  border-radius: 4px;
  padding: 0.15rem 0.5rem;
  cursor: pointer;
  font-size: 0.8rem;
}
.skip-btn:hover { background: #e0e0e0; }
.skip-value {
  min-width: 2.5rem;
  text-align: center;
  font-variant-numeric: tabular-nums;
}
.skip-sep {
  margin: 0 0.3rem;
  color: #ccc;
}
.preview-text {
  width: 100%;
  max-height: 70vh;
  overflow: auto;
  background: #f8f8f8;
  padding: 1rem;
  border-radius: 0;
  font-size: 0.9rem;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
  margin: 0;
}
.preview-unsupported {
  color: #999;
  font-size: 1rem;
}
</style>
