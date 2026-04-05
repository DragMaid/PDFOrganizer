#include "mainwindow.h"

// ── Models ────────────────────────────────────────────────────────────────────
#include "models/pdfmodel.h"
#include "models/tagmodel.h"
#include "models/foldermodel.h"

// ── Infrastructure ────────────────────────────────────────────────────────────
#include "database/databasemanager.h"
#include "controllers/folderwatcher.h"
#include "controllers/pdfcontroller.h"
#include "controllers/tagcontroller.h"
#include "utils/searchfilterproxy.h"

// ── Views ─────────────────────────────────────────────────────────────────────
#include "views/folderpanel.h"
#include "views/listview.h"
#include "views/gridview.h"
#include "views/recentview.h"
#include "views/tagmanagerdialog.h"
#include "views/settingsdialog.h"

// ── Qt ────────────────────────────────────────────────────────────────────────
#include <QApplication>
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QSplitter>
#include <QLineEdit>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <QDebug>
#include <QDialog>
#include <QVBoxLayout>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PDF Organizer"));
    setMinimumSize(900, 600);

    initModels();
    initControllers();
    initViews();
    initMenuBar();
    initToolBar();
    initStatusBar();
    initDockWidgets();
    connectSignals();

    // Load persisted data
    m_pdfCtrl->initialize();
    m_tagCtrl->initialize();

    // Restore folders into watcher
    const QStringList savedFolders = m_db->loadFolders();
    for (const QString& f : savedFolders)
        m_folderModel->addFolder(f);

    // Restore window geometry + dark mode
    restoreLayout();
}

MainWindow::~MainWindow()
{
    saveLayout();
    m_db->close();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialisation helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::initModels()
{
    m_pdfModel    = new PdfModel   (this);
    m_tagModel    = new TagModel   (this);
    m_folderModel = new FolderModel(this);

    m_proxy = new SearchFilterProxy(this);
    m_proxy->setSourceModel(m_pdfModel);
    m_proxy->setSortRole(PdfModel::FileNameRole);
}

void MainWindow::initControllers()
{
    m_db = new DatabaseManager(this);
    if (!m_db->open(dbPath())) {
        QMessageBox::critical(this, QStringLiteral("Database Error"),
                              QStringLiteral("Could not open the application database.\n")
                              + dbPath());
    }

    m_watcher = new FolderWatcher(this);
    m_pdfCtrl = new PdfController(m_pdfModel, m_db, m_watcher, this);
    m_tagCtrl = new TagController(m_tagModel, m_pdfModel, m_db, this);
}

void MainWindow::initViews()
{
    // ── Central splitter ──────────────────────────────────────────────────────
    m_splitter    = new QSplitter(Qt::Horizontal, this);
    m_folderPanel = new FolderPanel(m_folderModel, m_tagModel, m_splitter);

    m_viewStack   = new QStackedWidget(m_splitter);
    m_listView    = new ListView(m_pdfModel, m_proxy, m_viewStack);
    m_gridView    = new GridView(m_pdfModel, m_proxy, m_viewStack);

    m_viewStack->addWidget(m_listView);   // index 0 = list
    m_viewStack->addWidget(m_gridView);   // index 1 = grid

    m_splitter->addWidget(m_folderPanel);
    m_splitter->addWidget(m_viewStack);
    m_splitter->setStretchFactor(0, 0);   // folder panel: fixed
    m_splitter->setStretchFactor(1, 1);   // main content: stretch
    m_splitter->setSizes({220, 700});

    setCentralWidget(m_splitter);
}

void MainWindow::initToolBar()
{
    QToolBar* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setIconSize(QSize(18, 18));

    // ── Search ────────────────────────────────────────────────────────────────
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search by name or tag…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMinimumWidth(260);
    m_searchEdit->setMaximumWidth(400);
    tb->addWidget(m_searchEdit);

    tb->addSeparator();

    // ── View toggle ───────────────────────────────────────────────────────────
    auto* viewGroup = new QActionGroup(this);
    viewGroup->setExclusive(true);

    m_listAction = new QAction(QStringLiteral("☰  List"), this);
    m_listAction->setCheckable(true);
    m_listAction->setChecked(true);
    m_listAction->setToolTip(QStringLiteral("Switch to List View"));

    m_gridAction = new QAction(QStringLiteral("⊞  Grid"), this);
    m_gridAction->setCheckable(true);
    m_gridAction->setToolTip(QStringLiteral("Switch to Grid View"));

    viewGroup->addAction(m_listAction);
    viewGroup->addAction(m_gridAction);

    tb->addAction(m_listAction);
    tb->addAction(m_gridAction);

    tb->addSeparator();

    // ── Secondary actions ─────────────────────────────────────────────────────
    QAction* tagMgrAct = new QAction(QStringLiteral("🏷  Tags"), this);
    tagMgrAct->setToolTip(QStringLiteral("Manage Tags"));
    tb->addAction(tagMgrAct);
    connect(tagMgrAct, &QAction::triggered, this, &MainWindow::openTagManager);

    QAction* settingsAct = new QAction(QStringLiteral("⚙  Settings"), this);
    settingsAct->setToolTip(QStringLiteral("Preferences"));
    tb->addAction(settingsAct);
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);

    // ── Connect view toggle ───────────────────────────────────────────────────
    connect(m_listAction, &QAction::triggered, this, &MainWindow::switchToListView);
    connect(m_gridAction, &QAction::triggered, this, &MainWindow::switchToGridView);
}

