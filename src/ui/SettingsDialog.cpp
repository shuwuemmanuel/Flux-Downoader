#include "ui/SettingsDialog.h"

#include "core/SettingsManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QSpinBox* spin(int lo, int hi, int value, const QString& suffix = {})
{
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(value);
    if (!suffix.isEmpty())
        s->setSuffix(suffix);
    return s;
}

QString findChrome()
{
    QStringList candidates;
#if defined(Q_OS_WIN)
    candidates << "C:/Program Files/Google/Chrome/Application/chrome.exe"
               << "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe"
               << QDir::home().filePath("AppData/Local/Google/Chrome/Application/chrome.exe");
#elif defined(Q_OS_MACOS)
    candidates << "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
               << QDir::home().filePath("Applications/Google Chrome.app/Contents/MacOS/Google Chrome");
#endif
    for (const QString& c : candidates)
        if (QFile::exists(c))
            return c;
    for (const char* name : {"google-chrome", "google-chrome-stable", "chromium", "chromium-browser", "chrome"}) {
        const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!found.isEmpty())
            return found;
    }
    return {};
}

QString installPageHtml(const QString& extensionDir)
{
    QString escaped = extensionDir;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    QString html = QStringLiteral(R"HTML(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Flux Downloader - Install Extension</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
            max-width: 700px;
            margin: 50px auto;
            padding: 30px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
        }
        .container {
            background: rgba(255, 255, 255, 0.95);
            padding: 40px;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            color: #333;
        }
        h1 { color: #007AFF; margin-top: 0; font-size: 32px; }
        .step {
            background: #f5f5f7;
            padding: 20px;
            margin: 15px 0;
            border-radius: 12px;
            border-left: 4px solid #007AFF;
        }
        .step h3 { margin-top: 0; color: #007AFF; }
        .button {
            display: inline-block;
            background: linear-gradient(#34aaff, #007AFF);
            color: white;
            padding: 15px 30px;
            border-radius: 8px;
            text-decoration: none;
            font-weight: bold;
            margin: 10px 5px;
            font-size: 16px;
            border: none;
            cursor: pointer;
            box-shadow: 0 4px 12px rgba(0,122,255,0.3);
        }
        .button:hover { background: #0051d5; transform: translateY(-2px); }
        .path {
            background: #2d2d2d;
            color: #4af626;
            padding: 10px;
            border-radius: 6px;
            font-family: 'Courier New', monospace;
            word-break: break-all;
            margin: 10px 0;
        }
        .success { background: #e8f5e9; border-left-color: #4caf50; }
        .warning { background: #fff3e0; border-left-color: #ff9800; }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚡ Flux Downloader - Extension Installation</h1>

        <div class="step success">
            <h3>✓ Ready to Install!</h3>
            <p>Click the button below to open Chrome extensions page:</p>
            <button class="button" onclick="openExtensions()">🚀 Open Chrome Extensions</button>
        </div>

        <div class="step">
            <h3>📋 Quick Steps:</h3>
            <ol>
                <li><strong>Enable Developer Mode</strong>
                    <ul>
                        <li>Look at the TOP-RIGHT corner</li>
                        <li>Find the "Developer mode" toggle</li>
                        <li>Click to turn it ON (blue)</li>
                    </ul>
                </li>
                <br>
                <li><strong>Load Extension</strong>
                    <ul>
                        <li>Click "Load unpacked" button</li>
                        <li>Browse to the folder shown below</li>
                        <li>Click "Select Folder"</li>
                    </ul>
                </li>
                <br>
                <li><strong>Done!</strong>
                    <ul>
                        <li>Extension appears in list</li>
                        <li>Click icon to verify green dot</li>
                    </ul>
                </li>
            </ol>
        </div>

        <div class="step warning">
            <h3>📁 Extension Folder Path:</h3>
            <p>Select this folder when prompted:</p>
            <div class="path">%1</div>
            <button class="button" onclick="copyPath()">📋 Copy Path</button>
            <button class="button" onclick="openFolder()">📂 Open Folder</button>
        </div>

        <div class="step">
            <h3>❓ Need Help?</h3>
            <p>If you encounter any issues:</p>
            <ul>
                <li>Make sure Chrome is updated to the latest version</li>
                <li>Restart Chrome if extension doesn't appear</li>
                <li>Check that Flux Downloader app is running</li>
            </ul>
        </div>
    </div>

    <script>
        function openExtensions() {
            window.location.href = 'chrome://extensions/';
        }
        function copyPath() {
            const path = "%2";
            navigator.clipboard.writeText(path).then(() => {
                alert('✓ Path copied to clipboard!\n\nPaste it when browsing for the extension folder.');
            }).catch(() => {
                alert('Path: ' + path);
            });
        }
        function openFolder() {
            alert('Please manually navigate to:\n\n' + "%2" + '\n\nOr copy the path using the Copy button.');
        }
    </script>
</body>
</html>)HTML");
    return html.arg(extensionDir.toHtmlEscaped(), escaped);
}

}  // namespace

QString SettingsDialog::extensionDir()
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString& rel : {QStringLiteral("chrome_extension"), QStringLiteral("../chrome_extension"),
                               QStringLiteral("../../chrome_extension")}) {
        const QString p = QDir::cleanPath(app.filePath(rel));
        if (QFile::exists(QDir(p).filePath("manifest.json")))
            return QDir::toNativeSeparators(p);
    }
    return QDir::toNativeSeparators(app.filePath("chrome_extension"));
}

SettingsDialog::SettingsDialog(SettingsManager* settings, QWidget* parent) : QDialog(parent), m_settings(settings)
{
    setWindowTitle("Flux Downloader — Settings");
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs);

    // ---- General ----
    auto* general = new QWidget;
    auto* gform = new QFormLayout(general);
    auto* dirRow = new QHBoxLayout;
    m_dirEdit = new QLineEdit(settings->getString("download_dir"));
    auto* browse = new QPushButton("Browse…");
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "Choose folder", m_dirEdit->text());
        if (!d.isEmpty())
            m_dirEdit->setText(d);
    });
    dirRow->addWidget(m_dirEdit);
    dirRow->addWidget(browse);
    gform->addRow("Default download folder:", dirRow);

    m_themeCombo = new QComboBox;
    m_themeCombo->addItems({"aqua_light", "aqua_dark"});
    m_themeCombo->setCurrentText(settings->getString("theme"));
    gform->addRow("Theme:", m_themeCombo);

    m_trayCheck = new QCheckBox("Minimize to system tray");
    m_trayCheck->setChecked(settings->getBool("minimize_to_tray"));
    gform->addRow("", m_trayCheck);

    m_notifyCheck = new QCheckBox("Show notification when a download completes");
    m_notifyCheck->setChecked(settings->getBool("notify_on_complete"));
    gform->addRow("", m_notifyCheck);

    m_shutdownCheck = new QCheckBox("Shut down / sleep hint after queue completes");
    m_shutdownCheck->setChecked(settings->getBool("shutdown_after_queue"));
    gform->addRow("", m_shutdownCheck);

    gform->addRow("", new QLabel(""));  // Spacer
    gform->addRow("", new QLabel("<b>Download Popups:</b>"));

    m_progressWindowCheck = new QCheckBox("Show progress window for each download (IDM-style)");
    m_progressWindowCheck->setChecked(settings->getBool("show_progress_window"));
    gform->addRow("", m_progressWindowCheck);

    m_startDialogCheck = new QCheckBox("Ask for save location before starting download");
    m_startDialogCheck->setChecked(settings->getBool("show_download_start_dialog"));
    gform->addRow("", m_startDialogCheck);

    m_completeDialogCheck = new QCheckBox("Show completion dialog when download finishes");
    m_completeDialogCheck->setChecked(settings->getBool("show_download_complete_dialog"));
    gform->addRow("", m_completeDialogCheck);

    tabs->addTab(general, "General");

    // ---- Connection ----
    auto* conn = new QWidget;
    auto* cform = new QFormLayout(conn);
    m_maxConcSpin = spin(1, 20, settings->getInt("max_concurrent_downloads"));
    cform->addRow("Max simultaneous downloads:", m_maxConcSpin);
    m_maxConnSpin = spin(1, 32, settings->getInt("max_connections_per_file"));
    cform->addRow("Max connections per file (segments):", m_maxConnSpin);
    m_speedLimitSpin = spin(0, 1000000, settings->getInt("global_speed_limit_kbps"), " KB/s (0 = unlimited)");
    cform->addRow("Global speed limit:", m_speedLimitSpin);
    m_retrySpin = spin(0, 20, settings->getInt("auto_retry_count"));
    cform->addRow("Auto-retry attempts:", m_retrySpin);
    m_proxyEdit = new QLineEdit(settings->getString("proxy"));
    m_proxyEdit->setPlaceholderText("http://host:port (optional)");
    cform->addRow("Proxy:", m_proxyEdit);
    tabs->addTab(conn, "Connection");

    // ---- Browser integration ----
    auto* browser = new QWidget;
    auto* bform = new QFormLayout(browser);
    m_browserCheck = new QCheckBox("Enable browser integration server (for Chrome extension)");
    m_browserCheck->setChecked(settings->getBool("browser_integration_enabled"));
    bform->addRow("", m_browserCheck);
    m_portSpin = spin(1024, 65535, settings->getInt("browser_integration_port"));
    bform->addRow("Local server port:", m_portSpin);

    auto* installRow = new QHBoxLayout;
    auto* installBtn = new QPushButton("⚡ Auto-Install Extension");
    installBtn->setStyleSheet(
        "QPushButton { padding: 8px; font-weight: bold; background: #007AFF; color: white; }");
    connect(installBtn, &QPushButton::clicked, this, &SettingsDialog::installBrowserExtension);
    installRow->addWidget(installBtn);
    installRow->addStretch();
    bform->addRow("", installRow);
    bform->addRow(new QLabel("Click button above to automatically install extension in Chrome.\n"
                             "Extension will be loaded with one click!"));
    tabs->addTab(browser, "Browser Integration");

    // ---- Clipboard ----
    auto* clip = new QWidget;
    auto* clform = new QFormLayout(clip);
    m_clipCheck = new QCheckBox("Monitor clipboard for downloadable links");
    m_clipCheck->setChecked(settings->getBool("clipboard_monitor_enabled"));
    clform->addRow("", m_clipCheck);
    tabs->addTab(clip, "Clipboard");

    // ---- Torrents ----
    auto* torrent = new QWidget;
    auto* tform = new QFormLayout(torrent);
    m_torrentPortSpin = spin(1024, 65535, settings->getInt("torrent_port"));
    tform->addRow("Listen port:", m_torrentPortSpin);
    m_dhtCheck = new QCheckBox("Enable DHT / peer discovery (trackerless torrents)");
    m_dhtCheck->setChecked(settings->getBool("torrent_dht_enabled"));
    tform->addRow("", m_dhtCheck);
    m_torrentDlLimitSpin = spin(0, 1000000, settings->getInt("torrent_download_limit_kbps"), " KB/s (0 = unlimited)");
    tform->addRow("Torrent download limit:", m_torrentDlLimitSpin);
    m_torrentUlLimitSpin = spin(0, 1000000, settings->getInt("torrent_upload_limit_kbps"), " KB/s (0 = unlimited)");
    tform->addRow("Torrent upload limit:", m_torrentUlLimitSpin);
    tabs->addTab(torrent, "Torrents");

    // ---- Buttons ----
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* saveBtn = new QPushButton("Save");
    saveBtn->setObjectName("primary");
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    layout->addLayout(btnRow);
}

