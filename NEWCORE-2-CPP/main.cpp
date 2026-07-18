// KOutNet — application entry point
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

#include "core/security/CryptoManager.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"
#include "network/FileTransferHandler.h"
#include "core/i18n/Translations.h"
#include "core/constructor/AppSettings.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("KOutNet");
    app.setOrganizationName("KOutNet");

    // Single shared CryptoManager instance — injected into every module that
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

    // Apply persisted connection settings before start() — see AppSettings.
    if (appSettings->vdsMode()) {
        network->setRelayServer(appSettings->relayHost(), quint16(appSettings->relayPort()));
        network->setConnectionMode(koutnet::NetworkManager::ConnectionMode::Vds);
    }
    auto *voice = new koutnet::VoiceCallManager(network, crypto, &app);
    auto *fileTransfer = new koutnet::FileTransferHandler(&app);
    auto *translations = new koutnet::Translations(&app);

    QObject::connect(network, &koutnet::NetworkManager::fileMeta,
                     fileTransfer, &koutnet::FileTransferHandler::onMeta);
    QObject::connect(network, &koutnet::NetworkManager::fileChunk,
                     fileTransfer, &koutnet::FileTransferHandler::onChunkMessage);

    if (!network->start())
        qWarning("KOutNet: failed to start network layer");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.rootContext()->setContextProperty("cryptoManager", crypto);
    engine.rootContext()->setContextProperty("networkManager", network);
    engine.rootContext()->setContextProperty("voiceCallManager", voice);
    engine.rootContext()->setContextProperty("fileTransferHandler", fileTransfer);
    engine.rootContext()->setContextProperty("Translations", translations);
    engine.rootContext()->setContextProperty("appSettings", appSettings);

    engine.loadFromModule("koutnet.app", "Main");

    return app.exec();
}
