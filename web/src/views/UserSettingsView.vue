<template>
  <div class="settings-layout">
    <header class="header">
      <div class="header-left">
        <router-link to="/" class="logo">kiftd</router-link>
      </div>
      <div class="header-right">
        <router-link to="/" class="nav-link">Home</router-link>
        <span class="user">{{ userStore.username }}</span>
        <button class="btn-logout" @click="handleLogout">Logout</button>
      </div>
    </header>

    <main class="main">
      <div class="panel">
        <h2>Account Settings</h2>

        <!-- Admin Info -->
        <template v-if="userStore.isAdmin">
          <section class="section">
            <h3>Admin Account</h3>
            <p class="info-text">Admin account is managed by the config file. Password changes are not available here.</p>
            <p class="info-text">To change admin password, edit the config file and restart the server.</p>
          </section>
        </template>

        <!-- User Settings -->
        <template v-else>
          <!-- Change Password -->
          <section class="section">
            <h3>Change Password</h3>
            <div class="form-group">
              <label>Current Password</label>
              <input v-model="oldPassword" type="password" placeholder="Current password" />
            </div>
            <div class="form-group">
              <label>New Password</label>
              <input v-model="newPassword" type="password" placeholder="New password" />
            </div>
            <div class="form-group">
              <label>Confirm New Password</label>
              <input v-model="confirmPassword" type="password" placeholder="Confirm new password" />
            </div>
            <div v-if="passwordError" class="error">{{ passwordError }}</div>
            <div v-if="passwordSuccess" class="success">{{ passwordSuccess }}</div>
            <button class="btn btn-primary" @click="handleChangePassword">Change Password</button>
          </section>

          <!-- Delete Account -->
          <section class="section danger-zone">
            <h3>Danger Zone</h3>
            <p>Permanently delete your account and all associated data.</p>
            <button class="btn btn-danger" @click="showDeleteDialog = true">Delete Account</button>
          </section>
        </template>
      </div>
    </main>

    <!-- Delete Confirmation Dialog -->
    <div v-if="showDeleteDialog" class="dialog-overlay" @click.self="showDeleteDialog = false">
      <div class="dialog">
        <h3>Delete Account</h3>
        <p>Are you sure you want to delete your account?</p>
        <p class="warning">This action cannot be undone. All your files will be permanently deleted.</p>
        <div v-if="deleteError" class="error">{{ deleteError }}</div>
        <div class="dialog-actions">
          <button class="btn" @click="showDeleteDialog = false">Cancel</button>
          <button class="btn btn-danger" @click="handleDeleteAccount">Delete My Account</button>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'
import { changePassword, deleteMyAccount, logout } from '../api'

const router = useRouter()
const userStore = useUserStore()

const oldPassword = ref('')
const newPassword = ref('')
const confirmPassword = ref('')
const passwordError = ref('')
const passwordSuccess = ref('')
const showDeleteDialog = ref(false)
const deleteError = ref('')

async function handleChangePassword() {
  passwordError.value = ''
  passwordSuccess.value = ''

  if (!oldPassword.value || !newPassword.value) {
    passwordError.value = 'Please fill in all fields'
    return
  }

  if (newPassword.value !== confirmPassword.value) {
    passwordError.value = 'New passwords do not match'
    return
  }

  if (newPassword.value.length < 4) {
    passwordError.value = 'Password must be at least 4 characters'
    return
  }

  try {
    await changePassword(oldPassword.value, newPassword.value)
    passwordSuccess.value = 'Password changed successfully'
    oldPassword.value = ''
    newPassword.value = ''
    confirmPassword.value = ''
  } catch (e: any) {
    passwordError.value = e.response?.data?.error || 'Failed to change password'
  }
}

async function handleDeleteAccount() {
  deleteError.value = ''
  try {
    await deleteMyAccount()
    userStore.username = ''
    userStore.role = ''
    router.push('/login')
  } catch (e: any) {
    deleteError.value = e.response?.data?.error || 'Failed to delete account'
  }
}

async function handleLogout() {
  await logout()
  userStore.username = ''
  userStore.role = ''
  router.push('/login')
}
</script>

<style scoped>
.settings-layout {
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
  max-width: 600px;
  margin: 20px auto;
  padding: 0 20px;
}

.panel {
  background: white;
  border-radius: 8px;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
  padding: 24px;
}

.panel h2 {
  margin: 0 0 24px;
  font-size: 20px;
}

.section {
  padding: 20px 0;
  border-top: 1px solid #eee;
}

.section:first-of-type {
  border-top: none;
  padding-top: 0;
}

.section h3 {
  margin: 0 0 16px;
  font-size: 16px;
}

.danger-zone {
  margin-top: 20px;
}

.danger-zone h3 {
  color: #d32f2f;
}

.danger-zone p {
  color: #666;
  margin-bottom: 16px;
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
  padding: 10px 12px;
  border: 1px solid #ddd;
  border-radius: 4px;
  font-size: 14px;
}

.form-group input:focus {
  outline: none;
  border-color: #1976d2;
}

.btn {
  padding: 10px 20px;
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

.error {
  color: #d32f2f;
  font-size: 14px;
  margin-bottom: 12px;
}

.success {
  color: #388e3c;
  font-size: 14px;
  margin-bottom: 12px;
}

.warning {
  color: #f57c00;
  font-size: 14px;
}

.info-text {
  color: #666;
  font-size: 14px;
  margin-bottom: 8px;
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

.dialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 20px;
}
</style>
