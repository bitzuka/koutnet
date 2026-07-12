#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Base UI widgets (launcher, splash, hover cards)
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings
from gdf_network  import *      # S, AppSettings

# ═══════════════════════════════════════════════════════════════════════════
#  SPLASH SCREEN
# ═══════════════════════════════════════════════════════════════════════════
class SplashScreen(QWidget):
    """
    Startup splash screen.
    - Adapts to current theme colours
    - Supports custom background image (set in Settings → Кастомизация)
    - Animated progress dots
    """
    def __init__(self):
        super().__init__()
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint |
                            Qt.WindowType.WindowStaysOnTopHint |
                            Qt.WindowType.SplashScreen)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)

        self._dot_count = 0
        self._dot_timer = QTimer(self)
        self._dot_timer.timeout.connect(self._tick_dots)
        self._dot_timer.start(400)

        # Load image — custom or default
        self._bg_pixmap: QPixmap | None = None

        # Сплеш: сначала из настроек (явный выбор пользователя)
        bg_b64 = S().get("splash_image_b64", "", t=str)
        if bg_b64:
            try:
                pm2 = QPixmap()
                pm2.loadFromData(base64.b64decode(bg_b64))
                if not pm2.isNull():
                    self._bg_pixmap = pm2.scaled(
                        616, 338,
                        Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                        Qt.TransformationMode.SmoothTransformation)
            except Exception:
                pass
        # Default: splashq.jpg next to gdf.py
        if self._bg_pixmap is None:
            _sp = Path(__file__).parent / "splashq.jpg"
            if not _sp.exists():
                _sp = Path(__file__).parent / "splashq.png"
            if _sp.exists():
                pm = QPixmap(str(_sp))
                if not pm.isNull():
                    self._bg_pixmap = pm.scaled(
                        616, 338,
                        Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                        Qt.TransformationMode.SmoothTransformation)

        # Fixed size 616×338
        self.setFixedSize(616, 338)
        sg = QApplication.primaryScreen().geometry()
        self.move((sg.width() - 616) // 2, (sg.height() - 338) // 2)

        self._build()

    def _build(self):
        t = get_theme(S().theme)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        container = QWidget()
        container.setObjectName("splash_container")
        if self._bg_pixmap:
            # Image splash: fully transparent container — image drawn in paintEvent
            container.setStyleSheet(
                "#splash_container{background:transparent;border-radius:18px;}")
        else:
            container.setStyleSheet(f"""
                #splash_container {{
                    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                        stop:0 {t['bg3']}, stop:0.4 {t['bg2']}, stop:1 {t['bg']});
                    border-radius: 18px;
                    border: 2px solid {t['accent']};
                }}
            """)

        cl = QVBoxLayout(container)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)
        cl.addStretch()

        if not self._bg_pixmap:
            # No image: show icon+title+version
            icon_lbl = QLabel("📱")
            icon_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            icon_lbl.setStyleSheet("font-size:56px;background:transparent;")
            cl.addWidget(icon_lbl)
            title_lbl = QLabel(APP_NAME)
            title_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            title_lbl.setStyleSheet(f"color:{t['text']};font-size:30px;font-weight:bold;"
                "background:transparent;letter-spacing:3px;")
            cl.addWidget(title_lbl)
            ver_lbl = QLabel(f"v{APP_VERSION}  •  {COMPANY_NAME}")
            ver_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            ver_lbl.setStyleSheet(
                f"color:{t['text_dim']};font-size:12px;background:transparent;")
            cl.addWidget(ver_lbl)
            cl.addSpacing(16)

        cl.addStretch()

        # Loading dots — always shown, at bottom with semi-transparent bg if image
        bottom_bar = QWidget()
        bb_lay = QHBoxLayout(bottom_bar)
        bb_lay.setContentsMargins(0,0,0,0)
        if self._bg_pixmap:
            bottom_bar.setStyleSheet(
                "background:rgba(0,0,0,140);border-radius:0 0 16px 16px;padding:6px;")
        else:
            bottom_bar.setStyleSheet("background:transparent;")
        self._progress_lbl = QLabel(TR("splash_loading"))
        self._progress_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._progress_lbl.setStyleSheet(
            f"color:{'white' if self._bg_pixmap else t['accent']};"
            "font-size:11px;background:transparent;")
        bb_lay.addWidget(self._progress_lbl)
        cl.addWidget(bottom_bar)

        layout.addWidget(container)

    def _tick_dots(self):
        self._dot_count = (self._dot_count + 1) % 4
        dots = "." * self._dot_count
        self._progress_lbl.setText(f"Loading{dots}")

    def paintEvent(self, event):
        if self._bg_pixmap:
            painter = QPainter(self)
            painter.setRenderHint(QPainter.RenderHint.Antialiasing)
            from PyQt6.QtGui import QPainterPath
            path = QPainterPath()
            path.addRoundedRect(0, 0, self.width(), self.height(), 18, 18)
            painter.setClipPath(path)
            # Scale image to fill exactly, no dark overlay
            scaled = self._bg_pixmap.scaled(self.width(), self.height(),
                Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                Qt.TransformationMode.SmoothTransformation)
            x = (self.width() - scaled.width()) // 2
            y = (self.height() - scaled.height()) // 2
            painter.drawPixmap(x, y, scaled)
        super().paintEvent(event)

