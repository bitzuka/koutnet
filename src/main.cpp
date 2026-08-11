// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - application entry point
// QApplication rather than QGuiApplication: KStatusNotifierItem takes its menu
// through setContextMenu(QMenu *), and a QWidget without a QApplication is a
// warning followed by a crash. The cost is a link against Qt6::Widgets.
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
// setDesktopFileName is a QGuiApplication static, called by class name below.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QVariantMap>

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
#include "core/chat/HistoryManager.h"
#include "core/backend/ChatBackendRegistry.h"
#include "core/constructor/AppSettings.h"
#include "core/notify/NotificationManager.h"
#include "core/tray/TrayIcon.h"
#include "matrix/MatrixManager.h"
#include "matrix/MatrixRoomBridge.h"
#include "matrix/MatrixVerification.h"

#include "koutnet-version.h"
#include "koutnet_app_debug.h"
#include "koutnet_crypto_debug.h"
#include "koutnet_network_debug.h"

namespace
{
// One per thread, which is what instance() already returns; the factory exists
// only because QQmlEngine will not ask libQuotient for it on its own.
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
    QApplication app(argc, argv);
    // The window is hidden rather than closed when it goes to the tray, and Qt would
    // otherwise take the last window going away as the end of the session.
    QApplication::setQuitOnLastWindowClosed(false);
    // Names the catalog ki18n looks for. It has to happen before anything asks for a
    // translated string, so nothing resolves against whatever domain was current.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));

