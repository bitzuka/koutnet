#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Chat UI (bubbles, peer panel, chat panel)
try:
    from gdf_core import _L, TR, S, get_theme
    from gdf_core import *
    from gdf_network import *
    from gdf_ui_base import *
except ImportError:
    pass
# ═══════════════════════════════════════════════════════════════════════════
#  CHAT BUBBLE WIDGET  (for rendering messages)
# ═══════════════════════════════════════════════════════════════════════════
class MessageEntry:
    """Data class for a single chat message."""
    __slots__ = ('sender', 'text', 'ts', 'is_own', 'color', 'emoji',
                 'msg_type', 'image_data', 'is_system', 'is_edited',
                 'is_forwarded', 'forwarded_from', 'reply_to_text',
                 'chat_id', 'msg_id')

    def __init__(self, sender="", text="", ts=0.0, is_own=False,
                 color="#E0E0E0", emoji="", msg_type="public",
                 image_data=None, is_system=False, is_edited=False,
                 is_forwarded=False, forwarded_from="",
                 reply_to_text="", chat_id="", msg_id=""):
        self.sender        = sender
        self.text          = text
        self.ts            = ts
        self.is_own        = is_own
        self.color         = color
        self.emoji         = emoji
        self.msg_type      = msg_type
        self.image_data    = image_data
        self.is_system     = is_system
        self.is_edited     = is_edited
        self.is_forwarded  = is_forwarded
        self.forwarded_from = forwarded_from
        self.reply_to_text = reply_to_text
        self.chat_id       = chat_id
        self.msg_id        = msg_id or f"{ts:.3f}"


# ─── Image viewer dialog (fullscreen photo view) ──────────────────────────
def _find_main_window(widget=None) -> 'QMainWindow | None':
    """Walk up Qt parent chain to find the MainWindow instance."""
    # Try direct parent chain first
    w = widget
    for _ in range(20):
        if w is None: break
        if isinstance(w, QMainWindow):
            return w
        w = w.parent() if callable(getattr(w, 'parent', None)) else None
    # Fallback: search topLevelWidgets
    for w in QApplication.topLevelWidgets():
        if isinstance(w, QMainWindow):
            return w
    return None


class ImageViewer(QWidget):
    """
    Inline image viewer — fullscreen overlay inside the app window.
    Click outside image or press ESC to close. No separate window.
    """
    def __init__(self, image_data: bytes, parent=None):
        # Find the MainWindow to use as overlay target
        mw = _find_main_window(parent)
        overlay_parent = mw.centralWidget() if mw else parent
        super().__init__(overlay_parent)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        t = get_theme(S().theme)
        self.setStyleSheet(f"background:rgba(0,0,0,220);color:{t['text']};")
        self._scale   = 1.0
        self._offset  = QPoint(0, 0)
        self._drag_start = None
        self._rotation = 0
        self._raw_data = image_data
        pm = QPixmap()
        pm.loadFromData(image_data)
        self._pixmap = pm
        self._setup_ui(t)
        if overlay_parent:
            self.resize(overlay_parent.size())
            overlay_parent.installEventFilter(self)
        self.show()
        self.raise_()
        self.setFocus()
        QTimer.singleShot(50, self._fit_to_window)

    def eventFilter(self, obj, event):
        if event.type() == event.Type.Resize:
            self.resize(obj.size())
        return False

    def exec(self):
        """Compat shim — just show (we're not a dialog)."""
        self.show(); self.raise_(); self.setFocus()

    def _setup_ui(self, t):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        # ── Toolbar ──
        toolbar = QWidget()
        toolbar.setStyleSheet(
            f"background:{t['bg3']};border-bottom:1px solid {t['border']};")
        tl = QHBoxLayout(toolbar)
        tl.setContentsMargins(8,4,8,4)
        tl.setSpacing(4)

        def mk(text, tip, cb):
            b = QPushButton(text)
            b.setToolTip(tip)
            b.setFixedHeight(28)
            b.setStyleSheet(
                f"QPushButton{{background:{t['bg2']};color:{t['text']};"
                "border-radius:5px;padding:0 8px;font-size:12px;"
                f"border:1px solid {t['border']};}}"
                f"QPushButton:hover{{background:{t['btn_hover']};}}")
            b.clicked.connect(cb)
            tl.addWidget(b)
            return b

        mk("🔍+", "Увеличить (+)",  lambda: self._zoom(1.25))
        mk("🔍-", "Уменьшить (-)",  lambda: self._zoom(1/1.25))
        mk("⊡",  "По размеру окна", self._fit_to_window)
        mk("⊞",  "Заполнить окно",  self._fill_window)
        mk("↺",  "Повернуть влево", lambda: self._rotate(-90))
        mk("↻",  "Повернуть вправо",lambda: self._rotate(90))
        tl.addStretch()

        self._zoom_lbl = QLabel("100%")
        self._zoom_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:11px;background:transparent;"
            "padding:0 8px;")
        tl.addWidget(self._zoom_lbl)

        mk(_L("💾 Сохранить", "💾 Save", "💾 保存"), "Сохранить в файл",  self._save_file)
        mk(_L("📋 Copy", "📋 Copy", "📋 コピー"),"Копировать в буфер", self._copy_clipboard)
        def _close_overlay():
            if self.parent():
                self.parent().removeEventFilter(self)
            self.hide(); self.deleteLater()
        mk("✕",            "Закрыть  (Esc)",     _close_overlay)
        self._close_overlay = _close_overlay

        lay.addWidget(toolbar)

        # ── Canvas — fills overlay minus toolbar/info ──
        self._canvas = QWidget()
        self._canvas.setStyleSheet("background:transparent;")
        self._canvas.setMouseTracking(True)
        self._canvas.paintEvent        = self._paint_canvas
        self._canvas.mousePressEvent   = self._mouse_press
        self._canvas.mouseMoveEvent    = self._mouse_move
        self._canvas.mouseReleaseEvent = self._mouse_release
        self._canvas.wheelEvent        = self._wheel
        self._canvas.mouseDoubleClickEvent = lambda e: self._fit_to_window()
        lay.addWidget(self._canvas, stretch=1)

        # ── Bottom info bar ──
        info = QWidget()
        info.setStyleSheet(
            f"background:{t['bg3']};border-top:1px solid {t['border']};")
        il = QHBoxLayout(info)
        il.setContentsMargins(10,3,10,3)
        self._info_lbl = QLabel()
        self._info_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:10px;background:transparent;")
        il.addWidget(self._info_lbl)
        il.addStretch()
        hint = QLabel(TR("scroll_zoom_hint"))
        hint.setStyleSheet(
            f"color:{t['text_dim']};font-size:9px;background:transparent;")
        il.addWidget(hint)
        lay.addWidget(info)

        if not self._pixmap.isNull():
            w, h = self._pixmap.width(), self._pixmap.height()
            self._info_lbl.setText(f"{w} × {h} px")

    def _current_pixmap(self):
        """Return pixmap with rotation applied."""
        if self._rotation == 0:
            return self._pixmap
        t = QTransform().rotate(self._rotation)
        return self._pixmap.transformed(t, Qt.TransformationMode.SmoothTransformation)

    def _paint_canvas(self, event):
        painter = QPainter(self._canvas)
        # Transparent — the overlay widget bg provides the dim
        painter.fillRect(self._canvas.rect(), QColor(0, 0, 0, 0))
        pm = self._current_pixmap()
        if pm.isNull():
            return
        cw, ch = self._canvas.width(), self._canvas.height()
        scaled = pm.scaled(
            int(pm.width()  * self._scale),
            int(pm.height() * self._scale),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation)
        x = (cw - scaled.width())  // 2 + self._offset.x()
        y = (ch - scaled.height()) // 2 + self._offset.y()
        # Draw subtle shadow
        painter.fillRect(x+4, y+4, scaled.width(), scaled.height(),
                         QColor(0,0,0,60))
        painter.drawPixmap(x, y, scaled)

    def _zoom(self, factor: float):
        self._scale = max(0.05, min(self._scale * factor, 20.0))
        self._zoom_lbl.setText(f"{int(self._scale*100)}%")
        self._canvas.update()

    def _fit_to_window(self):
        pm = self._current_pixmap()
        if pm.isNull(): return
        cw = max(self._canvas.width(), 100)
        ch = max(self._canvas.height(), 100)
        sx = cw / pm.width()
        sy = ch / pm.height()
        self._scale = min(sx, sy) * 0.95
        self._offset = QPoint(0, 0)
        self._zoom_lbl.setText(f"{int(self._scale*100)}%")
        self._canvas.update()

    def _fill_window(self):
        pm = self._current_pixmap()
        if pm.isNull(): return
        sx = self._canvas.width()  / pm.width()
        sy = self._canvas.height() / pm.height()
        self._scale = max(sx, sy)
        self._offset = QPoint(0, 0)
        self._zoom_lbl.setText(f"{int(self._scale*100)}%")
        self._canvas.update()

    def _rotate(self, deg: int):
        self._rotation = (self._rotation + deg) % 360
        self._fit_to_window()

    def _mouse_press(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self._drag_start = event.pos()
            self._canvas.setCursor(Qt.CursorShape.ClosedHandCursor)

    def _mouse_move(self, event):
        if self._drag_start is not None:
            delta = event.pos() - self._drag_start
            self._drag_start = event.pos()
            self._offset = QPoint(self._offset.x()+delta.x(),
                                  self._offset.y()+delta.y())
            self._canvas.update()

    def _mouse_release(self, event):
        self._drag_start = None
        self._canvas.setCursor(Qt.CursorShape.OpenHandCursor)

    def _wheel(self, event):
        delta = event.angleDelta().y()
        self._zoom(1.12 if delta > 0 else 1/1.12)

    def _save_file(self):
        fn, _ = QFileDialog.getSaveFileName(
            self, "Сохранить изображение", "image.png",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp)")
        if fn:
            self._current_pixmap().save(fn)
            QMessageBox.information(self,"Сохранено",f"Файл сохранён:\n{fn}")

    def _copy_clipboard(self):
        QApplication.clipboard().setPixmap(self._current_pixmap())
        # Small toast via status bar is not available, just do it silently

    def mousePressEvent(self, event):
        """Click on dark backdrop closes the viewer."""
        # Check if click is outside the image area
        pm = self._current_pixmap()
        if not pm.isNull():
            cw, ch = self._canvas.width(), self._canvas.height()
            sw = int(pm.width()  * self._scale)
            sh = int(pm.height() * self._scale)
            img_x = (cw - sw) // 2 + self._offset.x() + self._canvas.x()
            img_y = (ch - sh) // 2 + self._offset.y() + self._canvas.y()
            from PyQt6.QtCore import QRect
            img_rect = QRect(img_x, img_y, sw, sh)
            if not img_rect.contains(event.pos()):
                self._close_overlay(); return
        super().mousePressEvent(event)

    def keyPressEvent(self, event):
        k = event.key()
        if k == Qt.Key.Key_Escape:
            self._close_overlay()
        elif k == Qt.Key.Key_Plus or k == Qt.Key.Key_Equal:
            self._zoom(1.25)
        elif k == Qt.Key.Key_Minus:
            self._zoom(1/1.25)
        elif k == Qt.Key.Key_0:
            self._fit_to_window()
        elif k == Qt.Key.Key_R:
            self._rotate(90)
        elif k == Qt.Key.Key_S and (event.modifiers() & Qt.KeyboardModifier.ControlModifier):
            self._save_file()



# ─── Single message bubble widget ─────────────────────────────────────────
class MessageBubble(QWidget):
    """One chat message as a proper widget bubble."""
    sig_forward = pyqtSignal(object)
    sig_edit    = pyqtSignal(object)
    sig_delete  = pyqtSignal(object)
    sig_react   = pyqtSignal(object, str)  # (entry, emoji)
    sig_reply   = pyqtSignal(object)
    sig_copy    = pyqtSignal(str)
    sig_ping    = pyqtSignal(str)

    QUICK_REACT = ["\U0001f44d","\u2764\ufe0f","\U0001f602","\U0001f62e",
                   "\U0001f622","\U0001f525","\U0001f4af","\U0001f44e"]

    def __init__(self, entry, read_map, known_users, parent=None):
        super().__init__(parent)
        self.entry = entry
        self._read_map = read_map
        self._known_users = known_users
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.customContextMenuRequested.connect(self._ctx_menu)
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground)
        self._chk = None
        self._build()

    def _build(self):
        t = get_theme(S().theme)
        m = self.entry
        self._text_lbl = None  # exposed for search highlight
        self._tick_lbl = None  # for fast read-receipt update
        outer = QVBoxLayout(self)
        self._chk = QCheckBox()
        self._chk.setVisible(False)
        self._chk.toggled.connect(self._on_select_toggled)
        outer.addWidget(self._chk)
        outer.setContentsMargins(8, 2, 8, 2)
        outer.setSpacing(0)

        def _count_emoji(s: str) -> int:
            """Считает количество эмодзи-символов в строке."""
            import unicodedata
            count = 0
            for ch in s:
                if ch in (' ', '\u200d', '\ufe0f', '\u20e3', '\ufe0e'):
                    continue
                cp = ord(ch)
                if (0x1F000 <= cp <= 0x1FFFF or
                    0x2600  <= cp <= 0x27BF  or
                    0x1F300 <= cp <= 0x1F9FF or
                    unicodedata.category(ch) in ('So', 'Mn')):
                    count += 1
                else:
                    return 0   # есть не-эмодзи символ
            return count

        def _is_emoji_only(s: str) -> bool:
            return bool(s) and _count_emoji(s.strip()) > 0

        # System message
        if m.is_system:
            lbl = QLabel(m.text)
            lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl.setStyleSheet(
                f"color:{t['text_dim']};font-size:10px;font-style:italic;"
                f"background:{t['bg2']};border-radius:8px;"
                f"padding:2px 12px;margin:2px 80px;")
            lbl.setWordWrap(True)
            outer.addWidget(lbl)
            return

        row = QHBoxLayout()
        row.setSpacing(8)
        row.setContentsMargins(0, 0, 0, 0)

        # Avatar for others
        if not m.is_own:
            av = QLabel()
            av.setFixedSize(32, 32)
            av.setPixmap(default_avatar(m.sender, 32))
            av.setAlignment(Qt.AlignmentFlag.AlignTop)
            row.addWidget(av)

        # Bubble
        bw = QWidget()
        bw.setObjectName("msg_bubble")
        if m.is_own:
            bw.setStyleSheet(
                "QWidget#msg_bubble{"
                "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                f"stop:0 {t['msg_own']},stop:1 {t['accent2']});"
                "border-radius:14px 14px 4px 14px;"
                f"border:1px solid {t['accent']};"
                f"border-bottom:2px solid rgba(0,0,0,63);}}")
        else:
            bw.setStyleSheet(
                "QWidget#msg_bubble{"
                "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                f"stop:0 {t['msg_other']},stop:1 {t['bg3']});"
                "border-radius:14px 14px 14px 4px;"
                f"border:1px solid {t['border']};"
                f"border-bottom:2px solid rgba(0,0,0,51);}}")

        bl = QVBoxLayout(bw)
        bl.setContentsMargins(12, 8, 12, 8)
        bl.setSpacing(4)

        # Sender name
        if not m.is_own:
            nl = QLabel(f"<b style='color:{m.color};'>{m.sender}</b>"
                        + (f" {m.emoji}" if m.emoji else ""))
            nl.setTextFormat(Qt.TextFormat.RichText)
            nl.setStyleSheet("font-size:11px;background:transparent;")
            bl.addWidget(nl)

        # Forward
        if m.is_forwarded and m.forwarded_from:
            fl = QLabel(f"\u21aa \u041f\u0435\u0440\u0435\u0441\u043b\u0430\u043d\u043e \u043e\u0442: <i>{m.forwarded_from}</i>")
            fl.setStyleSheet(f"font-size:9px;color:{t['text_dim']};background:transparent;")
            fl.setTextFormat(Qt.TextFormat.RichText)
            bl.addWidget(fl)

        # Reply quote — Telegram-style pill
        if m.reply_to_text:
            short = m.reply_to_text[:60] + ("\u2026" if len(m.reply_to_text) > 60 else "")
            rf = QWidget()
            rf.setObjectName("reply_pill")
            rf.setStyleSheet(
                "QWidget#reply_pill{"
                "background:rgba(255,255,255,15);"
                f"border-left:3px solid {t['accent']};"
                "border-radius:4px;margin-bottom:2px;}")
            rfl = QVBoxLayout(rf)
            rfl.setContentsMargins(8, 4, 8, 4)
            rfl.setSpacing(1)
            irt = QLabel("\u21a9 \u041e\u0442\u0432\u0435\u0442")
            irt.setStyleSheet(f"font-size:8px;font-weight:bold;color:{t['accent']};background:transparent;")
            rfl.addWidget(irt)
            rl = QLabel(short)
            rl.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
            rl.setWordWrap(True)
            rfl.addWidget(rl)
            bl.addWidget(rf)

        # Content
        if m.image_data:
            if m.msg_type == "sticker":
                bw.setStyleSheet("background:transparent;border:none;")
                bw.setMaximumWidth(180)
                self._add_image(bl, m, t)
                if m.is_own:
                    row.addStretch(); row.addWidget(bw)
                else:
                    row.addWidget(bw); row.addStretch()
                outer.addLayout(row)
                return
            if m.msg_type == "video":
                self._add_video(bl, m, t)
            else:
                self._add_image(bl, m, t)
        elif m.msg_type == "file":
            self._add_file(bl, m, t)
        elif m.text.startswith("__GROUP_INVITE__:"):
            # ── Group invite card ──────────────────────────────────────────
            import json as _json
            try:
                inv = _json.loads(m.text[len("__GROUP_INVITE__:"):])
                gid   = inv.get("gid", "")
                gname = inv.get("gname", "?")
                host  = inv.get("host", "")
                sender_name = inv.get("from", "?")
            except Exception:
                gid = gname = host = ""; sender_name = "?"

            card = QWidget()
            card.setStyleSheet(
                f"QWidget{{background:rgba(0,0,0,45);"
                f"border:1px solid {t['accent']};border-radius:10px;padding:4px;}}")
            card_lay = QVBoxLayout(card)
            card_lay.setContentsMargins(10, 8, 10, 8)
            card_lay.setSpacing(4)

            ico_lbl = QLabel(TR("group_invite_card"))
            ico_lbl.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;border:none;")
            card_lay.addWidget(ico_lbl)

            name_lbl = QLabel(f"<b style='font-size:14px;'>{gname}</b>")
            name_lbl.setTextFormat(Qt.TextFormat.RichText)
            name_lbl.setStyleSheet("background:transparent;border:none;")
            card_lay.addWidget(name_lbl)

            already_in = gid and gid in dict(GROUPS.list_for(get_local_ip()))

            if already_in:
                status_lbl = QLabel(TR("already_in_group"))
                status_lbl.setStyleSheet(f"font-size:10px;color:#80FF80;background:transparent;border:none;")
                card_lay.addWidget(status_lbl)
            elif gid:
                join_btn = QPushButton(TR("btn_join"))
                join_btn.setFixedHeight(30)
                join_btn.setCursor(Qt.CursorShape.PointingHandCursor)
                join_btn.setStyleSheet(
                    f"QPushButton{{background:{t['accent']};color:white;"
                    "border-radius:8px;border:none;font-weight:bold;font-size:11px;}}"
                    "")
                def _do_join(checked=False, _gid=gid, _gname=gname, _host=host, _btn=join_btn):
                    GROUPS.add_member(_gid, get_local_ip())
                    _btn.setText(TR("joined_group"))
                    _btn.setEnabled(False)
                    _btn.setStyleSheet("QPushButton{background:#2a6a2a;color:#80FF80;"
                                       "border-radius:8px;border:none;font-size:11px;}")
                join_btn.clicked.connect(_do_join)
                card_lay.addWidget(join_btn)
            bl.addWidget(card)
        else:
            # Detect solo-emoji messages → render big
            txt = m.text.strip()
            _emoji_count = _count_emoji(txt)
            is_big_emoji = _emoji_count > 0 and _emoji_count <= 3
            if is_big_emoji:
                # 1 → 72px, 2 → 56px, 3 → 44px
                _sizes = {1: 72, 2: 56, 3: 44}
                _fs = _sizes.get(_emoji_count, 44)
                tl = QLabel(txt)
                tl.setFont(QFont("Segoe UI Emoji,Noto Color Emoji,Apple Color Emoji", _fs))
                tl.setStyleSheet(
                    f"background:transparent;border:none;padding:6px 4px;"
                    f"font-size:{_fs}px;")
                tl.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
                tl.setWordWrap(False)
                tl.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
                tl.setMinimumSize(_fs + 16, _fs + 16)
                tl.adjustSize()
            else:
                html = TextFormatter.format(m.text, accent_color=t['accent'],
                                            known_users=self._known_users)
                tl = QLabel(html)
                tl.setTextFormat(Qt.TextFormat.RichText)
                tl.setWordWrap(True)
                tl.setOpenExternalLinks(False)   # we handle clicks ourselves
                tl.setTextInteractionFlags(
                    Qt.TextInteractionFlag.TextSelectableByMouse |
                    Qt.TextInteractionFlag.LinksAccessibleByMouse)
                tl.linkActivated.connect(
                    lambda url, _p=tl: _open_link(url, _p))
                tl.setStyleSheet(
                    f"color:{t['text']};font-size:13px;"
                    "background:transparent;line-height:1.5;")
            bl.addWidget(tl)

        # Footer
        fr = QHBoxLayout()
        fr.setSpacing(4)
        fr.setContentsMargins(0, 2, 0, 0)
        if m.is_edited:
            el = QLabel("\u0438\u0437\u043c\u0435\u043d\u0435\u043d\u043e")
            el.setStyleSheet(f"font-size:8px;color:{t['text_dim']};font-style:italic;background:transparent;")
            fr.addWidget(el)
        fr.addStretch()
        ts_str = datetime.fromtimestamp(m.ts).strftime("%H:%M")
        tl2 = QLabel(ts_str)
        tl2.setStyleSheet(f"font-size:9px;color:{t['text_dim']};background:transparent;")
        fr.addWidget(tl2)
        if m.is_own:
            read = self._read_map.get(m.msg_id, False)
            tick = QLabel("\u2713\u2713" if read else "\u2713")
            tc = t['accent'] if read else t['text_dim']
            tick.setStyleSheet(f"font-size:9px;color:{tc};background:transparent;")
            fr.addWidget(tick)
        bl.addLayout(fr)

        # Reactions
        reactions = REACTIONS.summary(m.chat_id or "public", m.ts)
        if reactions:
            rrow = QHBoxLayout()
            rrow.setSpacing(4)
            rrow.setContentsMargins(0, 2, 0, 0)
            for em, cnt in reactions[:6]:
                rb = QPushButton(f"{em} {cnt}")
                rb.setFixedHeight(22)
                rb.setCursor(Qt.CursorShape.PointingHandCursor)
                rb.setStyleSheet(
                    f"QPushButton{{background:{t['bg3']};border:1px solid {t['border']};"
                    f"border-radius:11px;padding:0 7px;font-size:11px;color:{t['text']};}}"
                    f"QPushButton:hover{{background:{t['btn_hover']};border-color:{t['accent']};}}")
                rb.clicked.connect(lambda _, e=em: self.sig_react.emit(self.entry, e))
                rrow.addWidget(rb)
            rrow.addStretch()
            bl.addLayout(rrow)

        # Assemble — adaptive width: content-driven up to 60% of typical window
        # Check if this is a big-emoji message → make bubble transparent & compact
        _is_emoji_msg = (not m.image_data and m.msg_type != "file"
                         and not m.reply_to_text
                         and _is_emoji_only(m.text.strip()) if not m.image_data else False)
        if _is_emoji_msg and not reactions:
            # Pure emoji — no bubble background
            bw.setStyleSheet("background:transparent;border:none;")
            bw.setMaximumWidth(220)
        elif _is_emoji_msg and reactions:
            # Emoji with reactions — show subtle bubble so reaction fits
            bw.setStyleSheet(
                f"background:{t['bg3']};border-radius:16px;"
                f"border:1px solid {t['border']};")
            bw.setMaximumWidth(220)
        else:
            bw.setMaximumWidth(540)
        bw.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Preferred)
        bw.setMinimumWidth(60)
        if m.is_own:
            row.addStretch()
            row.addWidget(bw)
        else:
            row.addWidget(bw)
            row.addStretch()

        outer.addLayout(row)

    def _add_image(self, layout, m, t):
        """Render image inline — красивый rounded thumbnail с hover."""
        from PyQt6.QtGui import QPainter, QPainterPath, QColor, QBrush
        is_sticker = getattr(m, 'msg_type', '') == "sticker"
        pm = QPixmap()
        pm.loadFromData(m.image_data)
        if pm.isNull():
            err = QLabel(_L("⚠ Не удалось загрузить", "⚠ Failed to load", "⚠ 読み込み失敗"))
            err.setStyleSheet(f"color:{t['error'] if 'error' in t else '#e74c3c'};font-size:10px;")
            layout.addWidget(err)
            return

        if is_sticker:
            max_w, max_h, radius = 160, 160, 0
        else:
            # Адаптивный размер под соотношение сторон, макс 320x400
            iw, ih = pm.width(), pm.height()
            scale = min(320/max(iw,1), 400/max(ih,1), 1.0)
            max_w, max_h, radius = int(iw*scale), int(ih*scale), 14

        pm_s = pm.scaled(max_w, max_h,
                         Qt.AspectRatioMode.KeepAspectRatio,
                         Qt.TransformationMode.SmoothTransformation)
        w, h = pm_s.width(), pm_s.height()

        # Создаём округлённый pixmap
        rounded = QPixmap(w, h)
        rounded.fill(Qt.GlobalColor.transparent)
        painter = QPainter(rounded)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        path = QPainterPath()
        path.addRoundedRect(0, 0, w, h, radius, radius)
        painter.setClipPath(path)
        painter.drawPixmap(0, 0, pm_s)
        painter.end()

        class _ImgWidget(QLabel):
            def __init__(self_2, parent=None):
                super().__init__(parent)
                self_2._hovered = False
                self_2.setPixmap(rounded)
                self_2.setFixedSize(w, h)
                self_2.setCursor(Qt.CursorShape.PointingHandCursor)
                self_2.setToolTip(TR("img_hint"))
                self_2.setAttribute(Qt.WidgetAttribute.WA_Hover)

            def enterEvent(self_2, e):
                self_2._hovered = True; self_2.update()
            def leaveEvent(self_2, e):
                self_2._hovered = False; self_2.update()
            def paintEvent(self_2, e):
                super().paintEvent(e)
                if self_2._hovered and not is_sticker:
                    p2 = QPainter(self_2)
                    p2.setRenderHint(QPainter.RenderHint.Antialiasing)
                    # Затемнение при наведении
                    p2.setBrush(QBrush(QColor(0, 0, 0, 60)))
                    p2.setPen(Qt.PenStyle.NoPen)
                    path2 = QPainterPath()
                    path2.addRoundedRect(0, 0, w, h, radius, radius)
                    p2.setClipPath(path2)
                    p2.drawRect(0, 0, w, h)
                    # Иконка лупы по центру
                    p2.setPen(QColor(255, 255, 255, 200))
                    from PyQt6.QtGui import QFont as _QF
                    f = _QF(); f.setPixelSize(28); p2.setFont(f)
                    p2.drawText(0, 0, w, h, Qt.AlignmentFlag.AlignCenter, "🔍")
                    # Тонкая рамка
                    p2.setPen(QColor(255, 255, 255, 80))
                    p2.setBrush(Qt.BrushStyle.NoBrush)
                    p2.drawRoundedRect(1, 1, w-2, h-2, radius, radius)
                    p2.end()
            def mousePressEvent(self_2, e):
                from PyQt6.QtCore import Qt as _Qt
                if e.button() == _Qt.MouseButton.LeftButton:
                    ImageViewer(m.image_data, self).exec()
                elif e.button() == _Qt.MouseButton.RightButton:
                    from PyQt6.QtWidgets import QMenu
                    menu = QMenu(self_2)
                    menu.addAction(TR("btn_view"), lambda: ImageViewer(m.image_data, self).exec())
                    menu.addAction(_L("💾 Сохранить", "💾 Save", "💾 保存"), lambda: self._save_image(m))
                    menu.exec(e.globalPosition().toPoint())

        img_w = _ImgWidget()
        if not is_sticker:
            # Добавим тень через контейнер
            container = QWidget()
            container.setFixedSize(w + 4, h + 4)
            container.setStyleSheet("background:transparent;")
            img_w.setParent(container)
            img_w.move(2, 2)
            layout.addWidget(container)
        else:
            layout.addWidget(img_w)

    def _save_image(self, m):
        fname = (m.text or "image") + ".png"
        sp, _ = QFileDialog.getSaveFileName(self, "Сохранить изображение", fname,
            "Images (*.png *.jpg *.jpeg *.webp);;All (*)")
        if sp:
            pm = QPixmap(); pm.loadFromData(m.image_data); pm.save(sp)

    def _add_video(self, layout, m, t):
        """Красивая видео-карточка с превью, градиентом и кнопкой play."""
        from PyQt6.QtGui import QPainter, QPainterPath, QLinearGradient, QColor, QFont as _QFont
        fname = m.text or "video.mp4"
        dest  = RECEIVED_DIR / fname
        if m.image_data and not dest.exists():
            try: dest.write_bytes(m.image_data)
            except Exception: pass

        VW, VH, R = 300, 169, 14   # 16:9 thumbnail

        # Получаем превью через ffmpeg
        thumb_path = RECEIVED_DIR / (fname + "_thumb.jpg")
        if not thumb_path.exists() and dest.exists():
            try:
                subprocess.run(["ffmpeg","-y","-i",str(dest),"-vframes","1",
                    "-vf",f"scale={VW}:{VH}:force_original_aspect_ratio=increase,crop={VW}:{VH}",
                    str(thumb_path)],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3)
            except Exception: pass

        # Базовый pixmap (превью или заглушка)
        if thumb_path.exists():
            pm_raw = QPixmap(str(thumb_path)).scaled(VW, VH,
                Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                Qt.TransformationMode.SmoothTransformation)
        else:
            pm_raw = QPixmap(VW, VH); pm_raw.fill(QColor("#0d0d1a"))

        # Округляем + накладываем градиент снизу
        pm_card = QPixmap(VW, VH)
        pm_card.fill(Qt.GlobalColor.transparent)
        p = QPainter(pm_card)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        path = QPainterPath()
        path.addRoundedRect(0, 0, VW, VH, R, R)
        p.setClipPath(path)
        p.drawPixmap(0, 0, pm_raw)
        # Градиент снизу (для текста)
        grad = QLinearGradient(0, VH*0.5, 0, VH)
        grad.setColorAt(0, QColor(0,0,0,0))
        grad.setColorAt(1, QColor(0,0,0,180))
        from PyQt6.QtGui import QBrush
        p.setBrush(QBrush(grad)); p.setPen(Qt.PenStyle.NoPen)
        p.drawRect(0, 0, VW, VH)
        # Имя файла снизу
        p.setPen(QColor(255,255,255,220))
        f2 = _QFont(); f2.setPointSize(8); f2.setBold(True); p.setFont(f2)
        p.drawText(12, VH-12, VW-24, 16, Qt.AlignmentFlag.AlignLeft, fname)
        p.end()

        class _VideoWidget(QLabel):
            def __init__(self_2, parent=None):
                super().__init__(parent)
                self_2._hov = False
                self_2.setPixmap(pm_card)
                self_2.setFixedSize(VW, VH)
                self_2.setCursor(Qt.CursorShape.PointingHandCursor)
                self_2.setToolTip(TR("video_hint"))
                self_2.setAttribute(Qt.WidgetAttribute.WA_Hover)
            def enterEvent(self_2, e): self_2._hov = True;  self_2.update()
            def leaveEvent(self_2, e): self_2._hov = False; self_2.update()
            def paintEvent(self_2, e):
                super().paintEvent(e)
                p2 = QPainter(self_2)
                p2.setRenderHint(QPainter.RenderHint.Antialiasing)
                # Затемнение при hover
                if self_2._hov:
                    cp = QPainterPath(); cp.addRoundedRect(0,0,VW,VH,R,R)
                    p2.setClipPath(cp)
                    p2.setBrush(QBrush(QColor(0,0,0,50)))
                    p2.setPen(Qt.PenStyle.NoPen); p2.drawRect(0,0,VW,VH)
                # Кнопка play по центру
                cx, cy, cr = VW//2, VH//2 - 10, 28
                p2.setBrush(QBrush(QColor(0,0,0,160 if not self_2._hov else 200)))
                p2.setPen(Qt.PenStyle.NoPen)
                p2.drawEllipse(cx-cr, cy-cr, cr*2, cr*2)
                # Рамка круга
                p2.setPen(QColor(255,255,255,200 if self_2._hov else 160))
                p2.setBrush(Qt.BrushStyle.NoBrush)
                p2.drawEllipse(cx-cr, cy-cr, cr*2, cr*2)
                # Треугольник ▶
                from PyQt6.QtGui import QPolygon
                from PyQt6.QtCore import QPoint
                tri = QPolygon([QPoint(cx-8,cy-12), QPoint(cx-8,cy+12), QPoint(cx+14,cy)])
                p2.setBrush(QBrush(QColor(255,255,255,230)))
                p2.setPen(Qt.PenStyle.NoPen); p2.drawPolygon(tri)
                p2.end()
            def mousePressEvent(self_2, e):
                from PyQt6.QtCore import Qt as _Qt2
                if e.button() == _Qt2.MouseButton.LeftButton:
                    _open_media_smart(str(dest), self)
                elif e.button() == _Qt2.MouseButton.RightButton:
                    from PyQt6.QtWidgets import QMenu
                    menu = QMenu(self_2)
                    menu.addAction(TR("btn_play"), lambda: _open_media_smart(str(dest), self))
                    menu.addAction(_L("💾 Сохранить", "💾 Save", "💾 保存"), lambda: (
                        lambda sp: __import__('shutil').copy2(str(dest), sp) if sp else None
                    )(QFileDialog.getSaveFileName(None,"Сохранить",fname,"Video (*.mp4 *.mkv);;All (*)")[0]))
                    menu.exec(e.globalPosition().toPoint())

        layout.addWidget(_VideoWidget())

    def _add_file(self, layout, m, t):
        fname = m.text
        ext = Path(fname).suffix.lower() if fname else ""
        icons = {'.pdf':'📄','.doc':'📝','.docx':'📝','.txt':'📃',
                 '.zip':'📦','.rar':'📦','.7z':'📦','.tar':'📦',
                 '.mp3':'🎵','.ogg':'🎵','.wav':'🎵','.flac':'🎵',
                 '.mp4':'🎬','.avi':'🎬','.mkv':'🎬','.mov':'🎬',
                 '.py':'🐍','.js':'📜','.cpp':'⚙','.c':'⚙',
                 '.exe':'💾','.sh':'🖥'}
        icon = icons.get(ext, '📎')
        ff = QFrame()
        ff.setStyleSheet(
            f"QFrame{{background:rgba(60,80,120,102);"
            f"border:1px solid {t['border']};border-radius:10px;}}")
        ffl = QHBoxLayout(ff)
        ffl.setContentsMargins(10, 8, 10, 8)
        ffl.setSpacing(10)
        il = QLabel(icon)
        il.setStyleSheet("font-size:26px;background:transparent;")
        ffl.addWidget(il)
        ic = QVBoxLayout()
        ic.setSpacing(2)
        nl = QLabel(fname or "\u0424\u0430\u0439\u043b")
        nl.setStyleSheet(f"font-size:12px;font-weight:bold;color:{t['text']};background:transparent;")
        ic.addWidget(nl)
        dest = RECEIVED_DIR / fname if fname else None
        size_str = ""
        if dest and dest.exists():
            sz = dest.stat().st_size
            size_str = (f"{sz/1024/1024:.1f} \u041c\u0411" if sz > 1024*1024
                        else f"{sz/1024:.0f} \u041a\u0411" if sz > 1024 else f"{sz} \u0411")
        sl = QLabel(size_str or "\u0444\u0430\u0439\u043b \u043f\u043e\u043b\u0443\u0447\u0435\u043d")
        sl.setStyleSheet(f"font-size:9px;color:{t['text_dim']};background:transparent;")
        ic.addWidget(sl)
        ffl.addLayout(ic)
        ffl.addStretch()
        ob = QPushButton("\u041e\u0442\u043a\u0440\u044b\u0442\u044c")
        ob.setFixedHeight(26)
        ob.setCursor(Qt.CursorShape.PointingHandCursor)
        ob.setStyleSheet(
            f"QPushButton{{background:{t['accent']};color:white;border:none;"
            f"border-radius:6px;padding:2px 12px;font-size:10px;}}"
            f"QPushButton:hover{{background:{t['btn_hover']};}}")
        if dest and dest.exists():
            def _do_open(checked=False, _d=dest):
                if _d.suffix.lower() in MEDIA_EXTS:
                    _open_media_smart(str(_d), self)
                else:
                    QDesktopServices.openUrl(QUrl.fromLocalFile(str(_d)))
            ob.clicked.connect(_do_open)
        else:
            ob.setEnabled(False)
        ffl.addWidget(ob)
        layout.addWidget(ff)

    def _on_select_toggled(self, checked):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                if checked:
                    w._selected_ts.add(self.entry.ts)
                else:
                    w._selected_ts.discard(self.entry.ts)
                w._update_sel_bar()
                return

    def _enter_select_mode(self):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                w._select_mode = True
                w._selected_ts = {self.entry.ts}
                for b in w._bubbles:
                    if b.entry.is_own and b._chk:
                        b._chk.setVisible(True)
                        b._chk.setChecked(b.entry.ts in w._selected_ts)
                w._update_sel_bar()
                return

    def _on_select_toggled(self, checked):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                if checked: w._selected_ts.add(self.entry.ts)
                else: w._selected_ts.discard(self.entry.ts)
                w._update_sel_bar()
                return

    def _enter_select_mode(self):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                w._select_mode = True
                w._selected_ts = {self.entry.ts}
                for b in w._bubbles:
                    if b.entry.is_own and b._chk:
                        b._chk.setVisible(True)
                        b._chk.setChecked(b.entry.ts in w._selected_ts)
                w._update_sel_bar()
                return

    def _ctx_menu(self, pos):
        t = get_theme(S().theme)
        menu = QMenu(self)
        menu.setStyleSheet(
            f"QMenu{{background:{t['bg2']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:10px;padding:4px 0;}}"
            f"QMenu::item{{padding:8px 20px 8px 14px;font-size:12px;}}"
            f"QMenu::item:selected{{background:{t['accent']};color:white;"
            f"border-radius:6px;margin:1px 3px;}}"
            f"QMenu::separator{{height:1px;background:{t['border']};margin:3px 10px;}}")
        m = self.entry

        # Reaction strip
        rl = QLabel("  \u0411\u044b\u0441\u0442\u0440\u0430\u044f \u0440\u0435\u0430\u043a\u0446\u0438\u044f:")
        rl.setStyleSheet(f"color:{t['text_dim']};font-size:10px;padding:4px 14px 0 14px;")
        rla = QWidgetAction(menu)
        rla.setDefaultWidget(rl)
        menu.addAction(rla)

        rsw = QWidget()
        rsl = QHBoxLayout(rsw)
        rsl.setContentsMargins(10, 2, 10, 4)
        rsl.setSpacing(3)
        for em in self.QUICK_REACT:
            rb = QPushButton(em)
            rb.setFixedSize(38, 38)
            rb.setFont(QFont("Segoe UI Emoji,Noto Color Emoji,Apple Color Emoji", 16))
            rb.setStyleSheet(
                f"QPushButton{{font-size:18px;background:{t['bg3']};"
                f"border:1px solid {t['border']};border-radius:10px;"
                f"border-bottom:2px solid rgba(0,0,0,89);}}"
                f"QPushButton:hover{{background:{t['btn_hover']};"
                f"border-color:{t['accent']};}}")
            rb.clicked.connect(lambda _, e=em: (menu.close(), self.sig_react.emit(self.entry, e)))
            rsl.addWidget(rb)
        rsa = QWidgetAction(menu)
        rsa.setDefaultWidget(rsw)
        menu.addAction(rsa)
        menu.addSeparator()

        acts = [
            ("\u21a9  \u041e\u0442\u0432\u0435\u0442\u0438\u0442\u044c",    lambda: self.sig_reply.emit(m)),
            ("\u21aa  \u041f\u0435\u0440\u0435\u0441\u043b\u0430\u0442\u044c", lambda: self.sig_forward.emit(m)),
            ("\U0001f4cb  \u041a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u0442\u044c", lambda: self.sig_copy.emit(m.text)),
        ]
        if m.is_own:
            acts += [None,
                     ("\u270f  \u0418\u0437\u043c\u0435\u043d\u0438\u0442\u044c",  lambda: self.sig_edit.emit(m)),
                     ("\U0001f5d1  \u0423\u0434\u0430\u043b\u0438\u0442\u044c",   lambda: self.sig_delete.emit(m)),
                     ("\u2611  \u0412\u044b\u0431\u0440\u0430\u0442\u044c \u043d\u0435\u0441\u043a\u043e\u043b\u044c\u043a\u043e",  lambda: self._enter_select_mode())]
        if not m.is_own:
            acts.append(("\U0001f4e1  \u041f\u0438\u043d\u0433\u043e\u0432\u0430\u0442\u044c", lambda: self.sig_ping.emit(m.sender)))
        for item in acts:
            if item is None:
                menu.addSeparator()
            else:
                label, slot = item
                a = QAction(label, menu)
                a.triggered.connect(slot)
                menu.addAction(a)
        menu.exec(self.mapToGlobal(pos))


