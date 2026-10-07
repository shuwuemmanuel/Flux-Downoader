#include "core/ClipboardMonitor.h"

#include "core/SettingsManager.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardMonitor::ClipboardMonitor(SettingsManager* settings, int intervalMs, QObject* parent)
    : QObject(parent), m_settings(settings)
{
    // Don't offer whatever was already on the clipboard when the app started.
    m_lastText = QGuiApplication::clipboard()->text().trimmed();
    connect(&m_timer, &QTimer::timeout, this, &ClipboardMonitor::checkClipboard);
    m_timer.start(intervalMs);
}

void ClipboardMonitor::checkClipboard()
{
    if (!m_settings->getBool("clipboard_monitor_enabled"))
        return;
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty() || text == m_lastText)
        return;
    m_lastText = text;
    if (!(text.startsWith("http://") || text.startsWith("https://")))
        return;
    const QString lowered = text.toLower().section('?', 0, 0).section('#', 0, 0);
    for (const QString& ext : m_settings->getStringList("clipboard_extensions")) {
        if (!ext.isEmpty() && lowered.endsWith(ext.toLower())) {
            emit linkDetected(text);
            return;
        }
    }
}
