// KOutNet — Zapret DPI-bypass integration.
// Wraps zapret-linux/service.sh (Sergeydigl3/zapret-discord-youtube-linux
// port) as a child QProcess: "run -s <strategy> -i <iface>" configures
// nftables/iptables and starts nfqws, "kill" tears both down. The script
// itself needs root for nft/iptables/nfqws — see zapret-linux/README.md's
// setup-permissions step (adds narrowly-scoped NOPASSWD sudoers rules for
// exactly those binaries, not blanket root).
#pragma once

#include <QObject>
#include <QProcess>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

class ZapretManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString logOutput READ logOutput NOTIFY logOutputChanged)
    Q_PROPERTY(QStringList availableStrategies READ availableStrategies CONSTANT)
    Q_PROPERTY(QStringList availableInterfaces READ availableInterfaces CONSTANT)
    Q_PROPERTY(bool deployed READ deployed CONSTANT)

public:
    explicit ZapretManager(QObject *parent = nullptr);
    ~ZapretManager() override;

    static ZapretManager *create(QQmlEngine *, QJSEngine *)
    {
        return new ZapretManager;
    }

    bool running() const { return m_running; }
    QString logOutput() const { return m_logOutput; }
    QStringList availableStrategies() const;
    QStringList availableInterfaces() const;
    // False if zapret-linux/ wasn't found near the binary — lets the QML
    // tab show a "not installed" placeholder instead of a broken control.
    bool deployed() const { return !m_zapretDir.isEmpty(); }

    Q_INVOKABLE void start(const QString &strategy, const QString &iface);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clearLog();

signals:
    void runningChanged();
    void logOutputChanged();
    void errorOccurred(QString message);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    void appendLog(const QString &text);
    void setRunning(bool running);
    static QString findZapretDir();

    QProcess *m_process = nullptr;
    QString m_zapretDir;
    QString m_logOutput;
    bool m_running = false;
};

