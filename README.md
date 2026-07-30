# PDF Organizer

A modern, dark-themed desktop application for browsing, tagging, and organising
PDF files — built with **C++17** and **Qt 6** (Widgets).

---

## Feature Overview

| Feature | Details |
|---|---|
| **Folder management** | Add/remove root folders; recursive auto-scan; drag & drop folders onto the panel |
| **Directory = group** | Every directory holding a PDF becomes a group of its own (`Papers`, `Papers/2023`); the PDFs sitting in it are tracked automatically |
| **Live file watching** | `QFileSystemWatcher` detects new / deleted PDFs without manual refresh |
| **External PDF viewer** | Opens with Okular (Linux) or the OS default (Windows / macOS fallback) |
| **Tagging** | Group-scoped tags, created/renamed/deleted on the backend; concurrent adds never conflict |
| **List view** | Sortable table: name · folder · tags · last opened · size |
| **Grid / card view** | Thumbnail cards with tag pills; async thumbnail generation |
| **Search** | Live filter by filename and/or tag text |
| **Tag filter** | One-click chips in the left panel (AND semantics) |
| **Groups** | One per directory; its creator invites and removes members by email, everyone else sees the roster |
| **File notes** | Threaded notes per file, per group; only the author can edit or delete their own |
| **Group sync** | Uploads a group's PDFs to Backblaze B2 *through the backend* — no storage credentials on the client |
| **Accounts** | Email + password sign-in against the FastAPI backend; session survives restarts |
| **Recent activity** | Bottom dock showing the 20 most recently opened PDFs |
| **Dark mode** | Full stylesheet; persisted per-user; toggled in Settings |
| **Persistent settings** | SQLite for metadata; `QSettings` for window geometry |

---

## Architecture

The application is split across two processes.

```
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  Qt desktop client           │  HTTPS │  FastAPI backend             │
│                              │ ─────▶ │                              │
│  • scans local folders       │  JWT   │  • Postgres (files, tags,    │
│  • renders thumbnails        │        │    notes, groups, members)   │
│  • local SQLite cache        │        │  • Backblaze B2 uploads      │
│                              │        │  • enforces who may do what  │
└──────────────────────────────┘        └──────────────────────────────┘
```

**The client holds no shared credential.** It knows a server address and a
refresh token; Postgres and Backblaze keys live only in the backend's
environment. Everything shared — groups, tags, notes, uploads — goes through
`src/api/ApiClient`, and nothing else in the client opens a socket.

Local SQLite keeps what is genuinely per-machine (watched folders, scan
results, thumbnails, preferences) plus two caches: file content hashes, and the
backend ids those hashes map to.

### A directory *is* a group

Every directory that **directly** holds a PDF becomes its own group, named after
its path below the watched root. It holds exactly the PDFs sitting in it — the
ones a level down belong to that level's own group. There is no other way to put
a file in one: **a file's group is decided by which directory it sits in**, not
by a checkbox.

```
Add /home/me/Papers
├── thesis.pdf             →  group "Papers"
├── 2023/tax.pdf           →  group "Papers/2023"
├── 2023/vat.pdf           →  group "Papers/2023"
└── 2023/receipts/a.pdf    →  group "Papers/2023/receipts"
```

Three directories, three separate groups, each with its own members. A directory
that holds no PDF of its own — only subdirectories that do — gets no group; it
is a container, not a sharing unit. Scanning is still recursive, so subfolders
are picked up and turned into groups without being added by hand.

Files are identified by the **SHA-256 of their contents**, not their path, so
two people holding the same PDF in different directories share its tags and
notes automatically, and a given PDF is uploaded to B2 exactly once.

The **active group** shown in the toolbar is therefore derived rather than
chosen: it is the group of the directory the selected file sits in, and it
decides where shared work lands — a tag joins that group's vocabulary, a note is
visible to exactly that group's members. With no file selected there is no
active group, and the shared controls are inert.

