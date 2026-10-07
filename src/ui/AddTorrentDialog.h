// Add a torrent by magnet link or .torrent file, with an FDM/qBittorrent-style
// per-file priority picker once metadata is available.
#pragma once

#include "core/TorrentEngine.h"

#include <QDialog>
#include <QPair>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;
class SettingsManager;

class AddTorrentDialog : public QDialog {
    Q_OBJECT
public:
    struct Result {
        bool fileMode = false;
        QString magnet;
        QString torrentFile;
        QString saveDir;
        QString priority;
    };

    AddTorrentDialog(SettingsManager* settings, TorrentEngine* engine, QWidget* parent = nullptr,
                     const QString& prefillMagnet = {});

    // Id of the torrent already added during "Fetch File List", if any.
    QString pendingTorrentId() const { return m_pendingTid; }
    void applyFilePriorities(const QString& tid);
    Result resultData() const;

    void reject() override;

private:
    void fetchMetadata();
    void onMetadataReady(const QString& tid, const QVector<TorrentFileInfo>& files);

    SettingsManager* m_settings;
    TorrentEngine* m_engine;
    QTabWidget* m_tabs;
    QLineEdit* m_magnetEdit;
    QLineEdit* m_fileEdit;
    QLineEdit* m_dirEdit;
    QComboBox* m_priorityCombo;
    QPushButton* m_fetchBtn;
    QTableWidget* m_fileTable;
    QLabel* m_statusLabel;
    QVector<QPair<TorrentFileInfo, QComboBox*>> m_fileRows;
    QString m_pendingTid;
};
