<template>
  <div v-if="visible" class="dialog-overlay" @click.self="close">
    <div class="dialog">
      <div class="dialog-header">
        <h3>Transcode</h3>
        <button class="btn-close" @click="close">&times;</button>
      </div>

      <div v-if="loading" class="loading">Probing file...</div>
      <div v-else-if="error" class="error-msg">{{ error }}</div>
      <div v-else class="dialog-body">
        <!-- File info -->
        <div class="info-section">
          <div class="info-row">
            <span class="info-label">File:</span>
            <span class="info-value">{{ fileName }}</span>
          </div>
          <div v-if="duration" class="info-row">
            <span class="info-label">Duration:</span>
            <span class="info-value">{{ formatDuration(duration) }}</span>
          </div>
        </div>

        <!-- Audio track -->
        <div class="field-group">
          <label class="field-label">Audio Track</label>
          <select v-model="selectedAudio" class="field-select">
            <option v-for="(a, i) in audioStreams" :key="i" :value="i">
              {{ formatAudioLabel(a, i) }}
            </option>
          </select>
        </div>

        <!-- Subtitle track -->
        <div class="field-group">
          <label class="field-label">Subtitle (burn-in)</label>
          <select v-model="selectedSub" class="field-select">
            <option value="none">No subtitle</option>
            <optgroup v-if="subtitleStreams.length" label="Embedded">
              <option v-for="(s, i) in subtitleStreams" :key="'emb_' + i" :value="'emb_' + i">
                {{ formatSubtitleLabel(s) }}
              </option>
            </optgroup>
            <optgroup v-if="externalSubtitles.length" label="External Files">
              <option v-for="(es, i) in externalSubtitles" :key="'ext_' + i" :value="'ext_' + i">
                {{ formatExternalSubLabel(es) }}
              </option>
            </optgroup>
          </select>
        </div>

        <!-- Quality preset -->
        <div class="field-group">
          <label class="field-label">Quality</label>
          <div class="preset-options">
            <label v-for="name in presetOrder" :key="name" class="preset-option"
                   :class="{ active: selectedPreset === name }">
              <input type="radio" :value="name" v-model="selectedPreset" />
              <span class="preset-name">{{ name }}</span>
              <span class="preset-desc">{{ presetDesc(name) }}</span>
            </label>
          </div>
        </div>
      </div>

      <div class="dialog-actions">
        <button class="btn" @click="close">Cancel</button>
        <button class="btn btn-primary" :disabled="loading || !!error" @click="doSubmit">
          Start Transcode
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, watch, computed } from 'vue'
import { probeFile } from '../api'

interface StreamInfo {
  index: number
  type: string
  codec: string
  language: string
  title?: string
  width?: number
  height?: number
  channels?: number
  channel_layout?: string
}

interface ExternalSub {
  path: string
  filename: string
  language: string
  ext: string
}

const props = defineProps<{
  visible: boolean
  fileId: string
  fileName: string
  presets: Record<string, { resolution: number; crf: number; preset: string }>
}>()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'submit', preset: string, audioIndex: number, subtitleIndex: number, externalSubtitlePath: string): void
}>()

const loading = ref(false)
const error = ref('')
const streams = ref<StreamInfo[]>([])
const externalSubtitles = ref<ExternalSub[]>([])
const duration = ref('')
const selectedAudio = ref(0)
const selectedSub = ref('none')
const selectedPreset = ref('medium')

const PRESET_ORDER = ['fast', 'medium', 'high']

const audioStreams = computed(() => streams.value.filter(s => s.type === 'audio'))
const subtitleStreams = computed(() => streams.value.filter(s => s.type === 'subtitle'))
const presetOrder = computed(() => {
  const available = new Set(Object.keys(props.presets))
  const ordered = PRESET_ORDER.filter(name => available.has(name))
  for (const name of available) {
    if (!ordered.includes(name)) ordered.push(name)
  }
  return ordered
})

watch(() => props.visible, async (val) => {
  if (val && props.fileId) {
    await doProbe()
  } else {
    streams.value = []
    externalSubtitles.value = []
    duration.value = ''
    error.value = ''
    selectedAudio.value = 0
    selectedSub.value = 'none'
    selectedPreset.value = 'medium'
  }
})

async function doProbe() {
  loading.value = true
  error.value = ''
  try {
    const res = await probeFile(props.fileId)
    streams.value = res.data.streams || []
    externalSubtitles.value = res.data.external_subtitles || []
    duration.value = res.data.duration || ''

    // Set default audio to first audio stream (0-based audio index)
    if (audioStreams.value.length > 0) {
      selectedAudio.value = 0
    }
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Probe failed'
  } finally {
    loading.value = false
  }
}

