#include "mainwindow.h"

// ── Models
// ────────────────────────────────────────────────────────────────────
#include "models/pdfmodel.h"

// ── Infrastructure
// ────────────────────────────────────────────────────────────
#include "controllers/folderwatcher.h"
#include "controllers/pdfcontroller.h"
#include "controllers/tagcontroller.h"
#include "database/databasemanager.h"

// ── Views
// ─────────────────────────────────────────────────────────────────────
#include "views/folderpanel.h"
#include "views/gridview.h"
#include "views/listview.h"
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
#include <QComboBox>
#include <QCryptographicHash>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

static QString groupSlug(QString name) {
  name = name.toLower();
  name.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
               QStringLiteral("-"));
  name = name.trimmed();
  while (name.startsWith(QLatin1Char('-')))
    name.remove(0, 1);
  while (name.endsWith(QLatin1Char('-')))
    name.chop(1);
  return name.isEmpty() ? QStringLiteral("group") : name;
}

static bool githubRepoParts(const QString &repoUrl, QString *owner,
                            QString *repo) {
  const QString url = repoUrl.trimmed();

  // Accept common GitHub URL forms:
  //  - https://github.com/owner/repo or https://github.com/owner/repo.git
  //  - git@github.com:owner/repo.git
  //  - ssh://git@github.com/owner/repo.git
  const QRegularExpression httpsRe(
      QStringLiteral("^https://github\\.com/([^/]+)/([^/.]+)(?:\\.git)?/?$"));
  QRegularExpressionMatch match = httpsRe.match(url);
  if (match.hasMatch()) {
    *owner = match.captured(1);
    *repo = match.captured(2);
    return true;
  }

  const QRegularExpression sshShortRe(
      QStringLiteral("^git@github\\.com:([^/]+)/([^/.]+)(?:\\.git)?$"));
  match = sshShortRe.match(url);
  if (match.hasMatch()) {
    *owner = match.captured(1);
    *repo = match.captured(2);
    return true;
  }

  const QRegularExpression sshUrlRe(
      QStringLiteral("^ssh://git@github\\.com/([^/]+)/([^/.]+)(?:\\.git)?/?$"));
  match = sshUrlRe.match(url);
  if (match.hasMatch()) {
    *owner = match.captured(1);
    *repo = match.captured(2);
    return true;
  }

  return false;
}

static QNetworkReply *blockingGet(QNetworkAccessManager &nam,
                                  const QNetworkRequest &req) {
  QNetworkReply *reply = nam.get(req);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  return reply;
}

static QNetworkReply *blockingPut(QNetworkAccessManager &nam,
                                  const QNetworkRequest &req,
                                  const QByteArray &body) {
  QNetworkReply *reply = nam.put(req, body);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  return reply;
}

static QNetworkReply *blockingPost(QNetworkAccessManager &nam,
                                   const QNetworkRequest &req,
                                   const QByteArray &body) {
  QNetworkReply *reply = nam.post(req, body);
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  loop.exec();
  return reply;
}

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

  statusBar()->addWidget(m_statusLabel);
  statusBar()->addPermanentWidget(m_scanLabel);
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
  refreshDetailPane();
  if (m_rightTabs)
    m_rightTabs->setCurrentIndex(0);
}

void MainWindow::onEditTagsRequested(const QString &filePath) {
  const PdfFile f = m_pdfModel->fileByPath(filePath);
  if (!f.isValid())
    return;

  // Build a simple tag-assignment dialog
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Edit Tags — %1").arg(f.fileName));
  dlg.setMinimumWidth(320);

  auto *layout = new QVBoxLayout(&dlg);
  layout->addWidget(
      new QLabel(QStringLiteral("Select tags for this file:"), &dlg));

  auto *listWidget = new QListWidget(&dlg);
  listWidget->setSelectionMode(QAbstractItemView::MultiSelection);

  const QStringList allTags = m_tagModel->allTags();
  for (const QString &tag : allTags) {
    auto *item = new QListWidgetItem(tag, listWidget);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(f.hasTag(tag) ? Qt::Checked : Qt::Unchecked);
  }

  layout->addWidget(listWidget);

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

  m_tagCtrl->setFileTags(filePath, selected);
}

void MainWindow::onPdfOpened(const QString & /*filePath*/,
                             const QDateTime & /*when*/) {
  m_recentView->refresh();
  updateStatusBar();
}

