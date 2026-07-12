#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Core module
# Constants, Security, Utilities, Toast, Themes, Localization, Settings
import warnings as _w
_w.filterwarnings("ignore", category=UserWarning, module="pkg_resources")
_w.filterwarnings("ignore", message=".*pkg_resources.*")

# ── Подавляем Qt CSS/logging спам ─────────────────────────────────────────────
import os as _os_early, sys as _sys_early

class _QtStderrFilter:
    """Фильтрует мусорные строки Qt из stderr."""
    _SKIP = (
        "Could not parse stylesheet",
        "Ignoring malformed logging rule",
        "qt.core.logging:",
        "qt.multimedia.ffmpeg:",
        "qt.qpa.fonts:",
        "qt.qpa.stylesheet:",
        "qt.widgets.stylesheet:",
        "Using Qt multimedia with FFmpeg",
        "qt.multimedia*=false",
        "qt.widgets.stylesheet=false",
        "qt.qpa.stylesheet=false",
        "qt.qpa.fonts=false",
        "qt.core.logging=false",
        "*.debug=false",
        "ffmpeg*=false",
    )
    def __init__(self, wrapped):
        self._w = wrapped
        self._buf = ""  # буфер для многострочных сообщений

    def write(self, s):
        if not s:
            return 0
        # Проверяем каждую строку отдельно
        lines = s.split('\n')
        out = []
        for line in lines:
            skip = any(p in line for p in self._SKIP)
            # Также пропускаем строки которые являются продолжением правил
            # (*.debug=false, qt.qpa.*=false и т.д.)
            if not skip and (
                line.strip() in ('', ) or
                (line.strip().endswith('=false') and '.' in line.strip())
            ):
                skip = True
            if not skip:
                out.append(line)
        result = '\n'.join(out)
        if result and result != '\n':
            return self._w.write(result)
        return len(s)

    def flush(self): return self._w.flush()
    def fileno(self): return self._w.fileno()
    def isatty(self): return self._w.isatty()
    @property
    def encoding(self): return self._w.encoding
    @property
    def errors(self): return getattr(self._w, "errors", "strict")

_sys_early.stderr = _QtStderrFilter(_sys_early.stderr)

# QT_LOGGING_RULES — одна строка без переносов (Qt 6 требует \n как разделитель)
_os_early.environ["QT_LOGGING_RULES"] = (
    "*.debug=false;"
    "qt.qpa.stylesheet=false;"
    "qt.qpa.fonts=false;"
    "qt.widgets.stylesheet=false;"
    "qt.core.logging=false;"
    "qt.multimedia*=false;"
    "ffmpeg*=false"
)
# ─────────────────────────────────────────────────────────────────────────────
"""
GoidaPhone v1.8 — by Winora Company
LAN/VPN messenger with voice calls, file sharing, groups, profiles.
Cross-platform: Windows & Linux.
"""

import sys
import os
import json
import traceback
import time
import secrets
import socket
import threading
import hashlib
import base64
import struct
import platform
import subprocess
import tempfile
import shutil
import zipfile
import re
import urllib.request
import urllib.error
import math
import colorsys
from datetime import datetime, timedelta
from pathlib import Path
from collections import defaultdict
import queue
import io

# ── AES-256 encryption (п.31) ───────────────────────────────────────────────
# Uses only stdlib — no external crypto deps required.
# Algorithm: AES-256-CBC with PKCS7 padding, PBKDF2-HMAC-SHA256 key derivation.
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives import padding as crypto_padding, hashes, hmac
    from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
    from cryptography.hazmat.backends import default_backend
    CRYPTO_AVAILABLE = True
except ImportError:
    CRYPTO_AVAILABLE = False

# ── Optional image/gif support ───────────────────────────────────────────────
try:
    from PIL import Image, ImageSequence
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

# ── PyQt6 ──────────────────────────────────────────────────────────────────
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTextEdit, QLineEdit, QPushButton, QListWidget, QLabel, QFrame,
    QTabWidget, QDialog, QComboBox, QSlider, QCheckBox, QGroupBox,
    QProgressBar, QFileDialog, QMessageBox, QScrollArea,
    QSystemTrayIcon, QMenu, QInputDialog, QSplitter, QListWidgetItem,
    QTextBrowser, QToolBar, QStatusBar, QToolButton, QSpinBox,
    QColorDialog, QGridLayout, QStackedWidget, QScrollBar,
    QSizePolicy, QAbstractItemView, QPlainTextEdit, QFormLayout, QWidgetAction,
    QTableWidget, QTableWidgetItem, QHeaderView
)
from PyQt6.QtCore import (
    Qt, QTimer, pyqtSignal, QThread, QSize, QSettings,
    QUrl, QFileInfo, QByteArray, QBuffer, QIODevice,
    QRunnable, QThreadPool, pyqtSlot, QObject, QPoint, QRect,
    QPropertyAnimation, QEasingCurve, QSequentialAnimationGroup,
    QParallelAnimationGroup, QAbstractAnimation
)
from PyQt6.QtGui import (
    QFont, QIcon, QPalette, QColor, QPixmap, QAction,
    QDesktopServices, QTextCharFormat, QTextCursor,
    QImage, QPainter, QBrush, QPen, QLinearGradient,
    QGradient, QFontMetrics, QMovie, QTransform, QKeySequence, QShortcut
)
# QtMultimedia is optional — only needed for future media playback features
try:
    from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput
    QTMULTIMEDIA_AVAILABLE = True
except ImportError:
    QTMULTIMEDIA_AVAILABLE = False
from PyQt6.QtNetwork import QUdpSocket, QTcpSocket, QTcpServer, QHostAddress, QNetworkInterface

# ── Optional audio ──────────────────────────────────────────────────────────
try:
    import pyaudio
    PYAUDIO_AVAILABLE = True
except ImportError:
    PYAUDIO_AVAILABLE = False
    print("⚠ pyaudio not found — voice calls disabled. Install: pip install pyaudio")

# ── Optional image handling ─────────────────────────────────────────────────
try:
    from PIL import Image
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

# ═══════════════════════════════════════════════════════════════════════════
#  CONSTANTS
# ═══════════════════════════════════════════════════════════════════════════
APP_VERSION       = "1.8.0"
APP_NAME          = "GoidaPhone"
COMPANY_NAME      = "Winora Company"
LICENSE_DIVISOR   = 909          # license_code % 909 == 0  → valid
LICENSE_DAYS      = 30           # premium lasts 30 days

# ── Protocol versioning ─────────────────────────────────────────────────────
# Used for cross-version compatibility.
# New clients send PROTOCOL_VERSION; if remote version is older, disable new features.
# Rule: major bump = incompatible, minor bump = backward-compatible.
PROTOCOL_VERSION  = 3            # bump when network format changes
PROTOCOL_COMPAT   = 2            # minimum protocol version we can talk to

# ── Feature flags per protocol version ─────────────────────────────────────
# Key = feature name, value = minimum protocol version required
FEATURE_REACTIONS  = 3
FEATURE_FORWARDING = 3
FEATURE_MENTIONS   = 3
FEATURE_FORMATTING = 3

