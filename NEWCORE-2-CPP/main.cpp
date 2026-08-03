// SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
// SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// KOutNet - application entry point
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

#include "core/security/CryptoManager.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"
#include "network/FileTransferHandler.h"
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include "core/constructor/AppSettings.h"
#include "core/audio/AudioDevices.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KOutNet"));
    app.setOrganizationName(QStringLiteral("KOutNet"));

    // Wayland reads the taskbar icon off the .desktop file it matches to the
    // window's app_id, which Qt takes from here. X11 ignores that and uses
    // the window icon hint below, so both get set.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.bitzuka.KOutNet"));
    // The QML module resources sit under the URI path directly rather than
    // below /qt/qml, since this build has not opted into the newer CMake
    // resource prefix policy. The isNull check is what caught the wrong path.
    const QIcon appIcon(QStringLiteral(":/koutnet/app/assets/koutnet_logo.png"));
    if (appIcon.isNull())
        qWarning("KOutNet: application icon missing from the QML module resources");
    app.setWindowIcon(appIcon);

    // Single shared CryptoManager instance - injected into every module that
    // needs it (NetworkManager, VoiceCallManager). Never create a second
    // instance elsewhere; identity keys and session state must stay
    // single-sourced. See core/security/CryptoManager.h.
    auto *appSettings = new koutnet::AppSettings(&app);

    auto *crypto = new koutnet::CryptoManager(&app);
    if (!crypto->isValid()) {
        qCritical("KOutNet: cryptographic identity failed to initialize — aborting startup");
        return 1;
    }
    auto *network = new koutnet::NetworkManager(crypto, &app);

    // Apply persisted connection settings before start() - see AppSettings.
    network->setRelayServer(appSettings->relayHost(), quint16(appSettings->relayPort()));
    network->setConnectionMode(
        static_cast<koutnet::NetworkManager::ConnectionMode>(appSettings->connectionMode()));
    auto *voice = new koutnet::VoiceCallManager(network, crypto, &app);
    auto *fileTransfer = new koutnet::FileTransferHandler(&app);
    // One digest over the whole profile, images included. Peers compare
    // it to decide whether anything changed; the files themselves are
    // not in presence, only this.
    const auto publishProfile = [network, appSettings]() {
        const QString material = appSettings->displayName() + QChar(0x1f)
            + appSettings->bio() + QChar(0x1f)
            + appSettings->avatarPath() + QChar(0x1f)
            + appSettings->bannerPath() + QChar(0x1f)
            + appSettings->profileBackgroundPath() + QChar(0x1f)
            + appSettings->nameBadgePath();
        const QString revision = QString::fromLatin1(
            QCryptographicHash::hash(material.toUtf8(),
                                     QCryptographicHash::Sha256).toHex().left(12));
        network->setProfile(appSettings->username(), appSettings->displayName(),
                            appSettings->bio(), revision);
    };
    publishProfile();
    network->setGroupPassphrase(appSettings->groupPassphrase());
    QObject::connect(appSettings, &koutnet::AppSettings::groupPassphraseChanged, network,
                     [network, appSettings]() {
                         network->setGroupPassphrase(appSettings->groupPassphrase());
                     });
    for (auto signal : { &koutnet::AppSettings::usernameChanged,
                         &koutnet::AppSettings::displayNameChanged,
                         &koutnet::AppSettings::bioChanged,
                         &koutnet::AppSettings::avatarPathChanged,
                         &koutnet::AppSettings::bannerPathChanged,
                         &koutnet::AppSettings::profileBackgroundPathChanged,
                         &koutnet::AppSettings::nameBadgePathChanged }) {
        QObject::connect(appSettings, signal, network, publishProfile);
    }

    auto *audioDevices = new koutnet::AudioDevices(&app);

    // Push the persisted audio choices into the engine before any call
    // can start, then keep them in sync as the settings dialog edits them.
    voice->setAudioInputDevice(appSettings->audioInputId());
    voice->setAudioOutputDevice(appSettings->audioOutputId());
    voice->setAudioVolume(appSettings->audioVolume() / 100.0);
    voice->setMute(appSettings->micMuted());
    voice->setVad(appSettings->vadEnabled());
    QObject::connect(appSettings, &koutnet::AppSettings::audioInputIdChanged, voice,
                     [voice, appSettings]() { voice->setAudioInputDevice(appSettings->audioInputId()); });
    QObject::connect(appSettings, &koutnet::AppSettings::audioOutputIdChanged, voice,
                     [voice, appSettings]() { voice->setAudioOutputDevice(appSettings->audioOutputId()); });
    QObject::connect(appSettings, &koutnet::AppSettings::audioVolumeChanged, voice,
                     [voice, appSettings]() { voice->setAudioVolume(appSettings->audioVolume() / 100.0); });
    QObject::connect(appSettings, &koutnet::AppSettings::micMutedChanged, voice,
                     [voice, appSettings]() { voice->setMute(appSettings->micMuted()); });
    QObject::connect(appSettings, &koutnet::AppSettings::vadEnabledChanged, voice,
                     [voice, appSettings]() { voice->setVad(appSettings->vadEnabled()); });

    QObject::connect(network, &koutnet::NetworkManager::fileMeta,
                     fileTransfer, &koutnet::FileTransferHandler::onMeta);
    QObject::connect(network, &koutnet::NetworkManager::fileChunk,
                     fileTransfer, &koutnet::FileTransferHandler::onChunkMessage);

    if (!network->start())
        qWarning("KOutNet: failed to start network layer");

    // Names the catalog ki18n looks for, so translated strings end up in
    // koutnet.mo rather than in whatever domain happens to be current.
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("koutnet"));

    QQmlApplicationEngine engine;

    // Gives QML the i18n family of functions. Without this the calls simply
    // are not there and every string in the interface fails to resolve.
    KLocalization::setupLocalizedContext(&engine);

    // The QML module sits under a resource prefix of its own rather than the
    // default qrc:/qt/qml, which is the only root the engine adds by itself.
    engine.addImportPath(QStringLiteral(":/"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.rootContext()->setContextProperty(QStringLiteral("cryptoManager"), crypto);
    engine.rootContext()->setContextProperty(QStringLiteral("networkManager"), network);
    engine.rootContext()->setContextProperty(QStringLiteral("voiceCallManager"), voice);
    engine.rootContext()->setContextProperty(QStringLiteral("fileTransferHandler"), fileTransfer);
    engine.rootContext()->setContextProperty(QStringLiteral("appSettings"), appSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("audioDevices"), audioDevices);

    engine.loadFromModule("koutnet.app", "Main");

    return app.exec();
}
