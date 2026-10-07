#include "ui/ProgressWindow.h"

#include "core/QueueManager.h"
#include "ui/Format.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

// ---- SegmentBar -----------------------------------------------------------------

SegmentBar::SegmentBar(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SegmentBar::setData(const QVector<SegmentSnapshot>& segments, qint64 total, double fallbackFraction)
{
    m_segments = segments;
    m_total = total;
    m_fallback = fallbackFraction;
    update();
}

void SegmentBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.fillRect(r, QColor("#eaeaec"));
    const QColor fill("#007AFF");
    const QColor start("#ff9500");

    if (m_total > 0 && !m_segments.isEmpty() && m_segments.first().end >= 0) {
        const double scale = r.width() / static_cast<double>(m_total);
        for (const SegmentSnapshot& s : m_segments) {
            const double x = r.left() + s.start * scale;
            p.fillRect(QRectF(x, r.top(), s.done * scale, r.height()), fill);
            p.fillRect(QRectF(x, r.top(), 1.5, r.height()), start);  // start position marker
        }
    } else {
        p.fillRect(QRectF(r.left(), r.top(), r.width() * m_fallback, r.height()), fill);
    }
    p.setPen(QColor("#999999"));
    p.drawRect(r);
}

// ---- ProgressWindow -----------------------------------------------------------------

