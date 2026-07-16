/****************************************************************************
** Meta object code from reading C++ file 'NetworkManager.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../network/NetworkManager.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'NetworkManager.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN7koutnet14NetworkManagerE_t {};
} // unnamed namespace

template <> constexpr inline auto koutnet::NetworkManager::qt_create_metaobjectdata<qt_meta_tag_ZN7koutnet14NetworkManagerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "koutnet::NetworkManager",
        "userOnline",
        "",
        "QJsonObject",
        "peerInfo",
        "userOffline",
        "ip",
        "message",
        "msg",
        "callRequest",
        "username",
        "callAccepted",
        "callRejected",
        "callEnded",
        "voiceData",
        "raw",
        "voiceDataFrom",
        "fileMeta",
        "meta",
        "fileChunk",
        "chunk",
        "groupInvite",
        "groupId",
        "name",
        "fromIp",
        "errorOccurred",
        "typing",
        "chatId",
        "voiceConnected",
        "voiceDisconnected",
        "onUdpReadyRead",
        "onNewTcpConnection",
        "onBroadcastTimer",
        "refreshLocalIps",
        "scanArpTable",
        "isRunning",
        "hostIp",
        "sendChat",
        "text",
        "sendPrivate",
        "toIp",
        "sendTyping",
        "targetIp",
        "sendCallRequest",
        "sendCallAccept",
        "sendCallReject",
        "sendCallEnd",
        "sendFile",
        "filePath"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'userOnline'
        QtMocHelpers::SignalData<void(QJsonObject)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'userOffline'
        QtMocHelpers::SignalData<void(QString)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'message'
        QtMocHelpers::SignalData<void(QJsonObject)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 8 },
        }}),
        // Signal 'callRequest'
        QtMocHelpers::SignalData<void(QString, QString)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'callAccepted'
        QtMocHelpers::SignalData<void(QString, QString)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'callRejected'
        QtMocHelpers::SignalData<void(QString)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'callEnded'
        QtMocHelpers::SignalData<void(QString)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'voiceData'
        QtMocHelpers::SignalData<void(QByteArray)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QByteArray, 15 },
        }}),
        // Signal 'voiceDataFrom'
        QtMocHelpers::SignalData<void(QString, QByteArray)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 }, { QMetaType::QByteArray, 15 },
        }}),
        // Signal 'fileMeta'
        QtMocHelpers::SignalData<void(QJsonObject)>(17, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 18 },
        }}),
        // Signal 'fileChunk'
        QtMocHelpers::SignalData<void(QJsonObject)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 20 },
        }}),
        // Signal 'groupInvite'
        QtMocHelpers::SignalData<void(QString, QString, QString)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 22 }, { QMetaType::QString, 23 }, { QMetaType::QString, 24 },
        }}),
        // Signal 'errorOccurred'
        QtMocHelpers::SignalData<void(QString)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Signal 'typing'
        QtMocHelpers::SignalData<void(QString, QString)>(26, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 }, { QMetaType::QString, 27 },
        }}),
        // Signal 'voiceConnected'
        QtMocHelpers::SignalData<void(QString)>(28, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'voiceDisconnected'
        QtMocHelpers::SignalData<void(QString)>(29, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Slot 'onUdpReadyRead'
        QtMocHelpers::SlotData<void()>(30, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onNewTcpConnection'
        QtMocHelpers::SlotData<void()>(31, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBroadcastTimer'
        QtMocHelpers::SlotData<void()>(32, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'refreshLocalIps'
        QtMocHelpers::SlotData<void()>(33, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'scanArpTable'
        QtMocHelpers::SlotData<void()>(34, 2, QMC::AccessPrivate, QMetaType::Void),
        // Method 'isRunning'
        QtMocHelpers::MethodData<bool() const>(35, 2, QMC::AccessPublic, QMetaType::Bool),
        // Method 'hostIp'
        QtMocHelpers::MethodData<QString() const>(36, 2, QMC::AccessPublic, QMetaType::QString),
        // Method 'sendChat'
        QtMocHelpers::MethodData<void(const QString &)>(37, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 38 },
        }}),
        // Method 'sendPrivate'
        QtMocHelpers::MethodData<void(const QString &, const QString &)>(39, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 38 }, { QMetaType::QString, 40 },
        }}),
        // Method 'sendTyping'
        QtMocHelpers::MethodData<void(const QString &, const QString &)>(41, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 27 }, { QMetaType::QString, 42 },
        }}),
        // Method 'sendTyping'
        QtMocHelpers::MethodData<void(const QString &)>(41, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { QMetaType::QString, 27 },
        }}),
        // Method 'sendCallRequest'
        QtMocHelpers::MethodData<void(const QString &)>(43, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 40 },
        }}),
        // Method 'sendCallAccept'
        QtMocHelpers::MethodData<void(const QString &)>(44, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 40 },
        }}),
        // Method 'sendCallReject'
        QtMocHelpers::MethodData<void(const QString &)>(45, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 40 },
        }}),
        // Method 'sendCallEnd'
        QtMocHelpers::MethodData<void(const QString &)>(46, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 40 },
        }}),
        // Method 'sendFile'
        QtMocHelpers::MethodData<void(const QString &, const QString &)>(47, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 40 }, { QMetaType::QString, 48 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<NetworkManager, qt_meta_tag_ZN7koutnet14NetworkManagerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject koutnet::NetworkManager::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7koutnet14NetworkManagerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7koutnet14NetworkManagerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN7koutnet14NetworkManagerE_t>.metaTypes,
    nullptr
} };

void koutnet::NetworkManager::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<NetworkManager *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->userOnline((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 1: _t->userOffline((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->message((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 3: _t->callRequest((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 4: _t->callAccepted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 5: _t->callRejected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->callEnded((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->voiceData((*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[1]))); break;
        case 8: _t->voiceDataFrom((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QByteArray>>(_a[2]))); break;
        case 9: _t->fileMeta((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 10: _t->fileChunk((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1]))); break;
        case 11: _t->groupInvite((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 12: _t->errorOccurred((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 13: _t->typing((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 14: _t->voiceConnected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 15: _t->voiceDisconnected((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->onUdpReadyRead(); break;
        case 17: _t->onNewTcpConnection(); break;
        case 18: _t->onBroadcastTimer(); break;
        case 19: _t->refreshLocalIps(); break;
        case 20: _t->scanArpTable(); break;
        case 21: { bool _r = _t->isRunning();
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 22: { QString _r = _t->hostIp();
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 23: _t->sendChat((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 24: _t->sendPrivate((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 25: _t->sendTyping((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 26: _t->sendTyping((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 27: _t->sendCallRequest((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 28: _t->sendCallAccept((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 29: _t->sendCallReject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 30: _t->sendCallEnd((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 31: _t->sendFile((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QJsonObject )>(_a, &NetworkManager::userOnline, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::userOffline, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QJsonObject )>(_a, &NetworkManager::message, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString , QString )>(_a, &NetworkManager::callRequest, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString , QString )>(_a, &NetworkManager::callAccepted, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::callRejected, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::callEnded, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QByteArray )>(_a, &NetworkManager::voiceData, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString , QByteArray )>(_a, &NetworkManager::voiceDataFrom, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QJsonObject )>(_a, &NetworkManager::fileMeta, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QJsonObject )>(_a, &NetworkManager::fileChunk, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString , QString , QString )>(_a, &NetworkManager::groupInvite, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::errorOccurred, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString , QString )>(_a, &NetworkManager::typing, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::voiceConnected, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (NetworkManager::*)(QString )>(_a, &NetworkManager::voiceDisconnected, 15))
            return;
    }
}

const QMetaObject *koutnet::NetworkManager::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *koutnet::NetworkManager::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN7koutnet14NetworkManagerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int koutnet::NetworkManager::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 32)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 32)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 32;
    }
    return _id;
}

// SIGNAL 0
void koutnet::NetworkManager::userOnline(QJsonObject _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void koutnet::NetworkManager::userOffline(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void koutnet::NetworkManager::message(QJsonObject _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void koutnet::NetworkManager::callRequest(QString _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2);
}

// SIGNAL 4
void koutnet::NetworkManager::callAccepted(QString _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1, _t2);
}

// SIGNAL 5
void koutnet::NetworkManager::callRejected(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void koutnet::NetworkManager::callEnded(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void koutnet::NetworkManager::voiceData(QByteArray _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void koutnet::NetworkManager::voiceDataFrom(QString _t1, QByteArray _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2);
}

// SIGNAL 9
void koutnet::NetworkManager::fileMeta(QJsonObject _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void koutnet::NetworkManager::fileChunk(QJsonObject _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void koutnet::NetworkManager::groupInvite(QString _t1, QString _t2, QString _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1, _t2, _t3);
}

// SIGNAL 12
void koutnet::NetworkManager::errorOccurred(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void koutnet::NetworkManager::typing(QString _t1, QString _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 13, nullptr, _t1, _t2);
}

// SIGNAL 14
void koutnet::NetworkManager::voiceConnected(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void koutnet::NetworkManager::voiceDisconnected(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}
QT_WARNING_POP
