#include "settingsdialog.h"
#include "database/databasemanager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>

SettingsDialog::SettingsDialog(DatabaseManager* db, QWidget* parent)
    : QDialog(parent), m_db(db)
{
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumWidth(380);
    buildUi();
    loadSettings();
}

void SettingsDialog::buildUi()
{
    auto* root = new QVBoxLayout(this);

    // ── Appearance ────────────────────────────────────────────────────────────
    auto* appearGrp = new QGroupBox(QStringLiteral("Appearance"), this);
    auto* form      = new QFormLayout(appearGrp);

    m_darkModeCheck = new QCheckBox(QStringLiteral("Dark mode"), this);
    form->addRow(m_darkModeCheck);

    m_defaultViewCbo = new QComboBox(this);
    m_defaultViewCbo->addItem(QStringLiteral("List View"),  QStringLiteral("list"));
    m_defaultViewCbo->addItem(QStringLiteral("Grid View"),  QStringLiteral("grid"));
    form->addRow(QStringLiteral("Default view:"), m_defaultViewCbo);

    root->addWidget(appearGrp);

    // ── Account ───────────────────────────────────────────────────────────────
    auto* accountGrp  = new QGroupBox(QStringLiteral("Account"), this);
    auto* accountForm = new QFormLayout(accountGrp);

    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText(QStringLiteral("https://coolwebsite.com"));
    accountForm->addRow(QStringLiteral("Server:"), m_serverEdit);

    m_accountLabel = new QLabel(this);
    m_accountLabel->setWordWrap(true);
    accountForm->addRow(QStringLiteral("Signed in as:"), m_accountLabel);

    m_staySignedInCheck =
        new QCheckBox(QStringLiteral("Stay signed in on this computer"), this);
    accountForm->addRow(m_staySignedInCheck);

    auto* note = new QLabel(
        QStringLiteral("Changing the server signs you out. Groups, tags and "
                       "notes live on the server, not on this machine."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #8a8d95; font-size: 8pt;"));
    accountForm->addRow(note);

    root->addWidget(accountGrp);
    root->addStretch();

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void SettingsDialog::loadSettings()
{
    m_darkModeCheck->setChecked(
        m_db->getSetting(QStringLiteral("darkMode"), true).toBool());

    const QString view = m_db->getSetting(
        QStringLiteral("defaultView"), QStringLiteral("list")).toString();
    const int idx = m_defaultViewCbo->findData(view);
    m_defaultViewCbo->setCurrentIndex(idx >= 0 ? idx : 0);

    m_originalServer = m_db->getSetting(QStringLiteral("serverUrl")).toString();
    m_serverEdit->setText(m_originalServer);

    const QString email = m_db->getSetting(QStringLiteral("userEmail")).toString();
    const QString name  =
        m_db->getSetting(QStringLiteral("userDisplayName")).toString();
    m_accountLabel->setText(email.isEmpty()
                                ? QStringLiteral("Not signed in")
                                : QStringLiteral("%1 <%2>").arg(name, email));

    m_staySignedInCheck->setChecked(
        m_db->getSetting(QStringLiteral("staySignedIn"), true).toBool());
}

void SettingsDialog::accept()
{
    const bool dark = m_darkModeCheck->isChecked();
    m_db->setSetting(QStringLiteral("darkMode"), dark);
    m_db->setSetting(QStringLiteral("defaultView"),
                     m_defaultViewCbo->currentData().toString());

    const bool stay = m_staySignedInCheck->isChecked();
    m_db->setSetting(QStringLiteral("staySignedIn"), stay);
    if (!stay)
        m_db->setSetting(QStringLiteral("refreshToken"), QString{});

    const QString server = m_serverEdit->text().trimmed();
    m_db->setSetting(QStringLiteral("serverUrl"), server);

    emit darkModeChanged(dark);

    // A different server means different accounts, groups and file ids, so the
    // cached remote ids and the saved session must not carry over.
    if (server != m_originalServer) {
        m_db->setSetting(QStringLiteral("refreshToken"), QString{});
        m_db->clearRemoteCache();
        emit serverChanged(server);
    }

    QDialog::accept();
}
