import { defineStore } from 'pinia'
import { ref } from 'vue'
import { getMe, login as apiLogin, logout as apiLogout } from '../api'

export const useUserStore = defineStore('user', () => {
  const username = ref('')

  async function fetchUser() {
    try {
      const res = await getMe()
      username.value = res.data.username
    } catch {
      username.value = ''
    }
  }

  async function login(user: string, pass: string) {
    await apiLogin(user, pass)
    username.value = user
  }

  async function logout() {
    await apiLogout()
    username.value = ''
  }

  return { username, fetchUser, login, logout }
})
