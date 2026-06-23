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
#include <QFormLayout>
#include <QInputDialog>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTextEdit>
#include <QUrl>

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
    m_pdfModel        = new PdfModel   (this);
    m_tagModel        = new TagModel   (this);
    m_folderModel     = new FolderModel(this);
    m_folderTreeModel = new FolderTreeModel(this);

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
    // ── Central splitter (3-way: folders | main view | recent) ─────────────────
    m_splitter    = new QSplitter(Qt::Horizontal, this);
    m_folderPanel = new FolderPanel(m_folderTreeModel, m_tagModel, m_splitter);

    m_viewStack   = new QStackedWidget(m_splitter);
    m_listView    = new ListView(m_pdfModel, m_proxy, m_viewStack);
    m_gridView    = new GridView(m_pdfModel, m_proxy, m_viewStack);

    m_viewStack->addWidget(m_listView);   // index 0 = list
    m_viewStack->addWidget(m_gridView);   // index 1 = grid

    m_rightTabs = new QTabWidget(m_splitter);
    m_rightTabs->addTab(buildDetailPane(), QStringLiteral("Details"));
    m_recentView = new RecentView(m_pdfModel, m_rightTabs);
    m_rightTabs->addTab(m_recentView, QStringLiteral("Recent"));

    m_splitter->addWidget(m_folderPanel);
    m_splitter->addWidget(m_viewStack);
    m_splitter->addWidget(m_rightTabs);

    m_splitter->setStretchFactor(0, 0);   // folder panel:  fixed (220px)
    m_splitter->setStretchFactor(1, 1);   // main content:  stretch
    m_splitter->setStretchFactor(2, 0);   // recent panel:  fixed (240px)
    m_splitter->setSizes({220, 700, 280});

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

    // ── Help ──────────────────────────────────────────────────────────────────
    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction* dataLocAct = helpMenu->addAction(QStringLiteral("Show &Data Location"));
    connect(dataLocAct, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QStringLiteral("Data Location"),
                                 QStringLiteral("Application data is stored at:\n\n%1")
                                 .arg(dataDir()));
    });
}

void MainWindow::initStatusBar()
{
    m_statusLabel = new QLabel(QStringLiteral("0 files"), this);
    m_scanLabel   = new QLabel(this);
    m_scanLabel->setObjectName(QStringLiteral("scanLabel"));

    statusBar()->addWidget(m_statusLabel);
    statusBar()->addPermanentWidget(m_scanLabel);
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
    connect(m_listView, &ListView::fileSelected,
            this, &MainWindow::onFileSelected);
    connect(m_listView, &ListView::editTagsRequested,
            this, &MainWindow::onEditTagsRequested);
    connect(m_listView, &ListView::thumbnailNeeded,
            m_pdfCtrl, &PdfController::requestThumbnail);

    // Grid view
    connect(m_gridView, &GridView::fileActivated,
            this, &MainWindow::onFileActivated);
    connect(m_gridView, &GridView::fileSelected,
            this, &MainWindow::onFileSelected);
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

    // Folder model ↔ tree model (for sidebar hierarchy display)
    connect(m_folderModel, &FolderModel::folderAdded, m_folderTreeModel, &FolderTreeModel::addRootFolder);
    connect(m_folderModel, &FolderModel::folderRemoved, m_folderTreeModel, &FolderTreeModel::removeRootFolder);

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
        QStringLiteral("Stop watching '%1'?\n\nPDF records in this folder will be removed.")
            .arg(path),
        QMessageBox::Yes | QMessageBox::Cancel);

    if (reply != QMessageBox::Yes) return;

    // Remove all PDFs from this folder and its subfolders
    const QList<PdfFile> allFiles = m_pdfModel->allFiles();
    for (const PdfFile& f : allFiles) {
        if (f.folderPath.startsWith(path)) {
            m_pdfModel->removeFile(f.filePath);
            m_db->deleteFile(f.filePath);
        }
    }

    m_db->deleteFolder(path);
    m_folderModel->removeFolder(path);   // triggers watcher via signal
}

void MainWindow::onFileActivated(const QString& filePath)
{
    m_pdfCtrl->openPdf(filePath);
}

