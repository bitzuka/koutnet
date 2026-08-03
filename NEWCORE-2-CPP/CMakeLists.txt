# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Configured from the top level CMakeLists.txt, which owns project(), the ECM
# lookup and the KDE includes. This file only describes the application.

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Multimedia Network)
find_package(OpenSSL REQUIRED)
# Kirigami is not optional: every QML file in qml/ imports it for the page
# structure and the units, so a build without it fails at first window.
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS I18n Kirigami)

# The i18n() call in QML comes from the localized context object, which lives
# in a target of its own rather than in KF6::I18n.
if(NOT TARGET KF6::I18nQml)
    message(FATAL_ERROR "KF6::I18nQml is missing; QML strings cannot be translated without it")
endif()

qt_standard_project_setup(REQUIRES 6.4)

qt_add_executable(KOutNet
    main.cpp
    core/audio/AudioEngine.cpp
    core/audio/AudioEngine.h
    core/audio/AudioMixer.cpp
    core/audio/AudioMixer.h
    core/audio/AudioDevices.cpp
    core/audio/AudioDevices.h
    core/security/CryptoManager.cpp
    core/security/CryptoManager.h
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
    core/theme/ThemeManager.cpp
    core/theme/ThemeManager.h
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
        qml/tabs/NotesTab.qml
        qml/tabs/CallsTab.qml
        qml/tabs/PlayerTab.qml
        qml/tabs/WnsTab.qml
        qml/profile/your_prof/YourProfile.qml
        qml/profile/not_your_prof/OtherProfile.qml
        qml/call/OutgoingCallWindow.qml
        qml/call/IncomingCallDialog.qml
        qml/call/ActiveCallWindow.qml
        qml/welcome/WelcomeScreen.qml
    RESOURCES
        assets/koutnet_logo.png
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
    KF6::I18n
    KF6::I18nQml
    KF6::Kirigami
)

install(TARGETS KOutNet RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

ki18n_install(${CMAKE_CURRENT_SOURCE_DIR}/../po)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# The installed icon is named after the desktop entry, not after the file in
# assets/, because that is the name Icon= refers to.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/koutnet_logo.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps
        RENAME io.github.bitzuka.KOutNet.png)

set_target_properties(KOutNet PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_include_directories(KOutNet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/core/chat
)

# Tests are opt out so a plain build stays quick, but they are on by default
# because a test nobody runs is not a test.
option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    include(ECMAddTests)
    add_subdirectory(tests)
endif()
