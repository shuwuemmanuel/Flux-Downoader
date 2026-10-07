#include "ui/AddTorrentDialog.h"

#include "core/SettingsManager.h"
#include "ui/Format.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <stdexcept>

namespace {
const QStringList kFilePriorityLabels = {"Skip (don't download)", "Low", "Normal", "High"};
const int kFilePriorityValues[] = {TorrentPriority::Skip, TorrentPriority::Low, TorrentPriority::Normal,
                                   TorrentPriority::High};

int labelIndexFor(int priority)
{
    for (int i = 0; i < 4; ++i)
        if (kFilePriorityValues[i] == priority)
            return i;
    if (priority == 0)
        return 0;
    return priority < TorrentPriority::Normal ? 1 : priority > TorrentPriority::Normal ? 3 : 2;
}
}  // namespace

AddTorrentDialog::AddTorrentDialog(SettingsManager* settings, TorrentEngine* engine, QWidget* parent,
                                   const QString& prefillMagnet)
    : QDialog(parent), m_settings(settings), m_engine(engine)
{
    setWindowTitle("Add Torrent");
    setMinimumSize(560, 480);

    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs);

    // --- Magnet tab ---
    auto* magnetTab = new QWidget;
    auto* mform = new QFormLayout(magnetTab);
    m_magnetEdit = new QLineEdit(prefillMagnet);
    m_magnetEdit->setPlaceholderText("magnet:?xt=urn:btih:…");
    mform->addRow("Magnet URI:", m_magnetEdit);
    m_tabs->addTab(magnetTab, "Magnet Link");

    // --- Torrent file tab ---
    auto* fileTab = new QWidget;
    auto* fform = new QFormLayout(fileTab);
    auto* fileRow = new QHBoxLayout;
    m_fileEdit = new QLineEdit;
    auto* browseBtn = new QPushButton("Browse…");
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getOpenFileName(this, "Choose .torrent file", QString(), "Torrent files (*.torrent)");
        if (!path.isEmpty())
            m_fileEdit->setText(path);
    });
    fileRow->addWidget(m_fileEdit);
    fileRow->addWidget(browseBtn);
    fform->addRow(".torrent file:", fileRow);
    m_tabs->addTab(fileTab, ".torrent File");

    // --- Save location + queue priority ---
    auto* form2 = new QFormLayout;
    auto* dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(QDir(settings->getString("download_dir")).filePath("Torrents"));
    auto* dirBrowse = new QPushButton("Browse…");
    connect(dirBrowse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "Choose folder", m_dirEdit->text());
        if (!d.isEmpty())
            m_dirEdit->setText(d);
    });
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(dirBrowse);
    form2->addRow("Save to:", dirRow);

    m_priorityCombo = new QComboBox;
    m_priorityCombo->addItems(torrentQueuePriorities());
    m_priorityCombo->setCurrentText(settings->getString("torrent_default_priority").isEmpty()
                                        ? QStringLiteral("Normal")
                                        : settings->getString("torrent_default_priority"));
    form2->addRow("Download priority:", m_priorityCombo);
    layout->addLayout(form2);

    auto* fetchRow = new QHBoxLayout;
    fetchRow->addWidget(new QLabel("Select files to download:"));
    fetchRow->addStretch();
    m_fetchBtn = new QPushButton("Fetch File List");
    connect(m_fetchBtn, &QPushButton::clicked, this, &AddTorrentDialog::fetchMetadata);
    fetchRow->addWidget(m_fetchBtn);
    layout->addLayout(fetchRow);

    m_fileTable = new QTableWidget(0, 3);
    m_fileTable->setHorizontalHeaderLabels({"File", "Size", "Priority"});
    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_fileTable);

    m_statusLabel = new QLabel(
        "For magnet links, click 'Fetch File List' after adding to resolve metadata "
        "from peers/DHT — you can also just download everything with default priorities.");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &AddTorrentDialog::reject);
    auto* addBtn = new QPushButton("Add Torrent");
    addBtn->setObjectName("primary");
    addBtn->setDefault(true);
    connect(addBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(addBtn);
    layout->addLayout(btnRow);

    connect(m_engine, &TorrentEngine::metadataReady, this, &AddTorrentDialog::onMetadataReady);
}

void AddTorrentDialog::fetchMetadata()
{
    if (!TorrentEngine::available()) {
        QMessageBox::warning(this, "Torrent support unavailable",
                             "This build of Flux Downloader was compiled without libtorrent.");
        return;
    }
    if (!m_pendingTid.isEmpty())
        return;  // already fetched / fetching
    QString saveDir = m_dirEdit->text().trimmed();
    if (saveDir.isEmpty())
        saveDir = m_settings->getString("download_dir");
    QDir().mkpath(saveDir);
    QString tid;
    try {
        if (m_tabs->currentIndex() == 1 && !m_fileEdit->text().trimmed().isEmpty()) {
            tid = m_engine->addTorrentFile(m_fileEdit->text().trimmed(), saveDir);
        } else {
            const QString uri = m_magnetEdit->text().trimmed();
            if (uri.isEmpty())
                return;
            tid = m_engine->addMagnet(uri, saveDir);
        }
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Torrent error", QString::fromUtf8(e.what()));
        return;
    }
    m_pendingTid = tid;
    m_fetchBtn->setEnabled(false);
    if (m_fileRows.isEmpty())
        m_statusLabel->setText("Resolving metadata… (magnet links may take a few seconds via DHT/peers)");
}

void AddTorrentDialog::onMetadataReady(const QString& tid, const QVector<TorrentFileInfo>& files)
{
    if (tid != m_pendingTid)
        return;
    m_statusLabel->setText(QStringLiteral("%1 file(s) found — choose priorities below.").arg(files.size()));
    m_fileTable->setRowCount(0);
    m_fileRows.clear();
    for (const TorrentFileInfo& f : files) {
        const int row = m_fileTable->rowCount();
        m_fileTable->insertRow(row);
        m_fileTable->setItem(row, 0, new QTableWidgetItem(f.path));
        m_fileTable->setItem(row, 1, new QTableWidgetItem(Fmt::size(static_cast<double>(f.size))));
        auto* combo = new QComboBox;
        combo->addItems(kFilePriorityLabels);
        combo->setCurrentIndex(labelIndexFor(f.priority));
        m_fileTable->setCellWidget(row, 2, combo);
        m_fileRows.push_back({f, combo});
    }
}

void AddTorrentDialog::applyFilePriorities(const QString& tid)
{
    for (const auto& row : m_fileRows)
        m_engine->setFilePriority(tid, row.first.index, kFilePriorityValues[row.second->currentIndex()]);
}

AddTorrentDialog::Result AddTorrentDialog::resultData() const
{
    Result r;
    r.fileMode = m_tabs->currentIndex() == 1;
    r.magnet = m_magnetEdit->text().trimmed();
    r.torrentFile = m_fileEdit->text().trimmed();
    r.saveDir = m_dirEdit->text().trimmed();
    if (r.saveDir.isEmpty())
        r.saveDir = m_settings->getString("download_dir");
    r.priority = m_priorityCombo->currentText();
    return r;
}

void AddTorrentDialog::reject()
{
    // "Fetch File List" already added the torrent; canceling should undo that.
    if (!m_pendingTid.isEmpty()) {
        m_engine->remove(m_pendingTid, false);
        m_pendingTid.clear();
    }
    QDialog::reject();
}
