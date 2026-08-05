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
# Notifications is not optional either: a messenger that only tells you about a
# message while you are already looking at it is not telling you anything. It
# also owns the installed koutnet.notifyrc, which is what puts the per-event
# switches in System Settings rather than in a dialog of this application's.
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n Kirigami Notifications Wallet)

# The i18n() call in QML comes from the localized context object, which lives
# in a target of its own rather than in KF6::I18n.
if(NOT TARGET KF6::I18nQml)
    message(FATAL_ERROR "KF6::I18nQml is missing; QML strings cannot be translated without it")
endif()

# Dark, light and follow-the-desktop are KColorSchemeManager's job. Asked for
# separately from the block above because the three calls used are instance(),
# indexForSchemeId() and activateScheme(), and the first two only exist from 6.6
# - a 6.0 that satisfied KF_MIN_VERSION would configure and then fail to
# compile.
find_package(KF6ColorScheme 6.6 CONFIG)
set_package_properties(KF6ColorScheme PROPERTIES
    TYPE REQUIRED
    PURPOSE "Dark, light and follow-the-system colour schemes"
)
if(NOT KF6ColorScheme_FOUND)
    message(FATAL_ERROR
        "KF6ColorScheme 6.6 or newer was not found. It ships with KDE Frameworks 6 "
        "(package names: kcolorscheme, kf6-kcolorscheme, libkf6colorscheme-dev).")
endif()

# kirigami-addons carries components this interface is built out of and has no
# fallback for: FormCard for the settings and about pages, Delegates for the
# conversation rows, AlbumMaximizeComponent for the image viewer and
# ConvergentContextMenu for the per-message menu. Those are QML modules, so a
# missing one surfaces as a runtime error in whichever window needs it first,
# which is a miserable way to find out. Hence the explicit check here.
#
# 1.8.0 rather than any version: ConvergentContextMenu does not exist before it.
find_package(KF6KirigamiAddons 1.8.0 CONFIG)
set_package_properties(KF6KirigamiAddons PROPERTIES
    TYPE REQUIRED
    PURPOSE "FormCard pages, list delegates, the image viewer and the convergent context menu"
)
if(NOT KF6KirigamiAddons_FOUND)
    message(FATAL_ERROR
        "KF6KirigamiAddons 1.8.0 or newer was not found. Install kirigami-addons "
        "(package names: kirigami-addons, kf6-kirigami-addons, libkf6kirigamiaddons-dev) "
        "and configure again.")
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
    core/chat/ChatListModel.cpp
    core/chat/ChatListModel.h
    core/chat/RelativeTime.cpp
    core/chat/RelativeTime.h
    core/chat/TextHandler.cpp
    core/chat/TextHandler.h
    core/emoji/EmojiModel.cpp
    core/emoji/EmojiModel.h
    core/emoji/EmojiTones.cpp
    core/emoji/EmojiTones.h
    core/notify/NotificationManager.cpp
    core/notify/NotificationManager.h
    core/theme/ColorSchemeSelector.cpp
    core/theme/ColorSchemeSelector.h
    network/NetworkManager.cpp
    network/NetworkManager.h
    network/Protocol.h
    network/VoiceCallManager.cpp
    network/VoiceCallManager.h
    network/FileTransferHandler.cpp
    network/FileTransferHandler.h
)

# The one brand colour is a QML singleton rather than a C++ one: it holds a
# single constant and nothing outside QML ever reads it. Has to be marked before
# qt_add_qml_module, which is where the type registration is generated.
set_source_files_properties(qml/Brand.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(KOutNet
    URI koutnet.app
    VERSION 1.0
    QML_FILES
        qml/Brand.qml
        qml/main_window/Main.qml
        qml/main_window/ChatListPage.qml
        qml/main_window/ChatSection.qml
        qml/main_window/ContactDelegate.qml
        qml/main_window/AccountRow.qml
        qml/main_window/ChatPage.qml
        qml/main_window/NewChatPage.qml
        qml/shell/ConnectionRail.qml
        qml/shell/ModeButton.qml
        qml/timeline/MessageTimeline.qml
        qml/timeline/MessageDelegate.qml
        qml/timeline/MessageActions.qml
        qml/timeline/DateSeparator.qml
        qml/timeline/SystemMessage.qml
        qml/timeline/ReplyQuote.qml
        qml/timeline/ReactionFlow.qml
        qml/timeline/AttachmentBlock.qml
        qml/timeline/DeliveryMark.qml
        qml/timeline/TypingIndicator.qml
        qml/composer/Composer.qml
        qml/composer/EmojiPicker.qml
        qml/composer/EmojiPopup.qml
        qml/composer/EmojiGrid.qml
        qml/composer/EmojiDelegate.qml
        qml/composer/EmojiTonesPicker.qml
        qml/composer/ReactionPicker.qml
        qml/peer/PeerInfoPage.qml
        qml/settings/SettingsPage.qml
        qml/settings/AboutPage.qml
        qml/tabs/NotesPage.qml
        qml/tabs/CallLogPage.qml
        qml/tabs/PlayerPage.qml
        qml/tabs/KeenlyPage.qml
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
    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::Kirigami
    KF6::Notifications
    KF6::Wallet
)

install(TARGETS KOutNet ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

ki18n_install(${CMAKE_CURRENT_SOURCE_DIR}/../po)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/io.github.bitzuka.KOutNet.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
# Named after the KAboutData component name rather than the application name,
# because that is what NotificationManager sets on every KNotification it makes.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/koutnet.notifyrc
        DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
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
    ${CMAKE_CURRENT_SOURCE_DIR}/core/emoji
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