ProgressWindow::ProgressWindow(QueueManager* queue, const QString& itemId, QWidget* parent)
    : QDialog(parent), m_queue(queue), m_id(itemId)
{
    const DownloadItem* item = queue->item(itemId);
    setWindowTitle(QStringLiteral("0% %1").arg(item ? item->filename : QString()));
    setMinimumSize(700, 500);
    setWindowFlags(Qt::Window);
    setAttribute(Qt::WA_DeleteOnClose, false);

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;

    // ===== Download Status Tab =====
    auto* statusTab = new QWidget;
    auto* statusLayout = new QVBoxLayout(statusTab);

    m_urlLabel = new QLabel(item ? item->url : QString());
    m_urlLabel->setWordWrap(true);
    m_urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_urlLabel->setStyleSheet("color: #0066cc; padding: 5px;");
    statusLayout->addWidget(m_urlLabel);

    auto* infoGroup = new QGroupBox;
    auto* infoLayout = new QFormLayout;
    m_statusLabel = new QLabel("Connecting...");
    m_statusLabel->setStyleSheet("color: blue; font-weight: bold;");
    infoLayout->addRow("Status:", m_statusLabel);
    m_filesizeLabel = new QLabel("Unknown");
    infoLayout->addRow("File size:", m_filesizeLabel);
    m_downloadedLabel = new QLabel("0 B (0 %)");
    infoLayout->addRow("Downloaded:", m_downloadedLabel);
    m_rateLabel = new QLabel("0 B/sec");
    infoLayout->addRow("Transfer rate:", m_rateLabel);
    m_timeLeftLabel = new QLabel("Unknown");
    infoLayout->addRow("Time left:", m_timeLeftLabel);
    m_resumeLabel = new QLabel("Unknown");
    infoLayout->addRow("Resume capability:", m_resumeLabel);
    infoGroup->setLayout(infoLayout);
    statusLayout->addWidget(infoGroup);

    m_mainProgress = new QProgressBar;
    m_mainProgress->setRange(0, 100);
    m_mainProgress->setTextVisible(true);
    m_mainProgress->setFormat("%p%");
    m_mainProgress->setMinimumHeight(25);
    statusLayout->addWidget(m_mainProgress);

    auto* btnLayout = new QHBoxLayout;
    auto* hideBtn = new QPushButton("<< Hide details");
    connect(hideBtn, &QPushButton::clicked, this, &QWidget::hide);
    m_pauseBtn = new QPushButton("Pause");
    connect(m_pauseBtn, &QPushButton::clicked, this, [this] {
        if (m_pauseBtn->text() == QLatin1String("Pause"))
            m_queue->pause(m_id);
        else
            m_queue->resume(m_id);
    });
    m_cancelBtn = new QPushButton("Cancel");
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] {
        if (m_cancelBtn->text() != QLatin1String("Close"))
            m_queue->cancel(m_id);
        hide();
    });
    btnLayout->addWidget(hideBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_pauseBtn);
    btnLayout->addWidget(m_cancelBtn);
    statusLayout->addLayout(btnLayout);

    auto* connLabel = new QLabel("Start positions and download progress by connections");
    connLabel->setAlignment(Qt::AlignCenter);
    statusLayout->addWidget(connLabel);

    m_segmentBar = new SegmentBar;
    statusLayout->addWidget(m_segmentBar);

    m_connText = new QTextEdit;
    m_connText->setReadOnly(true);
    m_connText->setMaximumHeight(150);
    m_connText->setStyleSheet("font-family: Consolas, 'DejaVu Sans Mono', monospace;");
    statusLayout->addWidget(m_connText);

    tabs->addTab(statusTab, "Download status");

    // ===== Speed Limiter Tab =====
    auto* speedTab = new QWidget;
    auto* speedLayout = new QVBoxLayout(speedTab);
    m_limitCheck = new QCheckBox("Use speed limiter for this download");
    m_limitSpin = new QSpinBox;
    m_limitSpin->setRange(1, 1000000);
    m_limitSpin->setSuffix(" KB/s");
    m_limitSpin->setValue(item && item->speedLimitKbps > 0 ? item->speedLimitKbps : 512);
    m_limitCheck->setChecked(item && item->speedLimitKbps > 0);
    m_limitSpin->setEnabled(m_limitCheck->isChecked());
    auto applyLimit = [this] {
        m_limitSpin->setEnabled(m_limitCheck->isChecked());
        m_queue->setItemSpeedLimit(m_id, m_limitCheck->isChecked() ? m_limitSpin->value() : 0);
    };
    connect(m_limitCheck, &QCheckBox::toggled, this, applyLimit);
    connect(m_limitSpin, &QSpinBox::editingFinished, this, applyLimit);
    auto* limitRow = new QHBoxLayout;
    limitRow->addWidget(new QLabel("Maximum download speed:"));
    limitRow->addWidget(m_limitSpin);
    limitRow->addStretch();
    speedLayout->addWidget(m_limitCheck);
    speedLayout->addLayout(limitRow);
    speedLayout->addWidget(new QLabel("The global limit from Settings still applies on top of this one."));
    speedLayout->addStretch();
    tabs->addTab(speedTab, "Speed Limiter");

    // ===== Options Tab =====
    auto* optionsTab = new QWidget;
    auto* optionsLayout = new QVBoxLayout(optionsTab);
    m_openFileCheck = new QCheckBox("Open the file when the download completes");
    m_openFolderCheck = new QCheckBox("Open the containing folder when the download completes");
    m_closeWhenDoneCheck = new QCheckBox("Close this window when the download completes");
    optionsLayout->addWidget(m_openFileCheck);
    optionsLayout->addWidget(m_openFolderCheck);
    optionsLayout->addWidget(m_closeWhenDoneCheck);
    optionsLayout->addStretch();
    tabs->addTab(optionsTab, "Options on completion");

    layout->addWidget(tabs);

    connect(queue, &QueueManager::itemStatus, this, [this](const QString& id, const QString& status) {
        if (id == m_id)
            updateStatus(status);
    });
    connect(queue, &QueueManager::itemError, this, [this](const QString& id, const QString& msg) {
        if (id == m_id)
            updateStatus(QStringLiteral("Error: ") + msg);
    });
    connect(queue, &QueueManager::itemCompleted, this, [this](const QString& id) {
        if (id == m_id)
            onCompleted();
    });
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &ProgressWindow::refresh);

    if (item)
        updateStatus(item->status == Status::Error ? QStringLiteral("Error: ") + item->errorMessage : item->status);
    refresh();
}

void ProgressWindow::showEvent(QShowEvent* event)
{
    m_timer.start();
    refresh();
    QDialog::showEvent(event);
}

