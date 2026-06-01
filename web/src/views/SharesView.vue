<template>
  <div class="app-layout">
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

    <div class="toolbar">
      <h2>My Shares</h2>
    </div>

    <div class="content">
      <div v-if="loading" class="loading">Loading...</div>
      <table v-else class="file-table">
        <thead>
          <tr>
            <th>File</th>
            <th>Share Link</th>
            <th>Expires</th>
            <th>Created</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="share in shares" :key="share.id">
            <td>{{ share.file_name }}</td>
            <td>
              <a :href="getShareUrl(share.id)" target="_blank" class="share-link">
                {{ getShareUrl(share.id) }}
              </a>
              <button class="btn-sm" @click="copyLink(share.id)">Copy</button>
            </td>
            <td>{{ share.expire_at || 'Never' }}</td>
            <td>{{ share.created_at }}</td>
            <td>
              <button class="btn-sm btn-danger" @click="removeShare(share.id)">Delete</button>
            </td>
          </tr>
          <tr v-if="!shares.length">
            <td colspan="5" class="empty">No shares yet</td>
          </tr>
        </tbody>
      </table>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getMyShares, deleteShare } from '../api'

const router = useRouter()
const userStore = useUserStore()

const loading = ref(true)
const shares = ref<any[]>([])

onMounted(async () => {
  await userStore.fetchUser()
  await loadShares()
})

async function loadShares() {
  loading.value = true
  try {
    const res = await getMyShares()
    shares.value = res.data
  } catch (e: any) {
    if (e.response?.status === 401) router.push('/login')
  } finally {
    loading.value = false
  }
}

function getShareUrl(id: string) {
  return `${window.location.origin}/s/${id}`
}

async function copyLink(id: string) {
  await navigator.clipboard.writeText(getShareUrl(id))
  alert('Link copied!')
}

async function removeShare(id: string) {
  if (!confirm('Delete this share?')) return
  await deleteShare(id)
  loadShares()
}

async function handleLogout() {
  await userStore.logout()
  router.push('/login')
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
}
.toolbar h2 {
  font-size: 1.2rem;
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
.share-link {
  color: #667eea;
  font-size: 0.85rem;
  word-break: break-all;
}
.btn-sm {
  padding: 0.25rem 0.6rem;
  background: #f0f0f0;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.8rem;
  margin-left: 0.3rem;
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
</style>
