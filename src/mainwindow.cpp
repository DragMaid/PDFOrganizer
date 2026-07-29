#include "mainwindow.h"

// ── Models
// ────────────────────────────────────────────────────────────────────
#include "models/foldermodel.h"
#include "models/pdfmodel.h"
#include "models/tagmodel.h"
#include "utils/searchfilterproxy.h"

// ── Infrastructure
// ────────────────────────────────────────────────────────────
#include "controllers/folderwatcher.h"
#include "controllers/pdfcontroller.h"
#include "controllers/tagcontroller.h"
#include "database/databasemanager.h"

// ── Backend
// ───────────────────────────────────────────────────────────────────
#include "api/apiclient.h"

// ── Views
// ─────────────────────────────────────────────────────────────────────
#include "views/folderpanel.h"
#include "views/gridview.h"
#include "views/listview.h"
#include "views/logindialog.h"
#include "views/recentview.h"
#include "views/settingsdialog.h"
#include "views/tagmanagerdialog.h"

// ── Qt
// ────────────────────────────────────────────────────────────────────────
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
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
  for (const QString &f : savedFolders)
    m_folderModel->addFolder(f);

  // Restore window geometry + dark mode
  restoreLayout();

  // Connect to the backend once the window is up, so any sign-in sheet has a
  // parent to centre on.
  QTimer::singleShot(0, this, &MainWindow::restoreSessionOrPrompt);
}

MainWindow::~MainWindow() {
  saveLayout();
  m_db->close();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Initialisation helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::initModels() {
  m_pdfModel = new PdfModel(this);
  m_tagModel = new TagModel(this);
  m_folderModel = new FolderModel(this);
  m_folderTreeModel = new FolderTreeModel(this);

  m_proxy = new SearchFilterProxy(this);
  m_proxy->setSourceModel(m_pdfModel);
  m_proxy->setSortRole(PdfModel::FileNameRole);
}

void MainWindow::initControllers() {
  m_db = new DatabaseManager(this);
  if (!m_db->open(dbPath())) {
    QMessageBox::critical(
        this, QStringLiteral("Database Error"),
        QStringLiteral("Could not open the application database.\n") +
            dbPath());
  }

  m_api = new ApiClient(this);

  m_watcher = new FolderWatcher(this);
  m_pdfCtrl = new PdfController(m_pdfModel, m_db, m_watcher, this);
  m_tagCtrl = new TagController(m_tagModel, m_pdfModel, m_db, this);
}

void MainWindow::initViews() {
  // ── Central splitter (3-way: folders | main view | recent) ─────────────────
  m_splitter = new QSplitter(Qt::Horizontal, this);
  m_folderPanel = new FolderPanel(m_folderTreeModel, m_tagModel, m_splitter);

  m_viewStack = new QStackedWidget(m_splitter);
  m_listView = new ListView(m_pdfModel, m_proxy, m_viewStack);
  m_gridView = new GridView(m_pdfModel, m_proxy, m_viewStack);

  m_viewStack->addWidget(m_listView); // index 0 = list
  m_viewStack->addWidget(m_gridView); // index 1 = grid

  m_rightTabs = new QTabWidget(m_splitter);
  m_rightTabs->addTab(buildDetailPane(), QStringLiteral("Details"));
  m_recentView = new RecentView(m_pdfModel, m_rightTabs);
  m_rightTabs->addTab(m_recentView, QStringLiteral("Recent"));

  m_splitter->addWidget(m_folderPanel);
  m_splitter->addWidget(m_viewStack);
  m_splitter->addWidget(m_rightTabs);

  m_splitter->setStretchFactor(0, 0); // folder panel:  fixed (220px)
  m_splitter->setStretchFactor(1, 1); // main content:  stretch
  m_splitter->setStretchFactor(2, 0); // recent panel:  fixed (240px)
  m_splitter->setSizes({220, 700, 280});

  setCentralWidget(m_splitter);
}

void MainWindow::initToolBar() {
  QToolBar *tb = addToolBar(QStringLiteral("Main"));
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

  // ── Active group ──────────────────────────────────────────────────────────
  // The permission context for everything shared: tags land in this group's
  // vocabulary and notes are visible to exactly this group's members.
  tb->addWidget(new QLabel(QStringLiteral(" Group: "), this));
  m_groupCombo = new QComboBox(this);
  m_groupCombo->setMinimumWidth(160);
  m_groupCombo->setToolTip(
      QStringLiteral("Tags and notes you add apply to this group"));
  tb->addWidget(m_groupCombo);
  connect(m_groupCombo, &QComboBox::currentIndexChanged, this,
          &MainWindow::onActiveGroupChanged);

  tb->addSeparator();

  // ── View toggle ───────────────────────────────────────────────────────────
  auto *viewGroup = new QActionGroup(this);
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
  QAction *tagMgrAct = new QAction(QStringLiteral("🏷  Tags"), this);
  tagMgrAct->setToolTip(QStringLiteral("Manage Tags"));
  tb->addAction(tagMgrAct);
  connect(tagMgrAct, &QAction::triggered, this, &MainWindow::openTagManager);

  QAction *settingsAct = new QAction(QStringLiteral("⚙  Settings"), this);
  settingsAct->setToolTip(QStringLiteral("Preferences"));
  tb->addAction(settingsAct);
  connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);

  // ── Connect view toggle ───────────────────────────────────────────────────
  connect(m_listAction, &QAction::triggered, this,
          &MainWindow::switchToListView);
  connect(m_gridAction, &QAction::triggered, this,
          &MainWindow::switchToGridView);
}

