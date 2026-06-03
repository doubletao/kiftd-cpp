<template>
  <div v-if="visible" class="preview-overlay" @click.self="close">
    <div class="preview-dialog">
      <div class="preview-header">
        <span class="preview-title">{{ fileName }}</span>
        <button class="preview-close" @click="close">&times;</button>
      </div>
      <div class="preview-body">
        <!-- Image -->
        <img v-if="isImage" :src="previewUrl" class="preview-image" />
        <!-- Audio -->
        <div v-else-if="isAudio" class="preview-audio">
          <audio :src="previewUrl" controls autoplay></audio>
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
import { getPreviewUrl } from '../api'

const props = defineProps<{
  visible: boolean
  fileId: string
  fileName: string
}>()

const emit = defineEmits<{
  (e: 'close'): void
}>()

const textContent = ref('')

const ext = computed(() => {
  const dot = props.fileName.lastIndexOf('.')
  return dot >= 0 ? props.fileName.substring(dot + 1).toLowerCase() : ''
})

const isImage = computed(() => ['png', 'jpg', 'jpeg', 'gif', 'svg', 'ico', 'bmp', 'webp'].includes(ext.value))
const isAudio = computed(() => ['mp3', 'wav', 'ogg', 'flac', 'aac'].includes(ext.value))
const isText = computed(() => ['txt', 'text', 'json', 'js', 'css', 'html', 'htm', 'xml', 'md', 'csv', 'log', 'ini', 'conf', 'yml', 'yaml', 'sh', 'bat', 'py', 'java', 'c', 'cpp', 'h', 'hpp'].includes(ext.value))

const previewUrl = computed(() => getPreviewUrl(props.fileId))

watch(() => props.visible, async (val) => {
  if (val && isText.value && props.fileId) {
    try {
      const res = await fetch(previewUrl.value, { credentials: 'same-origin' })
      textContent.value = await res.text()
    } catch {
      textContent.value = 'Failed to load file content.'
    }
  } else {
    textContent.value = ''
  }
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
  padding: 1rem;
}
.preview-image {
  max-width: 100%;
  max-height: 70vh;
  object-fit: contain;
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
.preview-text {
  width: 100%;
  max-height: 70vh;
  overflow: auto;
  background: #f8f8f8;
  padding: 1rem;
  border-radius: 6px;
  font-size: 0.9rem;
  line-height: 1.6;
  white-space: pre-wrap;
  word-break: break-all;
  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
}
.preview-unsupported {
  color: #999;
  font-size: 1rem;
}
</style>