void MainWindow::onFileSelected(const QString& filePath)
{
    m_selectedFilePath = filePath;
    refreshDetailPane();
    if (m_rightTabs)
        m_rightTabs->setCurrentIndex(0);
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

void MainWindow::onAddNote()
{
    const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
    if (!f.isValid()) return;

    const QString author = m_db->getSetting(QStringLiteral("githubUser"), QStringLiteral("local")).toString().trimmed();
    if (author.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("GitHub User Required"),
                             QStringLiteral("Set your GitHub username in Settings before adding notes."));
        return;
    }

    if (m_db->addNote(f.id, author, m_noteEdit->toPlainText())) {
        m_noteEdit->clear();
        refreshDetailPane();
    }
}

void MainWindow::onCreateGroup()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Group"),
                                               QStringLiteral("Group name:"), QLineEdit::Normal,
                                               QString{}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (m_db->createGroup(name) < 0) {
        QMessageBox::warning(this, QStringLiteral("Group Exists"),
                             QStringLiteral("Could not create that group."));
        return;
    }
    refreshDetailPane();
}

void MainWindow::onGroupItemChanged(QListWidgetItem* item)
{
    const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
    if (!f.isValid() || !item) return;
    m_db->setFileInGroup(f.id, item->data(Qt::UserRole).toInt(), item->checkState() == Qt::Checked);
}

void MainWindow::onValidateGithub()
{
    const int groupId = selectedGroupId();
    if (groupId < 0) return;

    const FileGroup group = m_db->groupById(groupId);
    const QString current = group.githubRepoUrl;
    bool ok = false;
    QString repoUrl = QInputDialog::getText(this, QStringLiteral("GitHub Repo"),
                                            QStringLiteral("Repo URL:"), QLineEdit::Normal,
                                            current, &ok).trimmed();
    if (!ok || repoUrl.isEmpty()) return;

    const QRegularExpression re(QStringLiteral("^https://github\\.com/([^/]+)/([^/.]+)(?:\\.git)?/?$"));
    const QRegularExpressionMatch match = re.match(repoUrl);
    if (!match.hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("Invalid Repo"),
                             QStringLiteral("Use https://github.com/owner/repo."));
        return;
    }

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/%2")
                         .arg(match.captured(1), match.captured(2))));
    req.setRawHeader("User-Agent", "PDFOrganizer");
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const bool valid = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (!valid) {
        QMessageBox::warning(this, QStringLiteral("Repo Not Valid"),
                             QStringLiteral("GitHub could not validate that public repo."));
        return;
    }

    m_db->saveGroupGithubValidation(groupId, repoUrl, QStringLiteral("valid"));
    refreshDetailPane();
}

