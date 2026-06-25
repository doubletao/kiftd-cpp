<template>
  <div class="admin-layout">
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
        <span class="admin-badge">Admin</span>
      </div>
      <div class="header-right">
        <router-link to="/" class="nav-link">Home</router-link>
        <span class="user">{{ userStore.username }}</span>
        <button class="btn-logout" @click="handleLogout">Logout</button>
      </div>
    </header>

    <main class="main">
      <div class="panel">
        <div class="panel-header">
          <h2>User Management</h2>
          <button class="btn btn-primary" @click="showCreateDialog = true">Create User</button>
        </div>

        <div v-if="loading" class="loading">Loading...</div>

        <table v-else class="user-table">
          <thead>
            <tr>
              <th>Username</th>
              <th>Role</th>
              <th>Created</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="u in users" :key="u.username">
              <td>{{ u.username }}</td>
              <td>
                <span class="role-badge" :class="u.role">{{ u.role }}</span>
              </td>
              <td>{{ u.created_at }}</td>
              <td>
                <template v-if="u.role !== 'admin'">
                  <button class="btn btn-sm" @click="showResetDialog(u.username)">Reset Password</button>
                  <button class="btn btn-sm btn-danger" @click="confirmDelete(u.username)">Delete</button>
                </template>
                <span v-else class="text-muted">-</span>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </main>

    <!-- Create User Dialog -->
    <div v-if="showCreateDialog" class="dialog-overlay" @click.self="showCreateDialog = false">
      <div class="dialog">
        <h3>Create User</h3>
        <div class="form-group">
          <label>Username</label>
          <input v-model="newUsername" placeholder="Username" autofocus />
        </div>
        <div class="form-group">
          <label>Password</label>
          <input v-model="newPassword" type="password" placeholder="Password" />
        </div>
        <div v-if="error" class="error">{{ error }}</div>
        <div class="dialog-actions">
          <button class="btn" @click="showCreateDialog = false">Cancel</button>
          <button class="btn btn-primary" @click="handleCreate">Create</button>
        </div>
      </div>
    </div>

    <!-- Reset Password Dialog -->
    <div v-if="resetTarget" class="dialog-overlay" @click.self="resetTarget = ''">
      <div class="dialog">
        <h3>Reset Password</h3>
        <p>Reset password for: <strong>{{ resetTarget }}</strong></p>
        <div class="form-group">
          <label>New Password</label>
          <input v-model="resetPasswordVal" type="password" placeholder="New Password" />
        </div>
        <div v-if="error" class="error">{{ error }}</div>
        <div class="dialog-actions">
          <button class="btn" @click="resetTarget = ''">Cancel</button>
          <button class="btn btn-primary" @click="handleReset">Reset</button>
        </div>
      </div>
    </div>

    <!-- Delete Confirmation Dialog -->
    <div v-if="deleteTarget" class="dialog-overlay" @click.self="deleteTarget = ''">
      <div class="dialog">
        <h3>Delete User</h3>
        <p>Are you sure you want to delete user: <strong>{{ deleteTarget }}</strong>?</p>
        <p class="warning">This will delete all their files and data.</p>
        <div class="dialog-actions">
          <button class="btn" @click="deleteTarget = ''">Cancel</button>
          <button class="btn btn-danger" @click="handleDelete">Delete</button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { getUsers, createUser, deleteUser, resetPassword, logout } from '../api'

interface User {
  username: string
  role: string
  created_at: string
}

const router = useRouter()
const userStore = useUserStore()

const users = ref<User[]>([])
const loading = ref(true)
const showCreateDialog = ref(false)
const newUsername = ref('')
const newPassword = ref('')
const resetTarget = ref('')
const resetPasswordVal = ref('')
const deleteTarget = ref('')
const error = ref('')

async function fetchUsers() {
  loading.value = true
  try {
    const res = await getUsers()
    users.value = res.data
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Failed to load users'
  } finally {
    loading.value = false
  }
}

async function handleCreate() {
  error.value = ''
  if (!newUsername.value || !newPassword.value) {
    error.value = 'Username and password are required'
    return
  }
  try {
    await createUser(newUsername.value, newPassword.value)
    showCreateDialog.value = false
    newUsername.value = ''
    newPassword.value = ''
    await fetchUsers()
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Failed to create user'
  }
}

