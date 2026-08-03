# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Configured from the top level CMakeLists.txt, which owns project(), the ECM
# lookup and the KDE includes. This file only describes the application.

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Multimedia Network)
find_package(OpenSSL REQUIRED)
# Kirigami is not optional: every QML file in qml/ imports it for the page
# structure and the units, so a build without it fails at first window.
# Wallet is required, not optional: the identity keys and the group passphrase
# have nowhere else to go, and the app refuses to write them in plain text.
# Config carries the generated settings class, CoreAddons the about data that
# DrKonqi and --version read.
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n Kirigami Wallet)

# The i18n() call in QML comes from the localized context object, which lives
# in a target of its own rather than in KF6::I18n.
if(NOT TARGET KF6::I18nQml)
    message(FATAL_ERROR "KF6::I18nQml is missing; QML strings cannot be translated without it")
endif()

qt_standard_project_setup(REQUIRES 6.4)

# One category per subsystem, so a bug report can be asked for
# QT_LOGGING_RULES="koutnet.network.debug=true" instead of the whole log.
# Crypto is declared into a variable of its own because the test suite compiles
# CryptoManager.cpp without the rest of the application.
ecm_qt_declare_logging_category(KOUTNET_CRYPTO_LOGGING_SRCS
    HEADER koutnet_crypto_debug.h
    IDENTIFIER KOUTNET_LOG_CRYPTO
    CATEGORY_NAME koutnet.crypto
    DESCRIPTION "KOutNet identity keys, encryption and secret storage"
    EXPORT KOUTNET
)
ecm_qt_declare_logging_category(KOUTNET_LOGGING_SRCS
    HEADER koutnet_network_debug.h
    IDENTIFIER KOUTNET_LOG_NETWORK
    CATEGORY_NAME koutnet.network
    DESCRIPTION "KOutNet peer discovery, transport and file transfer"
    EXPORT KOUTNET
)
ecm_qt_declare_logging_category(KOUTNET_LOGGING_SRCS
    HEADER koutnet_chat_debug.h
    IDENTIFIER KOUTNET_LOG_CHAT
    CATEGORY_NAME koutnet.chat
    DESCRIPTION "KOutNet chat history, groups and reactions"
    EXPORT KOUTNET
)
ecm_qt_declare_logging_category(KOUTNET_LOGGING_SRCS
    HEADER koutnet_audio_debug.h
    IDENTIFIER KOUTNET_LOG_AUDIO
    CATEGORY_NAME koutnet.audio
    DESCRIPTION "KOutNet audio devices, capture and playback"
    EXPORT KOUTNET
)
ecm_qt_declare_logging_category(KOUTNET_LOGGING_SRCS
    HEADER koutnet_app_debug.h
    IDENTIFIER KOUTNET_LOG_APP
    CATEGORY_NAME koutnet.app
    DESCRIPTION "KOutNet startup, settings and theming"
    EXPORT KOUTNET
)

qt_add_executable(KOutNet
    main.cpp
    ${KOUTNET_LOGGING_SRCS}
    ${KOUTNET_CRYPTO_LOGGING_SRCS}
    core/audio/AudioEngine.cpp
    core/audio/AudioEngine.h
    core/audio/AudioMixer.cpp
    core/audio/AudioMixer.h
    core/audio/AudioDevices.cpp
    core/audio/AudioDevices.h
    core/security/CryptoManager.cpp
    core/security/CryptoManager.h
    core/security/SecretStore.cpp
    core/security/SecretStore.h
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
        assets/512-apps-io.github.bitzuka.KOutNet.png
)

# The settings class is generated from the kcfg rather than written by hand: the
# hand-written one was twenty properties of identical setter, each opening a
# QSettings of its own.
kconfig_add_kcfg_files(KOutNet GENERATE_MOC core/constructor/koutnetsettings.kcfgc)

target_link_libraries(KOutNet PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Multimedia
    Qt6::Network
    OpenSSL::Crypto
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::Kirigami
    KF6::Wallet
)

install(TARGETS KOutNet ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

ki18n_install(${CMAKE_CURRENT_SOURCE_DIR}/../po)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
# The file name carries the size and the theme directory it belongs in, which is
# what ecm_install_icons reads it out of. It is still a single 512px bitmap: a
# scalable icon is what this actually wants, and drawing one is its own job.
ecm_install_icons(
    ICONS ${CMAKE_CURRENT_SOURCE_DIR}/assets/512-apps-io.github.bitzuka.KOutNet.png
    DESTINATION ${KDE_INSTALL_ICONDIR}
)
# Lets a bug reporter find the category names without reading the source.
ecm_qt_install_logging_categories(
    EXPORT KOUTNET
    FILE koutnet.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
)

set_target_properties(KOutNet PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

# The version header comes from ecm_setup_version at the top level, the logging
# categories and the generated settings class from this directory's build tree.
set(KOUTNET_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(KOutNet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/core/chat
    ${KOUTNET_GENERATED_INCLUDE_DIR}
    ${PROJECT_BINARY_DIR}
)

# Tests are opt out so a plain build stays quick, but they are on by default
# because a test nobody runs is not a test.
option(BUILD_TESTING "Build the test suite" ON)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    include(ECMAddTests)
    add_subdirectory(tests)
endif()
