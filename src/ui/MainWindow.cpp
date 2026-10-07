#include "ui/MainWindow.h"

#include "core/ClipboardMonitor.h"
#include "core/DownloadEngine.h"
#include "core/DownloadItem.h"
#include "core/QueueManager.h"
#include "core/SettingsManager.h"
#include "core/YouTubeEngine.h"
#include "ui/AddDownloadDialog.h"
#include "ui/AddTorrentDialog.h"
#include "ui/DownloadDialogs.h"
#include "ui/Format.h"
#include "ui/ProgressWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/YouTubeDialog.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressBar>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <stdexcept>

namespace {

const QStringList kCategories = {"All Downloads", "Video",     "Music",   "Programs", "Compressed", "Documents",
                                 "Other",         "YouTube",   "Torrents", "Completed", "Active",    "Queue"};

const QStringList kColumns = {"File Name", "Size", "Downloaded", "Progress", "Speed", "Time Left", "Status", "Category"};

enum Col { ColName = 0, ColSize, ColDownloaded, ColProgress, ColSpeed, ColTimeLeft, ColStatus, ColCategory };

bool isYouTubeUrl(const QString& url)
{
    const QString host = QUrl(url).host().toLower();
    return host == "youtu.be" || host.endsWith(".youtu.be") || host == "youtube.com" || host.endsWith(".youtube.com");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_settings(new SettingsManager(this)),
      m_queue(new QueueManager(m_settings, this)),
      m_torrents(new TorrentEngine(m_settings, this)),
      m_server(new LocalServer(this)),
      m_net(new QNetworkAccessManager(this))
{
    setWindowTitle("Flux Downloader");
    resize(1080, 640);
    const QByteArray geometry = QByteArray::fromBase64(m_settings->getString("window_geometry").toLatin1());
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    buildUi();
    connectQueueSignals();
    startServices();

    // Bring back what was in the list last time.
    m_queue->loadSaved();
    m_torrents->restoreSession();

    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::shutdown);
}

MainWindow::~MainWindow()
{
    shutdown();
}

// ---- UI construction ----------------------------------------------------------------

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    // Sidebar
    m_sidebar = new QListWidget;
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFixedWidth(190);
    m_sidebar->addItems(kCategories);
    m_sidebar->setCurrentRow(0);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, [this] { applyFilter(); });
    root->addWidget(m_sidebar);

    // Table
    auto* right = new QVBoxLayout;
    right->setContentsMargins(8, 8, 8, 8);
    m_table = new QTableWidget(0, kColumns.size());
    m_table->setHorizontalHeaderLabels(kColumns);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setWordWrap(false);
    connect(m_table, &QWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { onRowActivated(row); });
    auto* deleteAction = new QAction(this);
    deleteAction->setShortcut(QKeySequence::Delete);
    deleteAction->setShortcutContext(Qt::WidgetShortcut);
    connect(deleteAction, &QAction::triggered, this, [this] { removeIds(selectedIds(), false); });
    m_table->addAction(deleteAction);
    right->addWidget(m_table);
    root->addLayout(right);

    // Toolbar
    auto* toolbar = new QToolBar("Main");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    addToolBar(toolbar);
    toolbar->addAction("Add URL", this, &MainWindow::openAddDialog);
    toolbar->addAction("YouTube / Playlist", this, &MainWindow::openYouTubeDialog);
    toolbar->addAction("Add Torrent", this, &MainWindow::openTorrentDialog);
    toolbar->addSeparator();
    toolbar->addAction("Resume All", this, [this] {
        m_queue->resumeAll();
        for (const QString& tid : m_torrents->ids())
            m_torrents->resume(tid);
    });
    toolbar->addAction("Pause All", this, [this] {
        m_queue->pauseAll();
        for (const QString& tid : m_torrents->ids())
            m_torrents->pause(tid);
    });
    toolbar->addSeparator();
    toolbar->addAction("Settings", this, &MainWindow::openSettingsDialog);

    // Status bar
    statusBar()->setSizeGripEnabled(true);
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);

    // System tray
    const QIcon icon(":/app_icon.png");
    setWindowIcon(icon);
    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setToolTip("Flux Downloader");
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction("Show Flux Downloader", this, &MainWindow::showAndRaise);
    trayMenu->addAction("Add URL", this, [this] {
        showAndRaise();
        openAddDialog();
    });
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    m_tray->setContextMenu(trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            showAndRaise();
    });
    m_tray->show();
}

