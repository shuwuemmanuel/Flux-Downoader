// Flux Downloader - Clipboard Monitor
// Watches the system clipboard for URLs that look like direct downloads
// (matching known file extensions) and offers to grab them - just like
// IDM's "Download from clipboard" popup.
#pragma once

#include <QObject>
#include <QTimer>

class SettingsManager;

class ClipboardMonitor : public QObject {
    Q_OBJECT
public:
    explicit ClipboardMonitor(SettingsManager* settings, int intervalMs = 1000, QObject* parent = nullptr);

    // Text set by the app itself (e.g. "Copy URL") shouldn't trigger a prompt.
    void ignoreText(const QString& text) { m_lastText = text.trimmed(); }

signals:
    void linkDetected(const QString& url);

private:
    void checkClipboard();

    SettingsManager* m_settings;
    QString m_lastText;
    QTimer m_timer;
};