void MainWindow::initMenuBar() {
  // ── File ──────────────────────────────────────────────────────────────────
  QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("&File"));

  QAction *addFolderAct = fileMenu->addAction(QStringLiteral("Add &Folder…"));
  addFolderAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
  connect(addFolderAct, &QAction::triggered, this,
          &MainWindow::onAddFolderRequested);

  fileMenu->addSeparator();

  m_signInAction = fileMenu->addAction(QStringLiteral("&Sign In…"));
  connect(m_signInAction, &QAction::triggered, this, &MainWindow::promptSignIn);

  m_signOutAction = fileMenu->addAction(QStringLiteral("Sign &Out"));
  connect(m_signOutAction, &QAction::triggered, this, &MainWindow::onSignOut);

  fileMenu->addSeparator();

  QAction *quitAct = fileMenu->addAction(QStringLiteral("&Quit"));
  quitAct->setShortcut(QKeySequence::Quit);
  connect(quitAct, &QAction::triggered, this, &QWidget::close);

  // ── View ──────────────────────────────────────────────────────────────────
  QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

  QAction *listViewAct = viewMenu->addAction(QStringLiteral("&List View"));
  listViewAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
  connect(listViewAct, &QAction::triggered, this,
          &MainWindow::switchToListView);

  QAction *gridViewAct = viewMenu->addAction(QStringLiteral("&Grid View"));
  gridViewAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
  connect(gridViewAct, &QAction::triggered, this,
          &MainWindow::switchToGridView);

  // ── Tags ──────────────────────────────────────────────────────────────────
  QMenu *tagMenu = menuBar()->addMenu(QStringLiteral("&Tags"));
  QAction *tagMgrAct = tagMenu->addAction(QStringLiteral("&Manage Tags…"));
  connect(tagMgrAct, &QAction::triggered, this, &MainWindow::openTagManager);

  // ── Tools ─────────────────────────────────────────────────────────────────
  QMenu *toolsMenu = menuBar()->addMenu(QStringLiteral("T&ools"));
  QAction *rescanAct =
      toolsMenu->addAction(QStringLiteral("&Rescan All Folders"));
  connect(rescanAct, &QAction::triggered, m_watcher, &FolderWatcher::rescanAll);

  toolsMenu->addSeparator();
  QAction *settingsAct = toolsMenu->addAction(QStringLiteral("&Settings…"));
  connect(settingsAct, &QAction::triggered, this, &MainWindow::openSettings);

  // ── Help ──────────────────────────────────────────────────────────────────
  QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
  QAction *dataLocAct =
      helpMenu->addAction(QStringLiteral("Show &Data Location"));
  connect(dataLocAct, &QAction::triggered, this, [this]() {
    QMessageBox::information(
        this, QStringLiteral("Data Location"),
        QStringLiteral("Application data is stored at:\n\n%1").arg(dataDir()));
  });
}

void MainWindow::initStatusBar() {
  m_statusLabel = new QLabel(QStringLiteral("0 files"), this);
  m_scanLabel = new QLabel(this);
  m_scanLabel->setObjectName(QStringLiteral("scanLabel"));
  m_userLabel = new QLabel(QStringLiteral("Not signed in"), this);

  statusBar()->addWidget(m_statusLabel);
  statusBar()->addPermanentWidget(m_scanLabel);
  statusBar()->addPermanentWidget(m_userLabel);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Signal wiring
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::connectSignals() {
  // Search
  connect(m_searchEdit, &QLineEdit::textChanged, this,
          &MainWindow::onSearchTextChanged);

  // Folder panel → controller / model
  connect(m_folderPanel, &FolderPanel::addFolderRequested, this,
          &MainWindow::onAddFolderRequested);
  connect(m_folderPanel, &FolderPanel::removeFolderRequested, this,
          &MainWindow::onRemoveFolderRequested);
  connect(m_folderPanel, &FolderPanel::folderSelected, m_proxy,
          &SearchFilterProxy::setFolderFilter);
  connect(m_folderPanel, &FolderPanel::tagsSelected, m_proxy,
          &SearchFilterProxy::setActiveTags);

  // List view
  connect(m_listView, &ListView::fileActivated, this,
          &MainWindow::onFileActivated);
  connect(m_listView, &ListView::fileSelected, this,
          &MainWindow::onFileSelected);
  connect(m_listView, &ListView::editTagsRequested, this,
          &MainWindow::onEditTagsRequested);
  connect(m_listView, &ListView::thumbnailNeeded, m_pdfCtrl,
          &PdfController::requestThumbnail);

  // Grid view
  connect(m_gridView, &GridView::fileActivated, this,
          &MainWindow::onFileActivated);
  connect(m_gridView, &GridView::fileSelected, this,
          &MainWindow::onFileSelected);
  connect(m_gridView, &GridView::editTagsRequested, this,
          &MainWindow::onEditTagsRequested);
  connect(m_gridView, &GridView::thumbnailNeeded, m_pdfCtrl,
          &PdfController::requestThumbnail);

  // Recent view
  connect(m_recentView, &RecentView::fileActivated, this,
          &MainWindow::onFileActivated);

  // PDF controller
  connect(m_pdfCtrl, &PdfController::pdfOpened, this, &MainWindow::onPdfOpened);
  connect(m_pdfCtrl, &PdfController::errorOccurred, this,
          [this](const QString &msg) {
            QMessageBox::warning(this, QStringLiteral("Error"), msg);
          });

  // Model → status bar
  connect(m_pdfModel, &QAbstractItemModel::modelReset, this,
          &MainWindow::updateStatusBar);
  connect(m_pdfModel, &QAbstractItemModel::rowsInserted, this,
          &MainWindow::updateStatusBar);
  connect(m_pdfModel, &QAbstractItemModel::rowsRemoved, this,
          &MainWindow::updateStatusBar);

  // Scan status
  connect(m_watcher, &FolderWatcher::scanFinished, this,
          &MainWindow::onScanFinished);

  // Folder model ↔ watcher
  connect(m_folderModel, &FolderModel::folderAdded, m_watcher,
          &FolderWatcher::addRootFolder);
  connect(m_folderModel, &FolderModel::folderRemoved, m_watcher,
          &FolderWatcher::removeRootFolder);

  // Folder model ↔ tree model (for sidebar hierarchy display)
  connect(m_folderModel, &FolderModel::folderAdded, m_folderTreeModel,
          &FolderTreeModel::addRootFolder);
  connect(m_folderModel, &FolderModel::folderRemoved, m_folderTreeModel,
          &FolderTreeModel::removeRootFolder);

  // Backend — any failure without a dedicated handler surfaces as a modal.
  connect(m_api, &ApiClient::errorOccurred, this, &MainWindow::onApiError);
  connect(m_api, &ApiClient::sessionExpired, this,
          &MainWindow::onSessionExpired);

  // Tag model changes → refresh folder panel chips
  connect(m_tagModel, &QAbstractItemModel::modelReset, m_folderPanel,
          &FolderPanel::refresh);
  connect(m_tagModel, &QAbstractItemModel::rowsInserted, m_folderPanel,
          &FolderPanel::refresh);
  connect(m_tagModel, &QAbstractItemModel::rowsRemoved, m_folderPanel,
          &FolderPanel::refresh);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Slots
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::switchToListView() {
  m_viewStack->setCurrentIndex(0);
  m_listAction->setChecked(true);
  m_db->setSetting(QStringLiteral("defaultView"), QStringLiteral("list"));
}

void MainWindow::switchToGridView() {
  m_viewStack->setCurrentIndex(1);
  m_gridAction->setChecked(true);
  m_gridView->triggerThumbnailLoad();
  m_db->setSetting(QStringLiteral("defaultView"), QStringLiteral("grid"));
}

void MainWindow::onAddFolderRequested() {
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("Select Folder to Watch"),
      QStandardPaths::writableLocation(QStandardPaths::HomeLocation));

  if (dir.isEmpty())
    return;

  if (m_folderModel->hasFolder(dir)) {
    QMessageBox::information(
        this, QStringLiteral("Already Added"),
        QStringLiteral("This folder is already being watched."));
    return;
  }

  m_db->saveFolder(dir);
  m_folderModel->addFolder(dir); // triggers watcher via signal
}

void MainWindow::onRemoveFolderRequested(const QString &path) {
  const auto reply = QMessageBox::question(
      this, QStringLiteral("Remove Folder"),
      QStringLiteral(
          "Stop watching '%1'?\n\nPDF records in this folder will be removed.")
          .arg(path),
      QMessageBox::Yes | QMessageBox::Cancel);

  if (reply != QMessageBox::Yes)
    return;

  // Remove all PDFs from this folder and its subfolders
  const QList<PdfFile> allFiles = m_pdfModel->allFiles();
  for (const PdfFile &f : allFiles) {
    if (f.folderPath.startsWith(path)) {
      m_pdfModel->removeFile(f.filePath);
      m_db->deleteFile(f.filePath);
    }
  }

  m_db->deleteFolder(path);
  m_folderModel->removeFolder(path); // triggers watcher via signal
}

void MainWindow::onFileActivated(const QString &filePath) {
  m_pdfCtrl->openPdf(filePath);
}

void MainWindow::onFileSelected(const QString &filePath) {
  m_selectedFilePath = filePath;
  m_notes.clear();
  m_selectedFileGroups.clear();
  refreshDetailPane();
  if (m_rightTabs)
    m_rightTabs->setCurrentIndex(0);
}

void MainWindow::onEditTagsRequested(const QString &filePath) {
  const PdfFile f = m_pdfModel->fileByPath(filePath);
  if (!f.isValid())
    return;

  const int groupId = activeGroupId();
  if (groupId < 0) {
    QMessageBox::information(
        this, QStringLiteral("Sign In Required"),
        QStringLiteral("Sign in and pick a group before editing tags."));
    return;
  }

  // Build a simple tag-assignment dialog
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Edit Tags — %1").arg(f.fileName));
  dlg.setMinimumWidth(320);

  auto *layout = new QVBoxLayout(&dlg);
  layout->addWidget(new QLabel(
      QStringLiteral("Tags for this file in '%1':").arg(activeGroup().name),
      &dlg));

  auto *listWidget = new QListWidget(&dlg);
  const QStringList allTags = m_tagModel->allTags();
  for (const QString &tag : allTags) {
    auto *item = new QListWidgetItem(tag, listWidget);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(f.hasTag(tag) ? Qt::Checked : Qt::Unchecked);
  }
  layout->addWidget(listWidget);

  auto *newTagEdit = new QLineEdit(&dlg);
  newTagEdit->setPlaceholderText(QStringLiteral("Add a new tag…"));
  layout->addWidget(newTagEdit);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);

  if (dlg.exec() != QDialog::Accepted)
    return;

  QStringList selected;
  for (int i = 0; i < listWidget->count(); ++i) {
    auto *item = listWidget->item(i);
    if (item->checkState() == Qt::Checked)
      selected << item->text();
  }
  const QString extra = newTagEdit->text().trimmed();
  if (!extra.isEmpty() && !selected.contains(extra, Qt::CaseInsensitive))
    selected << extra;

  // The backend creates any missing tags and treats a tag another member
  // already applied as a no-op, so there is nothing to reconcile here.
  resolveRemoteFile(groupId, filePath, [this, groupId, filePath,
                                        selected](int remoteFileId) {
    m_api->setFileTags(
        groupId, remoteFileId, selected,
        [this, filePath](const QList<ApiTag> &tags) {
          QStringList names;
          for (const ApiTag &tag : tags)
            names << tag.name;

          m_tagCtrl->applyRemoteFileTags(filePath, names);
          reloadTagVocabulary();
          refreshDetailPane();
        });
  });
}

