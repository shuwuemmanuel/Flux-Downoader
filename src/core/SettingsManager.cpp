#include "core/SettingsManager.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace {

QJsonObject category(const QStringList& exts, const QString& dir)
{
    return QJsonObject{{"exts", QJsonArray::fromStringList(exts)}, {"dir", dir}};
}

const QStringList& categoryOrder()
{
    // QJsonObject sorts keys alphabetically; keep the original display order.
    static const QStringList kOrder = {"Video", "Music", "Programs", "Compressed", "Documents", "Other"};
    return kOrder;
}

QJsonObject defaultCategories()
{
    return QJsonObject{
        {"Video", category({".mp4", ".mkv", ".avi", ".mov", ".flv", ".webm", ".wmv"}, "Video")},
        {"Music", category({".mp3", ".wav", ".flac", ".aac", ".ogg", ".m4a"}, "Music")},
        {"Programs", category({".exe", ".msi", ".dmg", ".pkg", ".apk", ".deb", ".rpm", ".appimage"}, "Programs")},
        {"Compressed", category({".zip", ".rar", ".7z", ".tar", ".gz", ".xz"}, "Compressed")},
        {"Documents", category({".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".txt", ".csv"}, "Documents")},
        {"Other", category({}, "Other")},
    };
}

QJsonObject defaults()
{
    return QJsonObject{
        {"download_dir", QDir(QDir::homePath()).filePath("Downloads/FluxDownloader")},
        {"max_concurrent_downloads", 3},
        {"max_connections_per_file", 8},
        {"global_speed_limit_kbps", 0},  // 0 = unlimited
        {"auto_categorize", true},
        {"categories", defaultCategories()},
        {"clipboard_monitor_enabled", true},
        {"clipboard_extensions",
         QJsonArray{".zip", ".rar", ".7z", ".exe", ".msi", ".dmg", ".mp4", ".mkv", ".mp3", ".pdf", ".iso", ".apk",
                    ".tar.gz"}},
        {"theme", "aqua_light"},
        {"start_minimized", false},
        {"minimize_to_tray", true},
        {"browser_integration_port", 38019},
        {"browser_integration_enabled", true},
        {"auto_retry_count", 3},
        {"auto_retry_delay_sec", 5},
        {"notify_on_complete", true},
        {"shutdown_after_queue", false},
        {"youtube_default_format", "best"},
        {"youtube_download_subtitles", false},
        {"proxy", ""},
        {"schedule_enabled", false},
        {"schedule_start", ""},  // "HH:MM"
        {"schedule_stop", ""},
        {"window_geometry", QJsonValue::Null},
        {"torrent_port", 6881},
        {"torrent_dht_enabled", true},
        {"torrent_download_limit_kbps", 0},
        {"torrent_upload_limit_kbps", 0},
        {"torrent_default_priority", "Normal"},
        {"show_progress_window", true},           // individual IDM-style progress window per download
        {"show_download_start_dialog", false},    // ask for save location before starting
        {"show_download_complete_dialog", true},  // completion dialog
    };
}

bool writeJson(const QString& path, const QJsonDocument& doc)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    return f.commit();
}

}  // namespace

SettingsManager::SettingsManager(QObject* parent) : QObject(parent)
{
    QDir().mkpath(appDir());
    m_data = defaults();
    load();
}

QString SettingsManager::appDir()
{
    return QDir(QDir::homePath()).filePath(".flux_downloader");
}
QString SettingsManager::settingsPath() { return QDir(appDir()).filePath("settings.json"); }
QString SettingsManager::historyPath() { return QDir(appDir()).filePath("history.json"); }
QString SettingsManager::queuePath() { return QDir(appDir()).filePath("queue.json"); }
QString SettingsManager::torrentsPath() { return QDir(appDir()).filePath("torrents.json"); }

void SettingsManager::load()
{
    QFile f(settingsPath());
    if (!f.exists()) {
        save();
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        m_data = defaults();
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_data = defaults();
        return;
    }
    // merge so new default keys added in updates are preserved
    QJsonObject merged = defaults();
    const QJsonObject loaded = doc.object();
    for (auto it = loaded.begin(); it != loaded.end(); ++it)
        merged[it.key()] = it.value();
    m_data = merged;
}

void SettingsManager::save() const
{
    QDir().mkpath(appDir());
    writeJson(settingsPath(), QJsonDocument(m_data));
}

QJsonValue SettingsManager::get(const QString& key) const
{
    return m_data.value(key);
}

QString SettingsManager::getString(const QString& key) const
{
    return m_data.value(key).toString();
}

int SettingsManager::getInt(const QString& key) const
{
    return m_data.value(key).toInt();
}

bool SettingsManager::getBool(const QString& key) const
{
    return m_data.value(key).toBool();
}

QStringList SettingsManager::getStringList(const QString& key) const
{
    QStringList out;
    for (const QJsonValue& v : m_data.value(key).toArray())
        out << v.toString();
    return out;
}

void SettingsManager::set(const QString& key, const QJsonValue& value)
{
    if (m_data.value(key) == value)
        return;
    m_data[key] = value;
    save();
    emit changed(key);
}

QStringList SettingsManager::categoryNames() const
{
    const QJsonObject cats = m_data.value("categories").toObject();
    QStringList names;
    for (const QString& n : categoryOrder())
        if (cats.contains(n))
            names << n;
    for (const QString& n : cats.keys())
        if (!names.contains(n))
            names << n;
    return names;
}

QString SettingsManager::categoryForExt(const QString& ext) const
{
    const QString lowered = ext.toLower();
    const QJsonObject cats = m_data.value("categories").toObject();
    for (const QString& name : categoryNames()) {
        for (const QJsonValue& e : cats.value(name).toObject().value("exts").toArray())
            if (e.toString() == lowered)
                return name;
    }
    return "Other";
}

QString SettingsManager::dirForCategory(const QString& category) const
{
    const QDir base(getString("download_dir"));
    const QString sub =
        m_data.value("categories").toObject().value(category).toObject().value("dir").toString("Other");
    const QString path = base.filePath(sub);
    QDir().mkpath(path);
    return path;
}

QJsonArray loadHistory()
{
    QFile f(SettingsManager::historyPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isArray() ? doc.array() : QJsonArray{};
}

void appendHistory(const QJsonObject& entry)
{
    QJsonArray hist = loadHistory();
    hist.prepend(entry);
    while (hist.size() > 500)
        hist.removeLast();
    writeJson(SettingsManager::historyPath(), QJsonDocument(hist));
}
