#include "recentview.h"
#include "models/pdfmodel.h"
#include "models/pdffile.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QMouseEvent>
#include <algorithm>

/**
 * @brief A single recent file card with thumbnail, name, and timestamp.
 */
class RecentCard : public QFrame
{
    Q_OBJECT

public:
    explicit RecentCard(const PdfFile& file, QWidget* parent = nullptr)
        : QFrame(parent), m_file(file)
    {
        setFrameShape(QFrame::NoFrame);
        setFixedHeight(RecentView::kCardHeight);
        setObjectName(QStringLiteral("recentCard"));
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(QStringLiteral(R"(
            #recentCard {
                background: #2d3035;
                border: 1px solid #3a3d42;
                border-radius: 6px;
                margin: 0px;
            }
            #recentCard:hover {
                background: #30333a;
                border: 1px solid #4a4d52;
            }
        )"));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(10);

        // Thumbnail
        auto* thumbLabel = new QLabel(this);
        thumbLabel->setFixedSize(48, 64);
        thumbLabel->setStyleSheet(QStringLiteral(R"(
            QLabel {
                background: #222428;
                border-radius: 4px;
                border: 1px solid #3a3d42;
            }
        )"));

        if (!m_file.thumbnail.isNull()) {
            QPixmap scaled = m_file.thumbnail.scaled(48, 64, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
            thumbLabel->setPixmap(scaled);
        } else {
            thumbLabel->setText(QStringLiteral("📄"));
            thumbLabel->setAlignment(Qt::AlignCenter);
            thumbLabel->setStyleSheet(QStringLiteral(R"(
                QLabel {
                    background: #222428;
                    border-radius: 4px;
                    border: 1px solid #3a3d42;
                    font-size: 24px;
                }
            )"));
        }

        // Content (name + timestamp)
        auto* contentLayout = new QVBoxLayout;
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(2);

        auto* nameLabel = new QLabel(m_file.fileName, this);
        nameLabel->setStyleSheet(QStringLiteral(R"(
            QLabel {
                color: #e0e3e8;
                font-size: 9pt;
                font-weight: 500;
            }
        )"));
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setWordWrap(false);
        nameLabel->setMaximumWidth(250);

        const QString timeStr = m_file.lastOpened.toString(QStringLiteral("dd MMM, hh:mm"));
        auto* timeLabel = new QLabel(timeStr, this);
        timeLabel->setStyleSheet(QStringLiteral(R"(
            QLabel {
                color: #7a7d85;
                font-size: 8pt;
            }
        )"));

        contentLayout->addWidget(nameLabel);
        contentLayout->addWidget(timeLabel);
        contentLayout->addStretch();

        layout->addWidget(thumbLabel);
        layout->addLayout(contentLayout);
        layout->addStretch();
    }

    const PdfFile& file() const { return m_file; }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton)
            emit clicked();
        QFrame::mouseReleaseEvent(event);
    }

signals:
    void clicked();

private:
    PdfFile m_file;
};

// ─────────────────────────────────────────────────────────────────────────────

RecentView::RecentView(PdfModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    buildUi();

    // Refresh whenever the underlying model changes
    connect(m_model, &QAbstractItemModel::dataChanged,  this, &RecentView::refresh);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &RecentView::refresh);
    connect(m_model, &QAbstractItemModel::modelReset,   this, &RecentView::refresh);

    refresh();
}

void RecentView::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header with title and count
    auto* headerFrame = new QFrame(this);
    headerFrame->setFrameShape(QFrame::NoFrame);
    headerFrame->setStyleSheet(QStringLiteral(R"(
        QFrame {
            background: #27292e;
            border-bottom: 1px solid #30333a;
        }
    )"));
    headerFrame->setFixedHeight(40);

    auto* headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(10, 0, 10, 0);
    headerLayout->setSpacing(6);

    auto* titleLabel = new QLabel(QStringLiteral("Recently Opened"), this);
    titleLabel->setStyleSheet(QStringLiteral(R"(
        QLabel {
            color: #8a8d95;
            font-size: 8pt;
            font-weight: bold;
            letter-spacing: 1px;
            text-transform: uppercase;
        }
    )"));

    auto* countLabel = new QLabel(QStringLiteral("(0)"), this);
    countLabel->setObjectName(QStringLiteral("recentCountLabel"));
    countLabel->setStyleSheet(QStringLiteral(R"(
        QLabel {
            color: #5a5d65;
            font-size: 8pt;
            font-weight: normal;
        }
    )"));

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(countLabel);
    headerLayout->addStretch();

    // Scroll area for cards
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setStyleSheet(QStringLiteral(R"(
        QScrollArea {
            background: #1e2024;
            border: none;
        }
        QScrollBar:vertical {
            background: #1e2024;
            width: 6px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3d42;
            border-radius: 3px;
            min-height: 20px;
        }
        QScrollBar::handle:vertical:hover {
            background: #4a4d52;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )"));
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);

    auto* scrollWidget = new QWidget(this);
    scrollWidget->setStyleSheet(QStringLiteral(R"(
        QWidget { background: #1e2024; }
    )"));
    m_cardsLayout = new QVBoxLayout(scrollWidget);
    m_cardsLayout->setContentsMargins(8, 8, 8, 8);
    m_cardsLayout->setSpacing(kCardSpacing);

    m_scrollArea->setWidget(scrollWidget);

    layout->addWidget(headerFrame);
    layout->addWidget(m_scrollArea);

    setLayout(layout);
}

void RecentView::refresh()
{
    buildCards();

    // Update count label
    if (auto* label = findChild<QLabel*>(QStringLiteral("recentCountLabel"))) {
        label->setText(QStringLiteral("(%1)").arg(m_itemCount));
    }
}

void RecentView::buildCards()
{
    // Clear existing cards
    while (m_cardsLayout->count() > 0) {
        auto* item = m_cardsLayout->takeAt(0);
        if (auto* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }

    // Collect files that have been opened, sort by descending lastOpened
    QList<PdfFile> opened;
    const QList<PdfFile> all = m_model->allFiles();
    for (const PdfFile& f : all) {
        if (f.lastOpened.isValid())
            opened.append(f);
    }

    std::sort(opened.begin(), opened.end(),
              [](const PdfFile& a, const PdfFile& b) {
                  return a.lastOpened > b.lastOpened;
              });

    if (opened.size() > kMaxItems)
        opened = opened.mid(0, kMaxItems);

    m_itemCount = opened.size();

    // Add cards
    for (const PdfFile& f : opened) {
        createCard(f);
    }

    m_cardsLayout->addStretch();
}

void RecentView::createCard(const PdfFile& file)
{
    auto* card = new RecentCard(file, this);
    connect(card, &RecentCard::clicked, this, [this, filePath = file.filePath]() {
        emit fileActivated(filePath);
    });

    m_cardsLayout->insertWidget(m_cardsLayout->count() - 1, card);
}

void RecentView::onToggleExpanded()
{
    m_isExpanded = !m_isExpanded;
    m_scrollArea->setVisible(m_isExpanded);
}

#include "recentview.moc"