void MainWindow::onAddNote() {
  const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
  if (!f.isValid())
    return;

  const QString author =
      m_db->getSetting(QStringLiteral("githubUser"), QStringLiteral("local"))
          .toString()
          .trimmed();
  if (author.isEmpty()) {
    QMessageBox::warning(
        this, QStringLiteral("GitHub User Required"),
        QStringLiteral(
            "Set your GitHub username in Settings before adding notes."));
    return;
  }

  if (m_db->addNote(f.id, author, m_noteEdit->toPlainText())) {
    m_noteEdit->clear();
    refreshDetailPane();
  }
}

void MainWindow::onCreateGroup() {
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("New Group"));
  auto *form = new QFormLayout(&dlg);

  QLineEdit nameEdit;
  QLineEdit folderEdit;
  QPushButton folderBrowse(QStringLiteral("Browse"));
  QHBoxLayout folderRow;
  folderRow.addWidget(&folderEdit);
  folderRow.addWidget(&folderBrowse);

  QComboBox authCombo;
  authCombo.addItem(QStringLiteral("None"));
  authCombo.addItem(QStringLiteral("Token"));
  authCombo.addItem(QStringLiteral("SSH (use key)"));

  QLineEdit githubToken;
  githubToken.setEchoMode(QLineEdit::Password);
  QLineEdit githubSshKey;
  QPushButton keyBrowse(QStringLiteral("Browse"));
  QHBoxLayout keyRow;
  keyRow.addWidget(&githubSshKey);
  keyRow.addWidget(&keyBrowse);

  QLineEdit b2Token;
  b2Token.setEchoMode(QLineEdit::Password);

  form->addRow(QStringLiteral("Group name:"), &nameEdit);
  form->addRow(QStringLiteral("Folder to track:"), &folderRow);
  form->addRow(QStringLiteral("GitHub auth:"), &authCombo);
  form->addRow(QStringLiteral("GitHub token:"), &githubToken);
  form->addRow(QStringLiteral("GitHub SSH key:"), &keyRow);
  form->addRow(QStringLiteral("B2 auth token:"), &b2Token);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  connect(&folderBrowse, &QPushButton::clicked, this, [&]() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Folder"));
    if (!dir.isEmpty())
      folderEdit.setText(dir);
  });
  connect(&keyBrowse, &QPushButton::clicked, this, [&]() {
    const QString fn =
        QFileDialog::getOpenFileName(this, QStringLiteral("Select SSH Key"));
    if (!fn.isEmpty())
      githubSshKey.setText(fn);
  });

  if (dlg.exec() != QDialog::Accepted)
    return;

  const QString name = nameEdit.text().trimmed();
  if (name.isEmpty())
    return;

  const int gid = m_db->createGroup(name);
  if (gid < 0) {
    QMessageBox::warning(this, QStringLiteral("Group Exists"),
                         QStringLiteral("Could not create that group."));
    return;
  }

  // Persist provided settings so user doesn't have to re-enter them.
  const QString folderPath = folderEdit.text().trimmed();
  if (!folderPath.isEmpty())
    m_db->saveGroupSetting(gid, QStringLiteral("folderPath"), folderPath);

  const QString authMethod = authCombo.currentText();
  if (authMethod != QStringLiteral("None"))
    m_db->saveGroupSetting(gid, QStringLiteral("githubAuthMethod"), authMethod);
  if (!githubToken.text().trimmed().isEmpty())
    m_db->saveGroupSetting(gid, QStringLiteral("githubToken"),
                           githubToken.text().trimmed());
  if (!githubSshKey.text().trimmed().isEmpty())
    m_db->saveGroupSetting(gid, QStringLiteral("githubSshKey"),
                           githubSshKey.text().trimmed());
  if (!b2Token.text().trimmed().isEmpty())
    m_db->saveGroupSetting(gid, QStringLiteral("b2AuthToken"),
                           b2Token.text().trimmed());

  // If a folder was supplied, automatically include all PDFs under it in the
  // group.
  if (!folderPath.isEmpty()) {
    for (const PdfFile &f : m_pdfModel->allFiles()) {
      if (f.folderPath.startsWith(folderPath))
        m_db->setFileInGroup(f.id, gid, true);
    }
  }

  refreshDetailPane();
}

