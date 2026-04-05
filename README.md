# PDF Organizer

A modern, dark-themed desktop application for browsing, tagging, and organising
PDF files — built with **C++17** and **Qt 6** (Widgets).

---

## Feature Overview

| Feature | Details |
|---|---|
| **Folder management** | Add/remove root folders; recursive auto-scan; drag & drop folders onto the panel |
| **Live file watching** | `QFileSystemWatcher` detects new / deleted PDFs without manual refresh |
| **External PDF viewer** | Opens with Okular (Linux) or the OS default (Windows / macOS fallback) |
| **Tagging** | Create, rename, delete global tags; assign multiple tags per PDF; cascading delete |
| **List view** | Sortable table: name · folder · tags · last opened · size |
| **Grid / card view** | Thumbnail cards with tag pills; async thumbnail generation |
| **Search** | Live filter by filename and/or tag text |
| **Tag filter** | One-click chips in the left panel (AND semantics) |
| **Recent activity** | Bottom dock showing the 20 most recently opened PDFs |
| **Dark mode** | Full stylesheet; persisted per-user; toggled in Settings |
| **Persistent settings** | SQLite for metadata; `QSettings` for window geometry |

---

## Architecture

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
    ├── views/
    │   ├── folderpanel.h / .cpp      # Left sidebar
    │   ├── listview.h / .cpp         # Table view wrapper
    │   ├── gridview.h / .cpp         # Icon/card view wrapper
    │   ├── recentview.h / .cpp       # Dock: recently opened
    │   ├── tagmanagerdialog.h / .cpp # Create/rename/delete tags
    │   └── settingsdialog.h / .cpp   # Dark mode, default view
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
| Qt | 6.2 | Core, Widgets, Sql, Concurrent |
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