function showResetDialog(username: string) {
  resetTarget.value = username
  resetPasswordVal.value = ''
  error.value = ''
}

async function handleReset() {
  error.value = ''
  if (!resetPasswordVal.value) {
    error.value = 'Password is required'
    return
  }
  try {
    await resetPassword(resetTarget.value, resetPasswordVal.value)
    resetTarget.value = ''
    resetPasswordVal.value = ''
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Failed to reset password'
  }
}

function confirmDelete(username: string) {
  deleteTarget.value = username
  error.value = ''
}

async function handleDelete() {
  error.value = ''
  try {
    await deleteUser(deleteTarget.value)
    deleteTarget.value = ''
    await fetchUsers()
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Failed to delete user'
  }
}

async function handleLogout() {
  await logout()
  userStore.username = ''
  userStore.role = ''
  router.push('/login')
}

onMounted(() => {
  fetchUsers()
})
</script>

<style scoped>
.admin-layout {
  min-height: 100vh;
  background: #f5f5f5;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0 20px;
  height: 56px;
  background: #1976d2;
  color: white;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.logo {
  font-size: 20px;
  font-weight: bold;
  color: white;
  text-decoration: none;
}

.admin-badge {
  background: rgba(255, 255, 255, 0.2);
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
}

.nav-link {
  color: rgba(255, 255, 255, 0.9);
  text-decoration: none;
}

.nav-link:hover {
  color: white;
}

.user {
  opacity: 0.9;
}

.btn-logout {
  background: rgba(255, 255, 255, 0.2);
  border: none;
  color: white;
  padding: 6px 12px;
  border-radius: 4px;
  cursor: pointer;
}

.btn-logout:hover {
  background: rgba(255, 255, 255, 0.3);
}

.main {
  max-width: 900px;
  margin: 20px auto;
  padding: 0 20px;
}

.panel {
  background: white;
  border-radius: 8px;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  overflow: hidden;
}

.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid #eee;
}

.panel-header h2 {
  margin: 0;
  font-size: 18px;
}

.loading {
  padding: 40px;
  text-align: center;
  color: #666;
}

.user-table {
  width: 100%;
  border-collapse: collapse;
}

.user-table th,
.user-table td {
  padding: 12px 16px;
  text-align: left;
  border-bottom: 1px solid #eee;
}

.user-table th {
  background: #fafafa;
  font-weight: 600;
  font-size: 13px;
  color: #666;
}

.role-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 500;
}

.role-badge.admin {
  background: #e3f2fd;
  color: #1976d2;
}

.role-badge.user {
  background: #f5f5f5;
  color: #666;
}

.text-muted {
  color: #999;
}

.btn {
  padding: 8px 16px;
  border: 1px solid #ddd;
  border-radius: 4px;
  background: white;
  cursor: pointer;
  font-size: 14px;
}

.btn:hover {
  background: #f5f5f5;
}

.btn-primary {
  background: #1976d2;
  color: white;
  border-color: #1976d2;
}

.btn-primary:hover {
  background: #1565c0;
}

.btn-danger {
  background: #d32f2f;
  color: white;
  border-color: #d32f2f;
}

.btn-danger:hover {
  background: #c62828;
}

.btn-sm {
  padding: 4px 8px;
  font-size: 12px;
}

.dialog-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.dialog {
  background: white;
  border-radius: 8px;
  padding: 24px;
  min-width: 360px;
  max-width: 450px;
}

.dialog h3 {
  margin: 0 0 16px;
}

.form-group {
  margin-bottom: 16px;
}

.form-group label {
  display: block;
  margin-bottom: 4px;
  font-size: 14px;
  color: #666;
}

.form-group input {
  width: 100%;
  padding: 8px 12px;
  border: 1px solid #ddd;
  border-radius: 4px;
  font-size: 14px;
}

.form-group input:focus {
  outline: none;
  border-color: #1976d2;
}

.error {
  color: #d32f2f;
  font-size: 14px;
  margin-bottom: 12px;
}

.warning {
  color: #f57c00;
  font-size: 14px;
}

.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 20px;
}
</style>
