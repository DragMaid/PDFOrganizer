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
#include <QCheckBox>
#include <QCloseEvent>
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

namespace {

// A file name or a path has no spaces in it, so a word-wrapping QLabel sees one
// unbreakable word — and a wrapped label's minimum width is the width of its
// widest word. Left alone, one long name sets a floor that pushes the whole
// right pane wider. Zero-width spaces give the layout somewhere to break
// without changing a single visible character.
QString breakableText(const QString &text) {
  constexpr QChar kZeroWidthSpace(0x200B);
  constexpr int kMaxRun = 12; // longest run left unbreakable, in characters

  QString out;
  out.reserve(text.size() * 2);

  int run = 0;
  for (const QChar ch : text) {
    out.append(ch);
    if (ch.isSpace()) {
      run = 0;
      continue;
    }
    ++run;
    const bool separator = ch == u'/' || ch == u'\\' || ch == u'_' ||
                           ch == u'-' || ch == u'.' || ch == u',';
    if (separator || run >= kMaxRun) {
      out.append(kZeroWidthSpace);
      run = 0;
    }
  }
  return out;
}

} // namespace

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
  // Derived, not chosen: the group of the directory the selected file sits in.
  // It is the permission context for everything shared — tags land in its
  // vocabulary, notes are visible to exactly its members.
  tb->addWidget(new QLabel(QStringLiteral(" Group: "), this));
  m_groupLabel = new QLabel(QStringLiteral("—"), this);
  m_groupLabel->setMinimumWidth(160);
  m_groupLabel->setToolTip(QStringLiteral(
      "The group of the selected file's folder. Tags and notes you add apply "
      "to it."));
  tb->addWidget(m_groupLabel);

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

  // The group is created once the scan confirms there is at least one PDF —
  // see onScanFinished. Nothing to do here but say so.
  if (m_api->isAuthenticated()) {
    m_scanLabel->setText(QStringLiteral("Scanning %1…")
                             .arg(groupNameForFolder(dir)));
  }
}