void MainWindow::initMenuBar()
{
    // ── File ──────────────────────────────────────────────────────────────────
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

    QAction* addFolderAct = fileMenu->addAction(QStringLiteral("Add &Folder…"));
    addFolderAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(addFolderAct, &QAction::triggered, this, &MainWindow::onAddFolderRequested);

    fileMenu->addSeparator();

    QAction* quitAct = fileMenu->addAction(QStringLiteral("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    // ── View ──────────────────────────────────────────────────────────────────
    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

    QAction* listViewAct = viewMenu->addAction(QStringLiteral("&List View"));
    listViewAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(listViewAct, &QAction::triggered, this, &MainWindow::switchToListView);

    QAction* gridViewAct = viewMenu->addAction(QStringLiteral("&Grid View"));
    gridViewAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(gridViewAct, &QAction::triggered, this, &MainWindow::switchToGridView);

    viewMenu->addSeparator();

    QAction* recentAct = viewMenu->addAction(QStringLiteral("Toggle &Recent Panel"));
    connect(recentAct, &QAction::triggered, this, [this]() {
        if (m_recentDock) m_recentDock->setVisible(!m_recentDock->isVisible());
    });

    // ── Tags ──────────────────────────────────────────────────────────────────
    QMenu* tagMenu = menuBar()->addMenu(QStringLiteral("&Tags"));
    QAction* tagMgrAct = tagMenu->addAction(QStringLiteral("&Manage Tags…"));
    connect(tagMgrAct, &QAction::triggered, this, &MainWindow::openTagManager);

    // ── Tools ─────────────────────────────────────────────────────────────────
    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("T&ools"));
    QAction* rescanAct = toolsMenu->addAction(QStringLiteral("&Rescan All Folders"));
    connect(rescanAct, &QAction::triggered, m_watcher, &FolderWatcher::rescanAll);

    toolsMenu->addSeparator();
    QAction* settingsAct = toolsMenu->addAction(QStringLiteral("&Settings…"));
    connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);
}

void MainWindow::initStatusBar()
{
    m_statusLabel = new QLabel(QStringLiteral("0 files"), this);
    m_scanLabel   = new QLabel(this);
    m_scanLabel->setObjectName(QStringLiteral("scanLabel"));

    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_scanLabel);
}

