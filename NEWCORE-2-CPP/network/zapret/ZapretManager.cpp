#include "ZapretManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QDebug>

namespace {

// Same upward walk as Translations::findI18nDir(), so it works from build/
// during development and from an installed layout later.
//
// The two platform variants are not just a folder rename: zapret-linux
// drives nfqws through service.sh and nftables, zapret-windows wraps
// winws.exe with WinDivert. Picking the right one up front lets this fail
// loudly instead of trying to run a bash script on Windows.
QString findZapretDirImpl()
{
#if defined(Q_OS_WIN)
    const QString dirName = QStringLiteral("zapret-windows");
    const QString marker = QStringLiteral("winws.exe");
#else
    const QString dirName = QStringLiteral("zapret-linux");
    const QString marker = QStringLiteral("service.sh");
#endif

    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath(dirName);
        if (QFile::exists(candidate + "/" + marker))
            return candidate;
        if (!dir.cdUp())
            break;
    }
    return QString();
}

} // namespace

ZapretManager::ZapretManager(QObject *parent) : QObject(parent)
{
    m_zapretDir = findZapretDirImpl();
    if (m_zapretDir.isEmpty()) {
        qWarning() << "ZapretManager: zapret-linux/ not found near"
                   << QCoreApplication::applicationDirPath();
    }
}

ZapretManager::~ZapretManager()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
        stop();
}

QString ZapretManager::findZapretDir()
{
    return findZapretDirImpl();
}

QStringList ZapretManager::availableStrategies() const
{
    QStringList result;
    if (m_zapretDir.isEmpty())
        return result;

    // Mirrors src/lib/common.sh's own listing logic: custom-strategies/*.bat
    // first (user overrides), then the general*/discord*.bat presets from
    // the cloned zapret-latest/ repo.
    QDir customDir(m_zapretDir + "/custom-strategies");
    for (const QString &f : customDir.entryList(QStringList() << "*.bat", QDir::Files, QDir::Name))
        result << f;

    QDir repoDir(m_zapretDir + "/zapret-latest");
    QStringList filters;
    filters << "general*.bat" << "discord*.bat";
    for (const QString &f : repoDir.entryList(filters, QDir::Files, QDir::Name))
        result << f;

    return result;
}

QStringList ZapretManager::availableInterfaces() const
{
    QStringList result;
    result << QStringLiteral("any");

    QDir netDir(QStringLiteral("/sys/class/net"));
    for (const QString &iface : netDir.entryList(QDir::NoDotAndDotDot | QDir::Dirs, QDir::Name))
        result << iface;

    return result;
}

void ZapretManager::start(const QString &strategy, const QString &iface)
{
    if (m_zapretDir.isEmpty()) {
        emit errorOccurred(QStringLiteral("zapret directory not found for this platform"));
        return;
    }
#if defined(Q_OS_WIN)
    // zapret-windows drives winws.exe/WinDivert directly, not through
    // service.sh - that launch path isn't implemented yet. Fail loudly
    // here instead of trying to run a bash script that doesn't exist on
    // Windows.
    emit errorOccurred(QStringLiteral("Windows zapret integration not implemented yet"));
    return;
#endif
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit errorOccurred(QStringLiteral("zapret is already running"));
        return;
    }

    clearLog();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_zapretDir);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ZapretManager::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &ZapretManager::onReadyReadStderr);
    connect(m_process, &QProcess::finished,
            this, &ZapretManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &ZapretManager::onProcessErrorOccurred);

    const QStringList args = {
        QStringLiteral("run"),
        QStringLiteral("-s"), strategy,
        QStringLiteral("-i"), iface,
    };

    appendLog(QStringLiteral("$ ./service.sh %1\n").arg(args.join(' ')));
    m_process->start(m_zapretDir + "/service.sh", args);
}

void ZapretManager::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        setRunning(false);
        return;
    }

    // The script's own run loop traps SIGINT/SIGTERM to call stop_zapret
    // (stop_nfqws + firewall_clear) - ask nicely first, same as Ctrl+C in
    // a terminal, so it cleans up its own nftables/iptables rules rather
    // than leaving them behind.
    m_process->terminate();
    if (!m_process->waitForFinished(5000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }

    // Belt-and-suspenders: if the process was killed hard (or crashed)
    // before its trap ran, the firewall rules and nfqws process could
    // still be left behind. "service.sh kill" runs stop_zapret() directly
    // and is safe to call even if there's nothing to clean up.
    QProcess cleanup;
    cleanup.setWorkingDirectory(m_zapretDir);
    cleanup.start(m_zapretDir + "/service.sh", {QStringLiteral("kill")});
    cleanup.waitForFinished(5000);
    appendLog(QString::fromUtf8(cleanup.readAllStandardOutput()));
    appendLog(QString::fromUtf8(cleanup.readAllStandardError()));

    setRunning(false);
}

void ZapretManager::clearLog()
{
    m_logOutput.clear();
    emit logOutputChanged();
}

void ZapretManager::onReadyReadStdout()
{
    appendLog(QString::fromUtf8(m_process->readAllStandardOutput()));
    // The script prints its "started" banner only after nftables/nfqws
    // setup succeeds - treat that as our running signal rather than just
    // "process exists", since the process is alive during setup too.
    if (!m_running && m_logOutput.contains(QStringLiteral("Настройка успешно завершена")))
        setRunning(true);
}

void ZapretManager::onReadyReadStderr()
{
    appendLog(QString::fromUtf8(m_process->readAllStandardError()));
}

void ZapretManager::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status)
    if (exitCode != 0)
        emit errorOccurred(QStringLiteral("zapret exited with code %1").arg(exitCode));
    setRunning(false);
}

void ZapretManager::onProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    emit errorOccurred(m_process ? m_process->errorString() : QStringLiteral("unknown process error"));
    setRunning(false);
}

void ZapretManager::appendLog(const QString &text)
{
    if (text.isEmpty())
        return;
    m_logOutput += text;
    // Keep the in-memory log bounded - this is a debug console, not a
    // persistent log store, and unbounded growth would leak memory on a
    // long-running session.
    constexpr int kMaxLogChars = 200000;
    if (m_logOutput.size() > kMaxLogChars)
        m_logOutput = m_logOutput.right(kMaxLogChars);
    emit logOutputChanged();
}

void ZapretManager::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged();
}