void SettingsDialog::installBrowserExtension()
{
    const QString extDir = extensionDir();
    if (!QFile::exists(QDir(extDir).filePath("manifest.json"))) {
        QMessageBox::warning(this, "Extension Not Found",
                             QStringLiteral("Chrome extension folder not found:\n%1\n\n"
                                            "Please ensure the 'chrome_extension' folder exists.")
                                 .arg(extDir));
        return;
    }

    const QString chrome = findChrome();
    if (chrome.isEmpty()) {
        QMessageBox::warning(this, "Chrome Not Found",
                             "Google Chrome is not installed or could not be found.\n\n"
                             "Please install Chrome first, then try again.");
        return;
    }

    const QString htmlFile = QDir(QDir::tempPath()).filePath("flux_install_extension.html");
    QFile f(htmlFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(installPageHtml(extDir).toUtf8()) < 0 ||
        !QProcess::startDetached(chrome, {htmlFile})) {
        QMessageBox::critical(this, "Installation Failed",
                              QStringLiteral("Could not launch Chrome.\n\n"
                                             "Manual installation:\n"
                                             "1. Open Chrome → chrome://extensions\n"
                                             "2. Enable Developer mode\n"
                                             "3. Click 'Load unpacked'\n"
                                             "4. Select: %1")
                                  .arg(extDir));
        return;
    }
    f.close();

    QMessageBox::information(this, "Opening Installation Guide",
                             "Chrome will open with installation instructions.\n\n"
                             "✓ Click the blue button to go to chrome://extensions\n"
                             "✓ Follow the simple 3-step guide\n"
                             "✓ Extension path is provided and can be copied\n\n"
                             "The extension will be ready in under 1 minute!");

    // Clean up the HTML file after 30 seconds
    QTimer::singleShot(30000, qApp, [htmlFile] { QFile::remove(htmlFile); });
}