void MainWindow::applyTheme()
{
    QFile f(m_settings->getString("theme") == QLatin1String("aqua_dark") ? ":/style_dark.qss" : ":/style.qss");
    if (f.open(QIODevice::ReadOnly))
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
}

void MainWindow::showAndRaise()
{
    showNormal();
    raise();
    activateWindow();
}

// ---- services --------------------------------------------------------------------------

void MainWindow::startServices()
{
    // Browser integration server
    connect(m_server, &LocalServer::linkReceived, this,
            [this](const IncomingLink& link) { handleIncomingUrl(link.url, link.filename, link.referrer); });
    restartLocalServer();

    // Clipboard monitor
    m_clipboard = new ClipboardMonitor(m_settings, 1000, this);
    connect(m_clipboard, &ClipboardMonitor::linkDetected, this, [this](const QString& url) {
        if (m_clipboardPromptOpen)
            return;
        m_clipboardPromptOpen = true;
        const auto reply = QMessageBox::question(
            this, "Flux Downloader — Clipboard link detected",
            QStringLiteral("A downloadable link was found on your clipboard:\n\n%1\n\nDownload it now?").arg(url),
            QMessageBox::Yes | QMessageBox::No);
        m_clipboardPromptOpen = false;
        if (reply == QMessageBox::Yes)
            quickAdd(url);
    });
}

void MainWindow::restartLocalServer()
{
    m_server->stop();
    if (!m_settings->getBool("browser_integration_enabled"))
        return;
    const int port = m_settings->getInt("browser_integration_port");
    if (!m_server->start(static_cast<quint16>(port)))
        m_statusLabel->setText(
            QStringLiteral("Browser integration unavailable: port %1 is in use (%2)").arg(port).arg(m_server->errorString()));
}

void MainWindow::shutdown()
{
    if (m_shutDown)
        return;
    m_shutDown = true;
    m_settings->set("window_geometry", QString::fromLatin1(saveGeometry().toBase64()));
    m_server->stop();
    m_queue->shutdown();
    m_torrents->shutdown();
    m_tray->hide();
}

void MainWindow::handleIncomingUrl(const QString& rawUrl, const QString& filename, const QString& referrer)
{
    const QString url = rawUrl.trimmed();
    if (url.isEmpty())
        return;
    if (url.startsWith("magnet:", Qt::CaseInsensitive))
        quickAddMagnet(url);
    else if (QFileInfo(url).isFile() && url.endsWith(".torrent", Qt::CaseInsensitive))
        quickAddTorrentFile(url);  // e.g. a .torrent file opened with the app
    else if (url.toLower().section('?', 0, 0).endsWith(".torrent"))
        downloadThenAddTorrent(url);
    else if (isYouTubeUrl(url))
        quickAddYouTube(url);
    else
        quickAdd(url, QFileInfo(filename).fileName(), referrer);
}

void MainWindow::downloadThenAddTorrent(const QString& url)
{
    // A .torrent file URL was caught by the browser — fetch the small
    // .torrent file itself first, then hand it to the torrent engine.
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_statusLabel->setText(QStringLiteral("Could not fetch .torrent file: %1").arg(reply->errorString()));
            return;
        }
        const QString tmpDir = QDir(m_settings->getString("download_dir")).filePath(".torrent_cache");
        QDir().mkpath(tmpDir);
        QString fname = guessFilename(url);
        if (fname == QLatin1String("download"))
            fname = QStringLiteral("download.torrent");
        const QString path = QDir(tmpDir).filePath(fname);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(reply->readAll()) < 0) {
            m_statusLabel->setText(QStringLiteral("Could not save .torrent file: %1").arg(f.errorString()));
            return;
        }
        f.close();
        quickAddTorrentFile(path);
    });
}

// ---- queue signal wiring --------------------------------------------------------------

