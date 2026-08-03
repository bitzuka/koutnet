# SPDX-FileCopyrightText: 2026 bitzuka <bitzuka.koutnet@gmail.com>
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
#
# Top level build file. The sources live one directory down, but the project
# declaration belongs here so that the repository root is what gets configured,
# which is what packagers and the KDE CI both assume.
cmake_minimum_required(VERSION 3.21)
# Kept in step with the metainfo release and the welcome screen build label.
# The old 2.0 was inherited from the pre-port tree and matched nothing shipped.
project(KOutNet VERSION 0.1 LANGUAGES CXX)

set(QT_MIN_VERSION 6.4)
set(KF_MIN_VERSION 6.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# After the KDE includes on purpose: KDECompilerSettings picks a standard of
# its own, and this project wants 20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_subdirectory(NEWCORE-2-CPP)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
