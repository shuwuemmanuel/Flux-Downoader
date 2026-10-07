#include "ui/AddDownloadDialog.h"

#include "core/DownloadEngine.h"
#include "core/DownloadItem.h"
#include "core/SettingsManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

AddDownloadDialog::AddDownloadDialog(SettingsManager* settings, QWidget* parent, const QString& prefillUrl)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle("Add New Download");
    setMinimumWidth(480);

    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    layout->addWidget(m_tabs);

    // --- Single URL tab ---
    auto* single = new QWidget;
    auto* form = new QFormLayout(single);
    m_urlEdit = new QLineEdit(prefillUrl);
    connect(m_urlEdit, &QLineEdit::textChanged, this, &AddDownloadDialog::syncFromUrl);
    form->addRow("URL:", m_urlEdit);

    m_filenameEdit = new QLineEdit;
    form->addRow("Save as:", m_filenameEdit);

    auto* dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(settings->getString("download_dir"));
    connect(m_dirEdit, &QLineEdit::textEdited, this, [this] { m_dirTouched = true; });
    auto* browseBtn = new QPushButton("Browse…");
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "Choose folder", m_dirEdit->text());
        if (!d.isEmpty()) {
            m_dirEdit->setText(d);
            m_dirTouched = true;
        }
    });
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(browseBtn);
    form->addRow("Folder:", dirRow);

    m_categoryCombo = new QComboBox;
    m_categoryCombo->addItems(settings->categoryNames());
    connect(m_categoryCombo, &QComboBox::currentTextChanged, this, &AddDownloadDialog::syncFolder);
    form->addRow("Category:", m_categoryCombo);

    m_priorityCombo = new QComboBox;
    m_priorityCombo->addItems(Priority::levels());
    m_priorityCombo->setCurrentText("Normal");
    form->addRow("Priority:", m_priorityCombo);

    m_checksumEdit = new QLineEdit;
    m_checksumEdit->setPlaceholderText("optional - sha256/md5 to verify after download");
    form->addRow("Checksum:", m_checksumEdit);

    m_startNowCheck = new QCheckBox("Start download immediately");
    m_startNowCheck->setChecked(true);
    form->addRow("", m_startNowCheck);

    m_tabs->addTab(single, "Single URL");

    // --- Batch tab ---
    auto* batch = new QWidget;
    auto* bl = new QVBoxLayout(batch);
    bl->addWidget(new QLabel("Paste one URL per line:"));
    m_batchText = new QTextEdit;
    m_batchText->setAcceptRichText(false);
    bl->addWidget(m_batchText);
    m_tabs->addTab(batch, "Batch (multiple URLs)");

    syncFolder();
    if (!prefillUrl.isEmpty())
        syncFromUrl();

    // --- Buttons ---
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* okBtn = new QPushButton("Add Download");
    okBtn->setObjectName("primary");
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);
}

void AddDownloadDialog::syncFromUrl()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    // Only overwrite the name if the user hasn't typed their own.
    if (m_filenameEdit->text().isEmpty() || m_filenameEdit->text() == m_autoFilename) {
        m_autoFilename = guessFilename(url);
        m_filenameEdit->setText(m_autoFilename);
    }
    if (m_settings->getBool("auto_categorize")) {
        const QString cat = m_settings->categoryForExt(QStringLiteral(".") + QFileInfo(m_filenameEdit->text()).suffix());
        m_categoryCombo->setCurrentText(cat);
    }
}

void AddDownloadDialog::syncFolder()
{
    // The folder follows the category's folder until the user picks one.
    if (m_dirTouched || !m_settings->getBool("auto_categorize"))
        return;
    const QString cat = m_categoryCombo->currentText();
    if (!cat.isEmpty())
        m_dirEdit->setText(m_settings->dirForCategory(cat));
}

bool AddDownloadDialog::isBatchMode() const
{
    return m_tabs->currentIndex() == 1;
}

QStringList AddDownloadDialog::batchUrls() const
{
    QStringList out;
    for (const QString& line : m_batchText->toPlainText().split('\n')) {
        const QString t = line.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

AddDownloadDialog::Result AddDownloadDialog::resultData() const
{
    Result r;
    r.url = m_urlEdit->text().trimmed();
    const QString typed = sanitizeFilename(m_filenameEdit->text().trimmed());
    r.filename = typed.isEmpty() ? guessFilename(r.url) : typed;
    r.filenameEdited = !typed.isEmpty() && typed != m_autoFilename;
    r.saveDir = m_dirEdit->text().trimmed();
    if (r.saveDir.isEmpty())
        r.saveDir = m_settings->getString("download_dir");
    r.category = m_categoryCombo->currentText();
    r.checksum = m_checksumEdit->text().trimmed();
    r.startNow = m_startNowCheck->isChecked();
    r.priority = m_priorityCombo->currentText();
    return r;
}