void MainWindow::connectQueueSignals()
{
    connect(m_queue, &QueueManager::itemAdded, this, &MainWindow::onItemAdded);
    connect(m_queue, &QueueManager::itemChanged, this, &MainWindow::refreshRow);
    connect(m_queue, &QueueManager::itemProgress, this, &MainWindow::onItemProgress);
    connect(m_queue, &QueueManager::itemStatus, this, &MainWindow::onItemStatus);
    connect(m_queue, &QueueManager::itemError, this, &MainWindow::onItemError);
    connect(m_queue, &QueueManager::itemCompleted, this, &MainWindow::onItemCompleted);
    connect(m_queue, &QueueManager::queueEmptied, this, &MainWindow::onQueueEmptied);
    connect(m_queue, &QueueManager::livestreamPrompt, this, &MainWindow::onLivestreamPrompt);

    connect(m_torrents, &TorrentEngine::torrentAdded, this, &MainWindow::onTorrentAdded);
    connect(m_torrents, &TorrentEngine::torrentRemoved, this, [this](const QString& tid) {
        m_torrentIds.remove(tid);
        removeRowFor(tid);
    });
    connect(m_torrents, &TorrentEngine::progressUpdated, this, &MainWindow::onTorrentProgress);
    connect(m_torrents, &TorrentEngine::statusChanged, this, [this](const QString& tid, const QString& status) {
        const int row = rowFor(tid);
        if (row >= 0)
            m_table->item(row, ColStatus)->setText(status);
    });
    connect(m_torrents, &TorrentEngine::torrentError, this, [this](const QString& tid, const QString& msg) {
        const int row = rowFor(tid);
        if (row >= 0) {
            m_table->item(row, ColStatus)->setText("Error");
            m_table->item(row, ColStatus)->setToolTip(msg);
        }
        m_statusLabel->setText(QStringLiteral("Error: %1").arg(msg));
    });
    connect(m_torrents, &TorrentEngine::torrentFinished, this, &MainWindow::onTorrentFinished);
}

// ---- add flows --------------------------------------------------------------------------