# ═══════════════════════════════════════════════════════════════════════════
#  SECURITY ENGINE v2 — AES-256-GCM + X25519 ECDH + Ed25519 + replay guard
# ═══════════════════════════════════════════════════════════════════════════
class CryptoEngine:
    """
    GoidaPhone Security Engine v2.

    LAYER 1 — Automatic key exchange (X25519 ECDH)
      Each node generates a long-term X25519 keypair on first run.
      Ed25519 identity key signs the X25519 public key.
      On first contact, nodes exchange & verify signed DH keys.
      HKDF-SHA256 derives a 256-bit session key per peer.

    LAYER 2 — Message encryption (AES-256-GCM)
      AES-256-GCM: authenticated encryption, no padding oracle.
      Random 12-byte nonce per message (2^96 space).
      GCM tag authenticates ciphertext; tampering → decryption fails.
      Wire: type[1] + nonce[12] + ciphertext+tag

    LAYER 3 — Passphrase overlay (groups)
      PBKDF2-SHA256 (480 000 iters) → 256-bit group key.
      Salt[32] stored with each message.

    LAYER 4 — Packet HMAC (session-key signed control packets)
      Every UDP packet optionally carries HMAC-SHA256(session_key, payload).
      Prevents spoofing of reactions/edits/calls from unauthorized peers.

    LAYER 5 — Replay protection
      Nonce + timestamp checked per peer.
      Window: +-30 seconds; seen nonces cached for 60 s.

    LAYER 6 — Rate limiting
      Max 200 packets/second per source IP.
    """

    NONCE_LEN       = 12
    TAG_LEN         = 16
    KEY_LEN         = 32
    KDF_ITERS       = 480_000
    SALT_LEN        = 32
    REPLAY_WINDOW_S = 30
    NONCE_CACHE_TTL = 60

    def __init__(self):
        self._key_cache: dict        = {}
        self._session_keys: dict     = {}   # peer_ip -> bytes
        self._peer_id_pub: dict      = {}   # peer_ip -> Ed25519PublicKey
        self._seen_nonces: dict      = {}   # peer_ip -> {nonce_hex: ts}
        self._rate_counters: dict    = {}   # ip -> [timestamps]
        self._identity_priv          = None
        self._dh_priv                = None
        self._dh_pub_bytes: bytes    = b""
        self._identity_pub_bytes: bytes = b""
        self._dh_pub_sig: bytes      = b""
        if CRYPTO_AVAILABLE:
            self._init_keypairs()

    def _init_keypairs(self):
        """Load or generate Ed25519 identity + X25519 DH keypairs."""
        try:
            id_raw = _load_raw_setting("identity_priv_b64", "")
            dh_raw = _load_raw_setting("dh_priv_b64", "")
            if not id_raw or not dh_raw:
                raise ValueError("no keys stored")
            self._identity_priv = Ed25519PrivateKey.from_private_bytes(base64.b64decode(id_raw))
            self._dh_priv       = X25519PrivateKey.from_private_bytes(base64.b64decode(dh_raw))
        except Exception:
            self._identity_priv = Ed25519PrivateKey.generate()
            self._dh_priv       = X25519PrivateKey.generate()
            _save_raw_setting("identity_priv_b64", base64.b64encode(
                self._identity_priv.private_bytes(serialization.Encoding.Raw,
                    serialization.PrivateFormat.Raw, serialization.NoEncryption())).decode())
            _save_raw_setting("dh_priv_b64", base64.b64encode(
                self._dh_priv.private_bytes(serialization.Encoding.Raw,
                    serialization.PrivateFormat.Raw, serialization.NoEncryption())).decode())

        self._dh_pub_bytes = self._dh_priv.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self._identity_pub_bytes = self._identity_priv.public_key().public_bytes(
            serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self._dh_pub_sig = self._identity_priv.sign(self._dh_pub_bytes)

    # ── Handshake ─────────────────────────────────────────────────────
    def get_handshake_payload(self) -> dict:
        """Public key bundle for inclusion in presence packets."""
        if not CRYPTO_AVAILABLE:
            return {}
        return {
            "dh_pub":     base64.b64encode(self._dh_pub_bytes).decode(),
            "id_pub":     base64.b64encode(self._identity_pub_bytes).decode(),
            "dh_pub_sig": base64.b64encode(self._dh_pub_sig).decode(),
        }

    def process_handshake(self, peer_ip: str, data: dict) -> bool:
        """
        Derive session key from peer's handshake bundle.
        Verifies Ed25519 signature of DH public key.
        Returns True on success.
        """
        if not CRYPTO_AVAILABLE:
            return False
        try:
            peer_dh_bytes  = base64.b64decode(data["dh_pub"])
            peer_id_bytes  = base64.b64decode(data["id_pub"])
            peer_dh_sig    = base64.b64decode(data["dh_pub_sig"])

            # Verify: peer's identity key signed their DH key
            peer_id_pub = Ed25519PublicKey.from_public_bytes(peer_id_bytes)
            peer_id_pub.verify(peer_dh_sig, peer_dh_bytes)   # raises InvalidSignature if bad

            # ECDH key exchange
            peer_dh_pub = X25519PublicKey.from_public_bytes(peer_dh_bytes)
            shared_secret = self._dh_priv.exchange(peer_dh_pub)

            # HKDF-SHA256 -> 32-byte session key
            hkdf = HKDF(algorithm=crypto_hashes.SHA256(), length=32,
                        salt=None, info=b"GoidaPhone-v2-session",
                        backend=default_backend())
            session_key = hkdf.derive(shared_secret)
            self._session_keys[peer_ip] = session_key
            self._peer_id_pub[peer_ip]  = peer_id_pub
            return True
        except Exception as e:
            print(f"[crypto] handshake failed from {peer_ip}: {e}")
            return False

    def has_session(self, peer_ip: str) -> bool:
        return peer_ip in self._session_keys

    def fingerprint(self) -> str:
        """Human-readable fingerprint of our identity public key."""
        if not CRYPTO_AVAILABLE or not self._identity_pub_bytes:
            return "N/A"
        h = hashlib.sha256(self._identity_pub_bytes).hexdigest()
        return " ".join(h[i:i+4].upper() for i in range(0, 24, 4))

    def peer_fingerprint(self, peer_ip: str) -> str:
        """Fingerprint of peer's identity key (for verification UI)."""
        if not CRYPTO_AVAILABLE:
            return "N/A"
        pub = self._peer_id_pub.get(peer_ip)
        if pub is None:
            return "?"
        raw = pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        h = hashlib.sha256(raw).hexdigest()
        return " ".join(h[i:i+4].upper() for i in range(0, 24, 4))

    def security_level(self, peer_ip: str = None) -> str:
        """Return security level string for status bar."""
        if not CRYPTO_AVAILABLE:
            return "⚠ XOR"
        if peer_ip and peer_ip in self._session_keys:
            return "🔒 E2E"
        if S().encryption_enabled and S().encryption_passphrase:
            return "🔑 PSK"
        return "⚠ Plain"

    # ── AES-256-GCM ───────────────────────────────────────────────────
    def _gcm_encrypt(self, key: bytes, plaintext: bytes) -> bytes:
        nonce = secrets.token_bytes(self.NONCE_LEN)
        ct = AESGCM(key).encrypt(nonce, plaintext, None)
        return nonce + ct   # nonce + ciphertext+tag

    def _gcm_decrypt(self, key: bytes, data: bytes) -> bytes:
        nonce = data[:self.NONCE_LEN]
        ct    = data[self.NONCE_LEN:]
        return AESGCM(key).decrypt(nonce, ct, None)  # raises on bad tag

    # ── PBKDF2 for passphrase keys ────────────────────────────────────
    def _derive_key(self, passphrase: str, salt: bytes) -> bytes:
        k = passphrase + salt.hex()
        if k in self._key_cache:
            return self._key_cache[k]
        key = hashlib.pbkdf2_hmac("sha256", passphrase.encode(),
                                  salt, self.KDF_ITERS, dklen=self.KEY_LEN)
        self._key_cache[k] = key
        return key

    # ── Packet HMAC ───────────────────────────────────────────────────
    def sign_packet(self, peer_ip: str, payload_bytes: bytes) -> str:
        import hmac as _hmac
        key = self._session_keys.get(peer_ip, b"")
        if not key:
            return ""
        return base64.b64encode(
            _hmac.new(key, payload_bytes, hashlib.sha256).digest()
        ).decode()

    def verify_packet(self, peer_ip: str, payload_bytes: bytes, sig_b64: str) -> bool:
        import hmac as _hmac
        key = self._session_keys.get(peer_ip, b"")
        if not key:
            return True   # no session yet — accept
        try:
            expected = _hmac.new(key, payload_bytes, hashlib.sha256).digest()
            return _hmac.compare_digest(expected, base64.b64decode(sig_b64))
        except Exception:
            return False

    # ── Replay protection ─────────────────────────────────────────────
    def check_replay(self, peer_ip: str, nonce_hex: str, ts: float) -> bool:
        """Return True if packet is fresh and nonce not seen before."""
        now = time.time()
        if abs(now - ts) > self.REPLAY_WINDOW_S:
            return False
        bucket = self._seen_nonces.setdefault(peer_ip, {})
        if nonce_hex in bucket:
            return False
        bucket[nonce_hex] = now
        expired = [k for k, t_ in bucket.items() if now - t_ > self.NONCE_CACHE_TTL]
        for k in expired:
            del bucket[k]
        return True

    # ── Rate limiting ─────────────────────────────────────────────────
    def check_rate(self, peer_ip: str, max_per_sec: int = 200) -> bool:
        now = time.time()
        window = self._rate_counters.setdefault(peer_ip, [])
        window[:] = [t for t in window if now - t < 1.0]
        if len(window) >= max_per_sec:
            return False
        window.append(now)
        return True

    # ── Public encrypt/decrypt ────────────────────────────────────────
    def encrypt(self, plaintext: str, passphrase: str = "",
                peer_ip: str = None) -> str:
        """
        Encrypt message.
        Type 0x01 = AES-GCM with ECDH session key (best)
        Type 0x02 = AES-GCM with PBKDF2 passphrase key
        Type 0xFF = XOR fallback (no cryptography lib)
        """
        data = plaintext.encode("utf-8")
        if CRYPTO_AVAILABLE:
            if peer_ip and peer_ip in self._session_keys:
                ct   = self._gcm_encrypt(self._session_keys[peer_ip], data)
                wire = bytes([0x01]) + ct
            elif passphrase:
                salt = secrets.token_bytes(self.SALT_LEN)
                key  = self._derive_key(passphrase, salt)
                ct   = self._gcm_encrypt(key, data)
                wire = bytes([0x02]) + salt + ct
            else:
                return plaintext
            return "🔐" + base64.b64encode(wire).decode("ascii")
        if passphrase:
            key   = hashlib.sha256(passphrase.encode()).digest()
            xored = bytes(b ^ key[i % 32] for i, b in enumerate(data))
            return "🔐" + base64.b64encode(bytes([0xFF]) + xored).decode("ascii")
        return plaintext

    def decrypt(self, ciphertext: str, passphrase: str = "",
                peer_ip: str = None) -> str:
        if not ciphertext.startswith("🔐"):
            return ciphertext
        try:
            wire = base64.b64decode(ciphertext[len("🔐"):])
        except Exception:
            return "[🔒 Ошибка декодирования]"
        if not wire:
            return "[🔒 Пустой пакет]"
        t, payload = wire[0], wire[1:]

        if t == 0x01:
            if not CRYPTO_AVAILABLE:
                return "[🔒 Требуется: pip install cryptography]"
            key = self._session_keys.get(peer_ip, b"") if peer_ip else b""
            if not key:
                return "[🔒 E2E-ключ не согласован. Подождите переподключения.]"
            try:
                return self._gcm_decrypt(key, payload).decode("utf-8")
            except Exception:
                return "[🔒 Ошибка расшифровки E2E (подмена данных?)]"

        elif t == 0x02:
            if not CRYPTO_AVAILABLE:
                return "[🔒 Требуется: pip install cryptography]"
            if not passphrase:
                return "[🔒 Зашифровано — введите пароль в настройках]"
            try:
                salt = payload[:self.SALT_LEN]
                ct   = payload[self.SALT_LEN:]
                key  = self._derive_key(passphrase, salt)
                return self._gcm_decrypt(key, ct).decode("utf-8")
            except Exception:
                return "[🔒 Неверный пароль или данные повреждены]"

        elif t == 0xFF:
            if not passphrase:
                return "[🔒 Зашифровано (XOR). Введите пароль.]"
            key = hashlib.sha256(passphrase.encode()).digest()
            try:
                return bytes(b ^ key[i % 32] for i, b in enumerate(payload)).decode("utf-8")
            except Exception:
                return "[🔒 Ошибка расшифровки XOR]"

        return "[🔒 Неизвестный формат]"

    def is_encrypted(self, text: str) -> bool:
        return isinstance(text, str) and text.startswith("🔐")

    @staticmethod
    def status() -> str:
        if CRYPTO_AVAILABLE:
            return "✅ AES-256-GCM | X25519 ECDH | Ed25519 | PBKDF2-480k | Replay Guard"
        return "⚠ XOR-fallback (pip install cryptography)"


# ── QSettings helpers for early crypto init (before S() is available) ────────
def _load_raw_setting(key: str, default: str = "") -> str:
    s = QSettings("WinoraCompany", "GoidaPhone")
    v = s.value(key, default)
    return str(v) if v is not None else default

def _save_raw_setting(key: str, value: str):
    s = QSettings("WinoraCompany", "GoidaPhone")
    s.setValue(key, value)
    s.sync()


CRYPTO = CryptoEngine()   # global instance



UDP_PORT_DEFAULT  = 17385
TCP_PORT_DEFAULT  = 17386

# GitHub update check  — REPLACE with your repo URL
GITHUB_REPO = "nft1212/GoidaPhone-NT-Server-1.8-OPEN"   # e.g. "john/GoidaPhone"
GITHUB_API_URL    = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
GITHUB_RAW_URL    = f"https://raw.githubusercontent.com/{GITHUB_REPO}/main/gdf.py"

# Data directory — cross-platform
if platform.system() == "Windows":
    DATA_DIR = Path(os.getenv("APPDATA", Path.home())) / "GoidaPhone"
else:
    DATA_DIR = Path.home() / ".config" / "GoidaPhone"

DATA_DIR.mkdir(parents=True, exist_ok=True)
AVATARS_DIR   = DATA_DIR / "avatars"
RECEIVED_DIR  = DATA_DIR / "received_files"
HISTORY_DIR   = DATA_DIR / "history"
GROUPS_FILE   = DATA_DIR / "groups.json"
CONTACTS_FILE = DATA_DIR / "contacts.json"

for d in [AVATARS_DIR, RECEIVED_DIR, HISTORY_DIR]:
    d.mkdir(parents=True, exist_ok=True)

# ═══════════════════════════════════════════════════════════════════════════
#  UTILITIES
# ═══════════════════════════════════════════════════════════════════════════
def detect_connection_type(remote_ip: str) -> str:
    """Return 'LAN' or 'VPN' by comparing remote IP subnets with local interfaces."""
    try:
        for iface in QNetworkInterface.allInterfaces():
            for entry in iface.addressEntries():
                addr = entry.ip()
                mask = entry.netmask()
                if addr.protocol() != QHostAddress.NetworkLayerProtocol.IPv4Protocol:
                    continue
                local = addr.toString()
                netmask = mask.toString()
                # compute network prefix
                la = [int(x) for x in local.split('.')]
                ra = [int(x) for x in remote_ip.split('.')]
                ma = [int(x) for x in netmask.split('.')]
                if all((la[i] & ma[i]) == (ra[i] & ma[i]) for i in range(4)):
                    name = iface.name().lower()
                    # VPN adapters commonly contain these strings
                    if any(k in name for k in ['vpn','tun','tap','hamachi','radmin','virtual','veth','wg']):
                        return 'VPN'
                    return 'LAN'
    except Exception:
        pass
    return 'VPN'   # fallback — if can't match, assume VPN

# ─────────────────────────────────────────────────────────────────────────────
#  GoidaID  (Safe Mode)
#  A cryptographic pseudonym that replaces visible IP addresses.
#  Format:  GID-XXXXXX  (6 uppercase hex chars derived from HMAC-SHA256)
#  The mapping is local-only: others see only the GoidaID, not the raw IP.
# ─────────────────────────────────────────────────────────────────────────────

_GOIDA_ID_SALT_KEY = "goida_id_salt"

def _get_goida_salt() -> bytes:
    """Get or create persistent HMAC salt for GoidaID generation."""
    stored = _load_raw_setting(_GOIDA_ID_SALT_KEY, "")
    if stored:
        try:
            return bytes.fromhex(stored)
        except Exception:
            pass
    salt = secrets.token_bytes(32)
    _save_raw_setting(_GOIDA_ID_SALT_KEY, salt.hex())
    return salt

def ip_to_goida_id(ip: str) -> str:
    """
    Derive a stable GoidaID from an IP address using HMAC-SHA256.
    Same IP always produces the same GoidaID on this device,
    but the mapping cannot be reversed by other parties.
    """
    import hmac as _hmac
    salt = _get_goida_salt()
    h = _hmac.new(salt, ip.encode("utf-8"), hashlib.sha256).hexdigest()
    return f"GID-{h[:6].upper()}"

def display_id(ip: str) -> str:
    """
    Return the display identifier for an IP.
    In safe mode returns GoidaID; otherwise returns the raw IP.
    """
    if S().get("safe_mode", False, t=bool):
        return ip_to_goida_id(ip)
    return ip


def get_local_ip() -> str:
    """Return the best local IPv4 address.
    Prefers physical LAN (192.168.x, 10.x, 172.16-31.x) over VPN tunnels.
    Falls back to any non-loopback address, then 127.0.0.1."""
    try:
        # Primary: use routing table to find default interface
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
        if ip and ip != "0.0.0.0" and not ip.startswith("127."):
            return ip
    except Exception:
        pass
    # Fallback: enumerate interfaces
    try:
        for iface in QNetworkInterface.allInterfaces():
            if iface.flags() & QNetworkInterface.InterfaceFlag.IsLoopBack:
                continue
            if not (iface.flags() & QNetworkInterface.InterfaceFlag.IsUp):
                continue
            for entry in iface.addressEntries():
                addr = entry.ip()
                if addr.protocol() != QHostAddress.NetworkLayerProtocol.IPv4Protocol:
                    continue
                ip = addr.toString()
                if ip.startswith("127.") or ip.startswith("169.254."):
                    continue
                return ip
    except Exception:
        pass
    return "127.0.0.1"


def get_all_local_ips() -> set:
    """Return ALL local IPv4 addresses (LAN + VPN + loopback excluded).
    Used for robust self-message deduplication across multi-homed systems."""
    ips = set()
    # Routing-table method
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            ips.add(s.getsockname()[0])
    except Exception:
        pass
    # Interface enumeration — catches VPN adapters (Hamachi/ZeroTier/Radmin)
    try:
        for iface in QNetworkInterface.allInterfaces():
            if iface.flags() & QNetworkInterface.InterfaceFlag.IsLoopBack:
                continue
            for entry in iface.addressEntries():
                addr = entry.ip()
                if addr.protocol() != QHostAddress.NetworkLayerProtocol.IPv4Protocol:
                    continue
                ip = addr.toString()
                if not ip.startswith("127.") and not ip.startswith("169.254."):
                    ips.add(ip)
    except Exception:
        pass
    ips.discard("0.0.0.0")
    return ips

def get_os_name() -> str:
    s = platform.system()
    if s == "Windows":
        return f"Windows {platform.version()}"
    elif s == "Linux":
        try:
            import distro
            return f"Linux {distro.name()} {distro.version()}"
        except Exception:
            return f"Linux {platform.release()}"
    elif s == "Darwin":
        return f"macOS {platform.mac_ver()[0]}"
    return s

# ─────────────────────────────────────────────────────────────────────────────
#  SOUND SYSTEM
#
#  Strategy (so sounds survive PyInstaller / distribution):
#   1. On startup, copy sounds from  script-dir/gdfsound/  →  DATA_DIR/sounds/
#      This makes them available regardless of working directory.
#   2. At play time look in:
#        a. DATA_DIR/sounds/          (persistent copy)
#        b. script-dir/gdfsound/      (dev / same-folder run)
#        c. PyInstaller _MEIPASS/gdfsound/  (frozen bundle)
#   3. OS fallback beep if nothing found.
# ─────────────────────────────────────────────────────────────────────────────

_SOUND_MAP_DEFAULT = {
    "message":    ["gdfclick.wav"],
    "mention":    ["criterr.mp3"],
    "call":       ["criterr.mp3"],
    "call_end":   ["err.wav"],
    "online":     ["zakrep.wav"],
    "offline":    ["err.wav"],
    "error":      ["err.wav"],
    "user_error": ["oshibka_usera.mp3"],
    "critical":   ["criterr.mp3"],
    "delete":     ["mvdtotrash.wav"],
    "pin":        ["zakrep.wav"],
    "unpin":      ["mvdtotrash.wav"],
    "click":      ["gdfclick.wav"],
    "bookmark":   ["zakrep.wav"],
    "reaction":   ["gdfclick.wav"],
}

_SOUND_EVENT_LABELS = {
    "message":    "💬 Новое сообщение",
    "mention":    "🔔 Упоминание (@имя)",
    "call":       "📞 Входящий звонок",
    "call_end":   "📵 Call ended",
    "online":     "🟢 Пользователь online",
    "offline":    "🔴 Пользователь офлайн",
    "delete":     "🗑 Удаление",
    "pin":        "📌 Закрепление",
    "click":      "🖱 Клик кнопки",
    "error":      "⚠ Ошибка",
    "user_error": "❗ Ошибка пользователя",
    "critical":   "💀 Критическая ошибка",
}

def _get_sound_map() -> dict:
    """Возвращает звуковую схему — пользовательскую или дефолтную."""
    try:
        import json as _j
        user = _j.loads(S().get("sound_scheme", "{}", t=str))
        result = {}
        for event, default_files in _SOUND_MAP_DEFAULT.items():
            uv = user.get(event)
            if uv == "__none__":  result[event] = []
            elif uv:              result[event] = [uv]  # полный путь или имя
            else:                 result[event] = default_files
        return result
    except Exception:
        return dict(_SOUND_MAP_DEFAULT)

_SOUND_MAP = _SOUND_MAP_DEFAULT  # backward compat alias


_SOUNDS_INSTALLED = False

def _get_sound_dirs() -> list:
    """Return all directories to search for sound files, in priority order.

    Search order (most specific → fallback):
      1. DATA_DIR/sounds/            — persistent copy (written on first run)
      2. next to __file__ / gdfsound/— dev layout
      3. PyInstaller _MEIPASS/       — frozen bundle assets
      4. ~/Desktop/gdfsound/         — common dev machine location
      5. ~/gdfsound/                 — home dir fallback
      6. /usr/share/GoidaPhone/sounds — Linux system install
      7. %APPDATA%/GoidaPhone/sounds — Windows fallback
    """
    dirs = []
    # 1) SAME DIR as script — sounds live next to gdf.py (new layout)
    try:
        if getattr(sys, 'frozen', False):
            script_dir = Path(sys.executable).parent
        else:
            script_dir = Path(__file__).resolve().parent
        dirs.append(script_dir)          # sounds directly next to gdf.py
        dirs.append(script_dir / "gdfsound")
        dirs.append(script_dir / "sounds")
    except Exception:
        pass
    # 1b) sys.argv[0] — catches `python3 /path/gdf.py`
    try:
        argv0_dir = Path(sys.argv[0]).resolve().parent
        dirs.append(argv0_dir)           # sounds directly next to gdf.py
        dirs.append(argv0_dir / "gdfsound")
        dirs.append(argv0_dir / "sounds")
    except Exception:
        pass
    # 1c) cwd
    try:
        cwd = Path(os.getcwd())
        dirs.append(cwd)
        dirs.append(cwd / "gdfsound")
    except Exception:
        pass
    # 2) DATA_DIR persistent copy (cached on first run)
    dirs.append(DATA_DIR / "sounds")
    # 3) PyInstaller _MEIPASS
    try:
        if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
            dirs.append(Path(sys._MEIPASS) / "gdfsound")
            dirs.append(Path(sys._MEIPASS) / "sounds")
    except Exception:
        pass
    # 4+5) Common developer locations
    home = Path.home()
    dirs.append(home / "Desktop" / "gdfsound")
    dirs.append(home / "Desktop" / "sounds")
    dirs.append(home / "gdfsound")
    dirs.append(home / "sounds")
    # Also try every user's Desktop subfolder (multi-user systems)
    try:
        import os as _os
        xdg_desktop = _os.environ.get("XDG_DESKTOP_DIR", "")
        if xdg_desktop:
            dirs.append(Path(xdg_desktop) / "gdfsound")
    except Exception:
        pass
    # 5b) GitHub dev layout — common for pixless
    dirs.append(home / "Desktop" / "GitHub200" / "gdfsound")
    dirs.append(home / "Desktop" / "GitHub" / "gdfsound")
    # Windows: Documents/GitHub200/gdfsound
    try:
        docs = Path.home() / "Documents"
        dirs.append(docs / "GitHub200" / "gdfsound")
        dirs.append(docs / "gdfsound")
    except Exception: pass
    # 6) Linux system install
    dirs.append(Path("/usr/share/GoidaPhone/sounds"))
    dirs.append(Path("/usr/local/share/GoidaPhone/sounds"))
    # 7) Windows %APPDATA%
    try:
        appdata = os.environ.get("APPDATA", "")
        if appdata:
            dirs.append(Path(appdata) / "GoidaPhone" / "sounds")
    except Exception:
        pass
    return dirs

def install_sounds(force: bool = False):
    """
    Copy ALL sound files found in ANY source dir → DATA_DIR/sounds/
    Called once at startup so sounds are always available even if the
    user moves the app or runs it from a different directory.

    If no files were found last time (dest empty), re-scans every time.
    Pass force=True to always re-scan (used by /sounds install command).
    """
    global _SOUNDS_INSTALLED
    # Check if dest actually has files — if not, re-run even if flagged
    dest = DATA_DIR / "sounds"
    dest_has_files = dest.exists() and any(
        dest.glob(f) for f in ["*.wav", "*.mp3"])
    if _SOUNDS_INSTALLED and dest_has_files and not force:
        return
    _SOUNDS_INSTALLED = True
    dest = DATA_DIR / "sounds"
    try:
        dest.mkdir(parents=True, exist_ok=True)
        # Build complete list of expected filenames
        all_names: set[str] = set()
        for names in _SOUND_MAP.values():
            all_names.update(names)
        # Also grab any .wav/.mp3 found in any source dir (for future-proof)
        src_dirs = _get_sound_dirs()[1:]   # skip dest itself
        found_any = False
        for sdir in src_dirs:
            if not sdir.is_dir():
                continue
            for name in all_names:
                src_f = sdir / name
                dst_f = dest / name
                if src_f.exists() and not dst_f.exists():
                    try:
                        shutil.copy2(str(src_f), str(dst_f))
                        found_any = True
                        print(f"[sound] installed: {name} ← {sdir}")
                    except Exception as e:
                        print(f"[sound] copy error {name}: {e}")
        if found_any:
            print(f"[sound] installed sounds → {dest}")
    except Exception as e:
        print(f"[sound] install_sounds error: {e}")

def _find_sound_file(fname: str) -> str | None:
    """Find a sound file. Accepts full path or filename. Returns path or None."""
    # Если полный путь — проверяем напрямую
    try:
        p = Path(fname)
        if p.is_absolute() and p.exists():
            return str(p)
    except Exception:
        pass
    # Иначе ищем по имени файла в известных директориях
    name = Path(fname).name
    for d in _get_sound_dirs():
        f = d / name
        if f.exists():
            return str(f)
    return None

# ─────────────────────────────────────────────────────────────────────────────
#  QMediaPlayer-based sound engine (uses FFmpeg already bundled with PyQt6)
#  Keeps a small pool of players so overlapping sounds work.
# ─────────────────────────────────────────────────────────────────────────────
_SOUND_PLAYERS: list = []   # keep QMediaPlayer refs alive

def _play_file(sf: str):
    """Play a sound file. Primary: QMediaPlayer (FFmpeg). Fallback: aplay/mpg123."""
    from pathlib import Path as _P
    sf_path = _P(sf)
    if not sf_path.exists():
        print(f"[sound] file not found: {sf}")
        return
    try:
        from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput, QMediaDevices
        from PyQt6.QtCore import QUrl
        player = QMediaPlayer()
        # Всегда берём актуальный дефолтный выход (работает при смене устройства)
        try:
            _default_dev = QMediaDevices.defaultAudioOutput()
            audio = QAudioOutput(_default_dev)
        except Exception:
            audio = QAudioOutput()
        # Применяем пользовательскую громкость из настроек
        _vol = max(0.0, min(1.0, S().get("volume", 80, t=int) / 100.0))
        audio.setVolume(_vol)
        player.setAudioOutput(audio)
        player.setSource(QUrl.fromLocalFile(str(sf_path.resolve())))
        player.play()
        _SOUND_PLAYERS.append((player, audio))
        def _cleanup(state):
            from PyQt6.QtMultimedia import QMediaPlayer as _QMP
            if state == _QMP.PlaybackState.StoppedState:
                try: _SOUND_PLAYERS.remove((player, audio))
                except ValueError: pass
        player.playbackStateChanged.connect(_cleanup)
        return
    except Exception as e:
        print(f"[sound] QMediaPlayer failed: {e}, trying subprocess...")
    sys_name = platform.system()
    sf_str = str(sf_path)
    if sys_name == "Linux":
        ext = sf_path.suffix.lower()
        # aplay first (ALSA, no PulseAudio needed), then paplay, ffplay
        if ext == ".wav":
            cmds = [
                ["aplay", "-q", sf_str],
                ["paplay", sf_str],
                ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", sf_str],
            ]
        else:
            # mp3/ogg: ffplay is best (bundled with Qt multimedia)
            cmds = [
                ["ffplay", "-nodisp", "-autoexit", "-loglevel", "quiet", sf_str],
                ["mpg123", "-q", sf_str],
                ["paplay", sf_str],
                ["aplay", "-q", sf_str],
            ]
        for cmd in cmds:
            try:
                subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return
            except FileNotFoundError:
                continue
        print(f"[sound] no player found for {sf_path.name}")
    elif sys_name == "Windows":
        if sf_str.lower().endswith(".wav"):
            try:
                import winsound as _ws
                _ws.PlaySound(sf_str, _ws.SND_FILENAME | _ws.SND_ASYNC)
                return
            except Exception: pass
        subprocess.Popen(["powershell", "-c",
            f"Add-Type -AssemblyName presentationCore;"
            f"$mp=New-Object System.Windows.Media.MediaPlayer;"
            f"$mp.Open([uri]'{sf_str}');Start-Sleep -m 200;$mp.Play();Start-Sleep -m 4000"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    elif sys_name == "Darwin":
        subprocess.Popen(["afplay", sf_str],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# ── Synthetic tone generator via QAudioSink ───────────────────────────────────
# Used when no sound files are found — always works, no external files needed.
def _synth_tone(freqs: list[tuple[float,float,float]], volume: float = 0.35):
    """
    Play a synthesised tone sequence entirely in-process via QAudioSink.
    freqs: list of (frequency_hz, duration_ms, amplitude 0..1)
    No files, no subprocess — pure Python PCM via Qt.
    """
    try:
        import struct, math as _math
        from PyQt6.QtMultimedia import QAudioSink, QAudioFormat
        from PyQt6.QtCore import QByteArray

        fmt = QAudioFormat()
        fmt.setSampleRate(22050)
        fmt.setChannelCount(2)   # stereo — sound in both ears
        fmt.setSampleFormat(QAudioFormat.SampleFormat.Int16)

        pcm = bytearray()
        RATE = 22050
        for freq, dur_ms, amp in freqs:
            n = int(RATE * dur_ms / 1000)
            fade = int(RATE * 0.008)       # 8 ms fade in/out
            for i in range(n):
                env = 1.0
                if i < fade:   env = i / fade
                elif i > n-fade: env = (n - i) / fade
                v = int(amp * env * volume * _math.sin(2 * _math.pi * freq * i / RATE) * 32767)
                # stereo: same sample for L and R
                pcm += struct.pack('<hh', v, v)

        sink = QAudioSink(fmt)
        buf  = QByteArray(bytes(pcm))
        dev  = sink.start()
        dev.write(buf)
        _SOUND_PLAYERS.append(sink)   # keep alive
        # auto-cleanup after estimated duration
        total_ms = int(sum(d for _, d, _ in freqs)) + 200
        QTimer.singleShot(total_ms, lambda: _SOUND_PLAYERS.remove(sink) if sink in _SOUND_PLAYERS else None)
    except Exception as e:
        print(f"[sound] synth failed: {e}")


# Tone profiles for each event
_SYNTH_TONES: dict[str, list[tuple]] = {
    # (freq_hz, duration_ms, amplitude)
    "message":    [(880, 60, 0.6), (0, 20, 0), (1100, 60, 0.5)],
    "call":       [(440, 400, 0.7), (0, 200, 0), (440, 400, 0.7),
                   (0, 200, 0), (440, 400, 0.7)],
    "online":     [(660, 80, 0.5), (880, 120, 0.4)],
    "offline":    [(880, 80, 0.4), (660, 120, 0.3)],
    "error":      [(220, 150, 0.7), (180, 200, 0.6)],
    "user_error": [(330, 100, 0.5), (0, 30, 0), (330, 100, 0.5)],
    "critical":   [(180, 300, 0.8), (0, 100, 0), (180, 300, 0.8)],
    "delete":     [(440, 60, 0.4), (330, 100, 0.3)],
    "pin":        [(880, 50, 0.4), (1100, 80, 0.5)],
    "click":      [(1200, 30, 0.3)],
}

# Freedesktop system sound candidates (Linux)
_FREEDESKTOP_SOUNDS: dict[str, list[str]] = {
    "message": [
        "/usr/share/sounds/freedesktop/stereo/message.oga",
        "/usr/share/sounds/freedesktop/stereo/message-new-instant.oga",
        "/usr/share/sounds/ubuntu/stereo/message-new-instant.ogg",
    ],
    "call": [
        "/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga",
        "/usr/share/sounds/ubuntu/stereo/phone-incoming-call.ogg",
    ],
    "online": [
        "/usr/share/sounds/freedesktop/stereo/service-login.oga",
        "/usr/share/sounds/ubuntu/stereo/service-login.ogg",
    ],
    "offline": [
        "/usr/share/sounds/freedesktop/stereo/service-logout.oga",
    ],
    "error": [
        "/usr/share/sounds/freedesktop/stereo/dialog-error.oga",
        "/usr/share/sounds/ubuntu/stereo/dialog-error.ogg",
    ],
    "critical": [
        "/usr/share/sounds/freedesktop/stereo/dialog-error.oga",
    ],
}


_SOUND_LAST_PLAYED: dict = {}   # event → timestamp, for cooldown

def play_system_sound(event: str = "message"):
    """
    Play sound for a named event. Priority:
      1. Custom file in DATA_DIR/sounds/ or gdfsound/
      2. Freedesktop system sounds (Linux)
      3. Synthetic tone (always works, no files needed)
    """
    if not S().notification_sounds:
        return
    # Cooldown: same event can't play more than once per 300ms (except click: 50ms)
    import time as _t
    now = _t.time()
    cooldown = 0.05 if event == "click" else 0.3
    last = _SOUND_LAST_PLAYED.get(event, 0)
    if now - last < cooldown:
        return
    _SOUND_LAST_PLAYED[event] = now
    try:
        # 1. User sound scheme (читаем актуальную — учитывает настройки)
        for fname in _get_sound_map().get(event, []):
            sf = _find_sound_file(fname)

            if sf:
                _play_file(sf)
                return

        # 2. Freedesktop system sounds (Linux)
        if platform.system() == "Linux":
            for path in _FREEDESKTOP_SOUNDS.get(event, []):
                if Path(path).exists():
                    _play_file(path)
                    return
            # Also try pactl/canberra-gtk-play for themed sounds
            sound_id = {
                "message": "message-new-instant",
                "call":    "phone-incoming-call",
                "online":  "service-login",
                "offline": "service-logout",
                "error":   "dialog-error",
            }.get(event)
            if sound_id:
                for cmd in [
                    ["canberra-gtk-play", "--id", sound_id],
                    ["paplay", f"--property=media.name={sound_id}"],
                ]:
                    try:
                        subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                         stderr=subprocess.DEVNULL)
                        return
                    except FileNotFoundError:
                        continue

        # 3. Windows system sounds
        if platform.system() == "Windows":
            import winsound as _ws
            alias = {"message": "SystemAsterisk", "call": "SystemHand",
                     "online": "SystemDefault", "error": "SystemExclamation",
                     "critical": "SystemCriticalStop"}.get(event, "SystemAsterisk")
            try:
                _ws.PlaySound(alias, _ws.SND_ALIAS | _ws.SND_ASYNC)
                return
            except Exception: pass

        # 4. Synthetic tone — guaranteed fallback
        tones = _SYNTH_TONES.get(event, _SYNTH_TONES["message"])
        _synth_tone(tones)

    except Exception as e:
        print(f"[sound] play_system_sound error ({event}): {e}")
        try:
            _synth_tone(_SYNTH_TONES.get(event, [(880, 80, 0.5)]))
        except Exception:
            pass

def _play_system_sound_compat(event: str = "message"):
    """Legacy alias kept for any remaining call sites."""
    play_system_sound(event)

# ═══════════════════════════════════════════════════════════════════════════
#  IN-APP TOAST NOTIFICATION  (slide-in from bottom-right)
# ═══════════════════════════════════════════════════════════════════════════
# ─────────────────────────────────────────────────────────────────────────────
#  MEDIA OPEN HELPER — always ask: Mewa or system default
# ─────────────────────────────────────────────────────────────────────────────
_MEDIA_PREF: str = ""   # "mewa" | "system" | "" (ask each time)

MEDIA_EXTS = {".mp3",".wav",".ogg",".flac",".aac",".m4a",".opus",".wma",
              ".mp4",".avi",".mkv",".webm",".mov",".m4v",".mpg",".mpeg",
              ".wmv",".3gp",".ts",".m2ts"}

def _open_link(url: str, parent=None):
    """Open a URL from chat — ask WNS or system browser, with remember checkbox."""
    pref = S().link_open_pref

    def _do_wns():
        app = QApplication.instance()
        if app:
            for w in app.topLevelWidgets():
                if hasattr(w, '_wns_player') and hasattr(w, '_tabs'):
                    w._tabs.setCurrentWidget(w._wns_player)
                    wns = w._wns_player
                    if hasattr(wns, '_new_tab'):
                        wns._new_tab(url)
                    return
        _do_system()

    def _do_system():
        _open_system(url)

    if pref == "wns":
        _do_wns(); return
    elif pref == "system":
        _do_system(); return

    # Ask
    t = get_theme(S().theme)
    dlg = QDialog(parent)
    dlg.setWindowTitle(TR("open_link"))
    dlg.setFixedWidth(420)
    dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
    vl = QVBoxLayout(dlg)
    vl.setContentsMargins(20, 16, 20, 16)
    vl.setSpacing(12)

    url_lbl = QLabel(url[:60] + ("…" if len(url) > 60 else ""))
    url_lbl.setStyleSheet(
        f"color:{t['accent']};font-size:11px;background:transparent;"
        "text-decoration:underline;")
    url_lbl.setWordWrap(True)
    vl.addWidget(url_lbl)

    q = QLabel(TR("where_open"))
    q.setStyleSheet(
        f"font-size:13px;font-weight:bold;color:{t['text']};background:transparent;")
    vl.addWidget(q)

    btn_row = QHBoxLayout(); btn_row.setSpacing(8)
    wns_btn = QPushButton(TR("browser_builtin"))
    wns_btn.setObjectName("accent_btn"); wns_btn.setFixedHeight(36)
    sys_btn = QPushButton(TR("browser_system"))
    sys_btn.setFixedHeight(36)
    sys_btn.setStyleSheet(
        f"QPushButton{{background:{t['btn_bg']};color:{t['text']};"
        f"border:1px solid {t['border']};border-radius:8px;}}"
        f"QPushButton:hover{{background:{t['btn_hover']};}}")
    btn_row.addWidget(wns_btn); btn_row.addWidget(sys_btn)
    vl.addLayout(btn_row)

    remember = QCheckBox("Remember choice (change in Settings → Privacy)")
    remember.setStyleSheet(
        f"color:{t['text_dim']};font-size:10px;background:transparent;")
    vl.addWidget(remember)

    _choice = [""]
    def _pick(ch):
        _choice[0] = ch
        if remember.isChecked():
            S().set("link_open_pref", ch)
        dlg.accept()

    wns_btn.clicked.connect(lambda: _pick("wns"))
    sys_btn.clicked.connect(lambda: _pick("system"))
    dlg.exec()

    if _choice[0] == "wns":
        _do_wns()
    elif _choice[0] == "system":
        _do_system()


def _open_media_smart(path: str, parent=None):
    """Open a media file: ask whether to use Mewa or system default.
    Respects remembered choice from settings."""
    global _MEDIA_PREF
    # Load saved pref from settings
    saved = S().get("media_open_pref", "", t=str)
    if saved:
        _MEDIA_PREF = saved

    ext = Path(path).suffix.lower()
    is_media = ext in MEDIA_EXTS

    if not is_media:
        # Not a known media file — open with system default directly
        _open_system(path); return

    if _MEDIA_PREF == "mewa":
        _open_in_mewa(path, parent); return
    elif _MEDIA_PREF == "system":
        _open_system(path); return

    # Ask the user
    from PyQt6.QtWidgets import QDialog, QVBoxLayout, QHBoxLayout, QCheckBox
    t = get_theme(S().theme)
    dlg = QDialog(parent)
    dlg.setWindowTitle(TR("open_media"))
    dlg.setFixedSize(380, 210)
    dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
    vl = QVBoxLayout(dlg)
    vl.setContentsMargins(20,16,20,16); vl.setSpacing(12)

    fname = Path(path).name
    lbl = QLabel(f"<b>{fname}</b><br><br>" + _L("Чем открыть?", "How to open?", "何で開く?"))
    lbl.setTextFormat(Qt.TextFormat.RichText)
    lbl.setWordWrap(True)
    lbl.setStyleSheet(f"font-size:12px;color:{t['text']};background:transparent;")
    vl.addWidget(lbl)

    remember = QCheckBox("Remember my choice (changeable in Settings)")
    remember.setStyleSheet(f"color:{t['text_dim']};font-size:10px;background:transparent;")
    vl.addWidget(remember)

    info = QLabel(_L("Сменить: Настройки → Приватность", "Change: Settings → Privacy", "設定→プライバシー"))
    info.setStyleSheet(f"font-size:9px;color:{t['text_dim']};background:transparent;")
    vl.addWidget(info)

    btn_row = QHBoxLayout(); btn_row.setSpacing(10)

    btn_mewa = QPushButton(TR("player_builtin"))
    btn_mewa.setObjectName("accent_btn"); btn_mewa.setFixedHeight(34)
    btn_sys  = QPushButton(TR("player_system"))
    btn_sys.setFixedHeight(34)
    btn_sys.setStyleSheet(
        f"QPushButton{{background:{t['btn_bg']};color:{t['text']};"
        f"border:1px solid {t['border']};border-radius:8px;}}"
        f"QPushButton:hover{{background:{t['btn_hover']};}}")

    _choice = [""]
    def _pick(ch):
        _choice[0] = ch
        if remember.isChecked():
            S().set("media_open_pref", ch)
            global _MEDIA_PREF; _MEDIA_PREF = ch
        dlg.accept()

    btn_mewa.clicked.connect(lambda: _pick("mewa"))
    btn_sys.clicked.connect(lambda: _pick("system"))
    btn_row.addWidget(btn_mewa); btn_row.addWidget(btn_sys)
    vl.addLayout(btn_row)
    dlg.exec()

    if _choice[0] == "mewa":
        _open_in_mewa(path, parent)
    elif _choice[0] == "system":
        _open_system(path)


def _open_system(path: str):
    """Open file with OS default application."""
    try:
        s = platform.system()
        if s == "Linux":
            subprocess.Popen(["xdg-open", path],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif s == "Windows":
            os.startfile(path)
        elif s == "Darwin":
            subprocess.Popen(["open", path],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as e:
        print(f"[open_system] {e}")


def _open_in_mewa(path: str, parent=None):
    """Add file to Mewa, switch to Mewa tab, and start playback."""
    # Search all top-level windows for MainWindow (has _mewa_player)
    app_inst = QApplication.instance()
    if app_inst:
        for w in app_inst.topLevelWidgets():
            if hasattr(w, '_mewa_player') and hasattr(w, '_tabs'):
                mp = w._mewa_player
                # Switch to Mewa tab first
                w._tabs.setCurrentWidget(mp)
                # open_file handles _add_track + _play_idx (auto-switches to video page)
                mp.open_file(path)
                return
    # Fallback: walk parent hierarchy
    w = parent
    for _ in range(12):
        if w is None: break
        if hasattr(w, '_mewa_player') and hasattr(w, '_tabs'):
            w._tabs.setCurrentWidget(w._mewa_player)
            w._mewa_player.open_file(path)
            return
        w = w.parent() if callable(getattr(w, 'parent', None)) else None
    # Last resort: system open
    _open_system(path)



# ─────────────────────────────────────────────────────────────────────────────
#  MEDIA OPEN HELPER — спрашивает открыть через Mewa или системой

# ═══════════════════════════════════════════════════════════════════════════
#  GoidaCRYPTO — Защищённое локальное хранилище и многоуровневая безопасность
#  Версия 1.0 | GoidaPhone NT 1.8
# ═══════════════════════════════════════════════════════════════════════════

GOIDA_CRYPTO_VERSION = "1.0"

# ── Чувствительные ключи которые шифруются в SecureVault ──────────────────
_VAULT_SENSITIVE_KEYS = {
    "encryption_passphrase",  # пароль E2E шифрования
    "license_code",           # ключ лицензии
    "app_icon_b64",           # кастомная иконка
    "avatar_b64",             # аватар пользователя
    "pin_hash",               # хэш PIN
}

class SecureMemory:
    """
    Оборачивает чувствительную строку и обнуляет память при удалении.
    Предотвращает утечку паролей через дамп памяти.
    """
    def __init__(self, value: str):
        self._data = bytearray(value.encode('utf-8'))

    def get(self) -> str:
        return self._data.decode('utf-8')

    def wipe(self):
        for i in range(len(self._data)):
            self._data[i] = 0
        self._data = bytearray()

    def __del__(self):
        self.wipe()

    def __bool__(self):
        return len(self._data) > 0


class SecureVault:
    """
    GoidaCRYPTO SecureVault — зашифрованное хранилище чувствительных данных.

    АЛГОРИТМ:
      • Ключ хранилища = PBKDF2-HMAC-SHA256(vault_passphrase, salt, 600_000 итераций)
      • Данные шифруются AES-256-GCM с random nonce
      • Файл хранилища: DATA_DIR/vault.gcrypto
      • Целостность: HMAC-SHA256 поверх зашифрованного блоба

    СЛОИ ЗАЩИТЫ:
      Layers 0-1:  Хранилище данных (QSettings / AES-256-GCM + PBKDF2-600k)
      Layers 2-5:  Защита данных, UI и памяти
      Layers 6-10: Поведенческая и оперативная защита
      Layers 11-15: Сетевая и криптографическая защита
      Layers 16-19: Protocolьная защита
      Layer 20:    🔴 Параноидальный режим — всё enabled
    """

    VAULT_FILE    = "vault.gcrypto"
    VAULT_MAGIC   = b"GoidaCRYPTO/1.0\x00"
    PBKDF2_ITERS  = 600_000
    SALT_SIZE     = 32
    NONCE_SIZE    = 12
    TAG_SIZE      = 16

    def __init__(self):
        self._vault_path = DATA_DIR / self.VAULT_FILE
        self._unlocked   = False
        self._vault_key: bytes | None = None
        self._cache: dict = {}
        self._dirty  = False

    # ── Unlock / Lock ─────────────────────────────────────────────────────

    def unlock(self, passphrase: str) -> bool:
        """Открыть хранилище. Возвращает True если пароль верный."""
        if not self._vault_path.exists():
            # Первый запуск — создаём новое хранилище
            self._create_new_vault(passphrase)
            return True
        try:
            raw = self._vault_path.read_bytes()
            if not raw.startswith(self.VAULT_MAGIC):
                return False
            offset = len(self.VAULT_MAGIC)
            salt = raw[offset:offset+self.SALT_SIZE]; offset += self.SALT_SIZE
            nonce = raw[offset:offset+self.NONCE_SIZE]; offset += self.NONCE_SIZE
            ciphertext = raw[offset:]

            key = self._derive_key(passphrase, salt)
            import json as _j
            from cryptography.hazmat.primitives.ciphers.aead import AESGCM
            data = AESGCM(key).decrypt(nonce, ciphertext, self.VAULT_MAGIC + salt)
            self._cache    = _j.loads(data.decode('utf-8'))
            self._vault_key = key
            self._unlocked  = True
            return True
        except Exception:
            return False

    def lock(self):
        if self._dirty:
            self._flush()
        if self._vault_key:
            import ctypes
            buf = (ctypes.c_char * len(self._vault_key)).from_buffer_copy(self._vault_key)
            for i in range(len(buf)): buf[i] = 0
        self._vault_key = None
        self._cache.clear()
        self._unlocked = False

    def is_unlocked(self) -> bool:
        return self._unlocked

    def vault_exists(self) -> bool:
        return self._vault_path.exists()

    # ── Read / Write ──────────────────────────────────────────────────────

    def get(self, key: str, default=None):
        """Прочитать значение из хранилища (только если разблокировано)."""
        if not self._unlocked:
            return default
        return self._cache.get(key, default)

    def set(self, key: str, value) -> bool:
        """Записать значение в хранилище и сохранить на диск."""
        if not self._unlocked:
            return False
        self._cache[key] = value
        self._dirty = True
        return self._flush()

    def delete(self, key: str) -> bool:
        if not self._unlocked:
            return False
        self._cache.pop(key, None)
        self._dirty = True
        return self._flush()

    def all_keys(self) -> list:
        return list(self._cache.keys()) if self._unlocked else []

    # ── Vault management ──────────────────────────────────────────────────

    def change_passphrase(self, old_pass: str, new_pass: str) -> bool:
        """Смена пароля хранилища — перешифровывает всё."""
        if not self.unlock(old_pass):
            return False
        old_cache = dict(self._cache)
        self._create_new_vault(new_pass, old_cache)
        return True

    def destroy(self):
        """Безвозвратно уничтожить хранилище (3-кратная перезапись)."""
        if self._vault_path.exists():
            size = self._vault_path.stat().st_size
            for _ in range(3):
                self._vault_path.write_bytes(
                    __import__('os').urandom(size))
            self._vault_path.unlink()
        self.lock()

    def export_audit_log(self) -> str:
        """Вернуть читаемый отчёт о состоянии хранилища (без значений)."""
        lines = [
            f"GoidaCRYPTO SecureVault v{GOIDA_CRYPTO_VERSION}",
            f"Файл: {self._vault_path}",
            f"Существует: {self.vault_exists()}",
            f"Разблокировано: {self.is_unlocked()}",
            f"PBKDF2 итерации: {self.PBKDF2_ITERS:,}",
            f"Algorithm: AES-256-GCM",
            f"Ключ: PBKDF2-HMAC-SHA256",
        ]
        if self._unlocked:
            lines.append(f"Ключей в хранилище: {len(self._cache)}")
            for k in sorted(self._cache):
                v = self._cache[k]
                preview = (str(v)[:8] + "…") if len(str(v)) > 8 else "***"
                lines.append(f"  • {k}: {preview}")
        return "\n".join(lines)

    # ── Internal ──────────────────────────────────────────────────────────

    def _derive_key(self, passphrase: str, salt: bytes) -> bytes:
        from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
        from cryptography.hazmat.primitives import hashes
        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=32,
            salt=salt,
            iterations=self.PBKDF2_ITERS,
        )
        return kdf.derive(passphrase.encode('utf-8'))

    def _create_new_vault(self, passphrase: str, data: dict | None = None):
        import json as _j
        salt = __import__('os').urandom(self.SALT_SIZE)
        key  = self._derive_key(passphrase, salt)
        payload = _j.dumps(data or {}).encode('utf-8')
        nonce = __import__('os').urandom(self.NONCE_SIZE)
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        ct = AESGCM(key).encrypt(nonce, payload, self.VAULT_MAGIC + salt)
        DATA_DIR.mkdir(parents=True, exist_ok=True)
        self._vault_path.write_bytes(
            self.VAULT_MAGIC + salt + nonce + ct)
        self._vault_key = key
        self._cache     = data or {}
        self._unlocked  = True
        self._dirty     = False

    def _flush(self) -> bool:
        """Сохранить кэш на диск."""
        if not self._vault_key:
            return False
        try:
            import json as _j
            payload = _j.dumps(self._cache).encode('utf-8')
            salt    = self._vault_path.read_bytes()[len(self.VAULT_MAGIC):
                                                    len(self.VAULT_MAGIC)+self.SALT_SIZE] \
                      if self._vault_path.exists() else __import__('os').urandom(self.SALT_SIZE)
            nonce = __import__('os').urandom(self.NONCE_SIZE)
            from cryptography.hazmat.primitives.ciphers.aead import AESGCM
            ct = AESGCM(self._vault_key).encrypt(nonce, payload, self.VAULT_MAGIC + salt)
            self._vault_path.write_bytes(self.VAULT_MAGIC + salt + nonce + ct)
            self._dirty = False
            return True
        except Exception as e:
            print(f"[GoidaCRYPTO] flush error: {e}")
            return False


class IntegrityChecker:
    """
    Проверяет целостность файла настроек через HMAC-SHA256.
    Обнаруживает внешнее вмешательство в конфиг.
    """
    SIG_FILE = "settings.hmac"

    def __init__(self):
        self._sig_path = DATA_DIR / self.SIG_FILE

    def sign(self, key: bytes):
        """Подписать текущий файл настроек."""
        try:
            import hmac as _hmac, hashlib as _hl
            cfg_file = DATA_DIR / "GoidaPhone.ini"
            if not cfg_file.exists():
                # QSettings может использовать другой путь
                import glob
                matches = list(DATA_DIR.glob("*.ini")) + list(DATA_DIR.glob("*.conf"))
                if not matches: return
                cfg_file = matches[0]
            data = cfg_file.read_bytes()
            sig  = _hmac.new(key, data, _hl.sha256).digest()
            self._sig_path.write_bytes(sig)
        except Exception as e:
            print(f"[IntegrityChecker] sign: {e}")

    def verify(self, key: bytes) -> bool | None:
        """Проверить подпись. None = no подписи (первый запуск)."""
        if not self._sig_path.exists():
            return None
        try:
            import hmac as _hmac, hashlib as _hl
            cfg_file = DATA_DIR / "GoidaPhone.ini"
            if not cfg_file.exists():
                import glob
                matches = list(DATA_DIR.glob("*.ini")) + list(DATA_DIR.glob("*.conf"))
                if not matches: return None
                cfg_file = matches[0]
            data     = cfg_file.read_bytes()
            expected = self._sig_path.read_bytes()
            actual   = _hmac.new(key, data, _hl.sha256).digest()
            return _hmac.compare_digest(expected, actual)
        except Exception:
            return None


# Глобальные экземпляры
VAULT   = SecureVault()
ICHECK  = IntegrityChecker()


class ToastNotification(QWidget):
    """
    Beautiful in-app toast that slides in from the bottom-right of the parent window.
    Auto-dismisses after 4 seconds. Click to dismiss early.
    """
    clicked = pyqtSignal()

    _active_toasts: list = []   # class-level stack

    def __init__(self, title: str, body: str, avatar_b64: str = "",
                 parent=None, duration_ms: int = 4000):
        super().__init__(parent,
                         Qt.WindowType.FramelessWindowHint |
                         Qt.WindowType.Tool |
                         Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self._duration = duration_ms
        self._anim_show: QPropertyAnimation | None = None
        self._anim_hide: QPropertyAnimation | None = None
        self._build(title, body, avatar_b64)
        ToastNotification._active_toasts.append(self)
        QTimer.singleShot(duration_ms, self._start_hide)

    def _build(self, title: str, body: str, avatar_b64: str):
        self.setFixedWidth(320)
        t = get_theme(S().theme)

        card = QWidget(self)
        card.setObjectName("toast_card")
        card.setStyleSheet(f"""
            QWidget#toast_card {{
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                    stop:0 {t['bg2']}, stop:1 {t['bg3']});
                border: 1px solid {t['accent']};
                border-radius: 12px;
                border-left: 3px solid {t['accent']};
            }}
        """)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.addWidget(card)

        lay = QHBoxLayout(card)
        lay.setContentsMargins(12, 10, 10, 10)
        lay.setSpacing(10)

        # Avatar
        av_lbl = QLabel()
        av_lbl.setFixedSize(38, 38)
        if avatar_b64:
            try:
                pm = base64_to_pixmap(avatar_b64)
                av_lbl.setPixmap(make_circle_pixmap(pm, 38))
            except Exception:
                av_lbl.setPixmap(default_avatar(title, 38))
        else:
            av_lbl.setPixmap(default_avatar(title, 38))
        lay.addWidget(av_lbl)

        text_col = QVBoxLayout()
        text_col.setSpacing(2)
        title_lbl = QLabel(title)
        title_lbl.setStyleSheet(f"font-weight:bold;font-size:11px;"
                                f"color:{t['text']};background:transparent;")
        title_lbl.setMaximumWidth(220)
        text_col.addWidget(title_lbl)
        body_short = (body[:55] + "…") if len(body) > 55 else body
        body_lbl = QLabel(body_short)
        body_lbl.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        body_lbl.setWordWrap(True)
        body_lbl.setMaximumWidth(220)
        text_col.addWidget(body_lbl)
        lay.addLayout(text_col)

        lay.addStretch()
        close_btn = QPushButton("✕")
        close_btn.setFixedSize(18, 18)
        close_btn.setStyleSheet(f"QPushButton{{background:transparent;border:none;"
                                f"color:{t['text_dim']};font-size:9px;}}"
                                f"QPushButton:hover{{color:{t['text']};}}")
        close_btn.clicked.connect(self._start_hide)
        lay.addWidget(close_btn)

        self.adjustSize()
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def mousePressEvent(self, event):
        self.clicked.emit()
        self._start_hide()

    def show_at(self, parent_widget):
        """Position and animate in."""
        if parent_widget:
            pr = parent_widget.rect()
            gp = parent_widget.mapToGlobal(pr.bottomRight())
            # Stack above existing toasts
            offset_y = 0
            for t in ToastNotification._active_toasts:
                if t is not self and t.isVisible():
                    offset_y += t.height() + 8
            self.move(gp.x() - self.width() - 12,
                      gp.y() - self.height() - 12 - offset_y)
        self.show()
        self._anim_show = QPropertyAnimation(self, b"windowOpacity")
        self._anim_show.setDuration(250)
        self._anim_show.setStartValue(0.0)
        self._anim_show.setEndValue(0.96)
        self._anim_show.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._anim_show.start()

    def _start_hide(self):
        self._anim_hide = QPropertyAnimation(self, b"windowOpacity")
        self._anim_hide.setDuration(300)
        self._anim_hide.setStartValue(self.windowOpacity())
        self._anim_hide.setEndValue(0.0)
        self._anim_hide.setEasingCurve(QEasingCurve.Type.InCubic)
        self._anim_hide.finished.connect(self._cleanup)
        self._anim_hide.start()

    def _cleanup(self):
        if self in ToastNotification._active_toasts:
            ToastNotification._active_toasts.remove(self)
        self.deleteLater()


_MAIN_WINDOW_REF = None   # set in MainWindow.__init__ for toast positioning

def show_toast(title: str, body: str, avatar_b64: str = ""):
    """Show an in-app toast notification anchored to main window."""
    parent = _MAIN_WINDOW_REF
    if not parent:
        return
    toast = ToastNotification(title, body, avatar_b64, parent=None)
    toast.show_at(parent)



class NotificationCenter:
    """
    Централизованная система внутренних уведомлений GoidaPhone.
    Хранит историю, группирует по типам, отправляет toast.
    """
    _instance = None

    @classmethod
    def inst(cls):
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def __init__(self):
        self._items: list[dict] = []   # {ts, type, title, body, read, avatar}
        self._callbacks: list = []
        self._unread = 0

    def push(self, title: str, body: str = "", ntype: str = "info",
             avatar_b64: str = "", show_popup: bool = True):
        """Add notification to history and optionally show toast."""
        import time as _t
        item = {
            "ts":     _t.time(),
            "type":   ntype,    # info | msg | call | system | error
            "title":  title,
            "body":   body,
            "read":   False,
            "avatar": avatar_b64,
        }
        self._items.append(item)
        if len(self._items) > 200:
            self._items.pop(0)
        self._unread += 1

        for cb in self._callbacks:
            try: cb(item)
            except Exception: pass

        if show_popup and S().notifications_enabled:
            show_toast(title, body, avatar_b64)

    def mark_all_read(self):
        for i in self._items:
            i["read"] = True
        self._unread = 0
        for cb in self._callbacks:
            try: cb(None)
            except Exception: pass

    def subscribe(self, cb):
        self._callbacks.append(cb)

    def unsubscribe(self, cb):
        if cb in self._callbacks:
            self._callbacks.remove(cb)

    def recent(self, n: int = 50) -> list:
        return list(reversed(self._items[-n:]))

    @property
    def unread(self) -> int:
        return self._unread


NOTIF = NotificationCenter.inst()


def send_os_notification(title: str, body: str, icon: str = ""):
    """
    Send a native OS desktop notification.
    Windows: win10toast or plyer if available, fallback to tray balloon.
    Linux: notify-send (libnotify).
    macOS: osascript.
    """
    sys_name = platform.system()
    try:
        if sys_name == "Linux":
            # libnotify / notify-send
            cmd = ["notify-send"]
            if icon:
                cmd += ["--icon", icon]
            cmd += [title, body]
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif sys_name == "Windows":
            # Try plyer first (cross-platform notification lib)
            try:
                from plyer import notification as _notif
                _notif.notify(title=title, message=body,
                              app_name=APP_NAME, timeout=5)
            except ImportError:
                # Fallback: try win10toast
                try:
                    from win10toast import ToastNotifier
                    toaster = ToastNotifier()
                    toaster.show_toast(title, body, duration=4, threaded=True)
                except ImportError:
                    # Final fallback: Windows balloon tooltip via QSystemTrayIcon
                    # This is handled by MainWindow._tray
                    pass
        elif sys_name == "Darwin":
            script = f'display notification "{body}" with title "{title}"'
            subprocess.Popen(["osascript", "-e", script],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as e:
        print(f"OS notification error: {e}")

# Global reference to tray icon for fallback notifications (set in MainWindow)
_TRAY_ICON_REF = None

def send_notification(title: str, body: str):
    """Send OS notification with tray fallback."""
    send_os_notification(title, body)
    # Also try tray balloon as fallback
    global _TRAY_ICON_REF
    if _TRAY_ICON_REF and hasattr(_TRAY_ICON_REF, 'showMessage'):
        try:
            _TRAY_ICON_REF.showMessage(title, body,
                QSystemTrayIcon.MessageIcon.Information, 4000)
        except Exception:
            pass

def pixmap_to_base64(pixmap: QPixmap, fmt: str = "PNG") -> str:
    buf = QByteArray()
    buffer = QBuffer(buf)
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    pixmap.save(buffer, fmt)
    return base64.b64encode(buf.data()).decode()


    buf = QByteArray()
    buffer = QBuffer(buf)
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    pixmap.save(buffer, fmt)
    return base64.b64encode(buf.data()).decode()

def base64_to_pixmap(data: str) -> QPixmap:
    raw = base64.b64decode(data)
    pm = QPixmap()
    pm.loadFromData(raw)
    return pm

def make_circle_pixmap(pm: QPixmap, size: int) -> QPixmap:
    """Crop pixmap to circle."""
    scaled = pm.scaled(size, size, Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                       Qt.TransformationMode.SmoothTransformation)
    out = QPixmap(size, size)
    out.fill(Qt.GlobalColor.transparent)
    painter = QPainter(out)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setBrush(QBrush(scaled))
    painter.setPen(Qt.PenStyle.NoPen)
    painter.drawEllipse(0, 0, size, size)
    painter.end()
    return out

def default_avatar(name: str, size: int = 48) -> QPixmap:
    """Generate colored initial avatar."""
    colors = ["#E74C3C","#3498DB","#2ECC71","#9B59B6","#E67E22","#1ABC9C","#F39C12","#E91E63"]
    color = colors[sum(ord(c) for c in name) % len(colors)]
    pm = QPixmap(size, size)
    pm.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pm)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setBrush(QBrush(QColor(color)))
    painter.setPen(Qt.PenStyle.NoPen)
    painter.drawEllipse(0, 0, size, size)
    painter.setPen(QPen(QColor("white")))
    font = QFont("Arial", size // 3, QFont.Weight.Bold)
    painter.setFont(font)
    letter = name[0].upper() if name else "?"
    painter.drawText(QRect(0, 0, size, size), Qt.AlignmentFlag.AlignCenter, letter)
    painter.end()
    return pm

# ═══════════════════════════════════════════════════════════════════════════
#  THEME SYSTEM  (full palette + stylesheet per theme)
# ═══════════════════════════════════════════════════════════════════════════
THEMES = {
    "dark": {
        "label": "Тёмная",
        "bg":        "#323232",
        "bg2":       "#282828",
        "bg3":       "#1E1E1E",
        "border":    "#484848",
        "text":      "#E0E0E0",
        "text_dim":  "#909090",
        "accent":    "#0078D4",
        "accent2":   "#005A9E",
        "btn_bg":    "#4A4A4A",
        "btn_hover": "#5A5A5A",
        "btn_press": "#3A3A3A",
        "item_bg":   "#2E2E2E",
        "item_sel":  "#0063B1",
        "header_bg": "#3C3C3C",
        "msg_own":   "#1A3A5C",
        "msg_other": "#383838",
        "online":    "#2ECC71",
        "offline":   "#E74C3C",
    },
    "light": {
        "label": "Светлая",
        "bg":        "#F0F0F0",
        "bg2":       "#FAFAFA",
        "bg3":       "#FFFFFF",
        "border":    "#C8C8C8",
        "text":      "#1A1A1A",
        "text_dim":  "#707070",
        "accent":    "#0078D4",
        "accent2":   "#005A9E",
        "btn_bg":    "#E0E0E0",
        "btn_hover": "#D0D0D0",
        "btn_press": "#C0C0C0",
        "item_bg":   "#F8F8F8",
        "item_sel":  "#0078D4",
        "header_bg": "#E8E8E8",
        "msg_own":   "#C8E6FA",
        "msg_other": "#EEEEEE",
        "online":    "#27AE60",
        "offline":   "#E74C3C",
    },
    "dark_blue": {
        "label": "Тёмно-синяя",
        "bg":        "#1A2540",
        "bg2":       "#131B30",
        "bg3":       "#0D1220",
        "border":    "#2A3858",
        "text":      "#C8D8FF",
        "text_dim":  "#6878A8",
        "accent":    "#4080FF",
        "accent2":   "#2060DD",
        "btn_bg":    "#2A3A60",
        "btn_hover": "#3A4A70",
        "btn_press": "#1A2A50",
        "item_bg":   "#182038",
        "item_sel":  "#2060CC",
        "header_bg": "#202848",
        "msg_own":   "#1A3060",
        "msg_other": "#1E2840",
        "online":    "#00E676",
        "offline":   "#FF5252",
    },
    "dark_red": {
        "label": "Тёмно-красная",
        "bg":        "#2A1010",
        "bg2":       "#200808",
        "bg3":       "#160404",
        "border":    "#4A2020",
        "text":      "#FFD0D0",
        "text_dim":  "#A06060",
        "accent":    "#CC2020",
        "accent2":   "#AA1010",
        "btn_bg":    "#4A1818",
        "btn_hover": "#5A2828",
        "btn_press": "#3A0808",
        "item_bg":   "#281010",
        "item_sel":  "#AA0000",
        "header_bg": "#381818",
        "msg_own":   "#3A1010",
        "msg_other": "#2A1818",
        "online":    "#00E676",
        "offline":   "#FF5252",
    },
    "gray": {
        "label": "Серая",
        "bg":        "#606060",
        "bg2":       "#505050",
        "bg3":       "#404040",
        "border":    "#707070",
        "text":      "#F0F0F0",
        "text_dim":  "#B0B0B0",
        "accent":    "#909090",
        "accent2":   "#707070",
        "btn_bg":    "#707070",
        "btn_hover": "#808080",
        "btn_press": "#606060",
        "item_bg":   "#585858",
        "item_sel":  "#888888",
        "header_bg": "#686868",
        "msg_own":   "#5A5A7A",
        "msg_other": "#484848",
        "online":    "#90EE90",
        "offline":   "#FF9090",
    },
    "midnight": {
        "label": "Полночь",
        "bg":        "#0D0D1A",
        "bg2":       "#080810",
        "bg3":       "#040408",
        "border":    "#1A1A3A",
        "text":      "#B0B8FF",
        "text_dim":  "#5058A0",
        "accent":    "#6040FF",
        "accent2":   "#4020DD",
        "btn_bg":    "#151528",
        "btn_hover": "#202040",
        "btn_press": "#0A0A18",
        "item_bg":   "#0E0E20",
        "item_sel":  "#4030CC",
        "header_bg": "#121224",
        "msg_own":   "#120A30",
        "msg_other": "#0E0E22",
        "online":    "#00FFB0",
        "offline":   "#FF4060",
    },
    "forest": {
        "label": "Лес",
        "bg":        "#1A2A1A",
        "bg2":       "#122012",
        "bg3":       "#0A160A",
        "border":    "#2A3E2A",
        "text":      "#C8EEC8",
        "text_dim":  "#6A8A6A",
        "accent":    "#40AA40",
        "accent2":   "#208820",
        "btn_bg":    "#1E321E",
        "btn_hover": "#284228",
        "btn_press": "#142214",
        "item_bg":   "#162616",
        "item_sel":  "#308830",
        "header_bg": "#1E301E",
        "msg_own":   "#143014",
        "msg_other": "#182018",
        "online":    "#80FF80",
        "offline":   "#FF6060",
    },
    "win95": {
        # ── GoidaPhone 1.7543 — Windows 95 Easter Egg theme ──────────────
        # Activate via: Настройки → Темы → 1.7543
        # The name "1.7543" is the previous version number of GoidaPhone.
        "label": "1.7543",
        "bg":        "#C0C0C0",   # classic Win95 grey
        "bg2":       "#D4D0C8",   # slightly lighter
        "bg3":       "#FFFFFF",   # white input fields
        "border":    "#808080",   # dark grey border (sunken)
        "text":      "#000000",   # pure black text
        "text_dim":  "#444444",
        "accent":    "#000080",   # Win95 title bar blue
        "accent2":   "#000060",
        "btn_bg":    "#C0C0C0",
        "btn_hover": "#D4D0C8",
        "btn_press": "#B0B0B0",
        "item_bg":   "#FFFFFF",
        "item_sel":  "#000080",
        "header_bg": "#000080",   # navy title bars
        "msg_own":   "#E0E8FF",   # soft blue for own messages
        "msg_other": "#F0F0F0",   # near-white for others
        "online":    "#008000",
        "offline":   "#FF0000",
        # Extra Win95 feel is applied via special CSS below
        "__win95__": True,
    },
}

# Градиентные темы — bg поддерживает qlineargradient
GRADIENT_THEMES = {
    "aurora": {
        "label": "🌌 Aurora",
        "bg":        "#1a1a2e",
        "bg2":       "#16213e",
        "bg3":       "#0f3460",
        "border":    "#533483",
        "text":      "#e0e0ff",
        "text_dim":  "#8888bb",
        "accent":    "#e94560",
        "accent2":   "#c73652",
        "btn_bg":    "#1f2a4a",
        "btn_hover": "#2a3a6a",
        "btn_press": "#0f1f3a",
        "bubble_own":"#e9456022",
        "bubble_other":"#0f346022",
        "input_bg":  "#16213e",
        "gradient_start": "#1a1a2e",
        "gradient_end":   "#0f3460",
        "gradient_angle": "180",
    },
    "sunset": {
        "label": "🌅 Sunset",
        "bg":        "#1a0a0a",
        "bg2":       "#2d1515",
        "bg3":       "#3d2020",
        "border":    "#7a3030",
        "text":      "#ffd0c0",
        "text_dim":  "#b07060",
        "accent":    "#ff6b35",
        "accent2":   "#e55a25",
        "btn_bg":    "#3d2020",
        "btn_hover": "#5a3030",
        "btn_press": "#2d1515",
        "bubble_own":"#ff6b3530",
        "bubble_other":"#3d202040",
        "input_bg":  "#2d1515",
        "gradient_start": "#2d0a0a",
        "gradient_end":   "#1a0a2d",
        "gradient_angle": "135",
    },
    "ocean": {
        "label": "🌊 Ocean",
        "bg":        "#020f1a",
        "bg2":       "#041828",
        "bg3":       "#062038",
        "border":    "#0e4d6e",
        "text":      "#c0e8ff",
        "text_dim":  "#5090b0",
        "accent":    "#00b4d8",
        "accent2":   "#0096b4",
        "btn_bg":    "#062038",
        "btn_hover": "#0a3050",
        "btn_press": "#041828",
        "bubble_own":"#00b4d830",
        "bubble_other":"#06203840",
        "input_bg":  "#041828",
        "gradient_start": "#020f1a",
        "gradient_end":   "#041828",
        "gradient_angle": "160",
    },
    "neon": {
        "label": "⚡ Neon",
        "bg":        "#0a0a0a",
        "bg2":       "#111111",
        "bg3":       "#1a1a1a",
        "border":    "#333333",
        "text":      "#f0f0f0",
        "text_dim":  "#888888",
        "accent":    "#00ff88",
        "accent2":   "#00cc66",
        "btn_bg":    "#1a1a1a",
        "btn_hover": "#222222",
        "btn_press": "#111111",
        "bubble_own":"#00ff8825",
        "bubble_other":"#1a1a1a80",
        "input_bg":  "#111111",
        "gradient_start": "#050510",
        "gradient_end":   "#0a0a0a",
        "gradient_angle": "180",
    },
    "sakura": {
        "label": "🌸 Sakura",
        "bg":        "#1a0a12",
        "bg2":       "#280f1e",
        "bg3":       "#38182c",
        "border":    "#6a3050",
        "text":      "#ffd0e8",
        "text_dim":  "#b07090",
        "accent":    "#ff6eb4",
        "accent2":   "#e050a0",
        "btn_bg":    "#38182c",
        "btn_hover": "#502040",
        "btn_press": "#280f1e",
        "bubble_own":"#ff6eb430",
        "bubble_other":"#38182c40",
        "input_bg":  "#280f1e",
        "gradient_start": "#1a0a12",
        "gradient_end":   "#0a121a",
        "gradient_angle": "145",
    },
}
# Объединяем в один словарь
THEMES.update(GRADIENT_THEMES)

def get_theme(name: str) -> dict:
    base = THEMES.get("dark", {})
    theme = THEMES.get(name, base)
    return {**base, **theme}  # гарантируем все ключи

def build_stylesheet(t: dict) -> str:
    """Build a complete Qt stylesheet from a theme dict."""
    if t.get("__win95__"):
        return _build_win95_stylesheet(t)
    # Мержим с дефолтной тёмной темой — чтобы никогда не было KeyError
    _fallback = THEMES.get("dark", {})
    t = {**_fallback, **t}
    # Градиентные темы — подменяем bg на qlineargradient строку
    if t.get("gradient_start") and t.get("gradient_end"):
        import math
        rad = math.radians(int(t.get("gradient_angle", 180)))
        x2 = round(math.sin(rad) * 0.5 + 0.5, 3)
        y2 = round(-math.cos(rad) * 0.5 + 0.5, 3)
        t['_bg_gradient'] = (
            f"qlineargradient(x1:0,y1:0,x2:{x2},y2:{y2},"
            f"stop:0 {t['gradient_start']},stop:1 {t['gradient_end']})")
    return _build_modern_stylesheet(t)


def _build_win95_stylesheet(t: dict) -> str:
    """Classic Windows 95 stylesheet — raised/sunken borders, no rounded corners."""
    return f"""
/* ════════════════════════════════════════════════════
   GoidaPhone 1.7543 — Windows 95 theme
   Easter Egg: "Привет из 1995 года"
   ════════════════════════════════════════════════════ */
QWidget {{
    background-color: {t['bg']};
    color: {t['text']};
    font-family: "MS Sans Serif", "Tahoma", "Arial", sans-serif;
    font-size: 11px;
}}
QMainWindow, QDialog {{
    background-color: {t['bg']};
}}
/* Raised button — classic Win95 look */
QPushButton {{
    background-color: {t['btn_bg']};
    color: {t['text']};
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: 2px solid #808080;
    border-right: 2px solid #808080;
    border-radius: 0px;
    padding: 4px 10px;
    font-size: 11px;
}}
QPushButton:hover {{
    background-color: {t['btn_hover']};
}}
QPushButton:pressed {{
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-bottom: 2px solid #FFFFFF;
    border-right: 2px solid #FFFFFF;
    background-color: {t['btn_press']};
}}
QPushButton:disabled {{
    color: #808080;
    background-color: {t['bg']};
}}
QPushButton#accent_btn {{
    background-color: {t['accent']};
    color: white;
    font-weight: bold;
}}
QPushButton#danger_btn {{
    background-color: #CC0000;
    color: white;
}}
/* Sunken input fields */
QLineEdit, QPlainTextEdit, QTextEdit {{
    background-color: {t['bg3']};
    color: {t['text']};
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-bottom: 2px solid #FFFFFF;
    border-right: 2px solid #FFFFFF;
    border-radius: 0px;
    padding: 3px 5px;
    selection-background-color: {t['accent']};
    selection-color: white;
}}
QTextBrowser {{
    background-color: {t['bg3']};
    color: {t['text']};
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-bottom: 2px solid #FFFFFF;
    border-right: 2px solid #FFFFFF;
    border-radius: 0px;
    padding: 4px;
}}
QComboBox {{
    background-color: {t['btn_bg']};
    color: {t['text']};
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: 2px solid #808080;
    border-right: 2px solid #808080;
    border-radius: 0px;
    padding: 3px 6px;
}}
QComboBox QAbstractItemView {{
    background-color: {t['bg3']};
    color: {t['text']};
    border: 2px solid #808080;
    selection-background-color: {t['accent']};
    selection-color: white;
}}
QListWidget {{
    background-color: {t['bg3']};
    color: {t['text']};
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-bottom: 2px solid #FFFFFF;
    border-right: 2px solid #FFFFFF;
    border-radius: 0px;
    outline: none;
}}
QListWidget::item {{
    background-color: transparent;
    padding: 3px 4px;
}}
QListWidget::item:selected {{
    background-color: {t['accent']};
    color: white;
}}
QTabWidget::pane {{
    border: 2px solid #808080;
    background-color: {t['bg']};
    border-radius: 0px;
}}
QTabBar::tab {{
    background-color: {t['bg']};
    color: {t['text']};
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: none;
    border-right: 1px solid #808080;
    border-radius: 0px;
    padding: 4px 12px;
    margin-right: 2px;
}}
QTabBar::tab:selected {{
    background-color: {t['bg']};
    font-weight: bold;
    margin-top: -2px;
    padding-top: 6px;
}}
QGroupBox {{
    font-weight: bold;
    color: {t['text']};
    border: 2px solid #808080;
    border-radius: 0px;
    margin-top: 10px;
    padding-top: 10px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 4px;
    background-color: {t['bg']};
}}
QScrollBar:vertical {{
    background: {t['bg']};
    width: 16px;
    border: 1px solid #808080;
}}
QScrollBar::handle:vertical {{
    background: {t['btn_bg']};
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: 2px solid #808080;
    border-right: 2px solid #808080;
    min-height: 20px;
}}
QScrollBar::add-line:vertical {{
    background: {t['btn_bg']};
    height: 16px;
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: 2px solid #808080;
    border-right: 2px solid #808080;
    subcontrol-position: bottom;
    subcontrol-origin: scroll;
}}
QScrollBar::sub-line:vertical {{
    background: {t['btn_bg']};
    height: 16px;
    border-top: 2px solid #FFFFFF;
    border-left: 2px solid #FFFFFF;
    border-bottom: 2px solid #808080;
    border-right: 2px solid #808080;
    subcontrol-position: top;
    subcontrol-origin: scroll;
}}
QMenuBar {{
    background-color: {t['bg']};
    color: {t['text']};
    border-bottom: 1px solid #808080;
}}
QMenuBar::item:selected {{
    background-color: {t['accent']};
    color: white;
}}
QMenu {{
    background-color: {t['bg']};
    color: {t['text']};
    border: 2px solid #808080;
}}
QMenu::item:selected {{
    background-color: {t['accent']};
    color: white;
}}
QStatusBar {{
    background-color: {t['bg']};
    color: {t['text']};
    border-top: 1px solid #808080;
}}
QLabel#section_header {{
    background-color: {t['header_bg']};
    color: white;
    padding: 6px 8px;
    font-weight: bold;
}}
QProgressBar {{
    background-color: {t['bg3']};
    border-top: 2px solid #808080;
    border-left: 2px solid #808080;
    border-bottom: 2px solid #FFFFFF;
    border-right: 2px solid #FFFFFF;
    border-radius: 0px;
    text-align: center;
    color: {t['text']};
}}
QProgressBar::chunk {{
    background-color: {t['accent']};
}}
"""


def _build_modern_stylesheet(t: dict) -> str:
    """Modern stylesheet with depth, gradients, and rounded corners."""
    # Градиент: подменяем bg в итоговом CSS если тема его поддерживает
    _bg_val = t.get('_bg_gradient', t['bg'])
    t = dict(t); t['bg'] = _bg_val
    return f"""
/* ── Global ── */
QWidget {{
    background-color: {t['bg']};
    color: {t['text']};
    font-family: "Segoe UI", "Ubuntu", "Noto Sans", sans-serif;
    font-size: 9pt;
}}
QMainWindow, QDialog {{
    background-color: {t.get('_bg_gradient', t['bg'])};
}}
QMainWindow > QWidget#central_widget {{
    background-color: {t.get('_bg_gradient', t['bg'])};
}}

/* ── Buttons ── */
QPushButton {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['btn_hover']}, stop:1 {t['btn_bg']});
    color: {t['text']};
    border: 1px solid {t['border']};
    border-bottom: 2px solid rgba(0,0,0,89);
    border-radius: 7px;
    padding: 5px 12px;
    font-size: 9pt;
}}
QPushButton:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['accent']}, stop:1 {t['btn_hover']});
    border-color: {t['accent']};
    color: white;
}}
QPushButton:pressed {{
    background-color: {t['btn_press']};
    border-bottom-width: 1px;
    padding-top: 6px;
}}
QPushButton:disabled {{
    background-color: {t['bg2']};
    color: {t['text_dim']};
    border-color: {t['border']};
    border-bottom-width: 1px;
}}
QPushButton#accent_btn {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['accent']}, stop:1 {t['accent2']});
    color: white;
    border: 1px solid rgba(255,255,255,38);
    border-bottom: 3px solid rgba(0,0,0,102);
    font-weight: bold;
    border-radius: 8px;
}}
QPushButton#accent_btn:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['btn_hover']}, stop:1 {t['accent']});
}}
QPushButton#accent_btn:pressed {{
    border-bottom-width: 1px;
    padding-top: 6px;
}}
QPushButton#danger_btn {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #E03333, stop:1 #AA1111);
    color: white;
    border: 1px solid rgba(255,100,100,76);
    border-bottom: 2px solid rgba(0,0,0,102);
    border-radius: 7px;
}}
QPushButton#danger_btn:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #FF4444, stop:1 #CC2222);
}}

/* ── Inputs ── */
QLineEdit, QPlainTextEdit, QTextEdit {{
    background-color: {t['bg3']};
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    padding: 4px 7px;
    selection-background-color: {t['accent']};
}}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus {{
    border-color: {t['accent']};
}}
QTextBrowser {{
    background-color: {t['bg2']};
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    padding: 4px;
}}
QComboBox {{
    background-color: {t['btn_bg']};
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    padding: 4px 8px;
}}
QComboBox::drop-down {{
    border: none;
}}
QComboBox QAbstractItemView {{
    background-color: {t['bg2']};
    color: {t['text']};
    selection-background-color: {t['accent']};
    border: 1px solid {t['border']};
}}
QSpinBox {{
    background-color: {t['bg3']};
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    padding: 3px;
}}

/* ── Lists ── */
QListWidget {{
    background-color: {t['bg2']};
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    outline: none;
}}
QListWidget::item {{
    background-color: {t['item_bg']};
    border-bottom: 1px solid {t['border']};
    padding: 4px 6px;
    border-radius: 0px;
}}
QListWidget::item:selected {{
    background-color: {t['item_sel']};
    color: white;
}}
QListWidget::item:hover:!selected {{
    background-color: {t['btn_hover']};
}}

/* ── Tabs ── */
QTabWidget::pane {{
    border: 1px solid {t['border']};
    background-color: {t['bg2']};
    border-radius: 0 4px 4px 4px;
}}
QTabBar::tab {{
    background-color: {t['bg']};
    color: {t['text_dim']};
    border: 1px solid {t['border']};
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    padding: 3px 12px;
    margin-right: 2px;
    font-size: 9pt;
    min-height: 0px;
    max-height: 20px;
}}
QTabBar::tab:selected {{
    background-color: {t['bg2']};
    color: {t['text']};
    border-bottom: 1px solid {t['bg2']};
}}
QTabBar::tab:hover:!selected {{
    background-color: {t['btn_hover']};
    color: {t['text']};
}}

/* ── GroupBox ── */
QGroupBox {{
    font-weight: bold;
    color: {t['text']};
    border: 1px solid {t['border']};
    border-radius: 6px;
    margin-top: 10px;
    padding-top: 10px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 0 6px;
    color: {t['accent']};
    background-color: {t['bg']};
}}

/* ── ScrollBar ── */
QScrollBar:vertical {{
    background: {t['bg2']};
    width: 8px;
    border-radius: 4px;
}}
QScrollBar::handle:vertical {{
    background: {t['border']};
    border-radius: 4px;
    min-height: 20px;
}}
QScrollBar::handle:vertical:hover {{
    background: {t['accent']};
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0;
}}
QScrollBar:horizontal {{
    background: {t['bg2']};
    height: 8px;
    border-radius: 4px;
}}
QScrollBar::handle:horizontal {{
    background: {t['border']};
    border-radius: 4px;
    min-width: 20px;
}}
QScrollBar::handle:horizontal:hover {{
    background: {t['accent']};
}}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {{
    width: 0;
}}

/* ── Menu ── */
QMenuBar {{
    background-color: {t['header_bg']};
    color: {t['text']};
    border-bottom: 1px solid {t['border']};
    padding: 2px;
}}
QMenuBar::item {{
    background: transparent;
    padding: 4px 10px;
    border-radius: 3px;
}}
QMenuBar::item:selected {{
    background-color: {t['accent']};
    color: white;
}}
QMenu {{
    background-color: {t['bg2']};
    color: {t['text']};
    border: 1px solid {t['border']};
}}
QMenu::item {{
    padding: 5px 20px;
}}
QMenu::item:selected {{
    background-color: {t['accent']};
    color: white;
}}
QMenu::separator {{
    height: 1px;
    background: {t['border']};
    margin: 3px 0;
}}

/* ── StatusBar ── */
QStatusBar {{
    background-color: {t['header_bg']};
    color: {t['text_dim']};
    border-top: 1px solid {t['border']};
    font-size: 10px;
}}

/* ── ToolTip ── */
QToolTip {{
    background-color: {t['bg2']};
    color: {t['text']};
    border: 1px solid {t['accent']};
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 10px;
}}

/* ── Slider ── */
QSlider::groove:horizontal {{
    background: {t['border']};
    height: 4px;
    border-radius: 2px;
}}
QSlider::handle:horizontal {{
    background: {t['accent']};
    width: 14px;
    height: 14px;
    margin: -5px 0;
    border-radius: 7px;
}}
QSlider::sub-page:horizontal {{
    background: {t['accent']};
    border-radius: 2px;
}}

/* ── CheckBox ── */
QCheckBox {{
    color: {t['text']};
    spacing: 8px;
    font-size: 12px;
}}
QCheckBox::indicator {{
    width: 16px;
    height: 16px;
    border: 2px solid {t['border']};
    border-radius: 4px;
    background: {t['bg3']};
}}
QCheckBox::indicator:hover {{
    border-color: {t['accent']};
    background: {t['bg2']};
}}
QCheckBox::indicator:checked {{
    background: {t['accent']};
    border-color: {t['accent']};
    border-bottom: 3px solid rgba(255,255,255,127);
    border-right: 3px solid rgba(255,255,255,127);
    border-top: 2px solid {t['accent']};
    border-left: 2px solid {t['accent']};
}}

/* ── ProgressBar ── */
QProgressBar {{
    background-color: {t['bg3']};
    border: 1px solid {t['border']};
    border-radius: 4px;
    text-align: center;
    color: {t['text']};
    height: 16px;
}}
QProgressBar::chunk {{
    background-color: {t['accent']};
    border-radius: 3px;
}}

/* ── Splitter ── */
QSplitter::handle {{
    background: {t['border']};
    width: 2px;
    height: 2px;
}}

/* ── Labels ── */
QLabel#section_header {{
    background-color: {t['header_bg']};
    color: {t['text']};
    padding: 8px 10px;
    border-bottom: 1px solid {t['border']};
    font-weight: bold;
    font-size: 12px;
}}
QLabel#user_status_online {{
    color: {t['online']};
    font-size: 10px;
}}
QLabel#user_status_offline {{
    color: {t['offline']};
    font-size: 10px;
}}

/* ── Chat panel header (depth effect) ── */
QWidget#chat_header {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['header_bg']}, stop:1 {t['bg2']});
    border-bottom: 2px solid {t['border']};
    border-radius: 0px;
}}
QWidget#chat_header_top {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['header_bg']}, stop:1 {t['bg2']});
    border-bottom: 2px solid {t['border']};
    border-radius: 8px 8px 0 0;
}}

/* ── Peer list items ── */
QWidget#peer_item {{
    border-radius: 6px;
    padding: 2px;
}}
QWidget#peer_item:hover {{
    background-color: {t['btn_hover']};
}}

/* ── Input area (depth) ── */
QWidget#input_area {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['bg2']}, stop:1 {t['bg']});
    border-top: 1px solid {t['border']};
}}

/* ── Sidebar ── */
QWidget#sidebar {{
    background-color: {t['bg2']};
    border-right: 1px solid {t['border']};
}}

/* ── Call bar ── */
QWidget#call_bar_active {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 #1A4A1A, stop:1 #123012);
    border-top: 1px solid #2A5A2A;
    border-bottom: 1px solid #1A3A1A;
}}

/* ── Message reactions ── */
QPushButton#reaction_btn {{
    background-color: {t['bg3']};
    border: 1px solid {t['border']};
    border-radius: 10px;
    padding: 2px 6px;
    font-size: 12px;
    min-width: 30px;
}}
QPushButton#reaction_btn:hover {{
    background-color: {t['btn_hover']};
    border-color: {t['accent']};
}}

/* ── Launcher screen ── */
QWidget#launcher_screen {{
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
        stop:0 {t['bg3']}, stop:0.5 {t['bg2']}, stop:1 {t['bg']});
}}
QPushButton#launcher_btn {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['btn_hover']}, stop:1 {t['btn_bg']});
    border: 2px solid {t['accent']};
    border-radius: 12px;
    padding: 20px;
    font-size: 14px;
    font-weight: bold;
    color: {t['text']};
    min-width: 180px;
    min-height: 120px;
}}
QPushButton#launcher_btn:hover {{
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
        stop:0 {t['accent']}, stop:1 {t['accent2']});
    color: white;
}}

/* ── Unread badge ── */
QLabel#unread_badge {{
    background-color: {t['accent']};
    color: white;
    border-radius: 9px;
    padding: 1px 5px;
    font-size: 9px;
    font-weight: bold;
    min-width: 16px;
}}
"""

# ═══════════════════════════════════════════════════════════════════════════
#  LANGUAGE / LOCALIZATION  (п.35 — Русский / English)
# ═══════════════════════════════════════════════════════════════════════════
_STRINGS_RU = {
    "app_title": "GoidaPhone",
    "online": "Онлайн",
    "offline": "Офлайн",
    "public_chat": "💬 Общий чат",
    "all_users": "Все пользователи в сети",
    "private_chat": "Личный чат",
    "type_message": "Введите сообщение...",
    "send": "Отправить",
    "call": "Звонок",
    "hangup": "Завершить звонок",
    "attach": "Файл",
    "emoji": "Эмодзи",
    "stickers": "Стикеры",
    "typing": "печатает...",
    "you": "Это вы",
    "call_started": "Звонок начат",
    "call_ended": "Звонок завершён",
    "active_call": "📞 Вы в звонке",
    "mute_mic": "🎤 Микрофон",
    "muted": "🔇 Микрофон выкл",
    "users": "👥 Пользователи",
    "groups": "📂 Группы",
    "search": "🔍 Поиск...",
    "create_group": "➕ Создать группу",
    "new_group": "Новая группа",
    "group_name": "Название группы:",
    "group_created": "Группа создана.",
    "add_members": "Добавьте участников через контекстное меню.",
    "personal_chat": "💬 Личный чат",
    "call_peer": "📞 Позвонить",
    "send_file": "📎 Отправить файл",
    "add_to_group": "➕ Добавить в группу",
    "first_create_group": "Сначала создайте группу.",
    "groups_label": "Группы:",
    "msg_file": "📎 Файл",
    "msg_image": "🖼 Изображение",
    "msg_edit_hint": "(Изменено)",
    "msg_forwarded": "↪ Переслать",
    "msg_reaction_add": "Добавить реакцию",
    "msg_copy": "📋 Копировать",
    "msg_edit": "✏ Редактировать",
    "msg_delete": "🗑 Удалить",
    "msg_forward": "↪ Переслать",
    "msg_reply": "↩ Ответить",
    "msg_reactions": "😊 Реакция",
    "notif_new_message": "Новое сообщение",
    "notif_incoming_call": "Входящий звонок",
    "notif_file_received": "Получен файл",
    "notif_user_online": "пользователь online",
    "settings": "Настройки GoidaPhone",
    "tab_audio": "🎵 Аудио",
    "tab_network": "🌐 Сеть",
    "tab_themes": "🎨 Темы",
    "tab_license": "👑 Лицензия",
    "tab_data": "💾 Данные",
    "tab_specialist": "🔧 Для специалистов",
    "tab_language": "🌍 Язык",
    "save": "💾 Сохранить",
    "close": "Закрыть",
    "cancel": "Отмена",
    "yes": "Да",
    "no": "Нет",
    "ok": "OK",
    "saved": "Настройки сохранены!",
    "my_profile": "👤 Мой профиль",
    "username": "Имя пользователя:",
    "bio": "О себе:",
    "avatar": "Аватар",
    "change_avatar": "📷 Сменить аватар",
    "banner": "Баннер профиля",
    "nickname_color": "Цвет имени:",
    "custom_emoji": "Эмодзи рядом с именем:",
    "profile_saved": "Профиль сохранён!",
    "incoming_call": "звонит вам",
    "accept": "✅ Принять",
    "reject": "❌ Отклонить",
    "launcher_title": "GoidaPhone",
    "launcher_subtitle": "Добро пожаловать",
    "launcher_gui": "🖥 Запустить графический интерфейс",
    "launcher_cmd": "⌨ Запустить консольный режим",
    "launcher_gui_hint": "Полный интерфейс с чатом, звонками и файлами",
    "launcher_cmd_hint": "Для серверов, диагностики и автоматизации",
    "about": "О GoidaPhone",
    "check_updates": "🔄 Проверить обновления",
    "update_found": "🚀 Доступна версия",
    "update_available_title": "Доступно обновление!",
    "update_now": "⬇ Обновить",
    "no_updates": "✅ Обновлений не найдено.",
    "update_error": "❌ Ошибка:",
    "network_error": "Не удалось запустить сетевые службы. Проверьте, не заняты ли порты другими программами.",
    "file_send_error": "Ошибка отправки файла",
    "no_image": "Не удалось загрузить изображение.",
    "cmd_clear_done": "Чат очищен.",
    "cmd_help": "Команды: /clear /help /me /ping /version /ttl /poll /notes /schedule /tr /translate и тд...",
    "cmd_me": "действует",
    "cmd_ping": "Pong! (будет доступно в летнем обновлении)",
    "cmd_unknown": "Неизвестная команда. Введите /help для списка команд.",
    "searching": "🔍 Поиск...",
    "no_calls": "📞 Нет активных звонков",
    "mic_on": "🎤 Микрофон вкл",
    "mic_off": "🔇 Микрофон выкл",
    "premium_label": "👑 Премиум",
    "menu_file": "Файл",
    "menu_view": "Вид",
    "menu_themes": "🎨 Темы",
    "menu_calls": "Звонки",
    "menu_help": "Справка",
    "open_link": "Открыть ссылку",
    "where_open": "Где открыть ссылку?",
    "browser_builtin": "🌐 WNS (Встроенный)",
    "browser_system": "🖥 Системный браузер",
    "open_media": "Открыть медиа",
    "player_builtin": "🎵 Mewa (Встроенный)",
    "player_system": "📂 Системный плеер",
    "update_platform": "Выберите платформу:",
    "update_release_page": "🌐 Открыть страницу релиза",
    "btn_later": "Позже",
    "launcher_choose": "Выберите режим запуска",
    "launcher_hint": "← → стрелки  •  Enter запуск",
    "launcher_run": "Запуск ▶",
    "launcher_help": "❓ Помощь",
    "btn_close_lbl": "Закрыть",
    "loading": "Загрузка",
    "online_since": "🕐 Онлайн с",
    "premium_user": "👑 Премиум пользователь",
    "group_invite_card": "📂 Приглашение в группу",
    "already_in_group": "✅ Вы уже в группе",
    "btn_join": "👋  Вступить",
    "joined_group": "✅ Вы вступили!",
    "img_hint": "Клик для просмотра · ПКМ сохранить",
    "video_hint": "Клик для воспроизведения",
    "scroll_hint": "Скролл — масштаб  •  ЛКМ — перетащить",
    "new_messages": "↓ Новое сообщение",
    "reply_cancelled": "Ответ отменён",
    "call_floating": "📞 Активный звонок",
    "call_no_peer": "Нет активного пира",
    "call_muted": "🔇 Микрофон выкл",
    "call_unmuted": "🎤 Микрофон",
    "settings_restart": "Для применения нужен перезапуск",
    "theme_changed": "Тема изменена",
    "theme_back": "← Вернуть",
    "theme_restart": "↺ Перезапуск",
    "file_received_msg": "файл получен",
    "save_file": "💾 Сохранить",
    "open_file": "Открыть файл",
    "history_empty": "История пуста",
    "no_messages": "Нет сообщений",
    "quicksetup_title": "⚡ Быстрая настройка",
    "quicksetup_q1": "Как вас звать?",
    "quicksetup_q2": "Выберите цвет имени",
    "quicksetup_q3": "Выберите тему",
    "quicksetup_q4": "Включить звуки?",
    "quicksetup_q5": "Сохранять историю?",
    "quicksetup_q6": "Показывать сплеш при запуске?",
    "quicksetup_done": "Всё настроено! 🎉",
    "tutorial_skip": "✕ Завершить",
    "tutorial_next": "Дальше →",
    "tutorial_finish": "Завершить ✓",
    "tutorial_back": "← Назад",
    "connecting": "Подключение...",
    "connected": "Подключён",
    "disconnected": "Нет соединения",
    "network_error_short": "Ошибка сети",
    "tab_appearance": "🖼 Внешний вид",
    "tab_security": "🔒 Блокировка",
    "tab_privacy": "🛡 Приватность",
    "tab_calls": "📞 Звонки",
    "tab_sounds": "🔔 Звуки",
    "btn_save": "💾 Сохранить",
    "btn_close": "Закрыть",
    "btn_cancel": "Отмена",
    "lbl_online_users": "Пользователи online",
    "no_users": "Нет пользователей в сети",
    "incoming_call_from": "Исходящий звонок от",
    "call_ended_msg": "Звонок завершён",
    "call_started_msg": "Звонок начат",
    "msg_deleted": "Сообщение удалено",
    "msg_edited": "изменено",
    "file_received": "Файл получен",
    "image_received": "Изображение получено",
    "tab_users": "👥 Пользователи",
    "tab_groups": "📂 Группы",
    "tab_chat_pub": "💬 Чат",
    "open_with_what": "Чем открыть медиафайл?",
    "help_title": "Справка — GoidaPhone",
    "help_modes_title": "📖  Справка по режимам запуска",
    "splash_loading": "Загрузка",
    "scroll_zoom_hint": "Скролл — масштаб  •  ЛКМ перетащить",
    "btn_view": "🔍 Открыть",
    "btn_play": "▶ Воспроизвести",
    "note_saved": "Заметка сохранена",
    "note_placeholder": "Напишите заметку...",
    "search_msgs": "Поиск сообщений...",
    "pin_enter": "Введите PIN-код",
    "pin_wrong": "Неверный PIN-код",
    "sticker_label": "Стикер",
    "edited_label": "изменено",
    "forwarded_label": "Переслано",
    "update_available_h": "🚀 Доступно обновление!",
    "launcher_arrows": "← → стрелки  •  Enter запуск",
    "premium_user_lbl": "👑 Премиум",
    "goidaid_active": "GoidaID режим активен. IP скрыт",
    "_auto_000": "Запомнить мой выбор (Изменить в настройках → Приватность)",
    "_auto_001": "Запомнить мой выбор (можно изменить в настройках)",
    "_auto_002": "Запомнить мой выбор",
    "_auto_003": "Закрыть",
    "_auto_004": "Отмена",
    "_auto_005": "Доступно обновление GoidaPhone",
    "_auto_006": "🔍 Поиск...",
    "_auto_007": "Онлайн: 0",
    "_auto_008": "Онлайн",
    "_auto_009": "➕ Создать группу",
    "_auto_010": "⭐ Избранное",
    "_auto_011": "Отправить приглашение",
    "_auto_012": "Отправить групповое приглашение",
    "_auto_013": "📨 Отправить",
    "_auto_014": "Аватар группы",
    "_auto_015": "Групповой аватар",
    "_auto_016": "📷 Изменить аватар",
    "_auto_017": "Название:",
    "_auto_018": "Название группы",
    "_auto_019": "✏ Переименовать",
    "_auto_020": "Участники:",
    "_auto_021": "🚫 Удалить участника",
    "_auto_022": "➕ Пригласить пользователя",
    "_auto_023": "Ссылка-приглашение:",
    "_auto_024": "📋 Copy",
    "_auto_025": "🚪 Выйти",
    "_auto_026": "🗑 Удалить группу",
    "_auto_027": "Поиск сообщений... (Enter — дальше)",
    "_auto_028": "🎭 Стикеры",
    "_auto_029": "⚙ Паки",
    "_auto_030": "Настроить стикерпак",
    "_auto_031": "Стикеры",
    "_auto_032": "Не найдено",
    "_auto_033": "🔓 Незашифровано",
    "_auto_034": "{username} печатает...",
    "_auto_035": "Отклонить звонок",
    "_auto_036": "Позвонить",
    "_auto_037": "Завершить звонок",
    "_auto_038": "(нет паков — добавьте в ⚙)",
    "_auto_039": "Нет стикеров. Добавьте пак через ⚙ Паки",
    "_auto_040": "📝 Совместные заметки",
    "_auto_041": "📤 Синхронизировать",
    "_auto_042": "Заметки, ссылки, файлы для себя",
    "_auto_043": "🔵 СИНИЙ ЭКРАН СМЕРТИ",
    "_auto_044": "Критическая ошибка",
    "_auto_045": "Перезапуск",
    "_auto_046": "Сохранить отчёт",
    "_auto_047": "Копировать",
    "_auto_048": "Скопировано",
    "_auto_049": "Внешний вид",
    "_auto_050": "Блокировка",
    "_auto_051": "Приватность",
    "_auto_052": "Звонки",
    "_auto_053": "Звуки",
    "_auto_054": "Специалист",
    "_auto_055": "Имя пользователя:",
    "_auto_056": "Описание:",
    "_auto_057": "Цвет имени:",
    "_auto_058": "Название эмодзи:",
    "_auto_059": "Сохранить профиль",
    "_auto_060": "Профиль сохранён!",
    "_auto_061": "Тема:",
    "_auto_062": "Предпросмотр темы",
    "_auto_063": "Размер интерфейса:",
    "_auto_064": "Сохранить",
    "_auto_065": "👑 Премиум",
    "_auto_066": "Активировать лицензию",
    "_auto_067": "Введите ключ активации:",
    "_auto_068": "Активировать",
    "_auto_069": "Купить Премиум",
    "_auto_070": "Ключ принят! ✓",
    "_auto_071": "Неверный ключ",
    "_auto_072": "Файл получен",
    "_auto_073": "Изображение",
    "_auto_074": "Видео",
    "_auto_075": "Аудио",
    "_auto_076": "Файл",
    "_auto_077": "⚠ Не удалось загрузить",
    "_auto_078": "💾 Сохранить",
    "_auto_079": "GoidaID мод активен. Реальный IP скрыт.",
    "_auto_080": "Участники",
    "_auto_081": "Группа",
    "_auto_082": "👥 Пользователи",
    "_auto_083": "📂 Группы",
    "_auto_084": "💬 Чат",
    "_auto_085": "📷 Выбрать баннер",
    "_auto_086": "🗑 Убрать",
    "tab_main_chat": "💬 Чат",
    "tab_main_notes": "📝 Заметки",
    "tab_main_calls": "📋 Звонки",
    "tab_peer_list": "👥 Пользователи",
    "tab_group_list": "📂 Группы",
    "sidebar_settings": "⚙ Настройки",
    "sidebar_chat_btn": "💬 Чат",
    "drag_file_hint": "",
    "notif_settings": "Настройки уведомлений",
    "menu_my_profile": "👤 Мой профиль",
    "menu_settings": "⚙ Настройки",
    "menu_check_updates": "🔄 Проверить обновления",
    "menu_quit": "❌ Выход",
    "menu_public_chat": "💬 Общий чат",
    "menu_fullscreen": "⛶ Полный экран  F11",
    "menu_mute_toggle": "🎤 Вкл/Выкл микрофон",
    "menu_hangup_all": "📵 Завершить все звонки",
    "menu_about": "О программе",
    "menu_terminal": "⌨ ZLink Терминал",
    "menu_wns": "🌐 Winora NetScape (WNS)",
    "menu_tutorial": "❓ Учебник / Tutorial",
    "menu_lang_ru": "🇷🇺 Русский",
    "menu_lang_en": "🇬🇧 English",
    "menu_lang_ja": "🇯🇵 日本語",
    "settings_audio_tab": "🎵 Аудио",
    "settings_net_tab": "🌐 Сеть",
    "settings_theme_tab": "🎨 Темы",
    "settings_appear_tab": "🖼 Внешний вид",
    "settings_lic_tab": "👑 Лицензия",
    "settings_data_tab": "💾 Данные",
    "settings_lang_tab": "🌍 Язык",
    "settings_adv_tab": "🔧 Для специалистов",
    "settings_lock_tab": "🔒 Блокировка",
    "settings_priv_tab": "🛡 Приватность",
    "settings_call_tab": "📞 Звонки",
    "settings_title": "GoidaPhone — Настройки",
    "my_profile_title": "Мой профиль"
}

_STRINGS_EN = {
    "app_title": "GoidaPhone",
    "online": "Online",
    "offline": "Offline",
    "public_chat": "💬 Public Chat",
    "all_users": "All users on network",
    "private_chat": "Private Chat",
    "type_message": "Type a message...",
    "send": "Send",
    "call": "Call",
    "hangup": "Hang up",
    "attach": "File",
    "emoji": "Emoji",
    "stickers": "Stickers",
    "typing": "is typing...",
    "you": "You",
    "call_started": "Call started",
    "call_ended": "Call ended",
    "active_call": "📞 Active call",
    "mute_mic": "🎤 Microphone",
    "muted": "🔇 Muted",
    "users": "👥 Users",
    "groups": "📂 Groups",
    "search": "🔍 Search...",
    "create_group": "➕ Create group",
    "new_group": "New Group",
    "group_name": "Group name:",
    "group_created": "Group created.",
    "add_members": "Add members via context menu.",
    "personal_chat": "💬 Personal chat",
    "call_peer": "📞 Call",
    "send_file": "📎 Send file",
    "add_to_group": "➕ Add to group",
    "first_create_group": "Create a group first.",
    "groups_label": "Groups:",
    "msg_file": "📎 File",
    "msg_image": "🖼 Image",
    "msg_edit_hint": "(edited)",
    "msg_forwarded": "↪ Forwarded",
    "msg_reaction_add": "Add reaction",
    "msg_copy": "📋 Copy",
    "msg_edit": "✏ Edit",
    "msg_delete": "🗑 Delete",
    "msg_forward": "↪ Forward",
    "msg_reply": "↩ Reply",
    "msg_reactions": "😊 React",
    "notif_new_message": "New message",
    "notif_incoming_call": "Incoming call",
    "notif_file_received": "File received",
    "notif_user_online": "user is online",
    "settings": "GoidaPhone Settings",
    "tab_audio": "🎵 Audio",
    "tab_network": "🌐 Network",
    "tab_themes": "🎨 Themes",
    "tab_license": "👑 License",
    "tab_data": "💾 Data",
    "tab_specialist": "🔧 For Specialists",
    "tab_language": "🌍 Language",
    "save": "💾 Save",
    "close": "Close",
    "cancel": "Cancel",
    "yes": "Yes",
    "no": "No",
    "ok": "OK",
    "saved": "Settings saved!",
    "my_profile": "👤 My Profile",
    "username": "Username:",
    "bio": "About me:",
    "avatar": "Avatar",
    "change_avatar": "📷 Change avatar",
    "banner": "Profile banner",
    "nickname_color": "Nickname color:",
    "custom_emoji": "Emoji next to name:",
    "profile_saved": "Profile saved!",
    "incoming_call": "is calling you",
    "accept": "✅ Accept",
    "reject": "❌ Reject",
    "launcher_title": "GoidaPhone",
    "launcher_subtitle": "Welcome",
    "launcher_gui": "🖥 Launch\\nGraphical\\nInterface",
    "launcher_cmd": "⌨ Launch\\nConsole\\nMode",
    "launcher_gui_hint": "Full interface with chat, calls and files",
    "launcher_cmd_hint": "For servers, diagnostics and automation",
    "about": "About GoidaPhone",
    "check_updates": "🔄 Check for updates",
    "update_found": "🚀 Version available",
    "update_available_title": "Update Available!",
    "update_now": "⬇ Update",
    "no_updates": "✅ No updates found.",
    "update_error": "❌ Error:",
    "network_error": "Failed to start network services.\\nCheck if ports are in use by other programs.",
    "file_send_error": "File send error",
    "no_image": "Could not load image.",
    "cmd_clear_done": "Chat cleared.",
    "cmd_help": "Commands: /clear /help /me /ping /version /ttl /poll /notes /schedule /tr /translate",
    "cmd_me": "does",
    "cmd_ping": "Pong!",
    "cmd_unknown": "Unknown command. Type /help for command list.",
    "searching": "🔍 Searching for users...",
    "no_calls": "📞 No calls",
    "mic_on": "🎤 On",
    "mic_off": "🔇 Off",
    "menu_file": "File",
    "menu_view": "View",
    "menu_themes": "🎨 Themes",
    "menu_calls": "Calls",
    "menu_help": "Help",
    "open_link": "Open link",
    "where_open": "Where to open?",
    "browser_builtin": "🌐 WNS (built-in)",
    "browser_system": "🖥 Default browser",
    "open_media": "Open media file",
    "player_builtin": "🎵 Mewa (built-in)",
    "player_system": "📂 System player",
    "update_platform": "Choose your platform:",
    "update_release_page": "🌐 Open release page",
    "btn_later": "Later",
    "launcher_choose": "Choose launch mode",
    "launcher_hint": "← → arrows  •  Enter",
    "launcher_run": "Launch  ▶",
    "launcher_help": "❓ Help",
    "btn_close_lbl": "Close",
    "loading": "Loading",
    "online_since": "🕐 Online since",
    "premium_user": "👑 Premium user",
    "group_invite_card": "📂 Group invitation",
    "already_in_group": "✅ Already in this group",
    "btn_join": "👋 Join",
    "joined_group": "✅ Joined!",
    "img_hint": "Click to view · RMB save",
    "video_hint": "Click to play",
    "scroll_hint": "Scroll — zoom  •  LMB — drag",
    "new_messages": "↓ New",
    "reply_cancelled": "Reply cancelled",
    "call_floating": "📞 Active call",
    "call_no_peer": "No active peer",
    "call_muted": "🔇 Muted",
    "call_unmuted": "🎤 Microphone",
    "settings_restart": "Restart required to apply",
    "theme_changed": "Theme changed",
    "theme_back": "← Revert",
    "theme_restart": "↺ Restart",
    "file_received_msg": "file received",
    "save_file": "💾 Save",
    "open_file": "Open",
    "history_empty": "History is empty",
    "no_messages": "No messages",
    "quicksetup_title": "⚡ Quick setup",
    "quicksetup_q1": "What's your name?",
    "quicksetup_q2": "Choose nickname color",
    "quicksetup_q3": "Choose theme",
    "quicksetup_q4": "Enable sounds?",
    "quicksetup_q5": "Save message history?",
    "quicksetup_q6": "Show splash on startup?",
    "quicksetup_done": "All done! 🎉",
    "tutorial_skip": "✕ Skip",
    "tutorial_next": "Next →",
    "tutorial_finish": "Finish ✓",
    "tutorial_back": "← Back",
    "connecting": "Connecting...",
    "connected": "Connected",
    "disconnected": "Disconnected",
    "network_error_short": "Network error",
    "tab_appearance": "🖼 Appearance",
    "tab_security": "🔒 Security",
    "tab_privacy": "🛡 Privacy",
    "tab_calls": "📞 Calls",
    "tab_sounds": "🔔 Sounds",
    "btn_save": "💾 Save",
    "btn_close": "Close",
    "btn_cancel": "Cancel",
    "lbl_online_users": "Online Users",
    "no_users": "No users on network",
    "incoming_call_from": "Incoming call from",
    "call_ended_msg": "Call ended",
    "call_started_msg": "Call started",
    "msg_deleted": "Message deleted",
    "msg_edited": "edited",
    "file_received": "File received",
    "image_received": "Image received",
    "tab_users": "👥 Users",
    "tab_groups": "📂 Groups",
    "tab_chat_pub": "💬 Chat",
    "premium_label": "👑 Premium",
    "open_with_what": "How to open?",
    "help_title": "Help — GoidaPhone",
    "help_modes_title": "📖  Launch modes help",
    "splash_loading": "Loading",
    "scroll_zoom_hint": "Scroll — zoom  •  LMB drag",
    "btn_view": "🔍 View",
    "btn_play": "▶ Play",
    "note_saved": "Note saved",
    "note_placeholder": "Write a note...",
    "search_msgs": "Search messages...",
    "pin_enter": "Enter PIN",
    "pin_wrong": "Wrong PIN",
    "sticker_label": "Sticker",
    "edited_label": "edited",
    "forwarded_label": "Forwarded",
    "update_available_h": "🚀 Update available!",
    "launcher_arrows": "← → arrows  •  Enter",
    "premium_user_lbl": "👑 Premium user",
    "goidaid_active": "GoidaID mode active",
    "_auto_000": "Remember choice (change in Settings → Privacy)",
    "_auto_001": "Remember my choice",
    "_auto_002": "Remember my choice",
    "_auto_003": "Close",
    "_auto_004": "Cancel",
    "_auto_005": "GoidaPhone v{ver} available!",
    "_auto_006": "🔍 Search...",
    "_auto_007": "Online: 0",
    "_auto_008": "Online: {count}",
    "_auto_009": "➕ Create group",
    "_auto_010": "⭐ Favorites",
    "_auto_011": "Send invitation",
    "_auto_012": "Where to send invitation?",
    "_auto_013": "📨 Send",
    "_auto_014": "<b>Group avatar</b>",
    "_auto_015": "<b>Group avatar</b>",
    "_auto_016": "📷 Change avatar",
    "_auto_017": "Name:",
    "_auto_018": "Group name",
    "_auto_019": "✏ Rename",
    "_auto_020": "Members:",
    "_auto_021": "🚫 Remove member",
    "_auto_022": "➕ Invite user",
    "_auto_023": "Invite link:",
    "_auto_024": "📋 Copy",
    "_auto_025": "🚪 Leave",
    "_auto_026": "🗑 Delete group",
    "_auto_027": "Search messages... (Enter — next)",
    "_auto_028": "🎭 Stickers",
    "_auto_029": "⚙ Packs",
    "_auto_030": "Manage sticker packs",
    "_auto_031": "Stickers",
    "_auto_032": "Not found",
    "_auto_033": "🔓 Unencrypted",
    "_auto_034": "{username} is typing...",
    "_auto_035": "Cancel call",
    "_auto_036": "Call",
    "_auto_037": "End call",
    "_auto_038": "(no packs — add in ⚙)",
    "_auto_039": "No stickers. Add a pack via ⚙ Packs",
    "_auto_040": "📝 Shared notes (real-time sync)",
    "_auto_041": "📤 Sync",
    "_auto_042": "Notes, links, files for yourself",
    "_auto_043": "🔵 BLUE SCREEN OF DEATH",
    "_auto_044": "Critical error",
    "_auto_045": "Restart",
    "_auto_046": "Save report",
    "_auto_047": "Copy",
    "_auto_048": "Copied",
    "_auto_049": "Appearance",
    "_auto_050": "Lock",
    "_auto_051": "Privacy",
    "_auto_052": "Calls",
    "_auto_053": "Sounds",
    "_auto_054": "Advanced",
    "_auto_055": "Username:",
    "_auto_056": "About:",
    "_auto_057": "Nick color:",
    "_auto_058": "Emoji next to name:",
    "_auto_059": "Save profile",
    "_auto_060": "Profile saved!",
    "_auto_061": "Theme:",
    "_auto_062": "Preview theme",
    "_auto_063": "UI scale:",
    "_auto_064": "Save",
    "_auto_065": "👑 PREMIUM",
    "_auto_066": "Activate license",
    "_auto_067": "Enter license key:",
    "_auto_068": "Activate",
    "_auto_069": "Buy Premium",
    "_auto_070": "Key accepted! ✓",
    "_auto_071": "Invalid key",
    "_auto_072": "File received",
    "_auto_073": "Image",
    "_auto_074": "Video",
    "_auto_075": "Audio",
    "_auto_076": "File",
    "_auto_077": "⚠ Failed to load image",
    "_auto_078": "💾 Save",
    "_auto_079": "GoidaID mode active. Real IP hidden.",
    "_auto_080": "Members: {len(members)}",
    "_auto_081": "📂 {g.get('name','Group')}",
    "_auto_082": "👥 Users",
    "_auto_083": "📂 Groups",
    "_auto_084": "💬 Chat",
    "_auto_085": "📷 Choose banner",
    "_auto_086": "🗑 Remove",
    "tab_main_chat": "💬 Chat",
    "tab_main_notes": "📝 Notes",
    "tab_main_calls": "📋 Calls",
    "tab_peer_list": "👥 Users",
    "tab_group_list": "📂 Groups",
    "sidebar_settings": "⚙ Settings",
    "sidebar_chat_btn": "💬 Chat",
    "drag_file_hint": "Drop file here or Ctrl+V",
    "notif_settings": "Notification settings",
    "menu_my_profile": "👤 My Profile",
    "menu_settings": "⚙ Settings",
    "menu_check_updates": "🔄 Check for updates",
    "menu_quit": "❌ Quit",
    "menu_public_chat": "💬 Public Chat",
    "menu_fullscreen": "⛶ Fullscreen  F11",
    "menu_mute_toggle": "🎤 Toggle Microphone",
    "menu_hangup_all": "📵 End All Calls",
    "menu_about": "About GoidaPhone",
    "menu_terminal": "⌨ ZLink Terminal",
    "menu_wns": "🌐 Winora NetScape (WNS)",
    "menu_tutorial": "❓ Tutorial",
    "menu_lang_ru": "🇷🇺 Russian",
    "menu_lang_en": "🇬🇧 English",
    "menu_lang_ja": "🇯🇵 Japanese",
    "settings_audio_tab": "🎵 Audio",
    "settings_net_tab": "🌐 Network",
    "settings_theme_tab": "🎨 Themes",
    "settings_appear_tab": "🖼 Appearance",
    "settings_lic_tab": "👑 License",
    "settings_data_tab": "💾 Data",
    "settings_lang_tab": "🌍 Language",
    "settings_adv_tab": "🔧 Advanced",
    "settings_lock_tab": "🔒 Lock",
    "settings_priv_tab": "🛡 Privacy",
    "settings_call_tab": "📞 Calls",
    "settings_title": "GoidaPhone — Settings",
    "my_profile_title": "My Profile"
}

_STRINGS_JA = {
    "app_title": "GoidaPhone",
    "online": "オンライン",
    "offline": "オフライン",
    "public_chat": "💬 パブリックチャット",
    "all_users": "ネットワーク上の全ユーザー",
    "private_chat": "プライベートチャット",
    "type_message": "メッセージを入力...",
    "send": "送信",
    "call": "通話",
    "hangup": "切断",
    "attach": "ファイル",
    "emoji": "絵文字",
    "stickers": "スタンプ",
    "typing": "入力中...",
    "you": "あなた",
    "call_started": "通話開始",
    "call_ended": "通話終了",
    "active_call": "📞 通話中",
    "mute_mic": "🎤 マイク",
    "muted": "🔇 ミュート",
    "users": "👥 ユーザー",
    "groups": "📂 グループ",
    "search": "🔍 検索...",
    "create_group": "➕ グループ作成",
    "new_group": "新しいグループ",
    "group_name": "グループ名:",
    "group_created": "グループを作成しました。",
    "add_members": "右クリックでメンバーを追加。",
    "personal_chat": "💬 個人チャット",
    "call_peer": "📞 通話",
    "send_file": "📎 ファイル送信",
    "add_to_group": "➕ グループに追加",
    "first_create_group": "先にグループを作成してください。",
    "groups_label": "グループ:",
    "msg_file": "📎 ファイル",
    "msg_image": "🖼 画像",
    "msg_edit_hint": "(編集済)",
    "msg_forwarded": "↪ 転送",
    "msg_reaction_add": "リアクション追加",
    "msg_copy": "📋 コピー",
    "msg_edit": "✏ 編集",
    "msg_delete": "🗑 削除",
    "msg_forward": "↪ 転送",
    "msg_reply": "↩ 返信",
    "msg_reactions": "😊 リアクション",
    "notif_new_message": "新しいメッセージ",
    "notif_incoming_call": "着信",
    "notif_file_received": "ファイル受信",
    "notif_user_online": "がオンラインになりました",
    "settings": "GoidaPhone 設定",
    "tab_audio": "🎵 オーディオ",
    "tab_network": "🌐 ネットワーク",
    "tab_themes": "🎨 テーマ",
    "tab_license": "👑 ライセンス",
    "tab_data": "💾 データ",
    "tab_specialist": "🔧 上級者向け",
    "tab_language": "🌍 言語",
    "save": "💾 保存",
    "close": "閉じる",
    "cancel": "キャンセル",
    "yes": "はい",
    "no": "いいえ",
    "ok": "OK",
    "saved": "設定を保存しました！",
    "my_profile": "👤 マイプロフィール",
    "username": "ユーザー名:",
    "bio": "自己紹介:",
    "avatar": "アバター",
    "change_avatar": "📷 アバター変更",
    "banner": "プロフィールバナー",
    "nickname_color": "ニックネームの色:",
    "custom_emoji": "名前の横の絵文字:",
    "profile_saved": "プロフィールを保存しました！",
    "incoming_call": "から着信",
    "accept": "✅ 応答",
    "reject": "❌ 拒否",
    "launcher_title": "GoidaPhone",
    "launcher_subtitle": "ようこそ",
    "launcher_gui": "🖥 グラフィカル\\nインターフェース",
    "launcher_cmd": "⌨ コンソール\\nモード",
    "launcher_gui_hint": "チャット、通話、ファイル共有",
    "launcher_cmd_hint": "サーバー・診断・自動化向け",
    "about": "About GoidaPhone",
    "check_updates": "🔄 アップデート確認",
    "update_found": "🚀 新バージョンあり",
    "update_available_title": "アップデート利用可能！",
    "update_now": "⬇ アップデート",
    "no_updates": "✅ 最新バージョンです。",
    "update_error": "❌ エラー:",
    "network_error": "ネットワークサービスの起動に失敗しました。",
    "file_send_error": "ファイル送信エラー",
    "no_image": "画像を読み込めません。",
    "cmd_clear_done": "チャットをクリアしました。",
    "cmd_help": "コマンド: /clear /help /me /ping /version /ttl /poll /notes",
    "cmd_me": "が",
    "cmd_ping": "ポン！",
    "cmd_unknown": "不明なコマンドです。/help でコマンド一覧を表示。",
    "searching": "🔍 ユーザーを検索中...",
    "no_calls": "📞 通話なし",
    "mic_on": "🎤 オン",
    "mic_off": "🔇 オフ",
    "menu_file": "ファイル",
    "menu_view": "表示",
    "menu_themes": "🎨 テーマ",
    "menu_calls": "通話",
    "menu_help": "ヘルプ",
    "open_link": "リンクを開く",
    "where_open": "どこで開く?",
    "browser_builtin": "🌐 WNS (内蔵)",
    "browser_system": "🖥 デフォルトブラウザ",
    "open_media": "メディアを開く",
    "player_builtin": "🎵 Mewa (内蔵)",
    "player_system": "📂 システムプレーヤー",
    "update_platform": "プラットフォームを選択:",
    "update_release_page": "🌐 リリースページ",
    "btn_later": "後で",
    "launcher_choose": "起動モードを選択",
    "launcher_hint": "← → 矢印  •  Enter",
    "launcher_run": "起動  ▶",
    "launcher_help": "❓ ヘルプ",
    "btn_close_lbl": "閉じる",
    "loading": "読み込み中",
    "online_since": "🕐 オンライン",
    "premium_user": "👑 プレミアムユーザー",
    "group_invite_card": "📂 グループ招待",
    "already_in_group": "✅ 既にグループにいます",
    "btn_join": "👋  参加",
    "joined_group": "✅ 参加しました!",
    "img_hint": "クリックで表示 · 右クリック保存",
    "video_hint": "クリックで再生",
    "scroll_hint": "スクロール拡大 · ドラッグ",
    "new_messages": "↓ 新着",
    "reply_cancelled": "返信キャンセル",
    "call_floating": "📞 通話中",
    "call_no_peer": "アクティブな相手なし",
    "call_muted": "🔇 ミュート中",
    "call_unmuted": "🎤 マイク",
    "settings_restart": "適用には再起動が必要",
    "theme_changed": "テーマ変更",
    "theme_back": "← 元に戻す",
    "theme_restart": "↺ 再起動",
    "file_received_msg": "ファイル受信",
    "save_file": "💾 保存",
    "open_file": "開く",
    "history_empty": "履歴なし",
    "no_messages": "メッセージなし",
    "quicksetup_title": "⚡ クイック設定",
    "quicksetup_q1": "あなたの名前は?",
    "quicksetup_q2": "ニックネームの色",
    "quicksetup_q3": "テーマを選択",
    "quicksetup_q4": "サウンドを有効にする?",
    "quicksetup_q5": "メッセージ履歴を保存?",
    "quicksetup_q6": "起動時スプラッシュ表示?",
    "quicksetup_done": "設定完了! 🎉",
    "tutorial_skip": "✕ スキップ",
    "tutorial_next": "次へ →",
    "tutorial_finish": "完了 ✓",
    "tutorial_back": "← 戻る",
    "connecting": "接続中...",
    "connected": "接続済み",
    "disconnected": "切断",
    "network_error_short": "ネットワークエラー",
    "tab_appearance": "🖼 外観",
    "tab_security": "🔒 セキュリティ",
    "tab_privacy": "🛡 プライバシー",
    "tab_calls": "📞 通話",
    "tab_sounds": "🔔 サウンド",
    "btn_save": "💾 保存",
    "btn_close": "閉じる",
    "btn_cancel": "キャンセル",
    "lbl_online_users": "オンラインユーザー",
    "no_users": "ネットワークにユーザーがいません",
    "incoming_call_from": "着信:",
    "call_ended_msg": "通話終了",
    "call_started_msg": "通話開始",
    "msg_deleted": "メッセージが削除されました",
    "msg_edited": "編集済み",
    "file_received": "ファイル受信",
    "image_received": "画像受信",
    "tab_users": "👥 ユーザー",
    "tab_groups": "📂 グループ",
    "tab_chat_pub": "💬 チャット",
    "premium_label": "👑 プレミアム",
    "open_with_what": "何で開く?",
    "help_title": "ヘルプ — GoidaPhone",
    "help_modes_title": "📖  起動モードヘルプ",
    "splash_loading": "読み込み中",
    "scroll_zoom_hint": "スクロール拡大 · ドラッグ",
    "btn_view": "🔍 表示",
    "btn_play": "▶ 再生",
    "note_saved": "メモ保存",
    "note_placeholder": "メモを書く...",
    "search_msgs": "メッセージ検索...",
    "pin_enter": "PINを入力",
    "pin_wrong": "PINが違います",
    "sticker_label": "スタンプ",
    "edited_label": "編集済",
    "forwarded_label": "転送",
    "update_available_h": "🚀 アップデートあり!",
    "launcher_arrows": "← → 矢印  •  Enter",
    "premium_user_lbl": "👑 Premium",
    "goidaid_active": "GoidaIDモード有効",
    "_auto_000": "選択を記憶",
    "_auto_001": "選択を記憶",
    "_auto_002": "選択を記憶",
    "_auto_003": "閉じる",
    "_auto_004": "キャンセル",
    "_auto_005": "GoidaPhone v{ver} 利用可能!",
    "_auto_006": "🔍 検索...",
    "_auto_007": "オンライン: 0",
    "_auto_008": "オンライン: {count}",
    "_auto_009": "➕ グループ作成",
    "_auto_010": "⭐ お気に入り",
    "_auto_011": "招待を送る",
    "_auto_012": "どこに招待を送りますか?",
    "_auto_013": "📨 送信",
    "_auto_014": "グループアバター",
    "_auto_015": "<b>グループアバター</b>",
    "_auto_016": "📷 アバター変更",
    "_auto_017": "名前:",
    "_auto_018": "グループ名",
    "_auto_019": "✏ 名前変更",
    "_auto_020": "メンバー:",
    "_auto_021": "🚫 メンバー削除",
    "_auto_022": "➕ ユーザー招待",
    "_auto_023": "招待リンク:",
    "_auto_024": "📋 コピー",
    "_auto_025": "🚪 退出",
    "_auto_026": "🗑 グループ削除",
    "_auto_027": "メッセージ検索...",
    "_auto_028": "🎭 スタンプ",
    "_auto_029": "⚙ パック",
    "_auto_030": "スタンプパック管理",
    "_auto_031": "スタンプ",
    "_auto_032": "見つかりません",
    "_auto_033": "🔓 暗号化なし",
    "_auto_034": "{username} が入力中...",
    "_auto_035": "発信をキャンセル",
    "_auto_036": "通話",
    "_auto_037": "通話終了",
    "_auto_038": "(パックなし — ⚙で追加)",
    "_auto_039": "スタンプなし",
    "_auto_040": "📝 共有メモ",
    "_auto_041": "📤 同期",
    "_auto_042": "自分用メモ・リンク・ファイル",
    "_auto_043": "🔵 ブルースクリーン",
    "_auto_044": "致命的なエラー",
    "_auto_045": "再起動",
    "_auto_046": "レポート保存",
    "_auto_047": "コピー",
    "_auto_048": "コピー済み",
    "_auto_049": "外観",
    "_auto_050": "ロック",
    "_auto_051": "プライバシー",
    "_auto_052": "通話",
    "_auto_053": "サウンド",
    "_auto_054": "上級者",
    "_auto_055": "ユーザー名:",
    "_auto_056": "自己紹介:",
    "_auto_057": "ニックネーム色:",
    "_auto_058": "名前横絵文字:",
    "_auto_059": "プロフィール保存",
    "_auto_060": "プロフィール保存済み!",
    "_auto_061": "テーマ:",
    "_auto_062": "テーマプレビュー",
    "_auto_063": "UIスケール:",
    "_auto_064": "保存",
    "_auto_065": "👑 プレミアム",
    "_auto_066": "ライセンス認証",
    "_auto_067": "ライセンスキーを入力:",
    "_auto_068": "認証",
    "_auto_069": "プレミアム購入",
    "_auto_070": "キー受付! ✓",
    "_auto_071": "無効なキー",
    "_auto_072": "ファイル受信",
    "_auto_073": "画像",
    "_auto_074": "ビデオ",
    "_auto_075": "オーディオ",
    "_auto_076": "ファイル",
    "_auto_077": "⚠ 画像読み込み失敗",
    "_auto_078": "💾 保存",
    "_auto_079": "GoidaIDモード有効。実IPを隠匿。",
    "_auto_080": "メンバー: {len(members)}",
    "_auto_081": "📂 {g.get('name','グループ')}",
    "_auto_082": "👥 ユーザー",
    "_auto_083": "📂 グループ",
    "_auto_084": "💬 チャット",
    "_auto_085": "📷 バナー選択",
    "_auto_086": "🗑 削除",
    "tab_main_chat": "💬 チャット",
    "tab_main_notes": "📝 メモ",
    "tab_main_calls": "📋 通話",
    "tab_peer_list": "👥 ユーザー",
    "tab_group_list": "📂 グループ",
    "sidebar_settings": "⚙ 設定",
    "sidebar_chat_btn": "💬 チャット",
    "drag_file_hint": "ファイルをここにドロップ",
    "notif_settings": "通知設定",
    "menu_my_profile": "👤 マイプロフィール",
    "menu_settings": "⚙ 設定",
    "menu_check_updates": "🔄 アップデート確認",
    "menu_quit": "❌ 終了",
    "menu_public_chat": "💬 パブリックチャット",
    "menu_fullscreen": "⛶ フルスクリーン  F11",
    "menu_mute_toggle": "🎤 マイク切り替え",
    "menu_hangup_all": "📵 全通話終了",
    "menu_about": "GoidaPhoneについて",
    "menu_terminal": "⌨ ZLink ターミナル",
    "menu_wns": "🌐 Winora NetScape (WNS)",
    "menu_tutorial": "❓ チュートリアル",
    "menu_lang_ru": "🇷🇺 ロシア語",
    "menu_lang_en": "🇬🇧 英語",
    "menu_lang_ja": "🇯🇵 日本語",
    "settings_audio_tab": "🎵 オーディオ",
    "settings_net_tab": "🌐 ネットワーク",
    "settings_theme_tab": "🎨 テーマ",
    "settings_appear_tab": "🖼 外観",
    "settings_lic_tab": "👑 ライセンス",
    "settings_data_tab": "💾 データ",
    "settings_lang_tab": "🌍 言語",
    "settings_adv_tab": "🔧 上級者向け",
    "settings_lock_tab": "🔒 ロック",
    "settings_priv_tab": "🛡 プライバシー",
    "settings_call_tab": "📞 通話",
    "settings_title": "GoidaPhone — 設定",
    "my_profile_title": "マイプロフィール"
}

class Strings:
    """
    Localization manager.
    Usage: TR("key") or TR.get("key", "fallback")
    Language is read from AppSettings at call time, so switching language
    in settings takes effect immediately without restart.
    """
    _langs = {"ru": _STRINGS_RU, "en": _STRINGS_EN, "ja": _STRINGS_JA}

    @classmethod
    def _table(cls) -> dict:
        try:
            lang = AppSettings.inst().language
        except Exception:
            lang = "en"
        return cls._langs.get(lang, _STRINGS_EN)  # fallback EN not RU

    def __call__(self, key: str, **kwargs) -> str:
        val = self._table().get(key, key)
        if kwargs:
            try:
                val = val.format(**kwargs)
            except Exception:
                pass
        return val

    def get(self, key: str, fallback: str = "") -> str:
        return self._table().get(key, fallback)


class AppSettings:
    """Centralised settings wrapper."""
    _inst = None

    @classmethod
    def inst(cls):
        if cls._inst is None:
            cls._inst = cls()
        return cls._inst

    def __init__(self):
        self._s = QSettings("WinoraCompany", "GoidaPhone")

    def get(self, key, default=None, t=None):
        v = self._s.value(key, default)
        if t is bool:
            if isinstance(v, bool): return v
            return str(v).lower() in ("true", "1", "yes")
        if t is not None and v is not None:
            try:
                return t(v)
            except Exception:
                return default
        return v

    def set(self, key, value):
        self._s.setValue(key, value)
        self._s.sync()

    def remove(self, key):
        self._s.remove(key)

    # shortcuts
    @property
    def username(self):
        return self.get("username", f"User_{secrets.randbelow(1000):03d}", t=str)

    @username.setter
    def username(self, v):
        self.set("username", v)

    @property
    def theme(self):
        return self.get("theme", "dark", t=str)

    @theme.setter
    def theme(self, v):
        self.set("theme", v)

    @property
    def premium(self) -> bool:
        if not self.get("premium", False, t=bool):
            return False
        exp_str = self.get("premium_expires", "", t=str)
        if not exp_str:
            return False
        try:
            exp = datetime.fromisoformat(exp_str)
            if datetime.now() > exp:
                self.set("premium", False)
                return False
            return True
        except Exception:
            return False

    @property
    def premium_expires(self) -> str:
        return self.get("premium_expires", "", t=str)

    def activate_premium(self, code: str) -> bool:
        try:
            n = int(code)
            if len(code) == 12 and n % LICENSE_DIVISOR == 0:
                expires = datetime.now() + timedelta(days=LICENSE_DAYS)
                self.set("premium", True)
                self.set("license_code", code)
                self.set("premium_expires", expires.isoformat())
                return True
        except Exception:
            pass
        return False

    @property
    def nickname_color(self):
        return self.get("nickname_color", "#E0E0E0", t=str)

    @nickname_color.setter
    def nickname_color(self, v):
        self.set("nickname_color", v)

    @property
    def custom_emoji(self):
        if self.premium:
            return self.get("custom_emoji", "👑", t=str)
        return ""

    @custom_emoji.setter
    def custom_emoji(self, v):
        self.set("custom_emoji", v)

    @property
    def bio(self):
        return self.get("bio", "", t=str)

    @bio.setter
    def bio(self, v):
        self.set("bio", v)

    @property
    def user_status(self) -> str:
        """Presence status: online | away | busy | dnd"""
        return self.get("user_status", "online", t=str)

    @user_status.setter
    def user_status(self, v: str):
        self.set("user_status", v)

    @property
    def safe_mode(self) -> bool:
        """Safe mode: show GoidaID instead of IP addresses."""
        return self.get("safe_mode", False, t=bool)

    @safe_mode.setter
    def safe_mode(self, v: bool):
        self.set("safe_mode", v)

    @property
    def language(self) -> str:
        """Interface language: 'ru' or 'en'."""
        return self.get("language", "en", t=str)

    @language.setter
    def language(self, v: str):
        self.set("language", v)

    @property
    def show_launcher(self) -> bool:
        """Show GUI/CMD launcher screen on startup."""
        return self.get("show_launcher", True, t=bool)

    @show_launcher.setter
    def show_launcher(self, v: bool):
        self.set("show_launcher", v)

    @property
    def os_notifications(self) -> bool:
        """Send desktop OS notifications for new messages."""
        return self.get("os_notifications", True, t=bool)

    @os_notifications.setter
    def os_notifications(self, v: bool):
        self.set("os_notifications", v)

    @property
    def relay_server(self) -> str:
        """Optional relay server address (host:port)."""
        return self.get("relay_server", "", t=str)

    @relay_server.setter
    def relay_server(self, v: str):
        self.set("relay_server", v)

    @property
    def relay_enabled(self) -> bool:
        return self.get("relay_enabled", False, t=bool)

    @relay_enabled.setter
    def relay_enabled(self, v: bool):
        self.set("relay_enabled", v)


    @property
    def connection_mode(self) -> str:
        """'lan' — LAN/VPN only, 'internet' — через VDS goidaphone.ru"""
        return self.get("connection_mode", "lan", t=str)

    @connection_mode.setter
    def connection_mode(self, v: str):
        self.set("connection_mode", v)

    @property
    def encryption_enabled(self) -> bool:
        """True if end-to-end encryption is active."""
        return self.get("encryption_enabled", False, t=bool)

    @encryption_enabled.setter
    def encryption_enabled(self, v: bool):
        self.set("encryption_enabled", v)

    @property
    def encryption_passphrase(self) -> str:
        """Shared passphrase for AES-256 encryption.
        GoidaCRYPTO: если VAULT разблокирован — берём оттуда (зашифровано).
        Иначе — fallback в QSettings (plaintext, legacy)."""
        if VAULT.is_unlocked():
            v = VAULT.get("encryption_passphrase")
            if v is not None:
                return v
        return self.get("encryption_passphrase", "", t=str)

    @encryption_passphrase.setter
    def encryption_passphrase(self, v: str):
        if VAULT.is_unlocked():
            VAULT.set("encryption_passphrase", v)
        else:
            self.set("encryption_passphrase", v)

    @property
    def device_name(self) -> str:
        """Custom device/hostname label shown to peers."""
        return self.get("device_name", platform.node() or "PC", t=str)

    @device_name.setter
    def device_name(self, v: str):
        self.set("device_name", v)

    @property
    def udp_port(self):
        return self.get("udp_port", UDP_PORT_DEFAULT, t=int)

    @property
    def tcp_port(self):
        return self.get("tcp_port", TCP_PORT_DEFAULT, t=int)

    @property
    def volume(self):
        return self.get("volume", 80, t=int)

    @property
    def notification_sounds(self):
        return self.get("notification_sounds", True, t=bool)

    @property
    def show_splash(self):
        return self.get("show_splash", True, t=bool)

    @property
    def save_history(self):
        return self.get("save_history", True, t=bool)

    @property
    def link_open_pref(self) -> str:
        """'wns' | 'system' | 'ask' (default)"""
        return self.get("link_open_pref", "ask", t=str)

    @property
    def avatar_b64(self):
        return self.get("avatar_b64", "", t=str)

    @avatar_b64.setter
    def avatar_b64(self, v):
        self.set("avatar_b64", v)

    @property
    def banner_b64(self):
        return self.get("banner_b64", "", t=str)

    @banner_b64.setter
    def banner_b64(self, v):
        self.set("banner_b64", v)

    def custom_theme(self, slot: int) -> dict:
        raw = self.get(f"custom_theme_{slot}", "", t=str)
        if raw:
            try:
                return json.loads(raw)
            except Exception:
                pass
        return {}

    def save_custom_theme(self, slot: int, data: dict):
        self.set(f"custom_theme_{slot}", json.dumps(data))

S = AppSettings.inst  # callable shorthand → S() → AppSettings instance

# ═══════════════════════════════════════════════════════════════════════════
#  HISTORY MANAGER
# ═══════════════════════════════════════════════════════════════════════════
class HistoryManager:
    def __init__(self):
        self._cache: dict[str, list] = {}

    def _file(self, chat_id: str) -> Path:
        safe = re.sub(r'[^\w\-]', '_', chat_id)
        return HISTORY_DIR / f"{safe}.json"

    def load(self, chat_id: str) -> list:
        if chat_id in self._cache:
            return self._cache[chat_id]
        f = self._file(chat_id)
        if f.exists():
            try:
                data = json.loads(f.read_text(encoding="utf-8"))
                self._cache[chat_id] = data
                return data
            except Exception:
                pass
        self._cache[chat_id] = []
        return []

    def append(self, chat_id: str, entry: dict):
        if not S().save_history:
            return
        msgs = self.load(chat_id)
        msgs.append(entry)
        # keep max 1000 messages
        if len(msgs) > 1000:
            msgs = msgs[-1000:]
        self._cache[chat_id] = msgs
        try:
            self._file(chat_id).write_text(json.dumps(msgs, ensure_ascii=False, indent=1), encoding="utf-8")
        except Exception as e:
            print(f"History save error: {e}")

    def load_call_log(self) -> list:
        return self.load("__call_log__")

    def add_call(self, entry: dict):
        self.append("__call_log__", entry)

HISTORY = HistoryManager()

# ═══════════════════════════════════════════════════════════════════════════
#  UNREAD MESSAGE COUNTER  (п.22 — прочитано/непрочитано)
# ═══════════════════════════════════════════════════════════════════════════
class UnreadManager:
    """
    Tracks unread message counts per chat_id.
    chat_id is either an IP (private chat), 'public', or 'group_<gid>'.
    """
    def __init__(self):
        self._counts: dict[str, int] = {}
        self._callbacks: list = []   # fn(chat_id, count) called on change

    def increment(self, chat_id: str):
        self._counts[chat_id] = self._counts.get(chat_id, 0) + 1
        self._notify(chat_id)

    def mark_read(self, chat_id: str):
        if self._counts.get(chat_id, 0) > 0:
            self._counts[chat_id] = 0
            self._notify(chat_id)

    def get(self, chat_id: str) -> int:
        return self._counts.get(chat_id, 0)

    def total(self) -> int:
        return sum(self._counts.values())

    def on_change(self, fn):
        """Register callback: fn(chat_id: str, count: int)"""
        self._callbacks.append(fn)

    def _notify(self, chat_id: str):
        for fn in self._callbacks:
            try:
                fn(chat_id, self._counts.get(chat_id, 0))
            except Exception:
                pass

UNREAD = UnreadManager()

# ═══════════════════════════════════════════════════════════════════════════
#  MESSAGE REACTION STORE  (п.29 — реакции на сообщения)
# ═══════════════════════════════════════════════════════════════════════════
class ReactionStore:
    """
    Stores emoji reactions per message.
    Key: (chat_id, message_ts_str)
    Value: dict { emoji: [username, ...] }

    Reactions are stored in memory only (session-scoped).
    For persistence across restarts, they're embedded in history entries.
    """
    def __init__(self):
        self._data: dict[tuple, dict] = {}

    def _key(self, chat_id: str, ts: float) -> tuple:
        return (chat_id, round(ts, 3))

    def add(self, chat_id: str, ts: float, emoji: str, username: str):
        key = self._key(chat_id, ts)
        if key not in self._data:
            self._data[key] = {}
        if emoji not in self._data[key]:
            self._data[key][emoji] = []
        if username not in self._data[key][emoji]:
            self._data[key][emoji][: ] = self._data[key][emoji] + [username]
            self._data[key][emoji] = list(set(self._data[key][emoji]))

    def remove(self, chat_id: str, ts: float, emoji: str, username: str):
        key = self._key(chat_id, ts)
        if key in self._data and emoji in self._data[key]:
            try:
                self._data[key][emoji].remove(username)
            except ValueError:
                pass
            if not self._data[key][emoji]:
                del self._data[key][emoji]

    def toggle(self, chat_id: str, ts: float, emoji: str, username: str) -> bool:
        """Add if not present, remove if present. Returns True if added."""
        key = self._key(chat_id, ts)
        existing = self._data.get(key, {}).get(emoji, [])
        if username in existing:
            self.remove(chat_id, ts, emoji, username)
            result = False
        else:
            self.add(chat_id, ts, emoji, username)
            result = True
        # Debounced save — use QTimer if Qt available, else direct
        try:
            from PyQt6.QtCore import QTimer as _QT
            _QT.singleShot(500, self.save)
        except Exception:
            self.save()
        return result

    def get(self, chat_id: str, ts: float) -> dict:
        """Returns { emoji: [usernames] }"""
        return dict(self._data.get(self._key(chat_id, ts), {}))

    def summary(self, chat_id: str, ts: float) -> list[tuple[str, int]]:
        """Returns [(emoji, count)] sorted by count descending."""
        r = self.get(chat_id, ts)
        return sorted([(e, len(u)) for e, u in r.items()
                       if u], key=lambda x: -x[1])

    def save(self):
        """Persist all reactions to disk."""
        try:
            serialisable = {
                f"{k[0]}|||{k[1]}": v
                for k, v in self._data.items()
            }
            _rf = DATA_DIR / "reactions.json"
            _rf.write_text(
                json.dumps(serialisable, ensure_ascii=False),
                encoding="utf-8")
        except Exception as e:
            print(f"[reactions] save error: {e}")

    def load(self):
        """Load reactions from disk."""
        try:
            _rf = DATA_DIR / "reactions.json"
            if not _rf.exists():
                return
            raw = json.loads(_rf.read_text(encoding="utf-8"))
            for key_str, val in raw.items():
                parts = key_str.split("|||", 1)
                if len(parts) == 2:
                    self._data[(parts[0], float(parts[1]))] = val
        except Exception as e:
            print(f"[reactions] load error: {e}")

REACTIONS = ReactionStore()
REACTIONS.load()   # Load persisted reactions at startup

# ═══════════════════════════════════════════════════════════════════════════
class GroupManager:
    def __init__(self):
        self.groups: dict[str, dict] = {}
        self._load()

    def _load(self):
        if GROUPS_FILE.exists():
            try:
                self.groups = json.loads(GROUPS_FILE.read_text(encoding="utf-8"))
            except Exception:
                self.groups = {}

    def _save(self):
        try:
            GROUPS_FILE.write_text(json.dumps(self.groups, ensure_ascii=False, indent=2), encoding="utf-8")
        except Exception as e:
            print(f"Groups save error: {e}")

    def create(self, name: str, creator_ip: str) -> str:
        gid = f"g_{int(time.time())}_{secrets.randbelow(9999):04d}"
        self.groups[gid] = {
            "name": name,
            "creator": creator_ip,
            "members": [creator_ip],
            "created": datetime.now().isoformat(),
        }
        self._save()
        return gid

    def add_member(self, gid: str, ip: str):
        if gid in self.groups and ip not in self.groups[gid]["members"]:
            self.groups[gid]["members"].append(ip)
            self._save()

    def remove_member(self, gid: str, ip: str):
        if gid in self.groups:
            self.groups[gid]["members"] = [m for m in self.groups[gid]["members"] if m != ip]
            self._save()

    def delete(self, gid: str):
        self.groups.pop(gid, None)
        self._save()

    def rename(self, gid: str, new_name: str):
        if gid in self.groups:
            self.groups[gid]["name"] = new_name
            self._save()

    def get(self, gid: str) -> dict:
        return self.groups.get(gid, {})

    def list_for(self, ip: str) -> list[tuple[str, dict]]:
        return [(gid, g) for gid, g in self.groups.items() if ip in g.get("members", [])]

GROUPS = GroupManager()

# ═══════════════════════════════════════════════════════════════════════════
#  UPDATE CHECKER
# ═══════════════════════════════════════════════════════════════════════════
def _show_update_dialog(ver: str, desc: str, parent=None):
    """
    Диалог обновления с кнопками скачать EXE / AppImage / исходники.
    Ссылки берутся из GitHub Releases — файлы должны называться
    GoidaPhone-Windows-x86_64.exe и GoidaPhone-Linux-x86_64.AppImage.
    """
    import platform as _plt
    t = get_theme(S().theme)

    dlg = QDialog(parent)
    dlg.setWindowTitle(TR("update_available_h"))
    dlg.setFixedSize(460, 320)
    dlg.setStyleSheet(
        f"QDialog{{background:{t['bg2']};}}"
        f"QLabel{{background:transparent;color:{t['text']};}}")

    lay = QVBoxLayout(dlg)
    lay.setContentsMargins(28, 24, 28, 20)
    lay.setSpacing(14)

    # Заголовок
    title = QLabel(f'GoidaPhone v{ver} ' + _L("доступна!", "available!", "利用可能!"))
    title.setStyleSheet(
        f"font-size:16px;font-weight:700;color:{t['text']};")
    lay.addWidget(title)

    # Описание релиза
    if desc and desc.strip():
        desc_lbl = QLabel(desc[:200] + ("…" if len(desc) > 200 else ""))
        desc_lbl.setWordWrap(True)
        desc_lbl.setStyleSheet(
            f"font-size:9pt;color:{t['text_dim']};")
        lay.addWidget(desc_lbl)

    lay.addStretch()

    # Базовый URL релизов
    _base = (f"https://github.com/{GITHUB_REPO}/releases/download/v{ver}")
    _release_page = f"https://github.com/{GITHUB_REPO}/releases/tag/v{ver}"

    # Кнопки скачивания
    btns_lbl = QLabel(TR("update_platform"))
    btns_lbl.setStyleSheet(f"font-size:9pt;color:{t['text_dim']};")
    lay.addWidget(btns_lbl)

    btn_row = QHBoxLayout()
    btn_row.setSpacing(8)

    is_win   = _plt.system() == "Windows"
    is_linux = _plt.system() == "Linux"

    for label, url, is_current in [
        ("⬇ Windows .exe",
         f"{_base}/GoidaPhone-Windows-x86_64.exe",
         is_win),
        ("⬇ Linux AppImage",
         f"{_base}/GoidaPhone-Linux-x86_64.AppImage",
         is_linux),
        ("📦 Исходники",
         f"{_release_page}",
         not is_win and not is_linux),
    ]:
        btn = QPushButton(label)
        btn.setFixedHeight(36)
        if is_current:
            # Текущая платформа — акцентная кнопка
            btn.setStyleSheet(
                f"QPushButton{{background:{t['accent']};color:white;"
                "border-radius:8px;border:none;font-weight:600;padding:0 12px;}}"
                f"QPushButton:hover{{background:{t['accent2']};}}")
        else:
            btn.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:8px;padding:0 12px;}}"
                f"QPushButton:hover{{border-color:{t['accent']};color:{t['accent']};}}")
        btn.clicked.connect(
            lambda checked=False, u=url:
                QDesktopServices.openUrl(QUrl(u)))
        btn_row.addWidget(btn)

    lay.addLayout(btn_row)

    # Ссылка на страницу релиза
    page_btn = QPushButton(TR("update_release_page"))
    page_btn.setFixedHeight(28)
    page_btn.setStyleSheet(
        f"QPushButton{{background:transparent;color:{t['text_dim']};"
        "border:none;font-size:8pt;text-decoration:underline;}}"
        f"QPushButton:hover{{color:{t['accent']};}}")
    page_btn.clicked.connect(
        lambda: QDesktopServices.openUrl(QUrl(_release_page)))
    lay.addWidget(page_btn)

    # Кнопка закрыть
    close_btn = QPushButton(TR("btn_later"))
    close_btn.setFixedHeight(30)
    close_btn.setStyleSheet(
        f"QPushButton{{background:transparent;color:{t['text_dim']};"
        f"border:1px solid {t['border']};border-radius:6px;padding:0 16px;}}"
        f"QPushButton:hover{{border-color:{t['text']};}}")
    close_btn.clicked.connect(dlg.accept)
    lay.addWidget(close_btn, alignment=Qt.AlignmentFlag.AlignRight)

    dlg.exec()


class UpdateChecker(QThread):
    update_available  = pyqtSignal(str, str)   # version, description
    no_update         = pyqtSignal()
    check_failed      = pyqtSignal(str)

    def run(self):
        if GITHUB_REPO.startswith("YOUR_GITHUB"):
            self.check_failed.emit("GitHub repository not configured.\nEdit GITHUB_REPO constant in source.")
            return
        try:
            req = urllib.request.Request(GITHUB_API_URL,
                headers={"User-Agent": f"{APP_NAME}/{APP_VERSION}"})
            with urllib.request.urlopen(req, timeout=8) as resp:
                data = json.loads(resp.read().decode())
            latest = data.get("tag_name", "").lstrip("v")
            body   = data.get("body", "")[:300]
            if latest and latest != APP_VERSION:
                self.update_available.emit(latest, body)
            else:
                self.no_update.emit()
        except Exception as e:
            self.check_failed.emit(str(e))

class Updater(QThread):
    progress   = pyqtSignal(int)
    finished   = pyqtSignal(bool, str)

    def __init__(self, url: str, dest: str):
        super().__init__()
        self.url  = url
        self.dest = dest

    def run(self):
        try:
            req = urllib.request.Request(self.url,
                headers={"User-Agent": f"{APP_NAME}/{APP_VERSION}"})
            with urllib.request.urlopen(req, timeout=30) as resp:
                total = int(resp.headers.get("Content-Length", 0))
                done  = 0
                data  = b""
                chunk = 8192
                while True:
                    block = resp.read(chunk)
                    if not block:
                        break
                    data += block
                    done += len(block)
                    if total:
                        self.progress.emit(int(done * 100 / total))
            with open(self.dest, "wb") as f:
                f.write(data)
            self.finished.emit(True, self.dest)
        except Exception as e:
            self.finished.emit(False, str(e))

# ═══════════════════════════════════════════════════════════════════════════
#  AUDIO ENGINE  (real low-latency voice)
# ═══════════════════════════════════════════════════════════════════════════

# ─────────────────────────────────────────────────────────────────────────
#  AUDIO MIXER  —  per-peer jitter buffer + mixing
# ─────────────────────────────────────────────────────────────────────────
class PeerBuffer:
    """
    Adaptive jitter buffer for one remote peer.
    Target: hold TARGET_FRAMES frames before playing starts,
    then drain one frame per tick.
    """
    FRAME   = 512          # samples per frame
    BYTES   = FRAME * 2    # int16
    TARGET  = 6            # pre-fill frames before playback (~192 ms)
    MAX     = 25           # hard cap  (~800 ms)

    def __init__(self):
        self._buf    = bytearray()
        self._ready  = False      # True once pre-fill reached
        self._lock   = threading.Lock()
        self.stats_drops = 0

    def push(self, data: bytes):
        with self._lock:
            self._buf.extend(data)
            n = len(self._buf) // self.BYTES
            if n >= self.TARGET:
                self._ready = True
            # Hard cap — drop oldest if overflow
            cap = self.BYTES * self.MAX
            if len(self._buf) > cap:
                excess = len(self._buf) - cap
                del self._buf[:excess]
                self.stats_drops += 1

    def pull(self) -> bytes | None:
        """Return one frame or None (silence) if buffer not ready."""
        with self._lock:
            if not self._ready or len(self._buf) < self.BYTES:
                return None
            frame = bytes(self._buf[:self.BYTES])
            del self._buf[:self.BYTES]
            # If buffer drains below half target, reset pre-fill
            if len(self._buf) // self.BYTES < self.TARGET // 2:
                self._ready = False
            return frame

    def clear(self):
        with self._lock:
            self._buf.clear()
            self._ready = False


class AudioMixer:
    """
    Mixes audio from multiple peers into one output stream.
    Each peer has its own PeerBuffer (jitter buffer).
    On every tick: pull one frame per peer, sum+clip → play.
    """
    FRAME = 512
    BYTES = FRAME * 2

    def __init__(self):
        self._peers: dict[str, PeerBuffer] = {}
        self._lock  = threading.Lock()

    def add_peer(self, ip: str) -> 'PeerBuffer':
        with self._lock:
            if ip not in self._peers:
                self._peers[ip] = PeerBuffer()
            return self._peers[ip]

    def remove_peer(self, ip: str):
        with self._lock:
            self._peers.pop(ip, None)

    def push(self, ip: str, data: bytes):
        with self._lock:
            if ip not in self._peers:
                self._peers[ip] = PeerBuffer()
            self._peers[ip].push(data)

    def mix(self) -> bytes:
        """
        Pull one frame from every peer, sum sample-by-sample with clipping.
        Returns silence frame if nobody has data.
        """
        import array as _arr
        out = _arr.array('h', [0] * self.FRAME)
        got_any = False

        with self._lock:
            peers = list(self._peers.items())

        for ip, buf in peers:
            frame = buf.pull()
            if frame is None:
                continue            # peer buffer not ready → silence for this peer
            got_any = True
            src = _arr.array('h', frame)
            for i in range(self.FRAME):
                out[i] = max(-32768, min(32767, out[i] + src[i]))

        return bytes(out) if got_any else (b'\x00' * self.BYTES)

    def drop_all(self):
        with self._lock:
            for b in self._peers.values():
                b.clear()

    def peer_count(self) -> int:
        with self._lock:
            return len(self._peers)


# ─────────────────────────────────────────────────────────────────────────
#  VAD  —  Voice Activity Detection (webrtcvad optional)
# ─────────────────────────────────────────────────────────────────────────
try:
    import webrtcvad as _webrtcvad
    WEBRTCVAD_AVAILABLE = True
except ImportError:
    WEBRTCVAD_AVAILABLE = False


class VAD:
    """
    Wraps webrtcvad for noise-gating the microphone.
    Aggressiveness 2 = moderate (0=off, 3=aggressive).
    Falls back to always-transmit if webrtcvad not installed.
    """
    # webrtcvad supports only these sample rates
    SUPPORTED_RATES = {8000, 16000, 32000, 48000}
    # Frame durations it accepts (ms) → must match CHUNK
    # 512 samples @ 16kHz = 32 ms  ← not supported by webrtcvad (10/20/30 ms only)
    # So we'll use 480 samples @ 16000 = 30 ms for VAD check
    VAD_SAMPLES = 480
    VAD_BYTES   = VAD_SAMPLES * 2

    def __init__(self, aggressiveness: int = 2):
        self._vad  = None
        self._buf  = bytearray()
        self._gate = True   # last VAD decision
        if WEBRTCVAD_AVAILABLE:
            try:
                self._vad = _webrtcvad.Vad(aggressiveness)
            except Exception:
                pass

    def is_speech(self, raw: bytes, rate: int = 16000) -> bool:
        """Feed raw PCM, returns True if speech detected (or VAD unavailable)."""
        if self._vad is None:
            return True    # no VAD → always transmit
        self._buf.extend(raw)
        result = self._gate
        while len(self._buf) >= self.VAD_BYTES:
            frame = bytes(self._buf[:self.VAD_BYTES])
            del self._buf[:self.VAD_BYTES]
            try:
                result = self._vad.is_speech(frame, rate)
            except Exception:
                result = True
        self._gate = result
        return result


# ─────────────────────────────────────────────────────────────────────────
#  AUDIO ENGINE
# ─────────────────────────────────────────────────────────────────────────
class AudioEngine(QThread):
    audio_captured = pyqtSignal(bytes)
    sig_speaking   = pyqtSignal(bool)   # True=speaking, False=silent

    SAMPLE_RATE = 16000
    CHANNELS    = 1
    CHUNK       = 512    # ~32 ms @ 16kHz
    FORMAT      = None

    def __init__(self):
        super().__init__()
        self.setTerminationEnabled(True)
        self.FORMAT   = None
        self._pa      = None
        self._in      = None
        self._out     = None
        self.running  = False
        self.muted    = False
        self.volume   = 1.0
        self._in_dev  = None
        self._out_dev = None
        self.mixer    = AudioMixer()
        self.vad      = VAD(aggressiveness=2)
        self.vad_enabled = True     # can be toggled in settings

    def _init_pa(self) -> bool:
        if not PYAUDIO_AVAILABLE:
            return False
        # Если старый экземпляр есть но потоки закрыты — пересоздаём
        if self._pa and (self._in is not None or self._out is not None):
            return True   # streams живые — OK
        # Закрываем старый PyAudio перед созданием нового (предотвращает сегфолт)
        if self._pa:
            try: self._pa.terminate()
            except Exception: pass
            self._pa = None
        try:
            import pyaudio as pa
            self.FORMAT = pa.paInt16
            self._pa    = pa.PyAudio()
            return True
        except Exception as e:
            print(f"PyAudio init error: {e}")
            return False

    def start_capture(self, in_dev=None, out_dev=None) -> bool:
        if not self._init_pa():
            return False
        self.stop_all()
        self._in_dev  = in_dev
        self._out_dev = out_dev
        try:
            import pyaudio as pa
            self._in = self._pa.open(
                format=self.FORMAT, channels=self.CHANNELS,
                rate=self.SAMPLE_RATE, input=True,
                frames_per_buffer=self.CHUNK,
                input_device_index=in_dev)
            self._out = self._pa.open(
                format=self.FORMAT, channels=self.CHANNELS,
                rate=self.SAMPLE_RATE, output=True,
                frames_per_buffer=self.CHUNK,
                output_device_index=out_dev)
            self.running = True
            if not self.isRunning():
                self.start()
            return True
        except Exception as e:
            print(f"Audio open error: {e}")
            return False

    def stop_all(self):
        self.running = False
        for stream in [self._in, self._out]:
            if stream:
                try:
                    stream.stop_stream()
                    stream.close()
                except Exception: pass
        self._in = self._out = None
        # Terminate PyAudio instance — необходимо чтобы следующий звонок
        # не получил сегфолт при переинициализации
        if self._pa:
            try: self._pa.terminate()
            except Exception: pass
            self._pa = None

    def cleanup(self):
        self.stop_all()
        if self.isRunning():
            if not self.wait(2000):
                self.terminate()
                self.wait(500)
        if self._pa:
            try: self._pa.terminate()
            except Exception: pass
            self._pa = None

    def push_peer_audio(self, ip: str, data: bytes):
        """Called from network thread — push peer audio into its jitter buffer."""
        self.mixer.push(ip, data)

    def run(self):
        import array as _arr
        SILENCE = b'\x00' * (self.CHUNK * 2)
        while self.running:
            try:
                # ── Capture ──────────────────────────────────────────
                if self._in and not self.muted:
                    raw = self._in.read(self.CHUNK, exception_on_overflow=False)
                    # VAD gate
                    if self.vad_enabled and WEBRTCVAD_AVAILABLE:
                        _is_speech = self.vad.is_speech(raw, self.SAMPLE_RATE)
                    elif self.vad_enabled:
                        # Fallback: amplitude-based voice detection
                        import struct as _st, math as _mth
                        samples = _st.unpack(f'<{len(raw)//2}h', raw)
                        rms = _mth.sqrt(sum(s*s for s in samples) / len(samples))
                        _is_speech = rms > 800   # ~2.4% of full scale
                    else:
                        _is_speech = True
                    if _is_speech:
                        self.audio_captured.emit(raw)
                    # Debounce: only emit speaking signal every 4 frames (~128ms)
                    if not hasattr(self, '_speak_frame_ctr'):
                        self._speak_frame_ctr = 0
                        self._speak_last = False
                    self._speak_frame_ctr += 1
                    if self._speak_frame_ctr >= 4:
                        self._speak_frame_ctr = 0
                        if _is_speech != self._speak_last:
                            self._speak_last = _is_speech
                            self.sig_speaking.emit(_is_speech)
                    # silent frame → don't send
                elif self._in:
                    # still read to keep stream alive
                    self._in.read(self.CHUNK, exception_on_overflow=False)
                    # Emit silence when mic is open but muted
                    if not hasattr(self, '_speak_last'): self._speak_last = True
                    if self._speak_last:
                        self._speak_last = False
                        self.sig_speaking.emit(False)

                # ── Mix + play ────────────────────────────────────────
                if self._out:
                    mixed = self.mixer.mix()
                    if self.volume != 1.0:
                        a = _arr.array('h', mixed)
                        for i in range(len(a)):
                            a[i] = max(-32768, min(32767, int(a[i] * self.volume)))
                        mixed = bytes(a)
                    self._out.write(mixed)

            except Exception as e:
                print(f"Audio loop error: {e}")
                time.sleep(0.01)

    def list_devices(self) -> list[dict]:
        if not self._init_pa():
            return []
        devs = []
        for i in range(self._pa.get_device_count()):
            try:
                info = self._pa.get_device_info_by_index(i)
                devs.append({"index": i, "name": info["name"],
                             "inputs":  info["maxInputChannels"],
                             "outputs": info["maxOutputChannels"]})
            except Exception:
                pass
        return devs


# ═══════════════════════════════════════════════════════════════════════════
#  PROTOCOL CONSTANTS (message types)
# ═══════════════════════════════════════════════════════════════════════════
MSG_PRESENCE  = "presence"
MSG_CHAT      = "chat"
MSG_PRIVATE   = "private"
MSG_GROUP     = "group"
MSG_CALL_REQ    = "call_req"
MSG_CALL_END    = "call_end"
MSG_CALL_ACCEPT = "call_accept"
MSG_CALL_REJECT = "call_reject"
MSG_CALL_RING   = "call_ring"
MSG_CALL        = "call_req"   # alias
MSG_FILE_META = "file_meta"
MSG_FILE_DATA = "file_data"
MSG_GROUP_INV = "group_inv"
MSG_TYPING    = "typing"
MSG_PROFILE   = "profile_req"
MSG_REACTION  = "reaction"      # п.29 — emoji reactions
MSG_EDIT      = "msg_edit"      # п.21 — message editing
MSG_DELETE    = "msg_delete"    # п.21 — message deletion
MSG_READ      = "msg_read"      # п.22 — read receipts
MSG_STICKER   = "sticker"
MSG_TTL_EXPIRE = "ttl_expire"   # message self-destructs after N seconds
MSG_POLL       = "poll"         # interactive poll
MSG_POLL_VOTE  = "poll_vote"    # vote on a poll
MSG_NOTES_SYNC = "notes_sync"  # sync shared notepad

# ═══════════════════════════════════════════════════════════════════════════
#  NETWORK MANAGER
# ═══════════════════════════════════════════════════════════════════════════


TR = Strings()   # global translator — use TR("key")

def _L(ru: str, en: str = "", ja: str = "") -> str:
    """
    Inline локализация без регистрации ключа.
    Использование: _L("Русский текст", "English text", "日本語テキスト")
    Если en/ja пустые — возвращает ru для всех языков.
    """
    lang = S().language
    if lang == "en" and en:
        return en
    if lang == "ja" and ja:
        return ja
    return ru

