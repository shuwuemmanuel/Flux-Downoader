#include "ui/DownloadDialogs.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

DownloadStartDialog::DownloadStartDialog(const QString& filename, const QString& url, const QString& suggestedDir,
                                         QWidget* parent)
    : QDialog(parent), m_filename(filename), m_suggestedDir(suggestedDir)
{
    setWindowTitle("Start Download");
    setMinimumWidth(600);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel("📥 New Download");
    // The app stylesheet sets a global font-size, which beats setFont().
    title->setStyleSheet("font-size: 16px; font-weight: bold;");
    layout->addWidget(title);

    auto* infoGroup = new QGroupBox("Download Information");
    auto* infoLayout = new QFormLayout;
    auto* filenameLabel = new QLabel(filename);
    filenameLabel->setWordWrap(true);
    filenameLabel->setStyleSheet("font-weight: bold;");
    infoLayout->addRow("File name:", filenameLabel);
    auto* urlLabel = new QLabel(url);
    urlLabel->setWordWrap(true);
    urlLabel->setStyleSheet("color: #0066cc;");
    infoLayout->addRow("URL:", urlLabel);
    infoGroup->setLayout(infoLayout);
    layout->addWidget(infoGroup);

    auto* locationGroup = new QGroupBox("Save Location");
    auto* locationLayout = new QVBoxLayout;
    auto* pathLayout = new QHBoxLayout;
    m_pathEdit = new QLineEdit(QDir(suggestedDir).filePath(filename));
    m_pathEdit->setReadOnly(true);
    auto* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getSaveFileName(this, "Save File As", m_pathEdit->text(), "All Files (*.*)");
        if (!path.isEmpty())
            m_pathEdit->setText(path);
    });
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(browseBtn);
    locationLayout->addLayout(pathLayout);
    locationGroup->setLayout(locationLayout);
    layout->addWidget(locationGroup);

    m_rememberCheck = new QCheckBox("Don't ask me again (auto-save to default location)");
    layout->addWidget(m_rememberCheck);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* downloadBtn = new QPushButton("Start Download");
    downloadBtn->setObjectName("primary");
    downloadBtn->setDefault(true);
    connect(downloadBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(downloadBtn);
    layout->addLayout(btnLayout);
}

QString DownloadStartDialog::savePath() const
{
    return m_pathEdit->text();
}

bool DownloadStartDialog::shouldRemember() const
{
    return m_rememberCheck->isChecked();
}

DownloadCompletedDialog::DownloadCompletedDialog(const QString& filename, const QString& filepath, QWidget* parent)
    : QDialog(parent), m_filepath(filepath)
{
    setWindowTitle("Download Completed");
    setMinimumWidth(500);
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QVBoxLayout(this);

    auto* successLayout = new QHBoxLayout;
    auto* iconLabel = new QLabel("✅");
    iconLabel->setStyleSheet("font-size: 40px;");
    auto* msgLabel = new QLabel(QStringLiteral("<b>%1</b><br>has been downloaded successfully!").arg(filename.toHtmlEscaped()));
    msgLabel->setWordWrap(true);
    successLayout->addWidget(iconLabel);
    successLayout->addWidget(msgLabel, 1);
    layout->addLayout(successLayout);

    auto* locationLabel = new QLabel(QStringLiteral("<b>Location:</b> %1").arg(QDir::toNativeSeparators(filepath).toHtmlEscaped()));
    locationLabel->setWordWrap(true);
    locationLabel->setStyleSheet("color: #666; padding: 10px;");
    layout->addWidget(locationLabel);

    m_dontShowCheck = new QCheckBox("Don't show this notification again");
    layout->addWidget(m_dontShowCheck);

    auto* btnLayout = new QHBoxLayout;
    auto* openFileBtn = new QPushButton("Open File");
    connect(openFileBtn, &QPushButton::clicked, this, [this] {
        if (QFileInfo::exists(m_filepath)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_filepath));
            accept();
        } else {
            QMessageBox::warning(this, "File Not Found", "The downloaded file could not be found.");
        }
    });
    auto* openFolderBtn = new QPushButton("Open Folder");
    connect(openFolderBtn, &QPushButton::clicked, this, [this] {
        const QString folder = QFileInfo(m_filepath).absolutePath();
        if (QFileInfo::exists(folder)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            accept();
        } else {
            QMessageBox::warning(this, "Folder Not Found", "The download folder could not be found.");
        }
    });
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(openFileBtn);
    btnLayout->addWidget(openFolderBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}

bool DownloadCompletedDialog::shouldHideFuture() const
{
    return m_dontShowCheck->isChecked();
}
