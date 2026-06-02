# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

kiftd-cpp is a lightweight file server written in C++ with SQLite, inspired by [kiftd](https://github.com/KOHGYLW/kiftd). It provides file upload/download, folder management, and file sharing via public links. A Vue 3 SPA serves as the frontend.

## Build Commands

### Backend (C++)
```bash
cmake -B build -S .
cmake --build build --config Release
```

### Frontend (Vue 3)
```bash
cd web
npm install    # first time only
npm run build
npm run dev    # development server with hot reload
```

### One-click Package (Windows)
```bash
package.bat    # builds frontend + backend, creates release zip
```

## Architecture

### Backend (C++17)

The backend uses a **Crow** web framework (header-only, based on Asio) with **SQLite3** for persistence. All C++ dependencies are vendored in `third_party/` — no network access needed to build.

**Core modules:**
- `Config` — singleton, loads from `data/config.json`, falls back to defaults
- `Database` — SQLite3 wrapper, owns schema init and all CRUD operations
- `Auth` — password hashing (double SHA256 with salt), user management
- `FileStore` — physical file I/O, stores files as `UUID.bin` in `data/files/`

**Controllers** (register routes on Crow app):
- `auth_controller` — login/logout/session check
- `folder_controller` — CRUD for folders (tree structure via `parent_id`)
- `file_controller` — upload (multipart), download, preview, rename, delete
- `share_controller` — create/list/delete public share links

**Session management:** Crow cookie-based sessions (`crow::CookieParser`).

**Password flow:** Frontend SHA256-hashes the password before sending; backend applies a second SHA256 with salt before storing.

### Frontend (Vue 3 + TypeScript)

Built with Vite, uses Vue Router + Pinia + Axios.

**Routes:**
- `/login` — login page (public)
- `/` and `/folder/:id` — file browser (auth required)
- `/shares` — manage shared links (auth required)

Route guard checks auth via `GET /api/auth/me` before each navigation.

### Data Layout

```
data/
  config.json      # server configuration
  kiftd.db         # SQLite database (auto-created)
  files/           # uploaded files (auto-created, stored as UUID.bin)
```

### API Design

All API routes are under `/api/`. Authentication uses cookie sessions. The share download endpoint (`/s/:id`) is public and does not require auth.

## Key Technical Details

- C++17 required, MSVC `/bigobj` flag enabled for large translation units
- Crow is configured with `.multithreaded()` for concurrent request handling
- SPA fallback: non-API, non-share routes serve `index.html` for client-side routing
- Root folder has empty string `id` — all top-level items have `parent_id = ""`
- Default admin account: `admin` / `admin` (created on first run)