void MainWindow::onEditGroup() {
  const int groupId = selectedGroupId();
  if (groupId < 0)
    return;

  FileGroup group = m_db->groupById(groupId);

  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Edit Group"));
  auto *form = new QFormLayout(&dlg);

  QLineEdit nameEdit;
  QLineEdit folderEdit;
  QPushButton folderBrowse(QStringLiteral("Browse"));
  QHBoxLayout folderRow;
  folderRow.addWidget(&folderEdit);
  folderRow.addWidget(&folderBrowse);

  QComboBox authCombo;
  authCombo.addItem(QStringLiteral("None"));
  authCombo.addItem(QStringLiteral("Token"));
  authCombo.addItem(QStringLiteral("SSH (use key)"));

  QLineEdit githubToken;
  githubToken.setEchoMode(QLineEdit::Password);
  QLineEdit githubSshKey;
  QPushButton keyBrowse(QStringLiteral("Browse"));
  QHBoxLayout keyRow;
  keyRow.addWidget(&githubSshKey);
  keyRow.addWidget(&keyBrowse);

  QLineEdit b2Token;
  b2Token.setEchoMode(QLineEdit::Password);
  QLineEdit b2Bucket;

  nameEdit.setText(group.name);
  folderEdit.setText(
      m_db->getGroupSetting(groupId, QStringLiteral("folderPath")));
  const QString savedAuth =
      m_db->getGroupSetting(groupId, QStringLiteral("githubAuthMethod"));
  if (savedAuth == QLatin1String("SSH (use key)"))
    authCombo.setCurrentIndex(2);
  else if (savedAuth == QLatin1String("Token"))
    authCombo.setCurrentIndex(1);
  githubToken.setText(
      m_db->getGroupSetting(groupId, QStringLiteral("githubToken")));
  githubSshKey.setText(
      m_db->getGroupSetting(groupId, QStringLiteral("githubSshKey")));
  b2Token.setText(
      m_db->getGroupSetting(groupId, QStringLiteral("b2AuthToken")));
  b2Bucket.setText(group.b2BucketName);

  form->addRow(QStringLiteral("Group name:"), &nameEdit);
  form->addRow(QStringLiteral("Folder to track:"), &folderRow);
  form->addRow(QStringLiteral("GitHub auth:"), &authCombo);
  form->addRow(QStringLiteral("GitHub token:"), &githubToken);
  form->addRow(QStringLiteral("GitHub SSH key:"), &keyRow);
  form->addRow(QStringLiteral("B2 auth token:"), &b2Token);
  form->addRow(QStringLiteral("B2 bucket:"), &b2Bucket);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(&folderBrowse, &QPushButton::clicked, this, [&]() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Folder"));
    if (!dir.isEmpty())
      folderEdit.setText(dir);
  });
  connect(&keyBrowse, &QPushButton::clicked, this, [&]() {
    const QString fn =
        QFileDialog::getOpenFileName(this, QStringLiteral("Select SSH Key"));
    if (!fn.isEmpty())
      githubSshKey.setText(fn);
  });

  if (dlg.exec() != QDialog::Accepted)
    return;

  const QString newName = nameEdit.text().trimmed();
  if (newName.isEmpty())
    return;

  if (!m_db->renameGroup(groupId, newName))
    QMessageBox::warning(this, QStringLiteral("Rename Failed"),
                         QStringLiteral("Could not rename group."));

  const QString newFolder = folderEdit.text().trimmed();
  m_db->saveGroupSetting(groupId, QStringLiteral("folderPath"), newFolder);
  m_db->saveGroupSetting(groupId, QStringLiteral("githubAuthMethod"),
                         authCombo.currentText());
  m_db->saveGroupSetting(groupId, QStringLiteral("githubToken"),
                         githubToken.text().trimmed());
  m_db->saveGroupSetting(groupId, QStringLiteral("githubSshKey"),
                         githubSshKey.text().trimmed());
  m_db->saveGroupSetting(groupId, QStringLiteral("b2AuthToken"),
                         b2Token.text().trimmed());

  // If folder changed, reset members and add files under that folder
  if (!newFolder.isEmpty()) {
    m_db->clearGroupMembers(groupId);
    for (const PdfFile &f : m_pdfModel->allFiles()) {
      if (f.folderPath.startsWith(newFolder))
        m_db->setFileInGroup(f.id, groupId, true);
    }
  }

  refreshDetailPane();
}