void MainWindow::onPdfOpened(const QString & /*filePath*/,
                             const QDateTime & /*when*/) {
  m_recentView->refresh();
  updateStatusBar();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Session
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::restoreSessionOrPrompt() {
  const QString server =
      m_db->getSetting(QStringLiteral("serverUrl")).toString().trimmed();
  const QString refreshToken =
      m_db->getSetting(QStringLiteral("refreshToken")).toString();

  if (server.isEmpty() || refreshToken.isEmpty()) {
    promptSignIn();
    return;
  }

  m_api->setBaseUrl(QUrl(server));

  ApiUser saved;
  saved.id = m_db->getSetting(QStringLiteral("userId"), -1).toInt();
  saved.email = m_db->getSetting(QStringLiteral("userEmail")).toString();
  saved.displayName =
      m_db->getSetting(QStringLiteral("userDisplayName")).toString();
  m_api->restoreSession(refreshToken, saved);

  // Spend the stored refresh token for a live session. A failure here is
  // ordinary (the token expired), so it prompts rather than alarming the user.
  m_api->refreshSession([this]() { onSignedIn(); },
                        [this](const ApiError &error) {
                          clearSavedSession();
                          if (error.isNetworkFailure())
                            showError(error);
                          promptSignIn();
                        });
}

void MainWindow::promptSignIn() {
  if (m_signingIn)
    return;
  m_signingIn = true;

  LoginDialog dlg(m_api, this);
  dlg.setServerUrl(m_db->getSetting(QStringLiteral("serverUrl"),
                                    QStringLiteral("http://localhost:8000"))
                       .toString());
  dlg.setEmail(m_db->getSetting(QStringLiteral("userEmail")).toString());

  const bool accepted = dlg.exec() == QDialog::Accepted;
  m_signingIn = false;

  if (!accepted) {
    // Declining is allowed: local browsing keeps working, everything shared
    // is disabled until they sign in from the File menu.
    setCollaborationEnabled(false);
    m_userLabel->setText(QStringLiteral("Not signed in"));
    return;
  }

  m_db->setSetting(QStringLiteral("serverUrl"), dlg.serverUrl());
  m_db->setSetting(QStringLiteral("staySignedIn"), dlg.shouldStaySignedIn());
  onSignedIn();
}

void MainWindow::onSignedIn() {
  const ApiUser user = m_api->currentUser();
  m_userLabel->setText(
      QStringLiteral("%1 · %2").arg(user.displayName, m_api->baseUrl().host()));
  saveSession();
  setCollaborationEnabled(true);

  reloadGroups([this]() {
    reloadTagVocabulary();
    refreshDetailPane();
  });
}

void MainWindow::saveSession() {
  const ApiUser user = m_api->currentUser();
  m_db->setSetting(QStringLiteral("userId"), user.id);
  m_db->setSetting(QStringLiteral("userEmail"), user.email);
  m_db->setSetting(QStringLiteral("userDisplayName"), user.displayName);

  // Only persist the refresh token if the user asked to stay signed in.
  if (m_db->getSetting(QStringLiteral("staySignedIn"), true).toBool())
    m_db->setSetting(QStringLiteral("refreshToken"), m_api->refreshToken());
  else
    m_db->setSetting(QStringLiteral("refreshToken"), QString{});
}

void MainWindow::clearSavedSession() {
  m_db->setSetting(QStringLiteral("refreshToken"), QString{});
  m_db->clearRemoteCache();
}

void MainWindow::onSignOut() {
  m_api->clearSession();
  clearSavedSession();

  m_groups.clear();
  m_notes.clear();
  m_selectedFileGroups.clear();
  m_activeGroupId = -1;

  {
    const QSignalBlocker blocker(m_groupCombo);
    m_groupCombo->clear();
  }
  m_userLabel->setText(QStringLiteral("Not signed in"));
  setCollaborationEnabled(false);
  refreshDetailPane();
}

void MainWindow::onSessionExpired() {
  clearSavedSession();
  setCollaborationEnabled(false);
  m_userLabel->setText(QStringLiteral("Not signed in"));
  promptSignIn();
}

void MainWindow::onApiError(const ApiError &error) { showError(error); }

void MainWindow::showError(const ApiError &error) {
  QMessageBox box(this);
  box.setIcon(error.httpStatus == 403 || error.httpStatus == 409
                  ? QMessageBox::Warning
                  : QMessageBox::Critical);
  box.setWindowTitle(error.title());
  box.setText(error.message);
  box.setStandardButtons(QMessageBox::Ok);
  box.exec();
}

void MainWindow::setCollaborationEnabled(bool enabled) {
  m_groupCombo->setEnabled(enabled);
  if (m_signInAction)
    m_signInAction->setEnabled(!enabled);
  if (m_signOutAction)
    m_signOutAction->setEnabled(enabled);
  refreshDetailPane();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Backend helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::reloadGroups(std::function<void()> onDone) {
  if (!m_api->isAuthenticated()) {
    if (onDone)
      onDone();
    return;
  }

  m_api->listGroups([this, onDone](const QList<ApiGroup> &groups) {
    m_groups = groups;

    const int previous = m_activeGroupId;
    {
      const QSignalBlocker blocker(m_groupCombo);
      m_groupCombo->clear();
      for (const ApiGroup &group : m_groups)
        m_groupCombo->addItem(group.name, group.id);
    }

    // Keep the user on the group they were using; fall back to their personal
    // one, which always exists.
    int index = m_groupCombo->findData(previous);
    if (index < 0) {
      const int remembered =
          m_db->getSetting(QStringLiteral("activeGroupId"), -1).toInt();
      index = m_groupCombo->findData(remembered);
    }
    if (index < 0) {
      for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups.at(i).isPersonal) {
          index = i;
          break;
        }
      }
    }
    if (index < 0 && !m_groups.isEmpty())
      index = 0;

    if (index >= 0) {
      const QSignalBlocker blocker(m_groupCombo);
      m_groupCombo->setCurrentIndex(index);
      m_activeGroupId = m_groupCombo->itemData(index).toInt();
      m_db->setSetting(QStringLiteral("activeGroupId"), m_activeGroupId);
    } else {
      m_activeGroupId = -1;
    }

    if (onDone)
      onDone();
  });
}

