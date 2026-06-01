# kiftd-cpp

A lightweight file server written in C++ with SQLite, inspired by [kiftd](https://github.com/KOHGYLW/kiftd).

## Features

- User login (SHA256 password hashing, pure C++ implementation)
- File upload/download with multipart support
- Folder management (create, rename, delete)
- File sharing via public links
- Vue 3 SPA frontend
- Single binary + single SQLite database file = easy deployment
- Zero external runtime dependencies

## Build

### Prerequisites

- CMake 3.20+
- C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- Git (for CMake FetchContent to download dependencies)
- Node.js 18+ (for frontend build)

All C++ dependencies are included in `third_party/` (Crow, Asio, nlohmann/json, SQLite3) - no network access needed to build.

### Steps

```bash
# Build backend (no network required)
cd kiftd-cpp
cmake -B build -S .
cmake --build build --config Release

# Build frontend
cd web
npm install
npm run build
cd ..
```

## Run

```bash
# Start server (default port 8080)
./build/kiftd

# Custom port and data directory
./build/kiftd -p 9090 -d /path/to/data -w /path/to/web/dist
```

First run creates:
- `data/kiftd.db` - SQLite database
- `data/files/` - file storage directory
- Default admin account: `admin` / `admin`

Open http://localhost:8080 in your browser.

## Migration

Just copy the `data/` directory to the new location:

```bash
cp -r data/ /new/server/data/
./build/kiftd -d /new/server/data
```

## API

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| POST | /api/auth/login | No | Login |
| POST | /api/auth/logout | Yes | Logout |
| GET | /api/auth/me | Yes | Current user |
| GET | /api/folders/:id | Yes | Folder contents |
| POST | /api/folders | Yes | Create folder |
| PUT | /api/folders/:id | Yes | Rename folder |
| DELETE | /api/folders/:id | Yes | Delete folder |
| POST | /api/files/upload | Yes | Upload file |
| GET | /api/files/:id/download | Yes | Download file |
| GET | /api/files/:id/preview | Yes | Preview txt/image |
| PUT | /api/files/:id | Yes | Rename file |
| DELETE | /api/files/:id | Yes | Delete file |
| POST | /api/shares | Yes | Create share |
| GET | /api/shares/mine | Yes | My shares |
| DELETE | /api/shares/:id | Yes | Delete share |
| GET | /s/:share_id | No | Public download |
