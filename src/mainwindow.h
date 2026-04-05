#pragma once
#include <QMainWindow>
#include <QStackedWidget>

class QLineEdit;
class QToolBar;
class QLabel;
class QSplitter;
class QDockWidget;
class QAction;

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
 *  │ ┌──[FolderPanel]──┬──[StackedWidget: ListView | GridView]──────────┐ │
 *  │ │  Folders        │                                                 │ │
 *  │ │  Tag filters    │   PDF cards / rows                              │ │
 *  │ │                 │                                                 │ │
 *  │ └─────────────────┴─────────────────────────────────────────────────┘ │
 *  ├─[Dock: RecentView]──────────────────────────────────────────────────┤
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
    void initDockWidgets();
    void connectSignals();
    void restoreLayout();
    void saveLayout();
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
    RecentView*      m_recentView  = nullptr;
    QDockWidget*     m_recentDock  = nullptr;

    // ── Toolbar widgets ───────────────────────────────────────────────────────
    QLineEdit*       m_searchEdit  = nullptr;
    QAction*         m_listAction  = nullptr;
    QAction*         m_gridAction  = nullptr;

    // ── Status bar ────────────────────────────────────────────────────────────
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_scanLabel   = nullptr;
};
