// Flux Downloader - Settings Manager
// Persists user configuration to ~/.flux_downloader/settings.json
// (same file and keys as the original Python app, so existing settings carry over).
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject* parent = nullptr);

    static QString appDir();
    static QString settingsPath();
    static QString historyPath();
    static QString queuePath();
    static QString torrentsPath();

    void load();
    void save() const;

    QJsonValue get(const QString& key) const;
    QString getString(const QString& key) const;
    int getInt(const QString& key) const;
    bool getBool(const QString& key) const;
    QStringList getStringList(const QString& key) const;

    void set(const QString& key, const QJsonValue& value);

    QStringList categoryNames() const;
    QString categoryForExt(const QString& ext) const;
    // Returns <download_dir>/<category dir>, creating it.
    QString dirForCategory(const QString& category) const;

signals:
    void changed(const QString& key);

private:
    QJsonObject m_data;
};

QJsonArray loadHistory();
void appendHistory(const QJsonObject& entry);