void SettingsDialog::apply()
{
    SettingsManager* s = m_settings;
    s->set("download_dir", m_dirEdit->text().trimmed());
    s->set("theme", m_themeCombo->currentText());
    s->set("minimize_to_tray", m_trayCheck->isChecked());
    s->set("notify_on_complete", m_notifyCheck->isChecked());
    s->set("shutdown_after_queue", m_shutdownCheck->isChecked());
    s->set("show_progress_window", m_progressWindowCheck->isChecked());
    s->set("show_download_start_dialog", m_startDialogCheck->isChecked());
    s->set("show_download_complete_dialog", m_completeDialogCheck->isChecked());
    s->set("max_concurrent_downloads", m_maxConcSpin->value());
    s->set("max_connections_per_file", m_maxConnSpin->value());
    s->set("global_speed_limit_kbps", m_speedLimitSpin->value());
    s->set("auto_retry_count", m_retrySpin->value());
    s->set("proxy", m_proxyEdit->text().trimmed());
    s->set("browser_integration_enabled", m_browserCheck->isChecked());
    s->set("browser_integration_port", m_portSpin->value());
    s->set("clipboard_monitor_enabled", m_clipCheck->isChecked());
    s->set("torrent_port", m_torrentPortSpin->value());
    s->set("torrent_dht_enabled", m_dhtCheck->isChecked());
    s->set("torrent_download_limit_kbps", m_torrentDlLimitSpin->value());
    s->set("torrent_upload_limit_kbps", m_torrentUlLimitSpin->value());
}
