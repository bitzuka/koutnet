#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gdf_imports.py — Centralized imports for GoidaPhone modules.
Every module: from gdf_imports import *
Provides Qt6 + all GoidaPhone core symbols (TR, _L, S, get_theme, etc.)
"""

# ── Standard library ─────────────────────────────────────────────────────────
import os, sys, json, time, re, math, base64, hashlib, socket
import threading, traceback, platform, shutil, subprocess, secrets
import struct, tempfile, zipfile, urllib.request
from pathlib import Path
from typing import Optional

# ── Qt6 Widgets ──────────────────────────────────────────────────────────────
from PyQt6.QtWidgets import (
    QApplication, QWidget, QMainWindow, QDialog, QLabel, QPushButton,
    QVBoxLayout, QHBoxLayout, QGridLayout, QFormLayout, QStackedLayout,
    QStackedWidget,
    QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QCheckBox, QRadioButton,
    QSlider, QSpinBox, QDoubleSpinBox, QScrollArea, QScrollBar,
    QListWidget, QListWidgetItem, QTreeWidget, QTreeWidgetItem,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QTabWidget, QTabBar, QSplitter, QFrame, QGroupBox,
    QMenuBar, QToolBar, QStatusBar, QWidgetAction,
    QMessageBox, QFileDialog, QColorDialog, QInputDialog,
    QSystemTrayIcon, QProgressBar, QSizePolicy,
    QAbstractItemView, QAbstractScrollArea,
    QGraphicsOpacityEffect, QGraphicsDropShadowEffect,
    QMenu,
)

# ── Qt6 Core ─────────────────────────────────────────────────────────────────
from PyQt6.QtCore import (
    Qt, QTimer, QThread, QObject, QRunnable, QThreadPool,
    pyqtSignal, pyqtSlot, QSize, QPoint, QRect, QRectF,
    QPropertyAnimation, QParallelAnimationGroup, QSequentialAnimationGroup,
    QAbstractAnimation, QEasingCurve,
    QUrl, QMimeData, QByteArray, QBuffer, QIODevice,
    QDateTime, QDate, QTime, QLocale,
    QSettings, QStandardPaths,
    QSortFilterProxyModel,
)

# ── Qt6 Gui — NOTE: QAction lives here in PyQt6, NOT in QtWidgets ─────────────
from PyQt6.QtGui import (
    QColor, QFont, QFontMetrics, QPainter, QPainterPath, QPen, QBrush,
    QPixmap, QImage, QIcon, QCursor, QPalette,
    QKeySequence, QShortcut,
    QAction,          # In PyQt6, QAction is in QtGui, not QtWidgets
    QLinearGradient, QRadialGradient, QConicalGradient,
    QTextCharFormat, QTextCursor, QTextDocument,
    QDesktopServices, QDrag,
    QValidator, QIntValidator, QDoubleValidator,
)

# ── Qt6 Network ──────────────────────────────────────────────────────────────
from PyQt6.QtNetwork import (
    QUdpSocket, QTcpSocket, QTcpServer, QHostAddress,
    QNetworkInterface, QNetworkAddressEntry, QNetworkRequest,
    QNetworkAccessManager, QNetworkReply,
    QAbstractSocket,
)

# ── Qt6 Multimedia (optional) ─────────────────────────────────────────────────
try:
    from PyQt6.QtMultimedia import (
        QMediaPlayer, QAudioOutput, QAudioInput, QAudioDevice,
        QMediaDevices, QAudioFormat,
    )
    _HAS_MULTIMEDIA = True
except ImportError:
    _HAS_MULTIMEDIA = False

# ── GoidaPhone Core — TR, _L, S, AppSettings, themes, utils ─────────────────
# gdf_core.py has no Qt imports of its own (only std lib),
# so importing it here is safe — no circular dependency.
try:
    from gdf_core import *
except ImportError as _e:
    import sys as _sys
    print(f"[gdf_imports] gdf_core import failed: {_e}", file=_sys.stderr)
    # Minimal stubs so the app can at least show an error
    def _L(ru, en="", ja=""): return en or ru
    def TR(k, **kw): return k
    class _FakeS:
        def __getattr__(self, name): return None
        def get(self, *a, **kw): return kw.get("default", None)
    def S(): return _FakeS()
    def get_theme(name="dark"): return {}

__all__ = [name for name in dir() if not name.startswith('__')]