    KAboutData aboutData(QStringLiteral("koutnet"),
                         i18nc("@title application name", "KOutNet"),
                         QStringLiteral(KOUTNET_VERSION_STRING),
                         i18nc("@info:whatsthis", "P2P encrypted messenger for LAN, VPN and relay"),
                         KAboutLicense::GPL_V3,
                         i18nc("@info:credit", "Copyright 2026 bitzuka"));
    aboutData.addAuthor(i18nc("@info:credit", "bitzuka"), i18nc("@info:credit", "Author and maintainer"), QStringLiteral("bitzuka.koutnet@gmail.com"));
    aboutData.setHomepage(QStringLiteral("https://github.com/bitzuka/koutnet"));
    // DrKonqi offers to file a report at this address after a crash, so it has to be
    // a tracker that exists. The metainfo still points at the GitHub tracker.
    aboutData.setBugAddress(QByteArrayLiteral("https://bugs.kde.org/enter_bug.cgi?product=koutnet"));
    aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));
    // Deliberately unrelated to applicationName below. This is only the AppStream
    // component id and the .desktop basename; making applicationName match it would
    // move QSettings off ~/.config/KOutNet/ and re-key the Olm pickle libQuotient
    // stores under qAppName() + "-Pickle", orphaning every megolm session and making
    // existing encrypted Matrix history permanently unreadable.
    aboutData.setDesktopFileName(QStringLiteral("org.kde.koutnet"));
    KAboutData::setApplicationData(aboutData);

    // After setApplicationData on purpose: that call takes the application name from
    // the component name, which would point QSettings at KOutNet/koutnet.conf and
    // leave the plaintext-key cleanup looking at a file nobody ever wrote.
    // "KOutNet" rather than the organisation domain because every installed copy
    // already keeps its settings in ~/.config/KOutNet/, where the KWallet migration
    // reads them from.
    app.setApplicationName(QStringLiteral("KOutNet"));
    app.setOrganizationName(QStringLiteral("KOutNet"));

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.process(app);
    aboutData.processCommandLine(&parser);

    // Wayland reads the taskbar icon off the .desktop file it matches to the window
    // app_id, which Qt takes from here; X11 uses the window icon hint below instead.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.kde.koutnet"));
    // The QML module resources sit under the URI path directly rather than below
    // /qt/qml, since this build has not opted into the newer CMake resource policy.
    const QIcon appIcon(QStringLiteral(":/koutnet/app/assets/512-apps-org.kde.koutnet.png"));
    if (appIcon.isNull())
        qCWarning(KOUTNET_LOG_APP, "application icon missing from the QML module resources");
    app.setWindowIcon(appIcon);

    // Single shared CryptoManager - injected into every module that needs it. Never
    // create a second instance elsewhere; identity keys and session state must stay
    // single-sourced. See core/security/CryptoManager.h.
    auto *crypto = new koutnet::CryptoManager(&app);
    if (!crypto->isValid()) {
        qCCritical(KOUTNET_LOG_CRYPTO, "cryptographic identity failed to initialize - aborting startup");
        return 1;
    }

    // Constructed after CryptoManager because that is the last thing to edit the
    // config file through QSettings while it clears out plaintext keys. AppSettings
    // writes the same file with KConfig, which encodes non-ASCII differently.
    auto *appSettings = new koutnet::AppSettings(&app);
    auto *network = new koutnet::NetworkManager(crypto, &app);

    network->setRelayServer(appSettings->relayHost(), quint16(appSettings->relayPort()));
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

    // Matrix lives outside NetworkManager: libQuotient has no place in the
    // datagram path. Both register with the chat transport registry, and the
    // registry alone decides which backend a chat id belongs to. QML only
    // ever talks to chatTransport. The session resumes whatever the current
    // mode is; switching back to LAN for an afternoon does not sign the user
    // out of their homeserver.
    //
    // Built here, resumed after the window exists - see below.
    auto *matrixManager = new koutnet::MatrixManager(appSettings, &app);
    auto *matrixRooms = new koutnet::MatrixRoomBridge(matrixManager, &app);
    auto *matrixVerification = new koutnet::MatrixVerification(matrixManager, &app);

    // The one door every chat action in the interface goes through: the chat
    // id decides which backend does the work, and QML never sees a prefix.
    // Registration order is the order canHandle() is asked in; the prefixes
    // are disjoint, so the two lines below are all a transport costs here.
    // A third and fourth backend (Rocket.Chat, Telegram) register the same way.
    auto *chatTransport = new koutnet::ChatBackendRegistry(&app);
    chatTransport->registerBackend(network);
    chatTransport->registerBackend(matrixRooms);

    auto *audioDevices = new koutnet::AudioDevices(&app);
    // Owns the KNotification objects, so it has to outlive every window that
    // can raise one; parented to the application for that reason.
    auto *notifications = new koutnet::NotificationManager(&app);
    notifications->setAwayAfterMinutes(appSettings->awayAfterMinutes());
    QObject::connect(appSettings, &koutnet::AppSettings::awayAfterMinutesChanged, notifications, [notifications, appSettings]() {
        notifications->setAwayAfterMinutes(appSettings->awayAfterMinutes());
    });

    // Built only when the setting asks for it, and never rebuilt: registering a status
    // notifier item is a D-Bus name claim, and switching it off at runtime would leave
    // a hidden window with no way back.
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
    // While any call is live the network layer answers new call requests with
    // the busy reply instead of ringing a second window, and only peers a call
    // was actually asked of can complete one. The active set is a pure mirror
    // of VoiceCallManager's, which is the single place a call becomes real.
    QObject::connect(voice, &koutnet::VoiceCallManager::activeCallsChanged, network, [network, voice]() {
        network->setActiveCalls(voice->activeCalls());
    });

    QObject::connect(network, &koutnet::NetworkManager::fileMeta, fileTransfer, &koutnet::FileTransferHandler::onMeta);
    QObject::connect(network, &koutnet::NetworkManager::fileChunk, fileTransfer, &koutnet::FileTransferHandler::onChunkMessage);

    fileTransfer->setMaxTransferBytes(qint64(appSettings->maxTransferMb()) * 1024 * 1024);
    QObject::connect(appSettings, &koutnet::AppSettings::maxTransferMbChanged, fileTransfer, [fileTransfer, appSettings]() {
        fileTransfer->setMaxTransferBytes(qint64(appSettings->maxTransferMb()) * 1024 * 1024);
    });

    if (!network->start())
        qCWarning(KOUTNET_LOG_NETWORK, "failed to start network layer");

    // Touching the manager here is what restores the dark/light choice from the last
    // run: it does that in its own constructor, and the QML singleton that exposes the
    // setting is only built when the settings page is first opened.
    KColorSchemeManager::instance();

    // Declared before the engine so that it is still alive while the engine is
    // being torn down: the engine keeps a bare pointer to it.
    MatrixNetworkAccessManagerFactory matrixNamFactory;

    QQmlApplicationEngine engine;

    // What makes an mxc:// URI loadable by an ordinary Image or MediaPlayer.
    // libQuotient's network manager rewrites mxc into an authenticated request
    // against the homeserver the URI's user_id names; the engine's own manager
    // does not know the scheme and would report every room picture and every
    // attachment as an unsupported URL. NeoChat installs the same factory for
    // the same reason - the shape of the class is Qt's, not theirs.
    engine.setNetworkAccessManagerFactory(&matrixNamFactory);

    // Gives QML the i18n family of functions; without it every string fails to resolve.
    KLocalization::setupLocalizedContext(&engine);

    // The QML module sits under a resource prefix of its own rather than the
    // default qrc:/qt/qml, which is the only root the engine adds by itself.
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
    // Null when the tray is switched off. Main.qml checks for that, which is also what
    // makes the close-to-tray path fall back to really closing.
    engine.rootContext()->setContextProperty(QStringLiteral("trayIcon"), tray);
    // A flat map rather than KAboutData itself: the licence and author sit behind lists
    // QML would have to index by hand.
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

    // HistoryManager is a QML singleton, so the instance only exists once the
    // window component has loaded (Main.qml binds it into ChatListModel and the
    // dynamic chat models). Fetch it now and feed it the persisted value.
    if (auto *historyManager = engine.singletonInstance<HistoryManager *>(QStringLiteral("koutnet.app"), QStringLiteral("HistoryManager"))) {
        historyManager->setHistorySavingEnabled(appSettings->historySavingEnabled());
        QObject::connect(appSettings, &koutnet::AppSettings::historySavingEnabledChanged, historyManager, [historyManager, appSettings]() {
            historyManager->setHistorySavingEnabled(appSettings->historySavingEnabled());
        });
    }

    // After the window, not before it. A stored session that cannot be reopened
    // reports itself the moment it is tried, and resuming first meant nothing
    // was connected to hear it - the start was silent and the interface sat in
    // whatever state the failure had left behind.
    matrixManager->resumeSession();

    return app.exec();
}
