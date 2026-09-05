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
# 6.8 and not 6.0 because Kirigami.Units.cornerRadius, which the timeline and
# the pickers round their corners with, was added in 6.8.
set(KF_MIN_VERSION 6.8)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(ECMInstallIcons)
include(ECMQtDeclareLoggingCategory)
include(ECMSetupVersion)
include(FeatureSummary)

# After the KDE includes on purpose: KDECompilerSettings picks a standard of
# its own, and this project wants 20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# The About dialog and the welcome screen read the version from this header, so
# project() above stays the only place it is written down.
ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KOUTNET
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/koutnet-version.h
)

# Anything deprecated before the minimum versions this project claims to
# support has no business compiling, or the claim is not worth much.
ecm_set_disabled_deprecation_versions(
    QT ${QT_MIN_VERSION}
    KF ${KF_MIN_VERSION}
)

add_subdirectory(src)

# Formatting is a commit-time concern rather than a review-time one. The hook
# only lands in a git checkout, so builds from a tarball are unaffected.
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