void MainWindow::reloadTagVocabulary() {
  if (!m_api->isAuthenticated())
    return;

  // The sidebar shows one vocabulary drawn from every group the user belongs
  // to, even though storage is partitioned per group.
  m_api->listAllTags([this](const QList<ApiTag> &tags) {
    QStringList names;
    for (const ApiTag &tag : tags) {
      if (!names.contains(tag.name, Qt::CaseInsensitive))
        names << tag.name;
    }
    m_tagCtrl->applyRemoteVocabulary(names);
  });
}

QString MainWindow::contentHashFor(const QString &filePath) {
  const QFileInfo info(filePath);
  if (!info.exists())
    return {};

  const QString cached =
      m_db->cachedHash(filePath, info.size(), info.lastModified());
  if (!cached.isEmpty())
    return cached;

  const QString hash = ApiClient::hashFile(filePath);
  if (!hash.isEmpty())
    m_db->storeHash(filePath, hash, info.size(), info.lastModified());
  return hash;
}

void MainWindow::resolveRemoteFile(int groupId, const QString &filePath,
                                   std::function<void(int)> onReady) {
  if (groupId < 0 || !m_api->isAuthenticated())
    return;

  const QString hash = contentHashFor(filePath);
  if (hash.isEmpty()) {
    showError(ApiError::network(
        QStringLiteral("Could not read %1 to identify it.")
            .arg(QFileInfo(filePath).fileName())));
    return;
  }

  const int cached = m_db->remoteFileId(groupId, hash);
  if (cached >= 0) {
    onReady(cached);
    return;
  }

  const PdfFile local = m_pdfModel->fileByPath(filePath);
  const QFileInfo info(filePath);

  // Registration is idempotent on the backend: if another member already
  // added this exact PDF we get their record back instead of an error.
  m_api->registerFile(
      groupId, hash, info.fileName(), info.size(), local.pageCount,
      [this, groupId, hash, onReady](const ApiFile &file) {
        m_db->storeRemoteFileId(groupId, hash, file.id);
        onReady(file.id);
      });
}

int MainWindow::activeGroupId() const { return m_activeGroupId; }

ApiGroup MainWindow::activeGroup() const { return groupById(m_activeGroupId); }

ApiGroup MainWindow::groupById(int groupId) const {
  for (const ApiGroup &group : m_groups) {
    if (group.id == groupId)
      return group;
  }
  return {};
}

