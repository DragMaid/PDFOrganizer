#pragma once
#include <QMainWindow>
#include <QStackedWidget>

class QLineEdit;
class QToolBar;
class QLabel;
class QSplitter;
class QAction;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTabWidget;
class QTextEdit;
class QVBoxLayout;

class PdfModel;
class TagModel;
class FolderModel;
class FolderTreeModel;
class DatabaseManager;
class FolderWatcher;
class PdfController;
class TagController;
class SearchFilterProxy;

class FolderPanel;
class ListView;
class GridView;
class RecentView;

/**
 * @brief Application shell: assembles all controllers, models, and views.
 *
 * Layout
 * ──────
 *  ┌─[MenuBar]───────────────────────────────────────────────────────────┐
 *  ├─[ToolBar: search | List/Grid toggle | Tag Mgr | Settings]───────────┤
 *  │ ┌──[FolderPanel]──┬──[StackedWidget:  ├──[RecentView]──────────┐ │
 *  │ │  Folders        │   ListView |      │  Recently Opened       │ │
 *  │ │  Tag filters    │   GridView]       │  Card-based panel      │ │
 *  │ │                 │   PDF cards/rows  │                        │ │
 *  │ └─────────────────┴───────────────────┴────────────────────────┘ │
 *  └─[StatusBar: count | scan status]───────────────────────────────────┘
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // ── View switching ────────────────────────────────────────────────────────
    void switchToListView();
    void switchToGridView();

    // ── Folder management ─────────────────────────────────────────────────────
    void onAddFolderRequested();
    void onRemoveFolderRequested(const QString& path);

    // ── PDF events ────────────────────────────────────────────────────────────
    void onFileActivated   (const QString& filePath);
    void onEditTagsRequested(const QString& filePath);
    void onPdfOpened       (const QString& filePath, const QDateTime& when);
    void onFileSelected    (const QString& filePath);
    void onAddNote();
    void onCreateGroup();
    void onEditGroup();
    void onRemoveGroup();
    void onGroupItemChanged(QListWidgetItem* item);
    void onSyncGroup();

    // ── Search ────────────────────────────────────────────────────────────────
    void onSearchTextChanged(const QString& text);

    // ── Misc ──────────────────────────────────────────────────────────────────
    void openTagManager();
    void openSettings();
    void updateStatusBar();
    void applyDarkTheme(bool enabled);
    void onScanFinished(const QString& folder);

private:
    void initModels();
    void initControllers();
    void initViews();
    void initToolBar();
    void initMenuBar();
    void initStatusBar();
    void connectSignals();
    QWidget* buildDetailPane();
    void refreshDetailPane();
    int selectedGroupId() const;
    void restoreLayout();
    void saveLayout();
    QString dataDir() const;
    QString dbPath() const;

    // ── Models ────────────────────────────────────────────────────────────────
    PdfModel*        m_pdfModel      = nullptr;
    TagModel*        m_tagModel      = nullptr;
    FolderModel*     m_folderModel   = nullptr;
    FolderTreeModel* m_folderTreeModel = nullptr;
    SearchFilterProxy* m_proxy       = nullptr;

    // ── Infrastructure ────────────────────────────────────────────────────────
    DatabaseManager* m_db          = nullptr;
    FolderWatcher*   m_watcher     = nullptr;
    PdfController*   m_pdfCtrl     = nullptr;
    TagController*   m_tagCtrl     = nullptr;

    // ── Views ─────────────────────────────────────────────────────────────────
    QSplitter*       m_splitter    = nullptr;
    FolderPanel*     m_folderPanel = nullptr;
    QStackedWidget*  m_viewStack   = nullptr;
    ListView*        m_listView    = nullptr;
    GridView*        m_gridView    = nullptr;
    QTabWidget*      m_rightTabs   = nullptr;
    RecentView*      m_recentView  = nullptr;
    QLabel*          m_detailTitle = nullptr;
    QLabel*          m_detailMeta  = nullptr;
    QListWidget*     m_groupList   = nullptr;
    QTextEdit*       m_noteEdit    = nullptr;
    QVBoxLayout*     m_notesLayout = nullptr;
    QPushButton*     m_addNoteBtn  = nullptr;
    QPushButton*     m_editGroupBtn = nullptr;
    QPushButton*     m_removeGroupBtn = nullptr;
    QPushButton*     m_syncBtn     = nullptr;
    QString          m_selectedFilePath;

    // ── Toolbar widgets ───────────────────────────────────────────────────────
    QLineEdit*       m_searchEdit  = nullptr;
    QAction*         m_listAction  = nullptr;
    QAction*         m_gridAction  = nullptr;

    // ── Status bar ────────────────────────────────────────────────────────────
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_scanLabel   = nullptr;
};
