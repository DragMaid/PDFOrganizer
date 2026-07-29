#include "logindialog.h"
#include "api/apiclient.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

LoginDialog::LoginDialog(ApiClient* api, QWidget* parent)
    : QDialog(parent), m_api(api)
{
    setWindowTitle(QStringLiteral("Sign In — PDF Organizer"));
    setMinimumWidth(400);
    buildUi();
}

void LoginDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    auto* heading = new QLabel(QStringLiteral("Connect to your PDF Organizer server"), this);
    heading->setWordWrap(true);
    root->addWidget(heading);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText(QStringLiteral("http://localhost:8000"));
    form->addRow(QStringLiteral("Server:"), m_serverEdit);

    m_emailEdit = new QLineEdit(this);
    m_emailEdit->setPlaceholderText(QStringLiteral("you@example.com"));
    form->addRow(QStringLiteral("Email:"), m_emailEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Password:"), m_passwordEdit);

    m_nameEdit  = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("Shown on your notes"));
    m_nameLabel = new QLabel(QStringLiteral("Display name:"), this);
    form->addRow(m_nameLabel, m_nameEdit);
    m_nameLabel->hide();
    m_nameEdit->hide();

    root->addLayout(form);

    m_stayCheck = new QCheckBox(QStringLiteral("Stay signed in on this computer"), this);
    m_stayCheck->setChecked(true);
    root->addWidget(m_stayCheck);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();
    root->addWidget(m_statusLabel);

    auto* buttonRow = new QHBoxLayout;
    m_toggleBtn = new QPushButton(QStringLiteral("Create an account"), this);
    m_toggleBtn->setFlat(true);
    auto* cancelBtn = new QPushButton(QStringLiteral("Quit"), this);
    m_submitBtn = new QPushButton(QStringLiteral("Sign In"), this);
    m_submitBtn->setDefault(true);

    buttonRow->addWidget(m_toggleBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(cancelBtn);
    buttonRow->addWidget(m_submitBtn);
    root->addLayout(buttonRow);

    connect(m_submitBtn, &QPushButton::clicked, this, &LoginDialog::onSubmit);
    connect(m_toggleBtn, &QPushButton::clicked, this, &LoginDialog::onModeToggled);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::onSubmit);
    connect(m_emailEdit, &QLineEdit::returnPressed, this, &LoginDialog::onSubmit);
}

bool LoginDialog::isRegisterMode() const
{
    return m_registerMode;
}

QString LoginDialog::serverUrl() const
{
    return m_serverEdit->text().trimmed();
}

bool LoginDialog::shouldStaySignedIn() const
{
    return m_stayCheck->isChecked();
}

void LoginDialog::setServerUrl(const QString& url)
{
    m_serverEdit->setText(url);
}

void LoginDialog::setEmail(const QString& email)
{
    m_emailEdit->setText(email);
    if (!email.isEmpty())
        m_passwordEdit->setFocus();
}

void LoginDialog::onModeToggled()
{
    m_registerMode = !m_registerMode;

    m_nameLabel->setVisible(m_registerMode);
    m_nameEdit->setVisible(m_registerMode);
    m_submitBtn->setText(m_registerMode ? QStringLiteral("Create Account")
                                        : QStringLiteral("Sign In"));
    m_toggleBtn->setText(m_registerMode
                             ? QStringLiteral("I already have an account")
                             : QStringLiteral("Create an account"));
    setWindowTitle(m_registerMode
                       ? QStringLiteral("Create Account — PDF Organizer")
                       : QStringLiteral("Sign In — PDF Organizer"));
    m_statusLabel->hide();
    adjustSize();
}

void LoginDialog::setBusy(bool busy)
{
    m_submitBtn->setEnabled(!busy);
    m_toggleBtn->setEnabled(!busy);
    m_serverEdit->setEnabled(!busy);
    m_emailEdit->setEnabled(!busy);
    m_passwordEdit->setEnabled(!busy);
    m_nameEdit->setEnabled(!busy);
    m_submitBtn->setText(busy ? QStringLiteral("Working…")
                              : m_registerMode ? QStringLiteral("Create Account")
                                               : QStringLiteral("Sign In"));
}

void LoginDialog::showMessage(const QString& message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(isError
                                     ? QStringLiteral("color: #ff6b6b;")
                                     : QStringLiteral("color: #7ac77a;"));
    m_statusLabel->show();
    adjustSize();
}

void LoginDialog::onSubmit()
{
    const QString server   = serverUrl();
    const QString email    = m_emailEdit->text().trimmed();
    const QString password = m_passwordEdit->text();
    const QString name     = m_nameEdit->text().trimmed();

    if (server.isEmpty()) {
        showMessage(QStringLiteral("Enter the address of your server."));
        return;
    }
    const QUrl url(server);
    if (!url.isValid() || url.host().isEmpty()) {
        showMessage(QStringLiteral(
            "That server address isn't valid. It should look like "
            "http://localhost:8000"));
        return;
    }
    if (email.isEmpty() || password.isEmpty()) {
        showMessage(QStringLiteral("Enter your email address and password."));
        return;
    }
    if (m_registerMode && name.isEmpty()) {
        showMessage(QStringLiteral(
            "Enter a display name — it's what your teammates see on your notes."));
        return;
    }
    if (m_registerMode && password.size() < 8) {
        showMessage(QStringLiteral("Choose a password of at least 8 characters."));
        return;
    }

    m_api->setBaseUrl(url);
    setBusy(true);
    m_statusLabel->hide();

    auto onSuccess = [this](const ApiUser&) {
        setBusy(false);
        accept();
    };
    auto onFailure = [this](const ApiError& error) {
        setBusy(false);
        showMessage(error.message);
    };

    if (m_registerMode)
        m_api->registerAccount(email, password, name, onSuccess, onFailure);
    else
        m_api->login(email, password, onSuccess, onFailure);
}