void MainWindow::initDockWidgets()
{
    m_recentView = new RecentView(m_pdfModel, this);
    m_recentDock = new QDockWidget(QStringLiteral("Recently Opened"), this);
    m_recentDock->setObjectName(QStringLiteral("recentDock"));
    m_recentDock->setWidget(m_recentView);
    m_recentDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_recentDock);
    m_recentDock->setMaximumHeight(180);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Signal wiring
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::connectSignals()
{
    // Search
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &MainWindow::onSearchTextChanged);

    // Folder panel → controller / model
    connect(m_folderPanel, &FolderPanel::addFolderRequested,
            this, &MainWindow::onAddFolderRequested);
    connect(m_folderPanel, &FolderPanel::removeFolderRequested,
            this, &MainWindow::onRemoveFolderRequested);
    connect(m_folderPanel, &FolderPanel::folderSelected,
            m_proxy, &SearchFilterProxy::setFolderFilter);
    connect(m_folderPanel, &FolderPanel::tagsSelected,
            m_proxy, &SearchFilterProxy::setActiveTags);

    // List view
    connect(m_listView, &ListView::fileActivated,
            this, &MainWindow::onFileActivated);
    connect(m_listView, &ListView::editTagsRequested,
            this, &MainWindow::onEditTagsRequested);

    // Grid view
    connect(m_gridView, &GridView::fileActivated,
            this, &MainWindow::onFileActivated);
    connect(m_gridView, &GridView::editTagsRequested,
            this, &MainWindow::onEditTagsRequested);
    connect(m_gridView, &GridView::thumbnailNeeded,
            m_pdfCtrl, &PdfController::requestThumbnail);

    // Recent view
    connect(m_recentView, &RecentView::fileActivated,
            this, &MainWindow::onFileActivated);

    // PDF controller
    connect(m_pdfCtrl, &PdfController::pdfOpened,
            this, &MainWindow::onPdfOpened);
    connect(m_pdfCtrl, &PdfController::errorOccurred,
            this, [this](const QString& msg) {
                QMessageBox::warning(this, QStringLiteral("Error"), msg);
            });

    // Model → status bar
    connect(m_pdfModel, &QAbstractItemModel::modelReset,   this, &MainWindow::updateStatusBar);
    connect(m_pdfModel, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateStatusBar);
    connect(m_pdfModel, &QAbstractItemModel::rowsRemoved,  this, &MainWindow::updateStatusBar);

    // Scan status
    connect(m_watcher, &FolderWatcher::scanFinished, this, &MainWindow::onScanFinished);

    // Folder model ↔ watcher
    connect(m_folderModel, &FolderModel::folderAdded, m_watcher, &FolderWatcher::addRootFolder);
    connect(m_folderModel, &FolderModel::folderRemoved, m_watcher, &FolderWatcher::removeRootFolder);

    // Tag model changes → refresh folder panel chips
    connect(m_tagModel, &QAbstractItemModel::modelReset,   m_folderPanel, &FolderPanel::refresh);
    connect(m_tagModel, &QAbstractItemModel::rowsInserted, m_folderPanel, &FolderPanel::refresh);
    connect(m_tagModel, &QAbstractItemModel::rowsRemoved,  m_folderPanel, &FolderPanel::refresh);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Slots
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::switchToListView()
{
    m_viewStack->setCurrentIndex(0);
    m_listAction->setChecked(true);
    m_db->setSetting(QStringLiteral("defaultView"), QStringLiteral("list"));
}

void MainWindow::switchToGridView()
{
    m_viewStack->setCurrentIndex(1);
    m_gridAction->setChecked(true);
    m_gridView->triggerThumbnailLoad();
    m_db->setSetting(QStringLiteral("defaultView"), QStringLiteral("grid"));
}

void MainWindow::onAddFolderRequested()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Select Folder to Watch"),
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation));

    if (dir.isEmpty()) return;

    if (m_folderModel->hasFolder(dir)) {
        QMessageBox::information(this, QStringLiteral("Already Added"),
                                 QStringLiteral("This folder is already being watched."));
        return;
    }

    m_db->saveFolder(dir);
    m_folderModel->addFolder(dir);   // triggers watcher via signal
}

void MainWindow::onRemoveFolderRequested(const QString& path)
{
    const auto reply = QMessageBox::question(
        this,
        QStringLiteral("Remove Folder"),
        QStringLiteral("Stop watching '%1'?\n\nPDF records will be kept in the database.")
            .arg(path),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply != QMessageBox::Yes) return;

    m_db->deleteFolder(path);
    m_folderModel->removeFolder(path);   // triggers watcher via signal
}

void MainWindow::onFileActivated(const QString& filePath)
{
    m_pdfCtrl->openPdf(filePath);
}

void MainWindow::onEditTagsRequested(const QString& filePath)
{
    const PdfFile f = m_pdfModel->fileByPath(filePath);
    if (!f.isValid()) return;

    // Build a simple tag-assignment dialog
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Edit Tags — %1").arg(f.fileName));
    dlg.setMinimumWidth(320);

    auto* layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(
        QStringLiteral("Select tags for this file:"), &dlg));

    auto* listWidget = new QListWidget(&dlg);
    listWidget->setSelectionMode(QAbstractItemView::MultiSelection);

    const QStringList allTags = m_tagModel->allTags();
    for (const QString& tag : allTags) {
        auto* item = new QListWidgetItem(tag, listWidget);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(f.hasTag(tag) ? Qt::Checked : Qt::Unchecked);
    }

    layout->addWidget(listWidget);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    QStringList selected;
    for (int i = 0; i < listWidget->count(); ++i) {
        auto* item = listWidget->item(i);
        if (item->checkState() == Qt::Checked)
            selected << item->text();
    }

    m_tagCtrl->setFileTags(filePath, selected);
}