void MainWindow::onRemoveGroup() {
  const int groupId = selectedGroupId();
  if (groupId < 0)
    return;
  const FileGroup group = m_db->groupById(groupId);
  const auto reply = QMessageBox::question(
      this, QStringLiteral("Remove Group"),
      QStringLiteral("Delete group '%1' and remove its associations?")
          .arg(group.name),
      QMessageBox::Yes | QMessageBox::Cancel);
  if (reply != QMessageBox::Yes)
    return;
  m_db->deleteGroup(groupId);
  refreshDetailPane();
}

void MainWindow::onGroupItemChanged(QListWidgetItem *item) {
  const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
  if (!f.isValid() || !item)
    return;
  m_db->setFileInGroup(f.id, item->data(Qt::UserRole).toInt(),
                       item->checkState() == Qt::Checked);
}

void MainWindow::onSyncGroup() {
  const int groupId = selectedGroupId();
  if (groupId < 0)
    return;

  const FileGroup group = m_db->groupById(groupId);
  QList<PdfFile> files;
  for (const PdfFile &f : m_pdfModel->allFiles()) {
    if (m_db->fileGroupIds(f.id).contains(groupId))
      files.append(f);
  }
  if (files.isEmpty()) {
    QMessageBox::information(this, QStringLiteral("Nothing To Sync"),
                             QStringLiteral("This group has no files."));
    return;
  }

  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Sync Group"));
  auto *form = new QFormLayout(&dlg);
  QLineEdit repoUrl;
  QLineEdit githubToken;
  QCheckBox useSsh(QStringLiteral("Push via SSH (use key)"));
  QLineEdit sshKeyPath;
  QPushButton sshBrowse(QStringLiteral("Browse"));
  QHBoxLayout sshRow;
  sshRow.addWidget(&sshKeyPath);
  sshRow.addWidget(&sshBrowse);
  QLineEdit b2ApiUrl;
  QLineEdit b2BucketId;
  QLineEdit b2AuthToken;
  QLineEdit b2Prefix;
  repoUrl.setText(group.githubRepoUrl);
  b2ApiUrl.setText(QStringLiteral("https://api.backblazeb2.com"));
  b2BucketId.setText(group.b2BucketName);
  b2Prefix.setText(
      QStringLiteral("pdforganizer/%1").arg(groupSlug(group.name)));
  githubToken.setEchoMode(QLineEdit::Password);
  b2AuthToken.setEchoMode(QLineEdit::Password);

  // Prefill saved credentials if available
  const QString savedAuthMethod =
      m_db->getGroupSetting(groupId, QStringLiteral("githubAuthMethod"));
  const QString savedGithubToken =
      m_db->getGroupSetting(groupId, QStringLiteral("githubToken"));
  const QString savedSshKey =
      m_db->getGroupSetting(groupId, QStringLiteral("githubSshKey"));
  const QString savedB2Token =
      m_db->getGroupSetting(groupId, QStringLiteral("b2AuthToken"));
  if (!savedGithubToken.isEmpty())
    githubToken.setText(savedGithubToken);
  if (!savedSshKey.isEmpty())
    sshKeyPath.setText(savedSshKey);
  if (!savedB2Token.isEmpty())
    b2AuthToken.setText(savedB2Token);
  if (savedAuthMethod == QLatin1String("SSH (use key)"))
    useSsh.setChecked(true);

  form->addRow(QStringLiteral("GitHub repo URL:"), &repoUrl);
  form->addRow(QStringLiteral("GitHub token:"), &githubToken);
  form->addRow(QStringLiteral(""), &useSsh);
  form->addRow(QStringLiteral("SSH key:"), &sshRow);
  form->addRow(QStringLiteral("B2 API URL:"), &b2ApiUrl);
  form->addRow(QStringLiteral("B2 bucket ID:"), &b2BucketId);
  form->addRow(QStringLiteral("B2 auth token:"), &b2AuthToken);
  form->addRow(QStringLiteral("B2 prefix:"), &b2Prefix);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(&sshBrowse, &QPushButton::clicked, this, [&]() {
    const QString fn =
        QFileDialog::getOpenFileName(this, QStringLiteral("Select SSH Key"));
    if (!fn.isEmpty())
      sshKeyPath.setText(fn);
  });
  if (dlg.exec() != QDialog::Accepted)
    return;

  QString owner;
  QString repo;
  if (!githubRepoParts(repoUrl.text(), &owner, &repo)) {
    QMessageBox::warning(this, QStringLiteral("Invalid Repo"),
                         QStringLiteral("Use https://github.com/owner/repo."));
    return;
  }
  const bool haveGithubToken = !githubToken.text().trimmed().isEmpty();
  if (!haveGithubToken && !useSsh.isChecked()) {
    QMessageBox::warning(
        this, QStringLiteral("Missing Sync Credentials"),
        QStringLiteral("Provide a GitHub token or enable SSH push."));
    return;
  }
  if (b2AuthToken.text().trimmed().isEmpty() ||
      b2BucketId.text().trimmed().isEmpty() ||
      b2ApiUrl.text().trimmed().isEmpty()) {
    QMessageBox::warning(
        this, QStringLiteral("Missing Sync Credentials"),
        QStringLiteral(
            "B2 API URL, bucket ID, and B2 auth token are required."));
    return;
  }

  QJsonObject metadata;
  metadata[QStringLiteral("group")] = group.name;
  metadata[QStringLiteral("syncedAt")] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  QJsonArray filesJson;
  QJsonArray tagsJson;
  for (const QString &tag : m_tagModel->allTags())
    tagsJson.append(tag);
  for (const PdfFile &f : files) {
    QJsonObject obj;
    obj[QStringLiteral("path")] = f.filePath;
    obj[QStringLiteral("fileName")] = f.fileName;
    obj[QStringLiteral("fileSize")] = QString::number(f.fileSizeBytes);
    obj[QStringLiteral("lastModified")] = f.lastModified.toString(Qt::ISODate);
    QJsonArray fileTags;
    for (const QString &tag : f.tags)
      fileTags.append(tag);
    obj[QStringLiteral("tags")] = fileTags;
    QJsonArray notes;
    for (const FileNote &note : m_db->loadNotes(f.id)) {
      QJsonObject n;
      n[QStringLiteral("author")] = note.author;
      n[QStringLiteral("body")] = note.body;
      n[QStringLiteral("createdAt")] = note.createdAt.toString(Qt::ISODate);
      notes.append(n);
    }
    obj[QStringLiteral("notes")] = notes;
    filesJson.append(obj);
  }
  metadata[QStringLiteral("tags")] = tagsJson;
  metadata[QStringLiteral("files")] = filesJson;

  const QString metaPath =
      QStringLiteral("pdforganizer/groups/%1.json").arg(groupSlug(group.name));

  QNetworkAccessManager nam;

  // Persist credentials the user supplied for future syncs
  m_db->saveGroupSetting(groupId, QStringLiteral("githubAuthMethod"),
                         useSsh.isChecked() ? QStringLiteral("SSH (use key)")
                                            : QStringLiteral("Token"));
  if (!githubToken.text().trimmed().isEmpty())
    m_db->saveGroupSetting(groupId, QStringLiteral("githubToken"),
                           githubToken.text().trimmed());
  if (!sshKeyPath.text().trimmed().isEmpty())
    m_db->saveGroupSetting(groupId, QStringLiteral("githubSshKey"),
                           sshKeyPath.text().trimmed());
  if (!b2AuthToken.text().trimmed().isEmpty())
    m_db->saveGroupSetting(groupId, QStringLiteral("b2AuthToken"),
                           b2AuthToken.text().trimmed());

  if (useSsh.isChecked()) {
    // Attempt to push metadata via git+ssh using the provided key
    const QString repoSsh =
        QStringLiteral("git@github.com:%1/%2.git").arg(owner, repo);
    if (sshKeyPath.text().trimmed().isEmpty()) {
      QMessageBox::warning(
          this, QStringLiteral("SSH Key Required"),
          QStringLiteral("Provide an SSH key to push via SSH."));
      return;
    }

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
      QMessageBox::warning(
          this, QStringLiteral("Sync Failed"),
          QStringLiteral("Could not create temporary directory."));
      return;
    }
    const QString root = tmpDir.path();
    QDir repoDir(root);

    QProcess git;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // const QString sshCmd =
    //     QStringLiteral(
    //         "ssh -i %1 -o IdentitiesOnly=yes -o StrictHostKeyChecking=no")
    //         .arg(sshKeyPath.text().trimmed());
    // env.insert("GIT_SSH_COMMAND", sshCmd);
    // git.setProcessEnvironment(env);

    auto runGit = [&](const QStringList &args) -> bool {
      git.setWorkingDirectory(root);
      git.start(QStringLiteral("git"), args);
      if (!git.waitForFinished(30000))
        return false;
      return git.exitCode() == 0;
    };

    if (!runGit({QStringLiteral("init")})) {
      QMessageBox::warning(
          this, QStringLiteral("Git Init Failed"),
          QStringLiteral("Could not initialize temporary git repository."));
      return;
    }

    // write metadata file
    const QString fullPath = repoDir.filePath(metaPath);
    repoDir.mkpath(QFileInfo(fullPath).path());
    QFile out(fullPath);
    if (!out.open(QIODevice::WriteOnly)) {
      QMessageBox::warning(this, QStringLiteral("Write Failed"),
                           QStringLiteral("Could not write metadata file."));
      return;
    }
    out.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    out.close();

    if (!runGit({QStringLiteral("add"), QStringLiteral(".")}) ||
        !runGit({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("Sync PDF Organizer metadata")})) {
      QMessageBox::warning(this, QStringLiteral("Git Commit Failed"),
                           QStringLiteral("Could not commit metadata."));
      return;
    }

    if (!runGit({QStringLiteral("remote"), QStringLiteral("add"),
                 QStringLiteral("origin"), repoSsh})) {
      QMessageBox::warning(this, QStringLiteral("Git Remote Failed"),
                           QStringLiteral("Could not add remote."));
      return;
    }

    // Push to origin (attempt to push to main branch)
    if (!runGit({QStringLiteral("push"), QStringLiteral("origin"),
                 QStringLiteral("HEAD:refs/heads/main")})) {
      QMessageBox::warning(this, QStringLiteral("Git Push Failed"),
                           QStringLiteral("Could not push metadata via SSH."));
      return;
    }

    m_db->saveGroupGithubValidation(groupId, repoUrl.text(),
                                    QStringLiteral("synced"));
  } else {
    const QString contentsUrl =
        QStringLiteral("https://api.github.com/repos/%1/%2/contents/%3")
            .arg(owner, repo, metaPath);

    QNetworkRequest getReq{QUrl(contentsUrl)};
    getReq.setRawHeader("Accept", "application/vnd.github+json");
    getReq.setRawHeader("Authorization",
                        "Bearer " + githubToken.text().trimmed().toUtf8());
    getReq.setRawHeader("User-Agent", "PDFOrganizer");
    QNetworkReply *getReply = blockingGet(nam, getReq);
    const QByteArray getBody = getReply->readAll();
    const bool hasExistingMeta = getReply->error() == QNetworkReply::NoError;
    const int getStatus =
        getReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    getReply->deleteLater();
    if (!hasExistingMeta && getStatus != 404) {
      QMessageBox::warning(
          this, QStringLiteral("GitHub Sync Failed"),
          QStringLiteral("Could not read existing metadata from GitHub."));
      return;
    }

    QJsonObject putBody;
    putBody[QStringLiteral("message")] =
        QStringLiteral("Sync PDF Organizer metadata for %1").arg(group.name);
    putBody[QStringLiteral("content")] = QString::fromLatin1(
        QJsonDocument(metadata).toJson(QJsonDocument::Indented).toBase64());
    if (hasExistingMeta)
      putBody[QStringLiteral("sha")] = QJsonDocument::fromJson(getBody)
                                           .object()
                                           .value(QStringLiteral("sha"))
                                           .toString();

    QNetworkRequest putReq{QUrl(contentsUrl)};
    putReq.setHeader(QNetworkRequest::ContentTypeHeader,
                     QStringLiteral("application/json"));
    putReq.setRawHeader("Accept", "application/vnd.github+json");
    putReq.setRawHeader("Authorization",
                        "Bearer " + githubToken.text().trimmed().toUtf8());
    putReq.setRawHeader("User-Agent", "PDFOrganizer");
    QNetworkReply *putReply = blockingPut(
        nam, putReq, QJsonDocument(putBody).toJson(QJsonDocument::Compact));
    const bool githubOk = putReply->error() == QNetworkReply::NoError;
    putReply->deleteLater();
    if (!githubOk) {
      QMessageBox::warning(
          this, QStringLiteral("GitHub Sync Failed"),
          QStringLiteral(
              "Could not write metadata. Check token contents-write access."));
      return;
    }
    m_db->saveGroupGithubValidation(groupId, repoUrl.text(),
                                    QStringLiteral("synced"));
  }

  QUrl uploadUrl(b2ApiUrl.text().trimmed() +
                 QStringLiteral("/b2api/v4/b2_get_upload_url"));
  QUrlQuery uploadQuery;
  uploadQuery.addQueryItem(QStringLiteral("bucketId"),
                           b2BucketId.text().trimmed());
  uploadUrl.setQuery(uploadQuery);
  QNetworkRequest uploadUrlReq(uploadUrl);
  uploadUrlReq.setRawHeader("Authorization",
                            b2AuthToken.text().trimmed().toUtf8());
  QNetworkReply *uploadUrlReply = blockingGet(nam, uploadUrlReq);
  const QByteArray uploadUrlBody = uploadUrlReply->readAll();
  const bool uploadUrlOk = uploadUrlReply->error() == QNetworkReply::NoError;
  uploadUrlReply->deleteLater();
  if (!uploadUrlOk) {
    QMessageBox::warning(
        this, QStringLiteral("B2 Sync Failed"),
        QStringLiteral(
            "Could not get a B2 upload URL from the provided auth token."));
    return;
  }

  const QJsonObject uploadInfo =
      QJsonDocument::fromJson(uploadUrlBody).object();
  const QUrl fileUploadUrl(
      uploadInfo.value(QStringLiteral("uploadUrl")).toString());
  const QByteArray fileUploadToken =
      uploadInfo.value(QStringLiteral("authorizationToken"))
          .toString()
          .toUtf8();
  int uploaded = 0;
  int skipped = 0;
  for (const PdfFile &f : files) {
    if (m_db->wasFileUploaded(groupId, f.id, f.fileSizeBytes, f.lastModified)) {
      ++skipped;
      continue;
    }

    QFile file(f.filePath);
    if (!file.open(QIODevice::ReadOnly))
      continue;
    const QByteArray content =
        file.readAll(); // ponytail: stream when large PDFs become a problem.
    const QByteArray sha1 =
        QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex();
    const QString objectName =
        QStringLiteral("%1/%2-%3")
            .arg(b2Prefix.text().trimmed(), QString::number(f.id), f.fileName);

    QNetworkRequest uploadReq(fileUploadUrl);
    uploadReq.setHeader(QNetworkRequest::ContentTypeHeader,
                        QStringLiteral("b2/x-auto"));
    uploadReq.setHeader(QNetworkRequest::ContentLengthHeader, content.size());
    uploadReq.setRawHeader("Authorization", fileUploadToken);
    uploadReq.setRawHeader("X-Bz-File-Name",
                           QUrl::toPercentEncoding(objectName, "/"));
    uploadReq.setRawHeader("X-Bz-Content-Sha1", sha1);
    QNetworkReply *uploadReply = blockingPost(nam, uploadReq, content);
    const QByteArray body = uploadReply->readAll();
    const bool ok = uploadReply->error() == QNetworkReply::NoError;
    uploadReply->deleteLater();
    if (!ok) {
      QMessageBox::warning(
          this, QStringLiteral("B2 Sync Failed"),
          QStringLiteral("Upload failed for %1.").arg(f.fileName));
      return;
    }
    const QString b2FileId = QJsonDocument::fromJson(body)
                                 .object()
                                 .value(QStringLiteral("fileId"))
                                 .toString();
    m_db->markFileUploaded(groupId, f.id, f.fileSizeBytes, f.lastModified,
                           b2FileId);
    ++uploaded;
  }

  m_db->saveGroupB2Validation(groupId, QString{}, b2BucketId.text(), QString{},
                              QStringLiteral("synced"));
  refreshDetailPane();
  QMessageBox::information(
      this, QStringLiteral("Sync Complete"),
      QStringLiteral(
          "Metadata pushed. Uploaded %1 file(s), skipped %2 unchanged file(s).")
          .arg(uploaded)
          .arg(skipped));
}

