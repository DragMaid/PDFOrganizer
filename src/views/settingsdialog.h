#pragma once
#include <QDialog>

class DatabaseManager;
class QCheckBox;
class QComboBox;
class QLineEdit;

/**
 * @brief Settings dialog for user preferences (view mode, theme, etc.)
 *
 * All settings are persisted via DatabaseManager::setSetting.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(DatabaseManager* db, QWidget* parent = nullptr);

signals:
    void darkModeChanged(bool enabled);

private slots:
    void accept() override;

private:
    void buildUi();
    void loadSettings();

    DatabaseManager* m_db;
    QCheckBox*  m_darkModeCheck  = nullptr;
    QComboBox*  m_defaultViewCbo = nullptr;
    QLineEdit*  m_githubUserEdit = nullptr;
};
