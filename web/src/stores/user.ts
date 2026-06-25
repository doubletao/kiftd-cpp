import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { getMe, login as apiLogin, logout as apiLogout } from '../api'

export const useUserStore = defineStore('user', () => {
  const username = ref('')
  const role = ref('')

  const isAdmin = computed(() => role.value === 'admin')

  async function fetchUser() {
    try {
      const res = await getMe()
      username.value = res.data.username
      role.value = res.data.role || 'user'
    } catch {
      username.value = ''
      role.value = ''
    }
  }

  async function login(user: string, pass: string) {
    await apiLogin(user, pass)
    username.value = user
    // Fetch full info including role
    await fetchUser()
  }

  async function logout() {
    await apiLogout()
    username.value = ''
    role.value = ''
  }

  return { username, role, isAdmin, fetchUser, login, logout }
})
