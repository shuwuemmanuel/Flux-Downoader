// Single URL add dialog, plus a 'Batch' tab for pasting many URLs at once
// (IDM-style batch download).
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTabWidget;
class QTextEdit;
class SettingsManager;

class AddDownloadDialog : public QDialog {
    Q_OBJECT
public:
    struct Result {
        QString url;
        QString filename;
        QString saveDir;
        QString category;
        QString checksum;
        bool startNow = true;
        QString priority;
        bool filenameEdited = false;
    };

    explicit AddDownloadDialog(SettingsManager* settings, QWidget* parent = nullptr, const QString& prefillUrl = {});

    bool isBatchMode() const;
    QStringList batchUrls() const;
    Result resultData() const;

private:
    void syncFromUrl();
    void syncFolder();

    SettingsManager* m_settings;
    QTabWidget* m_tabs;
    QLineEdit* m_urlEdit;
    QLineEdit* m_filenameEdit;
    QLineEdit* m_dirEdit;
    QComboBox* m_categoryCombo;
    QComboBox* m_priorityCombo;
    QLineEdit* m_checksumEdit;
    QCheckBox* m_startNowCheck;
    QTextEdit* m_batchText;
    QString m_autoFilename;
    bool m_dirTouched = false;
};