| Action | Who may do it |
|---|---|
| Read files, tags and notes | Any member of the group |
| Add files, add/remove tags, write notes | Any member |
| **Edit or delete a note** | **Only its author** — the group's creator is not exempt |
| Rename the group, invite/remove members | **Only its creator** — whoever added the folder |
| Leave the group | Any member, of themselves |

The detail pane lists the group's members inline — name, email and whether they
created it. Invite and remove controls are drawn only for the creator; everyone
else sees the same roster read-only plus a **Leave** button. The backend applies
the same rules whatever the client draws.

Two housekeeping details follow from tying groups to directories:

- **Names are disambiguated.** A subfolder carries its path below the root, so
  `2023` under two different roots reads as `Papers/2023` and `Invoices/2023`.
  Two watched roots sharing a basename (`Work/Papers`, `Home/Papers`) become
  `Papers (Work)` and `Papers (Home)`, and their subfolder groups inherit that,
  so the toolbar is never ambiguous.
- **Removing a folder keeps its groups by default.** Un-watching a root is a
  local act, and it now spans several groups — its own directory plus every
  subdirectory that held a PDF. The confirmation says how many, and offers to
  delete the ones you created and leave the ones you joined; that box starts
  unticked so other members' notes are never destroyed by accident.

Signing out clears the local id caches, including the directory → group mapping.
On the next sign-in each directory re-attaches to the group of the same name you
already own before creating a new one, so a round trip through the login sheet
does not duplicate anything.

### How conflicts are handled

The two policies are deliberately opposite.

**Tags never fight.** Adding a tag someone else just added succeeds and changes
nothing; so does removing one they already removed. Names collide
case-insensitively in the database, so the dedup is atomic rather than a
read-then-write race. The one exception is renaming a tag onto an existing
name — merging would silently lose assignments, so that reports a conflict.

**Notes protect authorship.** Edits carry the version the UI displayed. If the
note changed in between, the write is refused and the dialog shows the text
that would have been overwritten instead of losing it.

Every failure the backend returns carries a human-readable `message`, which the
client shows verbatim in an error modal. Anything an `ApiClient` call does not
handle explicitly reaches `MainWindow::onApiError` and becomes a modal, so no
failure is silent.

### Running it

The client needs a backend. See **[`backend/README.md`](backend/README.md)** for
setup; the short version:

```bash
cd backend
python -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env          # set PDFORG_JWT_SECRET and the PDFORG_B2_* keys
python init_db.py
uvicorn app.main:app --port 8000
```

Then start the client and sign in; the address goes in the sign-in sheet and
can be changed later under Settings ▸ Account.

To check the client and backend agree on the wire:

```bash
cmake -S . -B build -DBUILD_API_SMOKETEST=ON
cmake --build build --target apiclient_smoketest
./build/apiclient_smoketest http://localhost:8000
```

### Pattern: MVC with dedicated Controllers

```
┌─────────────────────────────────────────────────────┐
│                     MainWindow                       │
│  (assembles all components; wires signals & slots)   │
└──────────┬───────────────────────────────────────────┘
           │
    ┌──────┴──────────────────────────────────────┐
    │                  VIEWS                       │
    │  FolderPanel  ListView  GridView  RecentView │
    └──────┬──────────────────────────────────────┘
           │ signals (fileActivated, editTagsRequested…)
    ┌──────┴──────────────────────────────────────┐
    │              CONTROLLERS                     │
    │  PdfController  TagController  FolderWatcher │
    └──────┬──────────────────────────────────────┘
           │ reads/writes
    ┌──────┴──────────────────────────────────────┐
    │               MODELS                         │
    │  PdfModel  TagModel  FolderModel             │
    │  SearchFilterProxy (QSortFilterProxyModel)   │
    └──────┬──────────────────────────────────────┘
           │ persists
    ┌──────┴──────────────────────────────────────┐
    │             INFRASTRUCTURE                   │
    │  DatabaseManager (SQLite)                    │
    │  ThumbnailGenerator (QtConcurrent)           │
    │  PdfOpener (Okular / QDesktopServices)       │
    └─────────────────────────────────────────────┘
```

