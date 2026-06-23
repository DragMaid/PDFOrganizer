#include "settingsdialog.h"
#include "database/databasemanager.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QProcessEnvironment>

SettingsDialog::SettingsDialog(DatabaseManager* db, QWidget* parent)
    : QDialog(parent), m_db(db)
{
    setWindowTitle(QStringLiteral("Settings"));
    setMinimumWidth(340);
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

    auto* identityGrp = new QGroupBox(QStringLiteral("Identity"), this);
    auto* identityForm = new QFormLayout(identityGrp);
    m_githubUserEdit = new QLineEdit(this);
    m_githubUserEdit->setPlaceholderText(QStringLiteral("GitHub username"));
    identityForm->addRow(QStringLiteral("GitHub user:"), m_githubUserEdit);
    root->addWidget(identityGrp);

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

    const QString fallbackUser = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("USER"), QStringLiteral("local"));
    m_githubUserEdit->setText(
        m_db->getSetting(QStringLiteral("githubUser"), fallbackUser).toString());
}

void SettingsDialog::accept()
{
    const bool dark = m_darkModeCheck->isChecked();
    m_db->setSetting(QStringLiteral("darkMode"), dark);
    m_db->setSetting(QStringLiteral("defaultView"),
                     m_defaultViewCbo->currentData().toString());
    m_db->setSetting(QStringLiteral("githubUser"), m_githubUserEdit->text().trimmed());

    emit darkModeChanged(dark);
    QDialog::accept();
}
