#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Apps (GoidaTerminal, Mewa, Visualizer)
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings
from gdf_network  import *
from gdf_ui_base  import *      # TextFormatter, ImageViewer, HoverCard      # NetworkManager, S, AudioEngine

# ═══════════════════════════════════════════════════════════════════════════
#  STICKER PACK MANAGER
# ═══════════════════════════════════════════════════════════════════════════
class StickerPackDialog(QDialog):
    """
    Create and manage sticker packs.
    - Import images as stickers
    - Name the pack
    - Share/import pack JSON
    """
    sticker_selected = pyqtSignal(str)   # base64 image data

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Stickers")
        self.setModal(False)
        self.resize(540, 460)
        self._packs: dict = {}   # pack_name -> [b64, b64, ...]
        self._setup()
        self._load_packs()

    def _setup(self):
        t = get_theme(S().theme)
        self.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        lay = QVBoxLayout(self)
        lay.setContentsMargins(10,10,10,10)
        lay.setSpacing(8)

        # Top: pack selector + controls
        top = QHBoxLayout()
        self._pack_combo = QComboBox()
        self._pack_combo.setMinimumWidth(160)
        self._pack_combo.currentTextChanged.connect(self._show_pack)
        top.addWidget(QLabel(_L("Пак:", "Pack:", "パック:")))
        top.addWidget(self._pack_combo)
        top.addStretch()

        new_btn = QPushButton(_L("+ Новый пак", "+ New pack", "+ 新パック"))
        new_btn.clicked.connect(self._new_pack)
        del_btn = QPushButton(_L("🗑 Удалить пак", "🗑 Delete pack", "🗑 削除"))
        del_btn.clicked.connect(self._delete_pack)
        import_btn = QPushButton(_L("📥 Импорт", "📥 Import", "📥 インポート"))
        import_btn.clicked.connect(self._import_pack)
        export_btn = QPushButton(_L("📤 Экспорт", "📤 Export", "📤 エクスポート"))
        export_btn.clicked.connect(self._export_pack)
        for b in [new_btn, del_btn, import_btn, export_btn]:
            top.addWidget(b)
        lay.addLayout(top)

        # Sticker grid
        self._grid_area = QScrollArea()
        self._grid_area.setWidgetResizable(True)
        self._grid_area.setStyleSheet(f"background:{t['bg3']};border-radius:8px;border:none;")
        self._grid_widget = QWidget()
        self._grid_lay = QGridLayout(self._grid_widget)
        self._grid_lay.setSpacing(6)
        self._grid_area.setWidget(self._grid_widget)
        lay.addWidget(self._grid_area, stretch=1)

        # Add sticker button
        bot = QHBoxLayout()
        add_sticker_btn = QPushButton("+ Добавить стикер из файла")
        add_sticker_btn.setObjectName("accent_btn")
        add_sticker_btn.clicked.connect(self._add_sticker)
        bot.addWidget(add_sticker_btn)
        bot.addStretch()
        close_btn = QPushButton(_L("Закрыть", "Close", "閉じる"))
        close_btn.clicked.connect(self.hide)
        bot.addWidget(close_btn)
        lay.addLayout(bot)

    def _load_packs(self):
        """Load packs. Internal format: dict {name: [b64, ...]}"""
        raw = S().get("sticker_packs", "[]", t=str)
        try:
            data = json.loads(raw)
        except Exception:
            data = []
        # Convert list-of-dicts format → internal dict format
        if isinstance(data, list):
            self._packs = {}
            for p in data:
                if isinstance(p, dict):
                    name = p.get("name", "Без имени")
                    stickers = [s.get("data","") if isinstance(s,dict) else s
                                for s in p.get("stickers",[])]
                    self._packs[name] = [s for s in stickers if s]
                else:
                    self._packs["Мои стикеры"] = []
        elif isinstance(data, dict):
            self._packs = data
        else:
            self._packs = {}
        if not self._packs:
            self._packs["Мои стикеры"] = []
        self._refresh_combo()

    def _save_packs(self):
        """Save packs in unified list-of-dicts format."""
        data = [
            {"name": name, "stickers": [{"data": b64} for b64 in stickers]}
            for name, stickers in self._packs.items()
        ]
        S().set("sticker_packs", json.dumps(data, ensure_ascii=False))

    def _refresh_combo(self):
        self._pack_combo.blockSignals(True)
        cur = self._pack_combo.currentText()
        self._pack_combo.clear()
        for name in self._packs:
            self._pack_combo.addItem(name)
        idx = self._pack_combo.findText(cur)
        if idx >= 0:
            self._pack_combo.setCurrentIndex(idx)
        self._pack_combo.blockSignals(False)
        self._show_pack(self._pack_combo.currentText())

    def _show_pack(self, pack_name: str):
        # Clear grid
        while self._grid_lay.count():
            item = self._grid_lay.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        stickers = self._packs.get(pack_name, [])
        cols = 6
        t = get_theme(S().theme)
        for idx, b64 in enumerate(stickers):
            pm = QPixmap()
            try:
                pm.loadFromData(base64.b64decode(b64))
            except Exception:
                continue
            if pm.isNull():
                continue
            pm = pm.scaled(72, 72,
                           Qt.AspectRatioMode.KeepAspectRatio,
                           Qt.TransformationMode.SmoothTransformation)
            btn = QPushButton()
            btn.setFixedSize(82, 82)
            btn.setIcon(QIcon(pm))
            btn.setIconSize(QSize(72, 72))
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setStyleSheet(f"""
                QPushButton {{
                    background:{t['bg2']};border:1px solid {t['border']};
                    border-radius:8px;
                    border-bottom:2px solid rgba(0,0,0,76);
                }}
                QPushButton:hover {{
                    background:{t['btn_hover']};border-color:{t['accent']};
                }}
            """)
            btn.setToolTip(f"Стикер {idx+1}")
            btn.clicked.connect(lambda _, b=b64: self.sticker_selected.emit(b))
            btn.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
            btn.customContextMenuRequested.connect(
                lambda pos, i=idx, pn=pack_name: self._sticker_ctx(pn, i))
            self._grid_lay.addWidget(btn, idx // cols, idx % cols)

        if not stickers:
            lbl = QLabel("Пак пустой.\nНажмите «+ Добавить стикер» чтобы добавить картинки.")
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl.setStyleSheet(f"color:{t['text_dim']};font-size:12px;")
            lbl.setWordWrap(True)
            self._grid_lay.addWidget(lbl, 0, 0, 1, cols)

    def _sticker_ctx(self, pack_name: str, idx: int):
        menu = QMenu(self)
        menu.addAction("🗑 Удалить стикер", lambda: self._del_sticker(pack_name, idx))
        menu.exec(QCursor.pos())

    def _del_sticker(self, pack_name: str, idx: int):
        if pack_name in self._packs and 0 <= idx < len(self._packs[pack_name]):
            self._packs[pack_name].pop(idx)
            self._save_packs()
            self._show_pack(pack_name)

    def _new_pack(self):
        name, ok = QInputDialog.getText(self, "Новый пак", "Название стикер-пака:")
        if ok and name.strip():
            name = name.strip()
            if name not in self._packs:
                self._packs[name] = []
                self._save_packs()
                self._refresh_combo()
                self._pack_combo.setCurrentText(name)

    def _delete_pack(self):
        name = self._pack_combo.currentText()
        if not name:
            return
        if QMessageBox.question(self, "Удалить пак", f"Удалить пак «{name}»?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)                 == QMessageBox.StandardButton.Yes:
            self._packs.pop(name, None)
            self._save_packs()
            self._refresh_combo()

    def _add_sticker(self):
        pack_name = self._pack_combo.currentText()
        if not pack_name:
            return
        files, _ = QFileDialog.getOpenFileNames(
            self, "Выбрать стикеры", "",
            "Images (*.png *.jpg *.jpeg *.gif *.webp)")
        for fn in files:
            pm = QPixmap(fn)
            if pm.isNull():
                continue
            pm = pm.scaled(256, 256,
                           Qt.AspectRatioMode.KeepAspectRatio,
                           Qt.TransformationMode.SmoothTransformation)
            b64 = pixmap_to_base64(pm)
            self._packs[pack_name].append(b64)
        self._save_packs()
        self._show_pack(pack_name)

    def _export_pack(self):
        pack_name = self._pack_combo.currentText()
        if not pack_name or pack_name not in self._packs:
            return
        fn, _ = QFileDialog.getSaveFileName(
            self, "Экспорт пака", f"{pack_name}.gstickers",
            "GoidaPhone Sticker Pack (*.gstickers)")
        if fn:
            data = json.dumps({"name": pack_name,
                               "stickers": self._packs[pack_name],
                               "version": 1})
            Path(fn).write_text(data, encoding="utf-8")
            QMessageBox.information(self, "Экспорт",
                f"Пак «{pack_name}» экспортирован:\n{fn}")

    def _import_pack(self):
        fn, _ = QFileDialog.getOpenFileName(
            self, "Импорт пака", "",
            "GoidaPhone Sticker Pack (*.gstickers);;JSON (*.json)")
        if not fn:
            return
        try:
            data = json.loads(Path(fn).read_text(encoding="utf-8"))
            name = data.get("name", Path(fn).stem)
            stickers = data.get("stickers", [])
            if not isinstance(stickers, list):
                raise ValueError("bad format")
            if name in self._packs:
                name = f"{name} (импорт)"
            self._packs[name] = stickers
            self._save_packs()
            self._refresh_combo()
            self._pack_combo.setCurrentText(name)
            QMessageBox.information(self, "Импорт",
                f"Пак «{name}» импортирован ({len(stickers)} стикеров).")
        except Exception as e:
            QMessageBox.warning(self, "Error", f"Не удалось импортировать пак:\n{e}")


# ═══════════════════════════════════════════════════════════════════════════
#  GOIDA TERMINAL  (floating in-app console, Shift+F10) FIX!!!!!!!!!
# ═══════════════════════════════════════════════════════════════════════════
class AdminManager:
    """
    First-run admin system. Whoever sets up the admin profile FIRST owns the network.
    Stored in settings — nobody else knows the console exists.
    """
    _ADMIN_KEY = "admin_profile"

    @staticmethod
    def is_admin() -> bool:
        return S().get(AdminManager._ADMIN_KEY + "_set", False, t=bool)

    @staticmethod
    def get_admin_name() -> str:
        return S().get(AdminManager._ADMIN_KEY + "_name", "", t=str)

    @staticmethod
    def setup_admin(name: str, password: str) -> bool:
        import hashlib
        pw_hash = hashlib.sha256(password.encode()).hexdigest()
        S().set(AdminManager._ADMIN_KEY + "_set",  True)
        S().set(AdminManager._ADMIN_KEY + "_name", name)
        S().set(AdminManager._ADMIN_KEY + "_hash", pw_hash)
        return True

    @staticmethod
    def verify_admin(password: str) -> bool:
        import hashlib
        stored = S().get(AdminManager._ADMIN_KEY + "_hash", "", t=str)
        return hashlib.sha256(password.encode()).hexdigest() == stored

    @staticmethod
    def get_banned() -> list:
        import json
        raw = S().get("admin_banned", "[]", t=str)
        try: return json.loads(raw)
        except: return []

    @staticmethod
    def ban(ip: str):
        import json
        banned = AdminManager.get_banned()
        if ip not in banned:
            banned.append(ip)
        S().set("admin_banned", json.dumps(banned))

    @staticmethod
    def unban(ip: str):
        import json
        banned = AdminManager.get_banned()
        if ip in banned: banned.remove(ip)
        S().set("admin_banned", json.dumps(banned))

    @staticmethod
    def get_muted() -> list:
        import json
        raw = S().get("admin_muted", "[]", t=str)
        try: return json.loads(raw)
        except: return []

    @staticmethod
    def mute(ip: str):
        import json
        muted = AdminManager.get_muted()
        if ip not in muted: muted.append(ip)
        S().set("admin_muted", json.dumps(muted))

    @staticmethod
    def unmute(ip: str):
        import json
        muted = AdminManager.get_muted()
        if ip in muted: muted.remove(ip)
        S().set("admin_muted", json.dumps(muted))

    @staticmethod
    def get_network_password() -> str:
        return S().get("admin_net_password", "", t=str)

    @staticmethod
    def set_network_password(pw: str):
        import hashlib
        S().set("admin_net_password",
                hashlib.sha256(pw.encode()).hexdigest() if pw else "")
        S().set("admin_net_password_enabled", bool(pw))

    @staticmethod
    def network_password_enabled() -> bool:
        return S().get("admin_net_password_enabled", False, t=bool)


class GoidaTerminal(QWidget):
    """
    Goida Terminal — встроенный терминал GoidaPhone
    Shift+F10 для открытия | Tab — автодополнение | ↑↓ — история команд
    """

    # ── ANSI-like color palette ────────────────────────────────────────
    C_GREEN   = "#39FF14"   # output / success
    C_YELLOW  = "#FFD700"   # prompt / warnings
    C_CYAN    = "#00E5FF"   # info / headers
    C_RED     = "#FF4444"   # errors / ban
    C_MAGENTA = "#FF44FF"   # special events
    C_WHITE   = "#E0E0E0"   # normal text
    C_DIM     = "#606070"   # dimmed / border art
    C_ORANGE  = "#FF8C00"   # kick / mute
    C_BLUE    = "#6080FF"   # admin badge

    # ── All commands for tab-completion ───────────────────────────────
    ALL_COMMANDS = [
        "/help", "/h", "/clear", "/cls", "/quit", "/exit",
        "/version", "/ver", "/sysinfo", "/uptime", "/date",
        "/whoami", "/colors", "/reload",
        "/nick", "/peers", "/ping", "/traceroute", "/who", "/whois",
        "/me", "/say", "/broadcast",
        "/history", "/history clear", "/history export",
        "/stats", "/netstat", "/crypto",
        "/theme", "/font", "/resize",
        "/log", "/log tail", "/log clear", "/log export",
        "/admin", "/admin setup", "/admin login", "/admin logout",
        "/admin status", "/admin reset",
        "/users", "/ban", "/unban", "/kick", "/mute", "/unmute",
        "/banlist", "/muteall", "/unmuteall",
        "/netpw", "/netpw_off", "/network", "/network block", "/network allow",
        "/group", "/group list", "/group kick", "/group info",
        "/msg", "/file", "/call",
        "/monitor", "/monitor on", "/monitor off",
        "/wipe", "/restart", "/about",
        "/goida", "/goida --time-1", "/goida --time-5", "/goida --time-10",
        "/goida --color-cyan", "/goida --color-green", "/goida --color-magenta",
        "/sounds", "/sounds test", "/sounds install",
        "/cmatrix", "/cmatrix --color-green", "/cmatrix --speed-5",
        "/mc", "/mc --color-cyan",
    ]

    def __init__(self, net, parent=None):
        super().__init__(parent)
        self._net          = net
        self._collapsed    = False
        self._drag_pos: QPoint | None = None
        self._admin_authed = False
        self._cmd_history: list[str] = self._load_term_history()
        self._hist_idx     = -1
        self._monitor_timer: QTimer | None = None
        self._monitor_active = False
        self._start_time   = time.time()
        self._log_entries: list[str] = []     # internal log buffer
        self._tab_cmp_idx  = -1
        self._tab_cmp_matches: list[str] = []
        self.setWindowFlags(Qt.WindowType.Widget)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setMinimumSize(700, 460)
        self.resize(860, 560)
        self._setup_ui()
        self._setup_shortcuts()

    def _load_term_history(self) -> list:
        try:
            raw = _load_raw_setting("terminal_cmd_history", "[]")
            data = json.loads(raw)
            return list(data) if isinstance(data, list) else []
        except Exception:
            return []

    def _save_term_history(self):
        try:
            _save_raw_setting("terminal_cmd_history",
                              json.dumps(self._cmd_history[-200:]))
        except Exception:
            pass

    # ══════════════════════════════════════════════════════════════════
    #  UI SETUP
    # ══════════════════════════════════════════════════════════════════
    def _setup_ui(self):
        self.setObjectName("goida_terminal")
        _tt = get_theme(S().theme)
        self._tt = _tt
        # BIOS-style: always black background regardless of theme
        # Accent colour adapts to theme (cyan for default, purple for purple theme etc.)
        _tbrd = _tt.get('accent', '#00E5FF')
        self.C_GREEN   = _tt.get('accent', '#39FF14')   # adapt primary colour to theme
        self.C_CYAN    = _tt.get('accent2', '#00E5FF')   # secondary
        self.setStyleSheet(f"""
            #goida_terminal {{
                background: #000000;
                border: 1px solid {_tbrd};
                border-radius: 6px;
            }}
        """)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        root.addWidget(self._build_titlebar())
        root.addWidget(self._build_tabs())
        root.addWidget(self._build_statusbar())

    def _build_titlebar(self) -> QWidget:
        bar = QWidget()
        bar.setObjectName("term_title")
        bar.setFixedHeight(38)
        bar.setStyleSheet("""
            #term_title {
                background: #111111;
                border-radius: 5px 5px 0 0;
                border-bottom: 1px solid #333333;
            }
        """)
        bar.setCursor(Qt.CursorShape.SizeAllCursor)
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(14, 0, 10, 0)
        lay.setSpacing(8)

        # Traffic-light dots — RIGHT side (Windows/Linux convention)
        self._traffic_dots = []
        for col, tip in [("#FF5F56",_L("Закрыть", "Close", "閉じる")),("#FFBD2E","Свернуть"),("#27C93F","Развернуть")]:
            d = QLabel("⬤")
            d.setStyleSheet(f"color:{col};font-size:11px;background:transparent;")
            d.setToolTip(tip)
            self._traffic_dots.append(d)

        lay.addSpacing(6)

        # Icon + title
        ico = QLabel("⌨")
        ico.setStyleSheet("color:#6060CC;font-size:14px;background:transparent;")
        lay.addWidget(ico)

        _acc2 = self._tt.get('accent', '#7C4DFF')
        title = QLabel("Goida")
        title.setStyleSheet(
            f"color:{_acc2};font-size:13px;font-weight:bold;"
            "letter-spacing:4px;background:transparent;font-family:monospace;")
        lay.addWidget(title)
        sub = QLabel("Terminal")
        sub.setStyleSheet(
            "color:#555566;font-size:9px;background:transparent;"
            "font-family:monospace;letter-spacing:2px;margin-left:2px;")
        lay.addWidget(sub)

        self._admin_badge = QLabel("◆ ADMIN")
        self._admin_badge.setStyleSheet(
            "color:#FFD700;font-size:9px;font-weight:bold;"
            "background:#1A1500;border:1px solid #605000;"
            "border-radius:3px;padding:1px 5px;")
        self._admin_badge.setVisible(self._admin_authed)
        lay.addWidget(self._admin_badge)

        self._monitor_badge = QLabel("● LIVE")
        self._monitor_badge.setStyleSheet(
            "color:#39FF14;font-size:9px;font-weight:bold;"
            "background:#001400;border:1px solid #003300;"
            "border-radius:3px;padding:1px 5px;")
        self._monitor_badge.setVisible(False)
        lay.addWidget(self._monitor_badge)

        lay.addStretch()

        for label, tip, cb in [
            ("▁", "Свернуть",   self._collapse),
            ("⛶", "Развернуть", self._expand),
            ("✕", _L("Закрыть", "Close", "閉じる"),    self.hide),
        ]:
            b = QPushButton(label)
            b.setFixedSize(24, 24)
            b.setToolTip(tip)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
            _btn_acc = self._tt.get('accent', '#7C4DFF')
            _btn_bg  = self._tt.get('bg3', '#151530')
            _btn_dim = self._tt.get('text_dim', '#8080A0')
            b.setStyleSheet(f"""
                QPushButton {{
                    background:{_btn_bg};color:{_btn_dim};
                    border:none;border-radius:12px;
                    font-size:10px;font-weight:bold;
                }}
                QPushButton:hover{{background:{_btn_acc}22;color:{_btn_acc};}}
            """)
            b.clicked.connect(cb)
            lay.addWidget(b)

        bar.mousePressEvent   = self._tb_press
        bar.mouseMoveEvent    = self._tb_move
        bar.mouseReleaseEvent = self._tb_release
        return bar

    def _build_tabs(self) -> QTabWidget:
        self._tabs = QTabWidget()
        _acc = self._tt.get('accent', '#7C4DFF')
        _acc_tab = self._tt.get('accent', '#39FF14')
        self._tabs.setStyleSheet(f"""
            QTabWidget::pane {{
                border: none;
                background: #000000;
                border-radius: 0;
            }}
            QTabBar {{
                background: #080808;
            }}
            QTabBar::tab {{
                background: transparent;
                color: #444444;
                font-size: 9px;
                font-weight: 700;
                letter-spacing: 2px;
                font-family: monospace;
                padding: 7px 18px 6px 18px;
                border: none;
                border-bottom: 2px solid transparent;
                margin-right: 1px;
                min-width: 72px;
                border-radius: 0;
            }}
            QTabBar::tab:selected {{
                background: #0D0D0D;
                color: {_acc_tab};
                border-bottom: 2px solid {_acc_tab};
            }}
            QTabBar::tab:hover:!selected {{
                background: #111111;
                color: #666666;
                border-bottom: 2px solid #222222;
            }}
        """)

        # ── Tab 1: Terminal ──
        term_w = QWidget()
        _tt = getattr(self, '_tt', get_theme(S().theme))
        term_w.setStyleSheet(f"background:{_tt.get('bg','#08080F')};")
        tl = QVBoxLayout(term_w)
        tl.setContentsMargins(0, 0, 0, 0)
        tl.setSpacing(0)

        self._output = QPlainTextEdit()
        self._output.setReadOnly(True)
        self._output.setMaximumBlockCount(2000)
        # BIOS-style: pure black, grey text, green for active content
        _tc = self._tt.get('accent', '#39FF14')
        self._output.setStyleSheet(f"""
            QPlainTextEdit {{
                background: #000000;
                color: #A0A0A0;
                font-family: 'JetBrains Mono','Fira Code','Courier New','DejaVu Sans Mono',monospace;
                font-size: 11px;
                border: none;
                padding: 10px 12px;
                selection-background-color: #333333;
            }}
            QScrollBar:vertical {{
                background: #111111; width: 7px; margin: 0;
            }}
            QScrollBar::handle:vertical {{
                background: #333333; border-radius: 3px; min-height: 20px;
            }}
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
                height: 0;
            }}
        """)
        tl.addWidget(self._output, stretch=1)
        tl.addWidget(self._build_input_row())
        self._tabs.addTab(term_w, "⌨  TERMINAL")

        # ── Tab 2: Network monitor ──
        self._monitor_w = self._build_monitor_tab()
        self._tabs.addTab(self._monitor_w, "◈  NETWORK")

        # ── Tab 3: Users ──
        self._users_w = self._build_users_tab()
        self._tabs.addTab(self._users_w, "◉  USERS")

        # ── Tab 4: Log ──
        self._log_w = self._build_log_tab()
        self._tabs.addTab(self._log_w, "▤  LOG")

        # ── Tab 5: Network Graph ──
        self._graph_tab = self._build_graph_tab()
        self._tabs.addTab(self._graph_tab, "◎  GRAPH")

        self._tabs.currentChanged.connect(self._on_tab_changed)
        return self._tabs

    def _build_input_row(self) -> QWidget:
        row_w = QWidget()
        row_w.setStyleSheet("background:#0A0A0A;border-top:1px solid #2A2A2A;")
        row_w.setFixedHeight(44)
        row = QHBoxLayout(row_w)
        row.setContentsMargins(12, 6, 10, 6)
        row.setSpacing(8)

        # Prompt shows username@hostname
        try:
            hostname = platform.node().split(".")[0]
            user     = S().username or "user"
        except Exception:
            hostname = "goidaphone"; user = "user"

        _tc2 = self._tt.get('accent', '#39FF14')
        self._prompt_lbl = QLabel("goida ❯")
        self._prompt_lbl.setStyleSheet(
            f"color:{_tc2};font-weight:bold;font-size:12px;"
            "font-family:monospace;background:transparent;"
            "padding-right:4px;")
        row.addWidget(self._prompt_lbl)

        self._input = QLineEdit()
        self._input.setPlaceholderText("Введите команду... (Tab — автодополнение, ↑↓ — история)")
        self._input.setStyleSheet("""
            QLineEdit {
                background: transparent;
                color: #CCCCCC;
                border: none;
                font-family: 'JetBrains Mono','Courier New',monospace;
                font-size: 11px;
                padding: 0;
                selection-background-color: #444444;
            }
            QLineEdit:focus { border: none; }
        """)
        self._input.returnPressed.connect(self._run_cmd)
        self._input.installEventFilter(self)
        row.addWidget(self._input, stretch=1)

        run_btn = QPushButton("⏎ RUN")
        run_btn.setFixedSize(58, 28)
        run_btn.setStyleSheet("""
            QPushButton {
                background: #1A1A40;
                color: #6060CC;
                border: 1px solid #2A2A5A;
                border-radius: 4px;
                font-size: 9px;
                font-weight: bold;
                letter-spacing: 1px;
            }
            QPushButton:hover {
                background: #252560;
                color: #A0A0FF;
                border-color: #4040A0;
            }
            QPushButton:pressed { background: #0D0D30; }
        """)
        run_btn.clicked.connect(self._run_cmd)
        row.addWidget(run_btn)
        return row_w

    def _build_monitor_tab(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet("background:#000000;")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12, 10, 12, 10)
        lay.setSpacing(8)

        hdr = QLabel("◈  NETWORK MONITOR")
        hdr.setStyleSheet("color:#6060CC;font-size:10px;font-weight:bold;"
                          "letter-spacing:2px;font-family:monospace;")
        lay.addWidget(hdr)

        self._mon_output = QPlainTextEdit()
        self._mon_output.setReadOnly(True)
        self._mon_output.setMaximumBlockCount(500)
        self._mon_output.setStyleSheet("""
            QPlainTextEdit {
                background:#040408;color:#00E5FF;
                font-family:'Courier New',monospace;font-size:10px;
                border:1px solid #1A1A3A;border-radius:4px;padding:6px;
            }
        """)
        lay.addWidget(self._mon_output, stretch=1)

        btn_row = QHBoxLayout()
        for label, cb in [
            ("▶ Запустить монитор", lambda: self._start_monitor()),
            ("■ Остановить",        lambda: self._stop_monitor()),
            ("🗑 Clear",         lambda: self._mon_output.clear()),
        ]:
            b = QPushButton(label)
            b.setStyleSheet("""
                QPushButton{background:#111128;color:#6080FF;border:1px solid #202050;
                    border-radius:4px;padding:4px 12px;font-size:9px;font-weight:bold;}
                QPushButton:hover{background:#1A1A40;color:#A0A0FF;}
            """)
            b.clicked.connect(cb)
            btn_row.addWidget(b)
        btn_row.addStretch()
        lay.addLayout(btn_row)
        return w

    def _build_graph_tab(self) -> QWidget:
        """Network topology graph — visualizes peers as nodes."""
        w = QWidget()
        w.setStyleSheet("background:#000000;")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(0,0,0,0); lay.setSpacing(0)

        class _GraphWidget(QWidget):
            def __init__(self, net_ref, parent=None):
                super().__init__(parent)
                self._net = net_ref
                self._tt  = get_theme(S().theme)
                self.setMinimumHeight(300)
                self._timer = QTimer(self)
                self._timer.timeout.connect(self.update)
                self._timer.start(3000)

            def paintEvent(self, event):
                from PyQt6.QtGui import QPainter, QFont, QPen, QColor, QBrush
                import math
                p = QPainter(self)
                p.setRenderHint(QPainter.RenderHint.Antialiasing)
                p.fillRect(self.rect(), QColor("#000000"))
                peers = getattr(self._net, 'peers', {}) if self._net else {}
                nodes = [("ME", None, True)] + [(v.get("username","?"), k, False)
                                                for k, v in peers.items()]
                n = len(nodes)
                if n == 0: return
                cx, cy = self.width()//2, self.height()//2
                r = min(cx, cy) - 50
                pos = {}
                for i, (name, ip, is_me) in enumerate(nodes):
                    if n == 1:
                        angle = 0
                    else:
                        angle = 2 * math.pi * i / n - math.pi/2
                    nx = cx + int(r * math.cos(angle)) if not is_me else cx
                    ny = cy + int(r * math.sin(angle)) if not is_me else cy
                    pos[name] = (nx, ny)

                    # Draw edge to center
                    if not is_me:
                        pen = QPen(QColor("#2A2A5A"), 1)
                        p.setPen(pen)
                        p.drawLine(cx, cy, nx, ny)

                    # Draw node
                    _gc = self._tt.get('accent', '#7C4DFF')
                    color = QColor(_gc) if is_me else QColor("#2D6A4F")
                    p.setBrush(QBrush(color))
                    p.setPen(QPen(QColor("#A0A0FF") if is_me else QColor("#74C69D"), 2))
                    node_r = 22 if is_me else 18
                    p.drawEllipse(nx - node_r, ny - node_r, node_r*2, node_r*2)

                    # Label
                    p.setPen(QPen(QColor("#FFFFFF")))
                    p.setFont(QFont("monospace", 8, QFont.Weight.Bold))
                    short = name[:8]
                    p.drawText(nx - 30, ny + node_r + 14, 60, 16,
                               Qt.AlignmentFlag.AlignCenter, short)
                    if ip:
                        p.setFont(QFont("monospace", 7))
                        p.setPen(QPen(QColor(self._tt.get('text_dim', '#666688'))))
                        p.drawText(nx - 35, ny + node_r + 26, 70, 14,
                                   Qt.AlignmentFlag.AlignCenter, ip.split(".")[-1])
                p.end()

        self._graph_w = _GraphWidget(getattr(self, '_net', None))
        lay.addWidget(self._graph_w, stretch=1)

        refresh_btn = QPushButton("↻ Обновить")
        refresh_btn.setFixedHeight(28)
        refresh_btn.setStyleSheet(
            "QPushButton{background:#111122;color:#7070BB;"
            "border:none;border-top:1px solid #1A1A3A;font-size:10px;}"
            f"QPushButton:hover{{color:#A0A0FF;}}")
        refresh_btn.clicked.connect(self._graph_w.update)
        lay.addWidget(refresh_btn)
        return w

    def _build_users_tab(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet("background:#000000;")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12, 10, 12, 10)
        lay.setSpacing(8)

        hdr_row = QHBoxLayout()
        hdr = QLabel("◉  ONLINE USERS")
        hdr.setStyleSheet("color:#6060CC;font-size:10px;font-weight:bold;"
                          "letter-spacing:2px;font-family:monospace;")
        hdr_row.addWidget(hdr)
        hdr_row.addStretch()
        ref_btn = QPushButton("⟳ Обновить")
        ref_btn.setStyleSheet("""
            QPushButton{background:#111128;color:#6080FF;border:1px solid #202050;
                border-radius:4px;padding:3px 10px;font-size:9px;}
            QPushButton:hover{background:#1A1A40;color:#A0A0FF;}
        """)
        ref_btn.clicked.connect(self._refresh_users_tab)
        hdr_row.addWidget(ref_btn)
        lay.addLayout(hdr_row)

        self._users_table = QTableWidget(0, 5)
        self._users_table.setHorizontalHeaderLabels(
            ["Ник", "IP", "Версия", "E2E", "Статус"])
        self._users_table.horizontalHeader().setStretchLastSection(True)
        self._users_table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        self._users_table.verticalHeader().setVisible(False)
        self._users_table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self._users_table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self._users_table.setStyleSheet("""
            QTableWidget {
                background:#040408;color:#C0C0E0;
                font-family:monospace;font-size:10px;
                border:1px solid #1A1A3A;border-radius:4px;
                gridline-color:#111128;
            }
            QHeaderView::section {
                background:#0D0D1F;color:#6060CC;
                font-size:9px;font-weight:bold;letter-spacing:1px;
                border:none;border-bottom:1px solid #2A2A5A;padding:4px;
            }
            QTableWidget::item:selected { background:#1A1A40; }
        """)
        lay.addWidget(self._users_table, stretch=1)

        # Quick-action buttons
        act_row = QHBoxLayout()
        for label, color, cmd_fn in [
            ("⊘ Ban",    "#FF4040", lambda: self._table_action("ban")),
            ("↯ Kick",   "#FF8C00", lambda: self._table_action("kick")),
            ("⊗ Mute",   "#FFAA00", lambda: self._table_action("mute")),
            ("✓ Unban",  "#39FF14", lambda: self._table_action("unban")),
            ("✓ Unmute", "#39FF14", lambda: self._table_action("unmute")),
            ("◌ Whois",  "#00E5FF", lambda: self._table_action("whois")),
        ]:
            b = QPushButton(label)
            b.setStyleSheet(f"""
                QPushButton{{background:#111128;color:{color};border:1px solid #202050;
                    border-radius:4px;padding:4px 10px;font-size:9px;font-weight:bold;}}
                QPushButton:hover{{background:#1A1A40;}}
            """)
            b.clicked.connect(cmd_fn)
            act_row.addWidget(b)
        act_row.addStretch()
        lay.addLayout(act_row)
        return w

    def _build_log_tab(self) -> QWidget:
        w = QWidget()
        w.setStyleSheet("background:#000000;")
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12, 10, 12, 10)
        lay.setSpacing(8)

        hdr = QLabel("▤  SYSTEM LOG")
        hdr.setStyleSheet("color:#6060CC;font-size:10px;font-weight:bold;"
                          "letter-spacing:2px;font-family:monospace;")
        lay.addWidget(hdr)

        self._log_output = QPlainTextEdit()
        self._log_output.setReadOnly(True)
        self._log_output.setMaximumBlockCount(1000)
        self._log_output.setStyleSheet("""
            QPlainTextEdit {
                background:#040408;color:#A0A0C0;
                font-family:'Courier New',monospace;font-size:10px;
                border:1px solid #1A1A3A;border-radius:4px;padding:6px;
            }
        """)
        lay.addWidget(self._log_output, stretch=1)

        btn_row = QHBoxLayout()
        for label, cb in [
            ("📋 Копировать всё", lambda: QApplication.clipboard().setText(
                self._log_output.toPlainText())),
            ("💾 Экспорт в файл", self._export_log),
            ("🗑 Clear",       lambda: (self._log_entries.clear(),
                                           self._log_output.clear())),
        ]:
            b = QPushButton(label)
            b.setStyleSheet("""
                QPushButton{background:#111128;color:#6080FF;border:1px solid #202050;
                    border-radius:4px;padding:4px 12px;font-size:9px;font-weight:bold;}
                QPushButton:hover{background:#1A1A40;color:#A0A0FF;}
            """)
            b.clicked.connect(cb)
            btn_row.addWidget(b)
        btn_row.addStretch()
        lay.addLayout(btn_row)
        return w

    def _build_statusbar(self) -> QWidget:
        bar = QWidget()
        bar.setFixedHeight(22)
        bar.setStyleSheet("""
            background: #0A0A18;
            border-top: 1px solid #141428;
            border-radius: 0 0 9px 9px;
        """)
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(12, 0, 12, 0)
        lay.setSpacing(16)

        self._sb_peers  = QLabel("◉ Peers: 0")
        self._sb_crypto = QLabel("🔒 E2E")
        self._sb_uptime = QLabel("⏱ 0s")
        self._sb_time   = QLabel("")

        for lbl in [self._sb_peers, self._sb_crypto, self._sb_uptime]:
            lbl.setStyleSheet(f"color:{self._tt.get('text_dim','#444466')};font-size:8px;font-family:monospace;background:transparent;")
            lay.addWidget(lbl)

        lay.addStretch()
        self._sb_time.setStyleSheet("color:#202040;font-size:8px;font-family:monospace;background:transparent;")
        lay.addWidget(self._sb_time)

        self._sb_timer = QTimer(self)
        self._sb_timer.timeout.connect(self._update_statusbar)
        self._sb_timer.start(1000)
        return bar

    def _setup_shortcuts(self):
        """Keyboard shortcut inside terminal widget."""
        pass  # handled by eventFilter on _input

    # ══════════════════════════════════════════════════════════════════
    #  EVENT FILTER — history navigation + tab completion
    # ══════════════════════════════════════════════════════════════════
    def eventFilter(self, obj, event):
        if obj is self._input and event.type() == event.Type.KeyPress:
            key = event.key()
            if key == Qt.Key.Key_Up:
                self._history_up(); return True
            elif key == Qt.Key.Key_Down:
                self._history_down(); return True
            elif key == Qt.Key.Key_Tab:
                self._tab_complete(); return True
            else:
                # Reset tab completion on any other key
                self._tab_cmp_idx = -1
                self._tab_cmp_matches = []
        return super().eventFilter(obj, event)

    def _history_up(self):
        if not self._cmd_history: return
        self._hist_idx = max(0, self._hist_idx - 1 if self._hist_idx >= 0
                             else len(self._cmd_history) - 1)
        self._input.setText(self._cmd_history[self._hist_idx])
        self._input.end(False)

    def _history_down(self):
        if self._hist_idx < 0: return
        self._hist_idx += 1
        if self._hist_idx >= len(self._cmd_history):
            self._hist_idx = -1
            self._input.clear()
        else:
            self._input.setText(self._cmd_history[self._hist_idx])
            self._input.end(False)

    def _tab_complete(self):
        text = self._input.text()
        if not text.startswith("/"):
            return
        # Build match list fresh if input changed
        prefix = text.strip()
        if not self._tab_cmp_matches or self._tab_cmp_idx == -1:
            self._tab_cmp_matches = [c for c in self.ALL_COMMANDS
                                     if c.startswith(prefix)]
            self._tab_cmp_idx = -1
        if not self._tab_cmp_matches:
            return
        self._tab_cmp_idx = (self._tab_cmp_idx + 1) % len(self._tab_cmp_matches)
        self._input.setText(self._tab_cmp_matches[self._tab_cmp_idx])
        self._input.end(False)

    # ══════════════════════════════════════════════════════════════════
    #  PRINT HELPERS
    # ══════════════════════════════════════════════════════════════════
    def _print(self, text: str, color: str = None):
        color = color or self.C_GREEN
        ts = datetime.now().strftime("%H:%M:%S")
        escaped = (text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
                       .replace(" ","&nbsp;").replace("\n","<br>"))
        self._output.appendHtml(
            f"<span style='color:{color};font-family:monospace;'>{escaped}</span>")
        # Also add to log
        self._add_log(f"[{ts}] {text}")

    def _println(self, text: str = "", color: str = None):
        self._print(text, color)

    def _print_sep(self, char="─", width=68, color=None):
        self._print(char * width, color or self.C_DIM)

    def _add_log(self, text: str):
        self._log_entries.append(text)
        if len(self._log_entries) > 1000:
            self._log_entries = self._log_entries[-800:]
        if hasattr(self, '_log_output'):
            ts = datetime.now().strftime("%H:%M:%S")
            escaped = (text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;"))
            self._log_output.appendHtml(
                f"<span style='color:#505070;font-size:9px;'>{escaped}</span>")

    def _mon_print(self, text: str, color: str = "#00E5FF"):
        escaped = (text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
                       .replace(" ","&nbsp;"))
        self._mon_output.appendHtml(
            f"<span style='color:{color};font-family:monospace;font-size:10px;'>{escaped}</span>")

    # ══════════════════════════════════════════════════════════════════
    #  BOOT HEADER
    # ══════════════════════════════════════════════════════════════════
    def _print_header(self):
        self._output.clear()
        t_str  = datetime.now().strftime("%Y-%m-%d  %H:%M:%S")
        _acc   = self.C_GREEN
        _acc2  = self.C_CYAN
        _dim   = self.C_DIM
        self._print("")
        self._print("  ██████╗  ██████╗ ██╗██████╗  █████╗ ", _acc)
        self._print(" ██╔════╝ ██╔═══██╗██║██╔══██╗██╔══██╗", _acc)
        self._print(" ██║  ███╗██║   ██║██║██║  ██║███████║", _acc2)
        self._print(" ██║   ██║██║   ██║██║██║  ██║██╔══██║", _acc2)
        self._print(" ╚██████╔╝╚██████╔╝██║██████╔╝██║  ██║", _acc)
        self._print("  ╚═════╝  ╚═════╝ ╚═╝╚═════╝ ╚═╝  ╚═╝", _acc)
        self._print("")
        self._print(f"  Terminal  ·  GoidaPhone v{APP_VERSION}  ·  {t_str}", _dim)
        self._print(f"  {COMPANY_NAME}", _dim)
        self._print("  " + "─" * 54, _dim)
        self._print("")
        if AdminManager.is_admin():
            self._print(f"  ◆ Авторизован: {AdminManager.get_admin_name()}", self.C_YELLOW)
            if AdminManager.network_password_enabled():
                self._print("  ● Пароль сети активен", self.C_GREEN)
        else:
            self._print("  ⚠  Администратор не настроен.", self.C_ORANGE)
            self._print("     /admin setup <name> <пароль>", _dim)
        self._print("")
        self._print("  /help — список команд   Tab — автодополнение   ↑↓ — история", _dim)
        self._print("")

    # ══════════════════════════════════════════════════════════════════
    #  COMMAND DISPATCHER
    # ══════════════════════════════════════════════════════════════════
    def _run_cmd(self):
        raw = self._input.text().strip()
        self._input.clear()
        self._tab_cmp_idx = -1
        self._tab_cmp_matches = []
        # Останавливаем любую анимацию если она идёт
        if getattr(self, '_goida_running', False):
            self._goida_running = False
            return
        if getattr(self, '_matrix_running', False):
            self._matrix_running = False
            if hasattr(self, '_matrix_timer'): self._matrix_timer.stop()
            self._print("  ✓ cmatrix stopped", self.C_GREEN)
            return
        if getattr(self, '_mc_running', False):
            self._mc_running = False
            if hasattr(self, '_mc_timer'): self._mc_timer.stop()
            self._print("  ✓ mc stopped", self.C_GREEN)
            return
        if not raw:
            return
        if not self._cmd_history or self._cmd_history[-1] != raw:
            self._cmd_history.append(raw)
            if len(self._cmd_history) > 200:
                self._cmd_history = self._cmd_history[-200:]
            self._save_term_history()
        self._hist_idx = -1
        self._tabs.setCurrentIndex(0)  # focus terminal tab

        self._print(f"» {raw}", self.C_YELLOW)

        parts = raw.split(maxsplit=3)
        cmd   = parts[0].lower()
        arg1  = parts[1] if len(parts) > 1 else ""
        arg2  = parts[2] if len(parts) > 2 else ""
        arg3  = parts[3] if len(parts) > 3 else ""

        # ── Universal (no auth) ────────────────────────────────────────
        if cmd in ("/help", "/h", "/?"):
            self._cmd_help(arg1)
        elif cmd in ("/clear", "/cls"):
            self._output.clear()
            self._print_header()
            return
        elif cmd in ("/quit", "/exit", "/q"):
            self.hide(); return
        elif cmd in ("/version", "/ver"):
            self._cmd_version()
        elif cmd == "/whoami":
            self._print_sep()
            cfg = S()
            self._print(f"  Ник:        {cfg.username}", self.C_CYAN)
            self._print(f"  IP:         {get_local_ip()}", self.C_WHITE)
            all_ips = get_all_local_ips()
            if len(all_ips) > 1:
                for ip in sorted(all_ips):
                    self._print(f"    └ {ip}", self.C_DIM)
            self._print(f"  GoidaID:    {display_id(get_local_ip())}", self.C_DIM)
            self._print(f"  Премиум:    {'✓' if cfg.premium else '✗'}", self.C_WHITE)
            self._print(f"  Status:     {cfg.user_status}", self.C_WHITE)
            self._print(f"  Theme:       {cfg.theme}", self.C_WHITE)
            self._print(f"  Язык:       {cfg.language}", self.C_WHITE)
            self._print_sep()

        elif cmd == "/colors":
            self._print_sep()
            self._print("  ЦВЕТОВАЯ ПАЛИТРА ZLink", self.C_CYAN)
            self._print_sep()
            for name, col in [
                ("GREEN  (успех/вывод)", self.C_GREEN),
                ("YELLOW (внимание)",    self.C_YELLOW),
                ("CYAN   (информация)",  self.C_CYAN),
                ("RED    (ошибка)",      self.C_RED),
                ("MAGENTA (сигнал)",     self.C_MAGENTA),
                ("ORANGE (предупрежд.)", self.C_ORANGE),
                ("BLUE   (служебный)",   self.C_BLUE),
                ("WHITE  (текст)",       self.C_WHITE),
                ("DIM    (второстепен.)",self.C_DIM),
            ]:
                self._print(f"  ● {name}", col)
            self._print_sep()

        elif cmd in ("/sounds", "/sounds test", "/sounds install"):
            sub = raw.split(" ", 1)[1].strip() if " " in raw else ""
            if sub == "test":
                self._print_sep()
                self._print("  ТЕСТ ЗВУКОВ GoidaPhone", self.C_CYAN)
                self._print_sep()
                # Check QMediaPlayer availability
                try:
                    from PyQt6.QtMultimedia import QMediaPlayer
                    self._print("  ✓ QMediaPlayer (PyQt6.QtMultimedia) — доступен", self.C_GREEN)
                except ImportError:
                    self._print("  ✗ QMediaPlayer недоступен — нужен пакет qt6-multimedia", self.C_RED)
                for ev in ["click", "message", "online", "delete", "error", "critical"]:
                    fname_list = _SOUND_MAP.get(ev, [])
                    found = None
                    for fn in fname_list:
                        found = _find_sound_file(fn)
                        if found:
                            break
                    if found:
                        self._print(f"  ▶ {ev:12s}  ✓  {found}", self.C_GREEN)
                        play_system_sound(ev)
                        import time as _t; _t.sleep(0.5)
                    else:
                        fnames = ", ".join(fname_list)
                        self._print(f"  ▶ {ev:12s}  ✗  файл не найден ({fnames})", self.C_RED)
                self._print_sep()
                self._print("  Если звуков no: /sounds install", self.C_DIM)
            elif sub == "install":
                self._print("  Поиск и копирование звуков...", self.C_CYAN)
                install_sounds(force=True)
                # Report what we found
                dest = DATA_DIR / "sounds"
                found_files = list(dest.glob("*.wav")) + list(dest.glob("*.mp3"))
                if found_files:
                    self._print(f"  ✓ Установлено {len(found_files)} файлов → {dest}", self.C_GREEN)
                    for f in found_files:
                        self._print(f"    ▸ {f.name}", self.C_WHITE)
                else:
                    self._print("  ✗ Файлы не найдены ни в одной из директорий:", self.C_RED)
                    for d in _get_sound_dirs()[1:]:
                        self._print(f"    - {d}", self.C_DIM)
                    self._print("  Убедись что папка gdfsound лежит рядом со скриптом", self.C_YELLOW)
                    self._print(f"  или в ~/Desktop/gdfsound/", self.C_YELLOW)
            else:
                self._print_sep()
                self._print("  ЗВУКОВАЯ SYSTEM GoidaPhone", self.C_CYAN)
                self._print_sep()
                # QMediaPlayer status
                try:
                    from PyQt6.QtMultimedia import QMediaPlayer
                    self._print("  Движок:  QMediaPlayer (PyQt6) — OK", self.C_GREEN)
                except ImportError:
                    self._print("  Движок:  QMediaPlayer недоступен!", self.C_RED)
                self._print(f"  DATA_DIR: {DATA_DIR / 'sounds'}", self.C_DIM)
                self._print_sep()
                self._print("  Директории поиска:", self.C_CYAN)
                dirs = _get_sound_dirs()
                any_found = False
                for d in dirs:
                    from pathlib import Path as _P
                    dp = _P(str(d))
                    if dp.exists():
                        files = list(dp.glob("*.wav")) + list(dp.glob("*.mp3"))
                        if files:
                            self._print(f"  ✓ {dp}", self.C_GREEN)
                            for f in sorted(files):
                                self._print(f"      {f.name}", self.C_WHITE)
                            any_found = True
                        else:
                            self._print(f"  ○ {dp}  (пусто)", self.C_DIM)
                    else:
                        self._print(f"  ✗ {dp}", self.C_DIM)
                self._print_sep()
                if not any_found:
                    self._print("  ⚠ Звуковые файлы не найдены!", self.C_RED)
                    self._print("  Положи папку gdfsound/ рядом со скриптом", self.C_YELLOW)
                    self._print("  или запусти /sounds install", self.C_YELLOW)
                else:
                    self._print("  /sounds test    — проверить воспроизведение", self.C_DIM)
                    self._print("  /sounds install — переустановить из источников", self.C_DIM)

        elif cmd == "/sysinfo":
            self._cmd_sysinfo()
        elif cmd == "/uptime":
            self._cmd_uptime()
        elif cmd == "/date":
            self._print(datetime.now().strftime("  %A, %d %B %Y  —  %H:%M:%S"), self.C_CYAN)
        elif cmd in ("/peers", "/who"):
            self._cmd_peers()
        elif cmd == "/whois":
            self._cmd_whois(arg1)
        elif cmd == "/ping":
            self._cmd_ping(arg1)
        elif cmd == "/traceroute":
            self._cmd_traceroute(arg1)
        elif cmd == "/nick":
            self._cmd_nick(arg1)
        elif cmd == "/me":
            self._print(f"  * {S().username} {arg1} {arg2} {arg3}".rstrip(), self.C_MAGENTA)
        elif cmd == "/crypto":
            self._cmd_crypto()
        elif cmd == "/stats":
            self._cmd_stats()
        elif cmd == "/netstat":
            self._cmd_netstat()
        elif cmd == "/about":
            self._cmd_about()
        elif cmd == "/goida":
            self._cmd_goida(arg1, arg2)
        elif cmd == "/cmatrix":
            self._cmd_cmatrix(arg1, arg2)
        elif cmd == "/mc":
            self._cmd_mc(arg1, arg2)
        elif cmd == "/history":
            self._cmd_history_cmd(arg1)
        elif cmd in ("/log",):
            self._cmd_log(arg1)
        elif cmd == "/theme":
            self._cmd_theme(arg1)
        elif cmd == "/font":
            self._cmd_font(arg1)
        elif cmd == "/resize":
            self._cmd_resize(arg1, arg2)
        elif cmd == "/monitor":
            if arg1 == "on":  self._start_monitor()
            elif arg1 == "off": self._stop_monitor()
            else: self._toggle_monitor()
        elif cmd in ("/say", "/broadcast"):
            self._cmd_broadcast(arg1 + " " + arg2 + " " + arg3)

        # ── Admin setup ────────────────────────────────────────────────
        elif cmd == "/admin":
            self._cmd_admin(arg1, arg2, arg3, raw)

        # ── Admin-only commands ────────────────────────────────────────
        elif cmd in ("/users", "/banlist"):
            self._require_auth(lambda c=cmd: self._admin_info_cmd(c))
        elif cmd in ("/ban", "/unban", "/kick", "/mute", "/unmute",
                     "/muteall", "/unmuteall"):
            self._require_auth(lambda c=cmd, a=arg1: self._admin_action_cmd(c, a))
        elif cmd in ("/netpw", "/netpw_off", "/network"):
            self._require_auth(lambda c=cmd, a=arg1: self._admin_net_cmd(c, a))
        elif cmd in ("/group",):
            self._require_auth(lambda a=arg1, b=arg2: self._admin_group_cmd(a, b))
        elif cmd in ("/msg",):
            self._require_auth(lambda a=arg1, rest=arg2+" "+arg3: self._cmd_msg(a, rest))
        elif cmd == "/wipe":
            self._require_auth(self._cmd_wipe)
        elif cmd == "/restart":
            self._require_auth(self._cmd_restart)

        else:
            self._print(f"  Unknown command: {cmd}", self.C_RED)
            self._print(f"  Введите /help или нажмите Tab для автодополнения.", self.C_DIM)

        self._print("")

    # ══════════════════════════════════════════════════════════════════
    #  COMMANDS IMPLEMENTATION
    # ══════════════════════════════════════════════════════════════════
    def _cmd_help(self, section: str = ""):
        if section == "admin":
            self._print_sep("─", 58)
            self._print("  ADMIN COMMANDS", self.C_YELLOW)
            self._print_sep("─", 58)
            rows = [
                ("/admin setup <n> <pw>", "Создать администратора"),
                ("/admin login <pw>",     "Авторизоваться"),
                ("/admin logout",         "Выйти из admin-режима"),
                ("/admin status",         "Статус admin"),
                ("/admin reset",          "Сброс (необратимо)"),
                ("/users",                "Список online + статусы"),
                ("/ban <ip>",             "Заблокировать"),
                ("/unban <ip>",           "Разблокировать"),
                ("/kick <ip>",            "Выгнать из сети"),
                ("/mute <ip>",            "Заглушить"),
                ("/unmute <ip>",          "Включить звук"),
                ("/muteall",              "Заглушить всех"),
                ("/unmuteall",            "Включить звук всем"),
                ("/banlist",              "Список банов и мутов"),
                ("/netpw <pw>",           "Пароль входа в сеть"),
                ("/netpw_off",            "Убрать пароль сети"),
                ("/network block|allow",  "Блокировка/открытие сети"),
                ("/group list|kick|info", "Управление группами"),
                ("/msg <ip> <text>",     "Личное сообщение"),
                ("/wipe",                 "Удалить все данные"),
                ("/restart",              "Перезапустить приложение"),
            ]
            for cmd, desc in rows:
                self._print(f"  {cmd:<28}  {desc}", self.C_WHITE)
            self._print_sep("─", 58)
            return

        _d = self.C_DIM
        _a = self.C_GREEN

        self._print_sep("─", 58)
        self._print("  Goida Terminal  ·  /help admin — команды администратора", _d)
        self._print_sep("─", 58)
        self._print("")

        sections = [
            ("GENERAL", _a, [
                ("/help [admin]",       "Справка"),
                ("/version",            "Версия GoidaPhone"),
                ("/sysinfo",            "Системная информация"),
                ("/uptime",             "Время работы"),
                ("/date",               "Дата и время"),
                ("/whoami",             "Информация о себе"),
                ("/about",              TR("menu_about")),
                ("/clear",              "Очистить экран"),
                ("/quit",               "Закрыть терминал"),
            ]),
            ("NETWORK", _a, [
                ("/peers",              "Онлайн пользователи"),
                ("/who",                "Кто в сети сейчас"),
                ("/whois <ip>",         "Подробно о пользователе"),
                ("/ping <ip>",          "Пинг до IP"),
                ("/traceroute <ip>",    "Трассировка маршрута"),
                ("/netstat",            "Статистика сети"),
                ("/stats",              "Статистика GoidaPhone"),
                ("/monitor [on|off]",   "Live-мониторинг пакетов"),
                ("/crypto",             "Статус шифрования"),
            ]),
            ("CHAT", _a, [
                ("/say <text>",        "Написать в публичный чат"),
                ("/broadcast <text>",  "Широковещательное сообщение"),
                ("/me <действие>",      "Эмоция"),
                ("/nick [имя]",         "Показать / изменить ник"),
                ("/history",            "История команд терминала"),
                ("/history clear",      "Очистить историю"),
            ]),
            ("PROFILE И ВИД", _a, [
                ("/theme [имя]",        "Текущая / сменить тему"),
                ("/themes",             "Список всех тем"),
                ("/font <размер>",      "Размер шрифта (8–18)"),
                ("/resize <W> <H>",     "Изменить размер терминала"),
                ("/colors",             "Палитра цветов терминала"),
            ]),
            ("ЛОГ", _a, [
                ("/log tail",           "Last 20 log lines"),
                ("/log clear",          "Clear log"),
                ("/log export",         "Export log to file"),
                ("/sounds test",        "Test sounds"),
                ("/cmatrix",            "Matrix rain animation"),
                ("/mc",                 "Minecraft-style world"),
            ]),
        ]

        for title, col, cmds in sections:
            self._print(f"  {title}", col)
            for c, d in cmds:
                self._print(f"    {c:<26}  {d}", self.C_WHITE)
            self._print("")

        self._print_sep("─", 58)
        self._print("  Tab — автодополнение    ↑↓ — история    /help admin", _d)
        self._print_sep("─", 58)

    def _cmd_version(self):
        self._print_sep()
        self._print(f"  GoidaPhone  v{APP_VERSION}  |  {COMPANY_NAME}", self.C_CYAN)
        self._print(f"  Protocol:   v{PROTOCOL_VERSION}  (совместимость с v{PROTOCOL_COMPAT}+)", self.C_WHITE)
        self._print(f"  Python:     {platform.python_version()}", self.C_WHITE)
        self._print(f"  Qt:         PyQt6", self.C_WHITE)
        self._print(f"  ОС:         {get_os_name()}", self.C_WHITE)
        self._print(f"  Платформа:  {platform.platform()}", self.C_WHITE)
        self._print_sep()

    def _cmd_sysinfo(self):
        self._print_sep()
        self._print("  SYSTEM INFO", self.C_CYAN)
        self._print_sep()
        # CPU / RAM via platform fallbacks
        self._print(f"  Хост:       {platform.node()}", self.C_WHITE)
        self._print(f"  ОС:         {platform.system()} {platform.release()}", self.C_WHITE)
        self._print(f"  Arch:       {platform.machine()}", self.C_WHITE)
        self._print(f"  Python:     {platform.python_version()} ({platform.python_implementation()})", self.C_WHITE)
        self._print(f"  IP:         {get_local_ip()}", self.C_WHITE)
        self._print(f"  DATA_DIR:   {DATA_DIR}", self.C_WHITE)
        # Storage
        try:
            usage = shutil.disk_usage(str(DATA_DIR))
            total = usage.total // (1024**3)
            free  = usage.free  // (1024**3)
            self._print(f"  Диск:       {free} GB свободно / {total} GB", self.C_WHITE)
        except Exception:
            pass
        # History size
        try:
            hist_files = list(HISTORY_DIR.glob("*.json"))
            hist_size  = sum(f.stat().st_size for f in hist_files)
            self._print(f"  История:    {len(hist_files)} диалогов, {hist_size//1024} КБ", self.C_WHITE)
        except Exception:
            pass
        self._print_sep()

    def _cmd_uptime(self):
        elapsed = time.time() - self._start_time
        h = int(elapsed // 3600); m = int((elapsed % 3600) // 60); s = int(elapsed % 60)
        self._print(f"  Терминал открыт: {h:02d}:{m:02d}:{s:02d}", self.C_CYAN)
        # App uptime via process
        try:
            import os as _os
            proc_start = _os.path.getmtime(f"/proc/{_os.getpid()}/stat")
            app_up = time.time() - proc_start
            ah = int(app_up // 3600); am = int((app_up % 3600) // 60)
            self._print(f"  Приложение:      {ah:02d}:{am:02d}", self.C_WHITE)
        except Exception:
            pass

    def _cmd_peers(self):
        peers = getattr(self._net, 'peers', {})
        if not peers:
            self._print("  No users online.", self.C_DIM); return
        self._print_sep()
        self._print(f"  {'НИК':<20} {'IP':<16} {'ВЕРСИЯ':<8} {'E2E':<6} {'ОС'}", self.C_CYAN)
        self._print_sep()
        banned = AdminManager.get_banned()
        muted  = AdminManager.get_muted()
        for ip, info in peers.items():
            flags = []
            if ip in banned: flags.append("BAN")
            if ip in muted:  flags.append("MUT")
            e2e  = "✓" if CRYPTO.has_session(ip) else "✗"
            flag = f" [{','.join(flags)}]" if flags else ""
            self._print(
                f"  {info.get('username','?'):<20} {ip:<16} "
                f"{info.get('version','?'):<8} {e2e:<6} "
                f"{info.get('os','?')}{flag}",
                self.C_RED if "BAN" in flags else self.C_WHITE)
        self._print_sep()
        self._print(f"  Всего online: {len(peers)}", self.C_GREEN)

    def _cmd_whois(self, ip: str):
        if not ip:
            self._print("  Использование: /whois <ip>", self.C_ORANGE); return
        peers = getattr(self._net, 'peers', {})
        info  = peers.get(ip)
        if not info:
            self._print(f"  Пользователь {ip} не online.", self.C_DIM); return
        self._print_sep()
        self._print(f"  WHOIS: {ip}", self.C_CYAN)
        self._print_sep()
        fields = [
            ("Ник",        info.get("username","?")),
            ("IP",         ip),
            ("Версия",     info.get("version","?")),
            ("Protocol",   info.get("protocol_version","?")),
            ("ОС",         info.get("os","?")),
            ("Тип связи",  info.get("conn_type","?")),
            ("Premium",    "Да" if info.get("premium") else "Нет"),
            ("E2E ключ",   "Установлен" if CRYPTO.has_session(ip) else "Нет"),
            ("Отпечаток",  CRYPTO.peer_fingerprint(ip)),
            ("Последний",  datetime.fromtimestamp(info.get("last_seen",0)).strftime("%H:%M:%S")),
            ("Бан",        "ДА" if ip in AdminManager.get_banned() else "Нет"),
            ("Мут",        "ДА" if ip in AdminManager.get_muted() else "Нет"),
        ]
        for k, v in fields:
            col = self.C_RED if v in ("ДА",) else self.C_WHITE
            self._print(f"  {k:<14} {v}", col)
        bio = info.get("bio","")
        if bio:
            self._print(f"  Bio:           {bio[:60]}", self.C_DIM)
        self._print_sep()

    def _cmd_ping(self, ip: str):
        if not ip:
            self._print("  Использование: /ping <ip>", self.C_ORANGE); return
        self._print(f"  Пингую {ip} (4 пакета)...", self.C_CYAN)
        def do_ping():
            try:
                flag = "-n" if platform.system() == "Windows" else "-c"
                res = subprocess.run(
                    ["ping", flag, "4", ip],
                    capture_output=True, text=True, timeout=12)
                lines = (res.stdout or res.stderr).splitlines()
                for line in lines[-8:]:
                    QTimer.singleShot(0, lambda l=line:
                        self._print(f"  {l}", self.C_CYAN))
            except Exception as ex:
                QTimer.singleShot(0, lambda: self._print(f"  Ошибка: {ex}", self.C_RED))
        threading.Thread(target=do_ping, daemon=True).start()

    def _cmd_traceroute(self, ip: str):
        if not ip:
            self._print("  Использование: /traceroute <ip>", self.C_ORANGE); return
        self._print(f"  Traceroute → {ip}...", self.C_CYAN)
        def do_tr():
            try:
                cmd = (["tracert", ip] if platform.system() == "Windows"
                       else ["traceroute", "-m", "15", ip])
                res = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                lines = (res.stdout or res.stderr).splitlines()
                for line in lines[:20]:
                    QTimer.singleShot(0, lambda l=line:
                        self._print(f"  {l}", self.C_CYAN))
            except Exception as ex:
                QTimer.singleShot(0, lambda: self._print(f"  Ошибка: {ex}", self.C_RED))
        threading.Thread(target=do_tr, daemon=True).start()

    def _cmd_nick(self, new_nick: str):
        if new_nick:
            old = S().username
            S().username = new_nick
            self._print(f"  Ник: {old} → {new_nick}", self.C_GREEN)
            try:
                self._prompt_lbl.setText(f"{new_nick}@{platform.node().split('.')[0]} »")
            except Exception:
                pass
        else:
            self._print(f"  Ник: {S().username}", self.C_CYAN)
            self._print(f"  IP:  {get_local_ip()}", self.C_WHITE)

    def _cmd_crypto(self):
        self._print_sep()
        self._print("  CRYPTO STATUS", self.C_CYAN)
        self._print_sep()
        self._print(f"  Движок:     {CRYPTO.status()}", self.C_WHITE)
        self._print(f"  Мой отпечаток:", self.C_WHITE)
        self._print(f"    {CRYPTO.fingerprint()}", self.C_GREEN)
        peers = getattr(self._net, 'peers', {})
        e2e_count = sum(1 for ip in peers if CRYPTO.has_session(ip))
        self._print(f"  E2E sessions: {e2e_count} / {len(peers)}", self.C_WHITE)
        if peers:
            self._print("  Sessions by peer:", self.C_DIM)
            for ip, info in peers.items():
                has = CRYPTO.has_session(ip)
                col = self.C_GREEN if has else self.C_RED
                sym = "✓" if has else "✗"
                self._print(f"    {sym} {info.get('username','?'):<20} {ip}", col)
        self._print_sep()

    def _cmd_stats(self):
        peers = getattr(self._net, 'peers', {})
        self._print_sep()
        self._print("  GOIDAPHONE STATS", self.C_CYAN)
        self._print_sep()
        self._print(f"  Online:      {len(peers)} users", self.C_WHITE)
        self._print(f"  Banned:      {len(AdminManager.get_banned())} IP", self.C_WHITE)
        self._print(f"  Muted:       {len(AdminManager.get_muted())} IP", self.C_WHITE)
        self._print(f"  E2E-сессий:  {sum(1 for ip in peers if CRYPTO.has_session(ip))}", self.C_WHITE)
        # Groups
        try:
            if GROUPS_FILE.exists():
                groups = json.loads(GROUPS_FILE.read_text(encoding="utf-8"))
                self._print(f"  Групп:       {len(groups)}", self.C_WHITE)
        except Exception:
            pass
        # History size
        try:
            hist = list(HISTORY_DIR.glob("*.json"))
            hist_kb = sum(f.stat().st_size for f in hist) // 1024
            self._print(f"  История:     {len(hist)} диалогов / {hist_kb} КБ", self.C_WHITE)
        except Exception:
            pass
        self._print(f"  Encryption:  {'AES-256-GCM' if CRYPTO_AVAILABLE else 'XOR'}", self.C_WHITE)
        self._print(f"  Admin:       {'активен' if AdminManager.is_admin() else 'не настроен'}", self.C_WHITE)
        self._print(f"  Пароль сети: {'● да' if AdminManager.network_password_enabled() else '○ no'}", self.C_WHITE)
        self._print_sep()

    def _cmd_netstat(self):
        self._print_sep()
        self._print("  NETWORK INTERFACES", self.C_CYAN)
        self._print_sep()
        for iface in QNetworkInterface.allInterfaces():
            flags = iface.flags()
            if not (flags & QNetworkInterface.InterfaceFlag.IsUp):
                continue
            for entry in iface.addressEntries():
                addr = entry.ip().toString()
                if addr in ("0.0.0.0", "::"):
                    continue
                self._print(f"  {iface.name():<12} {addr}", self.C_WHITE)
        self._print(f"  UDP порт:   {S().get('udp_port', 45678, t=int)}", self.C_DIM)
        self._print(f"  TCP порт:   {S().get('tcp_port', 45679, t=int)}", self.C_DIM)
        blocked = S().get("network_blocked", False, t=bool)
        self._print(f"  Сеть:       {'ЗАБЛОКИРОВАНА' if blocked else 'открыта'}",
                    self.C_RED if blocked else self.C_GREEN)
        self._print_sep()

    def _cmd_about(self):
        self._print_sep()
        self._print("  О ПРОГРАММЕ", self.C_CYAN)
        self._print_sep()
        self._print(f"  {APP_NAME}  v{APP_VERSION}", self.C_WHITE)
        self._print(f"  {COMPANY_NAME}", self.C_DIM)
        self._print("", self.C_WHITE)
        self._print("  P2P LAN/VPN мессенджер с шифрованием, группами,", self.C_WHITE)
        self._print("  голосовыми звонками, медиаплеером Mewa 1-2-3.", self.C_WHITE)
        self._print("", self.C_WHITE)
        self._print("  Encryption: AES-256-GCM + X25519 ECDH + Ed25519", self.C_WHITE)
        self._print("  Protocol:   UDP + TCP (п2п, без сервера)", self.C_WHITE)
        self._print_sep()

    def _cmd_goida(self, flag1: str = "", flag2: str = ""):
        """
        /goida [--time-N] [--color-COLOR]
        --time-1 очень медленно  --time-10 максимально быстро
        --color-cyan/green/magenta/yellow/red/white
        """
        # Парсим флаги
        flags = [f for f in [flag1, flag2] if f.startswith("--")]
        speed = 5
        color = self.C_GREEN
        color_map = {
            "cyan":    self.C_CYAN,    "green":   self.C_GREEN,
            "magenta": self.C_MAGENTA, "yellow":  self.C_YELLOW,
            "red":     self.C_RED,     "white":   self.C_WHITE,
        }
        for f in flags:
            if f.startswith("--time-"):
                try: speed = max(1, min(10, int(f[7:])))
                except ValueError: pass
            elif f.startswith("--color-"):
                color = color_map.get(f[8:].lower(), self.C_GREEN)

        # delay: time-1=800ms time-10=40ms
        delay_ms = int(800 - (speed - 1) * (760 / 9))

        FRAMES = [
            [
                " ██████╗  ██████╗ ██╗██████╗  █████╗ ",
                "██╔════╝ ██╔═══██╗██║██╔══██╗██╔══██╗",
                "██║  ███╗██║   ██║██║██║  ██║███████║",
                "██║   ██║██║   ██║██║██║  ██║██╔══██║",
                "╚██████╔╝╚██████╔╝██║██████╔╝██║  ██║",
                " ╚═════╝  ╚═════╝ ╚═╝╚═════╝ ╚═╝  ╚═╝",
            ],
            [
                "░██████╗░░█████╗░██╗██████╗░░█████╗░",
                "██╔════╝██╔══██╗██║██╔══██╗██╔══██╗",
                "██║░░██╗██║░░██║██║██║░░██║███████║",
                "██║░░██║██║░░██║██║██║░░██║██╔══██║",
                "╚██████╔╝╚█████╔╝██║██████╔╝██║░░██║",
                "░╚═════╝░╚═════╝╚═╝╚═════╝░╚═╝░░╚═╝",
            ],
            [
                "▓██████╗▓██████╗██╗██████╗▓█████╗",
                "██║════╝██║═══█║██║██║══█║██║══█║",
                "██║▓▓██╗██║▓▓▓║██║██║▓▓█║███████║",
                "██║▓▓██║██║▓▓▓║██║██║▓▓█║██║══█║",
                "╚██████╝╚██████╝██║██████╝██║▓▓█║",
                "▓╚════╝▓╚═════╝╚═╝╚═════╝▓╚═╝▓▓╝",
            ],
        ]
        TAGLINES = [
            "  P2P · Encrypted · No Servers",
            "  Winora Company © 2026",
            f"  GoidaPhone v{APP_VERSION}",
            "  goida ❯ _",
        ]

        max_rep = 3 if speed <= 3 else (6 if speed <= 7 else 30)
        self._goida_frame   = 0
        self._goida_repeats = 0
        self._goida_tag     = 0
        self._goida_color   = color
        self._goida_frames  = FRAMES
        self._goida_tags    = TAGLINES
        self._goida_maxrep  = max_rep
        # Разделитель перед анимацией
        self._print_sep("═", 42)
        self._goida_running = True

        # Используем QTimer в ГЛАВНОМ потоке — без threading
        if hasattr(self, '_goida_timer') and self._goida_timer:
            self._goida_timer.stop()
        self._goida_timer = QTimer(self)
        self._goida_timer.setInterval(delay_ms)
        self._goida_timer.timeout.connect(self._goida_tick)
        self._goida_timer.start()
        self._print(f"  speed={speed}  delay={delay_ms}ms  Enter — стоп", self.C_DIM)

    def _goida_tick(self):
        """Тик анимации goida — стираем предыдущий кадр и рисуем новый."""
        if not getattr(self, '_goida_running', False):
            if hasattr(self, '_goida_timer') and self._goida_timer:
                self._goida_timer.stop()
            return

        frames  = self._goida_frames
        tags    = self._goida_tags
        frame_n = self._goida_frame
        tag_n   = self._goida_tag
        color   = self._goida_color
        FRAME_H = len(frames[0]) + 2  # строк на кадр

        frame = frames[frame_n % len(frames)]
        tag   = tags[tag_n % len(tags)]

        # Стираем предыдущий кадр — удаляем последние FRAME_H блоков из QTextEdit
        if self._goida_frame > 0:
            doc    = self._output.document()
            cursor = self._output.textCursor()
            cursor.movePosition(cursor.MoveOperation.End)
            for _ in range(FRAME_H):
                cursor.movePosition(cursor.MoveOperation.StartOfBlock,
                                    cursor.MoveMode.KeepAnchor)
                cursor.movePosition(cursor.MoveOperation.PreviousBlock,
                                    cursor.MoveMode.KeepAnchor)
            cursor.removeSelectedText()
            self._output.setTextCursor(cursor)

        # Рисуем новый кадр
        for line in frame:
            self._print(f"  {line}", color)
        self._print(f"  {tag}", self.C_DIM)
        self._print("")

        self._goida_frame += 1
        if self._goida_frame % len(frames) == 0:
            self._goida_tag     = (self._goida_tag + 1) % len(tags)
            self._goida_repeats += 1

        if self._goida_repeats >= self._goida_maxrep:
            self._goida_running = False
            self._goida_timer.stop()
            self._print("  ✓ goida", self.C_GREEN)
            self._print_sep("═", 42)


    def _cmd_cmatrix(self, flag1: str = "", flag2: str = ""):
        """
        /cmatrix [--color-COLOR] [--speed-N]
        Matrix digital rain animation. Enter to stop.
        """
        import random as _r, string as _s

        flags = [f for f in [flag1, flag2] if f.startswith("--")]
        color = self.C_GREEN
        speed = 5
        color_map = {
            "green": self.C_GREEN, "cyan": self.C_CYAN,
            "white": self.C_WHITE, "yellow": self.C_YELLOW,
            "magenta": self.C_MAGENTA,
        }
        for f in flags:
            if f.startswith("--color-"):
                color = color_map.get(f[8:].lower(), self.C_GREEN)
            elif f.startswith("--speed-"):
                try: speed = max(1, min(10, int(f[8:])))
                except ValueError: pass

        delay_ms = int(300 - (speed - 1) * (260 / 9))
        COLS = 60
        ROWS = 14
        CHARS = "ｦｧｨｩｪｫｬｭｮｯｰｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ01"
        CHARS += "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&"

        # Инициализация колонок
        self._matrix_cols = []
        for c in range(COLS):
            head = _r.randint(0, ROWS - 1)
            speed_c = _r.randint(1, 3)
            self._matrix_cols.append({'head': head, 'trail': [], 'speed': speed_c, 'tick': 0})

        self._matrix_grid = [[' '] * COLS for _ in range(ROWS)]
        self._matrix_color = color
        self._matrix_frame = 0
        self._matrix_running = True

        self._print(f"  cmatrix  speed={speed}  Enter — stop", self.C_DIM)
        self._print("")

        if hasattr(self, '_matrix_timer') and self._matrix_timer:
            self._matrix_timer.stop()
        self._matrix_timer = QTimer(self)
        self._matrix_timer.setInterval(delay_ms)
        self._matrix_timer.timeout.connect(self._matrix_tick)
        self._matrix_timer.start()

    def _matrix_tick(self):
        import random as _r
        if not getattr(self, '_matrix_running', False):
            if hasattr(self, '_matrix_timer'): self._matrix_timer.stop()
            return

        COLS = 60
        ROWS = 14
        CHARS = "ｦｧｨｩｪｫｬｭｮｯｰｱｲｳｴｵｶｷｸｹｺｻｼｽｾｿﾀﾁﾂﾃﾄﾅﾆﾇﾈﾉﾊﾋﾌﾍﾎﾏﾐﾑﾒﾓﾔﾕﾖﾗﾘﾙﾚﾛﾜﾝ0123456789"
        color = self._matrix_color
        dim   = self.C_DIM

        grid = self._matrix_grid
        cols = self._matrix_cols

        # Обновляем каждую колонку
        for c, col in enumerate(cols):
            col['tick'] += 1
            if col['tick'] < col['speed']:
                continue
            col['tick'] = 0
            # Сдвигаем символы вниз
            for r in range(ROWS - 1, 0, -1):
                grid[r][c] = grid[r-1][c]
            # Новый символ сверху
            if _r.random() < 0.7:
                grid[0][c] = _r.choice(CHARS)
            else:
                grid[0][c] = ' '
            # Голова движется вниз
            col['head'] = (col['head'] + 1) % ROWS

        # Стираем предыдущий кадр
        if self._matrix_frame > 0:
            doc    = self._output.document()
            cursor = self._output.textCursor()
            cursor.movePosition(cursor.MoveOperation.End)
            for _ in range(ROWS + 1):
                cursor.movePosition(cursor.MoveOperation.StartOfBlock,
                                    cursor.MoveMode.KeepAnchor)
                cursor.movePosition(cursor.MoveOperation.PreviousBlock,
                                    cursor.MoveMode.KeepAnchor)
            cursor.removeSelectedText()
            self._output.setTextCursor(cursor)

        # Рисуем кадр
        for r in range(ROWS):
            row_str = ""
            for c in range(COLS):
                ch = grid[r][c]
                row_str += ch if ch != ' ' else '　'
            self._print(f"  {row_str}", color)
        self._print("")
        self._matrix_frame += 1

    def _cmd_mc(self, flag1: str = "", flag2: str = ""):
        """
        /mc [--color-COLOR]
        Minecraft-style ASCII world. Enter to stop.
        """
        import random as _r

        flags = [f for f in [flag1, flag2] if f.startswith("--")]
        color = self.C_GREEN
        color_map = {"green": self.C_GREEN, "cyan": self.C_CYAN, "yellow": self.C_YELLOW}
        for f in flags:
            if f.startswith("--color-"): color = color_map.get(f[8:].lower(), color)

        WORLD_W = 60
        WORLD_H = 12
        SEA_LEVEL = 8

        # Генерируем ландшафт (Перлин-шум эмуляция)
        heights = []
        h = SEA_LEVEL
        for x in range(WORLD_W):
            h += _r.randint(-1, 1)
            h = max(SEA_LEVEL - 4, min(SEA_LEVEL + 2, h))
            heights.append(h)

        BLOCKS = {
            'sky': '  ', 'grass': '██', 'dirt': '▓▓',
            'stone': '░░', 'water': '≈≈', 'tree': '🌲',
            'cloud': '☁ ', 'sun': '☀ ',
        }

        self._mc_world_w = WORLD_W
        self._mc_world_h = WORLD_H
        self._mc_heights = heights
        self._mc_scroll  = 0
        self._mc_frame   = 0
        self._mc_running = True
        self._mc_color   = color

        self._print("  /mc — Goida World  Enter — stop", self.C_DIM)
        self._print("")

        if hasattr(self, '_mc_timer') and self._mc_timer:
            self._mc_timer.stop()
        self._mc_timer = QTimer(self)
        self._mc_timer.setInterval(120)
        self._mc_timer.timeout.connect(self._mc_tick)
        self._mc_timer.start()

    def _mc_tick(self):
        import random as _r
        if not getattr(self, '_mc_running', False):
            if hasattr(self, '_mc_timer'): self._mc_timer.stop()
            return

        W = self._mc_world_w
        H = self._mc_world_h
        heights = self._mc_heights
        scroll  = self._mc_scroll
        color   = self._mc_color

        # Стираем предыдущий кадр
        if self._mc_frame > 0:
            cursor = self._output.textCursor()
            cursor.movePosition(cursor.MoveOperation.End)
            for _ in range(H + 2):
                cursor.movePosition(cursor.MoveOperation.StartOfBlock,
                                    cursor.MoveMode.KeepAnchor)
                cursor.movePosition(cursor.MoveOperation.PreviousBlock,
                                    cursor.MoveMode.KeepAnchor)
            cursor.removeSelectedText()
            self._output.setTextCursor(cursor)

        VIEW_W = 30  # видимых колонок
        VIEW_X = scroll % W

        rows = []
        for y in range(H):
            row = "  "
            for vx in range(VIEW_W):
                x = (VIEW_X + vx) % W
                gh = heights[x]  # высота земли
                world_y = y  # 0 = небо

                if world_y == 0 and vx == 2:
                    row += "☀ "
                elif world_y == 1 and (vx == 8 or vx == 14):
                    row += "☁ "
                elif world_y < gh:
                    row += "  "  # небо
                elif world_y == gh:
                    row += "██"  # трава
                elif world_y < gh + 2:
                    row += "▓▓"  # земля
                elif world_y < gh + 4:
                    row += "░░"  # камень
                else:
                    row += "▒▒"  # глубокий камень
            rows.append(row)

        for row in rows:
            self._print(row, color)
        self._print(f"  x={scroll:4d}  ← автопрокрутка →", self.C_DIM)
        self._print("")

        self._mc_scroll += 1
        self._mc_frame  += 1


    def _cmd_history_cmd(self, sub: str):
        if sub == "clear":
            self._cmd_history.clear()
            self._print("  История команд очищена.", self.C_GREEN)
        else:
            if not self._cmd_history:
                self._print("  История пуста.", self.C_DIM); return
            self._print(f"  История команд ({len(self._cmd_history)}):", self.C_CYAN)
            for i, c in enumerate(self._cmd_history[-30:], 1):
                self._print(f"  {i:>3}  {c}", self.C_WHITE)

    def _cmd_log(self, sub: str):
        if sub == "clear":
            self._log_entries.clear()
            self._log_output.clear()
            self._print("  Лог очищен.", self.C_GREEN)
        elif sub == "export":
            self._export_log()
        else:
            if not self._log_entries:
                self._print("  Лог пуст.", self.C_DIM); return
            self._print("  Последние 20 записей:", self.C_CYAN)
            for line in self._log_entries[-20:]:
                self._print(f"  {line}", self.C_DIM)

    def _export_log(self):
        try:
            path = DATA_DIR / f"terminal_log_{int(time.time())}.txt"
            path.write_text("\n".join(self._log_entries), encoding="utf-8")
            self._print(f"  Лог сохранён: {path}", self.C_GREEN)
        except Exception as e:
            self._print(f"  Ошибка: {e}", self.C_RED)

    def _cmd_theme(self, name: str):
        themes = ["dark", "light", "blue", "green", "pink", "black", "solarized"]
        if not name:
            self._print(f"  Текущая тема: {S().theme}", self.C_CYAN)
            self._print(f"  Доступно: {', '.join(themes)}", self.C_DIM)
        elif name in themes:
            S().theme = name
            self._print(f"  Тема изменена → {name}. Перезапустите для полного эффекта.", self.C_GREEN)
        else:
            self._print(f"  Неизвестная тема. Доступно: {', '.join(themes)}", self.C_ORANGE)

    def _cmd_font(self, size_str: str):
        try:
            size = int(size_str)
            if not 8 <= size <= 18:
                raise ValueError
            self._output.setStyleSheet(self._output.styleSheet().replace(
                "font-size: 11px", f"font-size: {size}px"))
            self._print(f"  Шрифт: {size}px", self.C_GREEN)
        except (ValueError, TypeError):
            self._print("  Использование: /font <8–18>", self.C_ORANGE)

    def _cmd_resize(self, w_str: str, h_str: str):
        try:
            w = max(600, min(1600, int(w_str)))
            h = max(400, min(1000, int(h_str)))
            self.resize(w, h)
            self._print(f"  Размер: {w}×{h}", self.C_GREEN)
        except (ValueError, TypeError):
            self._print("  Использование: /resize <W> <H>", self.C_ORANGE)

    def _cmd_broadcast(self, text: str):
        text = text.strip()
        if not text:
            self._print("  Использование: /broadcast <text>", self.C_ORANGE); return
        if hasattr(self._net, 'send_chat'):
            self._net.send_chat(f"[ADMIN] {text}")
            self._print(f"  Отправлено в публичный чат: {text}", self.C_GREEN)
        else:
            self._print("  Сеть недоступна.", self.C_RED)

    def _cmd_msg(self, ip: str, text: str):
        text = text.strip()
        if not ip or not text:
            self._print("  Использование: /msg <ip> <text>", self.C_ORANGE); return
        peers = getattr(self._net, 'peers', {})
        if ip not in peers:
            self._print(f"  Пользователь {ip} не online.", self.C_RED); return
        if hasattr(self._net, 'send_private'):
            self._net.send_private(f"[Терминал] {text}", ip)
            name = peers[ip].get("username","?")
            self._print(f"  → {name} ({ip}): {text}", self.C_GREEN)

    # ── Admin commands ─────────────────────────────────────────────────
    def _cmd_admin(self, sub: str, arg2: str, arg3: str, raw: str):
        sub = sub.lower()
        if sub == "setup":
            parts = raw.split(maxsplit=3)
            if len(parts) < 4:
                self._print("  Использование: /admin setup <name> <пароль>", self.C_ORANGE)
                return
            aname, apw = parts[2], parts[3]
            if AdminManager.is_admin():
                self._print("  Администратор уже создан. /admin reset — сброс.", self.C_RED)
                return
            AdminManager.setup_admin(aname, apw)
            self._admin_authed = True
            self._admin_badge.setVisible(True)
            self._print(f"  ◆ Администратор создан: {aname}", self.C_YELLOW)
            self._print("    Вы — владелец этой сети.", self.C_GREEN)

        elif sub == "login":
            if not AdminManager.is_admin():
                self._print("  Администратор не настроен.", self.C_RED); return
            if AdminManager.verify_admin(arg2):
                self._admin_authed = True
                self._admin_badge.setVisible(True)
                self._print(f"  ◆ Вход выполнен: {AdminManager.get_admin_name()}", self.C_YELLOW)
            else:
                self._print("  ✗ Неверный пароль.", self.C_RED)

        elif sub == "logout":
            self._admin_authed = False
            self._admin_badge.setVisible(False)
            self._print("  Вышли из admin-режима.", self.C_DIM)

        elif sub == "reset":
            self._require_auth(self._admin_reset)

        elif sub == "status":
            self._print(f"  Admin настроен:    {'да' if AdminManager.is_admin() else 'no'}", self.C_WHITE)
            self._print(f"  Аутентифицирован:  {'да' if self._admin_authed else 'no'}", self.C_WHITE)
            if AdminManager.is_admin():
                self._print(f"  Name:               {AdminManager.get_admin_name()}", self.C_WHITE)
            self._print(f"  Пароль сети:       {'● активен' if AdminManager.network_password_enabled() else '○ no'}", self.C_WHITE)
            self._print(f"  Сеть заблокирована:{'да' if S().get('network_blocked',False,t=bool) else 'no'}", self.C_WHITE)
        else:
            self._print("  Субкоманды: setup, login, logout, reset, status", self.C_ORANGE)

    def _admin_reset(self):
        reply = QMessageBox.warning(self, "Сброс администратора",
            "Сбросить настройки администратора?\nЭто необратимо.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            for k in ["_set","_name","_hash"]:
                S().remove("admin_profile" + k)
            self._admin_authed = False
            self._admin_badge.setVisible(False)
            self._print("  Администратор сброшен.", self.C_ORANGE)

    def _admin_info_cmd(self, cmd: str):
        if cmd == "/users":
            self._cmd_peers()
        elif cmd == "/banlist":
            banned = AdminManager.get_banned()
            muted  = AdminManager.get_muted()
            self._print(f"  Забанено ({len(banned)}):", self.C_RED)
            for ip in banned:
                peers = getattr(self._net, 'peers', {})
                name = peers.get(ip, {}).get("username", "?")
                self._print(f"    ⊘  {ip:<16} {name}", self.C_RED)
            self._print(f"  Заглушено ({len(muted)}):", self.C_ORANGE)
            for ip in muted:
                peers = getattr(self._net, 'peers', {})
                name = peers.get(ip, {}).get("username", "?")
                self._print(f"    ⊗  {ip:<16} {name}", self.C_ORANGE)

    def _admin_action_cmd(self, cmd: str, arg: str):
        peers = getattr(self._net, 'peers', {})

        if cmd == "/muteall":
            for ip in list(peers.keys()):
                AdminManager.mute(ip)
            self._print(f"  ⊗ Все {len(peers)} пользователей заглушены.", self.C_ORANGE)
            return
        if cmd == "/unmuteall":
            for ip in AdminManager.get_muted():
                AdminManager.unmute(ip)
            self._print("  ✓ Мут снят со всех.", self.C_GREEN)
            return

        if not arg:
            self._print(f"  Использование: {cmd} <ip>", self.C_ORANGE); return

        name = peers.get(arg, {}).get("username", arg)
        if cmd == "/ban":
            AdminManager.ban(arg)
            if hasattr(self._net, 'kick_peer'): self._net.kick_peer(arg)
            self._print(f"  ⊘ Забанен: {name} ({arg})", self.C_RED)
        elif cmd == "/unban":
            AdminManager.unban(arg)
            self._print(f"  ✓ Разбанен: {name} ({arg})", self.C_GREEN)
        elif cmd == "/kick":
            if hasattr(self._net, 'kick_peer'): self._net.kick_peer(arg)
            self._print(f"  ↯ Кикнут: {name} ({arg})", self.C_ORANGE)
        elif cmd == "/mute":
            AdminManager.mute(arg)
            self._print(f"  ⊗ Muted: {name} ({arg})", self.C_ORANGE)
        elif cmd == "/unmute":
            AdminManager.unmute(arg)
            self._print(f"  ✓ Звук восстановлен: {name} ({arg})", self.C_GREEN)

    def _admin_net_cmd(self, cmd: str, arg: str):
        if cmd == "/netpw":
            if not arg:
                self._print("  Использование: /netpw <пароль>", self.C_ORANGE); return
            AdminManager.set_network_password(arg)
            self._print(f"  ● Пароль сети установлен.", self.C_YELLOW)
            self._print("    Новые пользователи должны знать пароль для входа.", self.C_DIM)
        elif cmd == "/netpw_off":
            AdminManager.set_network_password("")
            self._print("  ○ Пароль сети disabled.", self.C_GREEN)
        elif cmd == "/network":
            sub = arg.lower()
            if sub == "block":
                S().set("network_blocked", True)
                if hasattr(self._net, 'set_blocked'): self._net.set_blocked(True)
                self._print("  ⊘ Сеть заблокирована. Входящие соединения отклоняются.", self.C_RED)
            elif sub == "allow":
                S().set("network_blocked", False)
                if hasattr(self._net, 'set_blocked'): self._net.set_blocked(False)
                self._print("  ✓ Сеть открыта.", self.C_GREEN)
            else:
                blocked = S().get("network_blocked", False, t=bool)
                self._print(f"  Статус сети: {'ЗАБЛОКИРОВАНА' if blocked else 'открыта'}",
                            self.C_RED if blocked else self.C_GREEN)
                self._print("  Субкоманды: /network block  |  /network allow", self.C_DIM)

    def _admin_group_cmd(self, sub: str, arg: str):
        sub = sub.lower()
        if sub == "list":
            try:
                if GROUPS_FILE.exists():
                    groups = json.loads(GROUPS_FILE.read_text(encoding="utf-8"))
                    self._print(f"  Групп: {len(groups)}", self.C_CYAN)
                    for gid, g in groups.items():
                        members = g.get("members", [])
                        self._print(f"  [{gid[:8]}]  {g.get('name','?'):<24} {len(members)} участников", self.C_WHITE)
                else:
                    self._print("  Групп no.", self.C_DIM)
            except Exception as e:
                self._print(f"  Ошибка: {e}", self.C_RED)
        elif sub == "info":
            self._print("  Субкоманды: /group list", self.C_DIM)
        else:
            self._print("  Субкоманды: list", self.C_ORANGE)

    def _handle_plugin_cmd(self, text: str):
        parts = text.strip().split(None, 1)
        sub = parts[0].lower() if parts else ""
        arg = parts[1] if len(parts) > 1 else ""
        if sub == "load" and arg:
            self._cmd_load_plugin(arg)
        elif sub == "list" or not sub:
            self._cmd_list_plugins()
        elif sub == "dir":
            pd = DATA_DIR / "zlink_plugins"
            pd.mkdir(parents=True, exist_ok=True)
            _open_system(str(pd))
        else:
            self._print("  /plugin list|load <path>|dir", self.C_DIM)

    def _cmd_wipe(self):
        reply = QMessageBox.warning(self, "УДАЛЕНИЕ ДАННЫХ",
            "⚠ УДАЛИТЬ ВСЕ ДАННЫЕ GoidaPhone?\n"
            "История, настройки, аватары, файлы — всё будет удалено.\n\n"
            "Это действие НЕОБРАТИМО.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            self._print("  Отменено.", self.C_DIM); return
        try:
            from functools import reduce
            S()._s.clear(); S()._s.sync()
            shutil.rmtree(DATA_DIR, ignore_errors=True)
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            HISTORY._cache.clear()
            self._print("  ✓ Данные удалены. Перезапустите приложение.", self.C_RED)
        except Exception as e:
            self._print(f"  Ошибка: {e}", self.C_RED)

    def _cmd_load_plugin(self, path: str):
        """Load a Python plugin script into ZLink terminal."""
        try:
            import importlib.util, os
            if not os.path.exists(path):
                self._print(f"  ✗ Файл не найден: {path}", self.C_RED)
                return
            spec = importlib.util.spec_from_file_location("zlink_plugin", path)
            mod  = importlib.util.module_from_spec(spec)
            # Expose terminal API to plugin
            mod.terminal   = self
            mod.print_line = self._print
            mod.net        = getattr(self, '_net', None)
            spec.loader.exec_module(mod)
            self._print(f"  ✓ Плагин загружен: {os.path.basename(path)}", self.C_GREEN)
            if hasattr(mod, 'on_load'):
                mod.on_load()
        except Exception as e:
            self._print(f"  ✗ Ошибка плагина: {e}", self.C_RED)

    def _cmd_list_plugins(self):
        plugins_dir = DATA_DIR / "zlink_plugins"
        if not plugins_dir.exists():
            plugins_dir.mkdir(parents=True, exist_ok=True)
        files = list(plugins_dir.glob("*.py"))
        if not files:
            self._print("  Плагинов no. Положите .py файлы в:", self.C_DIM)
            self._print(f"  {plugins_dir}", self.C_CYAN)
        else:
            self._print(f"  Плагины ({len(files)}):", self.C_CYAN)
            for f in files:
                self._print(f"  📜 {f.name}", self.C_WHITE)

    def _cmd_restart(self):
        reply = QMessageBox.question(self, "Перезапуск",
            "Перезапустить GoidaPhone?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._print("  Перезапуск...", self.C_YELLOW)
            QTimer.singleShot(500, lambda: (
                os.execv(sys.executable, [sys.executable] + sys.argv)))

    # ── Require auth helper ────────────────────────────────────────────
    def _require_auth(self, action):
        if not AdminManager.is_admin():
            self._print("  ✗ Создайте администратора: /admin setup <name> <пароль>", self.C_RED)
            return
        if not self._admin_authed:
            self._print("  ✗ Требуется авторизация: /admin login <пароль>", self.C_RED)
            return
        action()

    # ══════════════════════════════════════════════════════════════════
    #  NETWORK MONITOR
    # ══════════════════════════════════════════════════════════════════
    def _start_monitor(self):
        if self._monitor_active: return
        self._monitor_active = True
        self._monitor_badge.setVisible(True)
        self._monitor_timer = QTimer(self)
        self._monitor_timer.timeout.connect(self._monitor_tick)
        self._monitor_timer.start(2000)
        self._mon_print("● Монитор запущен. Обновление каждые 2 сек.", "#39FF14")
        self._tabs.setCurrentIndex(1)

    def _stop_monitor(self):
        if not self._monitor_active: return
        self._monitor_active = False
        self._monitor_badge.setVisible(False)
        if self._monitor_timer:
            self._monitor_timer.stop()
            self._monitor_timer = None
        self._mon_print("■ Монитор остановлен.", "#FF8C00")

    def _toggle_monitor(self):
        if self._monitor_active: self._stop_monitor()
        else: self._start_monitor()

    def _monitor_tick(self):
        peers = getattr(self._net, 'peers', {})
        ts    = datetime.now().strftime("%H:%M:%S")
        self._mon_print(f"── {ts} ── peers={len(peers)} ─────────────────────────────", "#1A3A5A")
        for ip, info in peers.items():
            e2e  = "E2E✓" if CRYPTO.has_session(ip) else "    "
            last = time.time() - info.get("last_seen", time.time())
            self._mon_print(
                f"  {info.get('username','?'):<18} {ip:<16} {e2e}  last={last:.0f}s",
                "#2A6A2A" if last < 10 else "#6A4A00")

    # ══════════════════════════════════════════════════════════════════
    #  USERS TAB
    # ══════════════════════════════════════════════════════════════════
    def _refresh_users_tab(self):
        peers  = getattr(self._net, 'peers', {})
        banned = AdminManager.get_banned()
        muted  = AdminManager.get_muted()
        self._users_table.setRowCount(0)
        for ip, info in peers.items():
            row = self._users_table.rowCount()
            self._users_table.insertRow(row)
            flags = []
            if ip in banned: flags.append("BAN")
            if ip in muted:  flags.append("MUT")
            flag_str = " ".join(flags) if flags else "OK"
            e2e = "✓" if CRYPTO.has_session(ip) else "✗"
            items = [
                info.get("username","?"),
                ip,
                info.get("version","?"),
                e2e,
                flag_str,
            ]
            for col, val in enumerate(items):
                item = QTableWidgetItem(val)
                if "BAN" in flag_str:
                    item.setForeground(QColor("#FF4444"))
                elif "MUT" in flag_str:
                    item.setForeground(QColor("#FF8C00"))
                elif val == "✓":
                    item.setForeground(QColor("#39FF14"))
                elif val == "✗":
                    item.setForeground(QColor("#444460"))
                self._users_table.setItem(row, col, item)

    def _table_action(self, action: str):
        row = self._users_table.currentRow()
        if row < 0:
            QMessageBox.information(self, "Выбор", "Выберите пользователя в таблице.")
            return
        ip_item = self._users_table.item(row, 1)
        if not ip_item: return
        ip = ip_item.text()
        if not self._admin_authed:
            self._print("  ✗ Требуется /admin login", self.C_RED)
            self._tabs.setCurrentIndex(0); return
        peers = getattr(self._net, 'peers', {})
        name  = peers.get(ip, {}).get("username", ip)
        if action == "ban":
            AdminManager.ban(ip)
            if hasattr(self._net, 'kick_peer'): self._net.kick_peer(ip)
        elif action == "unban":
            AdminManager.unban(ip)
        elif action == "kick":
            if hasattr(self._net, 'kick_peer'): self._net.kick_peer(ip)
        elif action == "mute":
            AdminManager.mute(ip)
        elif action == "unmute":
            AdminManager.unmute(ip)
        elif action == "whois":
            self._tabs.setCurrentIndex(0)
            self._cmd_whois(ip)
            return
        self._refresh_users_tab()
        self._add_log(f"Table action: {action} → {name} ({ip})")

    # ══════════════════════════════════════════════════════════════════
    #  STATUS BAR
    # ══════════════════════════════════════════════════════════════════
    def _update_statusbar(self):
        peers = getattr(self._net, 'peers', {})
        e2e   = sum(1 for ip in peers if CRYPTO.has_session(ip))
        up    = int(time.time() - self._start_time)
        h, m, s = up // 3600, (up % 3600) // 60, up % 60
        self._sb_peers.setText(f"◉ Peers: {len(peers)}")
        self._sb_crypto.setText(f"🔒 E2E: {e2e}/{len(peers)}")
        self._sb_uptime.setText(f"⏱ {h:02d}:{m:02d}:{s:02d}")
        self._sb_time.setText(datetime.now().strftime("%H:%M:%S"))

    # ══════════════════════════════════════════════════════════════════
    #  TABS CHANGED
    # ══════════════════════════════════════════════════════════════════
    def _on_tab_changed(self, idx: int):
        if idx == 2:
            self._refresh_users_tab()

    # ══════════════════════════════════════════════════════════════════
    #  DRAG & COLLAPSE
    # ══════════════════════════════════════════════════════════════════
    def _tb_press(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = event.globalPosition().toPoint() - self.pos()

    def _tb_move(self, event):
        if self._drag_pos and event.buttons() == Qt.MouseButton.LeftButton:
            new_pos = event.globalPosition().toPoint() - self._drag_pos
            if self.parent():
                p = self.parent()
                nx = max(0, min(new_pos.x(), p.width()  - self.width()))
                ny = max(0, min(new_pos.y(), p.height() - self.height()))
                new_pos = QPoint(nx, ny)
            self.move(new_pos)

    def _tb_release(self, event):
        self._drag_pos = None

    def _collapse(self):
        self._collapsed = True
        self.resize(self.width(), 40)
        if self.parent():
            p = self.parent()
            self.move(p.width() - self.width() - 8, p.height() - 48)

    def _expand(self):
        self._collapsed = False
        self.resize(860, 560)
        if self.parent():
            p = self.parent()
            self.move(p.width() - 868, p.height() - 568)

    def showEvent(self, event):
        super().showEvent(event)
        if self.parent():
            p = self.parent()
            self.move(max(0, p.width()  - self.width()  - 8),
                      max(0, p.height() - self.height() - 8))
        self._input.setFocus()
        self.raise_()



# ═══════════════════════════════════════════════════════════════════════════
#  MEWA BAR VISUALIZER
# ═══════════════════════════════════════════════════════════════════════════
class _BarVisualizer(QWidget):
    """
    Визуализатор музыки.
    Реальный FFT через PyAudio (pulse monitor) если доступен numpy.
    Иначе — красивая реактивная псевдо-анимация.
    """
    BAR_COUNT = 22
    CHUNK     = 2048
    RATE      = 44100

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedHeight(80)
        self.setMinimumWidth(100)
        self._playing  = False
        self._bars     = [1.0] * self.BAR_COUNT
        self._peaks    = [1.0] * self.BAR_COUNT
        self._peak_vel = [0.0] * self.BAR_COUNT
        self._color    = "#39FF14"
        self._stream   = None
        self._pa       = None
        self._fft_data = None
        self._np       = None
        self._use_real = False
        self._phase    = 0.0  # для псевдо-анимации

        # Пробуем загрузить numpy
        try:
            import numpy as _np
            self._np = _np
        except ImportError:
            pass

        self._timer = QTimer(self)
        self._timer.setInterval(33)  # 30fps
        self._timer.timeout.connect(self._tick)
        self._timer.start()

    def set_playing(self, playing: bool):
        self._playing = playing
        if playing:
            self._try_start_real()
        else:
            self._stop_stream()

    def set_color(self, color: str):
        self._color = color
        self.update()

    def _try_start_real(self):
        """Пробуем открыть PyAudio monitor для реального FFT."""
        if self._np is None or self._use_real:
            return
        try:
            import pyaudio as _pa
            if self._pa is None:
                self._pa = _pa.PyAudio()
            # Ищем pulse/pipewire monitor
            dev_idx = None
            n = self._pa.get_device_count()
            for i in range(n):
                try:
                    info = self._pa.get_device_info_by_index(i)
                    name = info.get('name', '').lower()
                    max_in = int(info.get('maxInputChannels', 0))
                    if max_in > 0 and ('monitor' in name or 'pulse' in name):
                        dev_idx = i
                        break
                except Exception:
                    continue

            if dev_idx is None:
                return  # no монитора — используем псевдо

            self._stream = self._pa.open(
                format=_pa.paFloat32,
                channels=1,
                rate=self.RATE,
                input=True,
                input_device_index=dev_idx,
                frames_per_buffer=self.CHUNK,
                stream_callback=self._pa_callback,
            )
            self._stream.start_stream()
            self._use_real = True
        except Exception:
            self._use_real = False
            self._stream = None

    def _pa_callback(self, in_data, frame_count, time_info, status):
        try:
            np = self._np
            import pyaudio as _pa
            samples = np.frombuffer(in_data, dtype=np.float32).copy()
            win     = np.hanning(len(samples))
            fft     = np.abs(np.fft.rfft(samples * win, n=self.CHUNK))
            fft     = fft[:self.CHUNK // 2]
            n_bins  = len(fft)
            n_bars  = self.BAR_COUNT
            # Логарифмические частотные полосы
            bars = []
            lo_hz, hi_hz = 40, 18000
            for i in range(n_bars):
                f_lo = lo_hz * ((hi_hz / lo_hz) ** (i / n_bars))
                f_hi = lo_hz * ((hi_hz / lo_hz) ** ((i + 1) / n_bars))
                b_lo = max(0, int(f_lo / (self.RATE / 2) * n_bins))
                b_hi = max(b_lo+1, min(n_bins, int(f_hi / (self.RATE / 2) * n_bins)))
                val  = float(np.mean(fft[b_lo:b_hi])) if b_hi > b_lo else 0.0
                bars.append(val)
            # Нормализация с защитой от тишины
            mx = max(bars) if max(bars) > 1e-6 else 1.0
            self._fft_data = [min(60.0, (v / mx) ** 0.6 * 60.0) for v in bars]
            return (None, _pa.paContinue)
        except Exception:
            import pyaudio as _pa
            return (None, _pa.paContinue)

    def _stop_stream(self):
        if self._stream:
            try:
                self._stream.stop_stream()
                self._stream.close()
            except Exception:
                pass
            self._stream = None
        self._use_real = False
        self._fft_data = None

    def _tick(self):
        import math as _m, random as _r
        MAX_H = 60.0
        MIN_H = 1.0

        if self._playing and self._use_real and self._fft_data:
            targets = self._fft_data[:]
        elif self._playing:
            # Красивая псевдо-анимация — синусоидальные волны
            self._phase += 0.18
            targets = []
            for i in range(self.BAR_COUNT):
                # Несколько синусоид с разными частотами
                v  = (_m.sin(self._phase + i * 0.4) * 0.5 + 0.5)
                v += (_m.sin(self._phase * 1.7 + i * 0.7) * 0.3 + 0.3)
                v += (_m.sin(self._phase * 0.5 + i * 1.1) * 0.2 + 0.2) * _r.uniform(0.7, 1.3)
                v  = max(0.05, min(1.0, v / 1.0))
                # Акцент на средние частоты
                mid_boost = 1.0 - abs(i - self.BAR_COUNT/2) / (self.BAR_COUNT * 0.7)
                v = v * (0.4 + 0.6 * mid_boost)
                targets.append(MIN_H + v * (MAX_H - MIN_H))
        else:
            targets = [MIN_H] * self.BAR_COUNT

        # Сглаживание: быстрый attack, медленный decay
        for i in range(self.BAR_COUNT):
            t = targets[i]
            if t > self._bars[i]:
                self._bars[i] += (t - self._bars[i]) * 0.55  # attack
            else:
                self._bars[i] += (t - self._bars[i]) * 0.12  # decay
            self._bars[i] = max(MIN_H, min(MAX_H, self._bars[i]))

            # Пики
            if self._bars[i] >= self._peaks[i]:
                self._peaks[i]    = self._bars[i]
                self._peak_vel[i] = 0.0
            else:
                self._peak_vel[i] = min(self._peak_vel[i] + 0.35, 6.0)
                self._peaks[i]    = max(self._bars[i], self._peaks[i] - self._peak_vel[i])

        self.update()

    def paintEvent(self, event):
        from PyQt6.QtGui import (QPainter, QColor, QLinearGradient,
                                  QBrush, QPen, QRadialGradient)
        from PyQt6.QtCore import QRectF, QPointF
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        w  = self.width()
        h  = self.height()
        n  = self.BAR_COUNT
        gap = 3
        bar_w = max(3, (w - gap * (n - 1)) // n)

        bc = QColor(self._color)

        for i in range(n):
            x      = i * (bar_w + gap)
            bar_h  = max(2, int(self._bars[i]))
            y      = h - bar_h

            # Многоуровневый градиент: яркий верх → тёмный низ
            top_c  = QColor(bc); top_c.setAlphaF(1.0)
            mid_c  = QColor(bc); mid_c.setAlphaF(0.6)
            bot_c  = QColor(bc); bot_c.setAlphaF(0.2)

            grad = QLinearGradient(QPointF(x, y), QPointF(x, h))
            grad.setColorAt(0.0, top_c)
            grad.setColorAt(0.5, mid_c)
            grad.setColorAt(1.0, bot_c)

            p.setBrush(QBrush(grad))
            p.setPen(Qt.PenStyle.NoPen)
            r = min(bar_w // 2, 4)
            p.drawRoundedRect(QRectF(x, y, bar_w, bar_h), r, r)

            # Подсветка — яркая полоска сверху полосы
            if bar_h > 6:
                glow = QColor(bc); glow.setAlphaF(0.9)
                p.setBrush(QBrush(glow))
                p.drawRoundedRect(QRectF(x, y, bar_w, min(3, bar_h)), r, r)

            # Пиковая метка
            pk = int(self._peaks[i])
            if pk > bar_h + 3:
                pc = QColor(bc); pc.setAlphaF(0.95)
                pen = QPen(pc, 1.5)
                pen.setCapStyle(Qt.PenCapStyle.RoundCap)
                p.setPen(pen)
                p.setBrush(Qt.BrushStyle.NoBrush)
                py = h - pk
                p.drawLine(QPointF(x + 1, py), QPointF(x + bar_w - 1, py))

        # Зеркальное отражение внизу (полупрозрачное)
        p.setOpacity(0.15)
        for i in range(n):
            x     = i * (bar_w + gap)
            bar_h = max(1, int(self._bars[i] * 0.3))
            ref_c = QColor(bc); ref_c.setAlphaF(0.4)
            grad2 = QLinearGradient(QPointF(x, h), QPointF(x, h + bar_h))
            grad2.setColorAt(0.0, ref_c)
            grad2.setColorAt(1.0, Qt.GlobalColor.transparent)
            p.setBrush(QBrush(grad2))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRect(QRectF(x, h, bar_w, min(bar_h, 10)))
        p.setOpacity(1.0)

        p.end()

    def closeEvent(self, event):
        self._stop_stream()
        if self._pa:
            try: self._pa.terminate()
            except Exception: pass
        super().closeEvent(event)


class _FSKeyFilter(QObject):
    """Event filter to exit fullscreen on ESC/F/F11."""
    def __init__(self, parent, handler):
        super().__init__(parent)
        self._handler = handler
    def eventFilter(self, obj, event):
        try:
            result = self._handler(event)
            if result: return True
        except Exception: pass
        return super().eventFilter(obj, event)


class MewaPlayer(QWidget):
    """
    Mewa 1-2-3 v1.32 — медиаплеер GoidaPhone.
    Структура: боковая иконочная панель + основная область со стеком страниц
    + нижняя полоска управления воспроизведением. Всё следует текущей теме.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self._playlist: list[dict] = []
        self._queue_idx  = -1
        self._shuffle    = False
        self._repeat     = "off"          # off | one | all
        self._has_player = False
        self._player     = None
        self._audio_out  = None
        self._setup()

    # ════════════════════════════════ UI SETUP ════════════════════════════════
    def _setup(self):
        t = get_theme(S().theme)
        outer = QHBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        # ── SIDEBAR ───────────────────────────────────────────────────────────
        self._sidebar = QWidget()
        self._sidebar.setFixedWidth(80)
        self._sidebar.setObjectName("mewa_side")
        self._sidebar.setStyleSheet(
            f"QWidget#mewa_side{{background:{t['bg3']};"
            f"border-right:1px solid {t['border']};}}")
        sb = QVBoxLayout(self._sidebar)
        sb.setContentsMargins(0, 6, 0, 6)
        sb.setSpacing(0)

        # Logo bar with accent bg — MEWA ASCII
        logo_bar = QWidget()
        logo_bar.setFixedHeight(64)
        logo_bar.setStyleSheet(
            f"background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            f"stop:0 {t['accent']},stop:1 {t['bg3']});")
        logo_lay = QVBoxLayout(logo_bar)
        logo_lay.setContentsMargins(0, 4, 0, 4)
        logo_lay.setSpacing(0)
        logo = QLabel("MEWA")
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo.setStyleSheet(
            "font-size:16px;color:white;background:transparent;"
            "font-weight:900;letter-spacing:4px;font-family:monospace;")
        logo_sub = QLabel("1-2-3")
        logo_sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
        logo_sub.setStyleSheet(
            "font-size:8px;color:rgba(255,255,255,160);background:transparent;"
            "letter-spacing:3px;font-family:monospace;")
        logo_lay.addWidget(logo)
        logo_lay.addWidget(logo_sub)
        sb.addWidget(logo_bar)

        self._section_btns: dict[str, QPushButton] = {}
        SECTIONS = [
            ("queue",    "▶",  "Queue"),
            ("library",  "♫",  "Library"),
            ("files",    "▤",  "Files"),
            ("playlists","≡",  "Playlists"),
            ("video",    "🎬", "Video"),
            ("radio",    "📻", "Radio"),
            ("eq",       "🎚", "EQ"),
            ("lyrics",   "📝", "Lyrics"),
        ]
        for key, ico, label in SECTIONS:
            btn = QPushButton(f"{ico}\n{label}")
            btn.setCheckable(True)
            btn.setFixedHeight(62)
            btn.setStyleSheet(
                f"QPushButton{{background:transparent;color:{t['text_dim']};"
                "border:none;border-left:3px solid transparent;"
                "font-size:10px;font-weight:bold;padding:4px 0;}}"
                f"QPushButton:hover{{background:{t['btn_hover']};color:{t['text']};"
                "border-left:3px solid transparent;}}"
                f"QPushButton:checked{{background:{t['bg3']};color:white;"
                f"border-left:3px solid {t['accent']};}}"
                f"QPushButton:checked:hover{{background:{t['bg3']};color:white;"
                f"border-left:3px solid {t['accent']};}}")
            btn.clicked.connect(lambda _, k=key: self._show_section(k))
            self._section_btns[key] = btn
            sb.addWidget(btn)

        sb.addStretch()
        ver = QLabel("v1.32\nWinora")
        ver.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ver.setStyleSheet(f"font-size:7px;color:{t['text_dim']};background:transparent;padding:6px 0;")
        sb.addWidget(ver)
        outer.addWidget(self._sidebar)

        # ── RIGHT COLUMN ──────────────────────────────────────────────────────
        right_col = QWidget()
        right_col.setObjectName("mewa_right")
        right_col.setStyleSheet(f"QWidget#mewa_right{{background:{t['bg']};}}")
        rc_lay = QVBoxLayout(right_col)
        rc_lay.setContentsMargins(0, 0, 0, 0)
        rc_lay.setSpacing(0)

        # ── TOOLBAR ───────────────────────────────────────────────────────────
        tb = QWidget()
        tb.setFixedHeight(38)
        tb.setObjectName("mewa_tb")
        tb.setStyleSheet(
            f"QWidget#mewa_tb{{background:{t['bg2']};"
            f"border-bottom:1px solid {t['border']};}}")
        tbl = QHBoxLayout(tb)
        tbl.setContentsMargins(8, 0, 8, 0)
        tbl.setSpacing(4)

        def mk_tb(txt, tip, cb):
            b = QPushButton(txt)
            b.setFixedSize(30, 26)
            b.setToolTip(tip)
            b.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:4px;font-size:11px;}}"
                f"QPushButton:hover{{background:{t['btn_hover']};}}")
            b.clicked.connect(cb)
            tbl.addWidget(b)

        mk_tb("+", "Add files", self._add_files)
        mk_tb("▤", "Добавить папку", self._add_folder)
        mk_tb("✕", "Clear queue", self._clear_queue)
        tbl.addStretch()
        self._search_edit = QLineEdit()
        self._search_edit.setPlaceholderText(_L("🔍 Поиск...", "🔍 Search...", "🔍 検索..."))
        self._search_edit.setFixedWidth(180)
        self._search_edit.setStyleSheet(
            f"QLineEdit{{background:{t['bg3']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:4px;"
            "padding:2px 8px;font-size:10px;}}")
        self._search_edit.textChanged.connect(self._filter_lib)
        tbl.addWidget(self._search_edit)
        rc_lay.addWidget(tb)

        # ── PAGE STACK ────────────────────────────────────────────────────────
        self._stack = QStackedWidget()

        # Page 0 — Queue
        qp = QWidget()
        qp_lay = QVBoxLayout(qp)
        qp_lay.setContentsMargins(0, 0, 0, 0)
        self._queue_tbl = self._mk_table(["#", "Название", "Исполнитель", "Длина"])
        self._queue_tbl.setColumnWidth(0, 36)
        self._queue_tbl.setColumnWidth(3, 54)
        self._queue_tbl.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.Stretch)
        self._queue_tbl.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.Stretch)
        self._queue_tbl.doubleClicked.connect(lambda i: self._play_idx(i.row()))
        self._queue_tbl.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._queue_tbl.customContextMenuRequested.connect(self._queue_ctx)
        qp_lay.addWidget(self._queue_tbl)
        self._stack.addWidget(qp)   # idx 0

        # Page 1 — Library  (art panel + track table)
        lp = QWidget()
        lp_lay = QHBoxLayout(lp)
        lp_lay.setContentsMargins(0, 0, 0, 0)
        lp_lay.setSpacing(0)

        art_panel = QWidget()
        art_panel.setFixedWidth(210)
        art_panel.setObjectName("mewa_art")
        art_panel.setStyleSheet(
            f"QWidget#mewa_art{{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            f"stop:0 {t['bg3']},stop:1 {t['bg2']});"
            f"border-right:1px solid {t['border']};}}")
        apl = QVBoxLayout(art_panel)
        apl.setContentsMargins(12, 14, 12, 14)
        apl.setSpacing(8)

        self._art_lbl = QLabel("♫")
        self._art_lbl.setFixedSize(186, 186)
        self._art_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._art_lbl.setObjectName("mewa_art_img")
        self._art_lbl.setStyleSheet(
            f"QLabel#mewa_art_img{{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            f"stop:0 {t['bg3']},stop:1 {t['bg']});"
            f"border-radius:14px;font-size:64px;color:{t['accent']};"
            f"border:2px solid {t['border']};}}")
        apl.addWidget(self._art_lbl)

        self._np_title  = QLabel("No track")
        self._np_title.setWordWrap(True)
        self._np_title.setStyleSheet(
            f"font-size:12px;font-weight:bold;color:{t['text']};background:transparent;")
        self._np_artist = QLabel("")
        self._np_artist.setStyleSheet(
            f"font-size:11px;color:{t['accent']};background:transparent;")
        self._np_album  = QLabel("")
        self._np_album.setStyleSheet(
            f"font-size:10px;color:{t['text_dim']};background:transparent;")
        for lbl in (self._np_title, self._np_artist, self._np_album):
            lbl.setWordWrap(True)
            apl.addWidget(lbl)

        # ── Визуализатор полосочки ─────────────────────────────────────
        self._visualizer = _BarVisualizer()
        self._visualizer.set_color(t.get('accent', '#39FF14'))
        apl.addWidget(self._visualizer)

        apl.addStretch()
        lp_lay.addWidget(art_panel)

        self._lib_tbl = self._mk_table(["Название", "Исполнитель", "Альбом", "Длина"])
        self._lib_tbl.setColumnWidth(3, 54)
        for c in (0, 1, 2):
            self._lib_tbl.horizontalHeader().setSectionResizeMode(c, QHeaderView.ResizeMode.Stretch)
        self._lib_tbl.doubleClicked.connect(lambda i: self._play_idx(i.row()))
        lp_lay.addWidget(self._lib_tbl)
        self._stack.addWidget(lp)   # idx 1

        # Page 2 — Files
        fp = QWidget()
        fpl = QVBoxLayout(fp)
        fpl.setContentsMargins(24, 24, 24, 24)
        fpl.setSpacing(12)
        hint = QLabel("Перетащите файлы/папки сюда\nили воспользуйтесь кнопками в тулбаре")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint.setStyleSheet(f"font-size:13px;color:{t['text_dim']};background:transparent;")
        fpl.addStretch()
        fpl.addWidget(hint)
        btn_row = QHBoxLayout()
        for txt, tip, cb in [
            ("➕ Добавить файлы", "Аудио/видео", self._add_files),
            ("📁 Добавить папку", "Рекурсивно",  self._add_folder),
        ]:
            b = QPushButton(txt)
            b.setToolTip(tip)
            b.setFixedHeight(38)
            b.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:6px;font-size:12px;padding:0 14px;}}"
                f"QPushButton:hover{{background:{t['btn_hover']};}}")
            b.clicked.connect(cb)
            btn_row.addWidget(b)
        fpl.addLayout(btn_row)
        fpl.addStretch()
        self._stack.addWidget(fp)   # idx 2

        # Page 3 — Playlists
        pp = QWidget()
        ppl = QVBoxLayout(pp)
        ppl.setContentsMargins(8, 8, 8, 8)
        ppl.setSpacing(6)
        self._pl_list = QListWidget()
        self._pl_list.setStyleSheet(
            f"QListWidget{{background:{t['bg']};color:{t['text']};border:none;}}"
            f"QListWidget::item{{padding:8px 12px;border-bottom:1px solid {t['bg3']};}}"
            f"QListWidget::item:selected{{background:{t['accent']};}}")
        self._pl_list.doubleClicked.connect(self._load_saved_playlist)
        ppl.addWidget(self._pl_list)
        pl_btns = QHBoxLayout()
        for txt, cb in [("💾 Сохранить текущую", self._save_playlist),
                        ("🗑 Удалить", self._delete_saved_playlist)]:
            b = QPushButton(txt)
            b.setFixedHeight(32)
            b.clicked.connect(cb)
            pl_btns.addWidget(b)
        ppl.addLayout(pl_btns)
        self._stack.addWidget(pp)   # idx 3

        # ── Page 4 — Video player ─────────────────────────────────────────────
        vp = QWidget()
        vp.setObjectName("mewa_video_page")
        vp.setStyleSheet(f"QWidget#mewa_video_page{{background:#000;}}")
        vp_lay = QVBoxLayout(vp)
        vp_lay.setContentsMargins(0, 0, 0, 0)
        vp_lay.setSpacing(0)

        try:
            from PyQt6.QtMultimediaWidgets import QVideoWidget as _QVW
            self._video_widget = _QVW()
            self._video_widget.setStyleSheet("background:#000;")
            vp_lay.addWidget(self._video_widget, stretch=1)
        except ImportError:
            self._video_widget = None
            _vp_hint = QLabel(
                "📦 Установи для видео:\n"
                "pip install PyQt6-QtMultimediaWidgets --break-system-packages")
            _vp_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
            _vp_hint.setStyleSheet(
                "color:#FF9040;font-size:12px;background:transparent;")
            vp_lay.addWidget(_vp_hint, stretch=1)

        # Info bar
        vp_info = QWidget()
        vp_info.setFixedHeight(32)
        vp_info.setStyleSheet(
            f"background:{t['bg3']};border-top:1px solid {t['border']};")
        vp_info_lay = QHBoxLayout(vp_info)
        vp_info_lay.setContentsMargins(10, 0, 10, 0)
        self._video_title_lbl = QLabel("Нет видео")
        self._video_title_lbl.setStyleSheet(
            f"font-size:11px;color:{t['text']};background:transparent;")
        vp_info_lay.addWidget(self._video_title_lbl)
        vp_info_lay.addStretch()

        _back_btn = QPushButton("⬅ К очереди")
        _back_btn.setFixedHeight(24)
        _back_btn.setStyleSheet(
            f"QPushButton{{background:{t['bg2']};color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:4px;"
            "padding:0 8px;font-size:10px;}}"
            f"QPushButton:hover{{color:{t['text']};background:{t['btn_hover']};}}")
        _back_btn.clicked.connect(lambda: self._show_section("queue"))
        vp_info_lay.addWidget(_back_btn)

        _fs_btn = QPushButton("⛶")
        _fs_btn.setFixedSize(24, 24)
        _fs_btn.setToolTip("Полный экран  F")
        _fs_btn.setStyleSheet(
            f"QPushButton{{background:{t['bg2']};color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:4px;font-size:13px;}}"
            f"QPushButton:hover{{color:{t['text']};background:{t['btn_hover']};}}")
        _fs_btn.clicked.connect(self._toggle_video_fullscreen)
        vp_info_lay.addWidget(_fs_btn)
        vp_lay.addWidget(vp_info)
        self._stack.addWidget(vp)   # idx 4

        # ── Page 5 — Online Radio ─────────────────────────────────────────────
        rp = QWidget()
        rp.setStyleSheet(f"background:{t['bg']};")
        rp_lay = QVBoxLayout(rp)
        rp_lay.setContentsMargins(12, 12, 12, 12)
        rp_lay.setSpacing(10)

        # Header
        rp_hdr = QLabel("📻  Онлайн Радио")
        rp_hdr.setStyleSheet(
            f"font-size:15px;font-weight:bold;color:{t['accent']};background:transparent;")
        rp_lay.addWidget(rp_hdr)

        # Current playing display
        self._radio_now = QLabel("Нет трансляции")
        self._radio_now.setStyleSheet(
            f"font-size:12px;color:{t['text']};background:{t['bg3']};"
            f"border-radius:8px;padding:10px;border:1px solid {t['border']};")
        self._radio_now.setWordWrap(True)
        rp_lay.addWidget(self._radio_now)

        # Station list
        self._radio_list = QListWidget()
        self._radio_list.setStyleSheet(
            f"QListWidget{{background:{t['bg3']};color:{t['text']};"
            "border:none;border-radius:8px;font-size:11px;}}"
            f"QListWidget::item{{padding:10px 14px;"
            f"border-bottom:1px solid {t['border']};}}"
            f"QListWidget::item:selected{{background:{t['accent']};color:white;}}"
            f"QListWidget::item:hover{{background:{t['btn_hover']};}}")

        RADIO_STATIONS = [
            # ── Lofi / Chill ──────────────────────────────────────────────
            ("🎵 Lofi Girl",         "http://lofi.stream.laut.fm/lofi"),
            ("🌊 Chill Beats",       "http://chill.stream.laut.fm/chill"),
            ("☕ Café Jazz",          "http://jazz.stream.laut.fm/jazz"),
            # ── Electronic ───────────────────────────────────────────────
            ("🔥 Dance & EDM",       "http://dance.stream.laut.fm/dance"),
            ("🎛 Techno FM",         "http://techno.stream.laut.fm/techno"),
            ("🌐 Trance Energy",     "http://trance.stream.laut.fm/trance"),
            # ── Rock / Metal ─────────────────────────────────────────────
            ("🎸 Rock Antenne",      "http://mp3channels.webradio.antenne.de/rockantenne"),
            ("🤘 Metal Rock",        "http://metal.stream.laut.fm/metal"),
            # ── Classical / Ambient ──────────────────────────────────────
            ("🎹 Classical Radio",   "http://classical.stream.laut.fm/classical"),
            ("🎻 Ambient",           "http://ambient.stream.laut.fm/ambient"),
            # ── Pop / Hits ───────────────────────────────────────────────
            ("🎤 Pop Hits",          "http://top40.stream.laut.fm/top40"),
            ("🇩🇪 Antenne Bayern",   "http://mp3channels.webradio.antenne.de/antenne"),
            # ── Русское ──────────────────────────────────────────────────
            ("🇷🇺 Европа Плюс",     "https://online.radiorecord.ru:8102/ep-320"),
            ("🛣 Радио Дорога",      "https://doroga.hostingradio.ru/doroga128.mp3"),
            ("📻 Радио Jazz",        "https://radiojazzfm.hostingradio.ru/jazz128.mp3"),
            ("🎙 Радио Рекорд",      "https://online.radiorecord.ru:8102/rr-320"),
            # ── World / Other ────────────────────────────────────────────
            ("🌍 Radio Paradise",    "http://stream.radioparadise.com/aac-320"),
            ("🎷 Jazz 24",           "http://live.amperwave.net/direct/ppm-jazz24aac-ibc1"),
        ]
        self._radio_stations = RADIO_STATIONS
        for name, url in RADIO_STATIONS:
            item = QListWidgetItem(name)
            item.setData(Qt.ItemDataRole.UserRole, url)
            self._radio_list.addItem(item)

        self._radio_list.itemDoubleClicked.connect(self._play_radio)
        rp_lay.addWidget(self._radio_list, stretch=1)

        # Custom URL row
        custom_row = QHBoxLayout()
        self._radio_url_input = QLineEdit()
        self._radio_url_input.setPlaceholderText("URL потока (.mp3 .ogg .m3u8)…")
        self._radio_url_input.setStyleSheet(
            f"QLineEdit{{background:{t['bg3']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:6px;"
            "padding:5px 10px;font-size:11px;}}"
            f"QLineEdit:focus{{border-color:{t['accent']};}}")
        custom_row.addWidget(self._radio_url_input, stretch=1)

        play_custom = QPushButton("▶ Слушать")
        play_custom.setFixedHeight(32)
        play_custom.setObjectName("accent_btn")
        play_custom.clicked.connect(self._play_radio_url)
        custom_row.addWidget(play_custom)
        rp_lay.addLayout(custom_row)

        # Install hint
        _hint = QLabel("ℹ Для потоков нужен mpv или vlc:  sudo emerge -av media-video/mpv")
        _hint.setStyleSheet(
            f"font-size:9px;color:{t['text_dim']};background:transparent;")
        _hint.setWordWrap(True)
        rp_lay.addWidget(_hint)

        # Stop button
        stop_radio = QPushButton("⏹ Стоп")
        stop_radio.setFixedHeight(32)
        stop_radio.setStyleSheet(
            f"QPushButton{{background:{t['bg3']};color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:6px;font-size:11px;}}"
            f"QPushButton:hover{{background:{t['btn_hover']};color:{t['text']};}}")
        stop_radio.clicked.connect(self._stop_radio)
        rp_lay.addWidget(stop_radio)

        self._stack.addWidget(rp)   # idx 5

        # ── Page 6 — Equalizer ────────────────────────────────────────────────
        ep = QWidget()
        ep.setStyleSheet(f"background:{t['bg']};")
        ep_lay = QVBoxLayout(ep)
        ep_lay.setContentsMargins(20,16,20,16); ep_lay.setSpacing(12)

        eq_hdr = QLabel("🎚  Эквалайзер")
        eq_hdr.setStyleSheet(
            f"font-size:15px;font-weight:bold;color:{t['accent']};background:transparent;")
        ep_lay.addWidget(eq_hdr)

        EQ_BANDS = [32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
        EQ_LABELS = ["32","64","125","250","500","1k","2k","4k","8k","16k"]
        self._eq_sliders = []
        sliders_row = QHBoxLayout()
        sliders_row.setSpacing(8)

        for i, (hz, lbl) in enumerate(zip(EQ_BANDS, EQ_LABELS)):
            col = QVBoxLayout()
            col.setAlignment(Qt.AlignmentFlag.AlignHCenter)
            sl = QSlider(Qt.Orientation.Vertical)
            sl.setRange(-12, 12); sl.setValue(0)
            sl.setFixedHeight(160)
            sl.setStyleSheet(
                f"QSlider::groove:vertical{{background:{t['bg3']};width:6px;border-radius:3px;}}"
                f"QSlider::handle:vertical{{background:{t['accent']};width:16px;height:16px;margin:-5px -5px;border-radius:8px;}}"
                f"QSlider::add-page:vertical{{background:{t['accent']};border-radius:3px;}}")
            val_lbl = QLabel("0 dB")
            val_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            val_lbl.setStyleSheet(
                f"font-size:9px;color:{t['text_dim']};background:transparent;")
            sl.valueChanged.connect(
                lambda v, _l=val_lbl, _i=i: (
                    _l.setText(f"{'+' if v>0 else ''}{v} dB"),
                    self._apply_eq()))
            hz_lbl = QLabel(lbl + " Hz")
            hz_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            hz_lbl.setStyleSheet(
                f"font-size:9px;color:{t['text_dim']};background:transparent;")
            col.addWidget(val_lbl)
            col.addWidget(sl, alignment=Qt.AlignmentFlag.AlignHCenter)
            col.addWidget(hz_lbl)
            sliders_row.addLayout(col)
            self._eq_sliders.append(sl)

        ep_lay.addLayout(sliders_row)

        eq_btns = QHBoxLayout()
        for preset_name, vals in [
            ("Flat",     [0]*10),
            ("Bass+",    [8,6,4,2,0,0,0,0,0,0]),
            ("Treble+",  [0,0,0,0,0,2,4,6,8,10]),
            ("Rock",     [5,4,3,0,-2,-1,2,4,5,5]),
            ("Classical",[0,0,0,0,0,0,-2,-4,-4,-5]),
            ("Vocal",    [-2,-1,0,2,4,4,3,2,1,0]),
        ]:
            pb = QPushButton(preset_name)
            pb.setFixedHeight(28)
            pb.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:6px;font-size:10px;}}"
                f"QPushButton:hover{{background:{t['btn_hover']};}}")
            pb.clicked.connect(
                lambda _, v=vals: [s.setValue(v[i])
                                   for i, s in enumerate(self._eq_sliders)])
            eq_btns.addWidget(pb)
        ep_lay.addLayout(eq_btns)

        # EQ hint
        import shutil as _sh_eq
        _eq_hint = QLabel(
            "ℹ EQ влияет на громкость полос. Для полноценного EQ установи mpv."
            if not _sh_eq.which("mpv") else
            "ℹ EQ применяется к следующему треку.")
        _eq_hint.setStyleSheet(
            f"color:{'#F39C12' if not _sh_eq.which('mpv') else t['text_dim']};"
            "font-size:8pt;background:transparent;")
        _eq_hint.setWordWrap(True)
        self._eq_status_lbl = _eq_hint
        ep_lay.addWidget(_eq_hint)

        ep_lay.addStretch()
        self._stack.addWidget(ep)   # idx 6

        # ── Page 7 — Lyrics ───────────────────────────────────────────────────
        lp2 = QWidget()
        lp2.setStyleSheet(f"background:{t['bg']};")
        lp2_lay = QVBoxLayout(lp2)
        lp2_lay.setContentsMargins(16,12,16,12); lp2_lay.setSpacing(8)

        ly_hdr_row = QHBoxLayout()
        ly_hdr = QLabel("📝  Текст песни")
        ly_hdr.setStyleSheet(
            f"font-size:14px;font-weight:bold;color:{t['accent']};background:transparent;")
        ly_hdr_row.addWidget(ly_hdr)
        ly_hdr_row.addStretch()
        ly_search_btn = QPushButton("🔍 Найти")
        ly_search_btn.setObjectName("accent_btn")
        ly_search_btn.setFixedHeight(30)
        ly_hdr_row.addWidget(ly_search_btn)
        lp2_lay.addLayout(ly_hdr_row)

        ly_query_row = QHBoxLayout()
        self._ly_artist = QLineEdit()
        self._ly_artist.setPlaceholderText("Исполнитель")
        self._ly_title  = QLineEdit()
        self._ly_title.setPlaceholderText("Название")
        for w in [self._ly_artist, self._ly_title]:
            w.setStyleSheet(
                f"background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:6px;"
                "padding:4px 8px;font-size:11px;")
        ly_query_row.addWidget(self._ly_artist)
        ly_query_row.addWidget(self._ly_title)
        lp2_lay.addLayout(ly_query_row)

        self._ly_text = QPlainTextEdit()
        self._ly_text.setReadOnly(True)
        self._ly_text.setStyleSheet(
            f"QPlainTextEdit{{background:{t['bg3']};color:{t['text']};"
            "font-size:12px;border:none;"
            "border-radius:10px;padding:16px;}}")
        self._ly_text.setPlaceholderText(
            "Выберите трек и нажмите Найти, или введите вручную. "
            "Тексты с lyrics.ovh (без ключа API).")
        lp2_lay.addWidget(self._ly_text, stretch=1)
        ly_search_btn.clicked.connect(self._fetch_lyrics)
        self._stack.addWidget(lp2)   # idx 7

        rc_lay.addWidget(self._stack, stretch=1)

        # ── PLAYER BAR ────────────────────────────────────────────────────────
        pbar = QWidget()
        pbar.setObjectName("mewa_pbar")
        pbar.setFixedHeight(84)
        pbar.setStyleSheet(
            f"QWidget#mewa_pbar{{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            f"stop:0 {t['bg3']},stop:1 {t['bg2']});"
            f"border-top:2px solid {t['accent']};}}")
        pbl = QVBoxLayout(pbar)
        pbl.setContentsMargins(12, 5, 12, 5)
        pbl.setSpacing(3)

        # Progress row
        pr = QHBoxLayout()
        self._t_cur = QLabel("0:00")
        self._t_cur.setFixedWidth(36)
        self._t_cur.setStyleSheet(
            f"font-size:9px;color:{t['text_dim']};background:transparent;")
        self._prog = QSlider(Qt.Orientation.Horizontal)
        self._prog.setRange(0, 1000)
        self._prog.sliderMoved.connect(self._seek)
        self._prog.setStyleSheet(
            f"QSlider::groove:horizontal{{height:4px;background:{t['bg3']};border-radius:2px;}}"
            f"QSlider::handle:horizontal{{width:12px;height:12px;margin:-4px 0;"
            f"background:{t['accent']};border-radius:6px;}}"
            f"QSlider::sub-page:horizontal{{background:{t['accent']};border-radius:2px;}}")
        self._t_tot = QLabel("0:00")
        self._t_tot.setFixedWidth(36)
        self._t_tot.setAlignment(Qt.AlignmentFlag.AlignRight|Qt.AlignmentFlag.AlignVCenter)
        self._t_tot.setStyleSheet(self._t_cur.styleSheet())
        pr.addWidget(self._t_cur)
        pr.addWidget(self._prog)
        pr.addWidget(self._t_tot)
        pbl.addLayout(pr)

        # Controls row
        cr = QHBoxLayout()
        cr.setSpacing(5)

        def mk_c(txt, tip, cb, w=34, accent=False):
            b = QPushButton(txt)
            b.setFixedSize(w, 30)
            b.setToolTip(tip)
            if accent:
                s = (f"QPushButton{{background:{t['accent']};color:white;"
                     "border-radius:15px;font-size:15px;border:none;"
                     "border-bottom:2px solid rgba(0,0,0,89);}}"
                     f"QPushButton:hover{{background:{t['accent2']};}}"
                     f"QPushButton:pressed{{padding-top:2px;border-bottom:1px solid;}}")
            else:
                s = (f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                     f"border-radius:6px;font-size:13px;border:1px solid {t['border']};"
                     "border-bottom:2px solid rgba(0,0,0,63);}}"
                     f"QPushButton:hover{{background:{t['btn_hover']};}}"
                     f"QPushButton:pressed{{padding-top:2px;border-bottom:1px solid;}}")
            b.setStyleSheet(s)
            b.clicked.connect(cb)
            return b

        cr.addWidget(mk_c("|◀", "Previous", self._prev))
        cr.addWidget(mk_c("◀◀", "−10 сек",    lambda: self._skip(-10)))
        self._play_btn = mk_c("▶", "Play/Pause", self._toggle_play, w=42, accent=True)
        cr.addWidget(self._play_btn)
        cr.addWidget(mk_c("▶▶", "+10 сек",    lambda: self._skip(10)))
        cr.addWidget(mk_c("▶|", "Next",  self._next))
        cr.addSpacing(10)

        self._shuf_btn = QPushButton("RND")
        self._shuf_btn.setFixedSize(30, 30)
        self._shuf_btn.setCheckable(True)
        self._shuf_btn.setToolTip("Случайный порядок")
        self._shuf_btn.toggled.connect(lambda v: setattr(self,'_shuffle',v))
        self._shuf_btn.setStyleSheet(
            f"QPushButton{{background:{t['bg3']};border-radius:5px;font-size:12px;"
            f"border:1px solid {t['border']};}}"
            f"QPushButton:checked{{background:{t['accent']};"
            f"border-color:{t['accent']};}}")
        cr.addWidget(self._shuf_btn)

        self._rep_btn = QPushButton("RPT")
        self._rep_btn.setFixedSize(30, 30)
        self._rep_btn.setCheckable(True)
        self._rep_btn.setToolTip("Repeat")
        self._rep_btn.toggled.connect(self._cycle_repeat)
        self._rep_btn.setStyleSheet(self._shuf_btn.styleSheet())
        cr.addWidget(self._rep_btn)
        cr.addStretch()

        np_box = QWidget()
        np_box.setMaximumWidth(340)
        np_box_lay = QVBoxLayout(np_box)
        np_box_lay.setContentsMargins(0,0,0,0)
        np_box_lay.setSpacing(0)
        self._np_bar = QLabel("No track")
        self._np_bar.setStyleSheet(
            f"font-size:12px;font-weight:bold;color:{t['text']};background:transparent;")
        self._np_bar.setMaximumWidth(340)
        self._np_bar_artist = QLabel("")
        self._np_bar_artist.setStyleSheet(
            f"font-size:9px;color:{t['accent']};background:transparent;")
        self._np_bar_artist.setMaximumWidth(340)
        np_box_lay.addWidget(self._np_bar)
        np_box_lay.addWidget(self._np_bar_artist)
        cr.addWidget(np_box)
        cr.addStretch()

        vol_ic = QLabel("🔊")
        vol_ic.setStyleSheet("font-size:13px;background:transparent;")
        self._vol_sl = QSlider(Qt.Orientation.Horizontal)
        self._vol_sl.setRange(0, 100)
        self._vol_sl.setValue(85)
        self._vol_sl.setFixedWidth(88)
        self._vol_sl.valueChanged.connect(self._set_vol)
        self._vol_sl.setStyleSheet(self._prog.styleSheet())
        cr.addWidget(vol_ic)
        cr.addWidget(self._vol_sl)
        pbl.addLayout(cr)

        rc_lay.addWidget(pbar)
        outer.addWidget(right_col)

        # ── INIT ──────────────────────────────────────────────────────────────
        self._init_player()
        self._refresh_saved_playlists()
        self._show_section("queue")

    # ── helper ───────────────────────────────────────────────────────────────
    def _mk_table(self, headers: list) -> QTableWidget:
        t = get_theme(S().theme)
        tbl = QTableWidget(0, len(headers))
        tbl.setHorizontalHeaderLabels(headers)
        tbl.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        tbl.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        tbl.verticalHeader().setVisible(False)
        tbl.setShowGrid(False)
        tbl.setStyleSheet(
            f"QTableWidget{{background:{t['bg']};color:{t['text']};"
            "border:none;font-size:11px;}}"
            f"QTableWidget::item{{padding:5px 8px;"
            f"border-bottom:1px solid {t['bg3']};}}"
            f"QTableWidget::item:selected{{background:{t['accent']};"
            f"color:{t['text']};}}"
            f"QHeaderView::section{{background:{t['bg2']};color:{t['text_dim']};"
            "border:none;padding:4px 8px;font-size:9px;font-weight:bold;"
            f"border-bottom:1px solid {t['border']};}}")
        tbl.horizontalHeader().setStretchLastSection(False)
        tbl.verticalHeader().setDefaultSectionSize(26)
        return tbl

    # ── sections ──────────────────────────────────────────────────────────────
    _SEC_IDX = {"queue":0,"library":1,"files":2,"playlists":3,"video":4,"radio":5,"eq":6,"lyrics":7}

    def _show_section(self, key: str):
        for k, b in self._section_btns.items():
            b.setChecked(k == key)
        self._stack.setCurrentIndex(self._SEC_IDX.get(key, 0))

    # ── player init ───────────────────────────────────────────────────────────
    def _init_player(self):
        try:
            from PyQt6.QtMultimedia import QMediaPlayer, QAudioOutput, QMediaDevices
            self._player     = QMediaPlayer()
            self._audio_out  = QAudioOutput()
            self._player.setAudioOutput(self._audio_out)
            self._audio_out.setVolume(0.85)
            self._player.positionChanged.connect(self._on_pos)
            self._player.durationChanged.connect(self._on_dur)
            self._player.playbackStateChanged.connect(self._on_state)
            self._player.mediaStatusChanged.connect(self._on_status)
            # Connect video output
            if getattr(self, '_video_widget', None) is not None:
                self._player.setVideoOutput(self._video_widget)
            # Hot-swap: пересоздаём QAudioOutput когда система меняет устройство
            try:
                self._media_devices = QMediaDevices(self)
                self._media_devices.audioOutputsChanged.connect(
                    self._on_audio_device_changed)
            except Exception:
                pass
            self._has_player = True
        except Exception as e:
            self._has_player = False
            err = QLabel(
                f"⚠  PyQt6.QtMultimedia недоступен:\n{e}\n\n"
                "Для воспроизведения установите:\n"
                "pip install PyQt6-Qt6-Multimedia --break-system-packages\n\n"
                "Плейлист всё равно работает — добавляйте файлы.")
            err.setWordWrap(True)
            err.setAlignment(Qt.AlignmentFlag.AlignCenter)
            err.setStyleSheet("color:#FF9040;font-size:11px;background:transparent;padding:20px;")
            self._queue_tbl.setRowCount(0)
            # Add a spanning label-row workaround
            self._queue_tbl.setRowCount(1)
            self._queue_tbl.setSpan(0, 0, 1, 4)
            lbl_item = QTableWidgetItem(
                f"⚠  Аудиодвижок недоступен: {e}  —  "
                "pip install PyQt6-Qt6-Multimedia --break-system-packages")
            lbl_item.setForeground(QColor("#FF9040"))
            self._queue_tbl.setItem(0, 0, lbl_item)

    # ── file management ───────────────────────────────────────────────────────
    def _add_files(self):
        files, _ = QFileDialog.getOpenFileNames(
            self, "Добавить медиафайлы", "",
            "Media (*.mp3 *.wav *.ogg *.flac *.aac *.m4a *.opus *.wma "
            "*.mp4 *.avi *.mkv *.webm *.mov *.m4v)")
        for f in files:
            self._add_track(f)

    def _add_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Папка с медиафайлами")
        if not folder:
            return
        EXTS = {".mp3",".wav",".ogg",".flac",".aac",".m4a",".opus",".wma",
                ".mp4",".avi",".mkv",".webm",".mov",".m4v"}
        for p in sorted(Path(folder).rglob("*")):
            if p.suffix.lower() in EXTS:
                self._add_track(str(p))

    def _add_track(self, path: str):
        if any(tr["path"] == path for tr in self._playlist):
            return
        info = self._read_tags(path)
        info["is_video"] = Path(path).suffix.lower() in {
            ".mp4",".avi",".mkv",".webm",".mov",".m4v",
            ".mpg",".mpeg",".wmv",".3gp",".ts",".m2ts"}
        self._playlist.append(info)
        n = len(self._playlist)

        r = self._queue_tbl.rowCount()
        self._queue_tbl.insertRow(r)
        self._queue_tbl.setItem(r, 0, QTableWidgetItem(
            ("🎬 " if info.get("is_video") else "") + str(n)))
        self._queue_tbl.setItem(r, 1, QTableWidgetItem(info["title"]))
        self._queue_tbl.setItem(r, 2, QTableWidgetItem(info["artist"]))
        self._queue_tbl.setItem(r, 3, QTableWidgetItem(self._fmt(info["dur"]*1000)))

        r2 = self._lib_tbl.rowCount()
        self._lib_tbl.insertRow(r2)
        self._lib_tbl.setItem(r2, 0, QTableWidgetItem(info["title"]))
        self._lib_tbl.setItem(r2, 1, QTableWidgetItem(info["artist"]))
        self._lib_tbl.setItem(r2, 2, QTableWidgetItem(info["album"]))
        self._lib_tbl.setItem(r2, 3, QTableWidgetItem(self._fmt(info["dur"]*1000)))

    @staticmethod
    def _read_tags(path: str) -> dict:
        p = Path(path)
        info = {"path": path, "title": p.stem,
                "artist": "Неизвестен", "album": "", "dur": 0}
        try:
            import mutagen
            f = mutagen.File(path, easy=True)
            if f:
                info["title"]  = str(f.get("title",  [p.stem])[0])
                info["artist"] = str(f.get("artist", ["Неизвестен"])[0])
                info["album"]  = str(f.get("album",  [""])[0])
                info["dur"]    = int(getattr(f, "info", None) and f.info.length or 0)
        except Exception:
            pass
        return info

    def _apply_eq(self):
        """Apply EQ via mpv IPC socket или через volume gain fallback."""
        if not hasattr(self, '_eq_sliders') or not self._eq_sliders:
            return
        vals = [s.value() for s in self._eq_sliders]
        try:
            import json as _j
            S().set("mewa_eq", _j.dumps(vals))
        except Exception: pass

        # Обновим label значений на слайдерах
        if hasattr(self, '_eq_val_labels'):
            for i, (v, lbl) in enumerate(zip(vals, self._eq_val_labels)):
                lbl.setText(f"{v:+d}" if v != 0 else "0")

        # === Реальное применение EQ ===
        # Способ 1: mpv через IPC socket (если радио играет через mpv)
        radio_proc = getattr(self, '_radio_proc', None)
        if radio_proc and radio_proc.poll() is None:
            try:
                import subprocess as _sp, os as _os, json as _j2
                # mpv IPC: отправляем af-команду
                bands = ["32","64","125","250","500","1000","2000","4000","8000","16000"]
                eq_str = ":".join(
                    f"{b}={vals[i] if i < len(vals) else 0}"
                    for i, b in enumerate(bands)
                )
                # Попробуем через stdin если mpv запущен с --input-ipc-server
                pass  # mpv IPC требует --input-ipc-server при запуске
            except Exception:
                pass

        # Способ 2: master volume adjustment (простой bass/treble gain)
        if self._has_player and self._audio_out:
            try:
                avg = sum(vals) / len(vals) if vals else 0
                # Средний гейн всех полос → корректируем общую громкость
                base_vol = S().get("volume", 80, t=int) / 100.0
                gain_factor = 1.0 + (avg / 24.0) * 0.3  # max ±30%
                new_vol = max(0.0, min(1.0, base_vol * gain_factor))
                self._audio_out.setVolume(new_vol)
            except Exception:
                pass

        # Способ 3: показываем уведомление что для полного EQ нужен mpv
        if hasattr(self, '_eq_status_lbl'):
            has_mpv = bool(__import__('shutil').which('mpv'))
            if has_mpv:
                self._eq_status_lbl.setText("ℹ Перезапусти трек для применения EQ")
                self._eq_status_lbl.setVisible(True)

    def _fetch_lyrics(self):
        """Fetch lyrics — lrclib.net (бесплатно, без ключа) с fallback на genius."""
        artist = getattr(self, '_ly_artist', None)
        title  = getattr(self, '_ly_title', None)
        a = artist.text().strip() if artist else ""
        t_str = title.text().strip() if title else ""

        # Автозаполнение из текущего трека
        if not a or not t_str:
            if self._queue_idx >= 0 and self._playlist:
                info = self._playlist[self._queue_idx]
                if not a:
                    a = info.get("artist","")
                    if artist: artist.setText(a)
                if not t_str:
                    t_str = info.get("title","")
                    if title: title.setText(t_str)

        if not a or not t_str:
            if hasattr(self, '_ly_text'):
                self._ly_text.setPlainText("Введите исполнителя и название песни")
            return

        if hasattr(self, '_ly_text'):
            self._ly_text.setPlainText(f"⏳ Ищем текст: {a} — {t_str}...")

        import threading, urllib.request, urllib.parse, json as _j

        def _set(text):
            QTimer.singleShot(0, lambda t=text:
                self._ly_text.setPlainText(t) if hasattr(self, '_ly_text') else None)

        def _fetch():
            # === Источник 1: lrclib.net (LRC + plain текст) ===
            try:
                params = urllib.parse.urlencode({
                    "artist_name": a, "track_name": t_str})
                url = f"https://lrclib.net/api/get?{params}"
                req = urllib.request.Request(url,
                    headers={"User-Agent": f"GoidaPhone/{S().get('version','1.8')}"})
                with urllib.request.urlopen(req, timeout=10) as resp:
                    data = _j.loads(resp.read().decode())
                # Предпочитаем синхронизированный текст (LRC), потом plainLyrics
                lrc  = data.get("syncedLyrics","") or ""
                plain = data.get("plainLyrics","") or ""
                if lrc:
                    # Убираем временные метки [mm:ss.xx]
                    import re as _re
                    clean = _re.sub(r'\[\d+:\d+\.\d+\]', '', lrc).strip()
                    _set(clean)
                    return
                if plain:
                    _set(plain)
                    return
            except Exception as e1:
                pass

            # === Источник 2: Musixmatch (без ключа, через unofficial) ===
            try:
                q = urllib.parse.quote(f"{a} {t_str}")
                url2 = f"https://api.musixmatch.com/ws/1.1/track.search?q={q}&apikey=&format=json"
                # Не работает без ключа — пропускаем
                pass
            except Exception:
                pass

            # === Источник 3: genius.com поиск (только ссылка) ===
            try:
                q = urllib.parse.quote(f"{a} {t_str}")
                genius_url = f"https://genius.com/search?q={q}"
                _set(f"\u274c Текст для «{a} — {t_str}» не найден.\n\nПоищи на Genius: https://genius.com/search?q={urllib.parse.quote(a+' '+t_str)}")
            except Exception as e3:
                _set(f"❌ Ошибка поиска: {e3}")

        threading.Thread(target=_fetch, daemon=True).start()

    def open_file(self, path: str):
        """Public API: add file to queue and play immediately.
        _play_idx will automatically switch to the video page for video files."""
        self._add_track(path)
        idx = len(self._playlist) - 1
        if idx >= 0:
            self._play_idx(idx)

    # ── Radio ─────────────────────────────────────────────────────────────────
    def _play_radio(self, item=None):
        """Play selected radio station."""
        if item is None:
            item = self._radio_list.currentItem()
        if not item: return
        url = item.data(Qt.ItemDataRole.UserRole)
        name = item.text()
        self._play_radio_url(url, name)

    def _play_radio_url(self, url: str = "", name: str = ""):
        if not url:
            inp = getattr(self, '_radio_url_input', None)
            if inp: url = inp.text().strip()
        if not url: return
        if not name: name = url

        self._stop_radio_proc()

        # Update UI
        self._play_btn.setText("⏸")
        try: self._np_bar.setText(f"📻 {name}")
        except Exception: pass
        try: self._np_bar_artist.setText("Online Radio")
        except Exception: pass
        try: self._np_title.setText(name)
        except Exception: pass
        try: self._np_artist.setText("Online Radio")
        except Exception: pass
        try: self._np_album.setText(url)
        except Exception: pass
        if hasattr(self, '_radio_now'):
            self._radio_now.setText(f"▶ {name}")
        self._show_section("radio")
        if hasattr(self, '_visualizer'):
            self._visualizer.set_playing(True)

        # Strategy 1: QMediaPlayer (built-in, no external deps)
        if self._has_player:
            try:
                from PyQt6.QtCore import QUrl as _QUrl
                from PyQt6.QtMultimedia import QMediaPlayer as _QMP
                self._player.stop()
                self._player.setSource(_QUrl(url))
                self._player.play()
                print(f"[radio] QMediaPlayer: {url}")

                def _check_qt_radio():
                    try:
                        st = self._player.mediaStatus()
                        ps = self._player.playbackState()
                        playing = (ps == _QMP.PlaybackState.PlayingState)
                        bad = (st in (_QMP.MediaStatus.InvalidMedia,
                                      _QMP.MediaStatus.NoMedia))
                        if not playing or bad:
                            print(f"[radio] QMediaPlayer failed → subprocess")
                            self._play_radio_subprocess(url, name)
                    except Exception:
                        self._play_radio_subprocess(url, name)

                QTimer.singleShot(4000, _check_qt_radio)
                return
            except Exception as e:
                print(f"[radio] QMediaPlayer error: {e}")

        # Strategy 2: subprocess fallback
        self._play_radio_subprocess(url, name)

    def _play_radio_subprocess(self, url: str, name: str):
        """Play radio via mpv/vlc/ffplay as subprocess fallback."""
        import subprocess as _sp
        self._radio_proc = None
        players = ["mpv", "vlc", "cvlc", "ffplay", "mplayer"]
        for player in players:
            try:
                args = {
                    "mpv":    [player,
                               "--no-video",
                               "--really-quiet",
                               "--no-terminal",
                               "--cache=yes",
                               "--demuxer-max-bytes=50MiB",
                               "--stream-buffer-size=512KiB",
                               "--hr-seek=no",
                               "--audio-stream-silence=no",
                               url],
                    "vlc":    [player, "--intf", "dummy", "--no-video",
                               "--no-video-title-show", "--quiet",
                               "--network-caching=3000",
                               "--http-reconnect", url],
                    "ffplay": [player, "-nodisp",
                               "-loglevel", "quiet",
                               "-vn",
                               "-fflags", "nobuffer",
                               "-flags", "low_delay",
                               url],
                    "mplayer":[player, "-really-quiet", "-vo", "null",
                               "-cache", "2048", "-cache-min", "10",
                               "-ao", "alsa", url],
                    "cvlc":   ["cvlc", "--no-video",
                               "--network-caching=3000", url],
                }.get(player, [player, url])
                self._radio_proc = _sp.Popen(
                    args, stdout=_sp.DEVNULL, stderr=_sp.DEVNULL)
                if hasattr(self, '_radio_now'):
                    self._radio_now.setText(f"▶ {name}  (via {player})")
                print(f"[radio] playing via {player}: {url}")
                # Запускаем мониторинг — авто-переподключение при обрыве
                QTimer.singleShot(1000, lambda: self._start_radio_monitor(url, name))
                # Обновляем визуализатор
                if hasattr(self, '_visualizer'):
                    self._visualizer.set_playing(True)
                return
            except FileNotFoundError:
                continue
        # Nothing worked
        if hasattr(self, '_radio_now'):
            self._radio_now.setText(
                "❌ Установи mpv или vlc для воспроизведения потока")
        self._play_btn.setText("▶")
        print("[radio] no player available: install mpv or vlc")

    def _stop_radio_proc(self):
        # Выключаем визуализатор
        if hasattr(self, '_visualizer'):
            self._visualizer.set_playing(False)
        # Останавливаем таймер мониторинга
        timer = getattr(self, '_radio_monitor_timer', None)
        if timer:
            timer.stop()
            self._radio_monitor_timer = None
        proc = getattr(self, '_radio_proc', None)
        if proc:
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except Exception:
                try: proc.kill()
                except Exception: pass
            self._radio_proc = None
        self._radio_url_playing  = ""
        self._radio_name_playing = ""

    def _start_radio_monitor(self, url: str, name: str):
        """Мониторим процесс mpv — если упал, перезапускаем."""
        self._radio_url_playing  = url
        self._radio_name_playing = name
        self._radio_restart_count = 0
        self._radio_paused = False  # сбрасываем флаг паузы

        timer = QTimer(self)
        timer.setInterval(3000)

        def _check():
            proc = getattr(self, '_radio_proc', None)
            if proc is None:
                timer.stop()
                return
            if proc.poll() is not None:  # процесс завершился
                # Не перезапускаем если пользователь поставил на паузу
                if getattr(self, '_radio_paused', False):
                    timer.stop()
                    return
                self._radio_restart_count = getattr(self, '_radio_restart_count', 0) + 1
                if self._radio_restart_count <= 5:  # максимум 5 попыток
                    url_  = getattr(self, '_radio_url_playing', '')
                    name_ = getattr(self, '_radio_name_playing', '')
                    if url_:
                        self._play_radio_subprocess(url_, name_)
                else:
                    timer.stop()
                    if hasattr(self, '_radio_now'):
                        self._radio_now.setText("❌ Соединение потеряно")

        timer.timeout.connect(_check)
        timer.start()
        self._radio_monitor_timer = timer

    def _stop_radio(self):
        self._stop_radio_proc()
        if self._has_player:
            try: self._player.stop()
            except Exception: pass
        self._play_btn.setText("▶")
        if hasattr(self, '_radio_now'):
            self._radio_now.setText("Нет трансляции")
        if hasattr(self, '_np_bar'):
            try: self._np_bar.setText("No track")
            except Exception: pass

    def _clear_queue(self):
        if QMessageBox.question(self, "Clear queue", "Очистить весь список?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                != QMessageBox.StandardButton.Yes:
            return
        self._playlist.clear()
        self._queue_tbl.setRowCount(0)
        self._lib_tbl.setRowCount(0)
        self._queue_idx = -1
        self._np_bar.setText("No track")
        self._np_title.setText("No track")
        self._np_artist.setText("")
        self._np_album.setText("")
        self._art_lbl.setText("♫")
        self._art_lbl.setPixmap(QPixmap())
        if self._has_player:
            self._player.stop()
        self._play_btn.setText("▶")

    def _filter_lib(self, q: str):
        q = q.lower()
        for row in range(self._lib_tbl.rowCount()):
            match = any(
                q in (self._lib_tbl.item(row,c).text().lower()
                       if self._lib_tbl.item(row,c) else "")
                for c in range(4))
            self._lib_tbl.setRowHidden(row, q != "" and not match)

    # ── playback ─────────────────────────────────────────────────────────────
    _VIDEO_EXTS = {".mp4",".avi",".mkv",".webm",".mov",".m4v",
                    ".mpg",".mpeg",".wmv",".3gp",".ts",".m2ts"}

    def _on_audio_device_changed(self):
        """Системное аудио-устройство изменилось — переподключаем вывод."""
        if not self._has_player or not self._player:
            return
        try:
            from PyQt6.QtMultimedia import QAudioOutput, QMediaDevices
            vol = self._audio_out.volume() if self._audio_out else 0.85
            state = self._player.playbackState()
            pos   = self._player.position()
            # Пересоздаём QAudioOutput с новым дефолтным устройством
            new_out = QAudioOutput()
            new_out.setVolume(vol)
            self._player.setAudioOutput(new_out)
            self._audio_out = new_out
            # Восстанавливаем позицию если играло
            from PyQt6.QtMultimedia import QMediaPlayer as _QMP
            if state == _QMP.PlaybackState.PlayingState:
                self._player.setPosition(pos)
                self._player.play()
        except Exception:
            pass

    def _play_idx(self, idx: int):
        if not self._has_player or idx < 0 or idx >= len(self._playlist):
            return
        self._queue_idx = idx
        info = self._playlist[idx]
        from PyQt6.QtCore import QUrl
        self._player.setSource(QUrl.fromLocalFile(info["path"]))
        self._player.play()
        self._play_btn.setText("⏸")
        self._np_bar.setText(info["title"])
        try: self._np_bar_artist.setText(info["artist"])
        except Exception: pass
        self._np_title.setText(info["title"])
        self._np_artist.setText(info["artist"])
        self._np_album.setText(info["album"])
        self._queue_tbl.selectRow(idx)
        self._lib_tbl.selectRow(idx)
        ext = Path(info["path"]).suffix.lower()
        if ext in self._VIDEO_EXTS:
            self._show_section("video")
            if hasattr(self, '_video_title_lbl'):
                self._video_title_lbl.setText(
                    info["title"] or Path(info["path"]).name)
            try: self._art_lbl.setPixmap(QPixmap()); self._art_lbl.setText("🎬")
            except Exception: pass
        else:
            self._load_art(info["path"])
            # Auto-populate lyrics fields
            if hasattr(self, '_ly_artist') and self._ly_artist:
                self._ly_artist.setText(info.get("artist",""))
            if hasattr(self, '_ly_title') and self._ly_title:
                self._ly_title.setText(info.get("title",""))

    def _load_art(self, path: str):
        t = get_theme(S().theme)
        pm = None
        try:
            import mutagen.id3
            tags = mutagen.id3.ID3(path)
            for k in tags:
                if k.startswith("APIC"):
                    pm = QPixmap()
                    pm.loadFromData(tags[k].data)
                    break
        except Exception:
            pass
        if pm and not pm.isNull():
            pm = pm.scaled(186, 186, Qt.AspectRatioMode.KeepAspectRatio,
                           Qt.TransformationMode.SmoothTransformation)
            self._art_lbl.setPixmap(pm)
            self._art_lbl.setText("")
        else:
            self._art_lbl.setPixmap(QPixmap())
            self._art_lbl.setText("♫")

    def _toggle_video_fullscreen(self):
        vw = getattr(self, '_video_widget', None)
        if vw is None: return
        going_fs = not vw.isFullScreen()
        vw.setFullScreen(going_fs)
        if going_fs:
            # Install key handler so ESC exits fullscreen
            def _fs_key(event, _vw=vw):
                if event.type() == event.Type.KeyPress:
                    if event.key() in (Qt.Key.Key_Escape, Qt.Key.Key_F, Qt.Key.Key_F11):
                        _vw.setFullScreen(False)
                        return True
                return False
            vw._esc_filter = _FSKeyFilter(vw, _fs_key)
            vw.installEventFilter(vw._esc_filter)
        else:
            ef = getattr(vw, '_esc_filter', None)
            if ef: vw.removeEventFilter(ef)

    def _toggle_play(self):
        # Радио играет через mpv subprocess — отдельная логика
        radio_proc = getattr(self, '_radio_proc', None)
        if radio_proc and radio_proc.poll() is None:
            # Радио активно — пауза = полная остановка потока
            if getattr(self, '_radio_paused', False):
                # Возобновляем
                url  = getattr(self, '_radio_url_playing', '')
                name = getattr(self, '_radio_name_playing', '')
                if url:
                    self._radio_paused = False
                    self._play_btn.setText("⏸")
                    self._play_radio_subprocess(url, name)
            else:
                # Останавливаем mpv
                self._radio_paused = True
                self._stop_radio_proc()
                self._play_btn.setText("▶")
                if hasattr(self, '_radio_now'):
                    self._radio_now.setText(
                        f"⏸ {getattr(self,'_radio_name_playing','')}")
            return

        # Обычный плеер
        if not self._has_player:
            return
        from PyQt6.QtMultimedia import QMediaPlayer as QMP
        st = self._player.playbackState()
        if st == QMP.PlaybackState.PlayingState:
            self._player.pause()
            self._play_btn.setText("▶")
        elif self._queue_idx >= 0:
            self._player.play()
            self._play_btn.setText("⏸")
        elif self._playlist:
            self._play_idx(0)

    def _prev(self):
        if not self._playlist: return
        import random
        self._play_idx(random.randint(0,len(self._playlist)-1) if self._shuffle
                       else (self._queue_idx-1)%len(self._playlist))

    def _next(self):
        if not self._playlist: return
        import random
        self._play_idx(random.randint(0,len(self._playlist)-1) if self._shuffle
                       else (self._queue_idx+1)%len(self._playlist))

    def _skip(self, sec: int):
        if not self._has_player: return
        self._player.setPosition(
            max(0, min(self._player.position()+sec*1000, self._player.duration())))

    def _seek(self, val: int):
        if not self._has_player: return
        dur = self._player.duration()
        if dur > 0:
            self._player.setPosition(int(val/1000*dur))

    def _set_vol(self, v: int):
        if not self._has_player: return
        self._audio_out.setVolume(v/100.0)

    def _cycle_repeat(self, checked: bool):
        self._repeat = "all" if checked else "off"
        self._rep_btn.setToolTip(f"Повтор: {'все' if checked else 'выкл'}")

    # ── signals ───────────────────────────────────────────────────────────────
    def _on_pos(self, ms: int):
        dur = self._player.duration() if self._has_player else 0
        if dur > 0:
            self._prog.setValue(int(ms/dur*1000))
        self._t_cur.setText(self._fmt(ms))

    def _on_dur(self, ms: int):
        self._t_tot.setText(self._fmt(ms))

    def _on_state(self, state):
        from PyQt6.QtMultimedia import QMediaPlayer as QMP
        is_playing = (state == QMP.PlaybackState.PlayingState)
        self._play_btn.setText("⏸" if is_playing else "▶")
        # Update visualizers
        if hasattr(self, '_visualizer'):
            self._visualizer.set_playing(is_playing)
        if hasattr(self, '_viz_strip'):
            self._viz_strip.set_playing(is_playing)

    def _on_status(self, status):
        from PyQt6.QtMultimedia import QMediaPlayer as QMP
        if status == QMP.MediaStatus.EndOfMedia:
            if self._repeat == "one":
                self._player.setPosition(0); self._player.play()
            else:
                self._next()

    @staticmethod
    def _fmt(ms: int) -> str:
        s = max(0, ms)//1000
        return f"{s//60}:{s%60:02d}"

    # ── context menu ─────────────────────────────────────────────────────────
    def _queue_ctx(self, pos):
        item = self._queue_tbl.itemAt(pos)
        if not item: return
        row = self._queue_tbl.row(item)
        menu = QMenu(self)
        menu.addAction(TR("btn_play"),   lambda: self._play_idx(row))
        menu.addAction("🗑 Убрать из очереди", lambda: self._remove_row(row))
        menu.addAction("📁 Открыть в менеджере",
            lambda: self._open_fm(self._playlist[row]["path"]))
        menu.exec(self._queue_tbl.mapToGlobal(pos))

    def _remove_row(self, row: int):
        if 0 <= row < len(self._playlist):
            self._playlist.pop(row)
        self._queue_tbl.removeRow(row)
        self._lib_tbl.removeRow(row)
        if row == self._queue_idx:
            self._queue_idx = -1

    @staticmethod
    def _open_fm(path: str):
        import subprocess, platform as pl
        folder = str(Path(path).parent)
        try:
            subprocess.Popen(
                ["explorer" if pl.system()=="Windows" else "xdg-open", folder])
        except Exception: pass

    # ── playlists ─────────────────────────────────────────────────────────────
    def _save_playlist(self):
        name, ok = QInputDialog.getText(self,"Сохранить плейлист",_L("Название:", "Name:", "名前:"))
        if not ok or not name.strip(): return
        import json as _json
        pls = _json.loads(S().get("mewa_playlists","[]",t=str))
        pls.append({"name":name.strip(),"tracks":[x["path"] for x in self._playlist]})
        S().set("mewa_playlists", _json.dumps(pls, ensure_ascii=False))
        self._refresh_saved_playlists()

    def _delete_saved_playlist(self):
        row = self._pl_list.currentRow()
        if row < 0: return
        import json as _json
        pls = _json.loads(S().get("mewa_playlists","[]",t=str))
        if row < len(pls):
            pls.pop(row)
            S().set("mewa_playlists", _json.dumps(pls, ensure_ascii=False))
        self._refresh_saved_playlists()

    def _load_saved_playlist(self, idx):
        import json as _json
        row = idx.row()
        pls = _json.loads(S().get("mewa_playlists","[]",t=str))
        if row < len(pls):
            self._clear_queue()
            for path in pls[row].get("tracks",[]):
                if Path(path).exists():
                    self._add_track(path)
            self._show_section("queue")

    def _refresh_saved_playlists(self):
        try:
            import json as _json
            self._pl_list.clear()
            pls = _json.loads(S().get("mewa_playlists","[]",t=str))
            for pl in pls:
                self._pl_list.addItem(
                    f"≡  {pl.get('name','?')}   ({len(pl.get('tracks',[]))} треков)")
        except Exception:
            pass


class PinLockScreen(QWidget):
    """Full-screen PIN lock. 6 digits, hint, forgot-password wipe."""
    unlocked = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground)
        self.setWindowFlags(Qt.WindowType.Widget)
        self._attempt_count = 0
        self._pin_entry = ""
        self._setup()

    def _setup(self):
        t = get_theme(S().theme)
        outer = QVBoxLayout(self)
        outer.setAlignment(Qt.AlignmentFlag.AlignCenter)
        box = QWidget()
        box.setFixedWidth(320)
        box.setStyleSheet(
            f"QWidget{{background:{t['bg2']};border-radius:18px;"
            f"border:1px solid {t['border']};}}"
            f"QLabel{{border:none;}}")
        lay = QVBoxLayout(box)
        lay.setContentsMargins(30,30,30,30)
        lay.setSpacing(14)
        lock_lbl = QLabel("\U0001F512")
        lock_lbl.setStyleSheet("font-size:36px;background:transparent;")
        lock_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(lock_lbl)
        app_lbl = QLabel(APP_NAME)
        app_lbl.setStyleSheet(
            f"font-size:17px;font-weight:bold;color:{t['accent']};")
        app_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(app_lbl)
        hint_text = S().get("pin_hint","",t=str)
        if hint_text:
            hint_lbl = QLabel(f"\u041f\u043e\u0434\u0441\u043a\u0430\u0437\u043a\u0430: {hint_text}")
            hint_lbl.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
            hint_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lay.addWidget(hint_lbl)
        self._dots_lbl = QLabel("\u25cb  \u25cb  \u25cb  \u25cb  \u25cb  \u25cb")
        self._dots_lbl.setStyleSheet(f"font-size:20px;color:{t['accent']};background:transparent;")
        self._dots_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(self._dots_lbl)
        self._err_lbl = QLabel("")
        self._err_lbl.setStyleSheet("color:#FF6060;font-size:11px;background:transparent;")
        self._err_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(self._err_lbl)
        numpad = QGridLayout()
        numpad.setSpacing(8)
        keys = [1,2,3,4,5,6,7,8,9,None,0,"\u232b"]
        for i, n in enumerate(keys):
            if n is None:
                _sp = QWidget()
                _sp.setFixedSize(70,50)
                _sp.setStyleSheet("background:transparent;border:none;")
                numpad.addWidget(_sp, i//3, i%3)
                continue
            btn = QPushButton(str(n))
            btn.setFixedSize(70,50)
            btn.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};")
            btn.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['text']};"  
                "border-radius:10px;font-size:17px;font-weight:bold;"
                f"border:1px solid {t['border']};"  
                "border-bottom:3px solid rgba(0,0,0,89);}}"  
                f"QPushButton:hover{{background:{t['btn_hover']};}}"  
                f"QPushButton:pressed{{padding-top:3px;"  
                "border-bottom:1px solid rgba(0,0,0,51);}}")
            btn.clicked.connect(lambda _, v=n: self._on_key(v))
            numpad.addWidget(btn, i//3, i%3)
        lay.addLayout(numpad)
        forgot_btn = QPushButton("\u0417\u0430\u0431\u044b\u043b PIN")
        forgot_btn.setFlat(True)
        forgot_btn.setStyleSheet(
            f"color:{t['text_dim']};font-size:10px;background:transparent;"  
            "text-decoration:underline;border:none;")
        forgot_btn.clicked.connect(self._forgot_pin)
        lay.addWidget(forgot_btn, alignment=Qt.AlignmentFlag.AlignCenter)
        outer.addWidget(box)

    def _update_dots(self):
        filled = "\u25cf" * len(self._pin_entry)
        empty  = "\u25cb" * (6 - len(self._pin_entry))
        self._dots_lbl.setText("  ".join(list(filled + empty)))

    def _on_key(self, val):
        if val == "\u232b":
            self._pin_entry = self._pin_entry[:-1]
            self._update_dots()
            return
        if len(self._pin_entry) < 6:
            self._pin_entry += str(val)
        self._update_dots()
        # Check at 4, 5, 6 digits — supports short PINs
        if len(self._pin_entry) >= 4:
            import hashlib as _hl
            stored = S().get("pin_hash","",t=str)
            if stored and _hl.sha256(self._pin_entry.encode()).hexdigest() == stored:
                self._check_pin()
                return
        if len(self._pin_entry) == 6:
            self._check_pin()

    def _check_pin(self):
        import hashlib
        stored = S().get("pin_hash","",t=str)
        if hashlib.sha256(self._pin_entry.encode()).hexdigest() == stored:
            self._err_lbl.setText("")
            self.unlocked.emit()
        else:
            self._attempt_count += 1
            self._err_lbl.setText(
                f"\u041d\u0435\u0432\u0435\u0440\u043d\u044b\u0439 PIN ({self._attempt_count})"
            )
            AnimationHelper.shake(self)
            self._pin_entry = ""
            self._update_dots()

    def _forgot_pin(self):
        reply = QMessageBox.warning(
            self,
            "\u0417\u0430\u0431\u044b\u043b\u0438 PIN?",
            "\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c?\n"
            "\u0412\u0421\u0415 \u0434\u0430\u043d\u043d\u044b\u0435 "
            "\u0431\u0443\u0434\u0443\u0442 \u0443\u0434\u0430\u043b\u0435\u043d\u044b.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._wipe_data()

    def _wipe_data(self):
        import shutil
        # 1. Wipe ALL QSettings keys
        cfg = S()
        cfg._s.clear()
        cfg._s.sync()
        # 2. Delete data directory (history, avatars, received files, groups, contacts)
        try:
            if DATA_DIR.exists():
                shutil.rmtree(str(DATA_DIR))
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            AVATARS_DIR.mkdir(exist_ok=True)
            RECEIVED_DIR.mkdir(exist_ok=True)
            HISTORY_DIR.mkdir(exist_ok=True)
        except Exception as e:
            print(f"[wipe] {e}")
        # 3. Clear in-memory caches
        try:
            HISTORY._cache.clear()
        except Exception:
            pass
        QMessageBox.information(self, "Reset", "Данные удалены. Приложение закроется.")
        QApplication.quit()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self.parent():
            self.resize(self.parent().size())
