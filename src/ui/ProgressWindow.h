// Flux Downloader - Individual Download Progress Window (IDM-style)
// Shows detailed progress for a single download, including the live state of
// every connection, a per-download speed limiter and on-completion actions.
#pragma once

#include "core/DownloadEngine.h"

#include <QDialog>
#include <QTimer>
#include <QVector>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QueueManager;

// Draws one bar where each connection's downloaded range is filled in.
class SegmentBar : public QWidget {
    Q_OBJECT
public:
    explicit SegmentBar(QWidget* parent = nullptr);
    void setData(const QVector<SegmentSnapshot>& segments, qint64 total, double fallbackFraction);
    QSize sizeHint() const override { return {400, 22}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QVector<SegmentSnapshot> m_segments;
    qint64 m_total = 0;
    double m_fallback = 0;
};

class ProgressWindow : public QDialog {
    Q_OBJECT
public:
    ProgressWindow(QueueManager* queue, const QString& itemId, QWidget* parent = nullptr);

    const QString& itemId() const { return m_id; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void updateStatus(const QString& status);
    void onCompleted();

    QueueManager* m_queue;
    QString m_id;
    QTimer m_timer;

    QLabel* m_urlLabel;
    QLabel* m_statusLabel;
    QLabel* m_filesizeLabel;
    QLabel* m_downloadedLabel;
    QLabel* m_rateLabel;
    QLabel* m_timeLeftLabel;
    QLabel* m_resumeLabel;
    QProgressBar* m_mainProgress;
    SegmentBar* m_segmentBar;
    QTextEdit* m_connText;
    QPushButton* m_pauseBtn;
    QPushButton* m_cancelBtn;
    QCheckBox* m_limitCheck;
    QSpinBox* m_limitSpin;
    QCheckBox* m_openFileCheck;
    QCheckBox* m_openFolderCheck;
    QCheckBox* m_closeWhenDoneCheck;
    bool m_completed = false;
};