### Key design decisions

**Qt Widgets over QML** — chosen for:
- Native OS integration (file dialogs, context menus, accessibility)
- Mature, stable API with predictable rendering
- Simpler deployment (no QML runtime / type registration overhead)
- Better fit for a data-heavy, list/grid-focused UI

**MVC split**
- Models (`PdfModel`, `TagModel`, `FolderModel`) own the *in-memory* data and
  implement the standard Qt item model interface. They are view-agnostic.
- Controllers coordinate between models, database, and external services.
  No controller touches a widget.
- Views only emit signals upward; they never call the database directly.

**Async everywhere**
- Folder scanning: `QtConcurrent::run` → result delivered via
  `QFutureWatcher::finished` on the main thread.
- Thumbnail rendering: same pattern — worker thread renders, main thread
  calls `PdfModel::setThumbnail`.

---

## Project Structure

```
PDFOrganizer/
├── CMakeLists.txt
├── README.md
├── resources/
│   └── resources.qrc
└── src/
    ├── main.cpp
    ├── mainwindow.h / .cpp
    ├── models/
    │   ├── pdffile.h / .cpp          # Domain value type
    │   ├── pdfmodel.h / .cpp         # QAbstractTableModel
    │   ├── tagmodel.h / .cpp         # QAbstractListModel
    │   └── foldermodel.h / .cpp      # QAbstractListModel
    ├── database/
    │   └── databasemanager.h / .cpp  # SQLite via Qt SQL
    ├── controllers/
    │   ├── folderwatcher.h / .cpp    # Scan + QFileSystemWatcher
    │   ├── pdfcontroller.h / .cpp    # Open PDF, track lastOpened
    │   └── tagcontroller.h / .cpp    # Tag CRUD + assignment
    ├── api/                          # ← every backend call lives here
    │   ├── apiclient.h / .cpp        # Async REST client, token refresh
    │   └── apitypes.h / .cpp         # DTOs + ApiError
    ├── views/
    │   ├── folderpanel.h / .cpp      # Left sidebar
    │   ├── logindialog.h / .cpp      # Sign in / create account
    │   ├── listview.h / .cpp         # Table view wrapper
    │   ├── gridview.h / .cpp         # Icon/card view wrapper
    │   ├── recentview.h / .cpp       # Dock: recently opened
    │   ├── tagmanagerdialog.h / .cpp # A group's tag vocabulary
    │   └── settingsdialog.h / .cpp   # Dark mode, default view, server
    ├── delegates/
    │   ├── listdelegate.h / .cpp     # Custom row painter
    │   └── griddelegate.h / .cpp     # Custom card painter
    └── utils/
        ├── pdfopener.h / .cpp        # Okular + fallback
        ├── thumbnailgenerator.h / .cpp # Async Qt::Pdf / placeholder
        └── searchfilterproxy.h / .cpp  # Name + tag filter proxy
```

---

## Building

### Prerequisites

| Dependency | Minimum version | Notes |
|---|---|---|
| CMake | 3.21 | |
| C++ compiler | GCC 10 / Clang 12 / MSVC 2019 | C++17 required |
| Qt | 6.2 | Core, Widgets, Sql, Concurrent, Network |
| Python | 3.11 | Backend only — see `backend/README.md` |
| PostgreSQL | 14 | Backend only |
| Qt PDF | 6.4 (optional) | Enables real PDF thumbnails |
| SQLite | bundled with Qt | Via `QSQLITE` driver |

### Linux (Ubuntu / Debian)

