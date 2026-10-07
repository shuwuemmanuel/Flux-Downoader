// Handles single videos, full playlists, and entire channels.
// Resolves the URL with yt-dlp (flat extraction) and lets the user pick which
// entries to queue, plus format / audio-only / subtitle options.
// Supports livestream detection and from-start downloading.
#pragma once

#include "core/YouTubeEngine.h"

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class SettingsManager;

class YouTubeDialog : public QDialog {
    Q_OBJECT
public:
    struct Options {
        bool audioOnly = false;
        QString quality = QStringLiteral("best");
        bool subtitles = false;
        bool isLivestream = false;
    };

    explicit YouTubeDialog(SettingsManager* settings, QWidget* parent = nullptr, const QString& prefillUrl = {});

    QVector<YouTubeEntry> selectedEntries() const;
    Options options() const;
    QString kind() const { return m_kind; }

private:
    void fetch();
    void onResolved(const QVector<YouTubeEntry>& entries, const QString& kind, bool isLivestream);
    void onFailed(const QString& message);
    void setAllChecked(bool checked);

    SettingsManager* m_settings;
    QLineEdit* m_urlEdit;
    QPushButton* m_fetchBtn;
    QLabel* m_statusLabel;
    QListWidget* m_list;
    QComboBox* m_modeCombo;
    QCheckBox* m_subsCheck;
    QPushButton* m_addBtn;
    QPointer<YouTubeProbe> m_probe;
    QVector<YouTubeEntry> m_entries;
    QString m_kind = QStringLiteral("youtube_video");
    bool m_isLivestream = false;
};
