# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The classes under test are compiled into each test rather than into a
# shared library. The project builds a single executable, so there is no
# library to link against yet.
#
# Every suite that links SecretStore.cpp puts it in its in-memory mode in
# main(). There is one KWallet per session and QStandardPaths test mode does not
# move it, so until that call these tests wrote their throwaway identity keys
# into whatever keyring the developer running them had open, and left them
# there. It also makes a run here and a run on headless CI take the same path,
# where before they differed for want of a kwalletd. What must never happen
# either way is a secret quietly landing in a config file instead.
# tools/wallet-cleanup.sh clears out what an older build left behind.
#
# The application dropped OpenSSL when CryptoManager moved to libsodium, but
# CryptoManagerTest's ForgedPeer still builds its attacker keys with raw EVP, so
# the dependency lives on here alone until that helper is ported too.
find_package(OpenSSL REQUIRED)

ecm_add_test(
    CryptoManagerTest.cpp
    ../core/security/CryptoManager.cpp
    ../core/security/SecretStore.cpp
    ${KOUTNET_CRYPTO_LOGGING_SRCS}
    TEST_NAME crypto-manager
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core PkgConfig::Sodium OpenSSL::Crypto KF6::Wallet KF6::I18n
)

# CryptoManager.cpp includes its logging category header, which is generated
# into the parent directory's build tree and not into this one.
target_include_directories(crypto-manager PRIVATE ${KOUTNET_GENERATED_INCLUDE_DIR})

# No sockets bound, no peers contacted and no wallet: the packet path is driven
# by handing handleDatagram() the bytes a datagram would have carried, so this
# runs on a machine with no network at all. CryptoManager comes along because a
# session key is what the signature policy is made of. The full logging set is
# linked rather than just the crypto category because NetworkManager.cpp now
# speaks too.
ecm_add_test(
    NetworkManagerTest.cpp
    ../network/NetworkManager.cpp
    ../core/chat/ChatAddress.cpp
    ../core/security/CryptoManager.cpp
    ../core/security/SecretStore.cpp
    ${KOUTNET_LOGGING_SRCS}
    ${KOUTNET_CRYPTO_LOGGING_SRCS}
    TEST_NAME network-manager
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core Qt6::Network PkgConfig::Sodium KF6::Wallet KF6::I18n
)
target_include_directories(network-manager PRIVATE ${KOUTNET_GENERATED_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)

# This one writes real files, so its main() points HOME at a scratch directory
# before anything else runs: the download location is the one path
# QStandardPaths test mode does not move out of the user's home.
ecm_add_test(
    FileTransferHandlerTest.cpp
    ../network/FileTransferHandler.cpp
    TEST_NAME file-transfer-handler
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core KF6::I18n
)

# The reversing proxy is arithmetic over model signals with no window anywhere
# near it, so it is the one part of the timeline that can be tested at all - and
# an off-by-one in it is a conversation whose messages are all signed by the
# wrong person, which nothing reports. Qt6::Qml is here for the QML_ELEMENT
# marker in the header and QAbstractItemModelTester comes from Qt6::Test.
ecm_add_test(
    ReversedChatModelTest.cpp
    ../core/chat/ReversedChatModel.cpp
    TEST_NAME reversed-chat-model
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core Qt6::Qml
)

# The registry is pure routing over the chat-id prefix table - no sockets, no
# libQuotient, no wallet - so the fakes are ten-line classes and the test runs
# on a machine with no network at all.
ecm_add_test(
    ChatBackendRegistryTest.cpp
    ../core/chat/ChatAddress.cpp
    ../core/backend/ChatBackendRegistry.cpp
    TEST_NAME chat-backend-registry
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core
)
target_include_directories(chat-backend-registry PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

# The Matrix path with no homeserver, no network and - deliberately - no
# libQuotient linked in: MatrixTranslate.cpp is kept free of Quotient types
# exactly so the decisions worth checking can be checked here. What is left is
# the addressing scheme and the file naming it has to survive, which is where a
# mistake quietly files a conversation under the wrong name or merges two of
# them into one log. MatrixManager and MatrixRoomBridge are absent because
# everything they do is a round trip to a server.
#
# Qt6::Qml is for the QML_ELEMENT marker in HistoryManager's header.
ecm_add_test(
    MatrixWiringTest.cpp
    ../core/chat/ChatAddress.cpp
    ../core/chat/HistoryManager.cpp
    ../matrix/MatrixTranslate.cpp
    ${KOUTNET_LOGGING_SRCS}
    TEST_NAME matrix-wiring
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core Qt6::Qml KF6::I18n
)
# HistoryManager.cpp includes its logging category header, which is generated
# into the parent directory's build tree and not into this one.
target_include_directories(matrix-wiring PRIVATE ${KOUTNET_GENERATED_INCLUDE_DIR})
