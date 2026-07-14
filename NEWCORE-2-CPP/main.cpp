// KOutNet — application entry point
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDebug>

#include "core/security/CryptoManager.h"
#include "network/NetworkManager.h"
#include "network/VoiceCallManager.h"
#include "network/FileTransferHandler.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("KOutNet");
    app.setOrganizationName("KOutNet");

    // Single shared CryptoManager instance — injected into every module that
    // needs it (NetworkManager, VoiceCallManager). Never create a second
    // instance elsewhere; identity keys and session state must stay
    // single-sourced. See core/security/CryptoManager.h.
    auto *crypto = new koutnet::CryptoManager(&app);
    auto *network = new koutnet::NetworkManager(crypto, &app);
    auto *voice = new koutnet::VoiceCallManager(network, crypto, &app);
    auto *fileTransfer = new koutnet::FileTransferHandler(&app);

    QObject::connect(network, &koutnet::NetworkManager::fileMeta,
                     fileTransfer, &koutnet::FileTransferHandler::onMeta);
    // TODO: file_data (chunk) packets aren't routed to FileTransferHandler
    // yet — NetworkManager::dispatch only emits fileMeta for file_meta.
    // Needs a dedicated fileChunk signal wired to onChunkMessage().

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

    engine.loadFromModule("koutnet.app", "Main");

    return app.exec();
}
