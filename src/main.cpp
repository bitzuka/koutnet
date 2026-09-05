// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet entry point
// QApplication rather than QGuiApplication: the tray menu needs a QWidget and
// crashes without one, at the cost of linking Qt6::Widgets.
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
// setDesktopFileName is a QGuiApplication static, called by class name below.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QVariantMap>
#include <clocale>

#include <Quotient/networkaccessmanager.h>

#include "core/security/CryptoManager.h"
#include "network/FileTransferHandler.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"
#include <KAboutData>
#include <KColorSchemeManager>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include "core/audio/AudioDevices.h"
#include "core/backend/ChatBackendRegistry.h"
#include "core/chat/HistoryManager.h"
#include "core/constructor/AppSettings.h"
#include "core/notify/NotificationManager.h"
#include "core/tray/TrayIcon.h"
#include "matrix/MatrixManager.h"
#include "matrix/MatrixRoomBridge.h"
#include "matrix/MatrixVerification.h"
#include "rocketchat/RocketChatBackend.h"
#include "telegram/TelegramBackend.h"
#include "tox/ToxBackend.h"

#include "koutnet-version.h"
#include "koutnet_app_debug.h"
#include "koutnet_crypto_debug.h"
#include "koutnet_network_debug.h"

namespace
{
// one factory per thread; QQmlEngine will not ask libQuotient for it on its own
class MatrixNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *) override
    {
        return Quotient::NetworkAccessManager::instance();
    }
};
} // namespace