void MainWindow::openAddDialog()
{
    AddDownloadDialog dlg(m_settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (dlg.isBatchMode()) {
        for (const QString& url : dlg.batchUrls())
            quickAdd(url, {}, {}, false);
        return;
    }
    const AddDownloadDialog::Result data = dlg.resultData();
    if (data.url.isEmpty())
        return;
    if (isYouTubeUrl(data.url) && !QUrl(data.url).path().contains('.')) {
        // A YouTube page isn't a direct file; hand it to the YouTube downloader.
        YouTubeDialog ydlg(m_settings, this, data.url);
        if (ydlg.exec() == QDialog::Accepted) {
            const auto opts = ydlg.options();
            const QString saveDir = QDir(m_settings->getString("download_dir")).filePath("YouTube");
            for (const YouTubeEntry& e : ydlg.selectedEntries())
                startYouTubeDownload(e.url, saveDir, opts.audioOnly, opts.quality, opts.subtitles,
                                     opts.isLivestream || e.isLive, e.title);
        }
        return;
    }
    DownloadItem item;
    item.url = data.url;
    item.saveDir = data.saveDir;
    QDir().mkpath(item.saveDir);
    item.filename = data.filenameEdited ? data.filename : uniqueFilename(item.saveDir, data.filename);
    item.autoName = !data.filenameEdited;
    item.category = data.category;
    item.checksumExpected = data.checksum;
    item.priority = data.priority;
    const QString id = m_queue->add(item, data.startNow);
    if (data.startNow)
        showProgressWindow(id);
}

void MainWindow::quickAdd(const QString& url, const QString& filenameIn, const QString& referrer, bool interactive)
{
    QString filename = sanitizeFilename(filenameIn);
    const bool guessed = filename.isEmpty();
    if (guessed)
        filename = guessFilename(url);
    const QString ext = QStringLiteral(".") + QFileInfo(filename).suffix();
    const QString category =
        m_settings->getBool("auto_categorize") ? m_settings->categoryForExt(ext) : QStringLiteral("Other");
    QString saveDir = m_settings->dirForCategory(category);

    if (interactive && m_settings->getBool("show_download_start_dialog")) {
        DownloadStartDialog dlg(filename, url, saveDir, this);
        showAndRaise();
        if (dlg.exec() != QDialog::Accepted)
            return;
        if (dlg.shouldRemember())
            m_settings->set("show_download_start_dialog", false);
        const QFileInfo chosen(dlg.savePath());
        saveDir = chosen.absolutePath();
        if (chosen.fileName() != filename) {
            filename = chosen.fileName();
            // explicit name from the user
            DownloadItem item;
            item.url = url;
            item.filename = filename;
            item.saveDir = saveDir;
            item.category = category;
            item.referrer = referrer;
            const QString id = m_queue->add(item, true);
            showProgressWindow(id);
            return;
        }
    }

    DownloadItem item;
    item.url = url;
    item.saveDir = saveDir;
    item.filename = uniqueFilename(saveDir, filename);
    item.autoName = guessed;
    item.category = category;
    item.referrer = referrer;
    const QString id = m_queue->add(item, true);
    if (interactive)
        showProgressWindow(id);
}

void MainWindow::openYouTubeDialog()
{
    YouTubeDialog dlg(m_settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const auto opts = dlg.options();
    const QString saveDir = QDir(m_settings->getString("download_dir")).filePath("YouTube");
    QDir().mkpath(saveDir);
    for (const YouTubeEntry& e : dlg.selectedEntries())
        startYouTubeDownload(e.url, saveDir, opts.audioOnly, opts.quality, opts.subtitles,
                             opts.isLivestream || e.isLive, e.title);
}

void MainWindow::quickAddYouTube(const QString& url)
{
    const QString saveDir = QDir(m_settings->getString("download_dir")).filePath("YouTube");
    QDir().mkpath(saveDir);
    QString quality = m_settings->getString("youtube_default_format");
    const bool audio = quality == QLatin1String("audio");
    if (quality.isEmpty() || audio)
        quality = "best";
    startYouTubeDownload(url, saveDir, audio, quality, m_settings->getBool("youtube_download_subtitles"), false, {});
}

void MainWindow::startYouTubeDownload(const QString& url, const QString& saveDir, bool audioOnly,
                                      const QString& quality, bool subtitles, bool isLivestream, const QString& title)
{
    if (YouTube::ytDlpPath().isEmpty()) {
        QMessageBox::warning(this, "YouTube Downloader", YouTube::missingMessage());
        return;
    }
    DownloadItem item;
    item.url = url;
    item.filename = title.isEmpty() ? url : title;
    item.saveDir = saveDir;
    item.category = "YouTube";
    item.kind = "youtube_video";
    item.ytAudioOnly = audioOnly;
    item.ytQuality = quality;
    item.ytSubtitles = subtitles;
    item.ytLivestream = isLivestream;
    m_queue->add(item, true);
}

void MainWindow::openTorrentDialog()
{
    if (!TorrentEngine::available()) {
        QMessageBox::warning(this, "Torrent support unavailable",
                             "This build of Flux Downloader was compiled without libtorrent.\n"
                             "Rebuild with libtorrent-rasterbar installed to enable torrents.");
        return;
    }
    AddTorrentDialog dlg(m_settings, m_torrents, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const AddTorrentDialog::Result data = dlg.resultData();
    QDir().mkpath(data.saveDir);
    try {
        QString tid = dlg.pendingTorrentId();
        if (tid.isEmpty()) {
            // user never clicked "Fetch File List" — add directly now
            if (data.fileMode && !data.torrentFile.isEmpty())
                tid = m_torrents->addTorrentFile(data.torrentFile, data.saveDir);
            else if (!data.magnet.isEmpty())
                tid = m_torrents->addMagnet(data.magnet, data.saveDir);
            else
                return;
        }
        dlg.applyFilePriorities(tid);
        m_torrents->setQueuePriority(tid, data.priority);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Torrent error", QString::fromUtf8(e.what()));
    }
}

void MainWindow::quickAddTorrentFile(const QString& path)
{
    if (!TorrentEngine::available())
        return;
    const QString saveDir = QDir(m_settings->getString("download_dir")).filePath("Torrents");
    QDir().mkpath(saveDir);
    try {
        m_torrents->addTorrentFile(path, saveDir);
    } catch (const std::exception& e) {
        m_statusLabel->setText(QStringLiteral("Torrent error: %1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::quickAddMagnet(const QString& uri)
{
    if (!TorrentEngine::available())
        return;
    const QString saveDir = QDir(m_settings->getString("download_dir")).filePath("Torrents");
    QDir().mkpath(saveDir);
    try {
        m_torrents->addMagnet(uri, saveDir);
    } catch (const std::exception& e) {
        m_statusLabel->setText(QStringLiteral("Torrent error: %1").arg(QString::fromUtf8(e.what())));
    }
}

void MainWindow::showProgressWindow(const QString& id)
{
    const DownloadItem* item = m_queue->item(id);
    if (!item || item->kind != QLatin1String("generic"))
        return;
    if (!m_settings->getBool("show_progress_window"))
        return;
    QPointer<ProgressWindow>& w = m_progressWindows[id];
    if (!w)
        w = new ProgressWindow(m_queue, id, this);
    w->show();
    w->raise();
}

// ---- table sync -------------------------------------------------------------------

int MainWindow::rowFor(const QString& id) const
{
    const QPersistentModelIndex idx = m_rows.value(id);
    return idx.isValid() ? idx.row() : -1;
}

QString MainWindow::idForRow(int row) const
{
    const QTableWidgetItem* it = m_table->item(row, ColName);
    return it ? it->data(Qt::UserRole).toString() : QString();
}

int MainWindow::addRow(const QString& id, const QString& name, const QString& category)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    auto* nameItem = new QTableWidgetItem(name);
    nameItem->setData(Qt::UserRole, id);
    nameItem->setToolTip(name);
    m_table->setItem(row, ColName, nameItem);
    m_table->setItem(row, ColSize, new QTableWidgetItem("—"));
    m_table->setItem(row, ColDownloaded, new QTableWidgetItem("0 B"));
    auto* progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(0);
    m_table->setCellWidget(row, ColProgress, progress);
    m_table->setItem(row, ColSpeed, new QTableWidgetItem(""));
    m_table->setItem(row, ColTimeLeft, new QTableWidgetItem("—"));
    m_table->setItem(row, ColStatus, new QTableWidgetItem(""));
    m_table->setItem(row, ColCategory, new QTableWidgetItem(category));
    m_rows.insert(id, QPersistentModelIndex(m_table->model()->index(row, ColName)));
    return row;
}

void MainWindow::removeRowFor(const QString& id)
{
    const int row = rowFor(id);
    if (row >= 0)
        m_table->removeRow(row);
    m_rows.remove(id);
    if (auto w = m_progressWindows.take(id))
        w->deleteLater();
    updateStatusSummary();
}

void MainWindow::setRowProgress(int row, qint64 downloaded, qint64 total, double speed)
{
    m_table->item(row, ColSize)->setText(Fmt::size(static_cast<double>(total)));
    m_table->item(row, ColDownloaded)->setText(downloaded > 0 ? Fmt::size(static_cast<double>(downloaded))
                                                              : QStringLiteral("0 B"));
    if (auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, ColProgress))) {
        if (total > 0) {
            const int percent = static_cast<int>(qMin<qint64>(100, downloaded * 100 / total));
            bar->setValue(percent);
            bar->setFormat(QStringLiteral("%1%").arg(percent));
        }
    }
    m_table->item(row, ColSpeed)->setText(Fmt::speed(speed));
    m_table->item(row, ColTimeLeft)
        ->setText(speed > 0 && total > 0 ? Fmt::time((total - downloaded) / speed) : QStringLiteral("—"));
}

void MainWindow::refreshRow(const QString& id)
{
    const DownloadItem* item = m_queue->item(id);
    const int row = rowFor(id);
    if (!item || row < 0)
        return;
    m_table->item(row, ColName)->setText(item->filename);
    m_table->item(row, ColName)->setToolTip(QDir::toNativeSeparators(item->finalPath()));
    m_table->item(row, ColCategory)->setText(item->category);
    m_table->item(row, ColStatus)->setText(item->status);
    m_table->item(row, ColStatus)->setToolTip(item->errorMessage);
    setRowProgress(row, item->downloaded, item->totalSize, m_queue->isRunning(id) ? item->speedBps : 0);
    if (item->status == Status::Completed) {
        if (auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, ColProgress))) {
            bar->setValue(100);
            bar->setFormat("100%");
        }
    }
}

void MainWindow::onItemAdded(const QString& id)
{
    const DownloadItem* item = m_queue->item(id);
    if (!item)
        return;
    const int row = addRow(id, item->filename, item->category);
    refreshRow(id);
    m_table->setRowHidden(row, !rowVisible(id));
    updateStatusSummary();
}

void MainWindow::onItemProgress(const QString& id, qint64 downloaded, qint64 total, double speed)
{
    const int row = rowFor(id);
    if (row < 0)
        return;
    setRowProgress(row, downloaded, total, speed);
}

void MainWindow::onItemStatus(const QString& id, const QString& status)
{
    const int row = rowFor(id);
    if (row < 0)
        return;
    m_table->item(row, ColStatus)->setText(status);
    if (status != Status::Downloading) {
        m_table->item(row, ColSpeed)->setText("");
        m_table->item(row, ColTimeLeft)->setText("—");
    }
    m_table->setRowHidden(row, !rowVisible(id));
    updateStatusSummary();
}

void MainWindow::onItemError(const QString& id, const QString& message)
{
    const int row = rowFor(id);
    if (row >= 0) {
        m_table->item(row, ColStatus)->setText("Error");
        m_table->item(row, ColStatus)->setToolTip(message);
    }
    m_statusLabel->setText(QStringLiteral("Error: %1").arg(message));
}

void MainWindow::onItemCompleted(const QString& id)
{
    refreshRow(id);
    const DownloadItem* item = m_queue->item(id);
    const QString name = item ? item->filename : QStringLiteral("Download");
    if (m_settings->getBool("notify_on_complete"))
        notify("Flux Downloader", QStringLiteral("%1 finished downloading.").arg(name));
    if (item && m_settings->getBool("show_download_complete_dialog")) {
        auto* dlg = new DownloadCompletedDialog(name, item->finalPath(), this);
        connect(dlg, &QDialog::finished, this, [this, dlg] {
            if (dlg->shouldHideFuture())
                m_settings->set("show_download_complete_dialog", false);
        });
        dlg->show();
    }
    updateStatusSummary();
}

void MainWindow::onQueueEmptied()
{
    m_statusLabel->setText("All downloads finished.");
    if (m_settings->getBool("shutdown_after_queue"))
        QMessageBox::information(this, "Flux Downloader",
                                 "Queue complete. (Auto shutdown/sleep is enabled in Settings — "
                                 "trigger your OS's shutdown/sleep command here if desired.)");
}

void MainWindow::onLivestreamPrompt(const QString& id, const QString& message)
{
    const auto reply =
        QMessageBox::question(this, "Livestream Download", message, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    m_queue->answerLivestream(id, reply == QMessageBox::Yes);
}

void MainWindow::onTorrentAdded(const QString& tid, const QString& name)
{
    if (m_rows.contains(tid))
        return;
    m_torrentIds.insert(tid);
    const int row = addRow(tid, name.isEmpty() ? QStringLiteral("Resolving torrent…") : name, "Torrent");
    m_table->item(row, ColStatus)->setText("Starting");
    m_table->setRowHidden(row, !rowVisible(tid));
    updateStatusSummary();
}

void MainWindow::onTorrentProgress(const QString& tid, const TorrentStatus& st)
{
    const int row = rowFor(tid);
    if (row < 0)
        return;
    if (!st.name.isEmpty())
        m_table->item(row, ColName)->setText(st.name);
    m_table->item(row, ColSize)->setText(Fmt::size(static_cast<double>(st.totalWanted)));
    m_table->item(row, ColDownloaded)->setText(st.totalWantedDone > 0 ? Fmt::size(static_cast<double>(st.totalWantedDone))
                                                                     : QStringLiteral("0 B"));
    if (auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, ColProgress))) {
        const int percent = static_cast<int>(st.progress);
        bar->setValue(percent);
        bar->setFormat(QStringLiteral("%1%").arg(percent));
    }
    const QString down = Fmt::speed(static_cast<double>(st.downloadRate));
    const QString up = Fmt::speed(static_cast<double>(st.uploadRate));
    m_table->item(row, ColSpeed)
        ->setText(QStringLiteral("↓%1 ↑%2 · %3 peers")
                      .arg(down.isEmpty() ? QStringLiteral("0 B/s") : down)
                      .arg(up.isEmpty() ? QStringLiteral("0 B/s") : up)
                      .arg(st.numPeers));
    if (st.downloadRate > 0 && st.totalWanted > 0)
        m_table->item(row, ColTimeLeft)
            ->setText(Fmt::time(static_cast<double>(st.totalWanted - st.totalWantedDone) / st.downloadRate));
    else
        m_table->item(row, ColTimeLeft)->setText("—");
    if (m_table->item(row, ColStatus)->text() != st.state) {
        m_table->item(row, ColStatus)->setText(st.state);
        m_table->setRowHidden(row, !rowVisible(tid));
    }
}

void MainWindow::onTorrentFinished(const QString& tid)
{
    if (m_settings->getBool("notify_on_complete")) {
        const int row = rowFor(tid);
        const QString name = row >= 0 ? m_table->item(row, ColName)->text() : QStringLiteral("Torrent");
        notify("Flux Downloader", QStringLiteral("%1 finished downloading.").arg(name));
    }
}

void MainWindow::updateStatusSummary()
{
    const int active = m_queue->activeCount();
    const int total = m_queue->order().size() + m_torrentIds.size();
    m_statusLabel->setText(QStringLiteral("%1 active / %2 total downloads").arg(active).arg(total));
}

void MainWindow::notify(const QString& title, const QString& message, int ms)
{
    if (m_tray->isVisible() && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, message, QSystemTrayIcon::Information, ms);
}

// ---- filtering ------------------------------------------------------------------------------

bool MainWindow::rowVisible(const QString& id) const
{
    const QString category = kCategories.value(m_sidebar->currentRow(), "All Downloads");
    if (category == QLatin1String("All Downloads"))
        return true;
    if (m_torrentIds.contains(id)) {
        const int row = rowFor(id);
        const QString state = row >= 0 ? m_table->item(row, ColStatus)->text() : QString();
        if (category == QLatin1String("Torrents"))
            return true;
        if (category == QLatin1String("Active"))
            return state == QLatin1String("Downloading") || state == QLatin1String("Fetching metadata");
        if (category == QLatin1String("Completed"))
            return state == QLatin1String("Seeding") || state == QLatin1String("Finished");
        return false;
    }
    const DownloadItem* item = m_queue->item(id);
    if (!item)
        return false;
    if (category == QLatin1String("Active"))
        return m_queue->isRunning(id);
    if (category == QLatin1String("Queue"))
        return item->status == Status::Queued;
    if (category == QLatin1String("Completed"))
        return item->status == Status::Completed;
    return item->category == category;
}

void MainWindow::applyFilter()
{
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
        if (it.value().isValid())
            m_table->setRowHidden(it.value().row(), !rowVisible(it.key()));
}

// ---- context menus ------------------------------------------------------------------------------

QStringList MainWindow::selectedIds() const
{
    QStringList ids;
    QList<int> rows;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows())
        rows << idx.row();
    std::sort(rows.begin(), rows.end());
    for (int r : rows)
        if (!m_table->isRowHidden(r))
            ids << idForRow(r);
    return ids;
}