void MainWindow::onSearchTextChanged(const QString &text) {
  m_proxy->setSearchText(text);
  updateStatusBar();
}

void MainWindow::openTagManager() {
  TagManagerDialog dlg(m_tagModel, m_tagCtrl, this);
  dlg.exec();
  m_folderPanel->refresh();
}

void MainWindow::openSettings() {
  SettingsDialog dlg(m_db, this);
  connect(&dlg, &SettingsDialog::darkModeChanged, this,
          &MainWindow::applyDarkTheme);
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
  root->addWidget(m_groupList);

  auto *groupRow = new QHBoxLayout;
  auto *addGroupBtn = new QPushButton(QStringLiteral("Add Group"), pane);
  m_editGroupBtn = new QPushButton(QStringLiteral("Edit Group"), pane);
  m_removeGroupBtn = new QPushButton(QStringLiteral("Remove Group"), pane);
  m_syncBtn = new QPushButton(QStringLiteral("Sync Group"), pane);
  groupRow->addWidget(addGroupBtn);
  groupRow->addWidget(m_editGroupBtn);
  groupRow->addWidget(m_removeGroupBtn);
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

  connect(addGroupBtn, &QPushButton::clicked, this, &MainWindow::onCreateGroup);
  connect(m_editGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onEditGroup);
  connect(m_removeGroupBtn, &QPushButton::clicked, this,
          &MainWindow::onRemoveGroup);
  connect(m_syncBtn, &QPushButton::clicked, this, &MainWindow::onSyncGroup);
  connect(m_addNoteBtn, &QPushButton::clicked, this, &MainWindow::onAddNote);
  connect(m_groupList, &QListWidget::itemChanged, this,
          &MainWindow::onGroupItemChanged);
  connect(m_groupList, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem *current) {
            const bool enabled = current && !m_selectedFilePath.isEmpty();
            m_editGroupBtn->setEnabled(enabled);
            m_removeGroupBtn->setEnabled(enabled);
            m_syncBtn->setEnabled(enabled);
          });

  return pane;
}