void MainWindow::onValidateB2()
{
    const int groupId = selectedGroupId();
    if (groupId < 0) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Backblaze B2"));
    auto* form = new QFormLayout(&dlg);
    QLineEdit keyId;
    QLineEdit appKey;
    QLineEdit bucket;
    appKey.setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Key ID:"), &keyId);
    form->addRow(QStringLiteral("App key:"), &appKey);
    form->addRow(QStringLiteral("Bucket:"), &bucket);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    QNetworkRequest req(QUrl(QStringLiteral("https://api.backblazeb2.com/b2api/v4/b2_authorize_account")));
    const QByteArray basic = (keyId.text().trimmed() + QLatin1Char(':') + appKey.text()).toUtf8().toBase64();
    req.setRawHeader("Authorization", "Basic " + basic);
    QNetworkAccessManager nam;
    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const bool valid = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    if (!valid) {
        QMessageBox::warning(this, QStringLiteral("B2 Not Valid"),
                             QStringLiteral("Backblaze rejected those keys."));
        return;
    }

    const QString accountId = QJsonDocument::fromJson(body).object().value(QStringLiteral("accountId")).toString();
    m_db->saveGroupB2Validation(groupId, keyId.text(), bucket.text(), accountId, QStringLiteral("valid"));
    refreshDetailPane();
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

QWidget* MainWindow::buildDetailPane()
{
    auto* pane = new QWidget;
    auto* root = new QVBoxLayout(pane);

    m_detailTitle = new QLabel(QStringLiteral("No file selected"), pane);
    m_detailTitle->setObjectName(QStringLiteral("sectionLabel"));
    m_detailTitle->setWordWrap(true);
    root->addWidget(m_detailTitle);

    m_detailMeta = new QLabel(pane);
    m_detailMeta->setWordWrap(true);
    root->addWidget(m_detailMeta);

    root->addWidget(new QLabel(QStringLiteral("GROUPS"), pane));
    m_groupList = new QListWidget(pane);
    m_groupList->setMinimumHeight(120);
    root->addWidget(m_groupList);

    auto* groupRow = new QHBoxLayout;
    auto* addGroupBtn = new QPushButton(QStringLiteral("Add Group"), pane);
    m_githubBtn = new QPushButton(QStringLiteral("Validate GitHub"), pane);
    m_b2Btn = new QPushButton(QStringLiteral("Validate B2"), pane);
    groupRow->addWidget(addGroupBtn);
    groupRow->addWidget(m_githubBtn);
    groupRow->addWidget(m_b2Btn);
    root->addLayout(groupRow);

    root->addWidget(new QLabel(QStringLiteral("NOTES"), pane));
    m_noteEdit = new QTextEdit(pane);
    m_noteEdit->setPlaceholderText(QStringLiteral("Add a note…"));
    m_noteEdit->setMaximumHeight(90);
    root->addWidget(m_noteEdit);
    m_addNoteBtn = new QPushButton(QStringLiteral("Add Note"), pane);
    root->addWidget(m_addNoteBtn);

    auto* noteScroll = new QScrollArea(pane);
    noteScroll->setWidgetResizable(true);
    noteScroll->setFrameShape(QFrame::NoFrame);
    auto* noteBody = new QWidget(noteScroll);
    m_notesLayout = new QVBoxLayout(noteBody);
    m_notesLayout->addStretch();
    noteScroll->setWidget(noteBody);
    root->addWidget(noteScroll, 1);

    connect(addGroupBtn, &QPushButton::clicked, this, &MainWindow::onCreateGroup);
    connect(m_githubBtn, &QPushButton::clicked, this, &MainWindow::onValidateGithub);
    connect(m_b2Btn, &QPushButton::clicked, this, &MainWindow::onValidateB2);
    connect(m_addNoteBtn, &QPushButton::clicked, this, &MainWindow::onAddNote);
    connect(m_groupList, &QListWidget::itemChanged, this, &MainWindow::onGroupItemChanged);
    connect(m_groupList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        const bool enabled = current && !m_selectedFilePath.isEmpty();
        m_githubBtn->setEnabled(enabled);
        m_b2Btn->setEnabled(enabled);
    });

    return pane;
}

void MainWindow::refreshDetailPane()
{
    const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
    const bool hasFile = f.isValid();
    m_detailTitle->setText(hasFile ? f.fileName : QStringLiteral("No file selected"));
    m_detailMeta->setText(hasFile
        ? QStringLiteral("%1\n%2").arg(f.filePath, f.tags.join(QStringLiteral(", ")))
        : QString{});
    m_groupList->setEnabled(hasFile);
    m_noteEdit->setEnabled(hasFile);
    m_addNoteBtn->setEnabled(hasFile);
    m_githubBtn->setEnabled(hasFile && selectedGroupId() >= 0);
    m_b2Btn->setEnabled(hasFile && selectedGroupId() >= 0);

    {
        const QSignalBlocker blocker(m_groupList);
        m_groupList->clear();
        const QList<int> fileGroups = hasFile ? m_db->fileGroupIds(f.id) : QList<int>{};
        for (const FileGroup& group : m_db->loadGroups()) {
            auto* item = new QListWidgetItem(group.name, m_groupList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setData(Qt::UserRole, group.id);
            item->setToolTip(QStringLiteral("GitHub: %1\nB2: %2")
                             .arg(group.githubStatus, group.b2Status));
            item->setCheckState(fileGroups.contains(group.id) ? Qt::Checked : Qt::Unchecked);
        }
    }

    while (QLayoutItem* item = m_notesLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    if (hasFile) {
        for (const FileNote& note : m_db->loadNotes(f.id)) {
            auto* label = new QLabel(QStringLiteral("%1 · %2\n%3")
                .arg(note.author,
                     note.createdAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd hh:mm")),
                     note.body));
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_notesLayout->addWidget(label);
        }
    }
    m_notesLayout->addStretch();
}

int MainWindow::selectedGroupId() const
{
    QListWidgetItem* item = m_groupList ? m_groupList->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toInt() : -1;
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

QString MainWindow::dataDir() const
{
    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    const QString dataDirPath = appData + QStringLiteral("/data");
    QDir().mkpath(dataDirPath);
    return dataDirPath;
}

QString MainWindow::dbPath() const
{
    return dataDir() + QStringLiteral("/pdforganizer.db");
}
