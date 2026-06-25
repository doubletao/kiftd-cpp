import { createRouter, createWebHistory } from 'vue-router'

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/login',
      name: 'login',
      component: () => import('../views/LoginView.vue')
    },
    {
      path: '/',
      name: 'home',
      component: () => import('../views/HomeView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/folder/:id',
      name: 'folder',
      component: () => import('../views/HomeView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/shares',
      name: 'shares',
      component: () => import('../views/SharesView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/transcode-tasks',
      name: 'transcode-tasks',
      component: () => import('../views/TranscodeTasksView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/play-history',
      name: 'play-history',
      component: () => import('../views/PlayHistoryView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/play/:folderId/:fileId',
      name: 'play',
      component: () => import('../views/PlayView.vue'),
      meta: { requiresAuth: true }
    },
    {
      path: '/admin',
      name: 'admin',
      component: () => import('../views/AdminView.vue'),
      meta: { requiresAuth: true, requiresAdmin: true }
    },
    {
      path: '/settings',
      name: 'settings',
      component: () => import('../views/UserSettingsView.vue'),
      meta: { requiresAuth: true }
    }
  ]
})

router.beforeEach(async (to) => {
  if (to.meta.requiresAuth) {
    try {
      const res = await fetch('/api/auth/me', { credentials: 'same-origin' })
      if (!res.ok) return { name: 'login' }
      if (to.meta.requiresAdmin) {
        const data = await res.json()
        if (data.role !== 'admin') return { name: 'home' }
      }
    } catch {
      return { name: 'login' }
    }
  }
})

export default router
