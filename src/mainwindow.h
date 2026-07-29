#pragma once
#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStackedWidget>
#include <functional>

#include "api/apitypes.h"

class QComboBox;
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
class ApiClient;

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
 *  ├─[ToolBar: search | Group | List/Grid | Tag Mgr | Settings]──────────┤
 *  │ ┌──[FolderPanel]──┬──[StackedWidget:  ├──[RecentView]──────────┐ │
 *  │ │  Folders        │   ListView |      │  Recently Opened       │ │
 *  │ │  Tag filters    │   GridView]       │  Card-based panel      │ │
 *  │ │                 │   PDF cards/rows  │                        │ │
 *  │ └─────────────────┴───────────────────┴────────────────────────┘ │
 *  └─[StatusBar: count | scan status | signed-in user]───────────────────┘
 *
 * Local vs. shared state
 * ──────────────────────
 * Folder watching, scanning and thumbnails stay local (DatabaseManager).
 * Groups, tags, notes and file storage belong to the backend and are reached
 * only through ApiClient — this class holds no database or storage credential.
 *
 * The **active group** (toolbar combo) is the permission context for
 * everything shared: it decides which vocabulary a tag lands in and which
 * team sees a note. New accounts get a private "Personal" group so this is
 * never empty.
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

    // ── Session ───────────────────────────────────────────────────────────────
    void promptSignIn();
    void onSignOut();
    void onApiError(const ApiError& error);
    void onSessionExpired();

    // ── Groups ────────────────────────────────────────────────────────────────
    void onActiveGroupChanged(int index);
    void onCreateGroup();
    void onRenameGroup();
    void onDeleteGroup();
    void onManageMembers();
    void onGroupItemChanged(QListWidgetItem* item);
    void onSyncGroup();

    // ── Notes ─────────────────────────────────────────────────────────────────
    void onAddNote();

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
    void refreshGroupList();
    void refreshNotes();
    void setCollaborationEnabled(bool enabled);

    /// One note, with Edit/Delete shown only when the signed-in user wrote it.
    QWidget* buildNoteBubble(const ApiNote& note);
    void editNote  (const ApiNote& note);
    void deleteNote(const ApiNote& note);

    // ── Session ───────────────────────────────────────────────────────────────
    void restoreSessionOrPrompt();
    void onSignedIn();
    void saveSession();
    void clearSavedSession();

    // ── Backend helpers ───────────────────────────────────────────────────────
    void reloadGroups(std::function<void()> onDone = {});
    void reloadTagVocabulary();

    /// SHA-256 for a local file, cached in SQLite so a file is hashed once.
    QString contentHashFor(const QString& filePath);

    /// Resolve a local path to its backend file id inside @p groupId,
    /// registering it if this is the first time. The callback does not run if
    /// resolution fails — the failure has already been shown to the user.
    void resolveRemoteFile(int groupId, const QString& filePath,
                           std::function<void(int)> onReady);

    [[nodiscard]] int      activeGroupId() const;
    [[nodiscard]] ApiGroup activeGroup()   const;
    [[nodiscard]] ApiGroup groupById(int groupId) const;
    [[nodiscard]] int      selectedGroupId() const;

    void showError(const ApiError& error);
    void uploadNext(int groupId, QList<ApiFile> pending, int uploaded,
                    int skipped, class QProgressDialog* progress);

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
    ApiClient*       m_api         = nullptr;

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
    QPushButton*     m_addGroupBtn = nullptr;
    QPushButton*     m_renameGroupBtn = nullptr;
    QPushButton*     m_removeGroupBtn = nullptr;
    QPushButton*     m_membersBtn  = nullptr;
    QPushButton*     m_syncBtn     = nullptr;
    QString          m_selectedFilePath;

    // ── Toolbar widgets ───────────────────────────────────────────────────────
    QLineEdit*       m_searchEdit  = nullptr;
    QComboBox*       m_groupCombo  = nullptr;
    QAction*         m_listAction  = nullptr;
    QAction*         m_gridAction  = nullptr;
    QAction*         m_signInAction  = nullptr;
    QAction*         m_signOutAction = nullptr;

    // ── Status bar ────────────────────────────────────────────────────────────
    QLabel*          m_statusLabel = nullptr;
    QLabel*          m_scanLabel   = nullptr;
    QLabel*          m_userLabel   = nullptr;

    // ── Backend state ─────────────────────────────────────────────────────────
    QList<ApiGroup>  m_groups;
    QList<ApiNote>   m_notes;          ///< Notes for the selected file
    int              m_activeGroupId = -1;
    /// Groups the selected file belongs to, refreshed whenever it changes.
    QSet<int>        m_selectedFileGroups;
    bool             m_signingIn = false;
};