function formatAudioLabel(s: StreamInfo, i: number): string {
  const lang = s.language || 'und'
  const codec = s.codec.toUpperCase()
  const ch = s.channels ? `${s.channels}ch` : ''
  const title = s.title ? ` - ${s.title}` : ''
  return `${lang}${title} - ${codec} ${ch}`.trim()
}

function formatSubtitleLabel(s: StreamInfo): string {
  const lang = s.language || 'und'
  const codec = s.codec.toUpperCase()
  const title = s.title ? ` - ${s.title}` : ''
  return `${lang}${title} - ${codec}`
}

function formatExternalSubLabel(es: ExternalSub): string {
  const lang = es.language || ''
  const label = lang ? `[${lang}] ` : ''
  return `${label}${es.filename}`
}

function presetDesc(name: string): string {
  const p = props.presets[name]
  if (!p) return ''
  const res = p.resolution > 0 ? `${p.resolution}p` : 'Original'
  return `${res} / CRF ${p.crf}`
}

function formatDuration(d: string): string {
  const sec = parseFloat(d)
  if (isNaN(sec)) return d
  const h = Math.floor(sec / 3600)
  const m = Math.floor((sec % 3600) / 60)
  const s = Math.floor(sec % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}

function doSubmit() {
  let subtitleIndex = -1
  let externalPath = ''

  if (selectedSub.value === 'none') {
    subtitleIndex = -1
  } else if (selectedSub.value.startsWith('emb_')) {
    subtitleIndex = parseInt(selectedSub.value.substring(4))
  } else if (selectedSub.value.startsWith('ext_')) {
    const idx = parseInt(selectedSub.value.substring(4))
    if (idx >= 0 && idx < externalSubtitles.value.length) {
      externalPath = externalSubtitles.value[idx].path
    }
    subtitleIndex = -1
  }

  emit('submit', selectedPreset.value, selectedAudio.value, subtitleIndex, externalPath)
}

function close() {
  emit('close')
}
</script>

<style scoped>
.dialog-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.4);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 50;
}
.dialog {
  background: white;
  border-radius: 10px;
  width: 460px;
  max-height: 80vh;
  overflow-y: auto;
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.2);
}
.dialog-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 1rem 1.5rem;
  border-bottom: 1px solid #eee;
}
.dialog-header h3 {
  margin: 0;
  font-size: 1.1rem;
}
.btn-close {
  background: none;
  border: none;
  font-size: 1.4rem;
  cursor: pointer;
  color: #999;
  padding: 0 0.25rem;
}
.btn-close:hover { color: #333; }
.dialog-body {
  padding: 1rem 1.5rem;
}
.loading {
  text-align: center;
  padding: 2rem;
  color: #999;
}
.error-msg {
  text-align: center;
  padding: 1.5rem;
  color: #e74c3c;
}
.info-section {
  margin-bottom: 1rem;
  padding: 0.6rem 0.8rem;
  background: #f8f9fa;
  border-radius: 6px;
}
.info-row {
  display: flex;
  gap: 0.5rem;
  font-size: 0.9rem;
  margin-bottom: 0.2rem;
}
.info-label {
  color: #666;
  min-width: 70px;
}
.info-value {
  color: #333;
  font-weight: 500;
}
.field-group {
  margin-bottom: 1rem;
}
.field-label {
  display: block;
  font-size: 0.85rem;
  font-weight: 600;
  color: #555;
  margin-bottom: 0.4rem;
}
.field-select {
  width: 100%;
  padding: 0.5rem 0.6rem;
  border: 1px solid #ddd;
  border-radius: 6px;
  font-size: 0.9rem;
  background: white;
}
.preset-options {
  display: flex;
  gap: 0.5rem;
}
.preset-option {
  flex: 1;
  padding: 0.6rem;
  border: 2px solid #eee;
  border-radius: 8px;
  cursor: pointer;
  text-align: center;
  transition: border-color 0.2s;
}
.preset-option.active {
  border-color: #667eea;
  background: #f8f9ff;
}
.preset-option input {
  display: none;
}
.preset-name {
  display: block;
  font-weight: 600;
  font-size: 0.9rem;
  text-transform: capitalize;
}
.preset-desc {
  display: block;
  font-size: 0.75rem;
  color: #888;
  margin-top: 0.2rem;
}
.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 0.5rem;
  padding: 0.75rem 1.5rem;
  border-top: 1px solid #eee;
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
.btn:hover { background: #5a6fd6; }
.btn:disabled { opacity: 0.5; cursor: not-allowed; }
.btn-primary { background: #667eea; }
</style>
