# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The classes under test are compiled into each test rather than into a
# shared library. The project builds a single executable, so there is no
# library to link against yet.
#
# Headless CI has no kwalletd, so SecretStore refuses to store anything and
# CryptoManager falls back to a keypair that lives only for the run. That is
# the path these tests exercise, and it is the one that must not quietly turn
# into writing secrets to a config file.
ecm_add_test(
    CryptoManagerTest.cpp
    ../core/security/CryptoManager.cpp
    ../core/security/SecretStore.cpp
    ${KOUTNET_CRYPTO_LOGGING_SRCS}
    TEST_NAME crypto-manager
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core OpenSSL::Crypto KF6::Wallet KF6::I18n
)

# CryptoManager.cpp includes its logging category header, which is generated
# into the parent directory's build tree and not into this one.
target_include_directories(crypto-manager PRIVATE ${KOUTNET_GENERATED_INCLUDE_DIR})
