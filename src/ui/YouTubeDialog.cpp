#include "ui/YouTubeDialog.h"

#include "core/SettingsManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

YouTubeDialog::YouTubeDialog(SettingsManager* settings, QWidget* parent, const QString& prefillUrl)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle("YouTube / Playlist / Channel Downloader");
    setMinimumSize(520, 480);

    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout;
    m_urlEdit = new QLineEdit(prefillUrl);
    m_urlEdit->setPlaceholderText("Paste a video, playlist, or channel URL…");
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &YouTubeDialog::fetch);
    form->addRow("URL:", m_urlEdit);
    layout->addLayout(form);

    auto* fetchRow = new QHBoxLayout;
    fetchRow->addStretch();
    m_fetchBtn = new QPushButton("Fetch");
    connect(m_fetchBtn, &QPushButton::clicked, this, &YouTubeDialog::fetch);
    fetchRow->addWidget(m_fetchBtn);
    layout->addLayout(fetchRow);

    m_statusLabel = new QLabel;
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    m_list = new QListWidget;
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(m_list);

    auto* selectRow = new QHBoxLayout;
    auto* allBtn = new QPushButton("Select All");
    connect(allBtn, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    auto* noneBtn = new QPushButton("Select None");
    connect(noneBtn, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    selectRow->addWidget(allBtn);
    selectRow->addWidget(noneBtn);
    selectRow->addStretch();
    layout->addLayout(selectRow);

    auto* optsForm = new QFormLayout;
    m_modeCombo = new QComboBox;
    m_modeCombo->addItems({"Video (best quality)", "Video 1080p", "Video 720p", "Audio only (MP3)"});
    const QString def = settings->getString("youtube_default_format");
    if (def == QLatin1String("1080"))
        m_modeCombo->setCurrentIndex(1);
    else if (def == QLatin1String("720"))
        m_modeCombo->setCurrentIndex(2);
    else if (def == QLatin1String("audio"))
        m_modeCombo->setCurrentIndex(3);
    optsForm->addRow("Format:", m_modeCombo);
    m_subsCheck = new QCheckBox("Download subtitles if available");
    m_subsCheck->setChecked(settings->getBool("youtube_download_subtitles"));
    optsForm->addRow("", m_subsCheck);
    layout->addLayout(optsForm);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    m_addBtn = new QPushButton("Download Selected");
    m_addBtn->setObjectName("primary");
    connect(m_addBtn, &QPushButton::clicked, this, &QDialog::accept);
    m_addBtn->setEnabled(false);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(m_addBtn);
    layout->addLayout(btnRow);

    if (!prefillUrl.isEmpty())
        fetch();
}

void YouTubeDialog::fetch()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty() || m_probe)
        return;
    m_statusLabel->setText("Resolving URL… this may take a moment for large channels.");
    m_statusLabel->setStyleSheet(QString());
    m_fetchBtn->setEnabled(false);
    m_addBtn->setEnabled(false);
    m_list->clear();
    m_probe = new YouTubeProbe(this);
    connect(m_probe, &YouTubeProbe::resolved, this, &YouTubeDialog::onResolved);
    connect(m_probe, &YouTubeProbe::failed, this, &YouTubeDialog::onFailed);
    m_probe->start(url);
}

void YouTubeDialog::onResolved(const QVector<YouTubeEntry>& entries, const QString& kind, bool isLivestream)
{
    if (m_probe)
        m_probe->deleteLater();
    m_entries = entries;
    m_kind = kind;
    m_isLivestream = isLivestream;
    m_fetchBtn->setEnabled(true);

    QString kindText = kind;
    kindText.replace('_', ' ');
    QString statusText = QStringLiteral("Found %1 item(s) — %2").arg(entries.size()).arg(kindText);
    if (isLivestream)
        statusText += " [LIVE STREAM - will download from beginning]";
    m_statusLabel->setText(statusText);
    m_statusLabel->setStyleSheet(isLivestream ? "color: #d32f2f;" : "");

    for (int i = 0; i < entries.size(); ++i) {
        const YouTubeEntry& e = entries[i];
        QString title = !e.title.isEmpty() ? e.title : !e.id.isEmpty() ? e.id : e.url;
        if (e.isLive)
            title = QStringLiteral("🔴 [LIVE] ") + title;
        auto* item = new QListWidgetItem(title);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, i);
        m_list->addItem(item);
    }
    m_addBtn->setEnabled(!entries.isEmpty());
}

void YouTubeDialog::onFailed(const QString& message)
{
    if (m_probe)
        m_probe->deleteLater();
    m_fetchBtn->setEnabled(true);
    m_statusLabel->setText("Failed to resolve URL.");
    QMessageBox::warning(this, "YouTube Downloader", QStringLiteral("Could not resolve URL:\n%1").arg(message));
}

void YouTubeDialog::setAllChecked(bool checked)
{
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}

QVector<YouTubeEntry> YouTubeDialog::selectedEntries() const
{
    QVector<YouTubeEntry> out;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem* item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            out << m_entries.value(item->data(Qt::UserRole).toInt());
    }
    return out;
}

YouTubeDialog::Options YouTubeDialog::options() const
{
    Options o;
    const QString mode = m_modeCombo->currentText();
    o.audioOnly = mode.startsWith("Audio");
    if (mode.contains("1080p"))
        o.quality = "1080";
    else if (mode.contains("720p"))
        o.quality = "720";
    o.subtitles = m_subsCheck->isChecked();
    o.isLivestream = m_isLivestream;
    return o;
}