int MainWindow::selectedGroupId() const {
  QListWidgetItem *item = m_groupList ? m_groupList->currentItem() : nullptr;
  return item ? item->data(Qt::UserRole).toInt() : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Notes
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onAddNote() {
  const QString body = m_noteEdit->toPlainText().trimmed();
  if (body.isEmpty())
    return;

  const int groupId = activeGroupId();
  if (groupId < 0 || m_selectedFilePath.isEmpty())
    return;

  resolveRemoteFile(groupId, m_selectedFilePath,
                    [this, groupId, body](int remoteFileId) {
                      m_api->createNote(groupId, remoteFileId, body,
                                        [this](const ApiNote &) {
                                          m_noteEdit->clear();
                                          refreshNotes();
                                        });
                    });
}

void MainWindow::refreshNotes() {
  // Clear the existing bubbles first so a failed reload does not leave stale
  // notes on screen.
  while (QLayoutItem *item = m_notesLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const int groupId = activeGroupId();
  const PdfFile file = m_pdfModel->fileByPath(m_selectedFilePath);
  if (groupId < 0 || !file.isValid() || !m_api->isAuthenticated()) {
    m_notes.clear();
    m_notesLayout->addStretch();
    return;
  }

  const QString hash = contentHashFor(m_selectedFilePath);
  const int remoteFileId =
      hash.isEmpty() ? -1 : m_db->remoteFileId(groupId, hash);
  if (remoteFileId < 0) {
    // Not registered in this group yet — there cannot be notes, and we should
    // not register a file just because it was clicked on.
    m_notes.clear();
    m_notesLayout->addStretch();
    return;
  }

  const QString pathAtRequest = m_selectedFilePath;
  m_api->listNotes(
      groupId, remoteFileId,
      [this, pathAtRequest, groupId, remoteFileId](const QList<ApiNote> &notes) {
        // The user may have clicked elsewhere while this was in flight.
        if (pathAtRequest != m_selectedFilePath)
          return;

        m_notes = notes;
        while (QLayoutItem *item = m_notesLayout->takeAt(0)) {
          delete item->widget();
          delete item;
        }

        for (const ApiNote &note : m_notes)
          m_notesLayout->addWidget(buildNoteBubble(note));

        m_notesLayout->addStretch();
      });
}

QWidget *MainWindow::buildNoteBubble(const ApiNote &note) {
  auto *bubble = new QWidget;
  bubble->setObjectName(QStringLiteral("noteBubble"));
  bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  auto *bl = new QVBoxLayout(bubble);
  bl->setContentsMargins(10, 8, 10, 8);
  bl->setSpacing(6);

  auto *headerRow = new QHBoxLayout;
  auto *header = new QLabel(
      QStringLiteral("%1 · %2").arg(
          note.authorName,
          note.createdAt.toLocalTime().toString(
              QStringLiteral("yyyy-MM-dd hh:mm"))),
      bubble);
  QFont hfont = header->font();
  hfont.setBold(true);
  header->setFont(hfont);
  header->setStyleSheet(QStringLiteral("color: #9fb3ff; font-size: 9pt;"));
  headerRow->addWidget(header);
  headerRow->addStretch();

  // Edit and Delete appear only on your own notes. The backend refuses them
  // for anyone else regardless, so this is presentation, not enforcement.
  if (note.editable) {
    auto *editBtn = new QPushButton(QStringLiteral("Edit"), bubble);
    auto *deleteBtn = new QPushButton(QStringLiteral("Delete"), bubble);
    for (QPushButton *button : {editBtn, deleteBtn}) {
      button->setFlat(true);
      button->setCursor(Qt::PointingHandCursor);
      button->setStyleSheet(
          QStringLiteral("padding: 0 6px; font-size: 8pt; color: #8a8d95;"));
    }
    headerRow->addWidget(editBtn);
    headerRow->addWidget(deleteBtn);

    connect(editBtn, &QPushButton::clicked, this,
            [this, note]() { editNote(note); });
    connect(deleteBtn, &QPushButton::clicked, this,
            [this, note]() { deleteNote(note); });
  }
  bl->addLayout(headerRow);

  auto *body = new QLabel(note.body, bubble);
  body->setWordWrap(true);
  body->setTextInteractionFlags(Qt::TextSelectableByMouse);
  bl->addWidget(body);

  if (note.version > 1) {
    auto *edited = new QLabel(
        QStringLiteral("edited %1")
            .arg(note.updatedAt.toLocalTime().toString(
                QStringLiteral("yyyy-MM-dd hh:mm"))),
        bubble);
    edited->setStyleSheet(QStringLiteral("color: #6a6d75; font-size: 8pt;"));
    bl->addWidget(edited);
  }

  bubble->setStyleSheet(QStringLiteral(
      "QWidget#noteBubble { background: rgba(77,142,255,0.08); border: 1px "
      "solid rgba(77,142,255,0.14); border-radius: 8px; }"));
  return bubble;
}

void MainWindow::editNote(const ApiNote &note) {
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Edit Note"));
  dlg.setMinimumWidth(420);

  auto *layout = new QVBoxLayout(&dlg);
  auto *editor = new QTextEdit(&dlg);
  editor->setPlainText(note.body);
  layout->addWidget(editor);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const QString body = editor->toPlainText().trimmed();
  if (body.isEmpty() || body == note.body)
    return;

  // Sending the version we displayed means a copy of this note edited
  // elsewhere in the meantime is reported instead of silently overwritten.
  m_api->updateNote(
      note.id, body, note.version, [this](const ApiNote &) { refreshNotes(); },
      [this, body](const ApiError &error) {
        if (error.code != QLatin1String(ApiError::StaleNote)) {
          showError(error);
          return;
        }

        const QString current =
            error.detail.value(QStringLiteral("current_body")).toString();
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(QStringLiteral("Note Changed Elsewhere"));
        box.setText(error.message);
        box.setInformativeText(
            QStringLiteral("Now saved:\n%1\n\nYour edit:\n%2")
                .arg(current, body));
        box.setStandardButtons(QMessageBox::Ok);
        box.exec();
        refreshNotes();
      });
}

void MainWindow::deleteNote(const ApiNote &note) {
  const auto reply = QMessageBox::question(
      this, QStringLiteral("Delete Note"),
      QStringLiteral("Delete this note? This cannot be undone."),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;

  m_api->deleteNote(note.id, [this]() { refreshNotes(); });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Groups
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onActiveGroupChanged(int index) {
  if (index < 0)
    return;

  m_activeGroupId = m_groupCombo->itemData(index).toInt();
  m_db->setSetting(QStringLiteral("activeGroupId"), m_activeGroupId);

  // Tags and notes are scoped to the group, so both views have to be redrawn.
  reloadTagVocabulary();
  refreshDetailPane();
}

void MainWindow::onCreateGroup() {
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, QStringLiteral("New Group"),
      QStringLiteral("Group name:\n\nYou'll be its owner — you can invite "
                     "people once it exists."),
      QLineEdit::Normal, QString{}, &ok);
  if (!ok || name.trimmed().isEmpty())
    return;

  m_api->createGroup(name.trimmed(), [this](const ApiGroup &group) {
    m_activeGroupId = group.id;
    reloadGroups([this]() {
      reloadTagVocabulary();
      refreshDetailPane();
    });
  });
}

void MainWindow::onRenameGroup() {
  const int groupId = selectedGroupId();
  const ApiGroup group = groupById(groupId);
  if (!group.isValid())
    return;

  bool ok = false;
  const QString name = QInputDialog::getText(
      this, QStringLiteral("Rename Group"), QStringLiteral("Group name:"),
      QLineEdit::Normal, group.name, &ok);
  if (!ok || name.trimmed().isEmpty() || name.trimmed() == group.name)
    return;

  m_api->renameGroup(groupId, name.trimmed(), [this](const ApiGroup &) {
    reloadGroups([this]() { refreshDetailPane(); });
  });
}

void MainWindow::onDeleteGroup() {
  const int groupId = selectedGroupId();
  const ApiGroup group = groupById(groupId);
  if (!group.isValid())
    return;

  const auto reply = QMessageBox::question(
      this, QStringLiteral("Delete Group"),
      QStringLiteral("Delete '%1'?\n\nIts tags and notes are removed for "
                     "every member. The PDFs on your disk are untouched.")
          .arg(group.name),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;

  m_api->deleteGroup(groupId, [this, groupId]() {
    if (m_activeGroupId == groupId)
      m_activeGroupId = -1;
    reloadGroups([this]() {
      reloadTagVocabulary();
      refreshDetailPane();
    });
  });
}

void MainWindow::onManageMembers() {
  const int groupId = selectedGroupId();
  const ApiGroup group = groupById(groupId);
  if (!group.isValid())
    return;

  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Members — %1").arg(group.name));
  dlg.setMinimumWidth(420);

  auto *layout = new QVBoxLayout(&dlg);
  auto *memberList = new QListWidget(&dlg);
  layout->addWidget(memberList);

  auto *addRow = new QHBoxLayout;
  auto *emailEdit = new QLineEdit(&dlg);
  emailEdit->setPlaceholderText(QStringLiteral("teammate@example.com"));
  auto *addBtn = new QPushButton(QStringLiteral("Invite"), &dlg);
  auto *removeBtn = new QPushButton(QStringLiteral("Remove"), &dlg);
  addRow->addWidget(emailEdit);
  addRow->addWidget(addBtn);
  addRow->addWidget(removeBtn);
  layout->addLayout(addRow);

  auto *hint = new QLabel(&dlg);
  hint->setWordWrap(true);
  hint->setStyleSheet(QStringLiteral("color: #8a8d95; font-size: 8pt;"));
  hint->setText(
      group.isOwner()
          ? QStringLiteral("Members can add files, tags and notes. Only you "
                           "can invite or remove people. Notes can only ever "
                           "be edited by whoever wrote them.")
          : QStringLiteral("Only the group owner can invite or remove "
                           "members. You can remove yourself."));
  layout->addWidget(hint);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);

  const int myId = m_api->currentUser().id;

  auto reload = [this, groupId, memberList]() {
    m_api->listMembers(groupId, [memberList](const QList<ApiMember> &members) {
      memberList->clear();
      for (const ApiMember &member : members) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  <%2>%3")
                .arg(member.displayName, member.email,
                     member.isOwner() ? QStringLiteral("  — owner")
                                      : QString{}),
            memberList);
        item->setData(Qt::UserRole, member.userId);
        item->setData(Qt::UserRole + 1, member.isOwner());
      }
    });
  };

  // The owner may invite anyone; a member may only remove themselves.
  addBtn->setEnabled(group.isOwner() && !group.isPersonal);
  emailEdit->setEnabled(group.isOwner() && !group.isPersonal);

  connect(addBtn, &QPushButton::clicked, &dlg, [&]() {
    const QString email = emailEdit->text().trimmed();
    if (email.isEmpty())
      return;
    m_api->addMember(groupId, email, [&, reload](const ApiMember &) {
      emailEdit->clear();
      reload();
      reloadGroups();
    });
  });

  connect(removeBtn, &QPushButton::clicked, &dlg, [&]() {
    QListWidgetItem *item = memberList->currentItem();
    if (!item)
      return;
    const int userId = item->data(Qt::UserRole).toInt();

    const QString question =
        userId == myId
            ? QStringLiteral("Leave '%1'? You'll lose access to its files, "
                             "tags and notes.")
                  .arg(group.name)
            : QStringLiteral("Remove %1 from '%2'?")
                  .arg(item->text(), group.name);
    if (QMessageBox::question(&dlg, QStringLiteral("Remove Member"), question,
                              QMessageBox::Yes | QMessageBox::Cancel) !=
        QMessageBox::Yes)
      return;

    m_api->removeMember(groupId, userId, [&, reload, userId]() {
      if (userId == myId) {
        dlg.accept();
        reloadGroups([this]() { refreshDetailPane(); });
        return;
      }
      reload();
      reloadGroups();
    });
  });

  reload();
  dlg.exec();
}

void MainWindow::onGroupItemChanged(QListWidgetItem *item) {
  const PdfFile file = m_pdfModel->fileByPath(m_selectedFilePath);
  if (!file.isValid() || !item)
    return;

  const int groupId = item->data(Qt::UserRole).toInt();
  const bool wanted = item->checkState() == Qt::Checked;
  const QString filePath = m_selectedFilePath;

  if (wanted) {
    m_selectedFileGroups.insert(groupId);
    resolveRemoteFile(groupId, filePath, [this, groupId](int) {
      if (groupId == activeGroupId())
        refreshNotes();
      reloadGroups();
    });
    return;
  }

  const QString hash = contentHashFor(filePath);
  const int remoteFileId =
      hash.isEmpty() ? -1 : m_db->remoteFileId(groupId, hash);
  if (remoteFileId < 0) {
    m_selectedFileGroups.remove(groupId);
    return;
  }

  const ApiGroup group = groupById(groupId);
  const auto reply = QMessageBox::question(
      this, QStringLiteral("Remove From Group"),
      QStringLiteral("Remove '%1' from '%2'?\n\nIts tags and notes in that "
                     "group are deleted for everyone. The file on your disk "
                     "is untouched.")
          .arg(file.fileName, group.name),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes) {
    refreshGroupList();  // put the tick back
    return;
  }

  m_api->removeFile(groupId, remoteFileId, [this, groupId, hash]() {
    m_selectedFileGroups.remove(groupId);
    m_db->forgetRemoteFile(groupId, hash);
    if (groupId == activeGroupId())
      refreshNotes();
    reloadGroups();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sync
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onSyncGroup() {
  const int groupId = selectedGroupId();
  const ApiGroup group = groupById(groupId);
  if (!group.isValid())
    return;

  m_api->syncStatus(groupId, [this, groupId,
                              group](const ApiSyncStatus &status) {
    if (status.pending.isEmpty()) {
      QMessageBox::information(
          this, QStringLiteral("Nothing To Sync"),
          QStringLiteral("All %1 file(s) in '%2' are already stored.")
              .arg(status.totalFiles)
              .arg(group.name));
      return;
    }

    auto *progress = new QProgressDialog(
        QStringLiteral("Uploading files in '%1'…").arg(group.name),
        QStringLiteral("Cancel"), 0, status.pending.size(), this);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setValue(0);

    uploadNext(groupId, status.pending, 0, 0, progress);
  });
}

void MainWindow::uploadNext(int groupId, QList<ApiFile> pending, int uploaded,
                            int skipped, QProgressDialog *progress) {
  const int done = uploaded + skipped;

  if (pending.isEmpty() || progress->wasCanceled()) {
    const bool canceled = progress->wasCanceled() && !pending.isEmpty();
    progress->close();
    progress->deleteLater();

    QMessageBox::information(
        this,
        canceled ? QStringLiteral("Sync Stopped")
                 : QStringLiteral("Sync Complete"),
        QStringLiteral("Uploaded %1 file(s), skipped %2.%3")
            .arg(uploaded)
            .arg(skipped)
            .arg(canceled ? QStringLiteral("\n\n%1 file(s) were not uploaded.")
                                .arg(pending.size())
                          : QString{}));
    refreshDetailPane();
    return;
  }

  const ApiFile file = pending.takeFirst();
  progress->setValue(done);
  progress->setLabelText(QStringLiteral("Uploading %1…").arg(file.fileName));

  // The backend knows the file by content hash; we have to find the copy on
  // this machine to send.
  QString localPath;
  for (const PdfFile &candidate : m_pdfModel->allFiles()) {
    if (contentHashFor(candidate.filePath) == file.contentHash) {
      localPath = candidate.filePath;
      break;
    }
  }

  if (localPath.isEmpty()) {
    // Another member registered it; we simply do not hold a copy to upload.
    uploadNext(groupId, pending, uploaded, skipped + 1, progress);
    return;
  }

  m_api->uploadFile(
      groupId, file.id, localPath,
      [this, groupId, pending, uploaded, skipped,
       progress](const ApiUploadResult &result) {
        uploadNext(groupId, pending, uploaded + (result.uploaded ? 1 : 0),
                   skipped + (result.uploaded ? 0 : 1), progress);
      },
      [this, progress](const ApiError &error) {
        progress->close();
        progress->deleteLater();
        showError(error);
        refreshDetailPane();
      });
}

void MainWindow::onSearchTextChanged(const QString &text) {
  m_proxy->setSearchText(text);
  updateStatusBar();
}

void MainWindow::openTagManager() {
  const int groupId = activeGroupId();
  if (groupId < 0) {
    QMessageBox::information(
        this, QStringLiteral("Sign In Required"),
        QStringLiteral("Sign in and pick a group to manage its tags."));
    return;
  }

  TagManagerDialog dlg(m_api, groupId, activeGroup().name, this);
  connect(&dlg, &TagManagerDialog::tagsChanged, this, [this]() {
    reloadTagVocabulary();
    m_folderPanel->refresh();
  });
  dlg.exec();
  m_folderPanel->refresh();
}

void MainWindow::openSettings() {
  SettingsDialog dlg(m_db, this);
  connect(&dlg, &SettingsDialog::darkModeChanged, this,
          &MainWindow::applyDarkTheme);
  connect(&dlg, &SettingsDialog::serverChanged, this,
          [this](const QString &serverUrl) {
            // A different server means different accounts and ids entirely.
            m_api->clearSession();
            m_api->setBaseUrl(QUrl(serverUrl));
            onSignOut();
            promptSignIn();
          });
  dlg.exec();
}

void MainWindow::updateStatusBar() {
  const int total = m_pdfModel->totalCount();
  const int visible = m_proxy->rowCount();

  if (total == visible)
    m_statusLabel->setText(
        QStringLiteral("%1 file%2")
            .arg(total)
            .arg(total == 1 ? QLatin1String("") : QLatin1String("s")));
  else
    m_statusLabel->setText(
        QStringLiteral("%1 of %2 files").arg(visible).arg(total));
}

void MainWindow::onScanFinished(const QString &folder) {
  m_scanLabel->setText(
      QStringLiteral("✓ Scanned: %1").arg(QDir(folder).dirName()));

  // Clear the message after 3 seconds
  QTimer::singleShot(3000, m_scanLabel, [this]() { m_scanLabel->clear(); });
}

QWidget *MainWindow::buildDetailPane() {
  auto *pane = new QWidget;
  auto *root = new QVBoxLayout(pane);

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
  m_groupList->setToolTip(
      QStringLiteral("Tick a group to share this file with its members"));
  root->addWidget(m_groupList);

  auto *groupRow = new QHBoxLayout;
  m_addGroupBtn = new QPushButton(QStringLiteral("New"), pane);
  m_renameGroupBtn = new QPushButton(QStringLiteral("Rename"), pane);
  m_removeGroupBtn = new QPushButton(QStringLiteral("Delete"), pane);
  m_membersBtn = new QPushButton(QStringLiteral("Members"), pane);
  m_syncBtn = new QPushButton(QStringLiteral("Sync"), pane);
  groupRow->addWidget(m_addGroupBtn);
  groupRow->addWidget(m_renameGroupBtn);
  groupRow->addWidget(m_removeGroupBtn);
  groupRow->addWidget(m_membersBtn);
  groupRow->addWidget(m_syncBtn);
  root->addLayout(groupRow);

  root->addWidget(new QLabel(QStringLiteral("NOTES"), pane));
  m_noteEdit = new QTextEdit(pane);
  m_noteEdit->setPlaceholderText(QStringLiteral("Add a note…"));
  m_noteEdit->setMaximumHeight(90);
  root->addWidget(m_noteEdit);
  m_addNoteBtn = new QPushButton(QStringLiteral("Add Note"), pane);
  root->addWidget(m_addNoteBtn);

  auto *noteScroll = new QScrollArea(pane);
  noteScroll->setWidgetResizable(true);
  noteScroll->setFrameShape(QFrame::NoFrame);
  auto *noteBody = new QWidget(noteScroll);
  m_notesLayout = new QVBoxLayout(noteBody);
  m_notesLayout->addStretch();
  noteScroll->setWidget(noteBody);
  root->addWidget(noteScroll, 1);

  connect(m_addGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onCreateGroup);
  connect(m_renameGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onRenameGroup);
  connect(m_removeGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onDeleteGroup);
  connect(m_membersBtn, &QPushButton::clicked, this,
          &MainWindow::onManageMembers);
  connect(m_syncBtn, &QPushButton::clicked, this, &MainWindow::onSyncGroup);
  connect(m_addNoteBtn, &QPushButton::clicked, this, &MainWindow::onAddNote);
  connect(m_groupList, &QListWidget::itemChanged, this,
          &MainWindow::onGroupItemChanged);
  connect(m_groupList, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *) { refreshDetailPane(); });

  return pane;
}

void MainWindow::refreshDetailPane() {
  if (!m_detailTitle)
    return;

  const PdfFile file = m_pdfModel->fileByPath(m_selectedFilePath);
  const bool hasFile = file.isValid();
  const bool online = m_api && m_api->isAuthenticated();

  m_detailTitle->setText(hasFile ? file.fileName
                                 : QStringLiteral("No file selected"));

  if (!online) {
    m_detailMeta->setText(QStringLiteral(
        "Sign in (File ▸ Sign In) to use groups, shared tags and notes."));
  } else if (hasFile) {
    m_detailMeta->setText(QStringLiteral("%1\n%2").arg(
        file.filePath, file.tags.join(QStringLiteral(", "))));
  } else {
    m_detailMeta->clear();
  }

  refreshGroupList();

  // A group must be selected in the list for the group-level buttons to have
  // a target; ownership decides which of them are permitted.
  const ApiGroup selected = groupById(selectedGroupId());
  const bool hasSelection = selected.isValid();
  const bool ownsSelection = hasSelection && selected.isOwner();

  m_groupList->setEnabled(online);
  m_addGroupBtn->setEnabled(online);
  m_renameGroupBtn->setEnabled(ownsSelection && !selected.isPersonal);
  m_removeGroupBtn->setEnabled(ownsSelection && !selected.isPersonal);
  m_membersBtn->setEnabled(hasSelection && !selected.isPersonal);
  m_syncBtn->setEnabled(hasSelection);

  m_renameGroupBtn->setToolTip(
      hasSelection && !ownsSelection
          ? QStringLiteral("Only the group's owner can rename it")
          : QString{});
  m_removeGroupBtn->setToolTip(
      hasSelection && !ownsSelection
          ? QStringLiteral("Only the group's owner can delete it")
          : QString{});

  const bool canWriteNotes = online && hasFile && activeGroupId() >= 0;
  m_noteEdit->setEnabled(canWriteNotes);
  m_addNoteBtn->setEnabled(canWriteNotes);
  m_noteEdit->setPlaceholderText(
      canWriteNotes
          ? QStringLiteral("Add a note visible to '%1'…").arg(activeGroup().name)
          : QStringLiteral("Add a note…"));

  refreshNotes();
}

void MainWindow::refreshGroupList() {
  if (!m_groupList)
    return;

  const int previous = selectedGroupId();
  const PdfFile file = m_pdfModel->fileByPath(m_selectedFilePath);
  const bool hasFile = file.isValid();

  // Which groups already hold this file, judged from the local id cache so
  // the list draws without a round trip.
  m_selectedFileGroups.clear();
  if (hasFile) {
    const QString hash = contentHashFor(m_selectedFilePath);
    if (!hash.isEmpty()) {
      for (const ApiGroup &group : m_groups) {
        if (m_db->remoteFileId(group.id, hash) >= 0)
          m_selectedFileGroups.insert(group.id);
      }
    }
  }

  const QSignalBlocker blocker(m_groupList);
  m_groupList->clear();

  for (const ApiGroup &group : m_groups) {
    auto *item = new QListWidgetItem(group.name, m_groupList);
    item->setData(Qt::UserRole, group.id);

    Qt::ItemFlags flags = item->flags();
    if (hasFile)
      flags |= Qt::ItemIsUserCheckable;
    else
      flags &= ~Qt::ItemIsUserCheckable;
    item->setFlags(flags);

    if (hasFile) {
      item->setCheckState(m_selectedFileGroups.contains(group.id)
                              ? Qt::Checked
                              : Qt::Unchecked);
    }

    item->setToolTip(
        QStringLiteral("%1\n%2 member(s), %3 file(s)\nYou are %4")
            .arg(group.isPersonal ? QStringLiteral("Private to you")
                                  : QStringLiteral("Shared group"))
            .arg(group.memberCount)
            .arg(group.fileCount)
            .arg(group.isOwner() ? QStringLiteral("the owner")
                                 : QStringLiteral("a member")));

    if (group.id == previous)
      m_groupList->setCurrentItem(item);
  }
}

void MainWindow::applyDarkTheme(bool enabled) {
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

void MainWindow::restoreLayout() {
  QSettings qs;
  restoreGeometry(qs.value(QStringLiteral("geometry")).toByteArray());
  restoreState(qs.value(QStringLiteral("windowState")).toByteArray());

  // Dark mode default ON
  const bool dark = m_db->getSetting(QStringLiteral("darkMode"), true).toBool();
  applyDarkTheme(dark);

  // Default view
  const QString view =
      m_db->getSetting(QStringLiteral("defaultView"), QStringLiteral("list"))
          .toString();
  if (view == QLatin1String("grid"))
    switchToGridView();
  else
    switchToListView();
}

void MainWindow::saveLayout() {
  QSettings qs;
  qs.setValue(QStringLiteral("geometry"), saveGeometry());
  qs.setValue(QStringLiteral("windowState"), saveState());
}

void MainWindow::closeEvent(QCloseEvent *event) {
  saveLayout();
  event->accept();
}

QString MainWindow::dataDir() const {
  const QString appData =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString dataDirPath = appData + QStringLiteral("/data");
  QDir().mkpath(dataDirPath);
  return dataDirPath;
}

QString MainWindow::dbPath() const {
  return dataDir() + QStringLiteral("/pdforganizer.db");
}
