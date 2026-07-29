#pragma once
#include <QDialog>
#include <QString>

class DatabaseManager;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

/**
 * @brief Settings dialog for user preferences (view mode, theme, account).
 *
 * All settings are persisted via DatabaseManager::setSetting. The account
 * section only stores *where* the backend is and whether to keep the session —
 * no service credentials are held on this machine.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(DatabaseManager* db, QWidget* parent = nullptr);

signals:
    void darkModeChanged(bool enabled);
    /// The backend address changed, so the caller must sign in again.
    void serverChanged(const QString& serverUrl);

private slots:
    void accept() override;

private:
    void buildUi();
    void loadSettings();

    DatabaseManager* m_db;
    QCheckBox*  m_darkModeCheck     = nullptr;
    QComboBox*  m_defaultViewCbo    = nullptr;
    QLineEdit*  m_serverEdit        = nullptr;
    QLabel*     m_accountLabel      = nullptr;
    QCheckBox*  m_staySignedInCheck = nullptr;
    QString     m_originalServer;
};