void MainWindow::removeIds(const QStringList& ids, bool deleteFiles)
{
    if (ids.isEmpty())
        return;
    if (deleteFiles) {
        const auto reply = QMessageBox::question(
            this, "Flux Downloader",
            ids.size() == 1 ? QStringLiteral("Remove this download and delete its file from disk?")
                            : QStringLiteral("Remove %1 downloads and delete their files from disk?").arg(ids.size()));
        if (reply != QMessageBox::Yes)
            return;
    }
    for (const QString& id : ids) {
        if (m_torrentIds.contains(id)) {
            m_torrents->remove(id, deleteFiles);  // emits torrentRemoved -> row removal
            m_torrentIds.remove(id);
            removeRowFor(id);
        } else {
            m_queue->remove(id, deleteFiles);
            removeRowFor(id);
        }
    }
}

void MainWindow::showContextMenu(const QPoint& pos)
{
    const int row = m_table->rowAt(pos.y());
    if (row < 0)
        return;
    const QString clickedId = idForRow(row);
    if (clickedId.isEmpty())
        return;
    QStringList ids = selectedIds();
    if (!ids.contains(clickedId)) {
        m_table->selectRow(row);
        ids = {clickedId};
    }

    if (m_torrentIds.contains(clickedId)) {
        QStringList tids;
        for (const QString& id : ids)
            if (m_torrentIds.contains(id))
                tids << id;
        showTorrentContextMenu(pos, tids);
        return;
    }
    QStringList itemIds;
    for (const QString& id : ids)
        if (!m_torrentIds.contains(id))
            itemIds << id;

    const DownloadItem* item = m_queue->item(clickedId);
    QMenu menu(this);
    QAction* progressA = nullptr;
    if (item && item->kind == QLatin1String("generic"))
        progressA = menu.addAction("Show Progress Window");
    QAction* openFileA = nullptr;
    if (item && item->status == Status::Completed)
        openFileA = menu.addAction("Open File");
    if (progressA || openFileA)
        menu.addSeparator();
    QAction* pauseA = menu.addAction("Pause");
    QAction* resumeA = menu.addAction("Resume");
    QAction* cancelA = menu.addAction("Cancel");
    menu.addSeparator();
    QMenu* priorityMenu = menu.addMenu("Priority");
    QHash<QAction*, QString> priorityActions;
    for (const QString& level : Priority::levels()) {
        QAction* a = priorityMenu->addAction(level);
        a->setCheckable(true);
        a->setChecked(item && item->priority == level);
        priorityActions.insert(a, level);
    }
    menu.addSeparator();
    QAction* openFolderA = menu.addAction("Open Containing Folder");
    QAction* copyUrlA = menu.addAction("Copy URL");
    menu.addSeparator();
    QAction* removeA = menu.addAction("Remove from list");
    QAction* removeDeleteA = menu.addAction("Remove and delete file");

    QAction* action = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!action)
        return;
    if (action == progressA) {
        showProgressWindow(clickedId);
        if (!m_settings->getBool("show_progress_window")) {  // explicit request overrides the setting
            QPointer<ProgressWindow>& w = m_progressWindows[clickedId];
            if (!w)
                w = new ProgressWindow(m_queue, clickedId, this);
            w->show();
            w->raise();
        }
    } else if (action == openFileA && item) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->finalPath()));
    } else if (action == pauseA) {
        for (const QString& id : itemIds)
            m_queue->pause(id);
    } else if (action == resumeA) {
        for (const QString& id : itemIds)
            m_queue->resume(id);
    } else if (action == cancelA) {
        for (const QString& id : itemIds)
            m_queue->cancel(id);
    } else if (priorityActions.contains(action)) {
        for (const QString& id : itemIds)
            m_queue->setPriority(id, priorityActions.value(action));
    } else if (action == openFolderA && item) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->saveDir));
    } else if (action == copyUrlA) {
        QStringList urls;
        for (const QString& id : itemIds)
            if (const DownloadItem* it = m_queue->item(id))
                urls << it->url;
        const QString text = urls.join('\n');
        m_clipboard->ignoreText(text);
        QApplication::clipboard()->setText(text);
    } else if (action == removeA) {
        removeIds(itemIds, false);
    } else if (action == removeDeleteA) {
        removeIds(itemIds, true);
    }
}