void ProgressWindow::hideEvent(QHideEvent* event)
{
    m_timer.stop();
    QDialog::hideEvent(event);
}

void ProgressWindow::refresh()
{
    const DownloadItem* item = m_queue->item(m_id);
    if (!item)
        return;
    const qint64 total = item->totalSize;
    const qint64 downloaded = item->downloaded;
    const double speed = m_queue->isRunning(m_id) ? item->speedBps : 0;

    const int percent = total > 0 ? static_cast<int>(qMin<qint64>(100, downloaded * 100 / total)) : 0;
    setWindowTitle(QStringLiteral("%1% %2").arg(percent).arg(item->filename));
    m_mainProgress->setValue(percent);

    m_filesizeLabel->setText(total > 0 ? Fmt::sizeDetailed(static_cast<double>(total)) : QStringLiteral("Unknown"));
    if (total > 0)
        m_downloadedLabel->setText(QStringLiteral("%1 (%2 %)")
                                       .arg(Fmt::sizeDetailed(static_cast<double>(downloaded)))
                                       .arg(downloaded * 100.0 / total, 0, 'f', 2));
    else
        m_downloadedLabel->setText(Fmt::sizeDetailed(static_cast<double>(downloaded)));
    m_rateLabel->setText(Fmt::speedDetailed(speed));
    m_timeLeftLabel->setText(speed > 0 && total > 0 ? Fmt::timeDetailed((total - downloaded) / speed)
                                                    : QStringLiteral("Unknown"));
    if (item->kind == QLatin1String("generic") && item->status != Status::Queued) {
        m_resumeLabel->setText(item->resumable ? "Yes" : "No");
        m_resumeLabel->setStyleSheet(item->resumable ? "color: green;" : "color: red;");
    }

    const QVector<SegmentSnapshot> segs = m_queue->segments(m_id);
    m_segmentBar->setData(segs, total, percent / 100.0);

    QString text = QStringLiteral("N.\tDownloaded\t\tInfo\n") + QString(50, '-') + '\n';
    if (segs.isEmpty()) {
        text += m_queue->isRunning(m_id) ? QStringLiteral("1\t%1\t\t%2\n")
                                               .arg(Fmt::sizeDetailed(static_cast<double>(downloaded)), item->status)
                                         : QStringLiteral("\t\t\tNo active connections\n");
    } else {
        for (int i = 0; i < segs.size(); ++i)
            text += QStringLiteral("%1\t%2\t\t%3\n")
                        .arg(i + 1)
                        .arg(Fmt::sizeDetailed(static_cast<double>(segs[i].done)))
                        .arg(segs[i].info);
    }
    if (m_connText->toPlainText() != text)
        m_connText->setPlainText(text);
}

void ProgressWindow::updateStatus(const QString& status)
{
    m_statusLabel->setText(status);
    const QString lower = status.toLower();
    if (lower == QLatin1String("completed")) {
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_pauseBtn->setEnabled(false);
        m_cancelBtn->setText("Close");
    } else if (lower.contains("error")) {
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_pauseBtn->setText("Resume");
    } else if (lower == QLatin1String("paused") || lower == QLatin1String("canceled")) {
        m_statusLabel->setStyleSheet("color: orange; font-weight: bold;");
        m_pauseBtn->setText("Resume");
    } else {
        m_statusLabel->setStyleSheet("color: blue; font-weight: bold;");
        m_pauseBtn->setText("Pause");
        m_pauseBtn->setEnabled(true);
        m_cancelBtn->setText("Cancel");
    }
    refresh();
}

void ProgressWindow::onCompleted()
{
    if (m_completed)
        return;
    m_completed = true;
    updateStatus(Status::Completed);
    const DownloadItem* item = m_queue->item(m_id);
    if (!item)
        return;
    if (m_openFileCheck->isChecked())
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->finalPath()));
    if (m_openFolderCheck->isChecked())
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->saveDir));
    if (m_closeWhenDoneCheck->isChecked())
        hide();
}

void ProgressWindow::closeEvent(QCloseEvent* event)
{
    // Just hide instead of destroying
    hide();
    event->ignore();
}
