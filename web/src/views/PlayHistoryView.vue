<template>
  <div class="app-layout">
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
      </div>
      <div class="header-right">
        <router-link to="/play-history" class="nav-link active">Play History</router-link>
        <router-link to="/transcode-tasks" class="nav-link">Transcode Tasks</router-link>
        <router-link to="/shares" class="nav-link">My Shares</router-link>
        <span class="user">{{ userStore.username }}</span>
        <button class="btn-logout" @click="handleLogout">Logout</button>
      </div>
    </header>

    <div class="content">
      <div class="page-header">
        <h2>Play History</h2>
        <button class="btn" @click="loadHistory">Refresh</button>
      </div>

      <div v-if="loading" class="loading">Loading...</div>

      <table v-else-if="records.length" class="history-table">
        <thead>
          <tr>
            <th style="width:40px">#</th>
            <th>Folder</th>
            <th>Last Played</th>
            <th>Progress</th>
            <th>Updated</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(rec, index) in records" :key="rec.folder_id">
            <td class="idx">{{ index + 1 }}</td>
            <td>{{ rec.folder_name }}</td>
            <td>{{ rec.file_name }}</td>
            <td>
              <div class="progress-bar">
                <div class="progress-fill" :style="{ width: getPercent(rec) + '%' }"></div>
                <span class="progress-text">{{ formatTime(rec.position) }} / {{ formatTime(rec.duration) }}</span>
              </div>
            </td>
            <td class="time">{{ rec.updated_at }}</td>
            <td class="actions-cell">
              <button class="btn-sm btn-play" @click="resume(rec)">Resume</button>
              <button class="btn-sm btn-danger" @click="remove(rec.folder_id)">Delete</button>
            </td>
          </tr>
        </tbody>
      </table>

      <div v-else class="empty">No play history</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getPlayHistory, deletePlayHistory } from '../api'

const router = useRouter()
const userStore = useUserStore()

interface HistoryRecord {
  folder_id: string
  folder_name: string
  file_id: string
  file_name: string
  position: number
  duration: number
  updated_at: string
}

const loading = ref(true)
const records = ref<HistoryRecord[]>([])

async function loadHistory() {
  try {
    const res = await getPlayHistory()
    records.value = res.data
  } catch {
    // ignore
  } finally {
    loading.value = false
  }
}

function getPercent(rec: HistoryRecord): number {
  if (!rec.duration || rec.duration <= 0) return 0
  return Math.min(100, Math.round((rec.position / rec.duration) * 100))
}

function formatTime(seconds: number): string {
  if (!seconds || seconds <= 0) return '0:00'
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  const s = Math.floor(seconds % 60)
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
  return `${m}:${String(s).padStart(2, '0')}`
}

function resume(rec: HistoryRecord) {
  router.push({ path: `/folder/${rec.folder_id}`, query: { play: rec.file_id } })
}

async function remove(folderId: string) {
  if (!confirm('Delete this play history record?')) return
  try {
    await deletePlayHistory(folderId)
    records.value = records.value.filter(r => r.folder_id !== folderId)
  } catch (e: any) {
    alert(e.response?.data?.error || 'Delete failed')
  }
}

async function handleLogout() {
  await userStore.logout()
  router.push('/login')
}

onMounted(async () => {
  await userStore.fetchUser()
  await loadHistory()
})
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
.nav-link.active {
  font-weight: 600;
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
.content {
  padding: 1.5rem;
  max-width: 960px;
  margin: 0 auto;
}
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}
.page-header h2 {
  margin: 0;
  font-size: 1.2rem;
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
.history-table {
  width: 100%;
  background: white;
  border-radius: 8px;
  overflow: hidden;
  box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  border-collapse: collapse;
}
.history-table th {
  text-align: left;
  padding: 0.75rem 1rem;
  background: #fafafa;
  border-bottom: 1px solid #eee;
  font-size: 0.85rem;
  color: #666;
}
.history-table td {
  padding: 0.65rem 1rem;
  border-bottom: 1px solid #f0f0f0;
  font-size: 0.9rem;
}
.history-table tr:hover { background: #f8f9ff; }
.idx { color: #999; }
.time { font-size: 0.8rem; color: #888; }
.progress-bar {
  position: relative;
  width: 160px;
  height: 20px;
  background: #f0f0f0;
  border-radius: 10px;
  overflow: hidden;
}
.progress-fill {
  height: 100%;
  background: #667eea;
  border-radius: 10px;
  transition: width 0.3s;
}
.progress-text {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 0.75rem;
  color: #333;
  font-weight: 500;
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
.btn-sm:hover { background: #e0e0e0; }
.btn-play {
  background: #e8f5e9;
  color: #2e7d32;
}
.btn-play:hover { background: #c8e6c9; }
.btn-danger { color: #e74c3c; }
.btn-danger:hover { background: #fde8e8; }
.empty {
  text-align: center;
  padding: 3rem;
  color: #999;
}
.loading {
  text-align: center;
  padding: 3rem;
  color: #999;
}
</style>
