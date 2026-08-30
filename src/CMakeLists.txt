# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
# Configured from the top level CMakeLists.txt, which owns project(), the ECM
# lookup and the KDE includes. This file only describes the application.

# Widgets is here for one reason: KStatusNotifierItem::setContextMenu() takes a
# QMenu, so the tray menu is a widget and the application has to be a
# QApplication. See the note at the top of main.cpp. Nothing else in the tree
# includes a widget header.
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2 Multimedia Network Positioning Widgets)
# libsodium ships no CMake config of its own, so it arrives through pkg-config.
# 1.0.18 is the floor: crypto_kx and the XChaCha20-Poly1305 AEAD that
# CryptoManager is built on have been stable since then.
find_package(PkgConfig REQUIRED)
pkg_check_modules(Sodium REQUIRED IMPORTED_TARGET libsodium>=1.0.18)
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
# StatusNotifierItem is the tray icon. It is a framework of its own rather than
# part of KNotifications, and has been since before the 6.8 floor this project
# claims, so it belongs in this list rather than in a version check of its own.
# Not optional: with it missing the close button has nowhere to minimise to, and
# a half-built tray is worse than none. Package names: kstatusnotifieritem,
# kf6-kstatusnotifieritem, libkf6statusnotifieritem-dev.
# IdleTime is what tells the notifications the difference between "the window is
# behind something" and "nobody is there", which is the whole of the sound
# behaviour. Package names: kidletime, kf6-kidletime, libkf6idletime-dev.
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config CoreAddons I18n IdleTime Kirigami Notifications StatusNotifierItem Wallet)

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

# K-Server mode is Matrix, and libQuotient is the Qt SDK for it - the same one
# NeoChat is built on, so a room here and a room there are the same room and
# neither this project nor its author has to invent federation. Required rather
# than optional: a mode present in the interface with nothing behind it is
# exactly the state this replaces.
#
# 0.9 rather than any version: Connection::assumeIdentity(), which is what
# resumes a stored session without asking for the password again, arrived in
# 0.7.2, and Room::postText() deprecated postPlainText() in 0.9. The package is
# named after the Qt major version it was built against, which is also why there
# is no Qt 5 fallback here.
#
# On the C++ standard: libQuotient publishes cxx_std_23 as an interface
# requirement, so linking it raises this target past the 20 set at the top
# level. That is libQuotient's floor, not a choice made here.
find_package(QuotientQt6 0.9 CONFIG)
set_package_properties(QuotientQt6 PROPERTIES
    DESCRIPTION "Qt SDK for the Matrix protocol"
    URL "https://github.com/quotient-im/libQuotient"
    TYPE REQUIRED
    PURPOSE "K-Server mode: Matrix login, sync, room list and messages"
)
if(NOT QuotientQt6_FOUND)
    message(FATAL_ERROR
        "libQuotient 0.9 or newer was not found. It is what K-Server mode is made of. "
        "Install it (package names: net-libs/libquotient on Gentoo, libquotient, "
        "libquotient-devel, libQuotient-dev) and configure again. It brings libolm, "
        "qtkeychain built for Qt 6 and Qt6::Sql with it; those are its dependencies "
        "rather than this project's.")
endif()

qt_standard_project_setup(REQUIRES 6.4)