```bash
# Install Qt6 and build tools
sudo apt install cmake ninja-build \
    qt6-base-dev qt6-base-dev-tools \
    libqt6sql6-sqlite

# Optional: real PDF thumbnails
sudo apt install qt6-pdf-dev

# Clone and build
git clone <repo-url> PDFOrganizer
cd PDFOrganizer
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/PDFOrganizer
```

### macOS (Homebrew)

```bash
brew install cmake ninja qt@6
export PATH="$(brew --prefix qt@6)/bin:$PATH"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
cmake --build build -j$(sysctl -n hw.logicalcpu)
open build/PDFOrganizer.app
```

### Windows (MSVC + Qt Installer)

```powershell
# Assumes Qt 6 installed to C:\Qt\6.x.x\msvc2022_64
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\msvc2022_64"
cmake --build build --config Release
```

---

## Database Schema

```sql
-- Root folders the user has added
CREATE TABLE folders (id INTEGER PRIMARY KEY, path TEXT UNIQUE);

-- Global tag vocabulary
CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT UNIQUE COLLATE NOCASE);

-- One row per discovered PDF
CREATE TABLE pdf_files (
    id            INTEGER PRIMARY KEY,
    path          TEXT UNIQUE,
    folder_path   TEXT,
    file_name     TEXT,
    file_size     INTEGER,
    last_modified TEXT,   -- ISO 8601
    last_opened   TEXT,   -- ISO 8601
    page_count    INTEGER
);

-- Many-to-many: PDF ↔ Tag
CREATE TABLE pdf_tags (
    pdf_id INTEGER REFERENCES pdf_files(id) ON DELETE CASCADE,
    tag_id INTEGER REFERENCES tags(id)      ON DELETE CASCADE,
    PRIMARY KEY (pdf_id, tag_id)
);

-- Key-value application settings
CREATE TABLE settings (key TEXT UNIQUE, value TEXT);

-- SHA-256 of each file's contents: the identity the backend keys files by,
-- cached so a file is hashed once rather than on every sync
CREATE TABLE file_hashes (
    path          TEXT PRIMARY KEY,
    content_hash  TEXT NOT NULL,
    file_size     INTEGER NOT NULL,
    last_modified TEXT
);

-- Maps a content hash to the id the backend assigned it within a group
CREATE TABLE remote_files (
    group_id       INTEGER NOT NULL,
    content_hash   TEXT    NOT NULL,
    remote_file_id INTEGER NOT NULL,
    PRIMARY KEY (group_id, content_hash)
);

-- Which backend group holds a directory's PDFs. One row per directory that
-- holds a PDF directly — subdirectories get their own rows and their own
-- groups. Cleared on sign-out along with the ids above, then re-attached by
-- group name on the way back in.
CREATE TABLE folder_groups (
    folder_path TEXT    PRIMARY KEY,
    group_id    INTEGER NOT NULL
);
```

---

## Extending the Project

### Adding a new column to the list view
1. Add a constant to `PdfModel::Column` enum.
2. Handle it in `PdfModel::data()` and `PdfModel::headerData()`.
3. Configure column width in `ListView::buildUi()`.

### Adding a new setting
```cpp
// Write
m_db->setSetting("myFeature", true);

// Read (with default)
bool enabled = m_db->getSetting("myFeature", false).toBool();
```

### Supporting a different PDF viewer
```cpp
// In PdfOpener::open():
if (QStandardPaths::findExecutable("evince").isEmpty() == false)
    return QProcess::startDetached("evince", {filePath});
```

### Enabling real thumbnails
Install `qt6-pdf-dev` (Linux) or the Qt PDF module, then CMake will detect
`Qt6::Pdf` automatically and define `HAVE_QT_PDF`. `ThumbnailGenerator`
will use `QPdfDocument` instead of the styled placeholder.

---

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Add folder |
| `Ctrl+1` | Switch to List view |
| `Ctrl+2` | Switch to Grid view |
| `Enter` / double-click | Open selected PDF |
| `Ctrl+Q` | Quit |

---

## License

MIT — see `LICENSE` for details.