void MainWindow::onRemoveFolderRequested(const QString &path) {
  // One watched root now spans many groups — its own directory plus every
  // subdirectory that held a PDF — so all of them are in scope here.
  QStringList affected;
  QList<int> owned;
  QList<int> joined;
  for (const QString &folder : m_db->mappedFolders()) {
    if (folder != path && !folder.startsWith(path + QDir::separator()))
      continue;
    affected << folder;
    const ApiGroup group = groupById(m_db->folderGroupId(folder));
    if (!group.isValid() || group.isPersonal)
      continue;
    (group.isOwner() ? owned : joined) << group.id;
  }

  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Remove Folder"));
  dlg.setMinimumWidth(440);
  auto *layout = new QVBoxLayout(&dlg);

  auto *question = new QLabel(
      QStringLiteral("Stop watching '%1'?\n\nPDF records for this folder and "
                     "its subfolders are removed from this machine. The files "
                     "on disk are untouched.")
          .arg(path),
      &dlg);
  question->setWordWrap(true);
  layout->addWidget(question);

  // Removing the folder locally says nothing about the shared groups, so the
  // destructive half is opt-in and off by default.
  QCheckBox *alsoDrop = nullptr;
  if (!owned.isEmpty() || !joined.isEmpty()) {
    alsoDrop = new QCheckBox(&dlg);
    alsoDrop->setChecked(false);
    if (joined.isEmpty()) {
      alsoDrop->setText(QStringLiteral("Also delete the %1 shared group(s) "
                                       "this folder created — removes their "
                                       "tags and notes for every member")
                            .arg(owned.size()));
    } else if (owned.isEmpty()) {
      alsoDrop->setText(QStringLiteral("Also leave the %1 shared group(s) from "
                                       "this folder — you lose access to their "
                                       "tags and notes")
                            .arg(joined.size()));
    } else {
      alsoDrop->setText(
          QStringLiteral("Also delete the %1 group(s) you created here and "
                         "leave the %2 you joined")
              .arg(owned.size())
              .arg(joined.size()));
    }
    alsoDrop->setToolTip(QStringLiteral("Leave unticked to keep sharing; the "
                                        "groups stay exactly as they are."));
    layout->addWidget(alsoDrop);
  }

  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
  auto *confirm = buttons->addButton(QStringLiteral("Stop Watching"),
                                     QDialogButtonBox::AcceptRole);
  connect(confirm, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  layout->addWidget(buttons);

  if (dlg.exec() != QDialog::Accepted)
    return;

  const bool dropGroups = alsoDrop && alsoDrop->isChecked();

  // Remove all PDFs from this folder and its subfolders
  const QList<PdfFile> allFiles = m_pdfModel->allFiles();
  for (const PdfFile &f : allFiles) {
    if (f.folderPath.startsWith(path)) {
      m_pdfModel->removeFile(f.filePath);
      m_db->deleteFile(f.filePath);
    }
  }

  m_db->deleteFolder(path);
  for (const QString &folder : affected)
    m_db->forgetFolderGroup(folder);
  m_folderModel->removeFolder(path); // triggers watcher via signal

  if (m_selectedFilePath.startsWith(path)) {
    m_selectedFilePath.clear();
    m_notes.clear();
    m_members.clear();
  }

  if (dropGroups) {
    const int myId = m_api->currentUser().id;
    for (const int groupId : owned)
      m_api->deleteGroup(groupId, [this]() { reloadGroups(); });
    for (const int groupId : joined)
      m_api->removeMember(groupId, myId, [this]() { reloadGroups(); });
  }

  refreshDetailPane();
}

void MainWindow::onFileActivated(const QString &filePath) {
  m_pdfCtrl->openPdf(filePath);
}

void MainWindow::onFileSelected(const QString &filePath) {
  m_selectedFilePath = filePath;
  m_notes.clear();
  m_members.clear();
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
        this, QStringLiteral("No Group Yet"),
        QStringLiteral("Tags belong to the group of the file's folder. Sign in "
                       "and wait for '%1' to finish being shared.")
            .arg(groupNameForFolder(groupFolderFor(filePath))));
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
    // Watched folders added while signed out — or on a previous run against a
    // different account — get their groups now.
    reconcileFolderGroups();
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
  m_members.clear();
  m_foldersTracking.clear();

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

    // A folder whose group has gone — deleted by its owner, or we were removed
    // from it — must not keep pointing at a dead id. Dropping the mapping lets
    // the next reconcile re-create or re-adopt one.
    for (const QString &folder : m_db->mappedFolders()) {
      const int mapped = m_db->folderGroupId(folder);
      if (mapped >= 0 && !groupById(mapped).isValid())
        m_db->forgetFolderGroup(folder);
    }

    if (onDone)
      onDone();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Folder-derived groups
//
//  Every directory that directly holds a PDF is a group, and it holds exactly
//  the PDFs sitting in it — not the ones in its subdirectories, which are
//  groups of their own. Everything below turns that sentence into backend
//  state.
// ─────────────────────────────────────────────────────────────────────────────

QString MainWindow::rootFolderFor(const QString &path) const {
  QString best;
  for (const QString &root : m_folderModel->allFolders()) {
    if (!path.startsWith(root))
      continue;
    // A root nested inside another wins, so the innermost one owns the path.
    if (root.length() > best.length())
      best = root;
  }
  return best;
}

QString MainWindow::groupFolderFor(const QString &filePath) const {
  if (filePath.isEmpty() || rootFolderFor(filePath).isEmpty())
    return {};
  return QFileInfo(filePath).absolutePath();
}

QStringList MainWindow::foldersHoldingPdfs() const {
  QStringList folders;
  for (const PdfFile &file : m_pdfModel->allFiles()) {
    const QString folder = groupFolderFor(file.filePath);
    if (!folder.isEmpty() && !folders.contains(folder))
      folders << folder;
  }
  return folders;
}

QString MainWindow::groupNameForFolder(const QString &folderPath) const {
  const QString root = rootFolderFor(folderPath);
  if (root.isEmpty())
    return QDir(folderPath).dirName();

  QString rootName = QDir(root).dirName();
  if (rootName.isEmpty())
    rootName = root; // a filesystem root has no directory name

  // Two watched roots can share a basename ("…/Work/Papers", "…/Home/Papers");
  // qualify both, so their subfolder groups stay distinguishable too.
  for (const QString &other : m_folderModel->allFolders()) {
    if (other != root && QDir(other).dirName() == rootName) {
      const QString parent = QFileInfo(root).dir().dirName();
      if (!parent.isEmpty())
        rootName = QStringLiteral("%1 (%2)").arg(rootName, parent);
      break;
    }
  }

  // Subfolders carry their path below the root, so "2023" under two different
  // roots reads as "Papers/2023" and "Invoices/2023" rather than twice "2023".
  const QString relative = QDir(root).relativeFilePath(folderPath);
  if (relative.isEmpty() || relative == QLatin1String("."))
    return rootName;
  return QStringLiteral("%1/%2").arg(rootName, relative);
}

int MainWindow::groupIdForFolder(const QString &folderPath) const {
  if (folderPath.isEmpty())
    return -1;
  const int mapped = m_db->folderGroupId(folderPath);
  return groupById(mapped).isValid() ? mapped : -1;
}

void MainWindow::reconcileFolderGroups() {
  if (!m_api->isAuthenticated())
    return;

  for (const QString &folder : foldersHoldingPdfs())
    syncFolderGroup(folder);
}

void MainWindow::reconcileFoldersUnder(const QString &rootPath) {
  if (!m_api->isAuthenticated())
    return;

  for (const QString &folder : foldersHoldingPdfs()) {
    if (folder == rootPath || folder.startsWith(rootPath + QDir::separator()))
      syncFolderGroup(folder);
  }
}

void MainWindow::syncFolderGroup(const QString &folderPath) {
  if (!m_api->isAuthenticated() || folderPath.isEmpty())
    return;

  const int mapped = groupIdForFolder(folderPath);
  if (mapped >= 0) {
    trackFilesIn(mapped, folderPath);
    return;
  }

  // A directory earns a group by holding a PDF of its own. Pure container
  // directories — ones whose PDFs all live further down — get nothing.
  if (filesIn(folderPath).isEmpty())
    return;

  // Sign-out clears the id cache, so on the way back in we re-attach to the
  // group we already own under this name rather than creating a second one.
  const QString name = groupNameForFolder(folderPath);
  const ApiGroup existing = groupByName(name);
  if (existing.isValid() && existing.isOwner() && !existing.isPersonal) {
    m_db->storeFolderGroup(folderPath, existing.id);
    trackFilesIn(existing.id, folderPath);
    refreshDetailPane();
    return;
  }

  // Scans overlap — a directory change can land while the group for that same
  // directory is still being created — so hold the folder until it comes back
  // or a second group would be created under the same name.
  if (m_foldersCreatingGroup.contains(folderPath))
    return;
  m_foldersCreatingGroup.insert(folderPath);

  m_api->createGroup(
      name,
      [this, folderPath](const ApiGroup &group) {
        m_foldersCreatingGroup.remove(folderPath);
        m_db->storeFolderGroup(folderPath, group.id);
        reloadGroups([this, folderPath, group]() {
          trackFilesIn(group.id, folderPath);
          refreshDetailPane();
        });
      },
      [this, folderPath](const ApiError &error) {
        m_foldersCreatingGroup.remove(folderPath);
        showError(error);
      });
}

QStringList MainWindow::filesIn(const QString &folderPath) const {
  QStringList paths;
  for (const PdfFile &file : m_pdfModel->allFiles()) {
    // Directly in this directory only; a PDF one level down belongs to that
    // level's own group.
    if (file.folderPath == folderPath)
      paths << file.filePath;
  }
  return paths;
}

void MainWindow::trackFilesIn(int groupId, const QString &folderPath) {
  if (groupId < 0 || m_foldersTracking.contains(folderPath))
    return;

  QStringList pending;
  for (const QString &filePath : filesIn(folderPath)) {
    const QString hash = contentHashFor(filePath);
    if (hash.isEmpty() || m_db->remoteFileId(groupId, hash) >= 0)
      continue;
    pending << filePath;
  }

  if (pending.isEmpty())
    return;

  m_foldersTracking.insert(folderPath);
  registerNext(folderPath, groupId, pending);
}

void MainWindow::registerNext(const QString &folderPath, int groupId,
                              QStringList pending) {
  const QString groupName = groupNameForFolder(folderPath);

  // Releasing the in-flight marker has to happen however the chain ends, or a
  // later scan of the same folder would find it permanently "already running".
  const auto finished = [this, folderPath, groupName](const QString &status) {
    m_foldersTracking.remove(folderPath);
    m_scanLabel->setText(status.arg(groupName));
    QTimer::singleShot(3000, m_scanLabel, [this]() { m_scanLabel->clear(); });
    reloadGroups([this]() { refreshDetailPane(); });
  };

  if (pending.isEmpty()) {
    finished(QStringLiteral("✓ %1 up to date"));
    return;
  }

  const QString filePath = pending.takeFirst();
  m_scanLabel->setText(QStringLiteral("Adding %1 file(s) to %2…")
                           .arg(pending.size() + 1)
                           .arg(groupName));

  // One at a time: a directory can hold hundreds of PDFs and each registration
  // is a round trip. resolveRemoteFile caches the id it gets back, so a rescan
  // of the same folder costs nothing.
  resolveRemoteFile(
      groupId, filePath,
      [this, folderPath, groupId, pending](int) {
        registerNext(folderPath, groupId, pending);
      },
      [this, finished](const ApiError &error) {
        // One failure means the rest would almost certainly fail the same way,
        // so stop and report once rather than opening a modal per file.
        finished(QStringLiteral("⚠ %1 not fully shared"));
        showError(error);
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

void MainWindow::resolveRemoteFile(
    int groupId, const QString &filePath, std::function<void(int)> onReady,
    std::function<void(const ApiError &)> onFailed) {
  const auto fail = [this, onFailed](const ApiError &error) {
    if (onFailed)
      onFailed(error);
    else
      showError(error);
  };

  if (groupId < 0 || !m_api->isAuthenticated()) {
    fail(ApiError::network(
        QStringLiteral("Sign in before sharing files with a group.")));
    return;
  }

  const QString hash = contentHashFor(filePath);
  if (hash.isEmpty()) {
    fail(ApiError::network(QStringLiteral("Could not read %1 to identify it.")
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
      },
      onFailed);  // empty → ApiClient falls back to the modal
}

int MainWindow::activeGroupId() const {
  return groupIdForFolder(groupFolderFor(m_selectedFilePath));
}

ApiGroup MainWindow::activeGroup() const { return groupById(activeGroupId()); }

ApiGroup MainWindow::groupById(int groupId) const {
  if (groupId < 0)
    return {};
  for (const ApiGroup &group : m_groups) {
    if (group.id == groupId)
      return group;
  }
  return {};
}

ApiGroup MainWindow::groupByName(const QString &name) const {
  for (const ApiGroup &group : m_groups) {
    if (group.name.compare(name, Qt::CaseSensitive) == 0)
      return group;
  }
  return {};
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

void MainWindow::onRenameGroup() {
  const ApiGroup group = activeGroup();
  if (!group.isValid() || !group.isOwner())
    return;

  bool ok = false;
  const QString name = QInputDialog::getText(
      this, QStringLiteral("Rename Group"),
      QStringLiteral("Group name:\n\nThis renames the group everyone sees. The "
                     "folder on your disk keeps its own name."),
      QLineEdit::Normal, group.name, &ok);
  if (!ok || name.trimmed().isEmpty() || name.trimmed() == group.name)
    return;

  m_api->renameGroup(group.id, name.trimmed(), [this](const ApiGroup &) {
    reloadGroups([this]() { refreshDetailPane(); });
  });
}

void MainWindow::onInviteMember() {
  const ApiGroup group = activeGroup();
  const QString email = m_inviteEdit->text().trimmed();
  if (!group.isValid() || !group.isOwner() || email.isEmpty())
    return;

  m_api->addMember(group.id, email, [this](const ApiMember &) {
    m_inviteEdit->clear();
    refreshMembers();
    reloadGroups();
  });
}

void MainWindow::removeMember(const ApiMember &member) {
  const ApiGroup group = activeGroup();
  if (!group.isValid() || !group.isOwner())
    return;

  const auto reply = QMessageBox::question(
      this, QStringLiteral("Remove Member"),
      QStringLiteral("Remove %1 <%2> from '%3'?\n\nThey lose access to the "
                     "group's files, tags and notes. Notes they wrote stay.")
          .arg(member.displayName, member.email, group.name),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;

  m_api->removeMember(group.id, member.userId, [this]() {
    refreshMembers();
    reloadGroups();
  });
}

void MainWindow::onLeaveGroup() {
  const ApiGroup group = activeGroup();
  if (!group.isValid() || group.isOwner() || group.isPersonal)
    return;

  const auto reply = QMessageBox::question(
      this, QStringLiteral("Leave Group"),
      QStringLiteral("Leave '%1'?\n\nYou lose access to its shared tags and "
                     "notes. Your local copies of the PDFs are untouched.")
          .arg(group.name),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;

  const int groupId = group.id;
  m_api->removeMember(groupId, m_api->currentUser().id, [this, groupId]() {
    const QString folder = m_db->folderForGroup(groupId);
    if (!folder.isEmpty())
      m_db->forgetFolderGroup(folder);
    reloadGroups([this]() {
      reloadTagVocabulary();
      refreshDetailPane();
    });
  });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sync
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onSyncGroup() {
  const int groupId = activeGroupId();
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
        this, QStringLiteral("No Group Selected"),
        QStringLiteral("A tag vocabulary belongs to a group, and a group comes "
                       "from a watched folder. Sign in and select a file to "
                       "manage its folder's tags."));
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

  // The scan is what tells us which directories under this root hold PDFs, so
  // it is also when their groups get created and their files registered.
  // Anything already known to the backend is skipped, so a rescan is cheap.
  reconcileFoldersUnder(folder);
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

  // ── Group ─────────────────────────────────────────────────────────────────
  // Not a picker: the file's folder decides its group, so this only reports
  // which group that is and who is in it.
  auto *groupTitle = new QLabel(QStringLiteral("GROUP"), pane);
  groupTitle->setObjectName(QStringLiteral("sectionLabel"));
  root->addWidget(groupTitle);

  m_groupHeader = new QLabel(QStringLiteral("—"), pane);
  m_groupHeader->setWordWrap(true);
  QFont groupFont = m_groupHeader->font();
  groupFont.setBold(true);
  m_groupHeader->setFont(groupFont);
  root->addWidget(m_groupHeader);

  m_groupMeta = new QLabel(pane);
  m_groupMeta->setWordWrap(true);
  m_groupMeta->setStyleSheet(QStringLiteral("color: #8a8d95; font-size: 8pt;"));
  root->addWidget(m_groupMeta);

  auto *groupRow = new QHBoxLayout;
  m_renameGroupBtn = new QPushButton(QStringLiteral("Rename"), pane);
  m_leaveGroupBtn = new QPushButton(QStringLiteral("Leave"), pane);
  m_syncBtn = new QPushButton(QStringLiteral("Sync"), pane);
  groupRow->addWidget(m_renameGroupBtn);
  groupRow->addWidget(m_leaveGroupBtn);
  groupRow->addWidget(m_syncBtn);
  groupRow->addStretch();
  root->addLayout(groupRow);

  // ── Members ───────────────────────────────────────────────────────────────
  m_membersTitle = new QLabel(QStringLiteral("MEMBERS"), pane);
  m_membersTitle->setObjectName(QStringLiteral("sectionLabel"));
  root->addWidget(m_membersTitle);

  auto *memberScroll = new QScrollArea(pane);
  memberScroll->setWidgetResizable(true);
  memberScroll->setFrameShape(QFrame::NoFrame);
  memberScroll->setMaximumHeight(140);
  auto *memberBody = new QWidget(memberScroll);
  m_membersLayout = new QVBoxLayout(memberBody);
  m_membersLayout->setContentsMargins(0, 0, 0, 0);
  m_membersLayout->setSpacing(2);
  m_membersLayout->addStretch();
  memberScroll->setWidget(memberBody);
  root->addWidget(memberScroll);

  auto *inviteRow = new QHBoxLayout;
  m_inviteEdit = new QLineEdit(pane);
  m_inviteEdit->setPlaceholderText(QStringLiteral("teammate@example.com"));
  m_inviteBtn = new QPushButton(QStringLiteral("Invite"), pane);
  inviteRow->addWidget(m_inviteEdit);
  inviteRow->addWidget(m_inviteBtn);
  root->addLayout(inviteRow);

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

  connect(m_renameGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onRenameGroup);
  connect(m_leaveGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onLeaveGroup);
  connect(m_syncBtn, &QPushButton::clicked, this, &MainWindow::onSyncGroup);
  connect(m_addNoteBtn, &QPushButton::clicked, this, &MainWindow::onAddNote);
  connect(m_inviteBtn, &QPushButton::clicked, this,
          &MainWindow::onInviteMember);
  connect(m_inviteEdit, &QLineEdit::returnPressed, this,
          &MainWindow::onInviteMember);

  return pane;
}

void MainWindow::refreshDetailPane() {
  if (!m_detailTitle)
    return;

  const PdfFile file = m_pdfModel->fileByPath(m_selectedFilePath);
  const bool hasFile = file.isValid();
  const bool online = m_api && m_api->isAuthenticated();

  m_detailTitle->setText(hasFile ? breakableText(file.fileName)
                                 : QStringLiteral("No file selected"));
  // The wrapped text carries invisible break points, so the untouched name
  // stays reachable on hover.
  m_detailTitle->setToolTip(hasFile ? file.fileName : QString{});

  if (!online) {
    m_detailMeta->setText(QStringLiteral(
        "Sign in (File ▸ Sign In) to use groups, shared tags and notes."));
    m_detailMeta->setToolTip(QString{});
  } else if (hasFile) {
    m_detailMeta->setText(QStringLiteral("%1\n%2").arg(
        breakableText(file.filePath),
        breakableText(file.tags.join(QStringLiteral(", ")))));
    m_detailMeta->setToolTip(file.filePath);
  } else {
    m_detailMeta->setToolTip(QString{});
    m_detailMeta->clear();
  }

  refreshGroupHeader();

  const ApiGroup group = activeGroup();
  const bool hasGroup = group.isValid();
  const bool isOwner = hasGroup && group.isOwner();

  // Only the person who added the folder — the group's creator — may rename it
  // or change who is in it. Everyone else gets the roster and a way out.
  m_renameGroupBtn->setEnabled(isOwner && !group.isPersonal);
  m_renameGroupBtn->setToolTip(
      hasGroup && !isOwner
          ? QStringLiteral("Only the group's creator can rename it")
          : QString{});
  m_leaveGroupBtn->setEnabled(hasGroup && !isOwner && !group.isPersonal);
  m_leaveGroupBtn->setToolTip(
      isOwner ? QStringLiteral(
                    "You created this group — remove its folder to delete it")
              : QString{});
  m_syncBtn->setEnabled(hasGroup);

  m_inviteEdit->setEnabled(isOwner && !group.isPersonal);
  m_inviteBtn->setEnabled(isOwner && !group.isPersonal);
  m_inviteEdit->setPlaceholderText(
      isOwner ? QStringLiteral("teammate@example.com")
              : QStringLiteral("Only the group's creator can invite people"));

  refreshMembers();

  const bool canWriteNotes = online && hasFile && activeGroupId() >= 0;
  m_noteEdit->setEnabled(canWriteNotes);
  m_addNoteBtn->setEnabled(canWriteNotes);
  m_noteEdit->setPlaceholderText(
      canWriteNotes
          ? QStringLiteral("Add a note visible to '%1'…").arg(activeGroup().name)
          : QStringLiteral("Add a note…"));

  refreshNotes();
}

void MainWindow::refreshGroupHeader() {
  // The detail pane is built before the toolbar, so both halves have to exist
  // before either is written to.
  if (!m_groupHeader || !m_groupLabel)
    return;

  const bool online = m_api && m_api->isAuthenticated();
  const QString folder = groupFolderFor(m_selectedFilePath);
  const ApiGroup group = activeGroup();

  // The toolbar and the detail pane name the same thing, so they are filled in
  // together and can never disagree.
  m_groupHeader->setToolTip(QString{});
  m_groupMeta->setToolTip(QString{});

  if (!online) {
    m_groupHeader->setText(QStringLiteral("—"));
    m_groupMeta->setText(QStringLiteral(
        "Sign in to share a folder's PDFs with a group."));
    m_groupLabel->setText(QStringLiteral("—"));
    return;
  }

  if (m_selectedFilePath.isEmpty() || folder.isEmpty()) {
    m_groupHeader->setText(QStringLiteral("No file selected"));
    m_groupMeta->setText(QStringLiteral(
        "A file's group is the directory it sits in. Select a file to see its "
        "group."));
    m_groupLabel->setText(QStringLiteral("—"));
    return;
  }

  if (!group.isValid()) {
    // The directory is watched but its group has not been created yet —
    // usually a scan still in flight, or a failed registration.
    m_groupHeader->setText(breakableText(groupNameForFolder(folder)));
    m_groupHeader->setToolTip(groupNameForFolder(folder));
    m_groupMeta->setText(
        QStringLiteral("Not shared yet — this directory's group is still being "
                       "set up."));
    m_groupMeta->setToolTip(folder);
    m_groupLabel->setText(QStringLiteral("(pending)"));
    return;
  }

  m_groupHeader->setText(
      QStringLiteral("%1  ·  %2")
          .arg(breakableText(group.name),
               group.isOwner() ? QStringLiteral("you created it")
                               : QStringLiteral("you're a member")));
  m_groupHeader->setToolTip(group.name);
  // Subdirectories are groups of their own, so say what this count covers.
  m_groupMeta->setText(
      QStringLiteral("from %1\n%2 file(s) shared — subfolders are their own "
                     "groups")
          .arg(breakableText(folder))
          .arg(group.fileCount));
  m_groupMeta->setToolTip(folder);
  m_groupLabel->setText(group.name);
}

void MainWindow::refreshMembers() {
  if (!m_membersLayout)
    return;

  // Clear first, so a failed reload never leaves someone else's roster on
  // screen next to this group's name.
  while (QLayoutItem *item = m_membersLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  const ApiGroup group = activeGroup();
  if (!group.isValid() || !m_api->isAuthenticated()) {
    m_members.clear();
    m_membersTitle->setText(QStringLiteral("MEMBERS"));
    m_membersLayout->addStretch();
    return;
  }

  const int groupIdAtRequest = group.id;
  m_api->listMembers(
      groupIdAtRequest, [this, groupIdAtRequest](const QList<ApiMember> &members) {
        // The user may have selected a file in another folder meanwhile.
        if (groupIdAtRequest != activeGroupId())
          return;

        m_members = members;
        while (QLayoutItem *item = m_membersLayout->takeAt(0)) {
          delete item->widget();
          delete item;
        }

        const ApiGroup current = activeGroup();
        m_membersTitle->setText(
            QStringLiteral("MEMBERS (%1)").arg(m_members.size()));
        for (const ApiMember &member : m_members)
          m_membersLayout->addWidget(buildMemberRow(member, current));

        m_membersLayout->addStretch();
      });
}

QWidget *MainWindow::buildMemberRow(const ApiMember &member,
                                    const ApiGroup &group) {
  auto *row = new QWidget;
  auto *rl = new QHBoxLayout(row);
  rl->setContentsMargins(2, 2, 2, 2);
  rl->setSpacing(6);

  auto *who = new QLabel(
      QStringLiteral("%1  <%2>").arg(member.displayName, member.email), row);
  who->setToolTip(QStringLiteral("Joined %1").arg(
      member.joinedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"))));
  rl->addWidget(who);
  rl->addStretch();

  auto *role = new QLabel(member.isOwner() ? QStringLiteral("creator")
                                           : QStringLiteral("member"),
                          row);
  role->setStyleSheet(
      member.isOwner()
          ? QStringLiteral("color: #9fb3ff; font-size: 8pt;")
          : QStringLiteral("color: #6a6d75; font-size: 8pt;"));
  rl->addWidget(role);

  // Only the creator removes people, and never themselves — the backend
  // enforces both rules regardless, so this is presentation.
  if (group.isOwner() && !member.isOwner()) {
    auto *removeBtn = new QPushButton(QStringLiteral("✕"), row);
    removeBtn->setFlat(true);
    removeBtn->setCursor(Qt::PointingHandCursor);
    removeBtn->setToolTip(
        QStringLiteral("Remove %1 from this group").arg(member.displayName));
    removeBtn->setStyleSheet(
        QStringLiteral("padding: 0 6px; font-size: 9pt; color: #8a8d95;"));
    rl->addWidget(removeBtn);
    connect(removeBtn, &QPushButton::clicked, this,
            [this, member]() { removeMember(member); });
  }

  return row;
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
