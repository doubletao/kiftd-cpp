<template>
  <div class="app-layout">
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
      </div>
      <div class="header-right">
        <router-link to="/transcode-tasks" class="nav-link active">Transcode Tasks</router-link>
        <router-link to="/shares" class="nav-link">My Shares</router-link>
        <span class="user">{{ userStore.username }}</span>
        <button class="btn-logout" @click="handleLogout">Logout</button>
      </div>
    </header>

    <div class="content">
      <div class="page-header">
        <h2>Transcode Tasks</h2>
        <button class="btn" @click="loadTasks">Refresh</button>
      </div>

      <div v-if="loading" class="loading">Loading...</div>

      <table v-else-if="tasks.length" class="task-table">
        <thead>
          <tr>
            <th style="width:40px">#</th>
            <th>File</th>
            <th>Preset</th>
            <th>Status</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="(task, index) in tasks" :key="task.file_id">
            <td class="idx">{{ index + 1 }}</td>
            <td>{{ task.file_name }}</td>
            <td>{{ task.preset }}</td>
            <td>
              <span :class="'badge badge-' + task.status">{{ task.status }}</span>
              <span v-if="task.error" class="error-hint" :title="task.error">!</span>
            </td>
            <td class="actions-cell">
              <template v-if="task.status === 'pending'">
                <button class="btn-sm" :disabled="index === 0" @click="moveTask(task.file_id, -1)">Up</button>
                <button class="btn-sm" :disabled="index === pendingEnd - 1" @click="moveTask(task.file_id, 1)">Down</button>
                <button class="btn-sm btn-danger" @click="cancelTask(task.file_id)">Cancel</button>
              </template>
              <template v-if="task.status === 'transcoding'">
                <button class="btn-sm btn-danger" @click="cancelTask(task.file_id)">Cancel</button>
              </template>
            </td>
          </tr>
        </tbody>
      </table>

      <div v-else class="empty">No transcode tasks</div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getTranscodeTasks, deleteTranscode, reorderTranscodeTask } from '../api'

const router = useRouter()
const userStore = useUserStore()

interface Task {
  file_id: string
  file_name: string
  preset: string
  status: string
  error?: string
}

const loading = ref(true)
const tasks = ref<Task[]>([])
let pollTimer: ReturnType<typeof setInterval> | null = null

const pendingEnd = computed(() => {
  const idx = tasks.value.findIndex(t => t.status !== 'pending')
  return idx === -1 ? tasks.value.length : idx
})

async function loadTasks() {
  try {
    const res = await getTranscodeTasks()
    tasks.value = res.data
  } catch {
    // ignore
  } finally {
    loading.value = false
  }
}

function hasActiveTasks() {
  return tasks.value.some(t => t.status === 'pending' || t.status === 'transcoding')
}

function startPolling() {
  if (pollTimer) return
  pollTimer = setInterval(async () => {
    await loadTasks()
    if (!hasActiveTasks()) stopPolling()
  }, 3000)
}

function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

async function cancelTask(fileId: string) {
  if (!confirm('Cancel this task?')) return
  try {
    await deleteTranscode(fileId)
    await loadTasks()
  } catch (e: any) {
    alert(e.response?.data?.error || 'Cancel failed')
  }
}

async function moveTask(fileId: string, direction: number) {
  try {
    await reorderTranscodeTask(fileId, direction)
    await loadTasks()
  } catch (e: any) {
    alert(e.response?.data?.error || 'Reorder failed')
  }
}

async function handleLogout() {
  await userStore.logout()
  router.push('/login')
}

onMounted(async () => {
  await userStore.fetchUser()
  await loadTasks()
  if (hasActiveTasks()) startPolling()
})

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
  max-width: 900px;
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
.task-table {
  width: 100%;
  background: white;
  border-radius: 8px;
  overflow: hidden;
  box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  border-collapse: collapse;
}
.task-table th {
  text-align: left;
  padding: 0.75rem 1rem;
  background: #fafafa;
  border-bottom: 1px solid #eee;
  font-size: 0.85rem;
  color: #666;
}
.task-table td {
  padding: 0.65rem 1rem;
  border-bottom: 1px solid #f0f0f0;
  font-size: 0.9rem;
}
.task-table tr:hover { background: #f8f9ff; }
.idx { color: #999; }
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
.btn-sm:disabled { opacity: 0.4; cursor: not-allowed; }
.btn-danger { color: #e74c3c; }
.btn-danger:hover { background: #fde8e8; }
.badge {
  display: inline-block;
  padding: 0.15rem 0.5rem;
  border-radius: 10px;
  font-size: 0.8rem;
  font-weight: 500;
}
.badge-pending { background: #e3f2fd; color: #1565c0; }
.badge-transcoding { background: #fff3e0; color: #e65100; }
.badge-done { background: #e8f5e9; color: #2e7d32; }
.badge-failed { background: #ffebee; color: #c62828; }
.error-hint {
  display: inline-block;
  width: 16px;
  height: 16px;
  line-height: 16px;
  text-align: center;
  background: #e74c3c;
  color: white;
  border-radius: 50%;
  font-size: 0.7rem;
  font-weight: 700;
  margin-left: 0.3rem;
  cursor: help;
}
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