void MainWindow::onPdfOpened(const QString& /*filePath*/, const QDateTime& /*when*/)
{
    m_recentView->refresh();
    updateStatusBar();
}

void MainWindow::onSearchTextChanged(const QString& text)
{
    m_proxy->setSearchText(text);
    updateStatusBar();
}

void MainWindow::openTagManager()
{
    TagManagerDialog dlg(m_tagModel, m_tagCtrl, this);
    dlg.exec();
    m_folderPanel->refresh();
}

void MainWindow::openSettings()
{
    SettingsDialog dlg(m_db, this);
    connect(&dlg, &SettingsDialog::darkModeChanged, this, &MainWindow::applyDarkTheme);
    dlg.exec();
}

void MainWindow::updateStatusBar()
{
    const int total   = m_pdfModel->totalCount();
    const int visible = m_proxy->rowCount();

    if (total == visible)
        m_statusLabel->setText(QStringLiteral("%1 file%2")
                               .arg(total).arg(total == 1 ? QLatin1String("") : QLatin1String("s")));
    else
        m_statusLabel->setText(QStringLiteral("%1 of %2 files").arg(visible).arg(total));
}

void MainWindow::onScanFinished(const QString& folder)
{
    m_scanLabel->setText(
        QStringLiteral("✓ Scanned: %1").arg(QDir(folder).dirName()));

    // Clear the message after 3 seconds
    QTimer::singleShot(3000, m_scanLabel, [this]() { m_scanLabel->clear(); });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dark theme
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::applyDarkTheme(bool enabled)
{
    if (!enabled) {
        qApp->setStyleSheet(QString{});
        return;
    }

    qApp->setStyleSheet(QStringLiteral(R"(
        QWidget {
            background-color: #1e2024;
            color: #e0e3e8;
            font-family: "Segoe UI", "SF Pro Text", "Helvetica Neue", sans-serif;
            font-size: 9pt;
        }
        QMainWindow, QDialog { background-color: #1e2024; }

        /* ── Toolbar ── */
        QToolBar {
            background: #27292e;
            border-bottom: 1px solid #30333a;
            spacing: 4px;
            padding: 3px 6px;
        }
        QToolBar QToolButton {
            background: transparent;
            border: none;
            border-radius: 4px;
            padding: 4px 10px;
            color: #b0b3b8;
        }
        QToolBar QToolButton:hover   { background: #30333a; color: #e0e3e8; }
        QToolBar QToolButton:checked { background: #2b4060; color: #6ea8ff; }

        /* ── Search box ── */
        QLineEdit {
            background: #2d3035;
            border: 1px solid #3a3d42;
            border-radius: 6px;
            padding: 5px 10px;
            color: #e0e3e8;
            selection-background-color: #2b4060;
        }
        QLineEdit:focus { border-color: #4d8eff; }

        /* ── Scroll bars ── */
        QScrollBar:vertical {
            background: #1e2024; width: 8px; margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3d42; border-radius: 4px; min-height: 24px;
        }
        QScrollBar::handle:vertical:hover { background: #4a4d52; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QScrollBar:horizontal {
            background: #1e2024; height: 8px; margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #3a3d42; border-radius: 4px; min-width: 24px;
        }
        QScrollBar::handle:horizontal:hover { background: #4a4d52; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        /* ── Menu bar ── */
        QMenuBar {
            background: #27292e;
            border-bottom: 1px solid #30333a;
        }
        QMenuBar::item { padding: 4px 10px; background: transparent; }
        QMenuBar::item:selected { background: #2b4060; border-radius: 4px; }

        QMenu {
            background: #27292e;
            border: 1px solid #3a3d42;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item { padding: 6px 20px; border-radius: 4px; }
        QMenu::item:selected { background: #2b4060; }
        QMenu::separator { height: 1px; background: #3a3d42; margin: 3px 6px; }

        /* ── Table / list views ── */
        QTableView, QListView, QListWidget {
            background: #1e2024;
            alternate-background-color: #222428;
            border: none;
            outline: none;
        }
        QTableView::item:selected, QListView::item:selected,
        QListWidget::item:selected {
            background: #2b4060;
            color: #e0e3e8;
        }
        QHeaderView::section {
            background: #27292e;
            color: #8a8d95;
            border: none;
            border-bottom: 1px solid #30333a;
            padding: 6px 10px;
            font-weight: bold;
            font-size: 8pt;
            text-transform: uppercase;
        }
        QHeaderView::section:hover { background: #2d3035; color: #e0e3e8; }

        /* ── Splitter ── */
        QSplitter::handle { background: #30333a; width: 1px; height: 1px; }

        /* ── Dock ── */
        QDockWidget {
            titlebar-close-icon: none;
            titlebar-normal-icon: none;
            background: #1e2024;
        }
        QDockWidget::title {
            background: #27292e;
            padding: 6px 10px;
            border-bottom: 1px solid #30333a;
            font-weight: bold;
            font-size: 8pt;
            color: #8a8d95;
            text-transform: uppercase;
        }

        /* ── Status bar ── */
        QStatusBar {
            background: #27292e;
            border-top: 1px solid #30333a;
            color: #7a7d85;
            font-size: 8pt;
        }

        /* ── Buttons ── */
        QPushButton {
            background: #2d3035;
            border: 1px solid #3a3d42;
            border-radius: 5px;
            padding: 5px 14px;
            color: #e0e3e8;
        }
        QPushButton:hover   { background: #30333a; border-color: #4a4d52; }
        QPushButton:pressed { background: #2b4060; border-color: #4d8eff; }
        QPushButton#dangerButton {
            background: #4a1a1a; border-color: #8b2020; color: #ff6b6b;
        }
        QPushButton#dangerButton:hover { background: #5a2020; border-color: #b02020; }
        QPushButton#iconButton {
            background: transparent; border: none;
            font-size: 14pt; color: #7a7d85;
        }
        QPushButton#iconButton:hover { color: #4d8eff; }

        /* ── Section labels ── */
        QLabel#sectionLabel {
            color: #5a5d65;
            font-size: 8pt;
            font-weight: bold;
            letter-spacing: 1px;
        }
        QWidget#sectionHeader { background: #1e2024; }

        /* ── Input / combo ── */
        QComboBox {
            background: #2d3035;
            border: 1px solid #3a3d42;
            border-radius: 5px;
            padding: 4px 8px;
            color: #e0e3e8;
        }
        QComboBox::drop-down { border: none; width: 18px; }
        QComboBox QAbstractItemView {
            background: #27292e;
            border: 1px solid #3a3d42;
            selection-background-color: #2b4060;
        }

        /* ── Group boxes ── */
        QGroupBox {
            border: 1px solid #3a3d42;
            border-radius: 6px;
            margin-top: 12px;
            padding: 12px 8px 8px 8px;
            color: #8a8d95;
            font-size: 8pt;
            font-weight: bold;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            padding: 0 4px;
        }

        /* ── Checkbox ── */
        QCheckBox { color: #e0e3e8; spacing: 6px; }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border: 1px solid #3a3d42;
            border-radius: 3px;
            background: #2d3035;
        }
        QCheckBox::indicator:checked {
            background: #4d8eff;
            border-color: #4d8eff;
        }

        /* ── Dialog button box ── */
        QDialogButtonBox QPushButton {
            min-width: 80px;
        }

        /* ── Scan label ── */
        QLabel#scanLabel { color: #3a8a3a; font-size: 8pt; }
    )"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout persistence
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::restoreLayout()
{
    QSettings qs;
    restoreGeometry(qs.value(QStringLiteral("geometry")).toByteArray());
    restoreState   (qs.value(QStringLiteral("windowState")).toByteArray());

    // Dark mode default ON
    const bool dark = m_db->getSetting(QStringLiteral("darkMode"), true).toBool();
    applyDarkTheme(dark);

    // Default view
    const QString view = m_db->getSetting(
        QStringLiteral("defaultView"), QStringLiteral("list")).toString();
    if (view == QLatin1String("grid"))
        switchToGridView();
    else
        switchToListView();
}

void MainWindow::saveLayout()
{
    QSettings qs;
    qs.setValue(QStringLiteral("geometry"),    saveGeometry());
    qs.setValue(QStringLiteral("windowState"), saveState());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveLayout();
    event->accept();
}

QString MainWindow::dbPath() const
{
    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir().mkpath(appData);
    return appData + QStringLiteral("/pdforganizer.db");
}
