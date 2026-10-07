// Main window: sidebar, download table, toolbar, status bar and system tray.
#pragma once

#include "core/LocalServer.h"
#include "core/TorrentEngine.h"

#include <QHash>
#include <QMainWindow>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QSystemTrayIcon>

class ClipboardMonitor;
class QLabel;
class QListWidget;
class QNetworkAccessManager;
class QTableWidget;
class QueueManager;
class ProgressWindow;
class SettingsManager;
struct DownloadItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    SettingsManager* settings() const { return m_settings; }
    void applyTheme();
    // Handles a URL from the command line / a second app instance / the browser.
    void handleIncomingUrl(const QString& url, const QString& filename = {}, const QString& referrer = {});
    void showAndRaise();

public slots:
    void openAddDialog();
    void openYouTubeDialog();
    void openTorrentDialog();
    void openSettingsDialog();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void connectQueueSignals();
    void startServices();
    void restartLocalServer();
    void shutdown();

    // add flows
    void quickAdd(const QString& url, const QString& filename = {}, const QString& referrer = {},
                  bool interactive = true);
    void quickAddYouTube(const QString& url);
    void quickAddMagnet(const QString& uri);
    void quickAddTorrentFile(const QString& path);
    void downloadThenAddTorrent(const QString& url);
    void startYouTubeDownload(const QString& url, const QString& saveDir, bool audioOnly, const QString& quality,
                              bool subtitles, bool isLivestream, const QString& title);
    void showProgressWindow(const QString& id);

    // table sync
    int rowFor(const QString& id) const;
    QString idForRow(int row) const;
    int addRow(const QString& id, const QString& name, const QString& category);
    void refreshRow(const QString& id);
    void setRowProgress(int row, qint64 downloaded, qint64 total, double speed);
    void removeRowFor(const QString& id);
    void onItemAdded(const QString& id);
    void onItemProgress(const QString& id, qint64 downloaded, qint64 total, double speed);
    void onItemStatus(const QString& id, const QString& status);
    void onItemError(const QString& id, const QString& message);
    void onItemCompleted(const QString& id);
    void onQueueEmptied();
    void onLivestreamPrompt(const QString& id, const QString& message);
    void onTorrentAdded(const QString& tid, const QString& name);
    void onTorrentProgress(const QString& tid, const TorrentStatus& st);
    void onTorrentFinished(const QString& tid);
    void updateStatusSummary();
    void notify(const QString& title, const QString& message, int ms = 4000);

    // filtering & menus
    void applyFilter();
    bool rowVisible(const QString& id) const;
    void showContextMenu(const QPoint& pos);
    void showTorrentContextMenu(const QPoint& pos, const QStringList& tids);
    QStringList selectedIds() const;
    void removeIds(const QStringList& ids, bool deleteFiles);
    void onRowActivated(int row);

    SettingsManager* m_settings;
    QueueManager* m_queue;
    TorrentEngine* m_torrents;
    LocalServer* m_server;
    ClipboardMonitor* m_clipboard = nullptr;
    QNetworkAccessManager* m_net;

    QListWidget* m_sidebar;
    QTableWidget* m_table;
    QLabel* m_statusLabel;
    QSystemTrayIcon* m_tray;

    QHash<QString, QPersistentModelIndex> m_rows;
    QSet<QString> m_torrentIds;
    QHash<QString, QPointer<ProgressWindow>> m_progressWindows;
    bool m_clipboardPromptOpen = false;
    bool m_quitting = false;
    bool m_shutDown = false;
};