void MainWindow::showTorrentContextMenu(const QPoint& pos, const QStringList& tids)
{
    QMenu menu(this);
    QAction* pauseA = menu.addAction("Pause");
    QAction* resumeA = menu.addAction("Resume");
    menu.addSeparator();
    QMenu* priorityMenu = menu.addMenu("Download Priority");
    QHash<QAction*, QString> priorityActions;
    for (const QString& level : torrentQueuePriorities())
        priorityActions.insert(priorityMenu->addAction(level), level);
    menu.addSeparator();
    QAction* openFolderA = menu.addAction("Open Containing Folder");
    menu.addSeparator();
    QAction* removeA = menu.addAction("Remove from list");
    QAction* removeDeleteA = menu.addAction("Remove and delete files");

    QAction* action = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!action)
        return;
    if (action == pauseA) {
        for (const QString& tid : tids)
            m_torrents->pause(tid);
    } else if (action == resumeA) {
        for (const QString& tid : tids)
            m_torrents->resume(tid);
    } else if (priorityActions.contains(action)) {
        for (const QString& tid : tids)
            m_torrents->setQueuePriority(tid, priorityActions.value(action));
    } else if (action == openFolderA && !tids.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_torrents->savePath(tids.first())));
    } else if (action == removeA) {
        removeIds(tids, false);
    } else if (action == removeDeleteA) {
        removeIds(tids, true);
    }
}

