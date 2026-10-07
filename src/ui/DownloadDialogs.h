// Flux Downloader - Download Start and Completion Dialogs
#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;

// Shown when a download starts - choose save location.
class DownloadStartDialog : public QDialog {
    Q_OBJECT
public:
    DownloadStartDialog(const QString& filename, const QString& url, const QString& suggestedDir,
                        QWidget* parent = nullptr);
    QString savePath() const;
    bool shouldRemember() const;

private:
    QString m_filename;
    QString m_suggestedDir;
    QLineEdit* m_pathEdit;
    QCheckBox* m_rememberCheck;
};

// Shown when a download completes.
class DownloadCompletedDialog : public QDialog {
    Q_OBJECT
public:
    DownloadCompletedDialog(const QString& filename, const QString& filepath, QWidget* parent = nullptr);
    bool shouldHideFuture() const;

private:
    QString m_filepath;
    QCheckBox* m_dontShowCheck;
};
