#include "joingroupdialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

/// Characters no mainstream filesystem accepts in a name. '/' survives on
/// purpose — it is what turns a nested group name into a nested folder.
constexpr auto kIllegal = R"(<>:"\|?*)";

QString sanitizeSegment(const QString& segment)
{
    QString clean;
    clean.reserve(segment.size());
    for (const QChar ch : segment) {
        // Control characters are as unwelcome as the punctuation above.
        clean += (ch.unicode() < 0x20 || QLatin1StringView(kIllegal).contains(ch))
                     ? QLatin1Char('_')
                     : ch;
    }
    // Windows rejects both, and a trailing dot silently disappears there.
    while (clean.endsWith(QLatin1Char('.')) || clean.endsWith(QLatin1Char(' ')))
        clean.chop(1);
    return clean.trimmed();
}

} // namespace

QString JoinGroupDialog::suggestedFolderName(const QString& groupName)
{
    QStringList segments;
    for (const QString& raw : groupName.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QString clean = sanitizeSegment(raw);
        // ".." would climb out of the chosen parent; a segment that sanitizes
        // away entirely would collapse the path.
        if (!clean.isEmpty() && clean != QLatin1String(".")
            && clean != QLatin1String(".."))
            segments << clean;
    }
    return segments.isEmpty() ? QStringLiteral("Shared Folder")
                              : segments.join(QLatin1Char('/'));
}

int JoinGroupDialog::entryCount(const QString& path)
{
    QDir dir(path);
    if (!dir.exists())
        return 0;
    return static_cast<int>(
        dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)
            .size());
}

JoinGroupDialog::JoinGroupDialog(const ApiGroup& group,
                                 const QString& defaultParent,
                                 QStringList watchedFolders, QWidget* parent)
    : QDialog(parent), m_group(group),
      m_watchedFolders(std::move(watchedFolders))
{
    setWindowTitle(QStringLiteral("Sync Shared Folder"));
    setMinimumWidth(520);

    auto* root = new QVBoxLayout(this);

    auto* heading = new QLabel(
        QStringLiteral("You joined '%1'.").arg(m_group.name), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    heading->setWordWrap(true);
    root->addWidget(heading);

    auto* summary = new QLabel(
        QStringLiteral("%1 file(s) shared · %2 member(s) · you're a %3\n\n"
                       "Choose where the synced folder should live. Its PDFs are "
                       "downloaded into it, and it is watched from then on, so "
                       "anything you add there is shared back with the group.")
            .arg(m_group.fileCount)
            .arg(m_group.memberCount)
            .arg(m_group.isOwner() ? QStringLiteral("owner")
                                   : QStringLiteral("member")),
        this);
    summary->setWordWrap(true);
    root->addWidget(summary);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto* parentRow = new QHBoxLayout;
    m_parentEdit = new QLineEdit(QDir::toNativeSeparators(defaultParent), this);
    auto* browseBtn = new QPushButton(QStringLiteral("Browse…"), this);
    parentRow->addWidget(m_parentEdit);
    parentRow->addWidget(browseBtn);
    form->addRow(QStringLiteral("Location:"), parentRow);

    m_nameEdit = new QLineEdit(suggestedFolderName(m_group.name), this);
    m_nameEdit->setToolTip(
        QStringLiteral("Taken from the group's name. A '/' creates nested "
                       "folders, matching the layout it was shared from."));
    form->addRow(QStringLiteral("Folder name:"), m_nameEdit);

    root->addLayout(form);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setWordWrap(true);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_pathLabel);

    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    root->addWidget(m_noteLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_okButton = buttons->addButton(QStringLiteral("Sync Here"),
                                   QDialogButtonBox::AcceptRole);
    m_okButton->setDefault(true);
    root->addWidget(buttons);

    connect(browseBtn, &QPushButton::clicked, this,
            &JoinGroupDialog::browseForParent);
    connect(m_parentEdit, &QLineEdit::textChanged, this,
            &JoinGroupDialog::revalidate);
    connect(m_nameEdit, &QLineEdit::textChanged, this,
            &JoinGroupDialog::revalidate);
    connect(m_okButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    revalidate();
}

QString JoinGroupDialog::targetPath() const
{
    const QString parentDir = m_parentEdit->text().trimmed();
    const QString name = m_nameEdit->text().trimmed();
    if (parentDir.isEmpty() || name.isEmpty())
        return {};
    return QDir::cleanPath(QDir(parentDir).absoluteFilePath(name));
}

void JoinGroupDialog::browseForParent()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Where should the synced folder go?"),
        m_parentEdit->text());
    if (!chosen.isEmpty())
        m_parentEdit->setText(QDir::toNativeSeparators(chosen));
}

