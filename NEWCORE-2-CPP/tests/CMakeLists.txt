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
    TEST_NAME crypto-manager
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core OpenSSL::Crypto KF6::Wallet
)
