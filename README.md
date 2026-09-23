<div align="center">

<img src="assets/icon.png" alt="PDF Organizer" width="128" />

# PDF Organizer

### Your team's papers, in one place — tagged, discussed, and always in sync.

[![Qt 6](https://img.shields.io/badge/Qt-6.2%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white)](https://www.qt.io/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![FastAPI](https://img.shields.io/badge/FastAPI-backend-009688?style=for-the-badge&logo=fastapi&logoColor=white)](https://fastapi.tiangolo.com/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14%2B-4169E1?style=for-the-badge&logo=postgresql&logoColor=white)](https://www.postgresql.org/)

[![Release](https://img.shields.io/github/v/release/DragMaid/PDFOrganizer?style=flat-square&color=blue&include_prereleases&sort=semver)](https://github.com/DragMaid/PDFOrganizer/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/DragMaid/PDFOrganizer/release.yml?style=flat-square&label=build)](https://github.com/DragMaid/PDFOrganizer/actions)
[![Platforms](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey?style=flat-square)](#get-it-running)
![License: MIT](https://img.shields.io/badge/license-MIT-green?style=flat-square)
[![Stars](https://img.shields.io/github/stars/DragMaid/PDFOrganizer?style=flat-square&color=yellow)](https://github.com/DragMaid/PDFOrganizer/stargazers)

</div>

---

<div align="center">
  <img src="assets/screenshot.png" alt="PDF Organizer — grid view with tags, group members and notes" width="100%" />
</div>

---

## Features

- Each directory containing PDFs becomes a group. Invite people with a join code
  such as `PDFORG-S5G2-QFR2-45PR`.
- Tags are shared with the group and show up as filter chips in the sidebar.
  If two people add or remove the same tag at once, nothing conflicts.
- Notes are attached to a file. Anyone in the group can read them, but only the
  author can edit or delete one.
- Live search across file names and tags. There is a sortable list view and a
  grid view with page thumbnails.
- Uploads run in the background with a progress bar. Tags and notes written
  while offline are queued and sent with the next sync.
- Each file has a status dot: green if the group has it, amber if it only
  exists locally, blue while it is uploading.
- Changes from other members arrive over a websocket, so tag and note updates
  show up without a refresh.
- Dark theme, saved window layout, and a list of recently opened files.

All local features (folders, tags, notes, thumbnails, search) work without an
account. You only need to sign in to share.

## Installation

Prebuilt binaries for Linux, macOS and Windows are on the
[releases page](https://github.com/DragMaid/PDFOrganizer/releases).

### Building from source

```bash
git clone https://github.com/DragMaid/PDFOrganizer.git
cd PDFOrganizer
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/PDFOrganizer
```

Requirements:

| Dependency | Minimum | Notes |
|---|---|---|
| CMake | 3.21 | |
| C++ compiler | GCC 10 / Clang 12 / MSVC 2019 | C++17 |
| Qt | 6.2 | Core, Widgets, Sql, Concurrent, Network, WebSockets |
| Qt PDF | 6.4 (optional) | Real page thumbnails instead of placeholders |
| Python | 3.11 | Backend only |
| PostgreSQL | 14 | Backend only |

Ubuntu / Debian:

```bash
sudo apt install cmake ninja-build qt6-base-dev qt6-base-dev-tools \
                 libqt6sql6-sqlite qt6-websockets-dev qt6-pdf-dev
```

macOS (Homebrew):

```bash
brew install cmake ninja qt@6
export PATH="$(brew --prefix qt@6)/bin:$PATH"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

Windows (MSVC + Qt installer):

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release
```

### Backend

Sharing goes through a FastAPI service. See [`backend/README.md`](backend/README.md)
for the full setup. In short:

```bash
cd backend
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # set PDFORG_JWT_SECRET and the PDFORG_B2_* keys
python init_db.py
uvicorn app.main:app --port 8000
```

Then start the client, sign in, and enter the server address in the sign-in
dialog. It can be changed later under Settings > Account.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Add folder |
| `Ctrl+1` / `Ctrl+2` | List view / grid view |
| `Enter` or double-click | Open the selected PDF |
| `Ctrl+Q` | Quit |

## Architecture

```
Qt desktop client                     FastAPI backend
- scans local folders    HTTPS/JWT    - Postgres (files, tags, notes,
- renders thumbnails     -------->      groups, members)
- local SQLite cache     websocket    - Backblaze B2 uploads
                                      - permission checks
```

The client doesn't hold any shared credentials. It only knows the server
address and a refresh token; the Postgres and Backblaze keys stay in the
backend's environment. All network access in the client goes through
`src/api/ApiClient`.

Files are identified by the SHA-256 of their contents rather than their path.
Two people with the same paper in different folders share its tags and notes,
and each PDF is only uploaded once.

### Groups

Every directory that directly contains a PDF is its own group, named after its
path below the watched root. A file's group is determined by which directory it
is in.

```
Add /home/me/Papers
├── thesis.pdf             ->  group "Papers"
├── 2023/tax.pdf           ->  group "Papers/2023"
├── 2023/vat.pdf           ->  group "Papers/2023"
└── 2023/receipts/a.pdf    ->  group "Papers/2023/receipts"
```

Directories that only contain subdirectories don't get a group. Scanning is
recursive, so subfolders are picked up automatically.

The active group in the toolbar is the group of the currently selected file.
With no file selected, the shared controls are disabled.

| Action | Allowed for |
|---|---|
| Read files, tags and notes | Any member |
| Add files, add/remove tags, write notes | Any member |
| Edit or delete a note | Only the note's author. The group creator can't edit other people's notes |
| Rename the group, invite/remove members | Only the group creator |
| Leave the group | Any member |

Some details:

- Duplicate names are disambiguated. `2023` under two roots shows as
  `Papers/2023` and `Invoices/2023`; two roots with the same basename become
  `Papers (Work)` and `Papers (Home)`.
- Removing a watched folder keeps its groups by default. The confirmation dialog
  shows how many groups are affected and offers to delete the ones you created.
  This option is unchecked by default so other people's notes aren't deleted by
  accident.
- Signing out clears the local id caches. On the next sign-in each directory is
  matched back to the group of the same name you already own, so nothing gets
  duplicated.

### Conflict handling

Tags and notes are handled differently.

Tag operations are idempotent. Adding a tag that someone else just added
succeeds and does nothing, and the same goes for removing one that's already
gone. Tag names are unique case-insensitively in the database, so this is atomic
rather than a read-then-write. The exception is renaming a tag to a name that
already exists, which returns a conflict since merging would lose assignments.

Note edits include the version the UI was showing. If the note changed in the
meantime, the write is rejected and the dialog shows the text you were about to
overwrite.

Backend errors include a `message` field that the client displays as-is.

### Code layout

The client follows MVC with separate controllers:

```
MainWindow          assembles everything; wires signals & slots
   │
   ├── views/       FolderPanel · ListView · GridView · RecentView · dialogs
   │                  emit signals upward, never touch the database
   ├── controllers/ PdfController · TagController · FolderWatcher
   │                  coordinate models, database and services
   ├── models/      PdfModel · TagModel · FolderModel · SearchFilterProxy
   │                  own in-memory data, view-agnostic
   ├── delegates/   ListDelegate · GridDelegate · SyncBadge
   ├── api/         ApiClient (REST + websocket) · ApiTypes
   ├── database/    DatabaseManager (SQLite)
   └── utils/       PdfOpener · ThumbnailGenerator · SearchFilterProxy
```

The UI uses Qt Widgets rather than QML, mainly for native dialogs and context
menus, simpler deployment, and because it suits a list/grid-heavy interface.

Folder scanning and thumbnail rendering run through `QtConcurrent::run` and
return results on the main thread. Backend calls are all asynchronous.

The local SQLite database stores per-machine data (watched folders, scan
results, thumbnails, preferences) along with caches for file hashes, the backend
ids they map to, and each directory's group.

### API smoke test

To check that the client and backend agree:

```bash
cmake -S . -B build -DBUILD_API_SMOKETEST=ON
cmake --build build --target apiclient_smoketest
./build/apiclient_smoketest http://localhost:8000
```

## License

MIT