void MainWindow::onRowActivated(int row)
{
    const QString id = idForRow(row);
    if (id.isEmpty())
        return;
    if (m_torrentIds.contains(id)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_torrents->savePath(id)));
        return;
    }
    const DownloadItem* item = m_queue->item(id);
    if (!item)
        return;
    if (item->status == Status::Completed) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(item->finalPath()));
        return;
    }
    if (item->kind == QLatin1String("generic")) {
        QPointer<ProgressWindow>& w = m_progressWindows[id];
        if (!w)
            w = new ProgressWindow(m_queue, id, this);
        w->show();
        w->raise();
    }
}

// ---- dialogs / lifecycle ------------------------------------------------------------------------------

void MainWindow::openSettingsDialog()
{
    SettingsDialog dlg(m_settings, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString oldTheme = m_settings->getString("theme");
    const bool oldServer = m_settings->getBool("browser_integration_enabled");
    const int oldPort = m_settings->getInt("browser_integration_port");
    dlg.apply();
    m_queue->setGlobalSpeedLimit(m_settings->getInt("global_speed_limit_kbps"));
    m_torrents->applySettings();
    if (oldTheme != m_settings->getString("theme"))
        applyTheme();
    if (oldServer != m_settings->getBool("browser_integration_enabled") ||
        oldPort != m_settings->getInt("browser_integration_port"))
        restartLocalServer();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_quitting && m_settings->getBool("minimize_to_tray") && m_tray->isVisible()) {
        event->ignore();
        hide();
        notify("Flux Downloader", "Still running in the background.", 2000);
        return;
    }
    event->accept();
    qApp->quit();
}