void MainWindow::refreshDetailPane() {
  const PdfFile f = m_pdfModel->fileByPath(m_selectedFilePath);
  const bool hasFile = f.isValid();
  m_detailTitle->setText(hasFile ? f.fileName
                                 : QStringLiteral("No file selected"));
  m_detailMeta->setText(hasFile
                            ? QStringLiteral("%1\n%2").arg(
                                  f.filePath, f.tags.join(QStringLiteral(", ")))
                            : QString{});
  m_groupList->setEnabled(hasFile);
  m_noteEdit->setEnabled(hasFile);
  m_addNoteBtn->setEnabled(hasFile);
  m_editGroupBtn->setEnabled(hasFile && selectedGroupId() >= 0);
  m_removeGroupBtn->setEnabled(hasFile && selectedGroupId() >= 0);
  m_syncBtn->setEnabled(hasFile && selectedGroupId() >= 0);

  {
    const QSignalBlocker blocker(m_groupList);
    m_groupList->clear();
    const QList<int> fileGroups =
        hasFile ? m_db->fileGroupIds(f.id) : QList<int>{};
    for (const FileGroup &group : m_db->loadGroups()) {
      auto *item = new QListWidgetItem(group.name, m_groupList);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setData(Qt::UserRole, group.id);
      item->setToolTip(QStringLiteral("GitHub: %1\nB2: %2")
                           .arg(group.githubStatus, group.b2Status));
      item->setCheckState(fileGroups.contains(group.id) ? Qt::Checked
                                                        : Qt::Unchecked);
    }
  }

  while (QLayoutItem *item = m_notesLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  if (hasFile) {
    for (const FileNote &note : m_db->loadNotes(f.id)) {
      // Create a chat-like bubble for each note
      auto *bubble = new QWidget;
      bubble->setObjectName(QStringLiteral("noteBubble"));
      bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
      auto *bl = new QVBoxLayout(bubble);
      bl->setContentsMargins(10, 8, 10, 8);
      bl->setSpacing(6);

      auto *header =
          new QLabel(QStringLiteral("%1 · %2").arg(
                         note.author, note.createdAt.toLocalTime().toString(
                                          QStringLiteral("yyyy-MM-dd hh:mm"))),
                     bubble);
      QFont hfont = header->font();
      hfont.setBold(true);
      header->setFont(hfont);
      header->setStyleSheet(QStringLiteral("color: #9fb3ff; font-size: 9pt;"));

      auto *body = new QLabel(note.body, bubble);
      body->setWordWrap(true);
      body->setTextInteractionFlags(Qt::TextSelectableByMouse);

      bl->addWidget(header);
      bl->addWidget(body);

      // Light styling for bubble; works with both themes
      bubble->setStyleSheet(QStringLiteral(
          "QWidget#noteBubble { background: rgba(77,142,255,0.08); border: 1px "
          "solid rgba(77,142,255,0.14); border-radius: 8px; }"));

      m_notesLayout->addWidget(bubble);
    }
  }
  m_notesLayout->addStretch();
}

int MainWindow::selectedGroupId() const {
  QListWidgetItem *item = m_groupList ? m_groupList->currentItem() : nullptr;
  return item ? item->data(Qt::UserRole).toInt() : -1;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dark theme
// ─────────────────────────────────────────────────────────────────────────────

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
