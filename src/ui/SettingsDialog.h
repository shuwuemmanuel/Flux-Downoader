#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class SettingsManager;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsManager* settings, QWidget* parent = nullptr);
    void apply();

    static QString extensionDir();

private:
    void installBrowserExtension();

    SettingsManager* m_settings;
    QLineEdit* m_dirEdit;
    QComboBox* m_themeCombo;
    QCheckBox* m_trayCheck;
    QCheckBox* m_notifyCheck;
    QCheckBox* m_shutdownCheck;
    QCheckBox* m_progressWindowCheck;
    QCheckBox* m_startDialogCheck;
    QCheckBox* m_completeDialogCheck;
    QSpinBox* m_maxConcSpin;
    QSpinBox* m_maxConnSpin;
    QSpinBox* m_speedLimitSpin;
    QSpinBox* m_retrySpin;
    QLineEdit* m_proxyEdit;
    QCheckBox* m_browserCheck;
    QSpinBox* m_portSpin;
    QCheckBox* m_clipCheck;
    QSpinBox* m_torrentPortSpin;
    QCheckBox* m_dhtCheck;
    QSpinBox* m_torrentDlLimitSpin;
    QSpinBox* m_torrentUlLimitSpin;
};