# ═══════════════════════════════════════════════════════════════════════════
#  CUSTOM LICENCE INPUT  (auto-insert dashes: XXXX-XXXX-XXXX)
# ═══════════════════════════════════════════════════════════════════════════
class LicenseLineEdit(QLineEdit):
    """Auto-formats 12-digit license as XXXX-XXXX-XXXX while typing."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setPlaceholderText("XXXX-XXXX-XXXX")
        self.setMaxLength(14)   # 12 digits + 2 dashes
        self.textEdited.connect(self._format)
        self._updating = False

    def raw_digits(self) -> str:
        return re.sub(r'\D', '', self.text())

    def _format(self, text: str):
        if self._updating:
            return
        self._updating = True
        digits = re.sub(r'\D', '', text)[:12]
        formatted = '-'.join(digits[i:i+4] for i in range(0, len(digits), 4) if digits[i:i+4])
        self.setText(formatted)
        self.setCursorPosition(len(formatted))
        self._updating = False

    def is_complete(self) -> bool:
        return len(self.raw_digits()) == 12

# ═══════════════════════════════════════════════════════════════════════════
#  USER TOOLTIP / HOVER CARD
# ═══════════════════════════════════════════════════════════════════════════
class UserHoverCard(QFrame):
    """Small floating card shown on hover over a user in the list."""

    def __init__(self, peer: dict, parent=None):
        super().__init__(parent, Qt.WindowType.ToolTip)
        self.setFrameShape(QFrame.Shape.StyledPanel)
        t = get_theme(S().theme)
        self.setStyleSheet(f"""
            QFrame {{
                background-color: {t['bg2']};
                border: 1px solid {t['accent']};
                border-radius: 8px;
                padding: 4px;
            }}
            QLabel {{ background: transparent; color: {t['text']}; }}
        """)
        self._build(peer)

    def _build(self, p: dict):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(12,10,12,10)
        lay.setSpacing(4)

        # Avatar
        av_b64 = p.get("avatar_b64","")
        if av_b64:
            try:
                pm = base64_to_pixmap(av_b64)
                circ = make_circle_pixmap(pm, 48)
            except Exception:
                circ = default_avatar(p.get("username","?"), 48)
        else:
            circ = default_avatar(p.get("username","?"), 48)

        top = QHBoxLayout()
        av = QLabel(); av.setPixmap(circ); av.setFixedSize(48,48)
        top.addWidget(av)
        info = QVBoxLayout()
        color = p.get("nickname_color","#E0E0E0")
        emoji = p.get("custom_emoji","")
        name = QLabel(f"<b style='color:{color}'>{p.get('username','?')} {emoji}</b>")
        name.setTextFormat(Qt.TextFormat.RichText)
        info.addWidget(name)
        info.addWidget(QLabel(f"🌐 {p.get('ip','?')}  •  {p.get('conn_type','?')}"))
        top.addLayout(info)
        lay.addLayout(top)

        bio = p.get("bio","")
        if bio:
            bio_lbl = QLabel(bio[:80])
            bio_lbl.setWordWrap(True)
            bio_lbl.setStyleSheet(f"color: #9090A0; font-size: 10px; background: transparent;")
            lay.addWidget(bio_lbl)

        t = get_theme(S().theme)
        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"background:{t['border']};min-height:1px;max-height:1px;border:none;")
        lay.addWidget(sep)

        lay.addWidget(QLabel(f"💻 {p.get('os','?')}"))
        lay.addWidget(QLabel(f"📦 GoidaPhone v{p.get('version','?')}"))
        ts = p.get("last_seen", 0)
        if ts:
            dt = datetime.fromtimestamp(ts).strftime("%H:%M:%S")
            lay.addWidget(QLabel(f"{TR("online_since_lbl")} {dt}"))
        if p.get("premium"):
            lay.addWidget(QLabel(TR("premium_user")))

        self.adjustSize()

# ═══════════════════════════════════════════════════════════════════════════
#  ANIMATION HELPER  (п.2 — анимации интерфейса)
# ═══════════════════════════════════════════════════════════════════════════
class AnimationHelper:
    """
    Helper for common UI animations.
    Uses QPropertyAnimation — no external deps.

    Animations used in GoidaPhone:
    - fade_in(widget)         — плавное появление виджета
    - fade_out(widget, cb)    — плавное исчезновение → callback
    - slide_in(widget, dir)   — выезд сбоку/снизу
    - pulse(widget)           — мигание (входящий звонок)
    - shake(widget)           — дрожание (неверный код лицензии)
    - bounce_button(btn)      — лёгкий bounce при клике
    """

    @staticmethod
    def shake(widget: QWidget, distance: int = 10, count: int = 3):
        """Shake widget horizontally (wrong PIN, error)."""
        anim = QPropertyAnimation(widget, b"pos", widget)
        anim.setDuration(300)
        orig = widget.pos()
        kf = [(0,orig), (0.1,orig+QPoint(-distance,0)),
              (0.25,orig+QPoint(distance,0)), (0.4,orig+QPoint(-distance,0)),
              (0.55,orig+QPoint(distance//2,0)), (0.7,orig+QPoint(-distance//2,0)),
              (0.85,orig+QPoint(distance//4,0)), (1.0,orig)]
        for t_val, pos in kf:
            anim.setKeyValueAt(t_val, pos)
        anim.setEasingCurve(QEasingCurve.Type.Linear)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return anim

    @staticmethod
    def fade_in(widget: QWidget, duration: int = 300,
                start: float = 0.0, end: float = 1.0):
        """Fade a widget in. Widget must have a graphics effect or support opacity."""
        from PyQt6.QtWidgets import QGraphicsOpacityEffect
        effect = QGraphicsOpacityEffect(widget)
        widget.setGraphicsEffect(effect)
        anim = QPropertyAnimation(effect, b"opacity", widget)
        anim.setDuration(duration)
        anim.setStartValue(start)
        anim.setEndValue(end)
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return anim

    @staticmethod
    def fade_out(widget: QWidget, duration: int = 250,
                 callback=None):
        """Fade a widget out, then call callback (e.g. widget.hide())."""
        from PyQt6.QtWidgets import QGraphicsOpacityEffect
        effect = QGraphicsOpacityEffect(widget)
        widget.setGraphicsEffect(effect)
        anim = QPropertyAnimation(effect, b"opacity", widget)
        anim.setDuration(duration)
        anim.setStartValue(1.0)
        anim.setEndValue(0.0)
        anim.setEasingCurve(QEasingCurve.Type.InCubic)
        if callback:
            anim.finished.connect(callback)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return anim

    @staticmethod
    def slide_in_from_right(widget: QWidget, duration: int = 350):
        """Slide widget in from the right."""
        start_x = widget.x() + widget.width()
        end_x   = widget.x()
        anim = QPropertyAnimation(widget, b"pos", widget)
        anim.setDuration(duration)
        anim.setStartValue(QPoint(start_x, widget.y()))
        anim.setEndValue(QPoint(end_x, widget.y()))
        anim.setEasingCurve(QEasingCurve.Type.OutBack)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return anim

    @staticmethod
    def slide_in_from_bottom(widget: QWidget, duration: int = 300):
        """Slide widget up from the bottom."""
        start_y = widget.y() + widget.height()
        end_y   = widget.y()
        anim = QPropertyAnimation(widget, b"pos", widget)
        anim.setDuration(duration)
        anim.setStartValue(QPoint(widget.x(), start_y))
        anim.setEndValue(QPoint(widget.x(), end_y))
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return anim

    @staticmethod
    def pulse(widget: QWidget, count: int = 3,
              duration_per: int = 400, callback=None):
        """
        Pulse (opacity oscillate) a widget N times.
        Used for incoming call notification.
        """
        from PyQt6.QtWidgets import QGraphicsOpacityEffect
        effect = QGraphicsOpacityEffect(widget)
        widget.setGraphicsEffect(effect)

        group = QSequentialAnimationGroup(widget)
        for _ in range(count):
            a1 = QPropertyAnimation(effect, b"opacity")
            a1.setDuration(duration_per // 2)
            a1.setStartValue(1.0)
            a1.setEndValue(0.3)
            a1.setEasingCurve(QEasingCurve.Type.InOutSine)
            a2 = QPropertyAnimation(effect, b"opacity")
            a2.setDuration(duration_per // 2)
            a2.setStartValue(0.3)
            a2.setEndValue(1.0)
            a2.setEasingCurve(QEasingCurve.Type.InOutSine)
            group.addAnimation(a1)
            group.addAnimation(a2)

        if callback:
            group.finished.connect(callback)
        group.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return group

    @staticmethod
    def shake(widget: QWidget, amplitude: int = 8, duration: int = 400):
        """
        Horizontal shake animation — for invalid license code etc.
        """
        orig = widget.pos()
        group = QSequentialAnimationGroup(widget)
        steps = [amplitude, -amplitude, amplitude//2, -amplitude//2, 0]
        step_dur = duration // len(steps)
        for dx in steps:
            a = QPropertyAnimation(widget, b"pos")
            a.setDuration(step_dur)
            a.setEndValue(QPoint(orig.x() + dx, orig.y()))
            a.setEasingCurve(QEasingCurve.Type.InOutQuad)
            group.addAnimation(a)
        group.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return group

    @staticmethod
    def bounce_button(btn: QPushButton, scale: float = 0.92):
        """
        Quick scale-down + restore on button press.
        Simulates a physical press feel.
        """
        # Qt doesn't natively support scale animations on widgets,
        # so we simulate with geometry shrink/expand.
        rect = btn.geometry()
        dx = int(rect.width()  * (1 - scale) / 2)
        dy = int(rect.height() * (1 - scale) / 2)
        small = rect.adjusted(dx, dy, -dx, -dy)

        a1 = QPropertyAnimation(btn, b"geometry", btn)
        a1.setDuration(80)
        a1.setStartValue(rect)
        a1.setEndValue(small)
        a1.setEasingCurve(QEasingCurve.Type.InQuad)

        a2 = QPropertyAnimation(btn, b"geometry", btn)
        a2.setDuration(120)
        a2.setStartValue(small)
        a2.setEndValue(rect)
        a2.setEasingCurve(QEasingCurve.Type.OutBack)

        group = QSequentialAnimationGroup(btn)
        group.addAnimation(a1)
        group.addAnimation(a2)
        group.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        return group


ANIM = AnimationHelper()   # global shorthand


class TextFormatter:
    """
    Converts markdown-like syntax to HTML for chat display.
    Supported:
      **text**  → <b>text</b>
      *text*    → <i>text</i>
      `text`    → <code style='...'>text</code>
      ~~text~~  → <s>text</s>
      ||text||  → spoiler (blurred, click to reveal) — rendered as title attr
      @username → highlighted mention
      http(s):// → clickable link
    """
    # Regex patterns (applied in order)
    _BOLD    = re.compile(r'\*\*(.+?)\*\*')
    _ITALIC  = re.compile(r'(?<!\*)\*(?!\*)(.+?)(?<!\*)\*(?!\*)')
    _CODE    = re.compile(r'`(.+?)`')
    _STRIKE  = re.compile(r'~~(.+?)~~')
    _SPOILER = re.compile(r'\|\|(.+?)\|\|')
    _MENTION = re.compile(r'@(\w+)')
    _URL     = re.compile(
        r'('
        r'https?://[^\s<>"\']+(?<![.,;:!?\)])'      # http:// or https:// links
        r'|(?<![\w@./])(?:www\.[\w\-]+\.[\w\-]{2,})'  # www.example.com
        r'|(?<![\w@./])'                              # bare domain: must not follow word char
        r'(?:[a-zA-Z0-9](?:[a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?\.)'  # domain label(s)
        r'(?:com|net|org|io|ru|dev|app|me|co|uk|de|fr|jp|cn|tv|gg|ai|xyz|fun|pro|club)'
        r'(?:/[^\s<>"\']*(?<![.,;:!?\)]))?'          # optional path
        r')'
    )

    @classmethod
    def format(cls, text: str, accent_color: str = "#4080FF",
               known_users: set | None = None) -> str:
        """Convert plain text with markdown to safe HTML."""
        # 1. Escape HTML special chars
        safe = (text.replace("&", "&amp;")
                    .replace("<", "&lt;")
                    .replace(">", "&gt;"))

        # 2. Apply markdown
        safe = cls._BOLD.sub(r'<b>\1</b>', safe)
        safe = cls._ITALIC.sub(r'<i>\1</i>', safe)
        safe = cls._CODE.sub(
            r'<code style="background:#1A1A2E;color:#80FF80;'
            r'padding:1px 4px;border-radius:3px;font-family:monospace;">\1</code>', safe)
        safe = cls._STRIKE.sub(r'<s>\1</s>', safe)
        safe = cls._SPOILER.sub(
            r'<span style="background:#333;color:#333;'
            r'border-radius:3px;padding:0 4px;" '
            r'title="Нажмите для просмотра">\1</span>', safe)

        # 3. @mentions
        def mention_sub(m):
            uname = m.group(1)
            col = accent_color if (known_users and uname in known_users) else "#80A0FF"
            return f'<b style="color:{col};">@{uname}</b>'
        safe = cls._MENTION.sub(mention_sub, safe)

        # 4. Clickable URLs — show full URL but styled nicely
        def url_sub(m):
            url = m.group(1)
            # Add https:// for bare www. links
            href = url if url.startswith("http") else "https://" + url
            try:
                from urllib.parse import urlparse as _up
                p = _up(href)
                display = p.netloc + (p.path[:28] + "…" if len(p.path) > 28 else p.path)
                display = display.rstrip("/")
            except Exception:
                display = url[:48] + ("…" if len(url) > 48 else "")
            return (f'<a href="{href}" style="color:{accent_color};'
                    f'text-decoration:none;border-bottom:1px solid {accent_color}80;">'
                    f'{display}</a>')
        safe = cls._URL.sub(url_sub, safe)

        # 5. Newlines
        safe = safe.replace("\n", "<br>")

        return safe

    @classmethod
    def is_formatting(cls, text: str) -> bool:
        """Quick check if text contains any formatting markers."""
        for pattern in (cls._BOLD, cls._ITALIC, cls._CODE,
                        cls._STRIKE, cls._SPOILER, cls._MENTION, cls._URL):
            if pattern.search(text):
                return True
        return False



class GroupCallWindow(QWidget):
    """
    Телемост-style group call window.
    Proper tile grid, bottom control bar with real menus.
    """
    sig_leave = pyqtSignal()

    def __init__(self, group_name: str, participants: list, voice_mgr,
                 parent=None):
        super().__init__(parent,
            Qt.WindowType.Window |
            Qt.WindowType.WindowTitleHint |
            Qt.WindowType.WindowCloseButtonHint |
            Qt.WindowType.WindowMinMaxButtonsHint)
        self._group_name   = group_name
        self._participants = list(participants)
        self._voice_mgr    = voice_mgr
        self._muted        = False
        self._cam_on       = False
        self._sharing      = False
        self._elapsed      = 0
        self._self_tile    = None   # ref to own tile for VAD ring
        self._share_timer  = None   # QTimer for screen share capture
        self._share_label  = None   # QLabel showing screen preview
        self.setWindowTitle(f"Групповой звонок — {group_name}")
        self.setMinimumSize(720, 500)
        self.resize(960, 620)
        self._build()
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(1000)
        # Subscribe to local VAD speaking events
        self._voice_mgr.subscribe_speaking(self._on_local_speaking)

    # ── Build UI ──────────────────────────────────────────────────────────────
    def _build(self):
        t = get_theme(S().theme)
        self.setStyleSheet(f"""
            QWidget {{ background:{t['bg3']}; color:{t['text']}; }}
            QFrame  {{ background:{t['bg2']}; }}
        """)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── Top bar ──────────────────────────────────────────────────────────
        topbar = QWidget()
        topbar.setFixedHeight(44)
        _tb = get_theme(S().theme)
        topbar.setStyleSheet(
            f"background:{_tb['bg3']}; border-bottom:1px solid {_tb['border']};")
        tbl = QHBoxLayout(topbar)
        tbl.setContentsMargins(16, 0, 16, 0)
        tbl.setSpacing(12)

        call_dot = QLabel("●")
        call_dot.setStyleSheet("color:#E74C3C;font-size:10px;")
        tbl.addWidget(call_dot)

        grp_lbl = QLabel(self._group_name)
        grp_lbl.setStyleSheet(
            "font-size:13px;font-weight:bold;color:white;")
        tbl.addWidget(grp_lbl)
        tbl.addStretch()

        self._dur_lbl = QLabel("00:00")
        self._dur_lbl.setStyleSheet(
            "font-size:12px;color:#7C4DFF;font-family:monospace;font-weight:bold;")
        tbl.addWidget(self._dur_lbl)
        tbl.addSpacing(16)

        self._pcount_lbl = QLabel(f"👥 {len(self._participants)+1}")
        self._pcount_lbl.setStyleSheet("font-size:11px;color:#8888AA;")
        tbl.addWidget(self._pcount_lbl)
        root.addWidget(topbar)

        # ── Participant grid ──────────────────────────────────────────────────
        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._scroll.setStyleSheet(
            f"QScrollArea{{border:none;background:{t['bg3']};}}"
            f"QScrollBar:vertical{{width:5px;background:{t['bg2']};}}"
            f"QScrollBar::handle:vertical{{background:{t['border']};border-radius:2px;}}")
        self._grid_w = QWidget()
        self._grid_w.setStyleSheet(f"background:{t['bg3']}")
        self._grid = QGridLayout(self._grid_w)
        self._grid.setSpacing(10)
        self._grid.setContentsMargins(14, 14, 14, 14)
        self._scroll.setWidget(self._grid_w)
        root.addWidget(self._scroll, stretch=1)

        # Populate tiles
        self._tiles: list = []
        all_peers = [{"username": S().username, "avatar_b64": S().avatar_b64,
                      "ip": get_local_ip(), "_is_self": True}]
        all_peers.extend(self._participants)
        self._rebuild_grid(all_peers)

        # ── Controls bar ─────────────────────────────────────────────────────
        ctrlbar = QWidget()
        ctrlbar.setFixedHeight(80)
        ctrlbar.setStyleSheet(
            "background:#16162A; border-top:1px solid #25254A;")
        cbl = QHBoxLayout(ctrlbar)
        cbl.setContentsMargins(20, 10, 20, 10)
        cbl.setSpacing(10)

        # Left group: link + mic + camera
        def _btn(icon, tip, bg="#2A2A46", hover="#38385E", size=52):
            b = QPushButton(icon)
            b.setFixedSize(size, size)
            b.setToolTip(tip)
            b.setCursor(Qt.CursorShape.PointingHandCursor)
            b.setCheckable(True)
            b.setStyleSheet(f"""
                QPushButton{{background:{bg};border-radius:{size//2}px;
                    font-size:18px;color:white;border:none;}}
                QPushButton:hover{{background:{hover};}}
                QPushButton:checked{{background:#3D1A1A;color:#FF6666;}}
            """)
            return b

        self._link_btn = _btn("🔗", "Скопировать ссылку на звонок")
        self._link_btn.setCheckable(False)
        self._link_btn.clicked.connect(self._copy_invite)
        cbl.addWidget(self._link_btn)

        self._mic_btn = _btn("🎤", "Микрофон вкл/выкл  (M)")
        self._mic_btn.clicked.connect(self._toggle_mute)
        cbl.addWidget(self._mic_btn)

        self._cam_btn = _btn("📷", "Camera on/off  (V)")
        self._cam_btn.clicked.connect(self._toggle_camera)
        cbl.addWidget(self._cam_btn)

        self._rec_btn = _btn("⏺", "Запись звонка  (R)")
        self._rec_btn.clicked.connect(self._toggle_recording)
        self._recording = False
        self._record_frames = []
        self._record_fname  = None
        cbl.addWidget(self._rec_btn)

        cbl.addStretch()

        # Centre group: participants + chat + more
        self._pts_btn = QPushButton(f"👥  {len(all_peers)}")
        self._pts_btn.setFixedHeight(40)
        self._pts_btn.setToolTip("Участники")
        self._pts_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        self._pts_btn.setStyleSheet(
            "QPushButton{background:#2A2A46;border-radius:20px;color:white;"
            "font-size:12px;padding:0 18px;border:none;}"
            f"QPushButton:hover{{background:#38385E;}}")
        self._pts_btn.clicked.connect(self._show_participants)
        cbl.addWidget(self._pts_btn)

        chat_btn = QPushButton("💬  Чат")
        chat_btn.setFixedHeight(40)
        chat_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        chat_btn.setStyleSheet(
            "QPushButton{background:#2A2A46;border-radius:20px;color:white;"
            "font-size:12px;padding:0 18px;border:none;}"
            f"QPushButton:hover{{background:#38385E;}}")
        cbl.addWidget(chat_btn)

        # ⋯ More menu
        more_btn = QPushButton("⋯")
        more_btn.setFixedSize(44, 44)
        more_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        more_btn.setToolTip("Ещё")
        more_btn.setStyleSheet(
            "QPushButton{background:#2A2A46;border-radius:22px;font-size:18px;"
            "color:white;border:none;}"
            f"QPushButton:hover{{background:#38385E;}}")
        more_btn.clicked.connect(lambda: self._show_more_menu(more_btn))
        cbl.addWidget(more_btn)

        cbl.addStretch()

        # Right: leave (red)
        leave_btn = QPushButton("📵")
        leave_btn.setFixedSize(56, 56)
        leave_btn.setToolTip("Покинуть звонок")
        leave_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        leave_btn.setStyleSheet(
            "QPushButton{background:#C0392B;border-radius:28px;"
            "font-size:24px;border:none;}"
            f"QPushButton:hover{{background:#922B21;}}")
        leave_btn.clicked.connect(self._leave)
        cbl.addWidget(leave_btn)

        root.addWidget(ctrlbar)

    # ── Tile helpers ──────────────────────────────────────────────────────────
    def _rebuild_grid(self, all_peers: list):
        # Clear existing tiles
        for tile in self._tiles:
            self._grid.removeWidget(tile)
            tile.deleteLater()
        self._tiles.clear()

        n    = len(all_peers)
        cols = 1 if n == 1 else (2 if n <= 4 else (3 if n <= 9 else 4))
        for i, peer in enumerate(all_peers):
            tile = self._make_tile(peer)
            self._grid.addWidget(tile, i // cols, i % cols)
            self._tiles.append(tile)
        self._all_peers = all_peers

    def _make_tile(self, peer: dict) -> QWidget:
        is_self = peer.get("_is_self", False)
        name    = peer.get("username", "?")
        av_b64  = peer.get("avatar_b64", "")

        tile = QWidget()
        border_idle    = "#27AE60" if is_self else "#25254A"
        border_speak   = "#39FF14" if is_self else "#7C4DFF"
        tile._border_idle  = border_idle
        tile._border_speak = border_speak
        tile._is_self      = is_self
        _tt = get_theme(S().theme)
        tile.setStyleSheet(f"""
            QWidget {{
                background: {_tt['bg2']};
                border-radius: 14px;
                border: 2px solid {border_idle};
            }}
        """)
        tile.setMinimumSize(180, 150)

        tl = QVBoxLayout(tile)
        tl.setContentsMargins(12, 16, 12, 12)
        tl.setSpacing(8)
        tl.setAlignment(Qt.AlignmentFlag.AlignCenter)

        av_size = 80
        av_lbl = QLabel()
        av_lbl.setFixedSize(av_size, av_size)
        av_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        av_lbl.setPixmap(_call_avatar_pixmap(av_b64, name, av_size))
        av_lbl.setStyleSheet(
            f"border-radius:{av_size//2}px;"
            f"border:3px solid {border_idle};")
        av_lbl.setObjectName("tile_av")
        tile._av_lbl      = av_lbl
        tile._av_idle_css = (f"border-radius:{av_size//2}px;"
                              f"border:3px solid {border_idle};")
        tile._av_speak_css= (f"border-radius:{av_size//2}px;"
                              f"border:3px solid {border_speak};"
                              f"background:rgba(57,255,20,15);")
        tl.addWidget(av_lbl, alignment=Qt.AlignmentFlag.AlignCenter)

        bottom = QHBoxLayout()
        bottom.setContentsMargins(0, 0, 0, 0)
        disp_name = "You" if is_self else name
        name_lbl = QLabel(disp_name)
        name_lbl.setStyleSheet(
            "font-size:12px;font-weight:bold;color:white;"
            "background:transparent;border:none;")
        bottom.addWidget(name_lbl)
        bottom.addStretch()
        mic_ico = QLabel("🎤" if not (is_self and self._muted) else "🔇")
        mic_ico.setStyleSheet("font-size:12px;background:transparent;border:none;")
        bottom.addWidget(mic_ico)
        if is_self:
            tile._mic_ico = mic_ico
        tl.addLayout(bottom)

        if is_self:
            self._self_tile = tile
        return tile

    # ── VAD speaking highlight ─────────────────────────────────────────────
    def _on_local_speaking(self, active: bool):
        """Called from VoiceCallManager when VAD detects speech / silence."""
        tile = self._self_tile
        if tile is None or self._muted:
            return
        if active:
            tile.setStyleSheet(f"""
                QWidget {{
                    background: #1A1A2E;
                    border-radius: 14px;
                    border: 2px solid {tile._border_speak};
                }}
            """)
            tile._av_lbl.setStyleSheet(tile._av_speak_css)
        else:
            tile.setStyleSheet(f"""
                QWidget {{
                    background: #1A1A2E;
                    border-radius: 14px;
                    border: 2px solid {tile._border_idle};
                }}
            """)
            tile._av_lbl.setStyleSheet(tile._av_idle_css)

    # ── Screen share ───────────────────────────────────────────────────────
    def _toggle_screen_share(self):
        if self._sharing:
            self._stop_screen_share()
        else:
            self._start_screen_share()

    def _start_screen_share(self):
        """Capture screen → show in a tile above the grid."""
        self._sharing = True

        # Create share overlay widget above grid
        share_w = QWidget()
        share_w.setObjectName("share_overlay")
        share_w.setStyleSheet(
            "QWidget#share_overlay{"
            "background:#0A0A14;border:2px solid #7C4DFF;"
            "border-radius:12px;}")
        share_w.setFixedHeight(200)
        sl = QVBoxLayout(share_w)
        sl.setContentsMargins(8, 8, 8, 8)

        hdr = QHBoxLayout()
        dot = QLabel("🔴  Демонстрация экрана")
        dot.setStyleSheet("color:#E74C3C;font-size:11px;font-weight:bold;")
        hdr.addWidget(dot)
        hdr.addStretch()
        stop_btn = QPushButton("✕ Остановить")
        stop_btn.setStyleSheet(
            "QPushButton{background:#3D1A1A;color:#FF6666;border:1px solid #5A2A2A;"
            "border-radius:8px;padding:3px 10px;font-size:10px;}"
            f"QPushButton:hover{{background:#5A2020;}}")
        stop_btn.clicked.connect(self._stop_screen_share)
        hdr.addWidget(stop_btn)
        sl.addLayout(hdr)

        self._share_label = QLabel()
        self._share_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._share_label.setStyleSheet("background:black;border-radius:6px;")
        sl.addWidget(self._share_label, stretch=1)

        # Insert before grid (index 1 = after topbar)
        root_layout = self.layout()
        root_layout.insertWidget(1, share_w)
        self._share_widget = share_w

        # Capture timer — 5 fps (200ms) to avoid lag
        self._share_timer = QTimer(self)
        self._share_timer.timeout.connect(self._capture_screen)
        self._share_timer.start(200)
        self._capture_screen()  # immediate first frame

        # Update ⋯ menu button state visually via tooltip
        self._sharing = True

    def _capture_screen(self):
        """Grab primary screen and display scaled preview."""
        try:
            screen = QApplication.primaryScreen()
            if screen is None:
                return
            pix = screen.grabWindow(0)
            if pix.isNull():
                return
            # Scale to fit label
            lbl = self._share_label
            if lbl is None:
                return
            w = lbl.width() or 400
            h = lbl.height() or 140
            scaled = pix.scaled(w, h,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.FastTransformation)
            lbl.setPixmap(scaled)
        except Exception as e:
            print(f"[screenshare] capture error: {e}")

    def _stop_screen_share(self):
        self._sharing = False
        if self._share_timer:
            self._share_timer.stop()
            self._share_timer = None
        if hasattr(self, '_share_widget') and self._share_widget:
            self._share_widget.setParent(None)
            self._share_widget.deleteLater()
            self._share_widget = None
        self._share_label = None


    # ── Controls ──────────────────────────────────────────────────────────────
    def _toggle_mute(self):
        self._muted = not self._muted
        self._voice_mgr.set_mute(self._muted)
        if self._muted:
            self._mic_btn.setText("🔇")
            self._mic_btn.setChecked(True)
        else:
            self._mic_btn.setText("🎤")
            self._mic_btn.setChecked(False)
        # Update self-tile mic icon
        for tile in self._tiles:
            if hasattr(tile, '_mic_ico'):
                tile._mic_ico.setText("🔇" if self._muted else "🎤")

    def _toggle_camera(self):
        self._cam_on = not self._cam_on
        self._cam_btn.setChecked(self._cam_on)
        if self._cam_on:
            self._start_camera_group()
        else:
            self._stop_camera_group()

    def _start_camera_group(self):
        try:
            from PyQt6.QtMultimedia import QCamera, QMediaCaptureSession
            from PyQt6.QtMultimediaWidgets import QVideoWidget

            # Find own tile and replace avatar with video preview
            tile = self._self_tile
            if tile is None:
                return

            # Create video widget inside tile
            self._cam_video = QVideoWidget(tile)
            self._cam_video.resize(tile.size())
            self._cam_video.setStyleSheet(
                "border-radius:14px;background:#000;")
            self._cam_video.show()

            self._group_cam = QCamera()
            self._group_cam_session = QMediaCaptureSession()
            self._group_cam_session.setCamera(self._group_cam)
            self._group_cam_session.setVideoOutput(self._cam_video)
            self._group_cam.start()

            self._cam_btn.setText("📷")
            self._cam_btn.setChecked(True)

        except ImportError:
            from PyQt6.QtWidgets import QToolTip
            QToolTip.showText(
                self._cam_btn.mapToGlobal(self._cam_btn.rect().center()),
                "pip install PyQt6-QtMultimediaWidgets --break-system-packages")
            self._cam_on = False
            self._cam_btn.setChecked(False)
        except Exception as e:
            print(f"[group cam] {e}")
            self._cam_on = False
            self._cam_btn.setChecked(False)

    def _stop_camera_group(self):
        try:
            if hasattr(self, '_group_cam') and self._group_cam:
                self._group_cam.stop()
                self._group_cam = None
            if hasattr(self, '_cam_video') and self._cam_video:
                self._cam_video.hide()
                self._cam_video.setParent(None)
                self._cam_video.deleteLater()
                self._cam_video = None
        except Exception as e:
            print(f"[group cam stop] {e}")
        self._cam_btn.setText("📷")
        self._cam_btn.setChecked(False)

    def _copy_invite(self):
        gid  = getattr(self, '_gid', "")
        ip   = get_local_ip()
        link = f"goidaphone://call/{gid}?host={ip}"
        QApplication.clipboard().setText(link)
        # Brief tooltip feedback
        from PyQt6.QtWidgets import QToolTip
        QToolTip.showText(
            self._link_btn.mapToGlobal(self._link_btn.rect().center()),
            "✓ Ссылка скопирована!")

    def _show_participants(self):
        t = get_theme(S().theme)
        dlg = QDialog(self)
        dlg.setWindowTitle("Участники звонка")
        dlg.resize(340, 360)
        dlg.setStyleSheet(f"background:#1A1A2E;color:white;")
        vl = QVBoxLayout(dlg)
        hdr = QLabel(f"В звонке: {len(getattr(self,'_all_peers',[]))} участников")
        hdr.setStyleSheet("font-size:13px;font-weight:bold;padding:8px 0;")
        vl.addWidget(hdr)
        lw = QListWidget()
        lw.setStyleSheet(
            "QListWidget{background:#12121F;border:1px solid #2D2D4E;"
            "border-radius:8px;}"
            f"QListWidget::item{{padding:8px;border-bottom:1px solid #1E1E32;}}"
            f"QListWidget::item:selected{{background:#2A2A4E;}}")
        for peer in getattr(self, '_all_peers', []):
            name = peer.get("username","?")
            flag = " (Вы)" if peer.get("_is_self") else ""
            lw.addItem(f"👤  {name}{flag}")
        vl.addWidget(lw)
        close = QPushButton(_L("Закрыть", "Close", "閉じる"))
        close.clicked.connect(dlg.accept)
        vl.addWidget(close)
        dlg.exec()

    def _show_more_menu(self, btn: QPushButton):
        menu = QMenu(self)
        menu.setStyleSheet("""
            QMenu {
                background:#1E1E32; color:white;
                border:1px solid #35354E; border-radius:10px; padding:4px 0;
            }
            QMenu::item { padding:9px 22px; font-size:12px; }
            QMenu::item:selected {
                background:#7C4DFF; border-radius:6px; color:white;
            }
            QMenu::separator { background:#35354E; height:1px; margin:3px 8px; }
        """)

        # Screen share
        share_act = menu.addAction("🖥  Демонстрация экрана")
        share_act.triggered.connect(self._toggle_screen_share)

        # Quality submenu
        quality_menu = menu.addMenu("📶  Качество звука")
        quality_menu.setStyleSheet(menu.styleSheet())
        for label, val in [("Низкое (8 кГц)", 8000),
                            ("Стандарт (16 кГц)", 16000),
                            ("Высокое (48 кГц)", 48000)]:
            act = quality_menu.addAction(label)
            act.triggered.connect(lambda _, v=val: self._set_quality(v))

        menu.addSeparator()

        # Layout mode
        layout_menu = menu.addMenu("🔲  Раскладка")
        layout_menu.setStyleSheet(menu.styleSheet())
        for label in ["Сетка", "Активный докладчик", "Боковая панель"]:
            layout_menu.addAction(label)

        menu.addSeparator()

        # Stats
        menu.addAction("📊  Статистика соединения",
                        self._show_stats)
        menu.addAction("🔔  Настройки уведомлений",
                        self._show_notif_settings)
        menu.addSeparator()
        menu.addAction("❓  Справка",
                        lambda: QMessageBox.information(self, "Справка",
                            "Групповой звонок GoidaPhone\n\n"
                            "M — mute/unmute mic\n"
                            "V — включить/выключить камеру\n"
                            "Esc — свернуть окно\n\n"
                            "Демонстрация экрана: кнопка ⋯ → Демонстрация"))

        r = btn.rect()
        menu.exec(btn.mapToGlobal(r.bottomLeft()))

    def _toggle_screen_share(self):
        if self._sharing:
            self._stop_screen_share()
        else:
            self._start_screen_share()

    def _start_screen_share(self):
        """Show screen share inline — in a new tile next to 'Вы' in the grid."""
        self._sharing = True

        # Create a share tile that looks like a participant tile
        share_tile = QWidget()
        share_tile.setStyleSheet(
            "QWidget{background:#0A0A14;border-radius:14px;"
            "border:2px solid #E74C3C;}")
        share_tile.setMinimumSize(280, 180)
        tl = QVBoxLayout(share_tile)
        tl.setContentsMargins(6, 6, 6, 6)
        tl.setSpacing(4)

        # Header row inside tile
        hdr = QHBoxLayout()
        dot = QLabel("🔴 Экран")
        dot.setStyleSheet(
            "color:#E74C3C;font-size:10px;font-weight:bold;background:transparent;border:none;")
        hdr.addWidget(dot)
        hdr.addStretch()
        stop_btn = QPushButton("✕")
        stop_btn.setFixedSize(20, 20)
        stop_btn.setStyleSheet(
            "QPushButton{background:#3D1A1A;color:#FF6666;border:none;"
            "border-radius:10px;font-size:10px;font-weight:bold;}"
            f"QPushButton:hover{{background:#882222;}}")
        stop_btn.clicked.connect(self._stop_screen_share)
        hdr.addWidget(stop_btn)
        tl.addLayout(hdr)

        self._share_label = QLabel()
        self._share_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._share_label.setStyleSheet(
            "background:#000;border-radius:8px;border:none;")
        tl.addWidget(self._share_label, stretch=1)

        # Add to grid next to existing tiles
        n = len([t for t in self._tiles if t])
        cols = max(2, min(4, n + 1))
        # Re-layout grid with share tile
        self._grid.addWidget(share_tile, n // cols, n % cols)
        self._share_widget = share_tile
        self._tiles.append(share_tile)

        self._share_timer = QTimer(self)
        self._share_timer.timeout.connect(self._capture_screen)
        self._share_timer.start(200)
        self._capture_screen()

    def _capture_screen(self):
        try:
            screen = QApplication.primaryScreen()
            if screen is None:
                return
            pix = screen.grabWindow(0)
            if pix.isNull():
                return
            lbl = self._share_label
            if lbl is None:
                return
            w = max(lbl.width(), 100)
            h = max(lbl.height(), 60)
            scaled = pix.scaled(
                w, h,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.FastTransformation)
            lbl.setPixmap(scaled)
        except Exception as e:
            print(f"[screenshare] {e}")

    def _stop_screen_share(self):
        self._sharing = False
        if self._share_timer:
            self._share_timer.stop()
            self._share_timer = None
        if hasattr(self, '_share_widget') and self._share_widget:
            self._share_widget.setParent(None)
            self._share_widget.deleteLater()
            self._share_widget = None
        self._share_label = None

    def _set_quality(self, rate: int):
        try:
            self._voice_mgr.audio.RATE = rate
            QMessageBox.information(self, "Качество звука",
                f"Частота дискретизации изменена на {rate//1000} кГц.\n"
                "Изменение вступит в силу при следующем звонке.")
        except Exception:
            pass

    def _toggle_recording(self):
        if getattr(self, '_recording', False):
            self._stop_recording()
        else:
            self._start_recording()

    def _start_recording(self):
        import threading
        self._recording = True
        self._record_frames = []
        try: self._btn_rec.setChecked(True)
        except Exception: pass
        fname = DATA_DIR / f"call_rec_{int(time.time())}.wav"
        self._record_fname = fname
        def _loop():
            try:
                import pyaudio as _pa
                p = _pa.PyAudio()
                s = p.open(format=_pa.paInt16, channels=1,
                           rate=16000, input=True, frames_per_buffer=512)
                while self._recording:
                    self._record_frames.append(
                        s.read(512, exception_on_overflow=False))
                s.stop_stream(); s.close(); p.terminate()
            except Exception as e:
                print(f"[rec] {e}")
        threading.Thread(target=_loop, daemon=True).start()

    def _stop_recording(self):
        self._recording = False
        try: self._btn_rec.setChecked(False)
        except Exception: pass
        try:
            import wave
            if self._record_frames and self._record_fname:
                with wave.open(str(self._record_fname), 'wb') as wf:
                    wf.setnchannels(1); wf.setsampwidth(2); wf.setframerate(16000)
                    wf.writeframes(b''.join(self._record_frames))
                QTimer.singleShot(0, lambda: QMessageBox.information(
                    self, "Запись", f"Сохранено: {self._record_fname.name}"))
        except Exception as e:
            print(f"[rec save] {e}")
        self._record_frames = []

    def _show_stats(self):
        dlg = QDialog(self)
        dlg.setWindowTitle("Statistics")
        dlg.resize(340, 260)
        dlg.setStyleSheet("background:#1A1A2E;color:white;")
        vl = QVBoxLayout(dlg)
        active = list(self._voice_mgr.active)
        stats_text = (
            f"Участников в звонке: {len(getattr(self,'_all_peers',[]))}\n"
            f"Активных голосовых соединений: {len(active)}\n"
            f"Длительность: {self._elapsed//60:02d}:{self._elapsed%60:02d}\n"
            f"Микрофон: {'заглушён' if self._muted else 'активен'}\n"
            f"Частота: {getattr(self._voice_mgr.audio,'RATE',16000)} Гц\n"
            f"VAD: {'вкл' if getattr(self._voice_mgr.audio,'vad_enabled',False) else 'выкл'}\n"
        )
        lbl = QLabel(stats_text)
        lbl.setStyleSheet("font-family:monospace;font-size:12px;padding:12px;")
        vl.addWidget(lbl)
        QPushButton(_L("Закрыть", "Close", "閉じる"), clicked=dlg.accept, parent=dlg)
        close = QPushButton(_L("Закрыть", "Close", "閉じる")); close.clicked.connect(dlg.accept)
        vl.addWidget(close)
        dlg.exec()

    def _show_notif_settings(self):
        QMessageBox.information(self, "Notifications",
            "Настройки уведомлений для звонка:\n\n"
            "• Звук входа участника — в Настройки → Звуки\n"
            "• Уведомления — в Настройки → Уведомления")

    # ── Add participant (called externally when someone joins) ────────────────
    def add_participant(self, peer: dict):
        self._participants.append(peer)
        all_peers = [{"username": S().username, "avatar_b64": S().avatar_b64,
                      "ip": get_local_ip(), "_is_self": True}]
        all_peers.extend(self._participants)
        self._rebuild_grid(all_peers)
        self._pcount_lbl.setText(f"👥 {len(all_peers)}")
        self._pts_btn.setText(f"👥  {len(all_peers)}")

    # ── Timer ─────────────────────────────────────────────────────────────────
    def _tick(self):
        self._elapsed += 1
        m = self._elapsed // 60; s = self._elapsed % 60
        self._dur_lbl.setText(f"{m:02d}:{s:02d}")

    # ── Keyboard shortcuts ────────────────────────────────────────────────────
    def keyPressEvent(self, ev):
        k = ev.key()
        if k == Qt.Key.Key_M:
            self._mic_btn.click()
        elif k == Qt.Key.Key_V:
            self._cam_btn.click()
        elif k == Qt.Key.Key_Escape:
            self.showMinimized()
        else:
            super().keyPressEvent(ev)

    # ── Leave ─────────────────────────────────────────────────────────────────
    def _leave(self):
        self._timer.stop()
        self._voice_mgr.unsubscribe_speaking(self._on_local_speaking)
        if self._sharing:
            self._stop_screen_share()
        self.sig_leave.emit()
        self.close()

    def closeEvent(self, event):
        self._timer.stop()
        try:
            self._voice_mgr.unsubscribe_speaking(self._on_local_speaking)
        except Exception:
            pass
        if self._sharing:
            self._stop_screen_share()
        try:
            self.sig_leave.emit()
        except Exception:
            pass
        super().closeEvent(event)