int main(int argc, char *argv[])
{
    // set the QQC2 style before the app starts, Qt picks a fallback otherwise
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QApplication app(argc, argv);
    // hide on close so the tray keeps the app alive; Qt would quit otherwise
    QApplication::setQuitOnLastWindowClosed(false);

    // icons here use breeze names, so add a breeze fallback for non-KDE desktops
    if (QIcon::fallbackThemeName().isEmpty())
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    // Ki18n in this build ignores KI18N_LOCALE_PATH, so register the catalog
    // directory here. A binary run from the build tree or an AppImage is not
    // under /usr/share/locale, and gettext only honours the system locale once
    // setlocale(LC_MESSAGES, "") has run - without both, the UI stays English
    // no matter how complete po/ru is.
    const QString exePath = QCoreApplication::applicationDirPath();
    const QString moPath = QStringLiteral("/ru/LC_MESSAGES/koutnet.mo");
    QString localeDir = exePath + QStringLiteral("/../src/locale");
    if (!QFile::exists(localeDir + moPath))
        localeDir = qEnvironmentVariable("APPDIR") + QStringLiteral("/usr/share/locale");
    if (!QFile::exists(localeDir + moPath))
        localeDir = exePath + QStringLiteral("/../share/locale");
    if (!QFile::exists(localeDir + moPath))
        localeDir = QStringLiteral("/usr/share/locale");
    KLocalizedString::addDomainLocaleDir(QByteArrayLiteral("koutnet"), localeDir);

    // set the translation catalog before any string is looked up
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));

    // apply the saved interface language before the first i18n call;
    // AppSettings is built later, so use a throwaway instance here
    const QString interfaceLanguage = koutnet::AppSettings().language();
    if (!interfaceLanguage.isEmpty())
        KLocalizedString::setLanguages({interfaceLanguage});

    setlocale(LC_MESSAGES, "");

    KAboutData aboutData(QStringLiteral("koutnet"),
                         i18nc("@title application name", "KOutNet"),
                         QStringLiteral(KOUTNET_VERSION_STRING),
                         i18nc("@info:whatsthis", "P2P encrypted messenger for LAN, VPN and Matrix. Linux only; Windows and macOS are unsupported."),
                         KAboutLicense::GPL_V3,
                         i18nc("@info:credit", "Copyright 2026 bitzuka"));
    aboutData.addAuthor(i18nc("@info:credit", "bitzuka"), i18nc("@info:credit", "Author and maintainer"), QStringLiteral("bitzuka.koutnet@gmail.com"));
    aboutData.setHomepage(QStringLiteral("https://github.com/bitzuka/koutnet"));
    // DrKonqi files crashes here, so point it at a real tracker (metainfo uses github)
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/bitzuka/koutnet/issues"));
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    // this is the AppStream id, not the app name; matching them would move QSettings
    // and re-key the Olm pickle libQuotient stores, breaking old encrypted history
    aboutData.setDesktopFileName(QStringLiteral("io.github.bitzuka.koutnet"));
    KAboutData::setApplicationData(aboutData);

    // call this after setApplicationData, else QSettings lands in the wrong file
    // and the plaintext-key cleanup reads a file that was never written
    app.setApplicationName(QStringLiteral("KOutNet"));
    app.setOrganizationName(QStringLiteral("KOutNet"));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Wayland takes the icon from the desktop file app_id; X11 uses the hint below
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.bitzuka.koutnet"));
    // resources live under the URI path, not /qt/qml (older CMake resource policy)
    const QIcon appIcon(QStringLiteral(":/koutnet/app/assets/512-apps-io.github.bitzuka.koutnet.png"));
    if (appIcon.isNull())
        qCWarning(KOUTNET_LOG_APP, "application icon missing from the QML module resources");
    app.setWindowIcon(appIcon);

    // one shared CryptoManager passed to every module; do not make another
    auto *crypto = new koutnet::CryptoManager(&app);
    if (!crypto->isValid()) {
        qCCritical(KOUTNET_LOG_CRYPTO, "cryptographic identity failed to initialize - aborting startup");
        return 1;
    }

    // after CryptoManager, since that is the last thing touching the config via QSettings
    auto *appSettings = new koutnet::AppSettings(&app);
    auto *network = new koutnet::NetworkManager(crypto, &app);

    network->setStaticPeers(appSettings->staticPeers());
    network->setConnectionMode(static_cast<koutnet::NetworkManager::ConnectionMode>(appSettings->connectionMode()));
    auto *voice = new koutnet::VoiceCallManager(network, crypto, &app);
    auto *fileTransfer = new koutnet::FileTransferHandler(&app);
    const auto publishProfile = [network, appSettings]() {
        const QString material = appSettings->displayName() + QChar(0x1f) + appSettings->bio() + QChar(0x1f) + appSettings->avatarPath() + QChar(0x1f)
            + appSettings->bannerPath() + QChar(0x1f) + appSettings->nameBadgePath();
        const QString revision = QString::fromLatin1(QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
        network->setProfile(appSettings->username(), appSettings->displayName(), appSettings->bio(), revision);
    };
    publishProfile();
    const auto publishStatus = [network, appSettings]() {
        network->setStatus(appSettings->presence(), appSettings->statusEmoji());
    };
    publishStatus();
    for (auto signal : {&koutnet::AppSettings::presenceChanged, &koutnet::AppSettings::statusEmojiChanged})
        QObject::connect(appSettings, signal, network, publishStatus);
    network->setGroupPassphrase(appSettings->groupPassphrase());
    QObject::connect(appSettings, &koutnet::AppSettings::groupPassphraseChanged, network, [network, appSettings]() {
        network->setGroupPassphrase(appSettings->groupPassphrase());
    });
    for (auto signal : {&koutnet::AppSettings::usernameChanged,
                        &koutnet::AppSettings::displayNameChanged,
                        &koutnet::AppSettings::bioChanged,
                        &koutnet::AppSettings::avatarPathChanged,
                        &koutnet::AppSettings::bannerPathChanged,
                        &koutnet::AppSettings::nameBadgePathChanged}) {
        QObject::connect(appSettings, signal, network, publishProfile);
    }

    // Matrix is separate from NetworkManager and registers as its own backend.
    // QML only talks to chatTransport. the session resumes as is, so a short
    // switch to LAN does not sign the user out. built here, resumed later.
    auto *matrixManager = new koutnet::MatrixManager(appSettings, &app);
    auto *matrixRooms = new koutnet::MatrixRoomBridge(matrixManager, &app);
    auto *matrixVerification = new koutnet::MatrixVerification(matrixManager, &app);

    // room calls go through the bridge over the LAN voice channel; both sides
    // exist already, so this is just the wiring
    matrixRooms->setCallStack(network, voice, crypto);

    // the one entry point for every chat action; the id picks the backend and
    // QML never sees a prefix. more backends register the same way.
    auto *chatTransport = new koutnet::ChatBackendRegistry(&app);
    chatTransport->registerBackend(network);
    chatTransport->registerBackend(matrixRooms);
    // The other three unified transports are scaffolded: registered so the
    // interface lists them and opens a preview page, with the real protocol
    // client grown in behind each later.
    chatTransport->registerBackend(new koutnet::RocketChatBackend(&app));
    chatTransport->registerBackend(new koutnet::TelegramBackend(&app));
    chatTransport->registerBackend(new koutnet::ToxBackend(&app));

    auto *audioDevices = new koutnet::AudioDevices(&app);
    // owns the notifications, so parent it to the app to outlive the windows
    auto *notifications = new koutnet::NotificationManager(&app);
    notifications->setAwayAfterMinutes(appSettings->awayAfterMinutes());
    QObject::connect(appSettings, &koutnet::AppSettings::awayAfterMinutesChanged, notifications, [notifications, appSettings]() {
        notifications->setAwayAfterMinutes(appSettings->awayAfterMinutes());
    });

    // only built when enabled; turning it off at runtime would strand a window
    koutnet::TrayIcon *tray = appSettings->trayEnabled() ? new koutnet::TrayIcon(&app) : nullptr;

    voice->setAudioInputDevice(appSettings->audioInputId());
    voice->setAudioOutputDevice(appSettings->audioOutputId());
    voice->setAudioVolume(appSettings->audioVolume() / 100.0);
    voice->setMute(appSettings->micMuted());
    voice->setVad(appSettings->vadEnabled());
    QObject::connect(appSettings, &koutnet::AppSettings::audioInputIdChanged, voice, [voice, appSettings]() {
        voice->setAudioInputDevice(appSettings->audioInputId());
    });
    QObject::connect(appSettings, &koutnet::AppSettings::audioOutputIdChanged, voice, [voice, appSettings]() {
        voice->setAudioOutputDevice(appSettings->audioOutputId());
    });
    QObject::connect(appSettings, &koutnet::AppSettings::audioVolumeChanged, voice, [voice, appSettings]() {
        voice->setAudioVolume(appSettings->audioVolume() / 100.0);
    });
    QObject::connect(appSettings, &koutnet::AppSettings::micMutedChanged, voice, [voice, appSettings]() {
        voice->setMute(appSettings->micMuted());
    });
    QObject::connect(appSettings, &koutnet::AppSettings::vadEnabledChanged, voice, [voice, appSettings]() {
        voice->setVad(appSettings->vadEnabled());
    });
    // while a call is live, new requests get the busy reply and only the asked
    // peers can complete one. the active set just mirrors VoiceCallManager.
    QObject::connect(voice, &koutnet::VoiceCallManager::activeCallsChanged, network, [network, voice]() {
        network->setActiveCalls(voice->activeCalls());
    });

    QObject::connect(network, &koutnet::NetworkManager::fileMeta, fileTransfer, &koutnet::FileTransferHandler::onMeta);
    QObject::connect(network, &koutnet::NetworkManager::fileChunkBytes, fileTransfer, &koutnet::FileTransferHandler::onChunkMessage);

    fileTransfer->setFileDecryptor([crypto](const QString &peerIp, const QByteArray &cipher) {
        QByteArray plain;
        if (!crypto->decryptFileBytes(peerIp, cipher, &plain))
            return QByteArray();
        return plain;
    });

    fileTransfer->setMaxTransferBytes(qint64(appSettings->maxTransferMb()) * 1024 * 1024);
    QObject::connect(appSettings, &koutnet::AppSettings::maxTransferMbChanged, fileTransfer, [fileTransfer, appSettings]() {
        fileTransfer->setMaxTransferBytes(qint64(appSettings->maxTransferMb()) * 1024 * 1024);
    });

    if (!network->start())
        qCWarning(KOUTNET_LOG_NETWORK, "failed to start network layer");

    // touching the manager here restores the saved colour scheme
    KColorSchemeManager::instance();

    // declared before the engine, which keeps a bare pointer to it
    MatrixNetworkAccessManagerFactory matrixNamFactory;

    QQmlApplicationEngine engine;

    // lets an mxc:// URI load in a plain Image or MediaPlayer; libQuotient rewrites
    // it to an authenticated request, which the engine manager does not know how to do
    engine.setNetworkAccessManagerFactory(&matrixNamFactory);

    // Gives QML the i18n family of functions; without it every string fails to resolve.
    KLocalization::setupLocalizedContext(&engine);

    // add our own import path; the engine only adds qrc:/qt/qml by itself
    engine.addImportPath(QStringLiteral(":/"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    engine.rootContext()->setContextProperty(QStringLiteral("cryptoManager"), crypto);
    engine.rootContext()->setContextProperty(QStringLiteral("networkManager"), network);
    engine.rootContext()->setContextProperty(QStringLiteral("chatTransport"), chatTransport);
    engine.rootContext()->setContextProperty(QStringLiteral("voiceCallManager"), voice);
    engine.rootContext()->setContextProperty(QStringLiteral("fileTransferHandler"), fileTransfer);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("matrixManager"), matrixManager);
    engine.rootContext()->setContextProperty(QStringLiteral("matrixRooms"), matrixRooms);
    engine.rootContext()->setContextProperty(QStringLiteral("matrixVerification"), matrixVerification);
    engine.rootContext()->setContextProperty(QStringLiteral("audioDevices"), audioDevices);
    engine.rootContext()->setContextProperty(QStringLiteral("notificationManager"), notifications);
    // null when the tray is off; Main.qml then falls back to a real close
    engine.rootContext()->setContextProperty(QStringLiteral("trayIcon"), tray);
    // flatten KAboutData into a map QML can read directly
    QVariantMap about;
    about[QStringLiteral("name")] = aboutData.displayName();
    about[QStringLiteral("version")] = aboutData.version();
    about[QStringLiteral("description")] = aboutData.shortDescription();
    about[QStringLiteral("copyright")] = aboutData.copyrightStatement();
    about[QStringLiteral("homepage")] = aboutData.homepage();
    if (!aboutData.licenses().isEmpty())
        about[QStringLiteral("license")] = aboutData.licenses().constFirst().name(KAboutLicense::FullName);
    if (!aboutData.authors().isEmpty())
        about[QStringLiteral("author")] = aboutData.authors().constFirst().name();
    engine.rootContext()->setContextProperty(QStringLiteral("aboutData"), about);

    engine.loadFromModule("koutnet.app", "Main");

    // HistoryManager is a singleton created with the window; grab it and load its settings
    if (auto *historyManager = engine.singletonInstance<HistoryManager *>(QStringLiteral("koutnet.app"), QStringLiteral("HistoryManager"))) {
        historyManager->setHistorySavingEnabled(appSettings->historySavingEnabled());
        QObject::connect(appSettings, &koutnet::AppSettings::historySavingEnabledChanged, historyManager, [historyManager, appSettings]() {
            historyManager->setHistorySavingEnabled(appSettings->historySavingEnabled());
        });
    }

    // resume after the window exists, so a failed session can report itself
    matrixManager->resumeSession();

    return app.exec();
}
