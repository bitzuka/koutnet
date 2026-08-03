# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

# The classes under test are compiled into each test rather than into a
# shared library. The project builds a single executable, so there is no
# library to link against yet.
ecm_add_test(
    CryptoManagerTest.cpp
    ../core/security/CryptoManager.cpp
    TEST_NAME crypto-manager
    NAME_PREFIX koutnet-
    LINK_LIBRARIES Qt6::Test Qt6::Core OpenSSL::Crypto
)