class ChatDisplay(QScrollArea):
    """Widget-based chat display with real bubbles, animations, image viewer."""
    request_forward = pyqtSignal(str, float)
    request_edit    = pyqtSignal(str, float)
    request_delete  = pyqtSignal(float)
    request_react   = pyqtSignal(float)
    request_react_with_emoji = pyqtSignal(float, str)
    request_reply   = pyqtSignal(str, float)

    def __init__(self, chat_id="public", parent=None):
        super().__init__(parent)
        self.chat_id      = chat_id
        self._messages    = []
        self._bubbles     = []
        self._read_map    = {}
        self._known_users = set()
        self._at_bottom   = True
        self._select_mode = False
        self._selected_ts = set()
        self._sel_bar = None
        self._select_mode  = False
        self._selected_ts  = set()
        self._sel_bar = None
        self._smooth_anim: QPropertyAnimation | None = None

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setContextMenuPolicy(Qt.ContextMenuPolicy.NoContextMenu)

        t = get_theme(S().theme)
        self.setStyleSheet(f"QScrollArea{{background:{t['bg']};border:none;}}")

        self._container = QWidget()
        self._container.setStyleSheet(f"background:{t['bg']};")
        self._container.setContextMenuPolicy(Qt.ContextMenuPolicy.NoContextMenu)
        self._lay = QVBoxLayout(self._container)
        self._lay.setContentsMargins(4, 8, 4, 8)
        self._lay.setSpacing(2)
        self._lay.addStretch()
        self._sel_bar_resize = None
        self.setWidget(self._container)
        self.verticalScrollBar().valueChanged.connect(self._on_scroll)

    def _smooth_scroll_to(self, target_val: int, duration: int = 350):
        """Animate scroll bar to target value smoothly."""
        sb = self.verticalScrollBar()
        # Stop previous animation safely — DeleteWhenStopped destroys C++ object
        # so we must check with try/except before calling any method on it
        if self._smooth_anim is not None:
            try:
                if self._smooth_anim.state() == QAbstractAnimation.State.Running:
                    self._smooth_anim.stop()
            except RuntimeError:
                pass  # C++ object already deleted — ignore
            self._smooth_anim = None

        anim = QPropertyAnimation(sb, b"value", self)
        anim.setDuration(duration)
        anim.setStartValue(sb.value())
        anim.setEndValue(target_val)
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        # Use KeepWhenStopped — keeps C++ object alive so we can call .state() later
        anim.start(QAbstractAnimation.DeletionPolicy.KeepWhenStopped)
        self._smooth_anim = anim

    def wheelEvent(self, event):
        """Smooth scroll on mouse wheel."""
        delta = event.angleDelta().y()
        if delta == 0:
            super().wheelEvent(event)
            return
        sb = self.verticalScrollBar()
        step = max(80, sb.singleStep() * 4)
        # If animation is running, start from its current end value
        current = sb.value()
        if self._smooth_anim is not None:
            try:
                if self._smooth_anim.state() == QAbstractAnimation.State.Running:
                    current = self._smooth_anim.endValue()
            except RuntimeError:
                self._smooth_anim = None
        target = current - int(delta / 120 * step)
        target = max(sb.minimum(), min(sb.maximum(), target))
        self._smooth_scroll_to(target, duration=180)
        event.accept()

    def _on_scroll(self, val):
        sb = self.verticalScrollBar()
        self._at_bottom = (val >= sb.maximum() - 60)
        if self._at_bottom:
            self._hide_jump_btn()

    def retheme(self):
        """Re-apply theme to all bubbles and container — called on theme change."""
        t = get_theme(S().theme)
        self.setStyleSheet(f"QScrollArea{{background:{t['bg']};border:none;}}")
        self._container.setStyleSheet(f"background:{t['bg']};")
        for b in self._bubbles:
            try:
                b.retheme(t)
            except Exception:
                pass

    def set_known_users(self, users):
        self._known_users = users

    def mark_read(self, msg_id):
        self._read_map[msg_id] = True
        t = get_theme(S().theme)
        for b in self._bubbles:
            if b.entry.msg_id == msg_id:
                tick_lbl = getattr(b, '_tick_lbl', None)
                if tick_lbl is not None:
                    tick_lbl.setText("✓✓")
                    tick_lbl.setStyleSheet(
                        f"font-size:9px;color:{t['accent']};background:transparent;")
                break

    # ── Search helpers ────────────────────────────────────────────────────
    _SEARCH_HL_STYLE = ";background:rgba(255,210,0,71);border-radius:3px;"

    def highlight_search(self, query: str):
        q = query.lower()
        for b in self._bubbles:
            lbl = getattr(b, '_text_lbl', None)
            if lbl is None: continue
            ss = lbl.styleSheet().replace(self._SEARCH_HL_STYLE, "")
            if q and q in b.entry.text.lower():
                ss += self._SEARCH_HL_STYLE
            lbl.setStyleSheet(ss)

    def clear_search_highlight(self):
        for b in self._bubbles:
            lbl = getattr(b, '_text_lbl', None)
            if lbl:
                lbl.setStyleSheet(
                    lbl.styleSheet().replace(self._SEARCH_HL_STYLE, ""))

    def get_search_matches(self, query: str) -> list:
        q = query.lower()
        return [i for i, b in enumerate(self._bubbles)
                if q in b.entry.text.lower()]

    def scroll_to_match(self, bubble_idx: int):
        if 0 <= bubble_idx < len(self._bubbles):
            self.ensureWidgetVisible(self._bubbles[bubble_idx], 0, 60)

    def _connect_bubble(self, b):
        b.sig_forward.connect(lambda m: self.request_forward.emit(m.text, m.ts))
        b.sig_edit.connect(lambda m: self.request_edit.emit(m.text, m.ts))
        b.sig_delete.connect(lambda m: self.request_delete.emit(m.ts))
        b.sig_react.connect(lambda m, e: self.request_react_with_emoji.emit(m.ts, e))
        b.sig_reply.connect(lambda m: self.request_reply.emit(m.text, m.ts))
        b.sig_copy.connect(QApplication.clipboard().setText)
        b.sig_ping.connect(lambda u: self.add_system(f"\U0001f4e1 \u041f\u0438\u043d\u0433 \u2192 {u}"))

    def add_message(self, sender, text, ts, is_own, color="#E0E0E0",
                    emoji="", msg_type="public", image_data=None,
                    is_edited=False, is_forwarded=False, forwarded_from="",
                    reply_to_text="", chat_id="", animate=True):
        entry = MessageEntry(
            sender=sender, text=text, ts=ts, is_own=is_own,
            color=color, emoji=emoji, msg_type=msg_type,
            image_data=image_data, is_edited=is_edited,
            is_forwarded=is_forwarded, forwarded_from=forwarded_from,
            reply_to_text=reply_to_text, chat_id=chat_id or self.chat_id)
        self._messages.append(entry)
        self._append_bubble(entry, animate=animate)

    def _append_bubble(self, entry, animate=False):
        b = MessageBubble(entry, self._read_map, self._known_users)
        self._connect_bubble(b)
        self._bubbles.append(b)
        self._lay.insertWidget(self._lay.count() - 1, b)
        if animate:
            self._anim_in(b, entry.is_own)
        # Use two ticks: first tick lets Qt calculate widget size,
        # second tick does the scroll (avoids scroll to wrong position)
        if self._at_bottom or entry.is_own:
            # Wait for layout to settle (one paint event), then scroll smoothly
            def _do_scroll():
                sb = self.verticalScrollBar()
                if abs(sb.value() - sb.maximum()) > 5:
                    self._smooth_scroll_to(sb.maximum(), 150)
                else:
                    sb.setValue(sb.maximum())
                self._hide_jump_btn()
            QTimer.singleShot(60, _do_scroll)
        else:
            QTimer.singleShot(50, self._show_jump_btn)

    def retheme(self, t: dict):
        """Re-apply bubble stylesheet when theme changes."""
        m = self.entry
        if m.is_system:
            return
        # Find the msg_bubble widget child
        for child in self.findChildren(QWidget):
            if child.objectName() == "msg_bubble":
                if m.is_own:
                    child.setStyleSheet(
                        "QWidget#msg_bubble{"
                        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                        f"stop:0 {t['msg_own']},stop:1 {t['accent2']});"
                        "border-radius:14px 14px 4px 14px;"
                        f"border:1px solid {t['accent']};"
                        "border-bottom:2px solid rgba(0,0,0,63);}}")
                else:
                    child.setStyleSheet(
                        "QWidget#msg_bubble{"
                        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                        f"stop:0 {t['msg_other']},stop:1 {t['bg3']});"
                        "border-radius:14px 14px 14px 4px;"
                        f"border:1px solid {t['border']};"
                        "border-bottom:2px solid rgba(0,0,0,51);}}")
                child.update()
                break
        self.update()

    def _anim_in(self, bubble, is_own):
        """Плавное появление пузыря: только fade-in (slide ломает layout)."""
        from PyQt6.QtWidgets import QGraphicsOpacityEffect
        eff = QGraphicsOpacityEffect(bubble)
        eff.setOpacity(0.0)
        bubble.setGraphicsEffect(eff)
        fade = QPropertyAnimation(eff, b"opacity", bubble)
        fade.setDuration(260)
        fade.setStartValue(0.0)
        fade.setEndValue(1.0)
        fade.setEasingCurve(QEasingCurve.Type.OutCubic)
        def _done():
            bubble.setGraphicsEffect(None)
            bubble.update()
        fade.finished.connect(_done)
        fade.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)

    def add_system(self, text):
        entry = MessageEntry(text=text, ts=time.time(), is_system=True)
        self._messages.append(entry)
        self._append_bubble(entry)

    def _add_widget_bubble(self, widget: QWidget, is_own: bool = True,
                           replace_widget: QWidget | None = None) -> QWidget:
        """Добавить произвольный виджет как пузырь. Возвращает wrapper."""
        from PyQt6.QtWidgets import QHBoxLayout, QWidget as _QW
        wrapper = _QW()
        wrapper.setStyleSheet("background:transparent;")
        row = QHBoxLayout(wrapper)
        row.setContentsMargins(8, 2, 8, 2)
        if is_own:
            row.addStretch()
            row.addWidget(widget)
        else:
            row.addWidget(widget)
            row.addStretch()
        if replace_widget is not None:
            # Заменяем существующий wrapper — находим его индекс
            idx = self._lay.indexOf(replace_widget)
            if idx >= 0:
                replace_widget.hide()
                replace_widget.deleteLater()
                self._lay.insertWidget(idx, wrapper)
                return wrapper
        self._lay.insertWidget(self._lay.count() - 1, wrapper)
        QTimer.singleShot(60, lambda: self.verticalScrollBar().setValue(
            self.verticalScrollBar().maximum()))
        return wrapper


    def add_upload_progress(self, fname: str, fsize: int, prog_id: str):
        """Show indeterminate upload progress card."""
        t = get_theme(S().theme)
        container = QWidget()
        container.setObjectName(f"prog_{prog_id}")
        cl = QHBoxLayout(container); cl.setContentsMargins(0,2,4,2)
        cl.addStretch()
        inner = QWidget()
        inner.setMaximumWidth(300)
        inner.setStyleSheet(f"QWidget{{background:{t['bg3']};border:1px solid {t['border']};"
            "border-radius:10px;}}")
        il = QVBoxLayout(inner); il.setContentsMargins(10,8,10,8); il.setSpacing(4)
        size_s = (f"{fsize/1024/1024:.1f} МБ" if fsize>1048576
                  else f"{fsize/1024:.0f} КБ" if fsize>1024 else f"{fsize} Б")
        lbl = QLabel(f"📤 {fname}  ({size_s})")
        lbl.setStyleSheet(f"font-size:11px;color:{t['text']};background:transparent;")
        il.addWidget(lbl)
        bar = QProgressBar(); bar.setRange(0,0); bar.setFixedHeight(5)
        bar.setTextVisible(False)
        bar.setStyleSheet(f"QProgressBar{{background:{t['bg2']};border-radius:3px;border:none;}}"
            f"QProgressBar::chunk{{background:{t['accent']};border-radius:3px;}}")
        il.addWidget(bar)
        cl.addWidget(inner)
        self._lay.insertWidget(self._lay.count()-1, container)
        if not hasattr(self, '_prog_widgets'): self._prog_widgets = {}
        self._prog_widgets[prog_id] = container
        QTimer.singleShot(40, self._scroll_to_bottom_if_at_bottom)

    def remove_progress(self, prog_id: str):
        pw = getattr(self, '_prog_widgets', {})
        w = pw.pop(prog_id, None)
        if w:
            self._lay.removeWidget(w)
            w.deleteLater()

    def update_reactions(self, ts):
        for i, b in enumerate(self._bubbles):
            if not b.entry.is_system and abs(b.entry.ts - ts) < 0.01:
                idx = self._lay.indexOf(b)
                entry = b.entry
                self._lay.removeWidget(b)
                b.deleteLater()
                nb = MessageBubble(entry, self._read_map, self._known_users)
                self._connect_bubble(nb)
                self._lay.insertWidget(idx, nb)
                self._bubbles[i] = nb
                break

    def _redraw_all(self):
        for b in self._bubbles:
            self._lay.removeWidget(b)
            b.deleteLater()
        self._bubbles.clear()
        msgs = list(self._messages)
        self._messages.clear()
        for m in msgs:
            if m.is_system:
                self.add_system(m.text)
            else:
                self.add_message(m.sender, m.text, m.ts, m.is_own, m.color,
                                 m.emoji, m.msg_type, m.image_data, m.is_edited,
                                 m.is_forwarded, m.forwarded_from, m.reply_to_text, m.chat_id)

    def edit_message(self, ts, new_text):
        for m in self._messages:
            if abs(m.ts - ts) < 0.01 and not m.is_system:
                m.text = new_text
                m.is_edited = True
                break
        self._redraw_all()

    def delete_message(self, ts):
        self._messages = [m for m in self._messages
                          if m.is_system or abs(m.ts - ts) >= 0.01]
        self._redraw_all()

    def add_reaction_update(self, ts, emoji, username, added):
        if added:
            REACTIONS.add(self.chat_id, ts, emoji, username)
        else:
            REACTIONS.remove(self.chat_id, ts, emoji, username)
        self.update_reactions(ts)

    def load_history(self, messages):
        for m in messages[-80:]:
            if m.get("system"):
                self.add_system(m["text"])
            elif m.get("msg_type") == "poll":
                # Восстанавливаем опрос из истории
                try:
                    poll_id  = m.get("poll_id", f"poll_{m.get('ts',0)}")
                    question = m.get("text", "?")
                    options  = m.get("poll_options") or []
                    votes    = m.get("poll_votes") or {o: [] for o in options}
                    is_own   = m.get("is_own", False)
                    if not options:
                        continue
                    self._polls[poll_id] = {
                        "question": question, "options": options,
                        "votes": votes, "creator": m.get("sender", "")}
                    self._add_poll_bubble(poll_id, question, options, is_own=is_own)
                except Exception:
                    continue  # пропускаем битые poll записи
            else:
                # Restore image/video data from disk if stored as path
                img_data = None
                msg_type = m.get("msg_type", "text")

                if msg_type in ("image", "video", "sticker", "file"):
                    # Try sticker_path first (saved to RECEIVED_DIR)
                    sp = m.get("sticker_path") or m.get("file_path")
                    if sp:
                        try:
                            p = Path(sp)
                            if p.exists():
                                img_data = p.read_bytes()
                        except Exception:
                            pass
                    # Fallback to base64 in history
                    if img_data is None:
                        b64 = m.get("image_b64","") or m.get("file_b64","")
                        if b64:
                            try:
                                import base64 as _b64
                                img_data = _b64.b64decode(b64)
                            except Exception:
                                pass

                self.add_message(
                    m.get("sender","?"), m.get("text",""),
                    m.get("ts", time.time()), m.get("is_own", False),
                    m.get("color","#E0E0E0"), m.get("emoji",""),
                    is_edited=m.get("is_edited", False),
                    is_forwarded=m.get("is_forwarded", False),
                    forwarded_from=m.get("forwarded_from", ""),
                    reply_to_text=m.get("reply_to_text", ""),
                    msg_type=msg_type,
                    image_data=img_data,
                    animate=False)

    def clear(self):
        for b in self._bubbles:
            self._lay.removeWidget(b)
            b.deleteLater()
        self._bubbles.clear()
        self._messages.clear()

    def _scroll_to_bottom(self):
        """Unconditional scroll to bottom (called by user action)."""
        sb = self.verticalScrollBar()
        sb.setValue(sb.maximum())
        self._hide_jump_btn()

    def _scroll_to_bottom_if_at_bottom(self):
        """Auto-scroll only if user is near bottom (uses tracked _at_bottom flag)."""
        if getattr(self, '_at_bottom', True):
            # Re-check with fresh layout values
            sb = self.verticalScrollBar()
            sb.setValue(sb.maximum())
            self._hide_jump_btn()
        else:
            self._show_jump_btn()

    def _show_jump_btn(self):
        """Show animated ↓ button when new messages arrive off-screen."""
        if not hasattr(self, '_jump_btn'):
            self._jump_btn = None
        if self._jump_btn is None:
            t = get_theme(S().theme)
            self._jump_btn = QPushButton(TR("new_messages"), self.viewport())
            self._jump_btn.setCursor(Qt.CursorShape.PointingHandCursor)
            self._jump_btn.setStyleSheet(
                f"QPushButton{{background:{t['accent']};color:white;"
                "border-radius:14px;padding:0 14px;font-size:11px;"
                "font-weight:bold;border:none;"
                "border-bottom:2px solid rgba(0,0,0,102);}}"
                "")
            self._jump_btn.setFixedHeight(28)
            self._jump_btn.clicked.connect(self._scroll_to_bottom)
        vp = self.viewport()
        self._jump_btn.adjustSize()
        bw = self._jump_btn.width()
        self._jump_btn.setGeometry(vp.width() // 2 - bw // 2,
                                   vp.height() - 46, bw, 28)
        self._jump_btn.show()
        self._jump_btn.raise_()

    def _update_sel_bar(self):
        t = get_theme(S().theme)
        n = len(self._selected_ts)
        if self._sel_bar:
            try:
                self._lay.removeWidget(self._sel_bar)
            except Exception:
                pass
            self._sel_bar.deleteLater()
            self._sel_bar = None
        if n == 0:
            self._select_mode = False
            for b in self._bubbles:
                if b._chk:
                    b._chk.setVisible(False)
                    b._chk.setChecked(False)
            return
        from PyQt6.QtWidgets import QWidget as _QW, QHBoxLayout as _QHL, QLabel as _QL, QPushButton as _QPB
        bar = _QW()
        bar.setStyleSheet(f"background:{t['accent']};border-radius:10px;margin:4px 12px;")
        bl = _QHL(bar)
        bl.setContentsMargins(12, 6, 12, 6)
        cancel_btn = _QPB(_L("\u2715 \u041e\u0442\u043c\u0435\u043d\u0430", "\u2715 Cancel", "\u2715 \u30ad\u30e3\u30f3\u30bb\u30eb"))
        cancel_btn.clicked.connect(self._cancel_select)
        bl.addWidget(cancel_btn)
        bl.addStretch()
        lbl = _QL(_L(f"\u0412\u044b\u0431\u0440\u0430\u043d\u043e: {n}", f"Selected: {n}", f"\u9078\u629e: {n}"))
        lbl.setStyleSheet("color:white;font-weight:bold;background:transparent;")
        bl.addWidget(lbl)
        bl.addStretch()
        del_btn = _QPB(_L(f"\U0001f5d1 \u0423\u0434\u0430\u043b\u0438\u0442\u044c ({n})", f"\U0001f5d1 Delete ({n})", f"\U0001f5d1 \u524a\u9664 ({n})"))
        del_btn.setStyleSheet("QPushButton{background:#CC2222;color:white;border-radius:6px;padding:4px 12px;font-weight:bold;}"
                              "QPushButton:hover{background:#FF3333;}")
        del_btn.clicked.connect(self._delete_selected)
        bl.addWidget(del_btn)
        self._sel_bar = bar
        self._lay.insertWidget(0, bar)
        self.verticalScrollBar().setValue(0)  # scroll to top to show bar

    def _cancel_select(self):
        self._selected_ts.clear()
        self._select_mode = False
        for b in self._bubbles:
            if b._chk:
                b._chk.setVisible(False)
                b._chk.setChecked(False)
        if self._sel_bar:
            try:
                self._lay.removeWidget(self._sel_bar)
            except Exception:
                pass
            self._sel_bar.deleteLater()
            self._sel_bar = None

    def _delete_selected(self):
        count = len(self._selected_ts)
        if count == 0:
            return
        from PyQt6.QtWidgets import QMessageBox
        reply = QMessageBox.question(
            self, _L("\u0423\u0434\u0430\u043b\u0435\u043d\u0438\u0435", "Delete", "\u524a\u9664"),
            _L(f"\u0423\u0434\u0430\u043b\u0438\u0442\u044c {count} \u0441\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0439?", f"Delete {count} messages?", f"{count}\u4ef6\u306e\u30e1\u30c3\u30bb\u30fc\u30b8\u3092\u524a\u9664\u3057\u307e\u3059\u304b\uff1f"),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            for ts in list(self._selected_ts):
                self.request_delete.emit(ts)
        self._selected_ts.clear()
        self._cancel_select()

    def _update_sel_bar(self):
        t = get_theme(S().theme)
        n = len(self._selected_ts)
        if self._sel_bar:
            self._sel_bar.hide()
            self._sel_bar.deleteLater()
            self._sel_bar = None
        if n == 0:
            self._select_mode = False
            for b in self._bubbles:
                if b._chk: b._chk.setVisible(False); b._chk.setChecked(False)
            return
        from PyQt6.QtWidgets import QWidget as W, QHBoxLayout as HL, QLabel as L, QPushButton as B
        bar = W(self.viewport())  # крепим к viewport — всегда сверху видимой области
        bar.setFixedHeight(44)
        bar.setStyleSheet(f"background:{t['accent']};border-radius:10px;")
        bl = HL(bar); bl.setContentsMargins(12,6,12,6)
        cancel_btn = B(_L("\u2715", "\u2715", "\u2715"))
        cancel_btn.setFixedSize(28,28)
        cancel_btn.setStyleSheet("QPushButton{background:rgba(255,255,255,40);color:white;border-radius:14px;font-weight:bold;border:none;}"
                                 "QPushButton:hover{background:rgba(255,255,255,70);}")
        cancel_btn.clicked.connect(self._cancel_select)
        bl.addWidget(cancel_btn)
        lbl = L(_L(f"\u0412\u044b\u0431\u0440\u0430\u043d\u043e: {n}", f"Selected: {n}", f"\u9078\u629e: {n}"))
        lbl.setStyleSheet("color:white;font-weight:bold;background:transparent;")
        bl.addWidget(lbl)
        bl.addStretch()
        del_btn = B(_L(f"\U0001f5d1 \u0423\u0434\u0430\u043b\u0438\u0442\u044c ({n})", f"\U0001f5d1 Delete ({n})", f"\U0001f5d1 \u524a\u9664 ({n})"))
        del_btn.setStyleSheet("QPushButton{background:#CC2222;color:white;border-radius:6px;padding:4px 12px;font-weight:bold;}"
                              "QPushButton:hover{background:#FF3333;}")
        del_btn.clicked.connect(self._delete_selected)
        bl.addWidget(del_btn)
        bar.move(6, 6)
        bar.resize(self.viewport().width() - 12, 44)
        bar.show()
        bar.raise_()
        self._sel_bar = bar
        # Ресайз при изменении ширины
        def _on_resize(event=None):
            if self._sel_bar:
                self._sel_bar.resize(self.viewport().width() - 12, 44)
        self.viewport().installEventFilter(self)
        self._sel_bar_resize = _on_resize

    def _cancel_select(self):
        self._selected_ts.clear(); self._select_mode = False
        for b in self._bubbles:
            if b._chk: b._chk.setVisible(False); b._chk.setChecked(False)
        if self._sel_bar:
            try: self._lay.removeWidget(self._sel_bar)
            except: pass
            self._sel_bar.deleteLater(); self._sel_bar = None

    def _delete_selected(self):
        count = len(self._selected_ts)
        if count == 0: return
        from PyQt6.QtWidgets import QMessageBox
        if QMessageBox.question(self, _L("\u0423\u0434\u0430\u043b\u0435\u043d\u0438\u0435","Delete","\u524a\u9664"),
            _L(f"\u0423\u0434\u0430\u043b\u0438\u0442\u044c {count} \u0441\u043e\u043e\u0431\u0449\u0435\u043d\u0438\u0439?",f"Delete {count} messages?",f"{count}\u4ef6\u524a\u9664?"),
            QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) == QMessageBox.StandardButton.Yes:
            for ts in list(self._selected_ts): 
                self.delete_message(ts)
                # Отправляем delete в сеть
                import gdf_network
                w = self
                while w and not hasattr(w, 'net'):
                    w = w.parent()
                if w and hasattr(w, 'net'):
                    chat_id = self.chat_id
                    to_ip = None
                    if hasattr(w, '_current_peer') and w._current_peer:
                        to_ip = w._current_peer.get("ip")
                    w.net.send_message_delete(to_ip, chat_id, ts)
        self._selected_ts.clear(); self._cancel_select()

    def _hide_jump_btn(self):
        if hasattr(self, '_jump_btn') and self._jump_btn:
            self._jump_btn.hide()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        # Re-position jump button on resize
        if hasattr(self, '_jump_btn') and self._jump_btn and self._jump_btn.isVisible():
            vp = self.viewport()
            bw = self._jump_btn.width()
            self._jump_btn.setGeometry(vp.width() // 2 - bw // 2,
                                       vp.height() - 46, bw, 28)



# ═══════════════════════════════════════════════════════════════════════════
#  PEER LIST WIDGET  (with hover cards)
# ═══════════════════════════════════════════════════════════════════════════
class PeerListItem(QWidget):
    """Custom widget for a peer in the list."""

    def __init__(self, peer: dict, parent=None):
        super().__init__(parent)
        self.peer = peer
        self._hover_card: UserHoverCard | None = None
        self.setMouseTracking(True)
        self._build()

    def _build(self):
        lay = QHBoxLayout(self)
        lay.setContentsMargins(6,4,6,4)
        lay.setSpacing(8)

        # Avatar
        av_b64 = self.peer.get("avatar_b64","")
        if av_b64:
            try:
                pm = base64_to_pixmap(av_b64)
                circ = make_circle_pixmap(pm, 36)
            except Exception:
                circ = default_avatar(self.peer.get("username","?"), 36)
        else:
            circ = default_avatar(self.peer.get("username","?"), 36)

        av = QLabel()
        av.setPixmap(circ)
        av.setFixedSize(36,36)
        lay.addWidget(av)

        # Info
        info = QVBoxLayout()
        info.setSpacing(1)

        color = self.peer.get("nickname_color","#E0E0E0")
        emoji = self.peer.get("custom_emoji","")
        ct    = self.peer.get("conn_type","LAN")
        ct_icon = "🔒" if ct == "VPN" else "🏠"

        name = QLabel(f"<b style='color:{color}'>{self.peer.get('username','?')}</b> {emoji}")
        name.setTextFormat(Qt.TextFormat.RichText)
        info.addWidget(name)

        _ip_disp = display_id(self.peer.get("ip",""))
        loyalty = int(self.peer.get("loyalty_months", 0))
        loyalty_str = f"  ❤{loyalty}" if loyalty > 0 else ""
        sub = QLabel(f"{ct_icon} {ct}  •  {_ip_disp}{loyalty_str}")
        t = get_theme(S().theme)
        sub.setStyleSheet(f"color: {t['text_dim']}; font-size: 9px;")
        if S().safe_mode:
            sub.setToolTip(f_L("GoidaID mode active. Real IP hidden.", "GoidaID mode active. Real IP hidden.", "GoidaIDモード有効"))
        elif loyalty > 0:
            lang = S().language
            sub.setToolTip(f"'Online for' "
                           f"{loyalty} {'мес. подряд' if lang=='ru' else 'months in a row'}")
        info.addWidget(sub)

        lay.addLayout(info)
        lay.addStretch()

        # Legacy / modded badge BEFORE status dot
        peer_ver   = str(self.peer.get("version", APP_VERSION))
        peer_proto = int(self.peer.get("protocol_version", PROTOCOL_VERSION))
        is_legacy  = peer_proto < PROTOCOL_COMPAT          # proto too old
        # Detect modded: version string contains a non-standard suffix (e.g. "1.8.0-mod")
        import re as _re
        is_modded  = bool(_re.search(r"[-+][a-zA-Z]", peer_ver))

        if is_legacy:
            legacy_badge = QLabel("🕰")
            legacy_badge.setStyleSheet("font-size:11px;background:transparent;")
            legacy_badge.setToolTip(
                f"Устаревший клиент GoidaPhone (протокол v{peer_proto})\n"
                f"Некоторые функции могут не работать.")
            lay.addWidget(legacy_badge)
        elif is_modded:
            mod_badge = QLabel("⚠")
            mod_badge.setStyleSheet("font-size:11px;color:#FFD700;background:transparent;")
            mod_badge.setToolTip(
                f"Модифицированная версия GoidaPhone ({peer_ver})\n"
                f"Поведение клиента может отличаться от оригинала.\n"
                f"Будьте осторожны при передаче файлов.")
            lay.addWidget(mod_badge)

        # Status dot — colour by presence status
        status = self.peer.get("status", "online")
        STATUS_COLORS = {
            "online": t.get("online","#4CAF50"),
            "away":   "#FFD700",
            "busy":   "#FF6B6B",
            "dnd":    "#9E9E9E",
        }
        STATUS_TIPS = {
            "online": "Онлайн",
            "away":   "Отошёл",
            "busy":   "Занят",
            "dnd":    "Не беспокоить",
        }
        dot_col = STATUS_COLORS.get(status, t.get("online","#4CAF50"))
        dot_sym = "●" if status != "dnd" else "⊘"
        dot = QLabel(dot_sym)
        dot.setStyleSheet(f"color: {dot_col}; font-size: 14px;")
        dot.setToolTip(STATUS_TIPS.get(status, "Онлайн"))
        lay.addWidget(dot)

    def enterEvent(self, event):
        QTimer.singleShot(600, self._show_hover)
        super().enterEvent(event)

    def leaveEvent(self, event):
        if self._hover_card:
            self._hover_card.hide()
            self._hover_card = None
        super().leaveEvent(event)

    def _show_hover(self):
        if not self.underMouse():
            return
        self._hover_card = UserHoverCard(self.peer)
        gp = self.mapToGlobal(QPoint(self.width() + 5, 0))
        self._hover_card.move(gp)
        self._hover_card.show()

# ═══════════════════════════════════════════════════════════════════════════
#  PEER PANEL  (left sidebar)
# ═══════════════════════════════════════════════════════════════════════════
class PeerPanel(QWidget):
    chat_requested  = pyqtSignal(dict)   # peer
    call_requested  = pyqtSignal(dict)   # peer
    group_selected  = pyqtSignal(str)    # group_id
    invite_message_requested = pyqtSignal(str, object, str)  # dest_type, dest_data, invite_json

    def __init__(self, net: NetworkManager, parent=None):
        super().__init__(parent)
        self.net = net
        self._peers: dict[str, dict] = {}
        self._setup()

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0,0,0,0)
        lay.setSpacing(0)

        tabs = QTabWidget()
        tabs.setDocumentMode(True)

        # ── Users tab ──
        users_w = QWidget()
        ul = QVBoxLayout(users_w)
        ul.setContentsMargins(4,4,4,4)
        ul.setSpacing(4)

        self._search = QLineEdit()
        self._search.setPlaceholderText(_L("🔍 Поиск...", "🔍 Search...", "🔍 検索..."))
        self._search.textChanged.connect(self._filter)
        ul.addWidget(self._search)

        self._list = QListWidget()
        self._list.setSpacing(1)
        self._list.itemDoubleClicked.connect(self._on_double)
        ul.addWidget(self._list)

        # context menu
        self._list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._list.customContextMenuRequested.connect(self._ctx_menu)

        self._status_lbl = QLabel(_L("Online: 0", "Online: 0", "オンライン: 0"))
        t = get_theme(S().theme)
        self._status_lbl.setStyleSheet(f"color:{t['text_dim']}; font-size:9px; padding:2px 6px;")
        ul.addWidget(self._status_lbl)

        tabs.addTab(users_w, TR("tab_peer_list"))

        # ── Groups tab ──
        groups_w = QWidget()
        gl = QVBoxLayout(groups_w)
        gl.setContentsMargins(4,4,4,4)
        gl.setSpacing(4)

        self._groups_list = QListWidget()
        self._groups_list.itemDoubleClicked.connect(self._on_group_double)
        self._groups_list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._groups_list.customContextMenuRequested.connect(self._group_ctx_menu)
        gl.addWidget(self._groups_list)

        grp_btns = QHBoxLayout()
        new_group_btn = QPushButton(_L("➕ Создать группу", "➕ Create group", "➕ グループ作成"))
        new_group_btn.clicked.connect(self._create_group)
        grp_btns.addWidget(new_group_btn)
        fav_btn = QPushButton(_L("⭐ Избранное", "⭐ Favorites", "⭐ お気に入り"))
        fav_btn.clicked.connect(self._open_favorites)
        grp_btns.addWidget(fav_btn)
        gl.addLayout(grp_btns)

        tabs.addTab(groups_w, TR("tab_group_list"))

        lay.addWidget(tabs)
        self._refresh_groups()

    # ── peer management ─────────────────────────────────────────────────
    def add_peer(self, peer: dict):
        ip = peer.get("ip","")
        if not ip or ip == get_local_ip():
            return
        self._peers[ip] = peer
        self._rebuild_list()

    def remove_peer(self, ip: str):
        self._peers.pop(ip, None)
        self._rebuild_list()

    def _rebuild_list(self):
        q = self._search.text().lower()
        self._list.clear()
        count = 0
        pinned_set = set(json.loads(S().get("pinned_peers", "[]", t=str)))
        # Pinned peers first
        sorted_peers = sorted(
            self._peers.items(),
            key=lambda kv: (0 if kv[0] in pinned_set else 1, kv[1].get("username","").lower())
        )
        t = get_theme(S().theme)
        for ip, peer in sorted_peers:
            name = peer.get("username","")
            if q and q not in name.lower() and q not in ip:
                continue
            item = QListWidgetItem(self._list)
            item.setSizeHint(QSize(0, 52))
            item.setData(Qt.ItemDataRole.UserRole, peer)
            widget = PeerListItem(peer)
            if ip in pinned_set:
                item.setBackground(QColor(t.get("accent","#6c63ff") + "22"))
            self._list.setItemWidget(item, widget)
            count += 1
        self._status_lbl.setText(f'Online: {count}')

    def _is_pinned(self, ip: str) -> bool:
        return ip in set(json.loads(S().get("pinned_peers", "[]", t=str)))

    def _toggle_pin(self, ip: str):
        import json as _j
        pinned = list(_j.loads(S().get("pinned_peers", "[]", t=str)))
        if ip in pinned:
            pinned.remove(ip)
            play_system_sound("unpin")
        else:
            pinned.insert(0, ip)
            play_system_sound("pin")
        S().set("pinned_peers", _j.dumps(pinned))
        self._rebuild_list()

    def _filter(self, _):
        self._rebuild_list()

    def _selected_peer(self) -> dict | None:
        items = self._list.selectedItems()
        if items:
            return items[0].data(Qt.ItemDataRole.UserRole)
        return None

    def _on_double(self, item):
        peer = item.data(Qt.ItemDataRole.UserRole)
        if peer:
            self.chat_requested.emit(peer)

    def _on_select_toggled(self, checked):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                if checked:
                    w._selected_ts.add(self.entry.ts)
                else:
                    w._selected_ts.discard(self.entry.ts)
                w._update_sel_bar()
                return

    def _enter_select_mode(self):
        w = self
        while w:
            w = w.parent()
            if hasattr(w, '_selected_ts'):
                w._select_mode = True
                w._selected_ts = {self.entry.ts}
                for b in w._bubbles:
                    if b.entry.is_own and b._chk:
                        b._chk.setVisible(True)
                        b._chk.setChecked(b.entry.ts in w._selected_ts)
                w._update_sel_bar()
                return

    def _ctx_menu(self, pos):
        peer = self._selected_peer()
        if not peer:
            return
        ip = peer.get("ip","")
        pinned = self._is_pinned(ip)
        menu = QMenu(self)
        def _open_profile():
            _show_peer_profile_overlay(peer, self)

        for label, cb in [
            ("👤 Профиль",          _open_profile),
            ("💬 Личный чат",       lambda: self.chat_requested.emit(peer)),
            ("📞 Позвонить",        lambda: self.call_requested.emit(peer)),
            ("📎 Отправить файл",   lambda: self._send_file(peer)),
            ("➕ Добавить в группу",lambda: self._add_to_group(peer)),
            ("📌 Открепить" if pinned else "📌 Закрепить чат",
                                    lambda: self._toggle_pin(ip)),
        ]:
            a = QAction(label, self)
            a.triggered.connect(cb)
            menu.addAction(a)
        menu.exec(self._list.mapToGlobal(pos))

    def _send_file(self, peer):
        path, _ = QFileDialog.getOpenFileName(self, "Select file")
        if path:
            self.net.send_file(peer["ip"], path)

    def _add_to_group(self, peer):
        groups = GROUPS.list_for(get_local_ip())
        if not groups:
            QMessageBox.information(self, "Группы", "Сначала создайте группу.")
            return
        items = [f"{g['name']} ({gid})" for gid, g in groups]
        choice, ok = QInputDialog.getItem(self, "Добавить в группу", "Группа:", items, editable=False)
        if ok and choice:
            gid = choice.split("(")[-1].rstrip(")")
            GROUPS.add_member(gid, peer["ip"])
            self.net.send_group_invite(gid, GROUPS.get(gid).get("name","?"), peer["ip"])
            self._refresh_groups()

    # ── groups ──────────────────────────────────────────────────────────
    def _refresh_groups(self):
        self._groups_list.clear()
        pin_groups = set(json.loads(S().get("pinned_groups", "[]", t=str)))
        t = get_theme(S().theme)
        all_groups = GROUPS.list_for(get_local_ip())
        sorted_groups = sorted(all_groups, key=lambda x: (0 if x[0] in pin_groups else 1,
                                                           x[1].get("name","").lower()))
        for gid, g in sorted_groups:
            member_count = len(g.get("members",[]))
            pin_icon = "📌 " if gid in pin_groups else ""
            item = QListWidgetItem(f"{pin_icon}📂 {g['name']}  ({member_count} уч.)")
            item.setData(Qt.ItemDataRole.UserRole, gid)
            if gid in pin_groups:
                item.setBackground(QColor(t.get("accent","#6c63ff") + "22"))
            self._groups_list.addItem(item)

    def _on_group_double(self, item):
        gid = item.data(Qt.ItemDataRole.UserRole)
        if gid:
            self.group_selected.emit(gid)

    def _open_favorites(self):
        self.group_selected.emit("__favorites__")

    def _group_ctx_menu(self, pos):
        item = self._groups_list.itemAt(pos)
        t = get_theme(S().theme)
        menu = QMenu(self)
        menu.setStyleSheet(
            f"QMenu{{background:{t['bg2']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:8px;padding:4px 0;}}"
            f"QMenu::item{{padding:7px 18px;font-size:12px;}}"
            f"QMenu::item:selected{{background:{t['accent']};color:white;"
            f"border-radius:5px;margin:1px 3px;}}"
            f"QMenu::separator{{height:1px;background:{t['border']};margin:3px 10px;}}")
        if item:
            gid = item.data(Qt.ItemDataRole.UserRole)
            g = GROUPS.get(gid) if gid else None
            name = g.get("name","?") if g else "?"
            open_a = QAction(f"💬 Открыть «{name}»", self)
            open_a.triggered.connect(lambda: self.group_selected.emit(gid))
            menu.addAction(open_a)
            menu.addSeparator()
            edit_a = QAction("✏ Редактировать группу", self)
            edit_a.triggered.connect(lambda: self._edit_group_dialog(gid))
            menu.addAction(edit_a)
            invite_a = QAction("📨 Отправить приглашение в чат", self)
            invite_a.triggered.connect(lambda checked, g=gid: self._send_invite_to_chat(g))
            menu.addAction(invite_a)
            pin_groups = set(json.loads(S().get("pinned_groups", "[]", t=str)))
            pin_lbl = "📌 Открепить группу" if gid in pin_groups else "📌 Закрепить группу"
            pin_a = QAction(pin_lbl, self)
            def _toggle_group_pin(checked=False, _gid=gid):
                import json as _j
                pg = list(_j.loads(S().get("pinned_groups", "[]", t=str)))
                if _gid in pg: pg.remove(_gid)
                else: pg.insert(0, _gid)
                S().set("pinned_groups", _j.dumps(pg))
                self._refresh_groups()
            pin_a.triggered.connect(_toggle_group_pin)
            menu.addAction(pin_a)
            menu.addAction(invite_a)
            menu.addSeparator()
            leave_a = QAction("🚪 Выйти из группы", self)
            leave_a.triggered.connect(lambda: self._leave_group(gid, name))
            menu.addAction(leave_a)
            del_a = QAction(_L("🗑 Удалить группу", "🗑 Delete group", "🗑 グループ削除"), self)
            del_a.triggered.connect(lambda: self._delete_group(gid, name))
            menu.addAction(del_a)
        else:
            new_a = QAction(_L("➕ Создать группу", "➕ Create group", "➕ グループ作成"), self)
            new_a.triggered.connect(self._create_group)
            menu.addAction(new_a)
        menu.exec(self._groups_list.mapToGlobal(pos))

    def _send_invite_to_chat(self, gid: str):
        """Show dialog to pick a chat and send a group invite message with JOIN button."""
        g = GROUPS.get(gid)
        if not g: return
        t = get_theme(S().theme)

        dlg = QDialog(self)
        dlg.setWindowTitle(_L("Send invitation", "Send invitation", "招待を送る"))
        dlg.resize(340, 420)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        lay = QVBoxLayout(dlg)
        lay.setContentsMargins(14, 14, 14, 14)
        lay.setSpacing(8)

        lbl = QLabel(f'Where to send invitation to «{g.get("name","?")}»?')
        lbl.setWordWrap(True)
        lbl.setStyleSheet("font-size:12px;font-weight:bold;")
        lay.addWidget(lbl)

        dest_list = QListWidget()
        dest_list.setStyleSheet(
            f"QListWidget{{background:{t['bg3']};border:1px solid {t['border']};"
            "border-radius:8px;}}"
            f"QListWidget::item{{padding:8px 12px;}}"
            f"QListWidget::item:selected{{background:{t['accent']};color:white;}}")

        # Add public chat
        pub_item = QListWidgetItem("💬 Общий чат")
        pub_item.setData(Qt.ItemDataRole.UserRole, ("public", None))
        dest_list.addItem(pub_item)

        # Add peer DMs
        for ip, peer in self._peers.items():
            name = peer.get("username", ip)
            item = QListWidgetItem(f"👤 {name}")
            item.setData(Qt.ItemDataRole.UserRole, ("peer", peer))
            dest_list.addItem(item)

        # Add other groups
        for other_gid, other_g in GROUPS.list_for(get_local_ip()):
            if other_gid != gid:
                item = QListWidgetItem(f"📂 {other_g.get('name','?')}")
                item.setData(Qt.ItemDataRole.UserRole, ("group", other_gid))
                dest_list.addItem(item)

        lay.addWidget(dest_list)

        btn_row = QHBoxLayout()
        send_btn = QPushButton(_L("📨 Отправить", "📨 Send", "📨 送信"))
        send_btn.setObjectName("accent_btn")
        cancel_btn = QPushButton(_L("Отмена", "Cancel", "キャンセル"))
        cancel_btn.clicked.connect(dlg.reject)
        btn_row.addStretch()
        btn_row.addWidget(send_btn)
        btn_row.addWidget(cancel_btn)
        lay.addLayout(btn_row)

        def do_send():
            item = dest_list.currentItem()
            if not item:
                QMessageBox.warning(dlg, "Выберите чат", "Выберите, куда отправить.")
                return
            dest_type, dest_data = item.data(Qt.ItemDataRole.UserRole)
            ip_self = get_local_ip()
            invite_payload = {
                "type": "group_invite_msg",
                "gid": gid,
                "gname": g.get("name", "?"),
                "host": ip_self,
                "from": S().username,
            }
            import json
            invite_json = json.dumps(invite_payload, ensure_ascii=False)

            # Emit invite_message signal so ChatPanel displays the invite bubble
            self.invite_message_requested.emit(dest_type,
                                               dest_data if dest_data else "",
                                               invite_json)
            dlg.accept()
            QMessageBox.information(self, "Отправлено",
                f"Приглашение в «{g.get('name','?')}» отправлено!")

        send_btn.clicked.connect(do_send)
        dest_list.itemDoubleClicked.connect(lambda _: do_send())
        dlg.exec()

    def _leave_group(self, gid: str, name: str):
        if QMessageBox.question(self, "Выйти из группы",
                f"Выйти из группы «{name}»?",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            GROUPS.remove_member(gid, get_local_ip())
            self._refresh_groups()

    def _delete_group(self, gid: str, name: str):
        if QMessageBox.question(self, "Удалить группу",
                f"Удалить группу «{name}»? Это действие нельзя отменить.",
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            GROUPS.delete(gid)
            self._refresh_groups()

    def _edit_group_dialog(self, gid: str):
        g = GROUPS.get(gid)
        if not g: return
        main_win = self.window()
        tabs = getattr(main_win, '_tabs', None)
        t = get_theme(S().theme)
        if tabs is not None:
            tab_title = f"📂 {g.get('name','?') }"
            for i in range(tabs.count()):
                if tabs.tabText(i).strip() == tab_title:
                    tabs.setCurrentIndex(i); return
            dlg = QWidget(main_win)
        else:
            dlg = QDialog(self)
            dlg.setWindowTitle(f'Group: {g.get("name","")}')
            dlg.resize(460, 560)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        # Wrap in scroll for tab display
        _outer = QVBoxLayout(dlg)
        _outer.setContentsMargins(0,0,0,0)
        _scroll = QScrollArea(dlg)
        _scroll.setWidgetResizable(True)
        _scroll.setStyleSheet(
            f"QScrollArea{{background:{t['bg2']};border:none;}}"
            f"QScrollBar:vertical{{width:6px;background:{t['bg3']};}}"
            f"QScrollBar::handle:vertical{{background:{t['border']};border-radius:3px;}}")
        _inner = QWidget()
        _inner.setStyleSheet(f"background:{t['bg2']};")
        _scroll.setWidget(_inner)
        _outer.addWidget(_scroll)
        dl = QVBoxLayout(_inner)
        dl.setContentsMargins(20, 20, 20, 20)
        dl.setSpacing(12)

        # ── Avatar ──────────────────────────────────────────────────────
        av_row = QHBoxLayout()
        av_lbl = QLabel()
        av_lbl.setFixedSize(72, 72)
        av_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        av_lbl.setStyleSheet(
            f"background:{t['bg3']};border-radius:36px;"
            f"border:2px solid {t['border']};font-size:24px;")
        # Load saved avatar if any
        av_b64 = g.get("avatar_b64", "")
        if av_b64:
            try:
                pm = QPixmap()
                pm.loadFromData(base64.b64decode(av_b64))
                pm = pm.scaled(64, 64, Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                               Qt.TransformationMode.SmoothTransformation)
                # Crop to square then make circular
                pm = pm.copy(
                    (pm.width()-64)//2, (pm.height()-64)//2, 64, 64)
                av_lbl.setPixmap(make_circle_pixmap(pm, 64))
                av_lbl.setStyleSheet(
                    f"background:transparent;border-radius:32px;"
                    f"border:2px solid {t['accent']};")
            except Exception:
                av_lbl.setText("📂")
        else:
            av_lbl.setText("📂")
        av_row.addWidget(av_lbl)
        av_info = QVBoxLayout()
        av_info.addWidget(QLabel("<b>Group avatar</b>"))
        av_btn = QPushButton(_L("📷 Изменить аватар", "📷 Change avatar", "📷 アバター変更"))
        av_btn.setObjectName("accent_btn")
        def do_change_avatar():
            path, _ = QFileDialog.getOpenFileName(
                dlg, "Выберите изображение", "",
                "Изображения (*.png *.jpg *.jpeg *.gif *.webp)")
            if not path: return
            try:
                pm2 = QPixmap(path).scaled(
                    128, 128,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation)
                import io
                buf = QBuffer()
                buf.open(QIODevice.OpenModeFlag.WriteOnly)
                pm2.save(buf, "PNG")
                b64 = base64.b64encode(bytes(buf.data())).decode()
                buf.close()
                g2 = GROUPS.get(gid) or {}
                g2["avatar_b64"] = b64
                GROUPS.groups[gid] = g2
                GROUPS._save()
                av_lbl.setPixmap(pm2.scaled(64, 64, Qt.AspectRatioMode.KeepAspectRatio,
                                            Qt.TransformationMode.SmoothTransformation))
                self._refresh_groups()
            except Exception as e:
                QMessageBox.warning(dlg, "Error", f"Не удалось загрузить: {e}")
        av_btn.clicked.connect(do_change_avatar)
        av_info.addWidget(av_btn)
        av_row.addLayout(av_info)
        av_row.addStretch()
        dl.addLayout(av_row)

        # ── Name ────────────────────────────────────────────────────────
        name_row = QHBoxLayout()
        name_row.addWidget(QLabel(_L("Название:", "Name:", "名前:")))
        name_edit = QLineEdit(g.get("name",""))
        name_edit.setPlaceholderText(_L("Название группы", "Group name", "グループ名"))
        name_row.addWidget(name_edit)
        rename_btn = QPushButton(_L("✏ Переименовать", "✏ Rename", "✏ 名前変更"))
        def do_rename():
            n = name_edit.text().strip()
            if n:
                GROUPS.rename(gid, n)
                dlg.setWindowTitle(f"Group: {n}")
                self._refresh_groups()
        rename_btn.clicked.connect(do_rename)
        name_row.addWidget(rename_btn)
        dl.addLayout(name_row)

        # Members
        dl.addWidget(QLabel(_L("Участники:", "Members:", "メンバー:")))
        members_list = QListWidget()
        members_list.setMaximumHeight(180)
        def refresh_members():
            members_list.clear()
            g2 = GROUPS.get(gid)
            if not g2: return
            for ip in g2.get("members",[]):
                name_lbl = ip
                # Try to find peer name
                if ip == get_local_ip():
                    name_lbl = f"[Вы]  {ip}"
                members_list.addItem(QListWidgetItem(f"👤 {name_lbl}"))
                members_list.item(members_list.count()-1).setData(
                    Qt.ItemDataRole.UserRole, ip)
        refresh_members()
        dl.addWidget(members_list)

        mem_btn_row = QHBoxLayout()
        kick_btn = QPushButton(_L("🚫 Удалить участника", "🚫 Remove member", "🚫 メンバー削除"))
        def do_kick():
            item = members_list.currentItem()
            if not item: return
            ip = item.data(Qt.ItemDataRole.UserRole)
            if ip == get_local_ip():
                QMessageBox.warning(dlg,"Нельзя","Нельзя удалить себя.")
                return
            GROUPS.remove_member(gid, ip)
            refresh_members()
            self._refresh_groups()
        kick_btn.clicked.connect(do_kick)
        mem_btn_row.addWidget(kick_btn)

        invite_peer_btn = QPushButton(_L("➕ Пригласить пользователя", "➕ Invite user", "➕ ユーザー招待"))
        def do_invite():
            peers_online = self._peers
            if not peers_online:
                QMessageBox.information(dlg,"Нет пользователей","Никого no online.")
                return
            names = [f"{v.get('username','?')} ({k})" for k,v in peers_online.items()]
            choice, ok = QInputDialog.getItem(dlg,"Пригласить","Выберите пользователя:",
                names, editable=False)
            if ok and choice:
                ip = choice.split("(")[-1].rstrip(")")
                GROUPS.add_member(gid, ip)
                self.net.send_group_invite(gid, g.get("name","?"), ip)
                refresh_members()
                self._refresh_groups()
        invite_peer_btn.clicked.connect(do_invite)
        mem_btn_row.addWidget(invite_peer_btn)
        dl.addLayout(mem_btn_row)

        # Invite link
        dl.addWidget(QLabel(_L("Ссылка-приглашение:", "Invite link:", "招待リンク:")))
        ip_self = get_local_ip()
        invite_str = f"goidaphone://join/{gid}?host={ip_self}&name={g.get('name','')}"
        inv_row = QHBoxLayout()
        inv_lbl = QLineEdit(invite_str)
        inv_lbl.setReadOnly(True)
        copy_inv = QPushButton(_L("📋 Copy", "📋 Copy", "📋 コピー"))
        copy_inv.clicked.connect(lambda: (QApplication.clipboard().setText(invite_str),
                                          QMessageBox.information(dlg,"Скопировано","Ссылка скопирована!")))
        inv_row.addWidget(inv_lbl)
        inv_row.addWidget(copy_inv)
        dl.addLayout(inv_row)

        # Danger zone
        dl.addWidget(QLabel(""))
        danger_row = QHBoxLayout()
        leave_btn2 = QPushButton(_L("🚪 Выйти", "🚪 Leave", "🚪 退出"))
        leave_btn2.clicked.connect(lambda: (dlg.accept(), self._leave_group(gid, g.get("name",""))))
        del_btn2 = QPushButton(_L("🗑 Удалить группу", "🗑 Delete group", "🗑 グループ削除"))
        del_btn2.setObjectName("danger_btn")
        del_btn2.clicked.connect(lambda: (dlg.accept(), self._delete_group(gid, g.get("name",""))))
        danger_row.addStretch()
        danger_row.addWidget(leave_btn2)
        danger_row.addWidget(del_btn2)
        dl.addLayout(danger_row)

        close_btn = QPushButton(_L("Закрыть", "Close", "閉じる"))
        dl.addWidget(close_btn)
        if tabs is not None:
            def _do_close():
                for i in range(tabs.count()):
                    if tabs.widget(i) is dlg:
                        tabs.removeTab(i); break
            close_btn.clicked.connect(_do_close)
            tab_title = f"📂 {g.get('name','?')  }"
            idx = tabs.addTab(dlg, tab_title)
            tabs.setCurrentIndex(idx)
            if hasattr(main_win, '_add_tab_close_btn'):
                main_win._add_tab_close_btn(idx)
        else:
            close_btn.clicked.connect(dlg.accept)
            dlg.exec()

    def _create_group(self):
        name, ok = QInputDialog.getText(self, "Новая группа", "Group name:")
        if ok and name.strip():
            gid = GROUPS.create(name.strip(), get_local_ip())
            self._refresh_groups()
            # Ask to immediately open edit dialog
            if QMessageBox.question(self,"Группа создана",
                    f"Группа «{name}» создана.\nОткрыть настройки группы?",
                    QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                    == QMessageBox.StandardButton.Yes:
                self._edit_group_dialog(gid)

# ═══════════════════════════════════════════════════════════════════════════
#  CHAT PANEL
# ═══════════════════════════════════════════════════════════════════════════
class _GrowingTextEdit(QTextEdit):
    """
    Многострочное поле ввода, которое растёт по мере ввода текста.
    Enter = отправить, Shift+Enter = новая строка.
    """
    send_pressed = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptRichText(False)
        self.setLineWrapMode(QTextEdit.LineWrapMode.WidgetWidth)
        self.setMinimumHeight(36)
        self.setMaximumHeight(200)
        self.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self.document().contentsChanged.connect(self._adjust_height)
        self.setStyleSheet(
            "QTextEdit{border:none;background:transparent;"
            "padding:6px 4px;font-size:13px;}")

    def _adjust_height(self):
        # Даём документу пересчитать layout по текущей ширине
        self.document().setTextWidth(self.viewport().width())
        doc_h = int(self.document().size().height()) + 16
        new_h = max(36, min(200, doc_h))
        if self.height() != new_h:
            self.setFixedHeight(new_h)
            # Скроллим вниз чтобы курсор был виден
            self.ensureCursorVisible()

    def keyPressEvent(self, event):
        if (event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter)
                and not (event.modifiers() & Qt.KeyboardModifier.ShiftModifier)):
            self.send_pressed.emit()
        else:
            super().keyPressEvent(event)

    def insertFromMimeData(self, source):
        """Ctrl+V: если в буфере картинка — передаём в ChatPanel."""
        if source.hasImage():
            # Поднимаем наверх к ChatPanel
            p = self.parent()
            while p:
                if hasattr(p, 'dropEvent') and hasattr(p, '_send_file_path'):
                    from PyQt6.QtCore import QMimeData
                    p.dropEvent(type('E', (), {
                        'mimeData': lambda s: source,
                        'acceptProposedAction': lambda s: None
                    })())
                    return
                p = p.parent() if hasattr(p, 'parent') else None
        elif source.hasUrls():
            p = self.parent()
            while p:
                if hasattr(p, '_send_file_path'):
                    for url in source.urls():
                        path = url.toLocalFile()
                        if path:
                            p._send_file_path(path)
                    return
                p = p.parent() if hasattr(p, 'parent') else None
        else:
            super().insertFromMimeData(source)

    def setPlaceholderText(self, text: str):
        self._placeholder = text
        self.document().setPlainText("")

    def paintEvent(self, event):
        super().paintEvent(event)
        if self.toPlainText() == "" and hasattr(self, '_placeholder'):
            from PyQt6.QtGui import QPainter, QColor
            p = QPainter(self.viewport())
            p.setPen(QColor(128, 128, 128, 160))
            p.setFont(self.font())
            p.drawText(self.viewport().rect().adjusted(4, 4, -4, -4),
                       Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop,
                       self._placeholder)


class ChatPanel(QWidget):
    def __init__(self, net: NetworkManager, voice: VoiceCallManager, parent=None):
        super().__init__(parent)
        self.net   = net
        self.voice = voice
        self._current_peer: dict | None = None
        self._current_gid:  str | None  = None
        self._typing_timer = QTimer()
        self._typing_timer.setSingleShot(True)
        self._typing_timer.timeout.connect(self._send_typing)
        self._in_call = False
        self._drafts: dict[str, str] = {}  # chat_id → draft text
        self._ttl_seconds: int = 0
        self._ttl_timers: list = []
        self._polls:       dict = {}   # poll_id → poll data
        self._setup()
        # Drag & drop — файлы и изображения прямо в чат
        self.setAcceptDrops(True)

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0,0,0,0)
        lay.setSpacing(0)

        # Header
        self._header = QWidget()
        self._header.setObjectName("chat_header")
        hl = QHBoxLayout(self._header)
        hl.setContentsMargins(12,8,12,8)
        t = get_theme(S().theme)
        self._header.setStyleSheet(f"""
            QWidget#chat_header {{
                background: {t['bg2']};
                border-bottom: 1px solid {t['border']};
            }}
        """)

        self._avatar_lbl = QLabel()
        self._avatar_lbl.setFixedSize(0, 0)
        self._avatar_lbl.setVisible(False)
        hl.addWidget(self._avatar_lbl)

        vl = QVBoxLayout()
        vl.setSpacing(0)
        self._title_lbl = QLabel(TR("public_chat"))
        self._title_lbl.setStyleSheet(
            f"font-weight:bold;font-size:13px;color:{t['text']};background:transparent;")
        vl.addWidget(self._title_lbl)
        self._sub_lbl = QLabel(TR("all_users"))
        self._sub_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:10px;background:transparent;")
        vl.addWidget(self._sub_lbl)
        hl.addLayout(vl)
        hl.addStretch()

        self._call_btn = QPushButton("📞")
        self._call_btn.setFixedSize(32,32)
        self._call_btn.setToolTip(TR("call"))
        self._call_btn.clicked.connect(self._toggle_call)
        self._call_btn.setVisible(False)
        hl.addWidget(self._call_btn)

        self._file_btn = QPushButton("📎")
        self._file_btn.setFixedSize(32,32)
        self._file_btn.setToolTip(TR("attach"))
        self._file_btn.clicked.connect(self._send_file)
        self._file_btn.setVisible(False)
        hl.addWidget(self._file_btn)

        lay.addWidget(self._header)

        # Reply bar (hidden by default) — п.21
        self._reply_bar = QWidget()
        rbl = QHBoxLayout(self._reply_bar)
        rbl.setContentsMargins(12,4,12,4)
        self._reply_bar.setStyleSheet(f"""
            background: {t['bg2']};
            border-bottom: 1px solid {t['border']};
        """)
        self._reply_lbl = QLabel("")
        self._reply_lbl.setStyleSheet(f"color: {t['text_dim']}; font-size: 10px;")
        self._reply_lbl.setWordWrap(False)
        rbl.addWidget(self._reply_lbl)
        rbl.addStretch()
        cancel_reply = QPushButton("×")
        cancel_reply.setFixedSize(20,20)
        cancel_reply.setStyleSheet(f"""
            QPushButton {{
                background: {t['btn_bg']}; color: {t['text_dim']};
                border: none; border-radius: 10px; font-weight: bold;
            }}
            QPushButton:hover {{ background: #CC2222; color: white; }}
        """)
        cancel_reply.clicked.connect(self._cancel_reply)
        rbl.addWidget(cancel_reply)
        self._reply_bar.setVisible(False)
        lay.addWidget(self._reply_bar)
        self._reply_to_text = ""
        self._reply_to_ts = 0.0

        # ── Search bar (Ctrl+F, hidden by default) ──────────────────────
        self._search_bar = QWidget()
        self._search_bar.setObjectName("chat_search_bar")
        self._search_bar.setFixedHeight(38)
        self._search_bar.setStyleSheet(f"""
            QWidget#chat_search_bar {{
                background: {t['bg2']};
                border-bottom: 1px solid {t['border']};
            }}
        """)
        sb_lay = QHBoxLayout(self._search_bar)
        sb_lay.setContentsMargins(10, 4, 10, 4)
        sb_lay.setSpacing(6)
        search_ico = QLabel("🔍")
        search_ico.setStyleSheet("background:transparent;font-size:12px;")
        sb_lay.addWidget(search_ico)
        self._search_input = QLineEdit()
        self._search_input.setPlaceholderText(_L("Search messages... (Enter — next)", "Search messages... (Enter — next)", "メッセージ検索..."))
        self._search_input.setStyleSheet(f"""
            QLineEdit {{
                background:transparent;border:none;
                color:{t['text']};font-size:11px;
            }}
        """)
        self._search_input.textChanged.connect(self._do_search)
        self._search_input.returnPressed.connect(self._search_next)
        sb_lay.addWidget(self._search_input, stretch=1)
        self._search_count_lbl = QLabel("")
        self._search_count_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:9px;background:transparent;min-width:80px;")
        sb_lay.addWidget(self._search_count_lbl)
        for label, tip, cb in [("▲", "Предыдущее", self._search_prev),
                                ("▼", "Следующее",  self._search_next)]:
            b = QPushButton(label)
            b.setFixedSize(22, 22)
            b.setToolTip(tip)
            b.setStyleSheet(f"QPushButton{{background:{t['btn_bg']};border:none;"
                            f"border-radius:4px;color:{t['text']};font-size:9px;}}"
                            f"QPushButton:hover{{background:{t['btn_hover']};}}")
            b.clicked.connect(cb)
            sb_lay.addWidget(b)
        close_sb = QPushButton("✕")
        close_sb.setFixedSize(22, 22)
        close_sb.setStyleSheet(f"QPushButton{{background:transparent;border:none;"
                               f"color:{t['text_dim']};font-size:11px;}}"
                               f"QPushButton:hover{{color:{t['text']};}}")
        close_sb.clicked.connect(self._close_search)
        sb_lay.addWidget(close_sb)
        self._search_bar.setVisible(False)
        self._search_matches: list[int] = []
        self._search_match_idx: int = -1
        lay.addWidget(self._search_bar)

        # Typing indicator
        self._typing_lbl = QLabel("")
        self._typing_lbl.setStyleSheet(f"color: {t['text_dim']}; font-size:10px; padding: 2px 12px; font-style:italic;")
        self._typing_lbl.setFixedHeight(18)
        lay.addWidget(self._typing_lbl)
        self._typing_hide_timer = QTimer()
        self._typing_hide_timer.setSingleShot(True)
        self._typing_hide_timer.timeout.connect(lambda: self._typing_lbl.setText(""))

        # Chat display
        self._display = ChatDisplay(chat_id="public")
        # Connect context menu signals
        self._display.request_forward.connect(self._on_forward_request)
        self._display.request_edit.connect(self._on_edit_request)
        self._display.request_delete.connect(self._on_delete_request)
        self._display.request_react.connect(self._on_react_request)
        self._display.request_react_with_emoji.connect(self._send_reaction)
        self._display.request_reply.connect(self._on_reply_request)
        lay.addWidget(self._display, stretch=1)

        # Ctrl+F = open search
        ctrlf = QShortcut(QKeySequence("Ctrl+F"), self)
        ctrlf.activated.connect(self._toggle_search_bar)

        # Call status bar (hidden by default)
        self._call_bar = QWidget()
        self._call_bar.setObjectName("call_bar_active")
        cbl = QHBoxLayout(self._call_bar)
        cbl.setContentsMargins(12,6,12,6)
        self._call_bar.setStyleSheet("""
            QWidget#call_bar_active {
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 #1A4A1A, stop:1 #123012);
                border-top: 1px solid #2A5A2A;
            }
        """)
        self._call_status_lbl = QLabel(TR("active_call"))
        self._call_status_lbl.setStyleSheet("color: #80FF80; font-weight: bold;")
        cbl.addWidget(self._call_status_lbl)
        cbl.addStretch()
        self._mute_btn = QPushButton(TR("mute_mic"))
        self._mute_btn.clicked.connect(self._toggle_mute)
        cbl.addWidget(self._mute_btn)
        self._hangup_btn = QPushButton("📵 " + TR("hangup"))
        self._hangup_btn.setObjectName("danger_btn")
        self._hangup_btn.clicked.connect(self._hangup)
        cbl.addWidget(self._hangup_btn)
        self._call_bar.setVisible(False)
        lay.addWidget(self._call_bar)

        # ── Inline sticker panel ──────────────────────────────────────
        t2 = get_theme(S().theme)
        self._sticker_panel = QWidget()
        self._sticker_panel.setObjectName("sticker_panel")
        self._sticker_panel.setStyleSheet(
            f"QWidget#sticker_panel{{background:{t2['bg2']};"
            f"border-top:1px solid {t2['border']};}}")
        sp_lay = QVBoxLayout(self._sticker_panel)
        sp_lay.setContentsMargins(8, 6, 8, 4)
        sp_lay.setSpacing(4)

        # Header row: title + pack combo + manage + close
        sp_top = QHBoxLayout()
        sp_top.setSpacing(6)
        sp_lbl = QLabel(_L("🎭 Стикеры", "🎭 Stickers", "🎭 スタンプ"))
        sp_lbl.setStyleSheet(
            f"font-weight:bold;font-size:11px;color:{t2['text']};background:transparent;")
        sp_top.addWidget(sp_lbl)

        self._sp_pack_combo = QComboBox()
        self._sp_pack_combo.setFixedHeight(26)
        self._sp_pack_combo.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self._sp_pack_combo.currentIndexChanged.connect(self._refresh_sticker_grid)
        sp_top.addWidget(self._sp_pack_combo)

        manage_btn = QPushButton(_L("⚙ Паки", "⚙ Packs", "⚙ パック"))
        manage_btn.setFixedHeight(26)
        manage_btn.setToolTip("Manage sticker packs")
        manage_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        manage_btn.setStyleSheet(
            f"QPushButton{{background:{t2['bg3']};color:{t2['text']};"
            f"border:1px solid {t2['border']};border-radius:5px;"
            "padding:0 8px;font-size:10px;}}"
            f"QPushButton:hover{{background:{t2['btn_hover']};}}")
        manage_btn.clicked.connect(self._open_sticker_manager)
        sp_top.addWidget(manage_btn)

        sp_close = QPushButton("✕")
        sp_close.setFixedSize(24, 24)
        sp_close.setFlat(True)
        sp_close.setCursor(Qt.CursorShape.PointingHandCursor)
        sp_close.setStyleSheet(
            f"QPushButton{{color:{t2['text_dim']};background:transparent;"
            "border:none;font-size:13px;font-weight:bold;border-radius:12px;}}"
            f"QPushButton:hover{{background:{t2['btn_hover']};color:{t2['text']};}}")
        sp_close.clicked.connect(lambda: self._toggle_sticker_panel(False))
        sp_top.addWidget(sp_close)
        sp_lay.addLayout(sp_top)

        # Sticker scroll grid
        self._sticker_scroll = QScrollArea()
        self._sticker_scroll.setWidgetResizable(True)
        self._sticker_scroll.setFixedHeight(156)
        self._sticker_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._sticker_scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        self._sticker_scroll.setFrameShape(QFrame.Shape.NoFrame)
        self._sticker_scroll.setStyleSheet(
        f"QScrollArea{{background:{t2['bg2']};border:none;}}"
        f"QScrollBar:vertical{{background:{t2['bg3']};width:6px;border-radius:3px;}}"
        f"QScrollBar::handle:vertical{{background:{t2['border']};border-radius:3px;}}")
        self._sticker_grid_widget = QWidget()
        self._sticker_grid_widget.setStyleSheet(
        f"background:{t2['bg2']};")
        self._sticker_grid_layout = QGridLayout(self._sticker_grid_widget)
        self._sticker_grid_layout.setSpacing(6)
        self._sticker_grid_layout.setContentsMargins(4, 4, 4, 4)
        self._sticker_scroll.setWidget(self._sticker_grid_widget)
        sp_lay.addWidget(self._sticker_scroll)

        self._sticker_panel.setVisible(False)
        # Sticker panel is now shown as floating side popup, NOT in main layout

        # Input area
        input_w = QWidget()
        input_w.setObjectName("input_area")
        t = get_theme(S().theme)
        input_w.setStyleSheet(f"""
            QWidget#input_area {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['bg2']}, stop:1 {t['bg']});
                border-top: 1px solid {t['border']};
            }}
        """)
        il = QHBoxLayout(input_w)
        il.setContentsMargins(8,8,8,8)
        il.setSpacing(6)

        self._input = _GrowingTextEdit()
        self._input.setPlaceholderText(TR("type_message") + "  •  перетащи файл или Ctrl+V")
        self._input.send_pressed.connect(self._send_text)
        self._input.textChanged.connect(self._on_typing_te)
        self._input.textChanged.connect(self._on_slash_complete_te)
        il.addWidget(self._input)

        self._emoji_btn = QPushButton("😊")
        self._emoji_btn.setFixedSize(34,34)
        self._emoji_btn.setToolTip(TR("emoji"))
        self._emoji_btn.clicked.connect(self._emoji_picker)
        il.addWidget(self._emoji_btn)

        self._sticker_btn = QPushButton("🎭")
        self._sticker_btn.setFixedSize(34,34)
        self._sticker_btn.setToolTip("Stickers")
        self._sticker_btn.setCheckable(True)
        self._sticker_btn.clicked.connect(lambda checked: self._toggle_sticker_panel(checked))
        il.addWidget(self._sticker_btn)

        send_btn = QPushButton("➤")
        send_btn.setFixedSize(38,34)
        send_btn.setObjectName("accent_btn")
        send_btn.clicked.connect(self._send_text)
        il.addWidget(send_btn)

        lay.addWidget(input_w)

        # Slash command hint popup — shows above input, does NOT intercept keyboard
        self._cmd_popup = QListWidget(self)
        self._cmd_popup.setWindowFlags(
            Qt.WindowType.Tool |
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowDoesNotAcceptFocus)
        self._cmd_popup.setFixedWidth(280)
        self._cmd_popup.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self._cmd_popup.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._cmd_popup.hide()
        self._cmd_popup.itemClicked.connect(self._on_cmd_selected)

        # Load public history
        self._load_history("public")

    # ── navigation ──────────────────────────────────────────────────────
    def _save_draft(self):
        """Save current input text as draft for current chat."""
        cid = self._get_current_chat_id()
        if cid:
            text = self._input.toPlainText().strip()
            if text:
                self._drafts[cid] = text
            else:
                self._drafts.pop(cid, None)

    def _restore_draft(self, chat_id: str):
        """Restore draft for given chat_id into input field."""
        draft = self._drafts.get(chat_id, "")
        self._input.setPlainText(draft)
        if draft:
            # Move cursor to end
            cur = self._input.textCursor()
            cur.movePosition(cur.MoveOperation.End)
            self._input.setTextCursor(cur)

    def open_public(self):
        self._save_draft()
        self._current_peer = None
        self._current_gid  = None
        self._title_lbl.setText(TR("public_chat"))
        self._title_lbl.setTextFormat(Qt.TextFormat.PlainText)
        self._sub_lbl.setText(TR("all_users"))
        self._call_btn.setVisible(False)
        self._file_btn.setVisible(False)
        # Public chat has no avatar — hide avatar slot completely
        self._avatar_lbl.setPixmap(QPixmap())
        self._avatar_lbl.setFixedSize(0, 0)
        self._avatar_lbl.setVisible(False)
        self._display.chat_id = "public"
        self._display.clear()
        self._polls.clear()
        self._display._messages.clear()
        self._load_history("public")
        UNREAD.mark_read("public")
        self._cancel_reply()

    def open_peer(self, peer: dict):
        self._save_draft()
        self._current_peer = peer
        self._current_gid  = None
        color  = peer.get("nickname_color","#E0E0E0")
        emoji  = peer.get("custom_emoji","")
        name   = peer.get("username","?")
        ip     = peer.get("ip","")
        self._title_lbl.setText(f"<span style='color:{color}'>{name} {emoji}</span>")
        self._title_lbl.setTextFormat(Qt.TextFormat.RichText)
        sec = CRYPTO.security_level(ip)
        fp  = CRYPTO.peer_fingerprint(ip) if CRYPTO.has_session(ip) else "no E2E-ключа"
        self._sub_lbl.setText(f"{TR('private_chat')} • {ip}  {sec}")
        self._sub_lbl.setToolTip(f"Peer fingerprint: {fp}")
        self._call_btn.setVisible(True)
        self._file_btn.setVisible(True)
        # Avatar
        self._avatar_lbl.setFixedSize(36, 36)
        self._avatar_lbl.setVisible(True)
        av_b64 = peer.get("avatar_b64","")
        if av_b64:
            try:
                pm = base64_to_pixmap(av_b64)
                self._avatar_lbl.setPixmap(make_circle_pixmap(pm, 36))
            except Exception:
                self._avatar_lbl.setPixmap(default_avatar(name, 36))
        else:
            self._avatar_lbl.setPixmap(default_avatar(name, 36))
        chat_id = peer["ip"]
        self._display.chat_id = chat_id
        self._display.clear()
        self._polls.clear()
        self._display._messages.clear()
        self._load_history(chat_id)
        UNREAD.mark_read(chat_id)
        self._cancel_reply()
        self._restore_draft(chat_id)
        # Send read receipt
        if self.net:
            self.net.send_read_receipt(peer["ip"], chat_id)

    def open_group(self, gid: str):
        self._save_draft()
        self._current_peer = None
        self._current_gid  = gid

        # ── Избранное (self-chat) ───────────────────────────────────────
        if gid == "__favorites__":
            self._title_lbl.setText(_L("⭐ Избранное", "⭐ Favorites", "⭐ お気に入り"))
            self._title_lbl.setTextFormat(Qt.TextFormat.PlainText)
            self._sub_lbl.setText(_L("Notes, links, files for yourself", "Notes, links, files for yourself", "自分用メモ・ファイル"))
            self._call_btn.setVisible(False)
            self._file_btn.setVisible(True)
            self._avatar_lbl.setPixmap(QPixmap())
            chat_id = "__favorites__"
            self._display.chat_id = chat_id
            self._display.clear()
            self._polls.clear()
            self._display._messages.clear()
            self._load_history(chat_id)
            UNREAD.mark_read(chat_id)
            self._cancel_reply()
            return

        g = GROUPS.get(gid)
        self._title_lbl.setText(f'📂 {g.get("name","Group")}')
        self._title_lbl.setTextFormat(Qt.TextFormat.PlainText)
        members = g.get("members",[])
        self._sub_lbl.setText(f"Members: {len(members)}")
        self._call_btn.setVisible(True)
        self._file_btn.setVisible(True)
        g_av = g.get("avatar_b64","")
        if g_av:
            try:
                pm_g = QPixmap(); pm_g.loadFromData(base64.b64decode(g_av))
                self._avatar_lbl.setPixmap(make_circle_pixmap(pm_g, 36))
            except Exception:
                self._avatar_lbl.setPixmap(QPixmap())
        else:
            self._avatar_lbl.setPixmap(QPixmap())
        chat_id = f"group_{gid}"
        self._display.chat_id = chat_id
        self._display.clear()
        self._polls.clear()
        self._display._messages.clear()
        self._load_history(chat_id)
        UNREAD.mark_read(chat_id)
        self._cancel_reply()

    def _load_history(self, chat_id: str):
        msgs = HISTORY.load(chat_id)
        self._display.load_history(msgs)

    # ── slash command handling ────────────────────────────────────────────
    # Available commands (п.26)
    _SLASH_COMMANDS = {
        "/clear":    "Очистить чат",
        "/help":     "Список команд",
        "/me":       "Действие (/me прыгает)",
        "/ping":     "Проверка соединения",
        "/version":  "Версия GoidaPhone",
        "/nick":     "Сменить ник (/nick Имя)",
        "/away":     "Status: отошёл",
        "/busy":     "Status: занят",
        "/dnd":      "Status: не беспокоить",
        "/online":   "Status: online",
        "/search":   "Поиск по истории",
        "/shrug":    "Добавить ¯_(ツ)_/¯",
        "/ttl":      "Исчезающие сообщения (/ttl 30)",
        "/poll":     "Опрос (/poll \"Вопрос?\" Да Нет)",
        "/notes":    "Совместные заметки",
        "/schedule": "Отправить в время (/schedule 18:30 текст)",
        "/tr":       "Перевод (/tr en привет)",
        "/gif":      "Поиск GIF",
        "/status":   "Установить статус",
    }

    def _on_slash_complete_te(self):
        """Called on textChanged from QTextEdit — shows slash autocomplete popup."""
        text = self._input.toPlainText().strip()
        self._on_slash_complete(text)

    def _on_slash_complete(self, text: str):
        """Show autocomplete popup only while typing the command name itself."""
        if not text.startswith("/"):
            self._cmd_popup.hide()
            return

        # If the user already typed a space (entered args), hide popup
        if " " in text:
            self._cmd_popup.hide()
            return

        cmd_part = text.lower()
        matches = [(cmd, desc) for cmd, desc in self._SLASH_COMMANDS.items()
                   if cmd.startswith(cmd_part) and cmd != cmd_part]

        # Exact match — hide popup, user has typed the full command
        if not matches or cmd_part in self._SLASH_COMMANDS:
            self._cmd_popup.hide()
            return

        self._cmd_popup.clear()
        t = get_theme(S().theme)
        self._cmd_popup.setStyleSheet(f"""
            QListWidget {{
                background: {t['bg2']}; color: {t['text']};
                border: 1px solid {t['accent']}; border-radius: 8px;
                padding: 4px;
            }}
            QListWidget::item {{ padding: 5px 10px; border-radius: 4px; }}
            QListWidget::item:selected, QListWidget::item:hover {{
                background: {t['accent']}; color: white;
            }}
        """)
        for cmd, desc in matches[:8]:
            item = QListWidgetItem(f"{cmd}  —  {desc}")
            item.setData(Qt.ItemDataRole.UserRole, cmd)
            self._cmd_popup.addItem(item)

        # Position popup DIRECTLY above input field — no gap
        popup_h = min(len(matches[:8]) * 34 + 8, 240)
        # Map top-left of input to global, then go up by exactly popup_h
        gp = self._input.mapToGlobal(QPoint(0, -popup_h))
        popup_w = max(320, self._input.width())
        self._cmd_popup.setFixedWidth(popup_w)
        self._cmd_popup.setFixedHeight(popup_h)
        self._cmd_popup.move(gp)
        self._cmd_popup.show()
        # Keep focus on input field — popup is just a hint
        self._input.setFocus()

    def _on_cmd_selected(self, item):
        cmd = item.data(Qt.ItemDataRole.UserRole)
        self._input.setPlainText(cmd + " ")
        # Move cursor to end
        cur = self._input.textCursor()
        cur.movePosition(cur.MoveOperation.End)
        self._input.setTextCursor(cur)
        self._input.setFocus()
        self._cmd_popup.hide()

    def _handle_slash_command(self, text: str) -> bool:
        """
        Handle slash commands. Returns True if command was consumed.
        """
        parts = text.strip().split(maxsplit=1)
        cmd = parts[0].lower()
        arg = parts[1] if len(parts) > 1 else ""

        if cmd == "/clear":
            chat_id = self._get_current_chat_id()
            self._display.clear()
            self._polls.clear()
            self._display._messages.clear()
            # Also wipe the history file so it doesn't reload on restart
            try:
                f = HISTORY._file(chat_id)
                if f.exists():
                    f.unlink()
                HISTORY._cache.pop(chat_id, None)
            except Exception as e:
                print(f"[clear] history wipe error: {e}")
            self._display.add_system(TR("cmd_clear_done"))
            return True

        elif cmd == "/help":
            help_text = TR("cmd_help")
            self._display.add_system(help_text)
            return True

        elif cmd == "/me":
            if arg:
                # /me jumps → "Username jumps"
                full = f"* {S().username} {arg}"
                ts = time.time()
                chat_id = self._get_current_chat_id()
                if self._current_peer:
                    self.net.send_private(full, self._current_peer["ip"])
                elif self._current_gid:
                    g = GROUPS.get(self._current_gid)
                    self.net.send_group_msg(self._current_gid, full, g.get("members",[]))
                else:
                    self.net.send_chat(full)
                self._display.add_message(S().username, full, ts, is_own=True,
                                          color=S().nickname_color)
                HISTORY.append(chat_id, {"sender": S().username, "text": full,
                                          "ts": ts, "is_own": True})
            return True

        elif cmd == "/ping":
            self._display.add_system(TR("cmd_ping"))
            return True

        elif cmd == "/version":
            self._display.add_system(
                f"GoidaPhone v{APP_VERSION} | Protocol v{PROTOCOL_VERSION} | {COMPANY_NAME}")
            return True

        elif cmd == "/nick":
            if arg.strip():
                old = S().username
                S().username = arg.strip()
                self._display.add_system(f"Nick changed: {old} → {arg.strip()}")
                self.net._broadcast()
            return True

        elif cmd in ("/away", "/busy", "/dnd", "/online"):
            status_map = {"/away": "away", "/busy": "busy",
                          "/dnd": "dnd", "/online": "online"}
            new_st = status_map[cmd]
            S().user_status = new_st
            icons  = {"online": "🟢", "away": "🟡", "busy": "🔴", "dnd": "⊘"}
            labels = {"online": "Онлайн", "away": "Отошёл",
                      "busy": "Занят", "dnd": "Не беспокоить"}
            self._display.add_system(f"{icons[new_st]} Status: {labels[new_st]}")
            if self.net:
                self.net._broadcast()  # broadcast updated presence immediately
            return True

        elif cmd == "/search":
            # Open in-chat search bar
            if rest.strip():
                self._do_search(rest.strip())
            else:
                self._toggle_search_bar()
            return True

        elif cmd == "/shrug":
            self._input.setPlainText(r"¯\_(ツ)_/¯")
            return False   # Don't consume — let user edit/send

        elif cmd in ("/notes", "/note", "/заметки"):
            # Open shared notes panel
            self._open_shared_notes()
            return True

        elif cmd == "/schedule":
            # /schedule HH:MM text   — send message at specific time
            parts2 = arg.strip().split(None, 1)
            if len(parts2) == 2:
                time_str = parts2[0]; msg_text = parts2[1]
                try:
                    import datetime as _dt
                    now = _dt.datetime.now()
                    h, m = map(int, time_str.split(":"))
                    send_at = now.replace(hour=h, minute=m, second=0, microsecond=0)
                    if send_at <= now:
                        send_at += _dt.timedelta(days=1)
                    delay_ms = int((send_at - now).total_seconds() * 1000)
                    def _sched_send(_text=msg_text):
                        self._input.setPlainText(_text)
                        self._send_text()
                    QTimer.singleShot(delay_ms, _sched_send)
                    self._display.add_system_message(
                        f"⏰ Сообщение запланировано на {time_str}: {msg_text[:30]}",
                        chat_id=self._get_current_chat_id())
                except Exception as e:
                    self._display.add_system_message(
                        f"⏰ Ошибка: {e}  Формат: /schedule HH:MM текст",
                        chat_id=self._get_current_chat_id())
            else:
                self._display.add_system_message(
                    "⏰ /schedule HH:MM текст сообщения",
                    chat_id=self._get_current_chat_id())
            return True

        elif cmd == "/ttl":
            try:
                n = int(arg.strip()) if arg.strip() else 0
                self._ttl_seconds = max(0, n)
                msg = f"⏱ Исчезающие сообщения: {n} сек" if n else "⏱ Исчезающие сообщения выключены"
                self._display.add_system_message(msg, chat_id=self._get_current_chat_id())
            except ValueError:
                self._display.add_system_message(
                    "⏱ /ttl <секунды>  (например /ttl 30)",
                    chat_id=self._get_current_chat_id())
            return True

        elif cmd in ("/translate", "/tr", "/перевести"):
            # /tr [lang] text  — translate text using MyMemory free API
            parts2 = arg.strip().split(None, 1)
            if len(parts2) == 2 and len(parts2[0]) == 2:
                lang = parts2[0]; text_to_translate = parts2[1]
            else:
                lang = "en"; text_to_translate = arg.strip()
            if not text_to_translate:
                self._display.add_system_message(
                    "🌍 /tr [lang] текст  (например /tr en привет)",
                    chat_id=self._get_current_chat_id())
                return True
            chat_id = self._get_current_chat_id()
            import threading, urllib.request, urllib.parse, json as _j
            def _translate():
                try:
                    url = ("https://api.mymemory.translated.net/get?q="
                           f"{urllib.parse.quote(text_to_translate)}"
                           f"&langpair=auto|{lang}")
                    with urllib.request.urlopen(url, timeout=6) as resp:
                        data = _j.loads(resp.read().decode())
                    result = data["responseData"]["translatedText"]
                    QTimer.singleShot(0, lambda r=result, c=chat_id:
                        self._display.add_system_message(
                            f"🌍 {r}", chat_id=c))
                except Exception as e:
                    QTimer.singleShot(0, lambda err=str(e), c=chat_id:
                        self._display.add_system_message(
                            f"🌍 Ошибка перевода: {err}", chat_id=c))
            threading.Thread(target=_translate, daemon=True).start()
            return True

        elif cmd in ("/poll", "/pool"):  # /pool is common typo for /poll
            import shlex as _shlex
            try: opts = _shlex.split(arg)
            except Exception: opts = arg.split()
            if len(opts) >= 2:
                question = opts[0]; options = opts[1:]
                poll_id = f"poll_{int(time.time())}"
                self._polls[poll_id] = {
                    "question": question, "options": options,
                    "votes": {o: [] for o in options},
                    "creator": S().username}
                self._add_poll_bubble(poll_id, question, options, is_own=True)
                poll_pkt = {"type": MSG_POLL, "poll_id": poll_id,
                            "question": question, "options": options,
                            "username": S().username, "ts": time.time()}
                if self._current_peer:
                    self.net.send_udp(poll_pkt, self._current_peer["ip"])
                elif self._current_gid:
                    for ip in GROUPS.get(self._current_gid).get("members", []):
                        if ip != get_local_ip(): self.net.send_udp(poll_pkt, ip)
                else: self.net.send_udp(poll_pkt)
                # Сохраняем в историю
                _chat_id = self._get_current_chat_id()
                HISTORY.append(_chat_id, {
                    "sender": S().username, "text": question,
                    "ts": time.time(), "is_own": True,
                    "color": S().nickname_color,
                    "msg_type": "poll",
                    "poll_id": poll_id,
                    "poll_options": options,
                    "poll_votes": {o: [] for o in options},
                })
            else:
                self._display.add_system_message(
                    '💡 /poll "Вопрос?" Вариант1 Вариант2 ...',
                    chat_id=self._get_current_chat_id())
            return True

        return False   # Not a recognized command

    # ── Message search (Ctrl+F) ─────────────────────────────────────────────
    def _toggle_search_bar(self):
        visible = not self._search_bar.isVisible()
        self._search_bar.setVisible(visible)
        if visible:
            self._search_input.setFocus()
            self._search_input.selectAll()
        else:
            self._close_search()

    def _close_search(self):
        self._search_bar.setVisible(False)
        self._search_input.clear()
        self._search_matches = []
        self._search_match_idx = -1
        self._search_count_lbl.setText("")
        self._display.clear_search_highlight()

    def _do_search(self, query: str = ""):
        query = query or self._search_input.text()
        self._search_matches = []
        self._search_match_idx = -1
        if not query.strip():
            self._search_count_lbl.setText("")
            self._display.clear_search_highlight()
            return
        self._display.highlight_search(query)
        self._search_matches = self._display.get_search_matches(query)
        count = len(self._search_matches)
        if count:
            self._search_match_idx = 0
            self._search_count_lbl.setText(f"1/{count}")
            self._display.scroll_to_match(self._search_matches[0])
        else:
            self._search_count_lbl.setText(_L("Не найдено", "Not found", "見つかりません"))

    def _search_next(self):
        if not self._search_matches: return
        self._search_match_idx = (self._search_match_idx + 1) % len(self._search_matches)
        self._display.scroll_to_match(self._search_matches[self._search_match_idx])
        self._search_count_lbl.setText(
            f"{self._search_match_idx+1}/{len(self._search_matches)}")

    def _search_prev(self):
        if not self._search_matches: return
        self._search_match_idx = (self._search_match_idx - 1) % len(self._search_matches)
        self._display.scroll_to_match(self._search_matches[self._search_match_idx])
        self._search_count_lbl.setText(
            f"{self._search_match_idx+1}/{len(self._search_matches)}")

    def _update_enc_badge(self):
        """Show lock icon depending on encryption state."""
        if not hasattr(self, '_enc_badge'): return
        enabled = S().encryption_enabled and bool(S().encryption_passphrase)
        if enabled:
            self._enc_badge.setText("🔒 AES-256")
            self._enc_badge.setStyleSheet(
                "font-size:9px;background:transparent;color:#4CAF50;")
        else:
            self._enc_badge.setText(_L("🔓 Незашифровано", "🔓 Unencrypted", "🔓 暗号化なし"))
            self._enc_badge.setStyleSheet(
                "font-size:9px;background:transparent;color:#FF9800;")

    def _get_current_chat_id(self) -> str:
        if self._current_peer:
            return self._current_peer["ip"]
        elif self._current_gid:
            return f"group_{self._current_gid}"
        return "public"

    # ── reply helpers ─────────────────────────────────────────────────────
    def _on_reply_request(self, text: str, ts: float):
        self._reply_to_text = text
        self._reply_to_ts = ts
        short = text[:60] + ("…" if len(text) > 60 else "")
        t = get_theme(S().theme)
        self._reply_lbl.setText(f"↩ {TR('msg_reply')}: {short}")
        self._reply_bar.setVisible(True)
        self._input.setFocus()

    def _cancel_reply(self):
        self._reply_to_text = ""
        self._reply_to_ts = 0.0
        self._reply_bar.setVisible(False)
        self._reply_lbl.setText("")

    # ── forward ───────────────────────────────────────────────────────────
    def _on_forward_request(self, text: str, ts: float):
        """Forward a message to another chat."""
        # Ask user to choose destination
        peers = list(self.net.peers.values())
        if not peers:
            QMessageBox.information(self, TR("msg_forward"),
                "No users online для пересылки." if S().language=="ru"
                else "No users online to forward to.")
            return
        items = ["Общий чат"] + [p.get("username","?") for p in peers]
        choice, ok = QInputDialog.getItem(self,
            TR("msg_forward"), "Переслать в:", items, editable=False)
        if not ok:
            return
        forwarded = f"↪ {text}"
        ts_new = time.time()
        if choice == "Общий чат" or choice == "Public Chat":
            self.net.send_chat(forwarded)
            self._display.add_message(
                TR("you"), forwarded, ts_new, is_own=True,
                is_forwarded=True, forwarded_from=S().username)
        else:
            for p in peers:
                if p.get("username") == choice:
                    self.net.send_private(forwarded, p["ip"])
                    self._display.add_message(
                        TR("you"), forwarded, ts_new, is_own=True,
                        is_forwarded=True, forwarded_from=S().username)
                    break

    # ── edit ──────────────────────────────────────────────────────────────
    def _on_edit_request(self, text: str, ts: float):
        new_text, ok = QInputDialog.getText(
            self, TR("msg_edit"),
            "Редактировать:" if S().language=="ru" else "Edit message:",
            text=text)
        if ok and new_text.strip() and new_text != text:
            self._display.edit_message(ts, new_text.strip())
            # Notify peer
            chat_id = self._get_current_chat_id()
            to_ip = self._current_peer["ip"] if self._current_peer else None
            self.net.send_message_edit(to_ip, chat_id, ts, new_text.strip())
            # Update history
            msgs = HISTORY.load(chat_id)
            for m in msgs:
                if abs(m.get("ts",0) - ts) < 0.01:
                    m["text"] = new_text.strip()
                    m["is_edited"] = True
                    break

    # ── delete ────────────────────────────────────────────────────────────
    def _on_delete_request(self, ts: float):
        reply = QMessageBox.question(
            self, TR("msg_delete"),
            "Удалить сообщение?" if S().language=="ru" else "Delete this message?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            self._display.delete_message(ts)
            chat_id = self._get_current_chat_id()
            to_ip = self._current_peer["ip"] if self._current_peer else None
            self.net.send_message_delete(to_ip, chat_id, ts)

    # ── react ─────────────────────────────────────────────────────────────
    def _on_react_request(self, ts: float):
        """Show emoji picker for reactions."""
        REACTION_EMOJIS = [
            "👍","👎","❤️","😂","😮","😢","😡","🔥",
            "🎉","💯","👏","🤔","😎","🚀","💎","✅",
            "⭐","🙏","💪","🥰"
        ]
        menu = QMenu(self)
        t = get_theme(S().theme)
        menu.setStyleSheet(f"""
            QMenu {{
                background: {t['bg2']}; color: {t['text']};
                border: 1px solid {t['accent']}; border-radius: 10px;
                padding: 6px;
            }}
            QMenu::item {{
                padding: 4px 8px; font-size: 16px;
                border-radius: 6px;
            }}
            QMenu::item:selected {{ background: {t['btn_hover']}; }}
        """)

        # Create a grid of emoji actions
        for i, em in enumerate(REACTION_EMOJIS):
            act = QAction(em, self)
            act.triggered.connect(
                lambda _, e=em, t_=ts: self._send_reaction(t_, e))
            menu.addAction(act)

        # Show near the input button
        menu.exec(self._emoji_btn.mapToGlobal(
            QPoint(0, -menu.sizeHint().height() - 5)))

    def _send_reaction(self, ts: float, emoji: str):
        """Toggle a reaction on a message."""
        try:
            ts = float(ts)
        except (TypeError, ValueError):
            return
        chat_id = self._get_current_chat_id()
        added = REACTIONS.toggle(chat_id, ts, emoji, S().username)
        self._display.update_reactions(ts)
        to_ip = self._current_peer["ip"] if self._current_peer else None
        self.net.send_reaction(to_ip, chat_id, ts, emoji, added)

    # ── send ─────────────────────────────────────────────────────────────
    # ── Drag & Drop ────────────────────────────────────────────────────────
    def dragEnterEvent(self, event):
        """Принимаем файлы и текст с изображениями."""
        md = event.mimeData()
        if md.hasUrls() or md.hasImage() or md.hasText():
            event.acceptProposedAction()
            # Визуальная подсказка — подсветить поле ввода
            t = get_theme(S().theme)
            _iw = self.findChild(QWidget, "input_area")
            if _iw:
                _iw.setStyleSheet(
                    f"QWidget#input_area{{background:{t['bg3']};border-radius:12px;"
                    f"border:2px dashed {t['accent']};}}")
        else:
            event.ignore()

    def dragLeaveEvent(self, event):
        t = get_theme(S().theme)
        _iw = self.findChild(QWidget, "input_area")
        if _iw: _iw.setStyleSheet("")   # сброс — применится global QSS

    def dropEvent(self, event):
        """Обработать дроп — файлы, изображения, текст."""
        t = get_theme(S().theme)
        _iw = self.findChild(QWidget, "input_area")
        if _iw: _iw.setStyleSheet("")   # сброс — применится global QSS
        md = event.mimeData()

        if md.hasUrls():
            # Файлы/папки — отправляем каждый
            for url in md.urls():
                path = url.toLocalFile()
                if path and Path(path).is_file():
                    self._send_file_path(path)
            event.acceptProposedAction()

        elif md.hasImage():
            # Изображение из буфера (скриншот, копипаст из браузера)
            img = md.imageData()
            if img and not img.isNull():
                import tempfile, os as _os
                tmp = Path(tempfile.mktemp(suffix=".png"))
                img.save(str(tmp))
                self._send_file_path(str(tmp))
                QTimer.singleShot(5000, lambda: tmp.unlink(missing_ok=True))
            event.acceptProposedAction()

        elif md.hasText():
            # Текст — вставляем в поле ввода
            self._input.insertPlainText(md.text())
            event.acceptProposedAction()

    def _send_file_path(self, path: str):
        """Отправить файл по пути — то же что нажать скрепку."""
        p = Path(path)
        if not p.exists(): return
        ext   = p.suffix.lower()
        fname = p.name
        fsize = p.stat().st_size
        ts    = time.time()
        prog_id = f"up_{int(ts*1000)}"
        self._display.add_upload_progress(fname, fsize, prog_id)
        to_ip = self._current_peer["ip"] if self._current_peer else None

        _pre_data = None
        if ext in self._IMG_EXTS or ext in self._VID_EXTS:
            try: _pre_data = p.read_bytes()
            except Exception: pass

        dest0 = RECEIVED_DIR / fname
        try:
            import shutil as _sh
            if not dest0.exists():
                _sh.copy2(path, str(dest0))
        except Exception: pass

        try:
            self.net.send_file(to_ip, path)
        except Exception as e:
            print(f"[drag-send] {e}")

        _chat_id = "public"
        if self._current_peer:   _chat_id = self._current_peer.get("ip", "public")
        elif self._current_gid:  _chat_id = f"group_{self._current_gid}"

        _msg_type = ("image" if ext in self._IMG_EXTS
                     else "video" if ext in self._VID_EXTS
                     else "file")

        self._display.remove_progress(prog_id)
        if ext in self._IMG_EXTS:
            self._display.add_message("You", fname, ts, is_own=True,
                image_data=_pre_data)
        elif ext in self._VID_EXTS:
            self._display.add_message("You", fname, ts, is_own=True,
                msg_type="video", image_data=_pre_data)
        else:
            self._display.add_message("You", fname, ts, is_own=True, msg_type="file")

        HISTORY.append(_chat_id, {
            "sender": S().username, "text": fname, "ts": ts,
            "is_own": True, "color": S().nickname_color,
            "msg_type": _msg_type, "file_path": str(dest0),
        })

    def _send_text(self):
        text = self._input.toPlainText().strip()
        if not text:
            return
        play_system_sound("click")

        # Handle slash commands first
        if text.startswith("/"):
            self._cmd_popup.hide()
            consumed = self._handle_slash_command(text)
            if consumed:
                self._input.clear()
                return

        text = self._input.toPlainText().strip()
        if not text:
            return
        self._cmd_popup.hide()
        ts = time.time()
        cfg = S()

        reply_text = self._reply_to_text
        chat_id = self._get_current_chat_id()

        # ── Optimistic render BEFORE clear so message never disappears ──
        self._display.add_message(
            TR("you"), text, ts, is_own=True,
            color=cfg.nickname_color, emoji=cfg.custom_emoji,
            reply_to_text=reply_text, chat_id=chat_id)

        # Clear input AFTER rendering so textChanged doesn't cause flicker
        self._input.clear()
        self._cancel_reply()

        HISTORY.append(chat_id, {
            "sender":       cfg.username,
            "text":         text,
            "ts":           ts,
            "is_own":       True,
            "color":        cfg.nickname_color,
            "emoji":        cfg.custom_emoji,
            "reply_to_text": reply_text,
            "ttl":          self._ttl_seconds if self._ttl_seconds > 0 else 0,
        })
        # Schedule self-destruct if TTL is set
        if self._ttl_seconds > 0:
            _ttl_ms = self._ttl_seconds * 1000
            _ts_ref = ts
            _display_ref = self._display
            t_timer = QTimer()
            t_timer.setSingleShot(True)
            t_timer.timeout.connect(
                lambda _ts=_ts_ref, _d=_display_ref: _d.delete_message(_ts))
            t_timer.start(_ttl_ms)
            self._ttl_timers.append(t_timer)

        if self._current_peer:
            self.net.send_private(text, self._current_peer["ip"])
        elif self._current_gid:
            g = GROUPS.get(self._current_gid)
            self.net.send_group_msg(self._current_gid, text, g.get("members",[]))
        else:
            self.net.send_chat(text)

    def _send_text_raw(self, text: str):
        """Send raw text into the currently open chat (used for invite messages etc.)."""
        if not text:
            return
        ts = time.time()
        cfg = S()
        chat_id = self._get_current_chat_id()
        if self._current_peer:
            self.net.send_private(text, self._current_peer["ip"])
        elif self._current_gid:
            g = GROUPS.get(self._current_gid)
            self.net.send_group_msg(self._current_gid, text, g.get("members", []))
        else:
            self.net.send_chat(text)
        self._display.add_message(
            TR("you"), text, ts, is_own=True,
            color=cfg.nickname_color, emoji=cfg.custom_emoji, chat_id=chat_id)

    def receive_message(self, msg: dict):
        sender = msg.get("username","?")
        from_ip = msg.get("from_ip","")
        # DEDUP: drop own messages echoed back from broadcast
        # Use ALL local IPs to handle LAN+VPN multi-homed setups
        _my_ips = get_all_local_ips() | {self.net.host_ip}
        if from_ip and from_ip in _my_ips:
            return
        if not from_ip and sender == S().username:
            return
        text   = msg.get("text","")
        ts     = msg.get("ts", time.time())
        gid    = msg.get("gid","")
        mtype  = msg.get("type","")

        # Handle reactions, edits and deletes BEFORE decryption (they don't need it)
        if mtype == MSG_REACTION:
            chat_id = msg.get("chat_id", "public")
            self._display.add_reaction_update(
                msg.get("msg_ts", 0), msg.get("emoji",""),
                sender, msg.get("added", True))
            return

        # Handle shared notes sync
        if mtype == MSG_NOTES_SYNC:
            content = msg.get("content", "")
            chat_id_n = (f"group_{gid}" if gid else
                        msg.get("from_ip","") if mtype == "private" else "public")
            notes_key = f"shared_notes_{chat_id_n}"
            S().set(notes_key, content)
            # Update editor if open
            ed = getattr(self, '_notes_editor', None)
            if ed:
                ed.setPlainText(content)
            return

        # Handle sticker
        if mtype == MSG_STICKER:
            b64  = msg.get("sticker_b64","")
            ts_s = msg.get("ts", time.time())
            gid_s= msg.get("gid","")
            if not b64: return
            img_bytes = base64.b64decode(b64)
            if gid_s:
                chat_id = f"group_{gid_s}"
                show = (self._current_gid == gid_s)
            elif msg.get("from_ip"):
                chat_id = msg["from_ip"]
                show = (self._current_peer and
                        self._current_peer.get("ip") == msg["from_ip"])
            else:
                chat_id = "public"
                show = (self._current_peer is None and self._current_gid is None)
            color = "#E0E0E0"
            for ip, p in self.net.peers.items():
                if p.get("username") == sender:
                    color = p.get("nickname_color","#E0E0E0"); break
            if show:
                self._display.add_message(sender, "", ts_s, is_own=False,
                                          color=color, msg_type="sticker",
                                          image_data=img_bytes, chat_id=chat_id)
            else:
                UNREAD.increment(chat_id)
            # Save sticker file to disk so it survives restart
            _sticker_path = RECEIVED_DIR / f"sticker_{int(ts_s)}.png"
            try:
                if not _sticker_path.exists():
                    _sticker_path.write_bytes(img_bytes)
            except Exception: pass
            HISTORY.append(chat_id, {
                "sender": sender, "text": "", "ts": ts_s,
                "is_own": False, "msg_type": "sticker",
                "image_b64": b64,
                "sticker_path": str(_sticker_path),
            })
            play_system_sound("message")
            return

        # Decrypt if needed
        if msg.get("encrypted") and CRYPTO.is_encrypted(text):
            pp = S().encryption_passphrase
            peer_ip_hint = msg.get("from_ip", "")
            text = CRYPTO.decrypt(text, pp, peer_ip=peer_ip_hint)
            msg = dict(msg)
            msg["text"] = text

        if mtype == MSG_EDIT:
            self._display.edit_message(
                msg.get("msg_ts", 0), msg.get("new_text",""))
            return
        if mtype == MSG_DELETE:
            self._display.delete_message(msg.get("msg_ts", 0))
            play_system_sound("delete")
            return
        if mtype == MSG_READ:
            # Mark our messages as read in this chat
            return

        # Determine chat_id & whether we should show
        if mtype == MSG_PRIVATE:
            chat_id = msg.get("from_ip", "")
            for ip, p in self.net.peers.items():
                if p.get("username") == sender:
                    chat_id = ip; break
            show = (self._current_peer and
                    self._current_peer.get("username") == sender)
        elif mtype == MSG_GROUP:
            chat_id = f"group_{gid}"
            show = (self._current_gid == gid)
        else:
            chat_id = "public"
            show = (self._current_peer is None and self._current_gid is None)

        # Find peer info for colors
        color = "#E0E0E0"; emoji_s = ""
        for ip, p in self.net.peers.items():
            if p.get("username") == sender:
                color  = p.get("nickname_color","#E0E0E0")
                emoji_s = p.get("custom_emoji","")
                break

        # Check for @mention of our username
        is_mention = f"@{S().username}" in text

        if show:
            self._display.add_message(sender, text, ts, is_own=False,
                                      color=color, emoji=emoji_s,
                                      msg_type=mtype, chat_id=chat_id)
        else:
            UNREAD.increment(chat_id)

        cfg = S()
        HISTORY.append(chat_id, {
            "sender": sender, "text": text, "ts": ts,
            "is_own": False, "color": color, "emoji": emoji_s,
        })

        # ── Sound notification (once per message, always) ─────────────────
        if cfg.notification_sounds:
            play_system_sound("mention" if is_mention else "message")

        # ── OS desktop notification ────────────────────────────────────────
        if cfg.os_notifications and (not show or is_mention):
            if mtype == MSG_PRIVATE:
                title = f"{TR('notif_new_message')} от {sender}"
            elif mtype == MSG_GROUP:
                g = GROUPS.get(gid)
                title = f"{sender} в {g.get('name','группе')}"
            else:
                title = f"{sender}: {TR('notif_new_message')}"
            body = text[:100] + ("…" if len(text) > 100 else "")
            send_notification(title, body)
        # Also show in-app toast if available
        avatar = ""
        if "avatar_b64" in peer:
            avatar = peer.get("avatar_b64","")
        try:
            show_toast(title, body, avatar)
        except Exception:
            pass

    def receive_file(self, meta: dict, data: bytes):
        sender   = meta.get("from","?")
        fname    = meta.get("filename","file")
        is_image = meta.get("is_image", False)
        ts       = meta.get("ts", time.time())

        # Save file
        dest = RECEIVED_DIR / fname
        dest.write_bytes(data)

        gid    = meta.get("gid","")
        to     = meta.get("to","")
        if gid:
            chat_id = f"group_{gid}"
            show = (self._current_gid == gid)
        elif to == "public":
            chat_id = "public"
            show = (self._current_peer is None and self._current_gid is None)
        else:
            for ip, p in self.net.peers.items():
                if p.get("username") == sender:
                    chat_id = ip; break
            else:
                chat_id = "public"
            show = (self._current_peer and
                    self._current_peer.get("username") == sender)

        color = "#E0E0E0"
        for ip, p in self.net.peers.items():
            if p.get("username") == sender:
                color = p.get("nickname_color","#E0E0E0"); break

        _rext = Path(fname).suffix.lower()
        _rvid = {".mp4",".avi",".mkv",".mov",".webm",".m4v",".wmv",".flv"}
        _is_vid = _rext in _rvid
        _msg_type = "video" if _is_vid else ("image" if is_image else "file")

        if show:
            self._display.add_message(sender, fname, ts, is_own=False,
                                      color=color, msg_type=_msg_type,
                                      image_data=data if (is_image or _is_vid) else None)

        # Always save to history with file path so it persists across restarts
        HISTORY.append(chat_id, {
            "sender": sender, "text": fname, "ts": ts,
            "is_own": False, "color": color,
            "msg_type": _msg_type,
            "file_path": str(dest),
        })

        if S().notification_sounds and not show:
            play_system_sound("message")

    def show_typing(self, username: str, chat_id: str):
        show = False
        if chat_id == "public" and not self._current_peer and not self._current_gid:
            show = True
        elif self._current_peer:
            for ip, p in self.net.peers.items():
                if p.get("username") == username and ip == self._current_peer.get("ip"):
                    show = True; break
        elif self._current_gid and chat_id == f"group_{self._current_gid}":
            show = True
        if show:
            self._typing_lbl.setText(f"{username} is typing...")
            self._typing_hide_timer.start(3000)

    # ── file ──────────────────────────────────────────────────────────────
    _VID_EXTS = {".mp4",".avi",".mkv",".mov",".webm",".m4v",".wmv",".flv"}
    _IMG_EXTS = {".png",".jpg",".jpeg",".gif",".bmp",".webp"}

    def _send_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select file")
        if not path:
            return
        p = Path(path); ext = p.suffix.lower(); fname = p.name
        fsize = p.stat().st_size; ts = time.time()
        prog_id = f"up_{int(ts*1000)}"
        self._display.add_upload_progress(fname, fsize, prog_id)
        to_ip = self._current_peer["ip"] if self._current_peer else None

        # Read file data synchronously (safe — happens before send)
        _pre_data = None
        if ext in self._IMG_EXTS or ext in self._VID_EXTS:
            try: _pre_data = p.read_bytes()
            except Exception: pass

        # Copy ALL sent files to RECEIVED_DIR so they persist after restart
        dest0 = RECEIVED_DIR / fname
        try:
            import shutil as _sh0
            if not dest0.exists():
                _sh0.copy2(path, str(dest0))
        except Exception: pass

        # send_file uses QTimer internally — returns immediately, no thread needed
        try:
            self.net.send_file(to_ip, path)
        except Exception as e:
            print(f"[send] {e}")

        # Determine chat_id for history
        _chat_id = "public"
        if self._current_peer:
            _chat_id = self._current_peer.get("ip", "public")
        elif self._current_gid:
            _chat_id = f"group_{self._current_gid}"

        _msg_type_s = ("image" if ext in self._IMG_EXTS
                       else "video" if ext in self._VID_EXTS
                       else "file")

        # Remove progress and show bubble right away
        self._display.remove_progress(prog_id)
        if ext in self._IMG_EXTS:
            self._display.add_message("You", fname, ts, is_own=True,
                image_data=_pre_data)
        elif ext in self._VID_EXTS:
            self._display.add_message("You", fname, ts, is_own=True,
                msg_type="video", image_data=_pre_data)
        else:
            self._display.add_message("You", fname, ts, is_own=True, msg_type="file")

        # Save to history WITH file_path — restores on restart
        HISTORY.append(_chat_id, {
            "sender": S().username, "text": fname, "ts": ts,
            "is_own": True, "color": S().nickname_color,
            "msg_type": _msg_type_s,
            "file_path": str(dest0),
        })

    # ── call ──────────────────────────────────────────────────────────────
    def _toggle_call(self):
        if self._in_call:
            self._hangup()
            return

        main_win = self.window()  # MainWindow reference

        if self._current_peer:
            ip   = self._current_peer.get("ip", "")
            name = self._current_peer.get("username", ip)
            av   = self._current_peer.get("avatar_b64", "")
            if not ip:
                return
            # Show outgoing call screen — actual call starts on accept
            win = OutgoingCallWindow(name, ip, av)
            self._float_call = win  # keep reference
            win.sig_cancelled.connect(lambda: self._cancel_outgoing_local(ip))
            win.show()
            self.net.send_call_request(ip)
            # Set call active visually but don't start voice yet
            self._call_btn.setText("📵")
            self._call_btn.setToolTip("Cancel call")

        elif self._current_gid:
            # Group call → GroupCallWindow (Телемост style)
            g       = GROUPS.get(self._current_gid)
            gname   = g.get("name", self._current_gid)
            members = [m for m in g.get("members", []) if m != get_local_ip()]
            participants = [
                self.net.peers[ip] for ip in members if ip in self.net.peers
            ]
            # Start voice with online members immediately
            self.voice.audio.start_capture()
            for ip in members:
                if ip in self.net.peers:
                    self.net.send_call_request(ip)
                    self.voice.call(ip)

            gcall = GroupCallWindow(gname, participants, self.voice)
            self._float_call = gcall
            gcall.sig_leave.connect(self._hangup)
            gcall.show()
            self._set_call_active(True)

    def _cancel_outgoing_local(self, ip: str):
        """Cancel outgoing call before answer."""
        self.net.send_call_end(ip)
        self._float_call = None
        self._call_btn.setText("📞")
        self._call_btn.setToolTip("Call")

    def _set_call_active(self, active: bool, peer: dict | None = None):
        # Guard против двойного вызова
        if not active and not self._in_call:
            return  # уже не в звонке — не дублируем сообщение
        self._in_call = active
        self._call_bar.setVisible(False)
        if active:
            self._call_btn.setText("📵")
            self._call_btn.setToolTip("End call")
            self._display.add_system(TR("call_started_msg"))
        else:
            self._call_btn.setText("📞")
            self._call_btn.setToolTip("Call")
            self._display.add_system(TR("call_ended_msg"))
            if hasattr(self, "_float_call") and self._float_call:
                try:
                    if hasattr(self._float_call, '_timer'):
                        self._float_call._timer.stop()
                    self._float_call.hide()
                    self._float_call.deleteLater()
                except Exception:
                    pass
                self._float_call = None

    def _accept_incoming_in_chat(self, caller: str, ip: str):
        """Called when user accepts incoming call — start voice + show active window."""
        av_b64 = self.net.peers.get(ip, {}).get("avatar_b64", "")
        self.net.send_call_accept(ip)
        self.net.connect_voice(ip)
        self.voice.call(ip)
        active = ActiveCallWindow(caller, ip, av_b64, voice_mgr=self.voice)
        self._float_call = active
        active.sig_hangup.connect(lambda: self._hangup())
        active.sig_mute.connect(self.voice.set_mute)
        active.show()
        self._set_call_active(True)


    def _hangup(self):
        if self._current_peer:
            self.voice.hangup(self._current_peer["ip"])
        else:
            self.voice.hangup_all()
        self._set_call_active(False)

    def _on_system_audio_output_changed(self):
        """Системное аудио-устройство изменилось — уведомляем все модули."""
        # Mewa: пересоздаёт QAudioOutput автоматически через свой коннект
        # Mewa делает это сам через self._media_devices в _init_player
        
        # VoiceCallManager: если идёт звонок — переподключаем стримы
        try:
            if hasattr(self.voice, '_voice') and self.voice._voice:
                v = self.voice._voice
                if v.running:
                    in_dev  = S().get("in_device",  None)
                    out_dev = S().get("out_device",  None)
                    v.stop_all()
                    v.start_capture(in_dev, out_dev)
        except Exception:
            pass

        # Звуки (_play_file) — каждый раз создают QAudioOutput заново,
        # уже используют QMediaDevices.defaultAudioOutput() — OK

    def _toggle_mute(self):
        muted = self.voice.toggle_mute()
        self._mute_btn.setText(TR("call_muted") if muted else "🎤 Микрофон")
        # Sync active call window if open
        if hasattr(self, "_float_call") and self._float_call:
            if hasattr(self._float_call, '_toggle_mute'):
                pass  # ActiveCallWindow manages its own mute state

    def on_call_ended(self, ip: str):
        if self._in_call:
            self._set_call_active(False)

    # ── typing ──────────────────────────────────────────────────────────
    def _on_typing_te(self):
        text = self._input.toPlainText().strip()
        if text:
            self._typing_timer.start(1500)

    def _on_typing(self, text: str):
        if text:
            self._typing_timer.start(1500)

    def _send_typing(self):
        if self._current_peer:
            self.net.send_typing(self._current_peer["ip"], self._current_peer["ip"])
        elif self._current_gid:
            g = GROUPS.get(self._current_gid)
            for ip in g.get("members",[]):
                if ip != get_local_ip():
                    self.net.send_typing(f"group_{self._current_gid}", ip)

    # ── emoji picker ──────────────────────────────────────────────────────
    def _emoji_picker(self):
        menu = QMenu(self)
        t = get_theme(S().theme)
        menu.setStyleSheet(f"""
            QMenu {{
                background: {t['bg2']}; color: {t['text']};
                border: 1px solid {t['accent']}; border-radius: 10px;
                padding: 6px;
            }}
            QMenu::item {{
                padding: 4px 6px; font-size: 18px;
                border-radius: 6px;
            }}
            QMenu::item:selected {{ background: {t['btn_hover']}; }}
        """)

        # Emoji categories
        categories = {
            "😊 Смайлы": ["😀","😂","😍","🤔","😎","😢","😡","🤣","😊","🙈","🥰","😏",
                          "🤩","😤","🥺","😴","🤯","🥳","😇","🤗"],
            "👍 Жесты":  ["👍","👎","👏","🙏","💪","🤝","✌️","🤞","👌","🤙","🖐️","✋"],
            "❤️ Символы": ["❤️","🔥","💯","🎉","✅","⭐","💎","🚀","💡","🎯","🏆","👑",
                           "💰","🌟","⚡","🌈","🍀","💫","🌙","☀️"],
            "🐱 Животные": ["🐱","🐶","🦊","🐺","🐻","🐼","🦁","🐯","🦄","🐸"],
        }

        for cat_name, emojis in categories.items():
            cat_action = QAction(cat_name, self)
            cat_action.setEnabled(False)
            menu.addAction(cat_action)
            row_menu = menu.addMenu("  ")
            row_menu.setStyleSheet(menu.styleSheet())
            for em in emojis:
                a = QAction(em, self)
                a.triggered.connect(lambda _, e=em: self._insert_emoji(e))
                row_menu.addAction(a)
            menu.addSeparator()

        # Also show flat list for quick access
        quick = ["😀","😂","😍","🤔","😎","👍","❤️","🔥","💯","🎉",
                 "😢","😡","🤣","😊","🙏","💪","✅","🚀","💎","👑"]
        for e in quick:
            a = QAction(e, self)
            a.triggered.connect(lambda _, em=e: self._insert_emoji(em))
            menu.addAction(a)

        menu.exec(self._emoji_btn.mapToGlobal(
            QPoint(0, -min(menu.sizeHint().height(), 300))))

    def _insert_emoji(self, emoji: str):
        cursor = self._input.textCursor()
        cursor.insertText(emoji)
        self._input.setFocus()

    def _toggle_sticker_panel(self, show: bool | None = None):
        """Show/hide sticker panel as a floating popup above sticker button."""
        visible = (not self._sticker_panel.isVisible()
                   if show is None else bool(show))
        if visible:
            # Detach from layout and show as popup
            t = get_theme(S().theme)
            self._sticker_panel.setWindowFlags(
                Qt.WindowType.Popup | Qt.WindowType.FramelessWindowHint)
            self._sticker_panel.setFixedSize(320, 220)
            self._sticker_panel.setStyleSheet(
                f"QWidget#sticker_panel{{background:{t['bg2']};"
                f"border:1px solid {t['accent']};border-radius:10px;}}")
            btn = getattr(self, '_sticker_btn', None)
            if btn:
                gp = btn.mapToGlobal(QPoint(0, 0))
                sw = self._sticker_panel.width()
                sh = self._sticker_panel.height()
                self._sticker_panel.move(
                    max(0, gp.x() - sw + btn.width()),
                    gp.y() - sh - 8)
            self._load_sticker_packs()
            self._refresh_sticker_grid()
            self._sticker_panel.show()
            self._sticker_panel.raise_()
        else:
            self._sticker_panel.hide()

    def _load_sticker_packs(self):
        """Reload pack list into combo box."""
        try:
            import json
            self._sp_pack_combo.blockSignals(True)
            self._sp_pack_combo.clear()
            raw = S().get("sticker_packs", "[]", t=str)
            packs = json.loads(raw) if raw else []
            if not packs:
                self._sp_pack_combo.addItem(_L("(no паков — добавьте в ⚙)", "(no packs — add in ⚙)", "(パックなし — ⚙で追加)"))
            for p in packs:
                self._sp_pack_combo.addItem(p.get("name", "Без имени"))
            self._sp_pack_combo.blockSignals(False)
        except Exception as ex:
            print(f"[load packs] {ex}")

    def _refresh_sticker_grid(self):
        """Fill grid with stickers from selected pack."""
        try:
            import json
            while self._sticker_grid_layout.count():
                item = self._sticker_grid_layout.takeAt(0)
                if item and item.widget():
                    item.widget().deleteLater()

            raw = S().get("sticker_packs", "[]", t=str)
            packs = json.loads(raw) if raw else []
            idx = self._sp_pack_combo.currentIndex()
            if not packs or idx < 0 or idx >= len(packs) or not isinstance(packs[idx], dict):
                no_lbl = QLabel(_L("Нет стикеров. Добавьте пак через ⚙ Паки", "No stickers. Add a pack via ⚙", "スタンプなし"))
                no_lbl.setStyleSheet("color:gray;font-size:11px;padding:8px;")
                no_lbl.setWordWrap(True)
                self._sticker_grid_layout.addWidget(no_lbl, 0, 0)
                return

            stickers = packs[idx].get("stickers", []) if isinstance(packs[idx], dict) else []
            cols = 6
            for i, s in enumerate(stickers):
                b64 = s.get("data","") if isinstance(s, dict) else str(s)
                if not b64:
                    continue
                try:
                    pm = QPixmap()
                    pm.loadFromData(base64.b64decode(b64))
                    if pm.isNull():
                        continue
                    pm = pm.scaled(64, 64,
                        Qt.AspectRatioMode.KeepAspectRatio,
                        Qt.TransformationMode.SmoothTransformation)
                    btn = QPushButton()
                    btn.setIcon(QIcon(pm))
                    btn.setIconSize(QSize(60,60))
                    btn.setFixedSize(68,68)
                    btn.setToolTip(s.get("name",""))
                    btn.setStyleSheet(
                        "QPushButton{background:transparent;border:1px solid transparent;"
                        "border-radius:8px;}"
                        "QPushButton:hover{background:rgba(255,255,255,20);"
                        "border-color:rgba(255,255,255,51);}")
                    btn.clicked.connect(lambda _, d=b64: self._send_sticker(d))
                    self._sticker_grid_layout.addWidget(btn, i//cols, i%cols)
                except Exception:
                    continue
        except Exception as ex:
            print(f"[refresh sticker grid] {ex}")

    def _open_sticker_manager(self):
        """Open StickerPackDialog as inline tab in main window."""
        # Walk up to find MainWindow and its tab widget
        main_win = self.window()
        tabs = getattr(main_win, '_tabs', None)
        if tabs is not None:
            # Check if already open
            for i in range(tabs.count()):
                if tabs.tabText(i).strip() == _L("🎭 Стикеры", "🎭 Stickers", "🎭 スタンプ"):
                    tabs.setCurrentIndex(i)
                    return
            dlg = StickerPackDialog(main_win)
            dlg.setWindowFlags(Qt.WindowType.Widget)
            idx = tabs.addTab(dlg, _L("🎭 Стикеры", "🎭 Stickers", "🎭 スタンプ"))
            tabs.setCurrentIndex(idx)
            if hasattr(main_win, '_add_tab_close_btn'):
                main_win._add_tab_close_btn(idx)
            # Reload packs when tab is closed
            def _on_sticker_tab_close():
                if self._sticker_panel.isVisible():
                    self._load_sticker_packs()
                    self._refresh_sticker_grid()
            dlg.destroyed.connect(_on_sticker_tab_close)
        else:
            # Fallback: open as dialog
            dlg = StickerPackDialog(self)
            dlg.setModal(False)
            dlg.show()
            dlg.finished.connect(lambda: (
                self._load_sticker_packs() if self._sticker_panel.isVisible() else None,
                self._refresh_sticker_grid() if self._sticker_panel.isVisible() else None,
            ))

    def _open_shared_notes(self):
        """Open shared collaborative notepad for current chat."""
        chat_id = self._get_current_chat_id()
        t = get_theme(S().theme)
        # Reuse existing notes window if open
        if hasattr(self, '_notes_dlg') and self._notes_dlg and self._notes_dlg.isVisible():
            self._notes_dlg.raise_(); return

        dlg = QWidget(self.window(), Qt.WindowType.Window)
        dlg.setWindowTitle(f'📝 Shared notes — {chat_id or "Public chat"}')
        dlg.resize(500, 400)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        lay = QVBoxLayout(dlg)
        lay.setContentsMargins(12,12,12,12); lay.setSpacing(8)

        hdr = QLabel(_L("📝 Совместные заметки", "📝 Shared notes (real-time)", "📝 共有メモ"))
        hdr.setStyleSheet(
            f"font-size:12px;color:{t['accent']};background:transparent;")
        hdr.setWordWrap(True)
        lay.addWidget(hdr)

        editor = QPlainTextEdit()
        editor.setStyleSheet(
            f"QPlainTextEdit{{background:{t['bg']};color:{t['text']};"
            "font-size:12px;border:none;border-radius:8px;padding:12px;}}")
        # Load saved notes
        import json as _j
        notes_key = f"shared_notes_{chat_id}"
        saved = S().get(notes_key, "", t=str)
        editor.setPlainText(saved)
        lay.addWidget(editor, stretch=1)

        btn_row = QHBoxLayout()
        sync_btn = QPushButton(_L("📤 Синхронизировать", "📤 Sync", "📤 同期"))
        sync_btn.setObjectName("accent_btn"); sync_btn.setFixedHeight(32)
        clear_btn = QPushButton("🗑 Clear")
        clear_btn.setFixedHeight(32)
        clear_btn.setStyleSheet(
            f"QPushButton{{background:{t['bg3']};color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:6px;padding:0 12px;}}"
            f"QPushButton:hover{{color:{t['text']};background:{t['btn_hover']};}}")

        def _sync():
            content = editor.toPlainText()
            S().set(notes_key, content)
            pkt = {"type": MSG_NOTES_SYNC, "content": content,
                   "username": S().username, "ts": time.time()}
            if self._current_peer:
                self.net.send_udp(pkt, self._current_peer["ip"])
            elif self._current_gid:
                for ip in GROUPS.get(self._current_gid).get("members",[]):
                    if ip != get_local_ip(): self.net.send_udp(pkt, ip)
            else: self.net.send_udp(pkt)
            sync_btn.setText("✅ Sent")
            QTimer.singleShot(2000, lambda: sync_btn.setText(_L("📤 Синхронизировать", "📤 Sync", "📤 同期")))

        def _clear():
            editor.clear()
            S().set(notes_key, "")

        sync_btn.clicked.connect(_sync)
        clear_btn.clicked.connect(_clear)
        btn_row.addWidget(sync_btn); btn_row.addWidget(clear_btn)
        btn_row.addStretch()
        lay.addLayout(btn_row)

        # Auto-save on change
        editor.textChanged.connect(lambda: S().set(notes_key, editor.toPlainText()))

        dlg.show(); dlg.raise_()
        self._notes_dlg = dlg
        self._notes_editor = editor

    def _add_poll_bubble(self, poll_id, question, options, is_own=True):
        t = get_theme(S().theme)
        acc = t['accent']

        frame = QFrame()
        frame.setMaximumWidth(320)
        frame.setMinimumWidth(240)
        # Красивый градиентный фон
        frame.setStyleSheet(
            f"QFrame{{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            f"stop:0 {t['bg2']},stop:1 {t['bg3']});"
            f"border-radius:16px;border:1px solid {t['border']};}}")
        fl = QVBoxLayout(frame)
        fl.setContentsMargins(16, 14, 16, 14)
        fl.setSpacing(10)

        # Заголовок с иконкой
        hdr = QHBoxLayout()
        ico = QLabel("📊")
        ico.setStyleSheet("font-size:18px;background:transparent;")
        hdr.addWidget(ico)
        q_lbl = QLabel(question)
        q_lbl.setStyleSheet(
            f"font-size:12pt;font-weight:700;color:{t['text']};"
            "background:transparent;")
        q_lbl.setWordWrap(True)
        hdr.addWidget(q_lbl, 1)
        fl.addLayout(hdr)

        # Разделитель
        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"background:{t['border']};max-height:1px;border:none;")
        fl.addWidget(sep)

        # Данные опроса
        poll_data = self._polls.get(poll_id, {})
        votes = poll_data.get("votes", {o: [] for o in options})
        total = sum(len(v) for v in votes.values())
        my_vote = next((o for o, vs in votes.items() if S().username in vs), None)

        for opt in options:
            opt_voters = votes.get(opt, [])
            pct = int(len(opt_voters) / max(total, 1) * 100)
            voted = (opt == my_vote)

            opt_w = QWidget()
            opt_w.setStyleSheet(
                f"QWidget{{background:{'rgba('+','.join(str(int(acc.lstrip('#')[i:i+2],16)) for i in (0,2,4))+',40)' if voted else 'transparent'};"
                f"border-radius:10px;border:{'2px solid '+acc if voted else 'none'};}}")
            ol = QVBoxLayout(opt_w)
            ol.setContentsMargins(10, 6, 10, 6)
            ol.setSpacing(4)

            # Текст + процент
            row = QHBoxLayout()
            opt_lbl = QLabel(("✓  " if voted else "    ") + opt)
            opt_lbl.setStyleSheet(
                f"font-size:10pt;color:{acc if voted else t['text']};"
                f"font-weight:{'700' if voted else '400'};"
                "background:transparent;")
            row.addWidget(opt_lbl, 1)
            pct_lbl = QLabel(f"{pct}%")
            pct_lbl.setStyleSheet(
                f"font-size:9pt;color:{acc if voted else t['text_dim']};"
                "background:transparent;font-weight:600;")
            row.addWidget(pct_lbl)
            ol.addLayout(row)

            # Прогресс-бар
            pb = QProgressBar()
            pb.setRange(0, 100); pb.setValue(pct)
            pb.setFixedHeight(4); pb.setTextVisible(False)
            pb.setStyleSheet(
                f"QProgressBar{{background:{t['bg3']};border-radius:2px;border:none;}}"
                f"QProgressBar::chunk{{background:{acc};border-radius:2px;}}")
            ol.addWidget(pb)

            opt_w.setCursor(Qt.CursorShape.PointingHandCursor)
            opt_w.mousePressEvent = (
                lambda e, _p=poll_id, _o=opt, _f=frame, _q=question, _opts=options, _io=is_own:
                self._vote_poll(_p, _o, _f, _q, _opts, is_own=_io))
            fl.addWidget(opt_w)

        # Футер
        n_str = f"{total} {'голос' if total==1 else 'голоса' if 2<=total<=4 else 'голосов'}"
        footer = QLabel(n_str)
        footer.setStyleSheet(
            f"color:{t['text_dim']};font-size:8pt;background:transparent;")
        fl.addWidget(footer)

        old_wrapper = self._polls.get(poll_id, {}).get("_wrapper")
        wrapper = self._display._add_widget_bubble(
            frame, is_own=is_own, replace_widget=old_wrapper)
        if poll_id in self._polls:
            self._polls[poll_id]["_wrapper"] = wrapper

    def _vote_poll(self, poll_id, option, frame, question, options, is_own=True):
        username = S().username
        if poll_id not in self._polls:
            self._polls[poll_id] = {
                "question": question, "options": options,
                "votes": {o: [] for o in options}, "creator": ""}
        poll = self._polls[poll_id]
        for v in poll["votes"].values():
            if username in v: v.remove(username)
        poll["votes"].setdefault(option, []).append(username)
        # Broadcast vote
        vote_pkt = {"type": MSG_POLL_VOTE, "poll_id": poll_id,
                    "option": option, "username": username, "ts": time.time()}
        if self._current_peer:
            self.net.send_udp(vote_pkt, self._current_peer["ip"])
        elif self._current_gid:
            for ip in GROUPS.get(self._current_gid).get("members",[]):
                if ip != get_local_ip(): self.net.send_udp(vote_pkt, ip)
        else: self.net.send_udp(vote_pkt)
        frame.setParent(None); frame.deleteLater()
        # Сохраняем выравнивание — is_own из оригинального пузыря
        self._add_poll_bubble(poll_id, question, options, is_own=is_own)

    def _send_sticker(self, b64: str):
        """Send a sticker as an image message."""
        chat_id = self._get_current_chat_id()
        if not chat_id:
            return
        try:
            img_bytes = base64.b64decode(b64)
            ts = time.time()
            self._display.add_message(
                sender    = S().username,
                text      = "",
                ts        = ts,
                is_own    = True,
                image_data= img_bytes,
            )
            to_ip = self._current_peer["ip"] if self._current_peer else None
            self.net.send_file(to_ip, None, raw_bytes=img_bytes, filename="sticker.png")
        except Exception as e:
            print(f"[sticker send] {e}")



# ═══════════════════════════════════════════════════════════════════════════
#  NOTES WIDGET
# ═══════════════════════════════════════════════════════════════════════════
class NotesWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._autosave = QTimer()
        self._autosave.setSingleShot(True)
        self._autosave.timeout.connect(self._save)
        self._setup()

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(6,6,6,6)
        lay.setSpacing(4)

        toolbar = QHBoxLayout()
        for label, cb in [(_L("💾 Сохранить", "💾 Save", "💾 保存"), self._save),
                          ("📂 Загрузить", self._load),
                          ("🧹 Очистить",  self._clear),
                          ("📤 Экспорт",   self._export)]:
            b = QPushButton(label)
            b.clicked.connect(cb)
            toolbar.addWidget(b)
        lay.addLayout(toolbar)

        self._edit = QTextEdit()
        self._edit.setPlaceholderText("Your notes...")
        self._edit.textChanged.connect(lambda: self._autosave.start(2000))
        lay.addWidget(self._edit)

        self._status = QLabel("Done")
        t = get_theme(S().theme)
        self._status.setStyleSheet(f"color:{t['text_dim']}; font-size:9px; padding:2px;")
        lay.addWidget(self._status)

        self._load()

    def _save(self):
        S().set("notes", self._edit.toPlainText())
        self._status.setText(f"Saved {datetime.now().strftime('%H:%M:%S')}")

    def _load(self):
        self._edit.setPlainText(S().get("notes","",t=str))
        self._status.setText("Loaded")

    def _clear(self):
        if QMessageBox.question(self,"Очистить","Очистить заметки?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            self._edit.clear()

    def _export(self):
        fn, _ = QFileDialog.getSaveFileName(self,"Экспорт заметок","notes.txt","Text (*.txt)")
        if fn:
            Path(fn).write_text(self._edit.toPlainText(), encoding="utf-8")
            self._status.setText(f"Exported: {fn}")
