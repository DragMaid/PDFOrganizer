#pragma once
#include <QDialog>
#include <QStringList>

#include "api/apitypes.h"

class QLabel;
class QLineEdit;
class QPushButton;

/**
 * @brief Asks where a group that was just joined should land on this machine.
 *
 * Shown after the share code has already been redeemed, so the group here is
 * one the user demonstrably has access to — this dialog only decides the local
 * destination, never the permission.
 *
 * A group is named after the directory it came from on its creator's machine
 * ("Papers", "Papers/2023"), and that name is offered verbatim as the folder
 * name. Keeping the separator means joining "Papers" and "Papers/2023" into the
 * same parent reproduces the creator's layout instead of flattening it, which
 * matters because each of those directories is a group in its own right.
 *
 * The full destination is shown live underneath, along with whatever is wrong
 * with it — an existing folder, a name that escapes the chosen parent, or a
 * collision with a folder already being watched. OK stays disabled while the
 * destination is unusable, so the only thing left to confirm afterwards is
 * whether an existing folder may be overwritten.
 */
class JoinGroupDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param group           the group just joined, for its name and counts
    /// @param defaultParent   directory the folder is offered in initially
    /// @param watchedFolders  roots already watched, which must not be reused
    JoinGroupDialog(const ApiGroup& group, const QString& defaultParent,
                    QStringList watchedFolders, QWidget* parent = nullptr);

    /// Absolute path of the folder to create. Only meaningful after accept().
    [[nodiscard]] QString targetPath() const;

    /// Turn a group name into a folder name that is safe on this filesystem,
    /// keeping '/' so nested group names stay nested.
    [[nodiscard]] static QString suggestedFolderName(const QString& groupName);

    /// Files and directories directly inside @p path, or 0 if it does not exist.
    [[nodiscard]] static int entryCount(const QString& path);

private slots:
    void browseForParent();
    void revalidate();

private:
    /// Why the current destination cannot be used, or an empty string when it
    /// can. Non-blocking observations (an existing folder) are not errors.
    [[nodiscard]] QString blockingProblem(const QString& target) const;

    ApiGroup    m_group;
    QStringList m_watchedFolders;

    QLineEdit*   m_parentEdit = nullptr;
    QLineEdit*   m_nameEdit   = nullptr;
    QLabel*      m_pathLabel  = nullptr;
    QLabel*      m_noteLabel  = nullptr;
    QPushButton* m_okButton   = nullptr;
};