QString JoinGroupDialog::blockingProblem(const QString& target) const
{
    const QString parentDir = m_parentEdit->text().trimmed();
    if (parentDir.isEmpty())
        return QStringLiteral("Choose a location.");
    if (m_nameEdit->text().trimmed().isEmpty())
        return QStringLiteral("Give the folder a name.");
    if (target.isEmpty())
        return QStringLiteral("That name cannot be used.");

    if (!QDir(parentDir).exists())
        return QStringLiteral("That location does not exist.");

    // A name of "../x" or "/tmp/x" would put the folder somewhere other than the
    // location shown above it, which is the one thing this dialog must not do.
    const QString parentAbs =
        QDir::cleanPath(QDir(parentDir).absolutePath()) + QDir::separator();
    if (!target.startsWith(parentAbs))
        return QStringLiteral("The folder name has to stay inside the location.");

    const QFileInfo info(target);
    if (info.exists() && !info.isDir())
        return QStringLiteral("A file of that name is already there.");

    for (const QString& watched : m_watchedFolders) {
        const QString clean = QDir::cleanPath(watched);
        if (clean == target) {
            return QStringLiteral(
                "You already watch that folder. Pick another name, or remove it "
                "first (right-click it under Folders).");
        }
        if (clean.startsWith(target + QDir::separator())) {
            return QStringLiteral("That folder contains '%1', which you already "
                                  "watch. Pick another location.")
                .arg(QDir(clean).dirName());
        }
        if (target.startsWith(clean + QDir::separator())) {
            return QStringLiteral(
                       "That would put the group inside '%1', which you already "
                       "watch — its PDFs would be shared twice. Pick a location "
                       "outside it.")
                .arg(QDir(clean).dirName());
        }
    }

    return {};
}

void JoinGroupDialog::revalidate()
{
    const QString target = targetPath();

    m_pathLabel->setText(
        target.isEmpty()
            ? QString{}
            : QStringLiteral("Folder: %1").arg(QDir::toNativeSeparators(target)));

    const QString problem = blockingProblem(target);
    if (!problem.isEmpty()) {
        m_noteLabel->setStyleSheet(QStringLiteral("color: #d9534f;"));
        m_noteLabel->setText(problem);
        m_okButton->setEnabled(false);
        return;
    }

    m_okButton->setEnabled(true);

    // Not an error — an existing folder is usable, it just needs a decision
    // about its contents, which is confirmed after this dialog closes.
    const int existing = entryCount(target);
    if (existing > 0) {
        m_noteLabel->setStyleSheet(QStringLiteral("color: #e0a800;"));
        m_noteLabel->setText(
            QStringLiteral("⚠ That folder already exists and holds %1 item(s). "
                           "You'll be asked whether to replace them.")
                .arg(existing));
    } else if (QFileInfo::exists(target)) {
        m_noteLabel->setStyleSheet(QStringLiteral("color: #8a8d95;"));
        m_noteLabel->setText(
            QStringLiteral("That folder already exists and is empty."));
    } else {
        m_noteLabel->setStyleSheet(QStringLiteral("color: #8a8d95;"));
        m_noteLabel->setText(QStringLiteral("This folder will be created."));
    }
}
