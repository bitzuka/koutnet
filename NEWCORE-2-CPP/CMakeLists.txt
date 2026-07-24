cmake_minimum_required(VERSION 3.21)
project(KOutNet VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Multimedia Network)
find_package(OpenSSL REQUIRED)

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(KOutNet
    main.cpp
    core/audio/AudioEngine.cpp
    core/audio/AudioEngine.h
    core/audio/AudioMixer.cpp
    core/audio/AudioMixer.h
    core/security/CryptoManager.cpp
    core/security/CryptoManager.h
    core/i18n/Translations.cpp
    core/i18n/Translations.h
    core/constructor/AppSettings.cpp
    core/constructor/AppSettings.h
    core/chat/MessageEntry.cpp
    core/chat/MessageEntry.h
    core/chat/HistoryManager.cpp
    core/chat/HistoryManager.h
    core/chat/UnreadManager.cpp
    core/chat/UnreadManager.h
    core/chat/ReactionStore.cpp
    core/chat/ReactionStore.h
    core/chat/GroupManager.cpp
    core/chat/GroupManager.h
    core/chat/ChatModel.cpp
    core/chat/ChatModel.h
    network/NetworkManager.cpp
    network/NetworkManager.h
    network/Protocol.h
    network/VoiceCallManager.cpp
    network/VoiceCallManager.h
    network/FileTransferHandler.cpp
    network/FileTransferHandler.h
)

qt_add_qml_module(KOutNet
    URI koutnet.app
    VERSION 1.0
    QML_FILES
        qml/main_window/Main.qml
        qml/main_window/ContactDelegate.qml
        qml/main_window/ChatPage.qml
        qml/components/SplashScreen.qml
)

target_link_libraries(KOutNet PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Multimedia
    Qt6::Network
    OpenSSL::Crypto
)

set_target_properties(KOutNet PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_include_directories(KOutNet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/chat
)
