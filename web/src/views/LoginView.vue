<template>
  <div class="login-container">
    <div class="login-card">
      <h1>kiftd</h1>
      <p class="subtitle">lightweight file server</p>
      <form @submit.prevent="handleLogin">
        <input v-model="username" type="text" placeholder="Username" required autofocus />
        <input v-model="password" type="password" placeholder="Password" required />
        <button type="submit" :disabled="loading">
          {{ loading ? 'Logging in...' : 'Login' }}
        </button>
        <p v-if="error" class="error">{{ error }}</p>
      </form>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useUserStore } from '../stores/user'

const router = useRouter()
const userStore = useUserStore()

const username = ref('')
const password = ref('')
const loading = ref(false)
const error = ref('')

async function handleLogin() {
  loading.value = true
  error.value = ''
  try {
    await userStore.login(username.value, password.value)
    router.push('/')
  } catch (e: any) {
    error.value = e.response?.data?.error || 'Login failed'
  } finally {
    loading.value = false
  }
}
</script>

<style scoped>
.login-container {
  display: flex;
  justify-content: center;
  align-items: center;
  min-height: 100vh;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
}
.login-card {
  background: white;
  padding: 2.5rem;
  border-radius: 12px;
  box-shadow: 0 20px 60px rgba(0,0,0,0.3);
  width: 100%;
  max-width: 360px;
  text-align: center;
}
h1 {
  font-size: 2rem;
  color: #333;
  margin-bottom: 0.25rem;
}
.subtitle {
  color: #999;
  font-size: 0.85rem;
  margin-bottom: 2rem;
}
form {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}
input {
  padding: 0.75rem 1rem;
  border: 1px solid #ddd;
  border-radius: 8px;
  font-size: 1rem;
  outline: none;
  transition: border-color 0.2s;
}
input:focus {
  border-color: #667eea;
}
button {
  padding: 0.75rem;
  background: #667eea;
  color: white;
  border: none;
  border-radius: 8px;
  font-size: 1rem;
  cursor: pointer;
  transition: background 0.2s;
}
button:hover {
  background: #5a6fd6;
}
button:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}
.error {
  color: #e74c3c;
  font-size: 0.85rem;
}
</style>
