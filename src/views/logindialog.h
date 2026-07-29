#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

class ApiClient;

/**
 * @brief Sign-in / sign-up sheet for the backend.
 *
 * Login failures are shown inline under the form rather than as a modal —
 * a wrong password is part of using a login form, not an error to interrupt
 * with. Everything else in the app reports through QMessageBox.
 */
class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(ApiClient* api, QWidget* parent = nullptr);

    [[nodiscard]] QString serverUrl() const;
    [[nodiscard]] bool    shouldStaySignedIn() const;

    void setServerUrl(const QString& url);
    void setEmail(const QString& email);

private slots:
    void onSubmit();
    void onModeToggled();

private:
    void buildUi();
    void setBusy(bool busy);
    void showMessage(const QString& message, bool isError = true);
    [[nodiscard]] bool isRegisterMode() const;

    ApiClient*   m_api          = nullptr;

    QLineEdit*   m_serverEdit   = nullptr;
    QLineEdit*   m_emailEdit    = nullptr;
    QLineEdit*   m_passwordEdit = nullptr;
    QLineEdit*   m_nameEdit     = nullptr;
    QLabel*      m_nameLabel    = nullptr;
    QCheckBox*   m_stayCheck    = nullptr;
    QLabel*      m_statusLabel  = nullptr;
    QPushButton* m_submitBtn    = nullptr;
    QPushButton* m_toggleBtn    = nullptr;

    bool m_registerMode = false;
};