# The code speaks the QT_NO_KEYWORDS dialect throughout - Q_EMIT, Q_SIGNALS and
# Q_SLOTS, with signal/slot as plain identifiers. qt_standard_project_setup
# already turns the cast and foreach hazards off; this is the last member of
# that family, and turning it on is what keeps a new file from drifting into
# the keywords version and compiling only by accident.
add_compile_definitions(QT_NO_KEYWORDS)

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
    HEADER koutnet_matrix_debug.h
    IDENTIFIER KOUTNET_LOG_MATRIX
    CATEGORY_NAME koutnet.matrix
    DESCRIPTION "KOutNet K-Server mode: the Matrix session, rooms and sync"
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
    core/chat/ChatAddress.cpp
    core/chat/ChatAddress.h
    core/backend/ChatBackend.h
    core/backend/ChatBackendRegistry.cpp
    core/backend/ChatBackendRegistry.h
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
    core/chat/ReversedChatModel.cpp
    core/chat/ReversedChatModel.h
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
    core/tray/TrayIcon.cpp
    core/tray/TrayIcon.h
    core/theme/ColorSchemeSelector.cpp
    core/theme/ColorSchemeSelector.h
    network/NetworkManager.cpp
    network/NetworkManager.h
    network/Protocol.h
    network/Protocol.cpp
    network/VoiceCallManager.cpp
    network/VoiceCallManager.h
    network/FileTransferHandler.cpp
    network/FileTransferHandler.h
    matrix/MatrixTranslate.cpp
    matrix/MatrixTranslate.h
    matrix/MatrixManager.cpp
    matrix/MatrixManager.h
    matrix/MatrixRoomBridge.cpp
    matrix/MatrixRoomBridge.h
    matrix/pollevent.cpp
    matrix/pollevent.h
    matrix/MatrixVerification.cpp
    matrix/MatrixVerification.h
    matrix/MatrixLoginUtils.cpp
    matrix/MatrixLoginUtils.h
)

# The one brand colour is a QML singleton rather than a C++ one: it holds a
# single constant and nothing outside QML ever reads it. Has to be marked before
# qt_add_qml_module, which is where the type registration is generated.
set_source_files_properties(qml/Brand.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Two words and two icon names shared by the member list and the member card,
# which have to agree about what a power level of 50 is called.
set_source_files_properties(qml/matrix/RoomRoles.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
        qml/shell/AnimatedAvatar.qml
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
        qml/peer/PeerCard.qml
        qml/peer/PeerInfoPage.qml
        qml/settings/SettingsPage.qml
        qml/settings/AboutPage.qml
        qml/matrix/MatrixLoginPage.qml
        qml/matrix/RoomRoles.qml
        qml/matrix/RoomInfoPage.qml
        qml/matrix/InvitationsPage.qml
        qml/matrix/RoomMemberCard.qml
        qml/matrix/DeviceVerificationDialog.qml
        qml/matrix/KeyBackupDialog.qml
        qml/tabs/NotesPage.qml
        qml/tabs/CallLogPage.qml
        qml/tabs/PlayerPage.qml
        qml/profile/ProfileHeader.qml
        qml/profile/ProfileBlock.qml
        qml/profile/AccountCard.qml
        qml/profile/not_your_prof/OtherProfile.qml
        qml/call/OutgoingCallWindow.qml
        qml/call/IncomingCallDialog.qml
        qml/call/ActiveCallWindow.qml
        qml/welcome/WelcomeScreen.qml
    RESOURCES
        assets/512-apps-org.kde.koutnet.png
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
    Qt6::Positioning
    Qt6::Widgets
    PkgConfig::Sodium
    KF6::ColorScheme
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::IdleTime
    KF6::Kirigami
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::Wallet
    QuotientQt6
)

install(TARGETS KOutNet ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

ki18n_install(${CMAKE_CURRENT_SOURCE_DIR}/../po)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/org.kde.koutnet.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/org.kde.koutnet.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
# Named after the KAboutData component name rather than the application name,
# because that is what NotificationManager sets on every KNotification it makes.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../packaging/koutnet.notifyrc
        DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
# The file name carries the size and the theme directory it belongs in, which is
# what ecm_install_icons reads it out of. It is still a single 512px bitmap: a
# scalable icon is what this actually wants, and drawing one is its own job.
ecm_install_icons(
    ICONS ${CMAKE_CURRENT_SOURCE_DIR}/assets/512-apps-org.kde.koutnet.png
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
    # matrix/ reaches core/ the way main.cpp does, by path from the source root.
    # Without this a quoted include from a subdirectory resolves against that
    # subdirectory and nowhere else.
    ${CMAKE_CURRENT_SOURCE_DIR}
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
