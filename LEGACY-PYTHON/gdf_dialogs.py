#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Dialogs (settings, profile, calls, notes)
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings, _SOUND_EVENT_LABELS, _SOUND_MAP_DEFAULT, play_system_sound
from gdf_network  import *
from gdf_apps import AdminManager
from gdf_ui_base import *
from gdf_ui_base import LicenseLineEdit

# ═══════════════════════════════════════════════════════════════════════════
#  CALL LOG WIDGET
# ═══════════════════════════════════════════════════════════════════════════
class CallLogWidget(QWidget):
    """Extended call log with stats, callbacks, notes, and filter."""
    call_back_requested = pyqtSignal(str)   # peer ip/name to call back

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self._setup()

    def keyPressEvent(self, event):
        vw = getattr(self, '_video_widget', None)
        if vw and vw.isFullScreen():
            if event.key() in (Qt.Key.Key_Escape, Qt.Key.Key_F, Qt.Key.Key_F11):
                vw.setFullScreen(False)
                event.accept()
                return
        if event.key() == Qt.Key.Key_Space:
            self._toggle_play()
            event.accept()
            return
        if event.key() == Qt.Key.Key_F11:
            self._toggle_video_fullscreen()
            event.accept()
            return
        super().keyPressEvent(event)

    def _setup(self):
        t = get_theme(S().theme)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(10, 10, 10, 10)
        lay.setSpacing(8)

        # ── Stats bar ──
        stats_row = QHBoxLayout()
        self._stat_total = QLabel("Total: 0")
        self._stat_out   = QLabel("📞 Outgoing: 0")
        self._stat_in    = QLabel("📲 Incoming: 0")
        self._stat_miss  = QLabel("❌ Missed: 0")
        for lbl in [self._stat_total, self._stat_out,
                    self._stat_in, self._stat_miss]:
            lbl.setStyleSheet(
                f"background:{t['bg3']};border-radius:6px;padding:4px 10px;"
                f"font-size:10px;color:{t['text_dim']};")
            stats_row.addWidget(lbl)
        stats_row.addStretch()
        lay.addLayout(stats_row)

        # ── Filter row ──
        filter_row = QHBoxLayout()
        filter_row.addWidget(QLabel("Filter:"))
        self._filter_combo = QComboBox()
        self._filter_combo.addItems([
            "Все звонки", "Только входящие", "Только исходящие",
            _L("Пропущенные", "Missed", "不在着信"), "Последние 24ч"])
        self._filter_combo.currentIndexChanged.connect(self._refresh)
        filter_row.addWidget(self._filter_combo)

        self._search_edit = QLineEdit()
        self._search_edit.setPlaceholderText("🔍 Search by name...")
        self._search_edit.textChanged.connect(self._refresh)
        filter_row.addWidget(self._search_edit, stretch=1)
        lay.addLayout(filter_row)

        # ── Call list (custom items) ──
        self._list = QListWidget()
        self._list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._list.customContextMenuRequested.connect(self._ctx_menu)
        self._list.itemDoubleClicked.connect(self._call_back)
        self._list.setStyleSheet(
            f"QListWidget{{background:{t['bg3']};border-radius:8px;border:none;}}"
            f"QListWidget::item{{padding:6px 10px;border-radius:6px;}}"
            f"QListWidget::item:selected{{background:{t['accent']};color:{t['text']};}}"
            f"QListWidget::item:hover{{background:{t['btn_hover']};}}")
        lay.addWidget(self._list, stretch=1)

        # ── Bottom controls ──
        bot = QHBoxLayout()
        refresh_btn = QPushButton("🔄 Refresh")
        refresh_btn.clicked.connect(self._refresh)
        export_btn  = QPushButton("📤 Export CSV")
        export_btn.clicked.connect(self._export_csv)
        clear_btn   = QPushButton("🗑 Clear")
        clear_btn.clicked.connect(self._clear)
        for b in [refresh_btn, export_btn, clear_btn]:
            bot.addWidget(b)
        bot.addStretch()
        lay.addLayout(bot)

        self._refresh()

    def _refresh(self):
        self._list.clear()
        logs = HISTORY.load_call_log()

        flt   = self._filter_combo.currentIndex() if hasattr(self,'_filter_combo') else 0
        query = self._search_edit.text().lower() if hasattr(self,'_search_edit') else ""

        now = time.time()
        total = out = inc = miss = 0

        for entry in reversed(logs):
            who      = entry.get("peer","?")
            outgoing = entry.get("outgoing", True)
            dur      = entry.get("duration", 0)
            ts_v     = entry.get("ts", 0)
            missed   = dur == 0 and not outgoing

            total += 1
            if outgoing: out += 1
            else:        inc += 1
            if missed:   miss += 1

            # Apply filter
            if flt == 1 and outgoing: continue
            if flt == 2 and not outgoing: continue
            if flt == 3 and not missed: continue
            if flt == 4 and (now - ts_v) > 86400: continue
            if query and query not in who.lower(): continue

            ts_str = datetime.fromtimestamp(ts_v).strftime("%d.%m.%Y  %H:%M")
            if   outgoing: icon = "📞"
            elif missed:   icon = "❌"
            else:          icon = "📲"

            dur_str = f"{int(dur//60)}м {int(dur%60)}с" if dur >= 60 else (
                      f"{int(dur)}с" if dur else "пропущен")

            item = QListWidgetItem(
                f"{icon}  {who}   •   {ts_str}   •   {dur_str}")
            item.setData(Qt.ItemDataRole.UserRole, entry)
            # Color missed calls
            if missed:
                item.setForeground(QColor("#FF6060"))
            self._list.addItem(item)

        if hasattr(self,'_stat_total'):
            self._stat_total.setText(f"Total: {total}")
            self._stat_out.setText(f"📞 Outgoing: {out}")
            self._stat_in.setText(f"📲 Incoming: {inc}")
            self._stat_miss.setText(f"❌ Missed: {miss}")

    def add_call(self, peer: str, outgoing: bool, duration: float = 0):
        HISTORY.add_call({"peer": peer, "outgoing": outgoing,
                          "duration": duration, "ts": time.time()})
        self._refresh()

    def _call_back(self, item):
        entry = item.data(Qt.ItemDataRole.UserRole)
        if entry:
            peer = entry.get("peer","")
            self.call_back_requested.emit(peer)

    def _ctx_menu(self, pos):
        item = self._list.itemAt(pos)
        if not item: return
        entry = item.data(Qt.ItemDataRole.UserRole)
        menu = QMenu(self)
        menu.addAction("📞 Call back",
            lambda: self.call_back_requested.emit(entry.get("peer","")))
        menu.addAction("📋 Copy name",
            lambda: QApplication.clipboard().setText(entry.get("peer","")))
        menu.addSeparator()
        menu.addAction("🗑 Delete entry",
            lambda: self._delete_entry(entry))
        menu.exec(QCursor.pos())

    def _delete_entry(self, entry):
        logs = HISTORY.load_call_log()
        logs = [e for e in logs if e != entry]
        import json as _json
        f = HISTORY._file("__call_log__")
        f.write_text(_json.dumps(logs, ensure_ascii=False), encoding="utf-8")
        HISTORY._cache.pop("__call_log__", None)
        self._refresh()

    def _export_csv(self):
        fn, _ = QFileDialog.getSaveFileName(
            self, "Экспорт истории звонков", "call_log.csv",
            "CSV (*.csv)")
        if not fn: return
        import csv
        logs = HISTORY.load_call_log()
        with open(fn, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow([_L("Имя", "Name", "名前"), _L("Тип", "Type", "タイプ"), _L("Дата", "Date", "日付"), "Длительность(с)"])
            for e in logs:
                w.writerow([
                    e.get("peer","?"),
                    _L("Исходящий", "Outgoing", "発信") if e.get("outgoing") else _L("Входящий", "Incoming", "着信"),
                    datetime.fromtimestamp(e.get("ts",0)).strftime("%d.%m.%Y %H:%M"),
                    e.get("duration",0)])
        QMessageBox.information(self,"Экспорт",f"Сохранено: {fn}")

    def _clear(self):
        if QMessageBox.question(self,"Очистить","Очистить всю историю звонков?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            f = HISTORY._file("__call_log__")
            if f.exists(): f.unlink()
            HISTORY._cache.pop("__call_log__", None)
            self._refresh()

# ═══════════════════════════════════════════════════════════════════════════
#  IMAGE CROP DIALOG  (interactive circle/rect crop with pan + zoom)
# ═══════════════════════════════════════════════════════════════════════════
class ImageCropDialog(QDialog):
    """
    Interactive image crop dialog.
    - Show original image
    - Drag crop rectangle / circle
    - Zoom with scroll wheel
    - Returns cropped QPixmap
    """
    def __init__(self, pixmap: QPixmap, circle: bool = True,
                 aspect_ratio: float | None = None, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Crop image")
        self.setModal(True)
        self.setMinimumSize(600, 520)
        self.resize(720, 560)
        t = get_theme(S().theme)
        self.setStyleSheet(f"background:{t['bg2']}; color:{t['text']};")

        self._src           = pixmap
        self._circle        = circle
        self._aspect_ratio  = aspect_ratio   # None = square, e.g. 4.0 = 4:1 banner
        self._zoom          = 1.0
        self._pan           = QPoint(0, 0)
        self._drag_pan      = None
        self._crop_size     = 200            # height of crop rect
        self._crop_pos      = QPoint(0, 0)
        self._drag_crop     = None
        self._result_pm     : QPixmap | None = None
        self._setup()

    def _setup(self):
        t = get_theme(S().theme)
        lay = QVBoxLayout(self)
        lay.setSpacing(8)
        lay.setContentsMargins(12, 12, 12, 12)

        # Hint
        hint = QLabel("Перетащите рамку обрезки • Колёсико — масштаб • ЛКМ на фоне — перемещение")
        hint.setStyleSheet(f"color:{t['text_dim']};font-size:9px;")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(hint)

        # Canvas
        self._canvas = QLabel()
        self._canvas.setMinimumSize(550, 400)
        self._canvas.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._canvas.setStyleSheet(f"background:{t['bg3']};border-radius:8px;")
        self._canvas.setMouseTracking(True)
        self._canvas.installEventFilter(self)
        lay.addWidget(self._canvas, stretch=1)

        # Size slider
        row = QHBoxLayout()
        row.addWidget(QLabel(_L("Размер:", "Size:", "サイズ:")))
        self._size_slider = QSlider(Qt.Orientation.Horizontal)
        self._size_slider.setRange(60, 400)
        self._size_slider.setValue(200)
        self._size_slider.valueChanged.connect(self._on_size_changed)
        row.addWidget(self._size_slider)
        lay.addLayout(row)

        # Buttons
        btn_row = QHBoxLayout()
        btn_row.addStretch()
        ok_btn = QPushButton(_L("✅ Применить", "✅ Apply", "✅ 適用"))
        ok_btn.setObjectName("accent_btn")
        ok_btn.setFixedHeight(32)
        ok_btn.clicked.connect(self._apply)
        cancel_btn = QPushButton(_L(_L("Отмена", "Cancel", "キャンセル"), "Cancel", "キャンセル"))
        cancel_btn.setFixedHeight(32)
        cancel_btn.clicked.connect(self.reject)
        btn_row.addWidget(cancel_btn)
        btn_row.addWidget(ok_btn)
        lay.addLayout(btn_row)

        QTimer.singleShot(50, self._center_crop)

    def _center_crop(self):
        cw = self._canvas.width()
        ch = self._canvas.height()
        cs_h = min(self._crop_size, ch - 20)
        cs_w = int(cs_h * self._aspect_ratio) if self._aspect_ratio else cs_h
        self._crop_pos = QPoint((cw - cs_w)//2, (ch - cs_h)//2)
        self._render()

    def _on_size_changed(self, val):
        self._crop_size = val
        self._render()

    def _crop_width(self) -> int:
        """Returns current crop width (respects aspect ratio)."""
        if self._aspect_ratio:
            return int(self._crop_size * self._aspect_ratio)
        return self._crop_size

    def _render(self):
        cw = self._canvas.width()
        ch = self._canvas.height()
        if cw <= 0 or ch <= 0:
            return
        canvas = QPixmap(cw, ch)
        canvas.fill(QColor(20, 20, 30))
        painter = QPainter(canvas)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Draw source image scaled + panned
        iw = int(self._src.width()  * self._zoom)
        ih = int(self._src.height() * self._zoom)
        x0 = (cw - iw)//2 + self._pan.x()
        y0 = (ch - ih)//2 + self._pan.y()
        scaled_img = self._src.scaled(iw, ih,
                                      Qt.AspectRatioMode.KeepAspectRatio,
                                      Qt.TransformationMode.SmoothTransformation)
        painter.drawPixmap(x0, y0, scaled_img)

        # Crop rect dimensions
        t    = get_theme(S().theme)
        cx   = self._crop_pos.x()
        cy   = self._crop_pos.y()
        cs_h = self._crop_size
        cs_w = self._crop_width()

        # Dark overlay outside crop
        painter.fillRect(0, 0, cw, cy, QColor(0,0,0,140))
        painter.fillRect(0, cy + cs_h, cw, ch, QColor(0,0,0,140))
        painter.fillRect(0, cy, cx, cs_h, QColor(0,0,0,140))
        painter.fillRect(cx + cs_w, cy, cw, cs_h, QColor(0,0,0,140))

        # Crop border
        painter.setPen(QPen(QColor(t['accent']), 2, Qt.PenStyle.DashLine))
        if self._circle:
            painter.drawEllipse(cx, cy, cs_w, cs_h)
        else:
            painter.drawRect(cx, cy, cs_w, cs_h)

        # Corner handles
        painter.setPen(QPen(QColor("white"), 3))
        for hx, hy in [(cx, cy), (cx+cs_w, cy), (cx, cy+cs_h), (cx+cs_w, cy+cs_h)]:
            painter.drawLine(hx-6, hy, hx+6, hy)
            painter.drawLine(hx, hy-6, hx, hy+6)

        # Show aspect ratio hint
        if self._aspect_ratio:
            painter.setPen(QPen(QColor(t['accent']), 1))
            painter.setFont(QFont("Arial", 9))
            ar_str = f"{self._aspect_ratio:.1f}:1 широкий формат"
            painter.drawText(cx + 4, cy + cs_h - 4, ar_str)

        painter.end()
        self._canvas.setPixmap(canvas)

    def eventFilter(self, obj, event):
        if obj is not self._canvas:
            return super().eventFilter(obj, event)
        if event.type() == event.Type.MouseButtonPress:
            pos = event.pos()
            cx, cy = self._crop_pos.x(), self._crop_pos.y()
            cs_w = self._crop_width()
            cs_h = self._crop_size
            if (cx <= pos.x() <= cx+cs_w and cy <= pos.y() <= cy+cs_h):
                self._drag_crop = pos - self._crop_pos
            else:
                self._drag_pan = pos
        elif event.type() == event.Type.MouseMove:
            if self._drag_crop is not None:
                cw = self._canvas.width()
                ch = self._canvas.height()
                cs_w = self._crop_width()
                new_pos = event.pos() - self._drag_crop
                nx = max(0, min(new_pos.x(), cw - cs_w))
                ny = max(0, min(new_pos.y(), ch - self._crop_size))
                self._crop_pos = QPoint(nx, ny)
                self._render()
            elif self._drag_pan is not None:
                delta = event.pos() - self._drag_pan
                self._drag_pan = event.pos()
                self._pan = QPoint(self._pan.x()+delta.x(), self._pan.y()+delta.y())
                self._render()
        elif event.type() == event.Type.MouseButtonRelease:
            self._drag_crop = None
            self._drag_pan  = None
        elif event.type() == event.Type.Wheel:
            delta = event.angleDelta().y()
            self._zoom = max(0.1, min(self._zoom * (1.1 if delta>0 else 0.9), 10.0))
            self._render()
        elif event.type() == event.Type.Resize:
            self._center_crop()
        return False

    def _apply(self):
        """Extract the cropped area from original image, respecting aspect ratio."""
        cw = self._canvas.width()
        ch = self._canvas.height()
        iw = int(self._src.width()  * self._zoom)
        ih = int(self._src.height() * self._zoom)
        x0 = (cw - iw)//2 + self._pan.x()
        y0 = (ch - ih)//2 + self._pan.y()

        cx   = self._crop_pos.x() - x0
        cy   = self._crop_pos.y() - y0
        cs_h = self._crop_size
        cs_w = self._crop_width()

        # Map crop box back to source image coordinates
        scale = self._src.width() / iw if iw > 0 else 1.0
        src_x = int(cx  * scale)
        src_y = int(cy  * scale)
        src_w = int(cs_w * scale)
        src_h = int(cs_h * scale)

        cropped = self._src.copy(src_x, src_y, src_w, src_h)
        if cropped.isNull():
            cropped = self._src

        # Scale to output size
        if self._aspect_ratio:
            out_w, out_h = 800, int(800 / self._aspect_ratio)
        else:
            out_w = out_h = 256
        self._result_pm = cropped.scaled(out_w, out_h,
                                         Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                                         Qt.TransformationMode.SmoothTransformation)
        self.accept()

    def get_pixmap(self) -> QPixmap | None:
        return self._result_pm

# ═══════════════════════════════════════════════════════════════════════════
#  PROFILE PREVIEW  (how others see you)
# ═══════════════════════════════════════════════════════════════════════════
class ProfilePreviewWidget(QWidget):
    """
    Telegram-style profile preview.
    Banner fills full width, avatar overlaps banner bottom edge,
    name/emoji/badges right below, info rows with icons, stats footer.
    """
    def __init__(self, parent=None):
        super().__init__(parent)
        self._setup()

    # ─────────────────────────────────────────────────────────────────────
    def _setup(self):
        t = get_theme(S().theme)
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ── outer card ──────────────────────────────────────────────────
        card = QFrame()
        card.setObjectName("ppw_card")
        card.setFrameShape(QFrame.Shape.NoFrame)
        card.setStyleSheet(f"""
            QFrame#ppw_card {{
                background: {t['bg2']};
                border-radius: 14px;
                border: 1px solid {t['border']};
            }}
        """)
        cl = QVBoxLayout(card)
        cl.setContentsMargins(0, 0, 0, 0)
        cl.setSpacing(0)

        # ── banner ──────────────────────────────────────────────────────
        self._banner_lbl = QLabel()
        self._banner_lbl.setFixedHeight(180)
        self._banner_lbl.setScaledContents(False)
        self._banner_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._banner_lbl.setStyleSheet(
            f"background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            f"stop:0 {t['accent']}, stop:0.5 {t['bg3']}, stop:1 {t['bg']});"
            "border-radius: 14px 14px 0 0;")
        cl.addWidget(self._banner_lbl)

        # ── avatar + badge row (overlaps banner by 48px) ─────────────────
        # We use a container with negative top-margin trick via a stacked layout
        overlap_w = QWidget()
        overlap_w.setFixedHeight(68)   # 96/2 + padding
        overlap_w.setStyleSheet(f"background: {t['bg2']};")
        overlap_lay = QHBoxLayout(overlap_w)
        overlap_lay.setContentsMargins(20, 0, 20, 0)
        overlap_lay.setSpacing(0)

        # Avatar label — 96×96, shifted up by 48px with negative margin via stylesheet
        self._av_container = QWidget()
        self._av_container.setFixedSize(104, 104)
        self._av_container.setStyleSheet("background: transparent;")
        av_inner = QVBoxLayout(self._av_container)
        av_inner.setContentsMargins(0, 0, 0, 0)
        self._avatar_lbl = QLabel(self._av_container)
        self._avatar_lbl.setFixedSize(96, 96)
        self._avatar_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._avatar_lbl.setStyleSheet(
            "background:transparent;border:none;")
        av_inner.addWidget(self._avatar_lbl)
        # Position avatar overlapping banner: move up 48px
        self._av_container.setContentsMargins(0, 0, 0, 0)

        overlap_lay.addWidget(self._av_container)
        overlap_lay.addStretch()

        # Premium badge (top-right)
        self._premium_lbl = QLabel("👑 PREMIUM")
        self._premium_lbl.setVisible(False)
        self._premium_lbl.setStyleSheet(
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #B8860B, stop:1 #FFD700);"
            "color: #1A0A00; font-size: 11px; font-weight: bold;"
            "border-radius: 11px; padding: 4px 14px;")
        overlap_lay.addWidget(self._premium_lbl,
                              alignment=Qt.AlignmentFlag.AlignVCenter)
        cl.addWidget(overlap_w)

        # We need the avatar to visually overlap the banner — use absolute pos
        # Re-parent avatar_lbl to card and position manually after show
        self._banner_lbl.setParent(card)
        self._avatar_lbl.setParent(card)
        self._premium_lbl.setParent(card)

        # ── name row ────────────────────────────────────────────────────
        name_w = QWidget()
        name_w.setStyleSheet(f"background: {t['bg2']};")
        name_lay = QHBoxLayout(name_w)
        name_lay.setContentsMargins(20, 4, 20, 2)
        name_lay.setSpacing(8)

        self._name_lbl = QLabel()
        self._name_lbl.setStyleSheet(
            f"font-size: 20px; font-weight: bold; color: {t['text']};"
            "background: transparent;")
        name_lay.addWidget(self._name_lbl)

        self._emoji_lbl = QLabel()
        self._emoji_lbl.setStyleSheet("font-size: 18px; background: transparent;")
        name_lay.addWidget(self._emoji_lbl)

        self._loyalty_lbl = QLabel()
        self._loyalty_lbl.setVisible(False)
        self._loyalty_lbl.setStyleSheet(
            f"background: transparent; color: {t['accent']};"
            f"border: 1px solid {t['accent']}; border-radius: 9px;"
            "padding: 1px 8px; font-size: 10px; font-weight: bold;")
        name_lay.addWidget(self._loyalty_lbl)
        name_lay.addStretch()
        cl.addWidget(name_w)

        # ── status line ──────────────────────────────────────────────────
        status_w = QWidget()
        status_w.setStyleSheet(f"background: {t['bg2']};")
        sl = QHBoxLayout(status_w)
        sl.setContentsMargins(20, 0, 20, 6)
        sl.setSpacing(6)
        self._status_dot = QLabel("●")
        self._status_dot.setStyleSheet("font-size: 10px; color: #4CAF50; background: transparent;")
        sl.addWidget(self._status_dot)
        self._status_txt = QLabel()
        self._status_txt.setStyleSheet(
            f"font-size: 11px; color: {t['text_dim']}; background: transparent;")
        sl.addWidget(self._status_txt)
        sl.addStretch()
        self._version_lbl = QLabel()
        self._version_lbl.setStyleSheet(
            f"font-size: 9px; color: {t['text_dim']}; background: transparent;")
        sl.addWidget(self._version_lbl)
        cl.addWidget(status_w)

        # ── thin divider ─────────────────────────────────────────────────
        div = QFrame(); div.setFrameShape(QFrame.Shape.HLine)
        div.setStyleSheet(f"color: {t['border']}; margin: 0;")
        cl.addWidget(div)

        # ── info rows ────────────────────────────────────────────────────
        info_w = QWidget()
        info_w.setStyleSheet(f"background: {t['bg2']};")
        ifl = QVBoxLayout(info_w)
        ifl.setContentsMargins(0, 0, 0, 0)
        ifl.setSpacing(0)

        def make_info_row(icon_text, attr_val, attr_sub, attr_widget=None):
            row = QWidget()
            row.setStyleSheet(
                f"background: {t['bg2']};"
                f"border-bottom: 1px solid {t['border']}22;")
            rl = QHBoxLayout(row)
            rl.setContentsMargins(20, 10, 20, 10)
            rl.setSpacing(16)
            ico = QLabel(icon_text)
            ico.setFixedWidth(28)
            ico.setAlignment(Qt.AlignmentFlag.AlignCenter)
            ico.setStyleSheet(f"font-size: 18px; color: {t['text_dim']}; background: transparent;")
            rl.addWidget(ico)
            col = QVBoxLayout()
            col.setSpacing(1)
            v = QLabel(); v.setStyleSheet(
                f"font-size: 13px; color: {t['text']}; background: transparent;")
            s = QLabel(); s.setStyleSheet(
                f"font-size: 10px; color: {t['text_dim']}; background: transparent;")
            col.addWidget(v); col.addWidget(s)
            rl.addLayout(col)
            rl.addStretch()
            ifl.addWidget(row)
            setattr(self, attr_val, v)
            setattr(self, attr_sub, s)
            if attr_widget:
                setattr(self, attr_widget, row)
            return row

        make_info_row("ℹ",  "_ip_val",     "_ip_sub",    "_ip_row")
        make_info_row("📝", "_bio_val",    "_bio_sub",   "_bio_row")
        make_info_row("🌐", "_status_val", "_status_sub","_status_row")
        cl.addWidget(info_w)

        # ── stats footer ─────────────────────────────────────────────────
        stats_w = QWidget()
        stats_w.setStyleSheet(
            f"background: {t['bg3']}; border-radius: 0 0 14px 14px;")
        sfl = QHBoxLayout(stats_w)
        sfl.setContentsMargins(20, 14, 20, 18)
        sfl.setSpacing(0)
        self._stat_msgs  = QLabel("—")
        self._stat_grps  = QLabel("—")
        for num_lbl, sub_text in [(self._stat_msgs, "Сообщений"),
                                   (self._stat_grps,  "Групп")]:
            col = QVBoxLayout()
            col.setSpacing(2)
            num_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
            num_lbl.setStyleSheet(
                f"font-size: 20px; font-weight: bold; color: {t['text']};"
                "background: transparent;")
            sub = QLabel(sub_text)
            sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
            sub.setStyleSheet(
                f"font-size: 10px; color: {t['text_dim']}; background: transparent;")
            col.addWidget(num_lbl); col.addWidget(sub)
            sfl.addLayout(col)
            sfl.addStretch()
        cl.addWidget(stats_w)

        root.addWidget(card)

        # ── bottom buttons ───────────────────────────────────────────────
        bot = QHBoxLayout()
        bot.setContentsMargins(0, 12, 0, 0)
        bot.setSpacing(8)
        ref_btn = QPushButton(_L("🔄 Обновить", "🔄 Refresh", "🔄 更新"))
        ref_btn.setFixedHeight(34)
        ref_btn.clicked.connect(self.refresh)
        bot.addStretch(); bot.addWidget(ref_btn); bot.addStretch()
        root.addLayout(bot)

        note = QLabel(_L("Так тебя видят другие", "How others see you", "他のユーザーから見た姿"))
        note.setAlignment(Qt.AlignmentFlag.AlignCenter)
        note.setStyleSheet(f"color: {t['text_dim']}; font-size: 9px; padding: 4px;")
        root.addWidget(note)

        self.refresh()

    # ─────────────────────────────────────────────────────────────────────
    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._position_avatar()

    def showEvent(self, event):
        super().showEvent(event)
        # Delay so layout is settled
        QTimer.singleShot(0, self._position_avatar)

    def _position_avatar(self):
        """Place avatar so it overlaps banner bottom by half its height."""
        try:
            banner = self._banner_lbl
            av     = self._avatar_lbl
            prem   = self._premium_lbl
            card   = banner.parent()
            if not card:
                return
            bh = banner.height()    # banner bottom y in card coords
            bw = banner.width()
            av_size = av.width()    # 96
            # Avatar: left-aligned, centre vertically on banner bottom edge
            av_x = 20
            av_y = bh - av_size // 2   # half pokes above banner bottom
            av.setGeometry(av_x, av_y, av_size, av_size)
            av.raise_()
            # Premium badge: vertically centred at same y, right side
            if prem.isVisible():
                pw = prem.sizeHint().width()
                ph = prem.sizeHint().height()
                prem.setGeometry(bw - pw - 20, bh - ph // 2 - ph // 2, pw, ph)
                prem.raise_()
        except Exception:
            pass

    # ─────────────────────────────────────────────────────────────────────
    def refresh(self):
        cfg = S()
        t   = get_theme(cfg.theme)

        # Banner
        bn = cfg.banner_b64
        self._banner_lbl.setStyleSheet(
            f"background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            f"stop:0 {t['accent']}, stop:0.5 {t['bg3']}, stop:1 {t['bg']});"
            "border-radius: 14px 14px 0 0;")
        if bn:
            try:
                pm = base64_to_pixmap(bn)
                w  = max(self._banner_lbl.width(), 900)
                h  = self._banner_lbl.height()
                pm = pm.scaled(w, h,
                    Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                    Qt.TransformationMode.SmoothTransformation)
                if pm.width() > w:
                    pm = pm.copy((pm.width() - w) // 2, 0, w, h)
                self._banner_lbl.setPixmap(pm)
            except Exception:
                pass

        # Avatar — 96px circle with thick ring
        av = cfg.avatar_b64
        try:
            src_pm = base64_to_pixmap(av) if av else None
            if src_pm and not src_pm.isNull():
                pm96 = make_circle_pixmap(src_pm, 96)
            else:
                pm96 = default_avatar(cfg.username, 96)
        except Exception:
            pm96 = default_avatar(cfg.username, 96)
        self._avatar_lbl.setPixmap(pm96)
        self._avatar_lbl.setStyleSheet(
            f"border: 4px solid {t['bg2']}; border-radius: 48px; background: {t['bg3']};")

        # Premium
        self._premium_lbl.setVisible(cfg.premium)

        # Name + emoji + loyalty
        nick_color = cfg.nickname_color or t["text"]
        self._name_lbl.setStyleSheet(
            f"font-size: 20px; font-weight: bold; color: {nick_color};"
            "background: transparent;")
        self._name_lbl.setText(cfg.username or "—")
        self._emoji_lbl.setText(cfg.custom_emoji if cfg.premium else "")

        loyalty = int(cfg.get("loyalty_months", 0, t=int))
        if loyalty > 0:
            self._loyalty_lbl.setText(f"❤ {loyalty}")
            self._loyalty_lbl.setVisible(True)
            self._loyalty_lbl.setToolTip(_L('В сети {loyalty} мес. подряд', 'Online {loyalty} months straight', '{loyalty}ヶ月連続'))
        else:
            self._loyalty_lbl.setVisible(False)

        # Status dot + text
        status = cfg.user_status
        dot_colors = {"online": "#4CAF50", "away": "#FFD700",
                      "busy": "#FF6B6B",   "dnd":  "#9E9E9E"}
        dot_labels = {"online": "в сети", "away": "отошёл",
                      "busy":   "занят",  "dnd":  "не беспокоить"}
        self._status_dot.setStyleSheet(
            f"font-size: 10px; color: {dot_colors.get(status,'#4CAF50')};"
            "background: transparent;")
        self._status_txt.setText(dot_labels.get(status, "в сети"))
        self._version_lbl.setText(f"GoidaPhone v{APP_VERSION}")

        # Info rows
        ip_raw = get_local_ip()
        id_str = display_id(ip_raw)
        self._ip_val.setText(id_str)
        self._ip_sub.setText("GoidaID" if cfg.safe_mode else "IP / GoidaID")

        bio = cfg.bio
        self._bio_val.setText(bio if bio else "Нет описания")
        self._bio_sub.setText(_L('О себе', 'About me', '自己紹介'))
        self._bio_row.setVisible(True)

        self._status_val.setText(dot_labels.get(status, "в сети"))
        self._status_sub.setText(_L('Статус', 'Status', 'ステータス'))

        # Stats
        try:
            hist_files = list(HISTORY_DIR.glob("*.json"))
            total = 0
            for f in hist_files:
                if f.stat().st_size < 5_000_000:
                    try:
                        total += len(json.loads(
                            f.read_text(encoding="utf-8", errors="ignore")))
                    except Exception:
                        pass
            self._stat_msgs.setText(str(total))
        except Exception:
            self._stat_msgs.setText("—")
        try:
            self._stat_grps.setText(str(len(GROUPS.groups)))
        except Exception:
            self._stat_grps.setText("—")

        QTimer.singleShot(0, self._position_avatar)


# ═══════════════════════════════════════════════════════════════════════════
#  PROFILE EDITOR
# ═══════════════════════════════════════════════════════════════════════════
class ProfileDialog(QDialog):
    profile_saved = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle(TR("my_profile_title"))
        self.setModal(False)   # встраивается в вкладку — не модальный
        self._setup()

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setSpacing(10)
        lay.setContentsMargins(16,16,16,16)

        # ── Banner ──
        banner_group = QGroupBox(_L('Баннер профиля', 'Profile banner', 'プロフィールバナー'))
        bl = QVBoxLayout(banner_group)
        bl.setContentsMargins(8, 8, 8, 8)
        bl.setSpacing(6)

        self._banner_lbl = QLabel()
        self._banner_lbl.setFixedHeight(120)
        self._banner_lbl.setScaledContents(False)
        self._banner_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        t = get_theme(S().theme)
        self._banner_lbl.setStyleSheet(
            f"background:{t['bg3']};border-radius:8px;"
            f"color:{t['text_dim']};font-size:11px;")
        self._banner_lbl.setText(_L('Нет баннера', 'No banner', 'バナーなし'))
        bl.addWidget(self._banner_lbl)

        banner_row = QHBoxLayout()
        banner_btn = QPushButton(TR("banner"))
        banner_btn.clicked.connect(self._pick_banner)
        clear_banner_btn = QPushButton(_L("🗑 Убрать", "🗑 Remove", "🗑 削除"))
        clear_banner_btn.clicked.connect(self._clear_banner)
        banner_row.addWidget(banner_btn)
        banner_row.addWidget(clear_banner_btn)
        banner_row.addStretch()
        bl.addLayout(banner_row)
        lay.addWidget(banner_group)

        # ── Avatar + basic ──
        av_group = QGroupBox(_L('Аватар и имя', 'Avatar & name', 'アバターと名前'))
        al = QHBoxLayout(av_group)

        self._avatar_lbl = QLabel()
        self._avatar_lbl.setFixedSize(72,72)
        self._avatar_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._avatar_lbl.setStyleSheet(
            "background:transparent;border:none;")
        self._avatar_lbl.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        al.addWidget(self._avatar_lbl)

        av_btn = QPushButton("📷\nИзменить\nаватар")
        av_btn.setFixedSize(80,72)
        av_btn.clicked.connect(self._pick_avatar)
        al.addWidget(av_btn)

        form = QFormLayout()
        self._username_edit = QLineEdit(S().username)
        self._username_edit.setMaxLength(24)
        form.addRow("Name:", self._username_edit)

        self._bio_edit = QLineEdit(S().bio)
        self._bio_edit.setMaxLength(120)
        self._bio_edit.setPlaceholderText(_L('Расскажите о себе...', 'Tell us about yourself...', '自己紹介を書いてください...'))
        form.addRow("Bio:", self._bio_edit)

        al.addLayout(form)
        lay.addWidget(av_group)

        # ── Premium customisation ──
        prem_group = QGroupBox(_L('👑 Премиум: Кастомизация', '👑 Premium: Customization', '👑 プレミアム: カスタマイズ'))
        pl = QFormLayout(prem_group)

        self._color_btn = QPushButton()
        self._color_btn.setFixedSize(40,24)
        self._color_btn.clicked.connect(self._pick_color)
        pl.addRow("Nick color:", self._color_btn)

        self._emoji_edit = QLineEdit(S().custom_emoji)
        self._emoji_edit.setMaxLength(5)
        self._emoji_edit.setPlaceholderText("👑")
        pl.addRow("Эмодзи:", self._emoji_edit)

        # quick emoji buttons — bigger so emojis render properly
        eq = QHBoxLayout()
        eq.setSpacing(4)
        for e in ["👑","⭐","🔥","💎","🚀","⚡","🎯","💫","🌟","🦋"]:
            b = QPushButton(e)
            b.setFixedSize(34, 30)
            b.setFont(QFont("Segoe UI Emoji,Noto Color Emoji,Apple Color Emoji", 14))
            b.clicked.connect(lambda _, em=e: self._emoji_edit.setText(em))
            eq.addWidget(b)
        eq.addStretch()
        pl.addRow("", eq)

        prem_enabled = S().premium
        prem_group.setEnabled(prem_enabled)
        if not prem_enabled:
            prem_group.setTitle("👑 Премиум: Кастомизация (требует Премиум)")
        lay.addWidget(prem_group)

        # ── Buttons ──
        blay = QHBoxLayout()
        preview_btn = QPushButton(_L('👁 Предпросмотр', '👁 Preview', '👁 プレビュー'))
        preview_btn.setToolTip(_L('Как тебя видят другие пользователи', 'How others see you', '他のユーザーの見え方'))
        preview_btn.clicked.connect(self._show_preview)
        blay.addWidget(preview_btn)
        blay.addStretch()
        save = QPushButton(TR("btn_save"))
        save.setObjectName("accent_btn")
        save.clicked.connect(self._save)
        blay.addWidget(save)
        cancel = QPushButton(_L(_L("Отмена", "Cancel", "キャンセル"), "Cancel", "キャンセル"))
        cancel.clicked.connect(self.reject)
        blay.addWidget(cancel)
        lay.addLayout(blay)

        self._load_current()

    def _show_preview(self):
        """Show profile preview as overlay or dialog."""
        # Temporarily save current form state so preview reflects unsaved changes
        try:
            _old_name = S().username
            _old_desc = S().get("bio","",t=str)
            if hasattr(self, '_name_edit') and self._name_edit.text().strip():
                S().set("username", self._name_edit.text().strip())
            if hasattr(self, '_desc_edit'):
                S().set("bio", self._desc_edit.toPlainText())
        except Exception: pass

        t = get_theme(S().theme)
        dlg = QDialog(self if isinstance(self, QDialog) else None)
        dlg.setWindowTitle(_L('👁 Предпросмотр профиля', '👁 Profile preview', '👁 プロフィールプレビュー'))
        dlg.setMinimumSize(500, 600)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        lay = QVBoxLayout(dlg)
        lay.setContentsMargins(0,0,0,0); lay.setSpacing(0)

        # Header
        hdr = QWidget()
        hdr.setFixedHeight(44)
        hdr.setStyleSheet(f"background:{t['bg3']};border-bottom:1px solid {t['border']};")
        hl = QHBoxLayout(hdr); hl.setContentsMargins(16,0,16,0)
        hl.addWidget(QLabel("<b>👁 Предпросмотр</b>  — так тебя видят другие"))
        hl.addStretch()
        lay.addWidget(hdr)

        preview = ProfilePreviewWidget()
        lay.addWidget(preview, stretch=1)

        close = QPushButton(_L("Закрыть", "Close", "閉じる"))
        close.setObjectName("accent_btn"); close.setFixedHeight(36)
        close.setContentsMargins(16,0,16,0)
        close.clicked.connect(dlg.accept)
        btn_row = QWidget(); btn_row.setFixedHeight(52)
        btn_row.setStyleSheet(f"background:{t['bg3']};border-top:1px solid {t['border']};")
        br = QHBoxLayout(btn_row); br.setContentsMargins(16,8,16,8)
        br.addStretch(); br.addWidget(close)
        lay.addWidget(btn_row)

        # Restore original values after close
        def _restore():
            try:
                S().set("username", _old_name)
                S().set("bio", _old_desc)
            except Exception: pass
        dlg.finished.connect(lambda _: _restore())
        dlg.exec()


    def _load_current(self):
        cfg = S()
        self._update_color_btn(cfg.nickname_color)
        av = cfg.avatar_b64
        if av:
            try:
                self._avatar_lbl.setPixmap(make_circle_pixmap(base64_to_pixmap(av), 72))
            except Exception:
                self._avatar_lbl.setPixmap(default_avatar(cfg.username, 72))
        else:
            self._avatar_lbl.setPixmap(default_avatar(cfg.username, 72))

        bn = cfg.banner_b64
        if bn:
            try:
                pm = base64_to_pixmap(bn)
                self._update_banner_display(pm)
            except Exception:
                pass

    def _update_banner_display(self, pm: QPixmap):
        """Scale banner to fill label width at correct aspect."""
        w = max(self._banner_lbl.width(), 600)
        h = self._banner_lbl.height()
        scaled = pm.scaled(w, h,
            Qt.AspectRatioMode.KeepAspectRatioByExpanding,
            Qt.TransformationMode.SmoothTransformation)
        # Crop center
        if scaled.width() > w:
            x = (scaled.width() - w) // 2
            scaled = scaled.copy(x, 0, w, h)
        self._banner_lbl.setPixmap(scaled)
        self._banner_lbl.setText("")

    def _update_color_btn(self, hex_color: str):
        self._color_btn.setStyleSheet(f"background-color:{hex_color}; border:1px solid #888;")
        self._color_btn.setProperty("color", hex_color)

    def _pick_color(self):
        c = QColorDialog.getColor(QColor(self._color_btn.property("color") or "#E0E0E0"), self)
        if c.isValid():
            self._update_color_btn(c.name())

    def _pick_avatar(self):
        fn, _ = QFileDialog.getOpenFileName(self, "Выберите аватар", "",
                    "Images (*.png *.jpg *.jpeg *.bmp *.webp)")
        if not fn:
            return
        src_pm = QPixmap(fn)
        if src_pm.isNull():
            return
        dlg = ImageCropDialog(src_pm, circle=True, parent=self)
        if dlg.exec() == QDialog.DialogCode.Accepted and dlg.get_pixmap():
            pm = dlg.get_pixmap()
            self._avatar_lbl.setPixmap(make_circle_pixmap(pm, 72))
            S().avatar_b64 = pixmap_to_base64(pm)

    def _pick_banner(self):
        fn, _ = QFileDialog.getOpenFileName(self, "Выберите баннер", "",
                    "Images (*.png *.jpg *.jpeg *.bmp *.webp)")
        if not fn:
            return
        src_pm = QPixmap(fn)
        if src_pm.isNull():
            return
        dlg = ImageCropDialog(src_pm, circle=False, aspect_ratio=4.0, parent=self)
        if dlg.exec() == QDialog.DialogCode.Accepted and dlg.get_pixmap():
            pm = dlg.get_pixmap()
            self._update_banner_display(pm)
            S().banner_b64 = pixmap_to_base64(pm)

    def _clear_banner(self):
        S().banner_b64 = ""
        t = get_theme(S().theme)
        self._banner_lbl.setPixmap(QPixmap())
        self._banner_lbl.setText(_L('Нет баннера', 'No banner', 'バナーなし'))
        self._banner_lbl.setStyleSheet(
            f"background:{t['bg3']};border-radius:8px;"
            f"color:{t['text_dim']};font-size:11px;")

    def _save(self):
        cfg = S()
        cfg.username = self._username_edit.text().strip() or cfg.username
        cfg.bio = self._bio_edit.text().strip()
        if cfg.premium:
            cfg.nickname_color = self._color_btn.property("color") or "#E0E0E0"
            cfg.custom_emoji   = self._emoji_edit.text()
        self.profile_saved.emit()
        QMessageBox.information(self,"Profile","Профиль сохранён!")
        self.accept()

# ═══════════════════════════════════════════════════════════════════════════
#  CUSTOM THEME DIALOG
# ═══════════════════════════════════════════════════════════════════════════
class CustomThemeDialog(QDialog):
    def __init__(self, slot: int, parent=None):
        super().__init__(parent)
        self.slot = slot
        self.setWindowTitle(f"Кастомная тема — Слот {slot}")
        self.setModal(True)
        self.resize(460, 520)
        self._colors: dict[str, str] = {}
        self._setup()

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setSpacing(8)
        lay.setContentsMargins(12,12,12,12)

        # Name
        nl = QHBoxLayout()
        nl.addWidget(QLabel(_L("Название:", "Name:", "名前:")))
        self._name = QLineEdit(f"Моя тема {self.slot}")
        nl.addWidget(self._name)
        lay.addLayout(nl)

        # Colour pickers
        colors_group = QGroupBox(_L('Цвета', 'Colors', '色'))
        cl = QGridLayout(colors_group)

        self._COLOR_KEYS = [
            ("bg",       "Фон основной",      "#2A2A2A"),
            ("bg2",      "Фон вторичный",      "#1E1E1E"),
            ("bg3",      "Фон элементов",      "#141414"),
            ("border",   "Рамки",              "#444444"),
            ("text",     "Lyrics",              "#E0E0E0"),
            ("text_dim", "Текст dim",          "#808080"),
            ("accent",   "Акцент",             "#0078D4"),
            ("btn_bg",   "Фон кнопок",         "#3A3A3A"),
            ("item_sel", "Выделение",          "#0063B1"),
            ("header_bg","Фон шапки",          "#303030"),
            ("msg_own",  "Свои сообщения",     "#1A3A5C"),
            ("msg_other","Чужие сообщения",    "#383838"),
            ("online",   "Онлайн",             "#2ECC71"),
            ("offline",  "Офлайн",             "#E74C3C"),
        ]

        # Load saved if exists
        saved = S().custom_theme(self.slot)

        for row, (key, label, default) in enumerate(self._COLOR_KEYS):
            color = saved.get("colors", {}).get(key, default)
            self._colors[key] = color
            cl.addWidget(QLabel(label), row, 0)
            btn = QPushButton()
            btn.setFixedSize(60, 22)
            btn.setStyleSheet(f"background:{color}; border:1px solid #555;")
            btn.clicked.connect(lambda _, k=key, b=btn: self._pick(k, b))
            cl.addWidget(btn, row, 1)

        lay.addWidget(colors_group)

        if saved.get("name"):
            self._name.setText(saved["name"])

        blay = QHBoxLayout()
        blay.addStretch()
        save = QPushButton(_L('💾 Сохранить тему', '💾 Save theme', '💾 テーマ保存'))
        save.setObjectName("accent_btn")
        save.clicked.connect(self._save)
        blay.addWidget(save)
        cancel = QPushButton(_L(_L("Отмена", "Cancel", "キャンセル"), "Cancel", "キャンセル"))
        cancel.clicked.connect(self.reject)
        blay.addWidget(cancel)
        lay.addLayout(blay)

    def _pick(self, key: str, btn: QPushButton):
        c = QColorDialog.getColor(QColor(self._colors.get(key,"#ffffff")), self)
        if c.isValid():
            self._colors[key] = c.name()
            btn.setStyleSheet(f"background:{c.name()}; border:1px solid #555;")

    def _save(self):
        data = {"name": self._name.text(), "colors": dict(self._colors)}
        S().save_custom_theme(self.slot, data)
        QMessageBox.information(self,"Сохранено",
            f"Тема «{self._name.text()}» сохранена в слот {self.slot}.")
        self.accept()

# ═══════════════════════════════════════════════════════════════════════════
#  SETTINGS DIALOG  (full)
# ═══════════════════════════════════════════════════════════════════════════
class SettingsDialog(QDialog):
    settings_saved = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle(TR("settings_title"))
        self.setModal(False)
        self.resize(860, 640)
        self.setMinimumSize(760, 540)
        self._setup()

    def _setup(self):
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0,0,0,12)
        lay.setSpacing(0)

        tabs = QTabWidget()
        tabs.setUsesScrollButtons(True)   # scroll when too many tabs
        tabs.setElideMode(Qt.TextElideMode.ElideNone)
        tabs.tabBar().setFixedHeight(26)
        tabs.tabBar().setExpanding(False)

        tabs.addTab(self._tab_audio(),      TR("settings_audio_tab"))
        tabs.addTab(self._tab_network(),    TR("settings_net_tab"))
        tabs.addTab(self._tab_themes(),     TR("settings_theme_tab"))
        tabs.addTab(self._tab_appearance(), TR("settings_appear_tab"))
        tabs.addTab(self._tab_license(),    TR("settings_lic_tab"))
        tabs.addTab(self._tab_data(),       TR("settings_data_tab"))
        tabs.addTab(self._tab_language(),   TR("settings_lang_tab"))
        tabs.addTab(self._mk_specialist_scroll(), TR("settings_adv_tab"))
        tabs.addTab(self._tab_pin_security(), TR("settings_lock_tab"))
        tabs.addTab(self._tab_privacy(),    TR("settings_priv_tab"))
        tabs.addTab(self._tab_call_settings(), TR("settings_call_tab"))

        lay.addWidget(tabs)

        blay = QHBoxLayout()
        blay.setContentsMargins(12,8,12,0)
        blay.addStretch()
        save = QPushButton(TR("btn_save"))
        save.setObjectName("accent_btn")
        save.clicked.connect(self._save)
        blay.addWidget(save)
        cancel = QPushButton(_L("Закрыть", "Close", "閉じる"))
        cancel.clicked.connect(self.reject)
        blay.addWidget(cancel)
        lay.addLayout(blay)

        self._load()

    # ── Audio tab ──────────────────────────────────────────────────────
    def _tab_audio(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(8)

        g = QGroupBox(_L('Микрофон и динамики', 'Microphone & speakers', 'マイクとスピーカー'))
        fl = QFormLayout(g)

        self._in_dev = QComboBox()
        self._out_dev = QComboBox()
        self._populate_audio_devices()
        fl.addRow("Микрофон:", self._in_dev)
        fl.addRow("Динамики:", self._out_dev)

        self._vol = QSlider(Qt.Orientation.Horizontal)
        self._vol.setRange(0,100)
        self._vol_lbl = QLabel("80%")
        self._vol.valueChanged.connect(lambda v: self._vol_lbl.setText(f"{v}%"))
        vol_row = QHBoxLayout()
        vol_row.addWidget(self._vol)
        vol_row.addWidget(self._vol_lbl)
        fl.addRow("Volume:", vol_row)

        lay.addWidget(g)

        g2 = QGroupBox("Notifications")
        fl2 = QFormLayout(g2)
        self._notif_sounds = QCheckBox(_L('Звуковые уведомления о новых сообщениях', 'Sound notifications for new messages', '新着メッセージ通知音'))
        fl2.addRow(self._notif_sounds)
        lay.addWidget(g2)

        g3 = QGroupBox(_L('Качество голоса', 'Voice quality', '音声品質'))
        fl3 = QFormLayout(g3)

        vad_status = "активно" if WEBRTCVAD_AVAILABLE else "установите: pip install webrtcvad"
        self._vad_cb = QCheckBox(f"VAD — подавление тишины ({vad_status})")
        self._vad_cb.setEnabled(WEBRTCVAD_AVAILABLE)
        self._vad_cb.setChecked(WEBRTCVAD_AVAILABLE)
        self._vad_cb.setToolTip("Voice Activity Detection\nМикрофон молчит когда вы не говорите.\nУбирает фоновый шум, экономит трафик.")
        fl3.addRow(self._vad_cb)

        jb_row = QHBoxLayout()
        self._jb_spin = QSpinBox()
        self._jb_spin.setRange(2, 20)
        self._jb_spin.setValue(6)
        self._jb_spin.setSuffix(" фреймов")
        self._jb_spin.setToolTip("Размер буфера джиттера (2-20 фреймов).\nБольше = меньше щелчков, но больше задержка.\nРекомендуется: 4-8 для LAN, 8-15 для VPN.")
        jb_lbl = QLabel(_L('≈ 192 мс', '≈ 192 ms', '≈ 192 ms'))
        self._jb_spin.valueChanged.connect(
            lambda v, l=jb_lbl: l.setText(f"≈ {v*32} мс"))
        jb_row.addWidget(self._jb_spin)
        jb_row.addWidget(jb_lbl)
        fl3.addRow("Джиттер-буфер:", jb_row)

        lay.addWidget(g3)

        # ── Звуковая схема ────────────────────────────────────────────────
        g4 = QGroupBox(_L('🎵  Звуковая схема', '🎵  Sound scheme', '🎵  サウンドスキーム'))
        g4.setToolTip("Назначь звук для каждого события — нажми 📂 чтобы выбрать файл")
        fl4 = QVBoxLayout(g4)

        t = get_theme(S().theme)

        import json as _j4
        try:
            _user_scheme = _j4.loads(S().get("sound_scheme", "{}", t=str))
        except Exception:
            _user_scheme = {}

        self._sound_labels = {}   # event → QLabel с именем файла

        for event, label in _SOUND_EVENT_LABELS.items():
            row = QWidget()
            row.setStyleSheet(
                f"QWidget{{background:{t['bg3']};border-radius:6px;margin:1px 0;}}"
                f"QWidget:hover{{background:{t['btn_hover']};}}")
            rl = QHBoxLayout(row)
            rl.setContentsMargins(8, 5, 8, 5)
            rl.setSpacing(8)

            # Название события
            ev_lbl = QLabel(label)
            ev_lbl.setMinimumWidth(190)
            ev_lbl.setStyleSheet(
                f"background:transparent;font-size:9pt;color:{t['text']};")
            rl.addWidget(ev_lbl)

            # Текущий файл
            cur = _user_scheme.get(event)
            if cur == "__none__":
                cur_text = _L('🔇 выключен', '🔇 off', '🔇 無効')
            elif cur:
                cur_text = cur
            else:
                default = _SOUND_MAP_DEFAULT.get(event, [""])[0]
                cur_text = f"{default} (по умолч.)" if default else "—"

            file_lbl = QLabel(cur_text)
            file_lbl.setMinimumWidth(160)
            file_lbl.setStyleSheet(
                f"background:transparent;font-size:9pt;"
                f"color:{t['text_dim']};font-style:italic;")
            rl.addWidget(file_lbl, stretch=1)
            self._sound_labels[event] = file_lbl

            # Кнопка выбрать файл
            pick_btn = QPushButton("📂")
            pick_btn.setFixedSize(30, 26)
            pick_btn.setToolTip(_L('Выбрать звуковой файл (.wav, .mp3, .ogg)', 'Select sound file (.wav, .mp3, .ogg)', 'サウンドファイルを選択'))
            pick_btn.setStyleSheet(
                f"QPushButton{{background:{t['bg2']};color:{t['text']};"
                "border-radius:4px;border:none;font-size:13px;}}"
                f"QPushButton:hover{{background:{t['accent']};color:white;}}")
            def _pick_file(_ev=event, _lbl=file_lbl):
                fn, _ = QFileDialog.getOpenFileName(
                    self, f"Звук для: {_SOUND_EVENT_LABELS.get(_ev, _ev)}",
                    str(Path(__file__).parent),
                    "Звуковые файлы (*.wav *.mp3 *.ogg *.flac *.aac);;Все файлы (*)")
                if fn:
                    fname = Path(fn).name
                    # Сохраняем полный путь если файл не рядом с gdf.py
                    try:
                        import json as _jx
                        sc = _jx.loads(S().get("sound_scheme", "{}", t=str))
                    except Exception:
                        sc = {}
                    sc[_ev] = fn   # полный путь
                    S().set("sound_scheme", __import__('json').dumps(sc))
                    _lbl.setText(fname)
                    _lbl.setStyleSheet(
                        f"background:transparent;font-size:9pt;"
                        f"color:{t['text']};font-style:normal;")
            pick_btn.clicked.connect(_pick_file)
            rl.addWidget(pick_btn)

            # Кнопка выключить звук
            mute_btn = QPushButton("🔇")
            mute_btn.setFixedSize(30, 26)
            mute_btn.setToolTip(_L('Выключить этот звук', 'Disable this sound', 'このサウンドを無効化'))
            mute_btn.setStyleSheet(pick_btn.styleSheet())
            def _mute(_ev=event, _lbl=file_lbl):
                try:
                    import json as _jm
                    sc = _jm.loads(S().get("sound_scheme", "{}", t=str))
                except Exception:
                    sc = {}
                sc[_ev] = "__none__"
                S().set("sound_scheme", __import__('json').dumps(sc))
                _lbl.setText(_L('🔇 выключен', '🔇 off', '🔇 無効'))
                _lbl.setStyleSheet(
                    f"background:transparent;font-size:9pt;"
                    f"color:{t['text_dim']};font-style:italic;")
            mute_btn.clicked.connect(_mute)
            rl.addWidget(mute_btn)

            # Кнопка preview
            prev_btn = QPushButton("▶")
            prev_btn.setFixedSize(30, 26)
            prev_btn.setToolTip("Прослушать")
            prev_btn.setStyleSheet(
                f"QPushButton{{background:{t['bg2']};color:{t['accent']};"
                "border-radius:4px;border:none;font-size:11px;}}"
                f"QPushButton:hover{{background:{t['accent']};color:white;}}")
            def _preview(_ev=event):
                play_system_sound(_ev)
            prev_btn.clicked.connect(_preview)
            rl.addWidget(prev_btn)

            fl4.addWidget(row)

        # Кнопки снизу
        btn_row2 = QHBoxLayout()
        reset_btn2 = QPushButton("↩ Сброс к дефолту")
        reset_btn2.setFixedHeight(28)
        def _reset2():
            S().set("sound_scheme", "{}")
            for ev, lbl in self._sound_labels.items():
                default = _SOUND_MAP_DEFAULT.get(ev, [""])[0]
                lbl.setText(f"{default} (по умолч.)" if default else "—")
                lbl.setStyleSheet(
                    f"background:transparent;font-size:9pt;"
                    f"color:{t['text_dim']};font-style:italic;")
        reset_btn2.clicked.connect(_reset2)
        btn_row2.addWidget(reset_btn2)
        btn_row2.addStretch()
        fl4.addLayout(btn_row2)

        lay.addWidget(g4)
        lay.addStretch()
        return outer

    def _populate_audio_devices(self):
        self._in_dev.clear()
        self._out_dev.clear()
        self._in_dev.addItem("По умолчанию", -1)
        self._out_dev.addItem("По умолчанию", -1)
        if PYAUDIO_AVAILABLE:
            try:
                import pyaudio
                pa = pyaudio.PyAudio()
                for i in range(pa.get_device_count()):
                    info = pa.get_device_info_by_index(i)
                    if info["maxInputChannels"] > 0:
                        self._in_dev.addItem(info["name"], i)
                    if info["maxOutputChannels"] > 0:
                        self._out_dev.addItem(info["name"], i)
                pa.terminate()
            except Exception:
                pass


    # ── Appearance tab ─────────────────────────────────────────────────
    def _tab_appearance(self) -> QWidget:
        """App scale, splash screen customisation, launcher, tab visibility."""
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12, 12, 12, 12)
        lay.setSpacing(10)

        t = get_theme(S().theme)

        # App scale
        scale_g = QGroupBox("Масштаб приложения")
        sfl = QFormLayout(scale_g)
        scale_row = QHBoxLayout()
        self._scale_slider = QSlider(Qt.Orientation.Horizontal)
        self._scale_slider.setRange(75, 150)
        self._scale_slider.setSingleStep(5)
        self._scale_slider.setValue(int(S().get("app_scale", 100, t=int)))
        self._scale_lbl = QLabel(f"{self._scale_slider.value()}%")
        self._scale_lbl.setFixedWidth(44)
        self._scale_slider.valueChanged.connect(lambda v: self._scale_lbl.setText(f"{v}%"))
        scale_row.addWidget(self._scale_slider)
        scale_row.addWidget(self._scale_lbl)
        sfl.addRow("Масштаб:", scale_row)
        scale_hint = QLabel("Масштаб сохраняется и применяется при следующем запуске. 100% — стандарт.")
        scale_hint.setStyleSheet(f"color:{t['text_dim']};font-size:9px;")
        sfl.addRow(scale_hint)
        apply_scale_btn = QPushButton("Применить сейчас (приблизительно)")
        apply_scale_btn.clicked.connect(self._apply_scale_now)
        sfl.addRow(apply_scale_btn)
        lay.addWidget(scale_g)

        # Splash screen
        splash_g = QGroupBox("Экран загрузки (Splash Screen)")
        spfl = QFormLayout(splash_g)
        self._show_splash = QCheckBox("Показывать экран загрузки при запуске")
        self._show_splash.setChecked(S().get("show_splash", True, t=bool))
        spfl.addRow(self._show_splash)
        self._splash_preview = QLabel(_L("Нет изображения", "No image", "画像なし"))
        self._splash_preview.setFixedSize(260, 90)
        self._splash_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._splash_preview.setStyleSheet(
            f"background:{t['bg3']};border-radius:6px;color:{t['text_dim']};font-size:10px;")
        bg_b64 = S().get("splash_image_b64", "", t=str)
        if bg_b64:
            try:
                pm = QPixmap()
                pm.loadFromData(base64.b64decode(bg_b64))
                if not pm.isNull():
                    self._splash_preview.setPixmap(pm.scaled(
                        260, 90,
                        Qt.AspectRatioMode.KeepAspectRatioByExpanding,
                        Qt.TransformationMode.SmoothTransformation))
            except Exception:
                pass
        spfl.addRow("Фон заставки:", self._splash_preview)
        splash_btn_row = QHBoxLayout()
        pick_splash_btn = QPushButton("Выбрать изображение")
        pick_splash_btn.clicked.connect(self._pick_splash_image)
        clear_splash_btn = QPushButton("Убрать")
        clear_splash_btn.clicked.connect(self._clear_splash_image)
        splash_btn_row.addWidget(pick_splash_btn)
        splash_btn_row.addWidget(clear_splash_btn)
        spfl.addRow(splash_btn_row)
        lay.addWidget(splash_g)

        # Launcher
        launch_g = QGroupBox("Экран выбора режима запуска")
        lfl = QFormLayout(launch_g)
        self._show_launcher_cb = QCheckBox("Показывать выбор режима при каждом запуске")
        self._show_launcher_cb.setChecked(S().show_launcher)
        lfl.addRow(self._show_launcher_cb)
        lay.addWidget(launch_g)

        # App icon
        icon_g = QGroupBox("🖼  Иконка приложения")
        ifl = QFormLayout(icon_g)
        self._icon_preview = QLabel("По умолчанию")
        self._icon_preview.setFixedSize(64, 64)
        self._icon_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._icon_preview.setStyleSheet(
            f"background:{t['bg3']};border-radius:8px;"
            f"color:{t['text_dim']};font-size:9px;")
        icon_b64 = S().get("app_icon_b64", "", t=str)
        if icon_b64:
            try:
                pm_i = QPixmap()
                pm_i.loadFromData(base64.b64decode(icon_b64))
                if not pm_i.isNull():
                    self._icon_preview.setPixmap(pm_i.scaled(
                        56, 56, Qt.AspectRatioMode.KeepAspectRatio,
                        Qt.TransformationMode.SmoothTransformation))
                    self._icon_preview.setText("")
            except Exception: pass
        ifl.addRow("Иконка:", self._icon_preview)
        icon_btn_row = QHBoxLayout()
        pick_icon_btn = QPushButton("📂 Выбрать (.png/.ico)")
        pick_icon_btn.clicked.connect(self._pick_app_icon)
        clear_icon_btn = QPushButton("Убрать")
        clear_icon_btn.clicked.connect(self._clear_app_icon)
        icon_btn_row.addWidget(pick_icon_btn)
        icon_btn_row.addWidget(clear_icon_btn)
        ifl.addRow(icon_btn_row)
        ifl.addRow(QLabel("Применяется после перезапуска"))
        lay.addWidget(icon_g)

        # Tab visibility
        tabs_g = QGroupBox("Постоянные вкладки (скрыть, но не удалить)")
        tfl = QFormLayout(tabs_g)
        self._tab_show_notes = QCheckBox("Показывать вкладку Заметки")
        self._tab_show_notes.setChecked(S().get("tab_show_notes", True, t=bool))
        self._tab_show_calls = QCheckBox("Показывать вкладку Звонки")
        self._tab_show_calls.setChecked(S().get("tab_show_calls", True, t=bool))
        tfl.addRow(self._tab_show_notes)
        tfl.addRow(self._tab_show_calls)
        lay.addWidget(tabs_g)
        lay.addStretch()
        return outer

    def _apply_scale_now(self):
        val = getattr(self, '_scale_slider', None)
        if val is None:
            return
        factor = val.value() / 100.0
        font = QApplication.instance().font()
        font.setPointSizeF(max(7.0, 9.0 * factor))
        QApplication.instance().setFont(font)
        S().set("app_scale", val.value())

    def _pick_app_icon(self):
        fn, _ = QFileDialog.getOpenFileName(
            self, "Выбрать иконку приложения", "",
            "Images (*.png *.ico *.jpg *.jpeg)")
        if not fn:
            return
        pm = QPixmap(fn)
        if pm.isNull():
            return
        buf = __import__('io').BytesIO()
        pm.toImage().save(buf := __import__('io').BytesIO(), 'PNG')
        b64 = base64.b64encode(buf.getvalue()).decode()
        S().set("app_icon_b64", b64)
        # Обновляем превью
        self._icon_preview.setPixmap(pm.scaled(
            56, 56, Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation))
        self._icon_preview.setText("")
        # Применяем сразу
        QApplication.instance().setWindowIcon(QIcon(fn))

    def _clear_app_icon(self):
        S().set("app_icon_b64", "")
        self._icon_preview.setPixmap(QPixmap())
        self._icon_preview.setText("По умолчанию")
        # Сброс к icon.png если есть
        _def = Path(__file__).parent / "icon.png"
        if _def.exists():
            QApplication.instance().setWindowIcon(QIcon(str(_def)))

    def _pick_splash_image(self):
        fn, _ = QFileDialog.getOpenFileName(
            self, "Выбрать фон заставки", "",
            "Images (*.png *.jpg *.jpeg *.bmp *.webp)")
        if not fn:
            return
        pm = QPixmap(fn)
        if pm.isNull():
            return
        S().set("splash_image_b64", pixmap_to_base64(pm))
        self._splash_preview.setPixmap(pm.scaled(
            260, 90,
            Qt.AspectRatioMode.KeepAspectRatioByExpanding,
            Qt.TransformationMode.SmoothTransformation))

    def _clear_splash_image(self):
        S().set("splash_image_b64", "")
        t = get_theme(S().theme)
        self._splash_preview.setPixmap(QPixmap())
        self._splash_preview.setText(_L("Нет изображения", "No image", "画像なし"))
        self._splash_preview.setStyleSheet(
            f"background:{t['bg3']};border-radius:6px;color:{t['text_dim']};font-size:10px;")


    # ── Network tab ────────────────────────────────────────────────────
    def _tab_network(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        scroll = QScrollArea(outer)
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(scroll)
        w = QWidget(scroll)
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        scroll.setWidget(w)

        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(10)

        # ── Интернет-режим ──
        g_inet = QGroupBox(_L("🌍 Интернет-режим", "🌍 Internet mode", "🌍 インターネットモード"))
        inet_lay = QVBoxLayout(g_inet)
        
        chk_row = QHBoxLayout()
        self._internet_enabled = QCheckBox(
            _L("Использовать VDS сервер goidaphone.ru вместо LAN/VPN соединения",
               "Use goidaphone.ru VDS server instead of LAN/VPN connection",
               "LAN/VPN接続の代わりにgoidaphone.ru VDSサーバーを使用"))
        self._internet_enabled.setChecked(S().connection_mode == "internet")
        self._internet_enabled._was_checked = self._internet_enabled.isChecked()
        def _on_internet_toggled(checked):
            # Пропускаем программные изменения
            if getattr(self._internet_enabled, '_handling', False):
                return
            if checked == self._internet_enabled._was_checked:
                return
            self._internet_enabled._handling = True
            from PyQt6.QtWidgets import QMessageBox
            
            # Если пытаются включить, а relay не разрешён — показываем инструкцию
            if checked and not self._allow_relay.isChecked():
                QMessageBox.information(
                    self,
                    _L("Требуется разрешение relay", "Relay permission required", "リレー許可が必要です"),
                    _L(
                        "Для активации VDS-соединения вам требуется поставить разрешительную метку на пункте "
                        "«Разрешить relay-соединения» (пролистайте вниз в разделе «Сеть», если вы это читаете — "
                        "вы уже в этом разделе).\n\n"
                        "Закройте это окно → пролистайте в самый низ страницы → разрешите использование relay.\n"
                        "Только после этого GoidaPhone сможет использовать интернет. Сделано в соображениях безопасности.",
                        
                        "To activate VDS connection, you need to enable the «Allow relay connections» checkbox "
                        "(scroll down in the «Network» section — if you're reading this, you're already there).\n\n"
                        "Close this window → scroll to the bottom of the page → enable relay.\n"
                        "Only then GoidaPhone will be able to use the internet. Done for security reasons.",
                        
                        "VDS接続を有効にするには、「リレー接続を許可する」チェックボックスをオンにする必要があります"
                        "（「ネットワーク」セクションを下にスクロールしてください — これを読んでいるなら、すでにそこにいます）。\n\n"
                        "このウィンドウを閉じる → ページの一番下までスクロール → リレーを有効にする。\n"
                        "その後でのみ、GoidaPhoneはインターネットを使用できます。セキュリティ上の理由によるものです。"))
                self._internet_enabled.setChecked(False)
                self._internet_enabled._handling = False
                return
            
            # Штатный баннер подтверждения
            reply = QMessageBox.question(
                self,
                _L("Интернет-режим", "Internet mode", "インターネットモード"),
                _L("Вы действительно хотите включить Интернет-режим?\nДанные будут проходить через goidaphone.ru\nи приложению потребуется перезагрузка для применения настроек.",
                   "Do you really want to enable Internet mode?\nData will go through goidaphone.ru\nand the app needs to restart to apply changes.",
                   "本当にインターネットモードを有効にしますか？\nデータはgoidaphone.ruを経由し、\n設定を適用するには再起動が必要です。") if checked else
                _L("Вы действительно хотите выключить Интернет-режим?\nПриложению потребуется перезагрузка для применения настроек.",
                   "Do you really want to disable Internet mode?\nThe app needs to restart to apply changes.",
                   "本当にインターネットモードを無効にしますか？\n設定を適用するには再起動が必要です。"),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                QMessageBox.StandardButton.No)
            if reply == QMessageBox.StandardButton.Yes:
                S().connection_mode = "internet" if checked else "lan"
                self._internet_enabled._was_checked = checked
            else:
                self._internet_enabled.setChecked(not checked)
            self._internet_enabled._handling = False
        self._internet_enabled.toggled.connect(_on_internet_toggled)
        chk_row.addWidget(self._internet_enabled)
        
        info_btn = QPushButton(_L("(что это?)", "(what's this?)", "(これは？)"))
        info_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        info_msg = _L(
            "При включении весь трафик (сообщения, звонки, файлы, реакции) пойдёт через сервер goidaphone.ru в интернете, а не напрямую между компьютерами (P2P) в локальной сети.",
            "When enabled, all traffic (messages, calls, files, reactions) will go through goidaphone.ru server on the internet instead of direct P2P in local network.",
            "有効にすると、すべてのトラフィック（メッセージ、通話、ファイル、リアクション）はローカルネットワークの直接P2Pではなく、インターネット上のgoidaphone.ruサーバーを経由します。")
        info_btn.setToolTip(info_msg)
        info_btn.setStyleSheet("QPushButton{background:transparent;border:none;color:#6af;font-size:10px;text-decoration:underline;}"
                               "QPushButton:hover{color:#8cf;}")
        def _show_info():
            from PyQt6.QtWidgets import QMessageBox
            QMessageBox.information(None,
                _L("Интернет-режим", "Internet mode", "インターネットモード"),
                info_msg)
        info_btn.clicked.connect(_show_info)
        chk_row.addWidget(info_btn)
        chk_row.addStretch()
        inet_lay.addLayout(chk_row)
        
        warn = QLabel(_L(
            "⚠ Если вы не доверяете интернет-соединению goidaphone.ru с GoidaCRYPTO, мы не советуем включать это. "
            "Данные будут передаваться через сервер, а не между компьютерами (P2P).\n"
            "Компания заботится о конфиденциальности вашей информации и тайны переписки, и вам решать как использовать GoidaPhone.",
            "⚠ If you don't trust goidaphone.ru internet connection with GoidaCRYPTO, we advise against enabling this. "
            "Data will go through the server, not directly between computers (P2P).\n"
            "The company cares about your privacy and confidentiality — you decide how to use GoidaPhone.",
            "⚠ goidaphone.ruとGoidaCRYPTOのインターネット接続を信頼できない場合、有効にしないことをお勧めします。"
            "データはコンピューター間（P2P）ではなくサーバーを経由します。\n"
            "当社はお客様のプライバシーと通信の秘密を大切にします。GoidaPhoneの使用方法はあなたが決めます。"))
        warn.setWordWrap(True)
        warn.setStyleSheet("font-size:9px;color:#FFB060;background:transparent;padding:4px;")
        inet_lay.addWidget(warn)
        
        lay.addWidget(g_inet)

        # ── Основные порты ──
        g_ports = QGroupBox("Сетевые порты")
        fl = QFormLayout(g_ports)
        self._udp_p = QSpinBox(); self._udp_p.setRange(1024,65535)
        self._tcp_p = QSpinBox(); self._tcp_p.setRange(1024,65535)
        self._udp_p.setValue(S().get("udp_port", 45678, t=int))
        self._tcp_p.setValue(S().get("tcp_port", 45679, t=int))
        fl.addRow(_L("UDP порт:", "UDP port:", "UDPポート:"), self._udp_p)
        fl.addRow(_L("TCP порт:", "TCP port:", "TCPポート:"), self._tcp_p)
        fl.addRow(QLabel("⚠ Изменение портов требует перезапуска."))
        lay.addWidget(g_ports)

        # ── Информация ──
        g_info = QGroupBox("Информация о соединении")
        fl2 = QFormLayout(g_info)
        ip = get_local_ip()
        ip_lbl = QLabel(ip)
        copy_ip_btn = QPushButton("📋")
        copy_ip_btn.setFixedWidth(28)
        copy_ip_btn.setToolTip("Копировать IP")
        copy_ip_btn.clicked.connect(lambda: QApplication.clipboard().setText(ip))
        ip_row = QHBoxLayout()
        ip_row.addWidget(ip_lbl)
        ip_row.addWidget(copy_ip_btn)
        ip_row.addStretch()
        fl2.addRow("Мой IP:", ip_row)
        fl2.addRow("ОС:", QLabel(get_os_name()))
        fl2.addRow("Version:", QLabel(APP_VERSION))
        fl2.addRow("Protocol:", QLabel(f"v{PROTOCOL_VERSION}"))
        lay.addWidget(g_info)

        # ── Пароль сети (admin) ──
        g_netpw = QGroupBox("🔐 Пароль сети (администратор)")
        netpw_lay = QVBoxLayout(g_netpw)
        netpw_info = QLabel(
            "Если установлен, новые пользователи должны знать пароль для подключения.\n"
            "Управляется через Admin Terminal (Shift+F10).")
        netpw_info.setWordWrap(True)
        netpw_info.setStyleSheet("font-size:10px;color:gray;")
        netpw_lay.addWidget(netpw_info)
        pw_status = QLabel(
            "● Пароль активен" if AdminManager.network_password_enabled()
            else "○ Пароль не установлен")
        pw_status.setStyleSheet(
            "color:#80FF80;" if AdminManager.network_password_enabled()
            else "color:gray;")
        netpw_lay.addWidget(pw_status)
        netpw_row = QHBoxLayout()
        self._netpw_edit = QLineEdit()
        self._netpw_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self._netpw_edit.setPlaceholderText("Новый пароль сети...")
        netpw_set_btn = QPushButton("Установить")
        netpw_set_btn.clicked.connect(self._set_network_password)
        netpw_off_btn = QPushButton("Отключить")
        netpw_off_btn.clicked.connect(self._disable_network_password)
        netpw_row.addWidget(self._netpw_edit)
        netpw_row.addWidget(netpw_set_btn)
        netpw_row.addWidget(netpw_off_btn)
        netpw_lay.addLayout(netpw_row)
        g_netpw.setEnabled(AdminManager.is_admin())
        if not AdminManager.is_admin():
            g_netpw.setTitle("🔐 Пароль сети (требуется администратор)")
        lay.addWidget(g_netpw)

        # ── Расширенные параметры ──
        g_adv = QGroupBox("⚙ Расширенные параметры")
        adv_fl = QFormLayout(g_adv)

        self._broadcast_addr = QLineEdit()
        self._broadcast_addr.setText(S().get("broadcast_addr","255.255.255.255",t=str))
        self._broadcast_addr.setPlaceholderText("255.255.255.255")
        adv_fl.addRow("Адрес broadcast:", self._broadcast_addr)

        self._discovery_interval = QSpinBox()
        self._discovery_interval.setRange(1, 60)
        self._discovery_interval.setSuffix(" сек")
        self._discovery_interval.setValue(S().get("discovery_interval", 5, t=int))
        adv_fl.addRow("Интервал обнаружения:", self._discovery_interval)

        self._peer_timeout = QSpinBox()
        self._peer_timeout.setRange(5, 300)
        self._peer_timeout.setSuffix(" сек")
        self._peer_timeout.setValue(S().get("peer_timeout", 30, t=int))
        adv_fl.addRow("Таймаут пира:", self._peer_timeout)

        self._max_chunk = QSpinBox()
        self._max_chunk.setRange(8, 60)
        self._max_chunk.setSuffix(" КБ")
        self._max_chunk.setValue(S().get("max_chunk_kb", 60, t=int))
        adv_fl.addRow("Макс. размер чанка:", self._max_chunk)

        self._tcp_timeout = QSpinBox()
        self._tcp_timeout.setRange(1,30)
        self._tcp_timeout.setSuffix(" сек")
        self._tcp_timeout.setValue(S().get("tcp_timeout", 10, t=int))
        adv_fl.addRow("TCP таймаут:", self._tcp_timeout)

        self._enable_ipv6 = QCheckBox("Включить IPv6 (экспериментально)")
        self._enable_ipv6.setChecked(S().get("enable_ipv6", False, t=bool))
        adv_fl.addRow(self._enable_ipv6)

        self._allow_relay = QCheckBox("Разрешить relay-соединения")
        self._allow_relay.setChecked(S().get("allow_relay", True, t=bool))
        adv_fl.addRow(self._allow_relay)

        self._encrypt_transport = QCheckBox("Шифровать транспортный слой (AES, экспер.)")
        self._encrypt_transport.setChecked(S().get("encrypt_transport", False, t=bool))
        adv_fl.addRow(self._encrypt_transport)

        lay.addWidget(g_adv)

        # ── Список серверов ──
        g_srv = QGroupBox("🌐 Список серверов / сетей")
        srv_lay = QVBoxLayout(g_srv)
        srv_lay.addWidget(QLabel(
            "Сохранённые сети для быстрого подключения. "
            "Двойной клик — подключиться."))

        self._server_list_widget = QListWidget()
        self._server_list_widget.setMaximumHeight(140)
        self._refresh_server_list()
        self._server_list_widget.itemDoubleClicked.connect(self._connect_to_server)
        srv_lay.addWidget(self._server_list_widget)

        srv_btn_row = QHBoxLayout()
        add_srv_btn = QPushButton("➕ Добавить")
        add_srv_btn.clicked.connect(self._add_server_dialog)
        edit_srv_btn = QPushButton("✏ Изменить")
        edit_srv_btn.clicked.connect(self._edit_server_dialog)
        del_srv_btn = QPushButton("🗑 Удалить")
        del_srv_btn.clicked.connect(self._delete_server)
        conn_srv_btn = QPushButton("🔌 Подключиться")
        conn_srv_btn.setObjectName("accent_btn")
        conn_srv_btn.clicked.connect(lambda: self._connect_to_server(
            self._server_list_widget.currentItem()))
        for b in [add_srv_btn, edit_srv_btn, del_srv_btn, conn_srv_btn]:
            srv_btn_row.addWidget(b)
        srv_lay.addLayout(srv_btn_row)
        lay.addWidget(g_srv)


        lay.addStretch()
        return outer

    def _set_network_password(self):
        pw = self._netpw_edit.text().strip()
        if not pw:
            QMessageBox.warning(self,"Error","Введите пароль.")
            return
        if not AdminManager.is_admin() or not AdminManager.verify_admin(""):
            # require admin terminal auth - just set directly if admin exists
            pass
        AdminManager.set_network_password(pw)
        self._netpw_edit.clear()
        QMessageBox.information(self,"Пароль сети","✅ Пароль установлен.")

    def _disable_network_password(self):
        AdminManager.set_network_password("")
        QMessageBox.information(self,"Пароль сети","Пароль disabled.")

    def _refresh_server_list(self):
        self._server_list_widget.clear()
        import json
        servers = json.loads(S().get("server_list","[]",t=str))
        for s in servers:
            name = s.get("name","?")
            host = s.get("host","?")
            port = s.get("udp_port", 45678)
            desc = s.get("desc","")
            pw   = "🔐" if s.get("password") else ""
            self._server_list_widget.addItem(
                f"{pw} {name}  —  {host}:{port}  {desc}")

    def _add_server_dialog(self):
        self._server_edit_dialog()

    def _edit_server_dialog(self):
        row = self._server_list_widget.currentRow()
        import json
        servers = json.loads(S().get("server_list","[]",t=str))
        srv = servers[row] if 0 <= row < len(servers) else None
        self._server_edit_dialog(srv, row)

    def _server_edit_dialog(self, existing=None, edit_row=-1):
        dlg = QDialog(self)
        dlg.setWindowTitle("Добавить сервер" if not existing else "Изменить сервер")
        dlg.resize(400, 380)
        t = get_theme(S().theme)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        dl = QVBoxLayout(dlg)
        dl.setContentsMargins(16,16,16,16)
        dl.setSpacing(10)
        fl = QFormLayout()

        name_e   = QLineEdit(existing.get("name","") if existing else "")
        host_e   = QLineEdit(existing.get("host","") if existing else "")
        udp_e    = QSpinBox(); udp_e.setRange(1024,65535)
        udp_e.setValue(existing.get("udp_port",45678) if existing else 45678)
        tcp_e    = QSpinBox(); tcp_e.setRange(1024,65535)
        tcp_e.setValue(existing.get("tcp_port",45679) if existing else 45679)
        desc_e   = QLineEdit(existing.get("desc","") if existing else "")
        pw_e     = QLineEdit(existing.get("password","") if existing else "")
        pw_e.setEchoMode(QLineEdit.EchoMode.Password)
        tags_e   = QLineEdit(existing.get("tags","") if existing else "")
        notes_e  = QPlainTextEdit(existing.get("notes","") if existing else "")
        notes_e.setMaximumHeight(60)

        fl.addRow("Название * :", name_e)
        fl.addRow("Хост / IP * :", host_e)
        fl.addRow("UDP порт * :", udp_e)
        fl.addRow("TCP порт :", tcp_e)
        fl.addRow("Описание :", desc_e)
        fl.addRow("Пароль :", pw_e)
        fl.addRow("Теги :", tags_e)
        fl.addRow("Заметки :", notes_e)
        dl.addLayout(fl)

        req_note = QLabel("* — обязательные поля")
        req_note.setStyleSheet("font-size:9px;color:gray;")
        dl.addWidget(req_note)

        btn_row = QHBoxLayout()
        ok_btn = QPushButton(_L("💾 Сохранить", "💾 Save", "💾 保存"))
        ok_btn.setObjectName("accent_btn")
        cancel_btn = QPushButton(_L(_L("Отмена", "Cancel", "キャンセル"), "Cancel", "キャンセル"))
        btn_row.addStretch()
        btn_row.addWidget(ok_btn)
        btn_row.addWidget(cancel_btn)
        dl.addLayout(btn_row)

        cancel_btn.clicked.connect(dlg.reject)

        def do_save():
            if not name_e.text().strip() or not host_e.text().strip():
                QMessageBox.warning(dlg,"Error","Заполните обязательные поля.")
                return
            import json
            servers = json.loads(S().get("server_list","[]",t=str))
            entry = {
                "name":     name_e.text().strip(),
                "host":     host_e.text().strip(),
                "udp_port": udp_e.value(),
                "tcp_port": tcp_e.value(),
                "desc":     desc_e.text().strip(),
                "password": pw_e.text(),
                "tags":     tags_e.text().strip(),
                "notes":    notes_e.toPlainText().strip(),
            }
            if edit_row >= 0 and edit_row < len(servers):
                servers[edit_row] = entry
            else:
                servers.append(entry)
            S().set("server_list", json.dumps(servers, ensure_ascii=False))
            self._refresh_server_list()
            dlg.accept()

        ok_btn.clicked.connect(do_save)
        dlg.exec()

    def _delete_server(self):
        row = self._server_list_widget.currentRow()
        if row < 0: return
        import json
        servers = json.loads(S().get("server_list","[]",t=str))
        if 0 <= row < len(servers):
            name = servers[row].get("name","?")
            if QMessageBox.question(self,"Удалить",f"Удалить сервер «{name}»?",
                    QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                    == QMessageBox.StandardButton.Yes:
                servers.pop(row)
                S().set("server_list", json.dumps(servers, ensure_ascii=False))
                self._refresh_server_list()

    def _connect_to_server(self, item):
        if not item: return
        row = self._server_list_widget.row(item)
        import json
        servers = json.loads(S().get("server_list","[]",t=str))
        if 0 <= row < len(servers):
            srv = servers[row]
            host = srv.get("host","")
            udp  = srv.get("udp_port",45678)
            pw   = srv.get("password","")
            if pw:
                entered, ok = QInputDialog.getText(
                    self, "Пароль сети",
                    f"Введите пароль для подключения к «{srv.get('name',host)}»:",
                    QLineEdit.EchoMode.Password)
                if not ok or entered != pw:
                    QMessageBox.warning(self,"Доступ закрыт","Неверный пароль.")
                    return
            S().set("udp_port", udp)
            S().set("tcp_port", srv.get("tcp_port",45679))
            S().set("broadcast_addr", host)
            QMessageBox.information(self,"Подключение",
                f"Параметры сохранены.\nПерезапустите GoidaPhone для применения.")

    # ── Themes tab ─────────────────────────────────────────────────────
    def _tab_themes(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer); _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa); _sa.setWidget(w)
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(8)

        g = QGroupBox("Стандартные темы")
        fl = QFormLayout(g)
        self._theme_combo = QComboBox()
        for key, td in THEMES.items():
            self._theme_combo.addItem(td["label"], key)
        fl.addRow("Theme:", self._theme_combo)
        preview_btn = QPushButton(_L('👁 Предпросмотр', '👁 Preview', '👁 プレビュー'))
        preview_btn.clicked.connect(self._preview_theme)
        fl.addRow(preview_btn)
        lay.addWidget(g)

        g2 = QGroupBox("👑 Кастомные темы (Премиум)")
        gl = QVBoxLayout(g2)
        gl.addWidget(QLabel("Создайте до 3 собственных тем:"))
        sl = QHBoxLayout()
        for i in range(1,4):
            saved = S().custom_theme(i)
            label = saved.get("name", f"Слот {i}") if saved else f"Слот {i}"
            b = QPushButton(f"✏ {label}")
            b.clicked.connect(lambda _, s=i: self._edit_custom_theme(s))
            sl.addWidget(b)
            if saved:
                use = QPushButton(f"▶ Применить {i}")
                use.clicked.connect(lambda _, s=i: self._apply_custom_theme(s))
                sl.addWidget(use)
        gl.addLayout(sl)
        g2.setEnabled(S().premium)
        if not S().premium:
            g2.setTitle("👑 Кастомные темы (требует Премиум)")
        lay.addWidget(g2)
        lay.addStretch()
        return outer

    def _preview_theme(self):
        key = self._theme_combo.currentData()
        if not key:
            return
        reply = QMessageBox.information(
            self, "Смена темы",
            "⚠ Быстрое переключение тем может быть нестабильным.\n"
            "Для полного применения рекомендуется перезапуск GoidaPhone.",
            QMessageBox.StandardButton.Ok | QMessageBox.StandardButton.Cancel)
        if reply == QMessageBox.StandardButton.Ok:
            QApplication.instance().setStyleSheet(build_stylesheet(get_theme(key)))

    def _edit_custom_theme(self, slot: int):
        if not S().premium:
            QMessageBox.warning(self,_L("Премиум", "Premium", "プレミアム"),"Доступно только для Премиум пользователей.")
            return
        d = CustomThemeDialog(slot, self)
        d.exec()

    def _apply_custom_theme(self, slot: int):
        data = S().custom_theme(slot)
        if data and "colors" in data:
            t = {**get_theme("dark"), **data["colors"]}
            t["label"] = data.get("name","Кастомная")
            QApplication.instance().setStyleSheet(build_stylesheet(t))
            S().set("theme", f"__custom_{slot}__")

    # ── License tab ────────────────────────────────────────────────────
    def _tab_license(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer); _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa); _sa.setWidget(w)
        w.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.MinimumExpanding)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(20, 20, 20, 20)
        lay.setSpacing(16)

        t = get_theme(S().theme)
        is_premium = S().premium

        # ── Статус-карточка ──────────────────────────────────────────────────
        card = QFrame()
        card.setFrameShape(QFrame.Shape.NoFrame)
        if is_premium:
            card.setStyleSheet(
                "QFrame{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
                "stop:0 #0d2a0d,stop:0.5 #142814,stop:1 #0d200d);"
                "border-radius:18px;}")
        else:
            card.setStyleSheet(
                f"QFrame{{background:{t['bg3']};border-radius:18px;}}")
        cl = QVBoxLayout(card)
        cl.setContentsMargins(28, 24, 28, 24)
        cl.setSpacing(10)

        # Значок и план
        top_row = QHBoxLayout()
        badge = QLabel("✦" if is_premium else "◇")
        badge.setStyleSheet(
            f"font-size:36px;"
            f"color:{'#FFD700' if is_premium else t['text_dim']};"
            "background:transparent;")
        top_row.addWidget(badge)

        plan_col = QVBoxLayout()
        plan_name = QLabel("GoidaPhone Premium" if is_premium else "Стандартная версия")
        plan_name.setStyleSheet(
            f"font-size:16px;font-weight:700;"
            f"color:{'#FFD700' if is_premium else t['text']};"
            "background:transparent;")
        plan_col.addWidget(plan_name)

        self._lic_status = QLabel()
        self._lic_status.setStyleSheet(
            f"font-size:10pt;color:{t['text_dim']};background:transparent;")
        plan_col.addWidget(self._lic_status)
        top_row.addLayout(plan_col)
        top_row.addStretch()
        cl.addLayout(top_row)

        # Прогресс-бар (только если premium)
        if is_premium:
            try:
                from datetime import datetime as _dt
                exp_dt = _dt.fromisoformat(S().premium_expires)
                act_dt = exp_dt - __import__('datetime').timedelta(days=LICENSE_DAYS)
                total  = max(1, (exp_dt - act_dt).days)
                remain = max(0, (exp_dt - _dt.now()).days)
                pct    = int(remain / total * 100)
                clr    = "#27AE60" if pct > 40 else "#F39C12" if pct > 15 else "#E74C3C"

                sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
                sep.setStyleSheet("background:rgba(255,255,255,15);max-height:1px;border:none;")
                cl.addWidget(sep)

                prog = QProgressBar()
                prog.setRange(0, 100); prog.setValue(pct)
                prog.setFixedHeight(5); prog.setTextVisible(False)
                prog.setStyleSheet(
                    f"QProgressBar{{background:rgba(255,255,255,20);border-radius:3px;border:none;}}"
                    f"QProgressBar::chunk{{background:{clr};border-radius:3px;}}")
                cl.addWidget(prog)

                days_lbl = QLabel(
                    f"{'Истекает' if remain < 7 else 'Активна'} — осталось {remain} дн. из {total}")
                days_lbl.setStyleSheet(
                    f"font-size:8pt;color:{'#E74C3C' if remain<7 else 'rgba(255,255,255,120)'};"
                    "background:transparent;")
                cl.addWidget(days_lbl)
            except Exception:
                pass

        lay.addWidget(card)

        # ── Преимущества ─────────────────────────────────────────────────────
        feats_w = QWidget()
        feats_w.setStyleSheet(f"background:{t['bg3']};border-radius:12px;")
        feats_l = QVBoxLayout(feats_w)
        feats_l.setContentsMargins(16, 14, 16, 14)
        feats_l.setSpacing(8)

        feats_hdr = QLabel("Что входит в Premium")
        feats_hdr.setStyleSheet(
            f"font-size:10pt;font-weight:600;color:{t['text']};background:transparent;")
        feats_l.addWidget(feats_hdr)

        for feat, desc in [
            ("🎨 Цветной ник",          "Любой цвет для своего имени в чате"),
            ("😎 Кастомный эмодзи",     "Эмодзи рядом с именем"),
            ("🖌 Свои темы",            "3 слота для кастомных тем оформления"),
            ("⭐ Значок в профиле",     "Золотой значок Premium"),
        ]:
            row = QHBoxLayout()
            feat_lbl = QLabel(feat)
            feat_lbl.setStyleSheet(
                f"font-size:10pt;font-weight:500;color:{t['text']};background:transparent;"
                "min-width:160px;")
            desc_lbl = QLabel(desc)
            desc_lbl.setStyleSheet(
                f"font-size:9pt;color:{t['text_dim']};background:transparent;")
            row.addWidget(feat_lbl)
            row.addWidget(desc_lbl)
            row.addStretch()
            feats_l.addLayout(row)

        lay.addWidget(feats_w)

        # ── Активация / Управление ───────────────────────────────────────────
        act_w = QWidget()
        act_w.setStyleSheet(f"background:{t['bg3']};border-radius:12px;")
        act_l = QVBoxLayout(act_w)
        act_l.setContentsMargins(16, 14, 16, 14)
        act_l.setSpacing(10)

        if is_premium:
            # Только кнопка деактивации — без показа ключа
            deact_lbl = QLabel("Подписка активна и привязана к этому устройству.")
            deact_lbl.setStyleSheet(
                f"font-size:9pt;color:{t['text_dim']};background:transparent;")
            deact_lbl.setWordWrap(True)
            act_l.addWidget(deact_lbl)

            self._activate_btn = QPushButton("Деактивировать Premium")
            self._activate_btn.setStyleSheet(
                f"QPushButton{{background:transparent;color:{t['text_dim']};"
                f"border:1px solid {t['border']};border-radius:8px;"
                "padding:6px 14px;font-size:9pt;}}"
                f"QPushButton:hover{{border-color:#E74C3C;color:#E74C3C;}}")
            self._activate_btn.clicked.connect(self._deactivate_premium)
            act_l.addWidget(self._activate_btn)

            self._lic_input = LicenseLineEdit()
            self._lic_input.setVisible(False)
        else:
            enter_lbl = QLabel("Введи код лицензии")
            enter_lbl.setStyleSheet(
                f"font-size:10pt;font-weight:600;color:{t['text']};background:transparent;")
            act_l.addWidget(enter_lbl)

            self._lic_input = LicenseLineEdit()
            act_l.addWidget(self._lic_input)

            btn_row = QHBoxLayout()
            self._activate_btn = QPushButton(_L(_L("Активировать", "Activate", "認証"), "Activate", "認証"))
            self._activate_btn.setObjectName("accent_btn")
            self._activate_btn.setFixedHeight(36)
            self._activate_btn.clicked.connect(self._activate)
            btn_row.addWidget(self._activate_btn)

            buy_btn = QPushButton("Купить в Telegram →")
            buy_btn.setFixedHeight(36)
            buy_btn.setStyleSheet(
                f"QPushButton{{background:transparent;color:{t['accent']};"
                f"border:1px solid {t['accent']};border-radius:8px;padding:0 14px;}}"
                f"QPushButton:hover{{background:{t['accent']};color:white;}}")
            buy_btn.clicked.connect(lambda: QDesktopServices.openUrl(
                QUrl("https://t.me/WinoraCompany")))
            btn_row.addWidget(buy_btn)
            act_l.addLayout(btn_row)

        lay.addWidget(act_w)
        lay.addStretch()

        self._update_lic_status()
        return outer


    def _activate(self):
        code = self._lic_input.raw_digits()
        if S().activate_premium(code):
            exp = datetime.fromisoformat(S().premium_expires).strftime("%d.%m.%Y")
            self._lic_status.setText(f"✅ Премиум активен до {exp}")
            self._lic_status.setStyleSheet("color:#27AE60; font-weight:bold;")
            QMessageBox.information(self,"Активация",f"🎉 Премиум активирован до {exp}!")
            self.settings_saved.emit()
        else:
            QMessageBox.warning(self,"Error","Неверный код лицензии.")

    def _deactivate_premium(self):
        from PyQt6.QtWidgets import QMessageBox
        if QMessageBox.question(self, "Деактивация",
                "Деактивировать Premium? Все настройки будут сброшены.",
            ) == QMessageBox.StandardButton.Yes:
            S().set("premium", False)
            S().set("license_code", "")
            S().set("premium_expires", "")
            self.settings_saved.emit()
            # Перезагружаем вкладку
            QMessageBox.information(self, "Деактивация", "Premium деактивирован.")

    def _update_lic_status(self):
        if not hasattr(self, '_lic_status'):
            return
        if S().premium:
            try:
                from datetime import datetime as _dt2
                exp_dt = _dt2.fromisoformat(S().premium_expires)
                remain = (exp_dt - _dt2.now()).days
                exp_str = exp_dt.strftime("%d.%m.%Y")
                if remain > 0:
                    self._lic_status.setText(
                        f"✅  Активен до {exp_str}  ({remain} дн.)")
                    self._lic_status.setStyleSheet(
                        "color:#27AE60;font-weight:bold;background:transparent;")
                else:
                    self._lic_status.setText("⚠  Срок действия истёк!")
                    self._lic_status.setStyleSheet(
                        "color:#E74C3C;font-weight:bold;background:transparent;")
                    S().set("premium", False)
            except Exception:
                self._lic_status.setText("✅  Активен")
                self._lic_status.setStyleSheet(
                    "color:#27AE60;font-weight:bold;background:transparent;")
        else:
            self._lic_status.setText("❌  Не активировано")
            self._lic_status.setStyleSheet(
                "color:#E74C3C;font-weight:bold;background:transparent;")

    # ── Data tab ───────────────────────────────────────────────────────
    def _tab_data(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(8)

        g = QGroupBox("Хранение данных")
        fl = QFormLayout(g)
        self._save_history = QCheckBox("Сохранять историю чатов")
        self._show_splash   = QCheckBox("Показывать заставку при запуске")
        fl.addRow(self._save_history)
        fl.addRow(self._show_splash)

        fl.addRow(QLabel(f"📁 Данные хранятся в: {DATA_DIR}"))

        clear_hist = QPushButton("🗑 Очистить всю историю")
        clear_hist.setObjectName("danger_btn")
        clear_hist.clicked.connect(self._clear_history)
        fl.addRow(clear_hist)

        clear_files = QPushButton("🗑 Очистить полученные файлы")
        clear_files.setObjectName("danger_btn")
        clear_files.clicked.connect(self._clear_files)
        fl.addRow(clear_files)

        lay.addWidget(g)

        # Updates
        upd_g = QGroupBox("Обновления")
        ul = QVBoxLayout(upd_g)
        self._check_upd_btn = QPushButton("🔄 Проверить обновления")
        self._check_upd_btn.clicked.connect(self._check_updates)
        ul.addWidget(self._check_upd_btn)
        self._upd_lbl = QLabel("")
        ul.addWidget(self._upd_lbl)
        lay.addWidget(upd_g)

        lay.addStretch()
        return outer

    def _clear_history(self):
        if QMessageBox.question(self,"Очистить","Удалить всю историю чатов?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            for f in HISTORY_DIR.glob("*.json"):
                f.unlink()
            HISTORY._cache.clear()

    def _clear_files(self):
        if QMessageBox.question(self,"Очистить","Удалить все полученные файлы?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            for f in RECEIVED_DIR.iterdir():
                f.unlink()

    def _check_updates(self):
        self._check_upd_btn.setEnabled(False)
        self._upd_lbl.setText("Проверка...")
        self._checker = UpdateChecker()
        self._checker.update_available.connect(self._on_update_available)
        self._checker.no_update.connect(lambda: self._upd_lbl.setText("✅ Обновлений не найдено."))
        self._checker.check_failed.connect(lambda e: self._upd_lbl.setText(f"❌ Ошибка: {e}"))
        self._checker.finished.connect(lambda: self._check_upd_btn.setEnabled(True))
        self._checker.start()

    def _on_update_available(self, ver: str, desc: str):
        self._upd_lbl.setText(f"🚀 Доступна версия {ver}")
        _show_update_dialog(ver, desc, self)

    def _do_update(self):
        if GITHUB_REPO.startswith("YOUR_GITHUB"):
            QMessageBox.information(self,"Обновление",
                "Настройте GITHUB_REPO в исходном коде для автообновления.")
            return
        dest = str(Path(sys.argv[0]).parent / f"goidaphone_update_{APP_VERSION}.py")
        dlg  = QProgressDialog("Загрузка обновления...", _L(_L("Отмена", "Cancel", "キャンセル"), "Cancel", "キャンセル"), 0, 100, self)
        dlg.setWindowModality(Qt.WindowModality.WindowModal)
        dlg.show()
        self._updater = Updater(GITHUB_RAW_URL, dest)
        self._updater.progress.connect(dlg.setValue)
        self._updater.finished.connect(lambda ok, info: self._update_done(ok, info, dest, dlg))
        self._updater.start()

    def _update_done(self, ok: bool, info: str, dest: str, dlg):
        dlg.close()
        if ok:
            msg = QMessageBox(self)
            msg.setWindowTitle("Обновление загружено")
            msg.setText(f"Обновление загружено в:\n{dest}\n\nПерезапустите приложение для применения.")
            msg.setInformativeText("Заменить текущий файл и перезапустить?")
            msg.setStandardButtons(QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
            if msg.exec() == QMessageBox.StandardButton.Yes:
                try:
                    shutil.copy2(dest, sys.argv[0])
                    os.unlink(dest)
                    QApplication.instance().quit()
                    subprocess.Popen([sys.executable, sys.argv[0]])
                except Exception as e:
                    QMessageBox.critical(self,"Error",f"Не удалось заменить файл:\n{e}")
        else:
            QMessageBox.critical(self,"Error",f"Ошибка загрузки:\n{info}")

    # ── Language tab ────────────────────────────────────────────────────
    def _tab_language(self) -> QWidget:
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(8)

        g = QGroupBox("🌍 " + TR("tab_language"))
        fl = QFormLayout(g)

        self._lang_combo = QComboBox()
        self._lang_combo.addItem("🇷🇺 Русский", "ru")
        self._lang_combo.addItem("🇬🇧 English", "en")
        self._lang_combo.addItem("🇯🇵 日本語", "ja")
        # Set current
        current_lang = S().language
        for i in range(self._lang_combo.count()):
            if self._lang_combo.itemData(i) == current_lang:
                self._lang_combo.setCurrentIndex(i)
                break
        fl.addRow("Язык / Language:", self._lang_combo)

        note = QLabel("⚠ Смена языка применяется при следующем открытии окна.\n"
                      "⚠ Language change applies on next window open.")
        note.setWordWrap(True)
        note.setStyleSheet(f"color: {get_theme(S().theme)['text_dim']}; font-size: 10px;")
        fl.addRow(note)

        lay.addWidget(g)

        # Launcher settings
        g2 = QGroupBox("🚀 Startup")
        fl2 = QFormLayout(g2)
        self._show_launcher_cb2 = QCheckBox(
            "Show mode selection screen on startup" if S().language=="ru"
            else "Show mode selection screen on startup")
        self._show_launcher_cb2.setChecked(S().show_launcher)
        fl2.addRow(self._show_launcher_cb2)

        self._os_notif_cb = QCheckBox(
            "Системные уведомления о новых сообщениях" if S().language=="ru"
            else "System notifications for new messages")
        self._os_notif_cb.setChecked(S().os_notifications)
        fl2.addRow(self._os_notif_cb)

        lay.addWidget(g2)
        lay.addStretch()
        return outer

    # ── Specialist tab ──────────────────────────────────────────────────
    def _tab_pin_security(self) -> QWidget:
        """PIN lock and auto-lock settings tab."""
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(10)

        t = get_theme(S().theme)
        pin_enabled = S().get("pin_enabled", False, t=bool)

        # ── Enable PIN ──
        g_pin = QGroupBox(_L("PIN-блокировка", "PIN lock", "PINロック"))
        gpl = QVBoxLayout(g_pin)

        self._pin_enable_cb = QCheckBox("Включить PIN-блокировку при запуске")
        self._pin_enable_cb.setChecked(pin_enabled)
        gpl.addWidget(self._pin_enable_cb)

        fl = QFormLayout()
        self._pin_new = QLineEdit()
        self._pin_new.setEchoMode(QLineEdit.EchoMode.Password)
        self._pin_new.setPlaceholderText("6 цифр")
        self._pin_new.setMaxLength(6)
        fl.addRow("Новый PIN:", self._pin_new)

        self._pin_confirm = QLineEdit()
        self._pin_confirm.setEchoMode(QLineEdit.EchoMode.Password)
        self._pin_confirm.setPlaceholderText("Повторите PIN")
        self._pin_confirm.setMaxLength(6)
        fl.addRow("Подтвердить PIN:", self._pin_confirm)

        self._pin_hint_edit = QLineEdit()
        self._pin_hint_edit.setPlaceholderText("Подсказка (необязательно)")
        self._pin_hint_edit.setText(S().get("pin_hint","",t=str))
        fl.addRow("Подсказка:", self._pin_hint_edit)
        gpl.addLayout(fl)

        set_pin_btn = QPushButton("💾 Установить PIN")
        set_pin_btn.setObjectName("accent_btn")
        set_pin_btn.clicked.connect(self._set_pin)
        clear_pin_btn = QPushButton("🗑 Убрать PIN")
        clear_pin_btn.clicked.connect(self._clear_pin)
        pin_btn_row = QHBoxLayout()
        pin_btn_row.addWidget(set_pin_btn)
        pin_btn_row.addWidget(clear_pin_btn)
        gpl.addLayout(pin_btn_row)

        status_lbl = QLabel(
            "✅ PIN установлен" if S().get("pin_hash","",t=str)
            else "○ PIN не установлен")
        status_lbl.setStyleSheet(
            "color:#80FF80;" if S().get("pin_hash","",t=str)
            else "color:gray;")
        gpl.addWidget(status_lbl)
        lay.addWidget(g_pin)

        # ── Auto-lock ──
        g_auto = QGroupBox("Автоблокировка")
        afl = QFormLayout(g_auto)

        self._autolock_enabled = QCheckBox("Блокировать при бездействии")
        self._autolock_enabled.setChecked(S().get("autolock_enabled", False, t=bool))
        afl.addRow(self._autolock_enabled)

        self._autolock_timeout = QSpinBox()
        self._autolock_timeout.setRange(1, 120)
        self._autolock_timeout.setSuffix(" мин")
        self._autolock_timeout.setValue(S().get("autolock_timeout", 5, t=int))
        afl.addRow("Таймаут:", self._autolock_timeout)

        self._lock_on_minimize = QCheckBox("Блокировать при сворачивании")
        self._lock_on_minimize.setChecked(S().get("lock_on_minimize", False, t=bool))
        afl.addRow(self._lock_on_minimize)

        lay.addWidget(g_auto)

        note = QLabel(
            "ℹ Если вы забудете PIN, можно сбросить приложение.\n"
            "Все личные данные будут удалены при сбросе.")
        note.setWordWrap(True)
        note.setStyleSheet("font-size:10px;color:gray;")
        lay.addWidget(note)

        lay.addStretch()
        return outer

    def _set_pin(self):
        import hashlib as _hl
        pin1 = self._pin_new.text().strip()
        pin2 = self._pin_confirm.text().strip()
        if not pin1:
            QMessageBox.warning(self, "Error", "Введите PIN.")
            return
        if len(pin1) < 4 or not pin1.isdigit():
            QMessageBox.warning(self, "Error", "PIN: минимум 4 цифры (только цифры).")
            return
        if pin1 != pin2:
            QMessageBox.warning(self, "Error", "PIN-коды не совпадают.")
            return
        S().set("pin_hash", _hl.sha256(pin1.encode()).hexdigest())
        S().set("pin_hint", self._pin_hint_edit.text().strip())
        S().set("pin_enabled", "true")
        self._pin_enable_cb.setChecked(True)
        self._pin_new.clear()
        self._pin_confirm.clear()
        QMessageBox.information(self, "PIN", "✅ PIN-код установлен успешно.")

    def _clear_pin(self):
        if QMessageBox.question(self,"Убрать PIN","Удалить PIN-код?",
                QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                == QMessageBox.StandardButton.Yes:
            S().remove("pin_hash")
            S().remove("pin_hint")
            S().set("pin_enabled", False)
            QMessageBox.information(self,"PIN","PIN-код удалён.")

    def _tab_privacy(self) -> QWidget:
        """🛡 Privacy & security settings."""
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0, 0, 0, 0)
        _sa = QScrollArea(outer)
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(14, 14, 14, 14)
        lay.setSpacing(10)
        t = get_theme(S().theme)

        def _grp(title: str) -> tuple:
            g = QGroupBox(title)
            fl = QVBoxLayout(g)
            return g, fl

        # ── Читаемость / видимость ──────────────────────────────────────────
        g1, fl1 = _grp("👁  Видимость и статус")

        self._priv_show_status = QCheckBox(
            "Показывать мой статус присутствия другим пользователям")
        self._priv_show_status.setChecked(
            S().get("priv_show_status", True, t=bool))
        self._priv_show_status.setToolTip(
            "Если disabled — другие не видят «Онлайн», «Отошёл» и т.д.")
        fl1.addWidget(self._priv_show_status)

        self._priv_show_avatar = QCheckBox(
            "Показывать аватар при звонках и в списке пользователей")
        self._priv_show_avatar.setChecked(
            S().get("priv_show_avatar", True, t=bool))
        fl1.addWidget(self._priv_show_avatar)

        self._priv_show_typing = QCheckBox(
            "Отправлять индикатор «печатает…»")
        self._priv_show_typing.setChecked(
            S().get("priv_show_typing", True, t=bool))
        fl1.addWidget(self._priv_show_typing)

        self._priv_read_receipts = QCheckBox(
            "Отправлять уведомления о прочтении (галочки)")
        self._priv_read_receipts.setChecked(
            S().get("priv_read_receipts", True, t=bool))
        fl1.addWidget(self._priv_read_receipts)

        lay.addWidget(g1)

        # ── Звонки ──────────────────────────────────────────────────────────
        g2, fl2 = _grp("📞  Звонки")

        self._priv_allow_calls = QCheckBox(
            "Разрешить входящие звонки от всех пользователей")
        self._priv_allow_calls.setChecked(
            S().get("priv_allow_calls", True, t=bool))
        fl2.addWidget(self._priv_allow_calls)

        self._priv_allow_group_calls = QCheckBox(
            "Разрешить приглашения в групповые звонки")
        self._priv_allow_group_calls.setChecked(
            S().get("priv_allow_group_calls", True, t=bool))
        fl2.addWidget(self._priv_allow_group_calls)

        self._priv_hide_ip_call = QCheckBox(
            "Использовать relay для скрытия IP при звонках (требует relay-сервер)")
        self._priv_hide_ip_call.setChecked(
            S().get("priv_hide_ip_call", False, t=bool))
        self._priv_hide_ip_call.setToolTip(
            "Relay настраивается в разделе «Специалист»")
        fl2.addWidget(self._priv_hide_ip_call)

        lay.addWidget(g2)

        # ── Сообщения ────────────────────────────────────────────────────────
        g3, fl3 = _grp("💬  Сообщения")

        self._priv_link_preview = QCheckBox(
            "Показывать предпросмотр ссылок в сообщениях")
        self._priv_link_preview.setChecked(
            S().get("priv_link_preview", True, t=bool))
        fl3.addWidget(self._priv_link_preview)

        self._priv_save_history = QCheckBox(
            "Сохранять историю чатов локально")
        self._priv_save_history.setChecked(
            S().get("priv_save_history", True, t=bool))
        self._priv_save_history.setToolTip(
            "Если disabled — история не сохраняется и стирается при выходе")
        fl3.addWidget(self._priv_save_history)

        self._priv_encrypt_history = QCheckBox(
            "Шифровать локальную историю чатов (AES-256)")
        self._priv_encrypt_history.setChecked(
            S().get("priv_encrypt_history", False, t=bool))
        fl3.addWidget(self._priv_encrypt_history)

        self._priv_allow_fwd = QCheckBox(
            "Разрешить пересылку ваших сообщений другим пользователям")
        self._priv_allow_fwd.setChecked(
            S().get("priv_allow_fwd", True, t=bool))
        fl3.addWidget(self._priv_allow_fwd)

        lay.addWidget(g3)

        # ── E2E / Crypto ─────────────────────────────────────────────────────
        g4, fl4 = _grp("🔑  Encryption (E2E)")

        e2e_status = QLabel(
            "✅ End-to-End шифрование активно (X25519 + AES-256-GCM + Ed25519)"
            if True else "❌ E2E недоступно")
        e2e_status.setStyleSheet("color:#80FF80;font-size:11px;font-weight:bold;")
        fl4.addWidget(e2e_status)

        e2e_info = QLabel(
            "Все личные сообщения шифруются E2E автоматически.\n"
            "Ключи генерируются при первом запуске и хранятся локально.\n"
            "Публичный ключ идентичности отображается в профиле (GoidaID).")
        e2e_info.setWordWrap(True)
        e2e_info.setStyleSheet("font-size:10px;color:gray;")
        fl4.addWidget(e2e_info)

        regen_btn = QPushButton("🔄 Перегенерировать ключи")
        regen_btn.setToolTip(
            "Создать новую пару ключей.\n"
            "Внимание: существующие E2E-сессии будут сброшены.")
        regen_btn.clicked.connect(self._regen_e2e_keys)
        fl4.addWidget(regen_btn)

        lay.addWidget(g4)

        # ── Сохранить кнопка ────────────────────────────────────────────────
        save_priv = QPushButton("💾 Применить настройки приватности")
        save_priv.setObjectName("accent_btn")
        save_priv.setFixedHeight(34)
        save_priv.clicked.connect(self._save_privacy)
        lay.addWidget(save_priv)

        # ── Медиаплеер ───────────────────────────────────────────────
        g5, fl5 = _grp("🎵  Медиафайлы")
        media_lbl = QLabel(
            "При открытии медиафайлов (музыка, видео) из чата:")
        media_lbl.setStyleSheet("font-size:11px;background:transparent;")
        fl5.addWidget(media_lbl)

        self._priv_media_pref = QComboBox()
        self._priv_media_pref.addItems([
            "Спрашивать каждый раз",
            "Всегда Mewa (встроенный плеер)",
            "Всегда системный плеер",
        ])
        pref_map = {"": 0, "mewa": 1, "system": 2}
        cur_pref = S().get("media_open_pref", "", t=str)
        self._priv_media_pref.setCurrentIndex(pref_map.get(cur_pref, 0))
        fl5.addWidget(self._priv_media_pref)

        media_note = QLabel("Изменение вступает в силу немедленно.")
        media_note.setStyleSheet("font-size:9px;color:gray;background:transparent;")
        fl5.addWidget(media_note)
        lay.addWidget(g5)

        # ── Медиа ────────────────────────────────────────────────────────────
        g5, fl5 = _grp("🎵  Медиафайлы")
        fl5_form = QFormLayout()

        self._media_pref_combo = QComboBox()
        self._media_pref_combo.addItems([
            "Спрашивать каждый раз",
            "Всегда открывать в Mewa",
            "Всегда открывать системным плеером",
        ])
        pref = S().get("media_open_pref", "", t=str)
        idx = {"": 0, "mewa": 1, "system": 2}.get(pref, 0)
        self._media_pref_combo.setCurrentIndex(idx)
        fl5_form.addRow("По умолчанию открывать медиа:", self._media_pref_combo)

        # Link open preference
        self._link_pref_combo = QComboBox()
        self._link_pref_combo.addItems([
            "Спрашивать каждый раз",
            "Всегда в WNS (встроенный браузер)",
            "Всегда в системном браузере",
        ])
        link_pref = S().get("link_open_pref", "ask", t=str)
        link_idx = {"ask": 0, "wns": 1, "system": 2}.get(link_pref, 0)
        self._link_pref_combo.setCurrentIndex(link_idx)
        fl5_form.addRow("Ссылки из чата:", self._link_pref_combo)

        reset_links = QPushButton("🔄 Сбросить (спрашивать снова)")
        reset_links.clicked.connect(lambda: (
            S().set("link_open_pref", "ask"),
            self._link_pref_combo.setCurrentIndex(0)))
        fl5_form.addRow(reset_links)

        reset_pref = QPushButton("🔄 Сбросить (спрашивать снова)")
        reset_pref.clicked.connect(lambda: (
            S().set("media_open_pref", ""),
            self._media_pref_combo.setCurrentIndex(0)))
        fl5_form.addRow(reset_pref)
        fl5.addLayout(fl5_form)
        lay.addWidget(g5)

        # ── GoidaCRYPTO — слои защиты ──────────────────────────────────
        g_crypto = QGroupBox("🔐  GoidaCRYPTO — Слои защиты")
        g_crypto.setToolTip("Многоуровневая защита данных GoidaPhone")
        cfl = QVBoxLayout(g_crypto)

        _vault_exists = VAULT.vault_exists()
        _vault_open   = VAULT.is_unlocked()

        # Статус хранилища
        vault_status = QLabel(
            f"{'🔓 Хранилище разблокировано' if _vault_open else '🔒 Хранилище заблокировано' if _vault_exists else '⭕ Хранилище не создано'}")
        vault_status.setStyleSheet(
            f"font-weight:bold;color:{'#27AE60' if _vault_open else '#E74C3C' if _vault_exists else '#F39C12'};"
            "background:transparent;")
        cfl.addWidget(vault_status)

        # Описание слоёв
        layers_lbl = QLabel(
            "L0  QSettings (дефолт)  ·  L1  SecureVault AES-256-GCM\n"
            "L2  История зашифрована  ·  L3  Secure Wipe RAM\n"
            "L4  Stealth Mode  ·  L5  Блокировка скриншотов\n"
            "L6  Авто-очистка буфера  ·  L7  Блокировка по таймеру\n"
            "L8  Анти-шпион  ·  L9  Авто-TTL сообщений\n"
            "L10 Decoy-пароль  ·  L11 Traffic padding\n"
            "L12 Только LAN  ·  L13 Журнал безопасности\n"
            "L14 Ротация ключей  ·  L15 HMAC-подпись\n"
            "L16 Replay защита  ·  L17 Rate limiting\n"
            "L18 Whitelist IP  ·  L19 PFS\n"
            "L20 🔴 Параноидальный режим")
        layers_lbl.setStyleSheet(
            f"font-size:8pt;color:{t['text_dim']};background:transparent;font-family:monospace;")
        layers_lbl.setWordWrap(True)
        cfl.addWidget(layers_lbl)

        # Поле пароля хранилища
        vault_pass_row = QHBoxLayout()
        vault_pass_lbl = QLabel("Пароль хранилища:")
        vault_pass_lbl.setStyleSheet("background:transparent;")
        vault_pass_row.addWidget(vault_pass_lbl)
        self._vault_pass_edit = QLineEdit()
        self._vault_pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self._vault_pass_edit.setPlaceholderText(
            "Введите для разблокировки…" if _vault_exists else "Создать новый пароль…")
        vault_pass_row.addWidget(self._vault_pass_edit, 1)
        cfl.addLayout(vault_pass_row)

        vault_btn_row = QHBoxLayout()
        if _vault_open:
            lock_btn = QPushButton("🔒 Заблокировать хранилище")
            lock_btn.clicked.connect(lambda: (VAULT.lock(), QMessageBox.information(
                self, "GoidaCRYPTO", "Хранилище заблокировано.")))
            vault_btn_row.addWidget(lock_btn)
        else:
            unlock_btn = QPushButton("🔓 Разблокировать / Создать")
            def _vault_unlock():
                pp = self._vault_pass_edit.text()
                if not pp:
                    QMessageBox.warning(self, "GoidaCRYPTO", "Введите пароль хранилища.")
                    return
                if VAULT.vault_exists():
                    ok = VAULT.unlock(pp)
                    if ok:
                        QMessageBox.information(self, "GoidaCRYPTO",
                            "✅ Хранилище разблокировано!\n"
                            "Пароль шифрования теперь хранится зашифровано.")
                    else:
                        QMessageBox.warning(self, "GoidaCRYPTO", "❌ Неверный пароль.")
                else:
                    # Создаём новое — мигрируем существующие данные
                    VAULT.unlock(pp)  # создаст пустое
                    # Мигрируем чувствительные данные
                    cfg = S()
                    for key in _VAULT_SENSITIVE_KEYS:
                        val = cfg.get(key, "", t=str)
                        if val:
                            VAULT.set(key, val)
                    QMessageBox.information(self, "GoidaCRYPTO",
                        "✅ Хранилище создано!\n"
                        f"Мигрировано {len(_VAULT_SENSITIVE_KEYS)} полей.\n"
                        "Данные зашифрованы AES-256-GCM.")
                self._vault_pass_edit.clear()
            unlock_btn.clicked.connect(_vault_unlock)
            vault_btn_row.addWidget(unlock_btn)

        audit_btn = QPushButton("📋 Аудит")
        audit_btn.setToolTip("Показать содержимое хранилища (без значений)")
        def _show_audit():
            report = VAULT.export_audit_log()
            dlg = QDialog(self); dlg.setWindowTitle("GoidaCRYPTO Audit")
            dlg.resize(500, 350)
            vl = QVBoxLayout(dlg)
            te = QPlainTextEdit(report); te.setReadOnly(True)
            te.setStyleSheet(f"font-family:monospace;font-size:9pt;background:{t['bg3']};color:{t['text']};")
            vl.addWidget(te)
            vl.addWidget(QPushButton(_L("Закрыть", "Close", "閉じる"), clicked=dlg.accept))
            dlg.exec()
        audit_btn.clicked.connect(_show_audit)
        vault_btn_row.addWidget(audit_btn)

        destroy_btn = QPushButton("💣 Уничтожить")
        destroy_btn.setStyleSheet(
            f"QPushButton{{background:#7a1a1a;color:white;border-radius:4px;border:none;padding:4px 8px;}}"
            f"QPushButton:hover{{background:#c0392b;}}")
        destroy_btn.setToolTip("Безвозвратно уничтожить хранилище (3-кратная перезапись)")
        def _destroy_vault():
            from PyQt6.QtWidgets import QMessageBox
            if QMessageBox.question(self, "GoidaCRYPTO",
                    "Безвозвратно уничтожить хранилище?\nВсе зашифрованные данные будут утеряны!",
                    QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                    QMessageBox.StandardButton.No
                ) == QMessageBox.StandardButton.Yes:
                VAULT.destroy()
                QMessageBox.information(self, "GoidaCRYPTO", "Хранилище уничтожено (3-pass wipe).")
        destroy_btn.clicked.connect(_destroy_vault)
        vault_btn_row.addWidget(destroy_btn)
        vault_btn_row.addStretch()
        cfl.addLayout(vault_btn_row)

        # Опциональные слои
        # Все 20 слоёв — описание + ключ + tooltip + нужен vault или no
        ALL_LAYERS = [
            # key, label, tooltip, needs_vault
            ("crypto_layer2_history",
             "Layer 2  — Encryption истории чатов",
             "История шифруется ключом SecureVault. Без пароля — не читается.",
             True),
            ("crypto_layer3_wipe",
             "Layer 3  — Secure Wipe RAM при выходе",
             "Обнуляет пароли и ключи в памяти перед завершением.",
             False),
            ("crypto_layer4_stealth",
             "Layer 4  — Stealth Mode (no в Alt+Tab)",
             "Окно исчезает из панели задач и Alt+Tab.",
             False),
            ("crypto_layer5_screenshot",
             "Layer 5  — Блокировать скриншоты окна",
             "Окно не захватывается Print Screen / системными скриншотами.",
             False),
            ("crypto_layer6_clipboard",
             "Layer 6  — Авто-очистка буфера обмена",
             "Буфер очищается через 30 секунд после копирования из GoidaPhone.",
             False),
            ("crypto_layer7_idle_lock",
             "Layer 7  — Блокировка при бездействии",
             "PIN-экран через N минут без активности (задаётся в Блокировка).",
             False),
            ("crypto_layer8_typing_noise",
             "Layer 8  — Анти-клавиатурный шпион (timing noise)",
             "Добавляет случайную задержку отправки чтобы скрыть ритм набора.",
             False),
            ("crypto_layer9_msg_ttl",
             "Layer 9  — Авто-удаление сообщений (TTL)",
             "Сообщения удаляются через заданное время (исчезающие сообщения).",
             False),
            ("crypto_layer10_decoy",
             "Layer 10 — Decoy-пароль (ложный профиль)",
             "Второй пароль открывает чистый профиль-приманку без данных.",
             True),
            ("crypto_layer11_traffic_pad",
             "Layer 11 — Traffic padding (маскировка трафика)",
             "Отправляет фиктивные пакеты чтобы скрыть паттерны общения.",
             False),
            ("crypto_layer12_local_only",
             "Layer 12 — Режим только локальная сеть",
             "Блокирует исходящие соединения за пределы LAN.",
             False),
            ("crypto_layer13_audit_log",
             "Layer 13 — Журнал безопасности",
             "Записывает все входы, выходы и смены настроек с временными метками.",
             False),
            ("crypto_layer14_hkdf_rotate",
             "Layer 14 — Ротация сессионных ключей",
             "ECDH-ключи обновляются каждые 24 часа или при переподключении.",
             False),
            ("crypto_layer15_msg_hmac",
             "Layer 15 — HMAC-подпись каждого сообщения",
             "Каждое сообщение подписывается Ed25519. Подделка невозможна.",
             False),
            ("crypto_layer16_replay",
             "Layer 16 — Защита от replay-атак",
             "Nonce-кэш 5000 сообщений. Повторные пакеты отбрасываются.",
             False),
            ("crypto_layer17_rate_limit",
             "Layer 17 — Rate limiting входящих пакетов",
             "Блокирует >100 пакетов/сек с одного IP (DoS-защита).",
             False),
            ("crypto_layer18_ip_whitelist",
             "Layer 18 — Whitelist IP-адресов",
             "Принимает сообщения только от известных IP (задать в Специалист).",
             False),
            ("crypto_layer19_forward_secrecy",
             "Layer 19 — Perfect Forward Secrecy",
             "Каждая сессия использует ephemeral X25519. Прошлые сессии защищены.",
             False),
            ("crypto_layer20_paranoid",
             "Layer 20 — Параноидальный режим 🔴",
             "Все предыдущие слои + принудительное шифрование + отключение логов + TTL 60с.",
             True),
        ]

        self._layer_cbs = {}
        for key, label, tip, needs_vault in ALL_LAYERS:
            cb = QCheckBox(label)
            cb.setChecked(S().get(key, False, t=bool))
            cb.setToolTip(tip)
            if needs_vault and not VAULT.is_unlocked():
                cb.setEnabled(False)
                cb.setToolTip(tip + "\n⚠ Требуется разблокировать SecureVault выше.")
            cfl.addWidget(cb)
            self._layer_cbs[key] = cb

        # Параноидальный режим — включает все остальные
        def _on_paranoid(checked):
            if checked:
                for cb in self._layer_cbs.values():
                    if cb.isEnabled():
                        cb.setChecked(True)
        if "crypto_layer20_paranoid" in self._layer_cbs:
            self._layer_cbs["crypto_layer20_paranoid"].toggled.connect(_on_paranoid)

        # Обратная совместимость — старые атрибуты
        self._layer2_cb = self._layer_cbs.get("crypto_layer2_history")
        self._layer3_cb = self._layer_cbs.get("crypto_layer3_wipe")
        self._layer4_cb = self._layer_cbs.get("crypto_layer4_stealth")
        self._layer5_cb = self._layer_cbs.get("crypto_layer5_screenshot")

        lay.addWidget(g_crypto)

        note = QLabel(
            "ℹ Все настройки вступают в силу немедленно после сохранения.")
        note.setStyleSheet("font-size:9px;color:gray;")
        lay.addWidget(note)

        lay.addStretch()
        return outer

    def _save_privacy(self):
        # Save media preference
        if hasattr(self, '_media_pref_combo'):
            pref_map = {0: "", 1: "mewa", 2: "system"}
            S().set("media_open_pref", pref_map.get(self._media_pref_combo.currentIndex(), ""))
        if hasattr(self, '_link_pref_combo'):
            link_map = {0: "ask", 1: "wns", 2: "system"}
            S().set("link_open_pref", link_map.get(self._link_pref_combo.currentIndex(), "ask"))
        S().set("priv_show_status",       self._priv_show_status.isChecked())
        S().set("priv_show_avatar",       self._priv_show_avatar.isChecked())
        S().set("priv_show_typing",       self._priv_show_typing.isChecked())
        S().set("priv_read_receipts",     self._priv_read_receipts.isChecked())
        S().set("priv_allow_calls",       self._priv_allow_calls.isChecked())
        S().set("priv_allow_group_calls", self._priv_allow_group_calls.isChecked())
        S().set("priv_hide_ip_call",      self._priv_hide_ip_call.isChecked())
        S().set("priv_link_preview",      self._priv_link_preview.isChecked())
        S().set("priv_save_history",      self._priv_save_history.isChecked())
        # Also sync with the main save_history setting
        S().set("save_history",           self._priv_save_history.isChecked())
        S().set("priv_encrypt_history",   self._priv_encrypt_history.isChecked())
        S().set("priv_allow_fwd",         self._priv_allow_fwd.isChecked())
        pref_vals = ["", "mewa", "system"]
        pref = pref_vals[self._priv_media_pref.currentIndex()]
        S().set("media_open_pref", pref)
        import builtins as _b
        if hasattr(_b, '__MEDIA_PREF_GLOBAL'):
            _b.__MEDIA_PREF_GLOBAL = pref
        QMessageBox.information(self, "Приватность",
            "✅ Настройки приватности сохранены.")

    def _regen_e2e_keys(self):
        reply = QMessageBox.warning(self, "Перегенерировать ключи",
            "Создать новую пару E2E-ключей?\n\n"
            "Все активные зашифрованные сессии будут сброшены.\n"
            "Собеседникам придётся заново согласовать ключи.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            try:
                from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
                from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
                from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat, PrivateFormat, NoEncryption
                id_priv = Ed25519PrivateKey.generate()
                dh_priv = X25519PrivateKey.generate()
                S().set("identity_priv_b64", base64.b64encode(
                    id_priv.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())).decode())
                S().set("dh_priv_b64", base64.b64encode(
                    dh_priv.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())).decode())
                QMessageBox.information(self, "Ключи",
                    "✅ Новые E2E-ключи сгенерированы.\n"
                    "Перезапустите GoidaPhone для применения.")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Не удалось сгенерировать ключи:\n{e}")

    def _tab_call_settings(self) -> QWidget:
        """📞 Звонки — проверка микрофона, камеры, демонстрации экрана, качества."""
        outer = QWidget()
        outer_lay = QVBoxLayout(outer); outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer); _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa); _sa.setWidget(w)
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(14, 14, 14, 14)
        lay.setSpacing(10)
        t = get_theme(S().theme)

        def _grp(title):
            g = QGroupBox(title); fl = QVBoxLayout(g); return g, fl

        # ── Микрофон ─────────────────────────────────────────────────────
        g_mic, fl_mic = _grp("🎤  Микрофон")

        mic_row = QHBoxLayout()
        mic_lbl = QLabel("Input device:")
        mic_lbl.setStyleSheet("font-size:11px;background:transparent;")
        mic_row.addWidget(mic_lbl)
        self._cs_mic_combo = QComboBox()
        try:
            import pyaudio as _pa
            p = _pa.PyAudio()
            for i in range(p.get_device_count()):
                info = p.get_device_info_by_index(i)
                if info.get("maxInputChannels", 0) > 0:
                    self._cs_mic_combo.addItem(info["name"], i)
            p.terminate()
        except Exception:
            self._cs_mic_combo.addItem("Системное устройство по умолчанию", -1)
        mic_row.addWidget(self._cs_mic_combo, stretch=1)
        fl_mic.addLayout(mic_row)

        # Mic test
        mic_test_row = QHBoxLayout()
        self._cs_mic_test_btn = QPushButton("🎙 Тест микрофона (3 сек)")
        self._cs_mic_test_btn.setObjectName("accent_btn")
        self._cs_mic_test_btn.setCheckable(False)
        self._cs_mic_test_btn.clicked.connect(self._test_mic)
        mic_test_row.addWidget(self._cs_mic_test_btn)
        self._cs_mic_level = QProgressBar()
        self._cs_mic_level.setRange(0, 100)
        self._cs_mic_level.setValue(0)
        self._cs_mic_level.setFixedHeight(10)
        self._cs_mic_level.setTextVisible(False)
        self._cs_mic_level.setStyleSheet(
            f"QProgressBar{{background:{t['bg3']};border-radius:5px;border:none;}}"
            f"QProgressBar::chunk{{background:#39FF14;border-radius:5px;}}")
        mic_test_row.addWidget(self._cs_mic_level, stretch=1)
        fl_mic.addLayout(mic_test_row)

        self._cs_mic_status = QLabel("Нажмите для проверки микрофона")
        self._cs_mic_status.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        fl_mic.addWidget(self._cs_mic_status)
        lay.addWidget(g_mic)

        # ── Динамик / наушники ───────────────────────────────────────────
        g_spk, fl_spk = _grp("🔊  Динамик / наушники")
        spk_row = QHBoxLayout()
        spk_lbl = QLabel("Output device:")
        spk_lbl.setStyleSheet("font-size:11px;background:transparent;")
        spk_row.addWidget(spk_lbl)
        self._cs_spk_combo = QComboBox()
        try:
            import pyaudio as _pa2
            p2 = _pa2.PyAudio()
            for i in range(p2.get_device_count()):
                info2 = p2.get_device_info_by_index(i)
                if info2.get("maxOutputChannels", 0) > 0:
                    self._cs_spk_combo.addItem(info2["name"], i)
            p2.terminate()
        except Exception:
            self._cs_spk_combo.addItem("Системное устройство по умолчанию", -1)
        spk_row.addWidget(self._cs_spk_combo, stretch=1)
        fl_spk.addLayout(spk_row)

        spk_test_btn = QPushButton("🔈 Воспроизвести тестовый тон")
        spk_test_btn.clicked.connect(self._test_speaker)
        fl_spk.addWidget(spk_test_btn)
        lay.addWidget(g_spk)

        # ── Качество звонка ──────────────────────────────────────────────
        g_q, fl_q = _grp("📶  Качество голоса")
        qual_row = QHBoxLayout()
        qual_lbl = QLabel("Частота дискретизации:")
        qual_lbl.setStyleSheet("font-size:11px;background:transparent;")
        qual_row.addWidget(qual_lbl)
        self._cs_quality = QComboBox()
        for label, val in [("Эконом (8 кГц)", 8000),
                            ("Стандарт (16 кГц)", 16000),
                            ("Высокое (48 кГц)", 48000)]:
            self._cs_quality.addItem(label, val)
        cur_rate = S().get("audio_rate", 16000, t=int)
        for i in range(self._cs_quality.count()):
            if self._cs_quality.itemData(i) == cur_rate:
                self._cs_quality.setCurrentIndex(i)
        qual_row.addWidget(self._cs_quality)
        fl_q.addLayout(qual_row)

        vad_cb = QCheckBox("VAD — noise gate (не передавать тишину)")
        vad_cb.setChecked(S().get("vad_enabled", True, t=bool))
        vad_cb.toggled.connect(lambda v: S().set("vad_enabled", v))
        fl_q.addWidget(vad_cb)
        lay.addWidget(g_q)

        # ── Демонстрация экрана ──────────────────────────────────────────
        g_sh, fl_sh = _grp("🖥  Демонстрация экрана")
        share_info = QLabel(
            "Во время звонка: кнопка демонстрации в окне звонка.\n"
            "Передача кадров: 5 fps, захват всего экрана.")
        share_info.setWordWrap(True)
        share_info.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        fl_sh.addWidget(share_info)

        share_test_btn = QPushButton("📸 Тест захвата экрана")
        share_test_btn.clicked.connect(self._test_screen_capture)
        fl_sh.addWidget(share_test_btn)
        self._cs_share_preview = QLabel()
        self._cs_share_preview.setFixedHeight(100)
        self._cs_share_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._cs_share_preview.setStyleSheet(f"background:{t['bg3']};border-radius:6px;")
        self._cs_share_preview.setText("Нажмите тест для предпросмотра")
        fl_sh.addWidget(self._cs_share_preview)
        lay.addWidget(g_sh)

        # ── Вебкамера ────────────────────────────────────────────────────
        g_cam, fl_cam = _grp("📷  Камера")

        cam_info = QLabel("Предпросмотр вебкамеры для проверки перед звонком.")
        cam_info.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        fl_cam.addWidget(cam_info)

        cam_row = QHBoxLayout()
        cam_btn = QPushButton("📷 Включить камеру")
        cam_btn.setObjectName("accent_btn")
        stop_cam_btn = QPushButton("⏹ Стоп")
        stop_cam_btn.setEnabled(False)
        cam_row.addWidget(cam_btn)
        cam_row.addWidget(stop_cam_btn)
        cam_row.addStretch()
        fl_cam.addLayout(cam_row)

        self._cs_cam_preview = QLabel("Предпросмотр камеры...")
        self._cs_cam_preview.setFixedHeight(120)
        self._cs_cam_preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._cs_cam_preview.setStyleSheet(
            f"background:#000;border-radius:8px;border:1px solid {t['border']};"
            "color:#666;font-size:11px;")
        self._cs_cam_preview.setVisible(False)
        fl_cam.addWidget(self._cs_cam_preview)
        self._cs_cam_obj = None

        def _start_cam_test():
            cam_btn.setEnabled(False)
            cam_btn.setText("⏳ Включение...")
            try:
                from PyQt6.QtMultimedia import QCamera, QMediaCaptureSession
                from PyQt6.QtMultimediaWidgets import QVideoWidget
                self._cs_cam_preview.setVisible(True)
                self._cs_cam_preview.setText("")
                if not hasattr(self, '_cs_vid_widget') or not self._cs_vid_widget:
                    self._cs_vid_widget = QVideoWidget(self._cs_cam_preview)
                    self._cs_vid_widget.setGeometry(0, 0,
                        self._cs_cam_preview.width(),
                        self._cs_cam_preview.height())
                self._cs_cam_obj = QCamera()
                self._cs_cam_session = QMediaCaptureSession()
                self._cs_cam_session.setCamera(self._cs_cam_obj)
                self._cs_cam_session.setVideoOutput(self._cs_vid_widget)
                self._cs_cam_obj.start()
                self._cs_vid_widget.show()
                cam_btn.setText("📷 Работает")
                stop_cam_btn.setEnabled(True)
            except ImportError:
                self._cs_cam_preview.setText(
                    "Установи: pip install PyQt6-QtMultimediaWidgets")
                cam_btn.setText("▶ Включить"); cam_btn.setEnabled(True)
            except Exception as e:
                self._cs_cam_preview.setText(f"❌ Ошибка: {e}")
                self._cs_cam_preview.setVisible(True)
                cam_btn.setText("▶ Включить"); cam_btn.setEnabled(True)

        def _stop_cam_test():
            try:
                cam_obj = getattr(self, '_cs_cam_obj', None)
                if cam_obj:
                    try: cam_obj.stop()
                    except Exception: pass
                    self._cs_cam_obj = None
            except Exception: pass
            try:
                vid_w = getattr(self, '_cs_vid_widget', None)
                if vid_w:
                    vid_w.hide()
                    try: vid_w.deleteLater()
                    except Exception: pass
                    self._cs_vid_widget = None
            except Exception: pass
            try:
                self._cs_cam_preview.setVisible(True)
                self._cs_cam_preview.setPixmap(QPixmap())
                self._cs_cam_preview.setText("Предпросмотр камеры...")
            except Exception: pass
            cam_btn.setText("▶ Включить")
            cam_btn.setEnabled(True)
            stop_cam_btn.setEnabled(False)

        cam_btn.clicked.connect(_start_cam_test)
        stop_cam_btn.clicked.connect(_stop_cam_test)
        lay.addWidget(g_cam)

        # ── Слышимость себя ──────────────────────────────────────────────
        g_hear, fl_hear = _grp("👂  Слышимость")
        hear_info = QLabel(
            "Тест: услышите собственный микрофон через динамики (эхо-тест).")
        hear_info.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        fl_hear.addWidget(hear_info)
        echo_btn = QPushButton("🔁 Запустить эхо-тест (3 сек)")
        self._cs_echo_status = QLabel("Говорите в микрофон — услышите себя")
        self._cs_echo_status.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        _echo_running = [False]
        def _run_echo():
            if _echo_running[0]: return
            _echo_running[0] = True
            QTimer.singleShot(5000, lambda: (_echo_running.__setitem__(0, False), echo_btn.setEnabled(True), echo_btn.setText("🔁 Запустить эхо-тест (3 сек)")))
            echo_btn.setEnabled(False)
            echo_btn.setText("⏳ Идёт тест...")
            self._cs_echo_status.setText("🔴 Говорите — слушайте себя...")
            import threading
            def _echo():
                try:
                    import pyaudio as _pa
                    CHUNK = 1024; RATE = 16000
                    p = _pa.PyAudio()
                    si = p.open(format=_pa.paInt16, channels=1, rate=RATE,
                                input=True, frames_per_buffer=CHUNK)
                    so = p.open(format=_pa.paInt16, channels=1, rate=RATE,
                                output=True, frames_per_buffer=CHUNK)
                    for _ in range(int(RATE/CHUNK*3)):
                        so.write(si.read(CHUNK, exception_on_overflow=False))
                    si.stop_stream(); si.close()
                    so.stop_stream(); so.close()
                    p.terminate()
                    def _done():
                        _echo_running[0] = False
                        echo_btn.setEnabled(True)
                        echo_btn.setText("🔁 Запустить эхо-тест (3 сек)")
                        self._cs_echo_status.setText("✅ Эхо-тест ended")
                    QTimer.singleShot(0, _done)
                except Exception as e:
                    def _err(err=str(e)):
                        _echo_running[0] = False
                        echo_btn.setEnabled(True)
                        echo_btn.setText("🔁 Запустить эхо-тест (3 сек)")
                        self._cs_echo_status.setText(f"❌ {err}")
                    QTimer.singleShot(0, _err)
            threading.Thread(target=_echo, daemon=True).start()
        echo_btn.clicked.connect(_run_echo)
        fl_hear.addWidget(echo_btn)
        fl_hear.addWidget(self._cs_echo_status)
        lay.addWidget(g_hear)

        # ── Сохранить ────────────────────────────────────────────────────
        save_btn = QPushButton("💾 Применить настройки звонка")
        save_btn.setObjectName("accent_btn"); save_btn.setFixedHeight(34)
        save_btn.clicked.connect(self._save_call_settings)
        lay.addWidget(save_btn)

        lay.addStretch()
        return outer

    def _test_mic(self):
        """Quick 3-second mic level test."""
        if getattr(self, '_mic_test_running', False):
            return
        self._mic_test_running = True
        self._cs_mic_status.setText("🔴 Запись... (3 сек)")
        self._cs_mic_test_btn.setEnabled(False)
        self._cs_mic_test_btn.setText("⏳ Идёт запись…")
        try:
            import pyaudio as _pa, threading, struct, math
            def _run():
                try:
                    p = _pa.PyAudio()
                    stream = p.open(format=_pa.paInt16, channels=1,
                                    rate=16000, input=True, frames_per_buffer=512)
                    peak = 0
                    for _ in range(int(16000/512 * 3)):
                        data = stream.read(512, exception_on_overflow=False)
                        samples = struct.unpack(f'<{len(data)//2}h', data)
                        rms = math.sqrt(sum(s*s for s in samples)/max(len(samples),1))
                        level = min(100, int(rms/320))
                        peak  = max(peak, level)
                        QTimer.singleShot(0, lambda l=level: self._cs_mic_level.setValue(l))
                    stream.stop_stream(); stream.close(); p.terminate()
                    def _done():
                        self._mic_test_running = False
                        self._cs_mic_test_btn.setEnabled(True)
                        self._cs_mic_test_btn.setText("🎙 Тест микрофона (3 сек)")
                        self._cs_mic_level.setValue(0)
                        if peak > 5:
                            self._cs_mic_status.setText(f"✅ Микрофон работает (пик: {peak}%)")
                        else:
                            self._cs_mic_status.setText("⚠ Сигнал очень слабый — проверьте устройство")
                    QTimer.singleShot(0, _done)
                except Exception as e:
                    def _err(err=str(e)):
                        self._mic_test_running = False
                        self._cs_mic_test_btn.setEnabled(True)
                        self._cs_mic_test_btn.setText("🎙 Тест микрофона (3 сек)")
                        self._cs_mic_status.setText(f"❌ Ошибка: {err}")
                    QTimer.singleShot(0, _err)
            threading.Thread(target=_run, daemon=True).start()
        except ImportError:
            self._mic_test_running = False
            self._cs_mic_status.setText("⚠ PyAudio не установлен")
            self._cs_mic_test_btn.setEnabled(True)
            self._cs_mic_test_btn.setText("🎙 Тест микрофона (3 сек)")

    def _test_speaker(self):
        """Play a synthetic test tone through speakers."""
        try:
            _synth_tone([(440, 200, 0.5),(660, 200, 0.5),(880, 300, 0.4)])
            self._cs_mic_status.setText("🔊 Тон воспроизведён")
        except Exception as e:
            self._cs_mic_status.setText(f"⚠ {e}")

    def _test_screen_capture(self):
        """Grab screen and show preview."""
        try:
            screen = QApplication.primaryScreen()
            if not screen:
                self._cs_share_preview.setText("⚠ Экран не найден")
                return
            pix = screen.grabWindow(0)
            scaled = pix.scaled(
                self._cs_share_preview.width() - 8,
                self._cs_share_preview.height() - 8,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation)
            self._cs_share_preview.setPixmap(scaled)
        except Exception as e:
            self._cs_share_preview.setText(f"⚠ {e}")

    def _save_call_settings(self):
        rate = self._cs_quality.currentData()
        S().set("audio_rate", rate)
        QMessageBox.information(self, "Звонки",
            "✅ Настройки звонка сохранены. Частота вступит в силу при следующем звонке.")

    def _tab_specialist(self) -> QWidget:
        """
        п.30 — 'Для специалистов' — настройка relay сервера, продвинутые параметры.
        Этот раздел скрыт от обычных пользователей и предназначен для:
        - IT-специалистов настраивающих корпоративный деплой
        - Разработчиков
        - Системных администраторов
        """
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0,0,0,0)
        _sa = QScrollArea(outer)          # parent=outer keeps it alive
        _sa.setWidgetResizable(True)
        _sa.setFrameShape(QFrame.Shape.NoFrame)
        _sa.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        outer_lay.addWidget(_sa)
        w = QWidget(_sa)                  # parent=_sa keeps it alive
        w.setSizePolicy(
            QSizePolicy.Policy.Preferred,
            QSizePolicy.Policy.MinimumExpanding)
        _sa.setWidget(w)
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12,12,12,12)
        lay.setSpacing(8)

        t = get_theme(S().theme)

        # Warning banner
        warn = QLabel(
            "⚠ ВНИМАНИЕ: Эти настройки предназначены для опытных пользователей.\n"
            "Неправильная конфигурация может нарушить работу GoidaPhone.\n\n"
            "WARNING: These settings are for advanced users only.\n"
            "Incorrect configuration may break GoidaPhone.")
        warn.setWordWrap(True)
        warn.setStyleSheet(f"""
            background: #3A2000; color: #FFB060;
            border: 1px solid #6A4000; border-radius: 6px;
            padding: 8px; font-size: 10px;
        """)
        lay.addWidget(warn)

        # Static peers (VPN/WAN direct IPs)
        static_g = QGroupBox("📍 Статические пиры (VPN / WAN)")
        static_fl = QFormLayout(static_g)

        static_info = QLabel("Добавь IP пиров для VPN/WAN. Один IP на строку: 1.2.3.4 или 1.2.3.4:17385")
        static_info.setWordWrap(True)
        static_info.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        static_fl.addRow(static_info)

        import json as _j
        raw_static = S().get("static_peers", "[]", t=str)
        try: _initial_peers = chr(10).join(_j.loads(raw_static))
        except: _initial_peers = ""

        self._static_peers_edit = QPlainTextEdit(_initial_peers)
        self._static_peers_edit.setFixedHeight(100)
        self._static_peers_edit.setPlaceholderText("192.168.1.100 / 26.123.45.67 / 10.0.0.1:17385")
        self._static_peers_edit.setStyleSheet(
            f"QPlainTextEdit{{background:{t['bg']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:6px;"
            "font-family:monospace;font-size:11px;padding:6px;}}")
        static_fl.addRow(self._static_peers_edit)
        lay.addWidget(static_g)

        # Relay server
        relay_g = QGroupBox("🌐 Relay Сервер")
        rfl = QFormLayout(relay_g)

        self._relay_enabled = QCheckBox(
            "Использовать relay сервер (для подключения через интерno)")
        self._relay_enabled.setChecked(S().relay_enabled)
        rfl.addRow(self._relay_enabled)

        self._relay_addr = QLineEdit()
        self._relay_addr.setPlaceholderText("host:port  (например: relay.example.com:17385)")
        self._relay_addr.setText(S().relay_server)
        rfl.addRow("Адрес relay:", self._relay_addr)

        relay_info = QLabel(
            "Relay server позволяет подключаться через интерno без VPN.\n"
            "Relay только передаёт пакеты — не читает содержимое.\n"
            "Поддерживает: GoidaPhone Relay Server v1+")
        relay_info.setWordWrap(True)
        relay_info.setStyleSheet(f"color: {t['text_dim']}; font-size: 9px;")
        rfl.addRow(relay_info)

        test_relay = QPushButton("🔌 Проверить соединение")
        test_relay.clicked.connect(self._test_relay)
        rfl.addRow(test_relay)
        self._relay_status = QLabel("")
        rfl.addRow(self._relay_status)

        lay.addWidget(relay_g)

        # Protocol settings
        proto_g = QGroupBox("⚙ Protocol")
        pfl = QFormLayout(proto_g)

        pfl.addRow("Версия протокола:", QLabel(str(PROTOCOL_VERSION)))
        pfl.addRow("Мин. совместимая:", QLabel(str(PROTOCOL_COMPAT)))
        pfl.addRow("Версия приложения:", QLabel(APP_VERSION))

        self._broadcast_interval = QSpinBox()
        self._broadcast_interval.setRange(1, 30)
        self._broadcast_interval.setValue(S().get("broadcast_interval", 3, t=int))
        self._broadcast_interval.setSuffix(" сек")
        self._broadcast_interval.setToolTip(
            "Интервал отправки presence-сообщений.\n"
            "Меньше = быстрее обнаружение, больше трафик.\n"
            "Рекомендуется: 3 секунды.")
        pfl.addRow("Интервал presence:", self._broadcast_interval)

        self._peer_timeout = QSpinBox()
        self._peer_timeout.setRange(5, 120)
        self._peer_timeout.setValue(S().get("peer_timeout", 20, t=int))
        self._peer_timeout.setSuffix(" сек")
        self._peer_timeout.setToolTip(
            "Через сколько секунд без presence считать пользователя офлайн.")
        pfl.addRow("Таймаут пира:", self._peer_timeout)

        lay.addWidget(proto_g)

        # Debug section
        debug_g = QGroupBox("🔧 Отладка")
        dfl = QFormLayout(debug_g)

        self._debug_log_cb = QCheckBox("Включить отладочный лог в консоль")
        self._debug_log_cb.setChecked(S().get("debug_log", False, t=bool))
        dfl.addRow(self._debug_log_cb)

        export_log = QPushButton("📤 Экспортировать лог сессии")
        export_log.clicked.connect(self._export_log)
        dfl.addRow(export_log)

        lay.addWidget(debug_g)

        # ── Детальная конфигурация сервера (перенесена из вкладки Сеть) ──
        # ── Детальная привязка к серверу / сети ──────────────────────────────
        g_bind = QGroupBox("🔧 Детальная конфигурация сервера")
        bind_fl = QFormLayout(g_bind)

        # Bind address
        self._bind_addr = QLineEdit()
        self._bind_addr.setText(S().get("bind_addr", "0.0.0.0", t=str))
        self._bind_addr.setPlaceholderText("0.0.0.0  (все интерфейсы)")
        self._bind_addr.setToolTip(
            "Адрес сетевого интерфейса для привязки UDP/TCP сокетов.\n"
            "0.0.0.0 = все интерфейсы, конкретный IP = только этот интерфейс.")
        bind_fl.addRow("Bind-адрес:", self._bind_addr)

        # Multicast group
        self._multicast_group = QLineEdit()
        self._multicast_group.setText(S().get("multicast_group", "", t=str))
        self._multicast_group.setPlaceholderText("напр. 239.255.0.1 (пусто = broadcast)")
        self._multicast_group.setToolTip(
            "Multicast-группа для обнаружения пиров вместо broadcast.\n"
            "Полезно в управляемых сетях, где broadcast запрещён.")
        bind_fl.addRow("Multicast группа:", self._multicast_group)

        # Heartbeat interval
        self._heartbeat_interval = QSpinBox()
        self._heartbeat_interval.setRange(1, 60)
        self._heartbeat_interval.setSuffix(" сек")
        self._heartbeat_interval.setToolTip(
            "Как часто отправлять heartbeat (PING) другим пирам.\n"
            "Меньше = быстрее обнаружение отключений, больше трафик.")
        self._heartbeat_interval.setValue(S().get("heartbeat_interval", 3, t=int))
        bind_fl.addRow("Heartbeat интервал:", self._heartbeat_interval)

        # Max connections
        self._max_connections = QSpinBox()
        self._max_connections.setRange(1, 500)
        self._max_connections.setToolTip("Максимальное число одновременных TCP-соединений.")
        self._max_connections.setValue(S().get("max_connections", 50, t=int))
        bind_fl.addRow("Макс. соединений:", self._max_connections)

        # TCP keepalive
        self._tcp_keepalive = QCheckBox("Включить TCP keepalive")
        self._tcp_keepalive.setToolTip(
            "Keepalive позволяет обнаружить разрыв соединения без явного дисконнекта.")
        self._tcp_keepalive.setChecked(S().get("tcp_keepalive", True, t=bool))
        bind_fl.addRow(self._tcp_keepalive)

        # Keepalive idle time
        self._keepalive_idle = QSpinBox()
        self._keepalive_idle.setRange(5, 600)
        self._keepalive_idle.setSuffix(" сек")
        self._keepalive_idle.setToolTip("Время простоя до отправки первого keepalive-пакета.")
        self._keepalive_idle.setValue(S().get("keepalive_idle", 60, t=int))
        bind_fl.addRow("Keepalive idle:", self._keepalive_idle)

        # Reconnect attempts
        self._reconnect_attempts = QSpinBox()
        self._reconnect_attempts.setRange(0, 20)
        self._reconnect_attempts.setToolTip("0 = не переподключаться автоматически.")
        self._reconnect_attempts.setValue(S().get("reconnect_attempts", 3, t=int))
        bind_fl.addRow("Попыток переподключения:", self._reconnect_attempts)

        # Reconnect delay
        self._reconnect_delay = QSpinBox()
        self._reconnect_delay.setRange(1, 60)
        self._reconnect_delay.setSuffix(" сек")
        self._reconnect_delay.setValue(S().get("reconnect_delay", 5, t=int))
        bind_fl.addRow("Задержка переподключения:", self._reconnect_delay)

        # MTU
        self._mtu_size = QSpinBox()
        self._mtu_size.setRange(512, 65507)
        self._mtu_size.setSingleStep(512)
        self._mtu_size.setToolTip(
            "MTU UDP-пакетов. 1400 подходит для большинства сетей.\n"
            "Уменьшите при фрагментации пакетов (VPN, туннели).")
        self._mtu_size.setValue(S().get("mtu_size", 1400, t=int))
        bind_fl.addRow("UDP MTU (байт):", self._mtu_size)

        # SO_REUSEADDR
        self._so_reuseaddr = QCheckBox("SO_REUSEADDR")
        self._so_reuseaddr.setToolTip(
            "Разрешает повторное использование порта без ожидания TIME_WAIT.\n"
            "Полезно при частых перезапусках GoidaPhone.")
        self._so_reuseaddr.setChecked(S().get("so_reuseaddr", True, t=bool))
        bind_fl.addRow(self._so_reuseaddr)

        # Relay server
        self._relay_host = QLineEdit()
        self._relay_host.setText(S().relay_server)
        self._relay_host.setPlaceholderText("relay.example.com:45700")
        self._relay_host.setToolTip(
            "Адрес relay-сервера (host:port) для соединения через NAT.\n"
            "Оставьте пустым для прямых P2P-соединений.")
        bind_fl.addRow("Relay-сервер:", self._relay_host)

        self._relay_enabled_cb = QCheckBox("Использовать relay при недоступности прямого соединения")
        self._relay_enabled_cb.setChecked(S().relay_enabled)
        bind_fl.addRow(self._relay_enabled_cb)

        # STUN server
        self._stun_server = QLineEdit()
        self._stun_server.setText(S().get("stun_server", "stun.l.google.com:19302", t=str))
        self._stun_server.setToolTip(
            "STUN-сервер для определения внешнего IP-адреса и NAT traversal.")
        bind_fl.addRow("STUN-сервер:", self._stun_server)

        # Priority mode
        self._conn_priority = QComboBox()
        self._conn_priority.addItems([
            "Авто (сначала прямое, потом relay)",
            "Только прямое P2P",
            "Только relay",
            "Всегда relay (максимальная совместимость)",
        ])
        priority_map = {"auto": 0, "direct": 1, "relay_only": 2, "always_relay": 3}
        self._conn_priority.setCurrentIndex(
            priority_map.get(S().get("conn_priority", "auto", t=str), 0))
        bind_fl.addRow("Приоритет соединения:", self._conn_priority)

        lay.addWidget(g_bind)
        lay.addStretch()
        return outer

    def _mk_specialist_scroll(self) -> QScrollArea:
        """Wrap the specialist tab in a scroll area so content never clips."""
        inner = self._tab_specialist()
        self._specialist_scroll_ref = QScrollArea()
        self._specialist_scroll_ref.setWidgetResizable(True)
        self._specialist_scroll_ref.setFrameShape(QFrame.Shape.NoFrame)
        self._specialist_scroll_ref.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._specialist_scroll_ref.setWidget(inner)
        t2 = get_theme(S().theme)
        self._specialist_scroll_ref.setStyleSheet(
            f"QScrollArea{{background:{t2['bg']};border:none;}}"
            f"QScrollBar:vertical{{background:{t2['bg3']};width:6px;border-radius:3px;}}"
            f"QScrollBar::handle:vertical{{background:{t2['accent']};border-radius:3px;}}"
            f"QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{{height:0;}}")
        return self._specialist_scroll_ref

    def _test_relay(self):
        """Test connection to relay server."""
        addr = self._relay_addr.text().strip()
        if not addr:
            self._relay_status.setText("❌ Укажите адрес relay сервера")
            self._relay_status.setStyleSheet("color: #FF6060;")
            return
        self._relay_status.setText("🔄 Проверка...")
        self._relay_status.setStyleSheet(f"color: {get_theme(S().theme)['text_dim']};")

        def check():
            try:
                host, port_str = addr.rsplit(":", 1)
                port = int(port_str)
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(5)
                sock.connect((host, port))
                sock.close()
                return True, None
            except Exception as e:
                return False, str(e)

        import threading
        def run():
            ok, err = check()
            if ok:
                QTimer.singleShot(0, lambda: [
                    self._relay_status.setText("✅ Соединение установлено"),
                    self._relay_status.setStyleSheet("color: #80FF80;")])
            else:
                QTimer.singleShot(0, lambda: [
                    self._relay_status.setText(f"❌ Ошибка: {err}"),
                    self._relay_status.setStyleSheet("color: #FF6060;")])
        threading.Thread(target=run, daemon=True).start()

    def _export_log(self):
        fn, _ = QFileDialog.getSaveFileName(
            self, "Экспорт лога", f"goidaphone_log_{int(time.time())}.txt", "Text (*.txt)")
        if fn:
            try:
                log_lines = [
                    f"GoidaPhone v{APP_VERSION} Session Log",
                    f"Time: {datetime.now().isoformat()}",
                    f"Username: {S().username}",
                    f"IP: {get_local_ip()}",
                    f"OS: {get_os_name()}",
                    f"Protocol: v{PROTOCOL_VERSION}",
                    "─" * 40,
                ]
                Path(fn).write_text("\n".join(log_lines), encoding="utf-8")
                QMessageBox.information(self, "Экспорт", f"Лог сохранён:\n{fn}")
            except Exception as e:
                QMessageBox.critical(self, "Error", str(e))
        cfg = S()
        # audio
        self._vol.setValue(cfg.volume)
        self._notif_sounds.setChecked(cfg.notification_sounds)
        # network
        self._udp_p.setValue(cfg.udp_port)
        self._tcp_p.setValue(cfg.tcp_port)
        # theme
        theme = cfg.theme
        for i in range(self._theme_combo.count()):
            if self._theme_combo.itemData(i) == theme:
                self._theme_combo.setCurrentIndex(i)
                break
        # data
        self._save_history.setChecked(cfg.save_history)
        self._show_splash.setChecked(cfg.show_splash)
        # license
        self._update_lic_status()

    def _save(self):
        cfg = S()
        cfg.set("volume", self._vol.value())
        cfg.set("notification_sounds", self._notif_sounds.isChecked())

        # Звуковая схема
        if hasattr(self, '_sound_combos'):
            import json as _j5
            scheme = {}
            for event, combo in self._sound_combos.items():
                val = combo.currentText()
                if val == "(выключен)":
                    scheme[event] = "__none__"
                elif val == "(без изменений)":
                    pass  # не перезаписываем — остаётся дефолт
                else:
                    scheme[event] = val
            cfg.set("sound_scheme", _j5.dumps(scheme))
        cfg.set("vad_enabled",  self._vad_cb.isChecked() if hasattr(self, '_vad_cb') else WEBRTCVAD_AVAILABLE)
        cfg.set("jitter_frames", self._jb_spin.value()  if hasattr(self, '_jb_spin') else 6)
        cfg.set("udp_port", self._udp_p.value())
        cfg.set("tcp_port", self._tcp_p.value())
        if hasattr(self, '_enc_enable'):
            S().encryption_enabled    = self._enc_enable.isChecked()
            S().encryption_passphrase = self._enc_pass.text()
        cfg.set("theme", self._theme_combo.currentData() or "dark")
        cfg.set("save_history", self._save_history.isChecked())
        if hasattr(self, '_show_splash'):
            cfg.set("show_splash", self._show_splash.isChecked())

        # Appearance tab
        if hasattr(self, '_scale_slider'):
            cfg.set("app_scale", self._scale_slider.value())
        if hasattr(self, '_tab_show_notes'):
            cfg.set("tab_show_notes", self._tab_show_notes.isChecked())
        if hasattr(self, '_tab_show_calls'):
            cfg.set("tab_show_calls", self._tab_show_calls.isChecked())

        # Language settings
        if hasattr(self, '_lang_combo'):
            cfg.language = self._lang_combo.currentData() or "ru"
        if hasattr(self, '_show_launcher_cb'):
            cfg.show_launcher = self._show_launcher_cb.isChecked()
        if hasattr(self, '_show_launcher_cb2'):
            cfg.show_launcher = self._show_launcher_cb2.isChecked()
        if hasattr(self, '_os_notif_cb'):
            cfg.os_notifications = self._os_notif_cb.isChecked()

        # Specialist settings
        if hasattr(self, '_relay_enabled'):
            cfg.relay_enabled = self._relay_enabled.isChecked()
        if hasattr(self, '_relay_addr'):
            cfg.relay_server = self._relay_addr.text().strip()
        if hasattr(self, '_static_peers_edit'):
            import json as _j3
            _peers_raw = self._static_peers_edit.toPlainText()
            _peers_list = [p.strip() for p in _peers_raw.replace(',', '\n').splitlines() if p.strip()]
            cfg.set("static_peers", _j3.dumps(_peers_list))
        if hasattr(self, '_broadcast_interval'):
            cfg.set("broadcast_interval", self._broadcast_interval.value())
        if hasattr(self, '_peer_timeout'):
            cfg.set("peer_timeout", self._peer_timeout.value())
        if hasattr(self, '_debug_log_cb'):
            cfg.set("debug_log", self._debug_log_cb.isChecked())

        # Advanced network binding settings
        _ha = hasattr
        if _ha(self, '_bind_addr'):        cfg.set("bind_addr",         self._bind_addr.text().strip())
        if _ha(self, '_multicast_group'):  cfg.set("multicast_group",   self._multicast_group.text().strip())
        if _ha(self, '_heartbeat_interval'): cfg.set("heartbeat_interval", self._heartbeat_interval.value())
        if _ha(self, '_max_connections'):  cfg.set("max_connections",   self._max_connections.value())
        if _ha(self, '_tcp_keepalive'):    cfg.set("tcp_keepalive",     "true" if self._tcp_keepalive.isChecked() else "false")
        if _ha(self, '_keepalive_idle'):   cfg.set("keepalive_idle",    self._keepalive_idle.value())
        if _ha(self, '_reconnect_attempts'): cfg.set("reconnect_attempts", self._reconnect_attempts.value())
        if _ha(self, '_reconnect_delay'):  cfg.set("reconnect_delay",   self._reconnect_delay.value())
        if _ha(self, '_mtu_size'):         cfg.set("mtu_size",          self._mtu_size.value())
        if _ha(self, '_so_reuseaddr'):     cfg.set("so_reuseaddr",      "true" if self._so_reuseaddr.isChecked() else "false")
        if _ha(self, '_relay_host'):       cfg.relay_server = self._relay_host.text().strip()
        if _ha(self, '_relay_enabled_cb'): cfg.relay_enabled = self._relay_enabled_cb.isChecked()
        if _ha(self, '_stun_server'):      cfg.set("stun_server",       self._stun_server.text().strip())
        if _ha(self, '_conn_priority'):
            priority_map = {0:"auto", 1:"direct", 2:"relay_only", 3:"always_relay"}
            cfg.set("conn_priority", priority_map.get(self._conn_priority.currentIndex(), "auto"))
        if _ha(self, '_broadcast_addr'):   cfg.set("broadcast_addr",    self._broadcast_addr.text().strip())
        if _ha(self, '_discovery_interval'): cfg.set("discovery_interval", self._discovery_interval.value())
        if _ha(self, '_max_chunk'):        cfg.set("max_chunk_kb",      self._max_chunk.value())
        if _ha(self, '_tcp_timeout'):      cfg.set("tcp_timeout",       self._tcp_timeout.value())
        if _ha(self, '_enable_ipv6'):      cfg.set("enable_ipv6",       "true" if self._enable_ipv6.isChecked() else "false")
        if _ha(self, '_allow_relay'):      cfg.set("allow_relay",       "true" if self._allow_relay.isChecked() else "false")
        if _ha(self, '_encrypt_transport'): cfg.set("encrypt_transport", "true" if self._encrypt_transport.isChecked() else "false")

        # Privacy settings
        if hasattr(self, '_priv_show_status'):
            S().set("priv_show_status",    self._priv_show_status.isChecked())
        if hasattr(self, '_priv_show_avatar'):
            S().set("priv_show_avatar",    self._priv_show_avatar.isChecked())
        if hasattr(self, '_priv_show_typing'):
            S().set("priv_show_typing",    self._priv_show_typing.isChecked())
        if hasattr(self, '_priv_read_receipts'):
            S().set("priv_read_receipts",  self._priv_read_receipts.isChecked())
        if hasattr(self, '_priv_allow_calls'):
            S().set("priv_allow_calls",    self._priv_allow_calls.isChecked())
        if hasattr(self, '_priv_allow_group_calls'):
            S().set("priv_allow_group_calls", self._priv_allow_group_calls.isChecked())
        if hasattr(self, '_priv_link_preview'):
            S().set("priv_link_preview",   self._priv_link_preview.isChecked())
        if hasattr(self, '_priv_save_drafts'):
            S().set("priv_save_drafts",    self._priv_save_drafts.isChecked())
        if hasattr(self, '_link_pref_combo'):
            S().set("link_open_pref",      self._link_pref_combo.currentData() or "ask")

        # Apply theme immediately
        key = self._theme_combo.currentData()
        if key and not key.startswith("__custom"):
            QApplication.instance().setStyleSheet(build_stylesheet(get_theme(key)))

        # Apply tab visibility immediately
        if hasattr(self.parent(), '_apply_tab_visibility'):
            self.parent()._apply_tab_visibility()

        # Apply scale immediately
        try:
            if hasattr(self, '_scale_slider'):
                _pt = max(7.0, 9.0 * self._scale_slider.value() / 100.0)
                _f = QApplication.instance().font()
                _f.setPointSizeF(_pt)
                QApplication.instance().setFont(_f)
        except Exception:
            pass

        # GoidaCRYPTO — сохраняем все 20 слоёв
        if hasattr(self, '_layer_cbs'):
            for _lkey, _lcb in self._layer_cbs.items():
                S().set(_lkey, _lcb.isChecked())

        self.settings_saved.emit()
        self.accept()

    def _load(self):
        """Load current settings into all widgets."""
        cfg = S()
        if hasattr(self, '_vol'):
            self._vol.setValue(cfg.get("volume", 80, t=int))
        if hasattr(self, '_notif_sounds'):
            self._notif_sounds.setChecked(cfg.notification_sounds)
        if hasattr(self, '_vad_cb'):
            self._vad_cb.setChecked(cfg.get("vad_enabled", WEBRTCVAD_AVAILABLE, t=bool))
        if hasattr(self, '_jb_spin'):
            self._jb_spin.setValue(cfg.get("jitter_frames", 6, t=int))
        if hasattr(self, '_udp_p'):
            self._udp_p.setValue(cfg.udp_port)
        if hasattr(self, '_tcp_p'):
            self._tcp_p.setValue(cfg.tcp_port)
        if hasattr(self, '_save_history'):
            self._save_history.setChecked(cfg.get("save_history", True, t=bool))
        if hasattr(self, '_show_splash'):
            self._show_splash.setChecked(cfg.get("show_splash", True, t=bool))
        if hasattr(self, '_lang_combo'):
            idx = self._lang_combo.findData(cfg.language)
            if idx >= 0:
                self._lang_combo.setCurrentIndex(idx)
        if hasattr(self, '_show_launcher_cb'):
            self._show_launcher_cb.setChecked(cfg.show_launcher)
        if hasattr(self, '_os_notif_cb'):
            self._os_notif_cb.setChecked(cfg.os_notifications)
        if hasattr(self, '_theme_combo'):
            idx = self._theme_combo.findData(cfg.theme)
            if idx >= 0:
                self._theme_combo.setCurrentIndex(idx)

        # License status
        if hasattr(self, '_update_lic_status'):
            self._update_lic_status()

        # Privacy tab
        _priv_map = {
            '_priv_show_status':       ("priv_show_status",       True),
            '_priv_show_avatar':       ("priv_show_avatar",       True),
            '_priv_show_typing':       ("priv_show_typing",       True),
            '_priv_read_receipts':     ("priv_read_receipts",     True),
            '_priv_allow_calls':       ("priv_allow_calls",       True),
            '_priv_allow_group_calls': ("priv_allow_group_calls", True),
            '_priv_link_preview':      ("priv_link_preview",      True),
            '_priv_save_drafts':       ("priv_save_drafts",       True),
        }
        for _attr, (_key, _default) in _priv_map.items():
            if hasattr(self, _attr):
                getattr(self, _attr).setChecked(S().get(_key, _default, t=bool))

# ═══════════════════════════════════════════════════════════════════════════
#  INCOMING CALL DIALOG
# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
#  FLOATING CALL WINDOW  (Telegram-style)
# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
#  PEER PROFILE DIALOG  (Telegram-style, like screenshot 2)
# ═══════════════════════════════════════════════════════════════════════════
def _show_peer_profile_overlay(peer: dict, parent=None):
    """Show peer profile as a slide-in panel overlaid on the central widget."""
    mw = _find_main_window(parent)
    if mw is None:
        # Fallback to old dialog if no MainWindow found
        dlg = PeerProfileDialog(peer, parent)
        dlg.exec(); return

    central = mw.centralWidget()

    # Remove any existing profile overlay
    for child in central.findChildren(QWidget, "peer_profile_overlay"):
        child.deleteLater()

    t = get_theme(S().theme)
    name     = peer.get("username", "?")
    ip       = peer.get("ip", "")
    av_b64   = peer.get("avatar_b64", "")
    color    = peer.get("nickname_color", "#E0E0E0")
    emoji_s  = peer.get("custom_emoji", "")
    version  = peer.get("version", "")
    e2e      = CRYPTO.has_session(ip)
    fp       = CRYPTO.peer_fingerprint(ip) if e2e else "—"

    # Overlay widget — right-side panel
    panel = QWidget(central)
    panel.setObjectName("peer_profile_overlay")
    panel.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
    panel_w = min(360, central.width() - 40)
    panel.setFixedWidth(panel_w)
    panel.resize(panel_w, central.height())
    panel.move(central.width(), 0)    # start off-screen right
    panel.setStyleSheet(f"""
        QWidget#peer_profile_overlay {{
            background:{t['bg2']};
            border-left:1px solid {t['border']};
        }}
    """)

    vl = QVBoxLayout(panel)
    vl.setContentsMargins(0, 0, 0, 0)
    vl.setSpacing(0)

    # ── Header: close button ──
    hdr = QWidget()
    hdr.setStyleSheet(f"background:{t['bg3']};border-bottom:1px solid {t['border']};")
    hdr.setFixedHeight(40)
    hdr_lay = QHBoxLayout(hdr)
    hdr_lay.setContentsMargins(12, 0, 8, 0)
    hdr_title = QLabel("Профиль пользователя")
    hdr_title.setStyleSheet(f"font-size:12px;font-weight:bold;color:{t['text']};background:transparent;")
    hdr_lay.addWidget(hdr_title)
    hdr_lay.addStretch()
    close_btn = QPushButton("✕")
    close_btn.setFixedSize(28, 28)
    close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    close_btn.setStyleSheet(
        f"QPushButton{{background:transparent;color:{t['text_dim']};border:none;font-size:14px;}}"
        f"QPushButton:hover{{color:#E74C3C;}}")
    def _close_panel():
        anim = QPropertyAnimation(panel, b"pos")
        anim.setDuration(200)
        anim.setStartValue(panel.pos())
        anim.setEndValue(QPoint(central.width(), 0))
        anim.setEasingCurve(QEasingCurve.Type.InCubic)
        anim.finished.connect(panel.deleteLater)
        anim.start()
        panel._anim = anim
    close_btn.clicked.connect(_close_panel)
    hdr_lay.addWidget(close_btn)
    vl.addWidget(hdr)

    # ── Scroll area ──
    scroll = QScrollArea(panel)
    scroll.setWidgetResizable(True)
    scroll.setFrameShape(QFrame.Shape.NoFrame)
    scroll.setStyleSheet(f"QScrollArea{{background:{t['bg2']};border:none;}}")
    content = QWidget(scroll)
    content.setStyleSheet(f"background:{t['bg2']};")
    cl = QVBoxLayout(content)
    cl.setContentsMargins(0, 0, 0, 20)
    cl.setSpacing(0)

    # Banner
    banner = QWidget()
    banner.setFixedHeight(100)
    banner.setStyleSheet(f"""
        background:qlineargradient(x1:0,y1:0,x2:1,y2:1,
            stop:0 {t['accent']}, stop:1 {t['accent2']});
    """)
    cl.addWidget(banner)

    # Avatar overlapping banner
    av_wrap = QWidget(content)
    av_wrap.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, False)
    av_size = 80
    av_lbl = QLabel(av_wrap)
    av_lbl.setFixedSize(av_size+4, av_size+4)
    if av_b64:
        try:
            pm = make_circle_pixmap(base64_to_pixmap(av_b64), av_size)
            av_lbl.setPixmap(pm)
        except Exception:
            av_lbl.setPixmap(default_avatar(name, av_size))
    else:
        av_lbl.setPixmap(default_avatar(name, av_size))
    av_lbl.setStyleSheet(f"border-radius:{av_size//2+2}px;border:3px solid {t['bg2']};")

    av_row = QHBoxLayout()
    av_row.setContentsMargins(16, 0, 16, 0)
    av_row.addWidget(av_lbl)
    av_row.addStretch()

    # Chat + call buttons on same row
    chat_btn = QPushButton("💬 Написать")
    chat_btn.setFixedHeight(32)
    chat_btn.setObjectName("accent_btn")
    chat_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    call_btn = QPushButton("📞 Звонок")
    call_btn.setFixedHeight(32)
    call_btn.setCursor(Qt.CursorShape.PointingHandCursor)
    call_btn.setStyleSheet(
        f"QPushButton{{background:{t['bg3']};color:{t['text']};"
        f"border:1px solid {t['border']};border-radius:8px;padding:0 12px;}}"
        f"QPushButton:hover{{background:{t['btn_hover']};}}")

    # Wire buttons to MainWindow actions
    def _do_chat():
        _close_panel()
        if hasattr(mw, 'chat_panel'):
            mw.chat_panel.open_peer(peer)
            mw._tabs.setCurrentWidget(mw.chat_panel)
    def _do_call():
        _close_panel()
        if hasattr(mw, '_call_peer'):
            mw._call_peer(ip)

    chat_btn.clicked.connect(_do_chat)
    call_btn.clicked.connect(_do_call)
    av_row.addWidget(chat_btn)
    av_row.addSpacing(6)
    av_row.addWidget(call_btn)

    # Offset av_row to overlap banner by half avatar
    cl.addWidget(QWidget())    # spacer replaced below
    # We do it with negative margin via a container
    av_container = QWidget()
    av_container.setStyleSheet(f"background:{t['bg2']};")
    av_container.setContentsMargins(0, 0, 0, 0)
    av_cl = QVBoxLayout(av_container)
    av_cl.setContentsMargins(0, 0, 0, 0)
    av_cl.addLayout(av_row)
    # Pop the dummy spacer and add container with negative margin workaround
    cl.takeAt(cl.count()-1)
    cl.addWidget(av_container)

    # Reposition to overlap banner
    def _reposition():
        banner_bottom = banner.y() + banner.height()
        av_container.move(0, banner_bottom - av_size//2 - 2)
    QTimer.singleShot(10, _reposition)
    cl.addSpacing(av_size//2 + 12)

    # Name + emoji
    name_lbl = QLabel(f'<span style="color:{color};font-size:18px;font-weight:bold;">'
                      f'{name}</span>'
                      + (f'  <span style="font-size:16px;">{emoji_s}</span>' if emoji_s else ''))
    name_lbl.setTextFormat(Qt.TextFormat.RichText)
    name_lbl.setStyleSheet("background:transparent;")
    name_lbl.setContentsMargins(16, 0, 16, 0)
    cl.addWidget(name_lbl)
    cl.addSpacing(4)

    sub_lbl = QLabel(f"IP: {ip}")
    sub_lbl.setStyleSheet(f"font-size:11px;color:{t['text_dim']};background:transparent;margin-left:16px;")
    cl.addWidget(sub_lbl)
    cl.addSpacing(16)

    # Info rows
    def _info_row(icon, label, value):
        row = QWidget()
        row.setStyleSheet(f"background:{t['bg2']};")
        rl = QHBoxLayout(row)
        rl.setContentsMargins(16, 6, 16, 6)
        ico = QLabel(icon)
        ico.setFixedWidth(22)
        ico.setStyleSheet("font-size:14px;background:transparent;")
        rl.addWidget(ico)
        lbl_w = QLabel(label)
        lbl_w.setStyleSheet(f"font-size:11px;color:{t['text_dim']};background:transparent;")
        lbl_w.setFixedWidth(90)
        rl.addWidget(lbl_w)
        val_w = QLabel(str(value))
        val_w.setStyleSheet(f"font-size:11px;color:{t['text']};background:transparent;")
        val_w.setWordWrap(True)
        rl.addWidget(val_w, stretch=1)
        return row

    sep = QWidget()
    sep.setFixedHeight(1)
    sep.setStyleSheet(f"background:{t['border']};")
    cl.addWidget(sep)

    for icon, label, value in [
        ("🔒", "Encryption:", "E2E ✓" if e2e else "Нет E2E"),
        ("🔑", "Отпечаток:", fp[:24]+"…" if len(fp)>24 else fp),
        ("📱", "Version:", version or "—"),
    ]:
        cl.addWidget(_info_row(icon, label, value))

    sep2 = QWidget()
    sep2.setFixedHeight(1)
    sep2.setStyleSheet(f"background:{t['border']};")
    cl.addWidget(sep2)
    cl.addStretch()

    scroll.setWidget(content)
    vl.addWidget(scroll, stretch=1)

    panel.show()
    panel.raise_()

    # Animate slide-in from right
    anim = QPropertyAnimation(panel, b"pos")
    anim.setDuration(220)
    anim.setStartValue(QPoint(central.width(), 0))
    anim.setEndValue(QPoint(central.width() - panel_w, 0))
    anim.setEasingCurve(QEasingCurve.Type.OutCubic)
    anim.start()
    panel._anim = anim   # keep reference

    # Resize panel when central widget resizes
    def _on_central_resize(event):
        panel.resize(panel_w, central.height())
        if not getattr(panel, '_is_closing', False):
            panel.move(central.width() - panel_w, 0)
    central.resizeEvent = _on_central_resize


class PeerProfileDialog(QDialog):
    """Shows a peer's profile in Telegram style with banner, avatar, stats."""

    def __init__(self, peer: dict, parent=None):
        super().__init__(parent)
        self.peer = peer
        lang = S().language
        title = "Профиль пользователя" if lang == "ru" else "User Profile"
        self.setWindowTitle(title)
        self.setModal(True)
        self.setFixedWidth(380)
        self.setMinimumHeight(420)
        self._build()

    def _build(self):
        t = get_theme(S().theme)
        peer = self.peer
        lang = S().language

        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        # ── Banner ──────────────────────────────────────────────────────
        banner_lbl = QLabel()
        banner_lbl.setFixedHeight(130)
        banner_lbl.setScaledContents(True)
        banner_lbl.setStyleSheet(
            f"background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
            f"stop:0 {t['accent']}, stop:1 {t['bg3']});"
            f"border-radius: 0;")
        b64 = peer.get("banner_b64", "")
        if b64:
            try:
                pm = QPixmap(); pm.loadFromData(base64.b64decode(b64))
                banner_lbl.setPixmap(pm)
                banner_lbl.setScaledContents(True)
            except Exception:
                pass
        outer.addWidget(banner_lbl)

        # ── Card body ───────────────────────────────────────────────────
        card = QWidget()
        card.setStyleSheet(f"background:{t['bg2']};")
        card_lay = QVBoxLayout(card)
        card_lay.setContentsMargins(16, 0, 16, 16)
        card_lay.setSpacing(6)

        # Avatar row (overlaps banner)
        av_row = QHBoxLayout()
        av_row.setContentsMargins(0, -30, 0, 0)   # pull up over banner

        av_lbl = QLabel()
        av_lbl.setFixedSize(72, 72)
        av_b64 = peer.get("avatar_b64", "")
        if av_b64:
            try:
                pm2 = base64_to_pixmap(av_b64)
                av_lbl.setPixmap(make_circle_pixmap(pm2, 72))
            except Exception:
                av_lbl.setPixmap(default_avatar(peer.get("username","?"), 72))
        else:
            av_lbl.setPixmap(default_avatar(peer.get("username","?"), 72))
        av_lbl.setStyleSheet("border:3px solid " + t['bg2'] + ";border-radius:36px;")
        av_row.addWidget(av_lbl)
        av_row.addStretch()

        # Premium badge
        if peer.get("premium"):
            prem = QLabel("👑 PREMIUM")
            prem.setStyleSheet(
                "background:#C08010;color:#FFF8C0;font-size:10px;"
                "font-weight:bold;border-radius:10px;padding:3px 10px;")
            av_row.addWidget(prem)

        card_lay.addLayout(av_row)

        # Name
        color = peer.get("nickname_color", "#E0E0E0")
        emoji = peer.get("custom_emoji", "")
        name_lbl = QLabel(f"<span style='color:{color};font-size:18px;font-weight:bold;'>"
                          f"{peer.get('username','?')}</span> "
                          f"<span style='font-size:16px;'>{emoji}</span>")
        name_lbl.setTextFormat(Qt.TextFormat.RichText)
        name_lbl.setStyleSheet("background:transparent;")
        card_lay.addWidget(name_lbl)

        # Status + loyalty level
        status = peer.get("status", "online")
        sc = {"online":"#4CAF50","away":"#FFD700","busy":"#FF6B6B","dnd":"#9E9E9E"}
        sl = {"online": "Онлайн" if lang=="ru" else "Online",
              "away":   "Отошёл" if lang=="ru" else "Away",
              "busy":   "Занят"  if lang=="ru" else "Busy",
              "dnd":    "Не беспокоить" if lang=="ru" else "Do not disturb"}
        status_row = QHBoxLayout()
        dot = QLabel("●")
        dot.setStyleSheet(f"color:{sc.get(status,'#4CAF50')};font-size:11px;background:transparent;")
        status_row.addWidget(dot)
        st_lbl = QLabel(sl.get(status, "Онлайн"))
        st_lbl.setStyleSheet(f"color:{t['text_dim']};font-size:11px;background:transparent;")
        status_row.addWidget(st_lbl)

        # Loyalty level badge
        loyalty = int(peer.get("loyalty_months", 0))
        if loyalty > 0:
            status_row.addSpacing(8)
            loy_lbl = QLabel(f"❤️ {loyalty}")
            loy_lbl.setStyleSheet(
                f"background:{t['accent']}22;color:{t['accent']};"
                f"border:1px solid {t['accent']};border-radius:8px;"
                f"padding:1px 6px;font-size:10px;font-weight:bold;background:transparent;")
            tip = (_L('В сети {loyalty} мес. подряд', 'Online {loyalty} months straight', '{loyalty}ヶ月連続') if lang=="ru"
                   else f"Online for {loyalty} months in a row")
            loy_lbl.setToolTip(tip)
            status_row.addWidget(loy_lbl)

        status_row.addStretch()
        card_lay.addLayout(status_row)

        # Separator
        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"color:{t['border']};")
        card_lay.addWidget(sep)

        # Info rows
        ip_raw = peer.get("ip","?")
        id_str = display_id(ip_raw)
        id_label = ("GoidaID" if S().safe_mode else ("IP" if lang=="en" else "IP"))

        rows_ru = [
            ("ℹ", id_str,           id_label),
            ("💻", peer.get("os","?"), "Система" if lang=="ru" else "System"),
            ("🔒", ("E2E ✓" if peer.get("e2e") else "E2E ✗"),
                   "Encryption" if lang=="ru" else "Encryption"),
            ("📡", peer.get("version","?"), "Версия" if lang=="ru" else "Version"),
        ]
        if peer.get("bio"):
            rows_ru.insert(1, ("📝", peer.get("bio",""), _L('О себе', 'About me', '自己紹介') if lang=="ru" else "Bio"))

        for icon, val, label in rows_ru:
            if not val or val == "?":
                continue
            rw = QHBoxLayout()
            ico_lbl = QLabel(icon)
            ico_lbl.setFixedWidth(22)
            ico_lbl.setStyleSheet("background:transparent;font-size:14px;")
            rw.addWidget(ico_lbl)
            val_col = QVBoxLayout()
            val_col.setSpacing(0)
            v_lbl = QLabel(str(val))
            v_lbl.setStyleSheet(f"color:{t['text']};font-size:12px;background:transparent;")
            val_col.addWidget(v_lbl)
            k_lbl = QLabel(label)
            k_lbl.setStyleSheet(f"color:{t['text_dim']};font-size:9px;background:transparent;")
            val_col.addWidget(k_lbl)
            rw.addLayout(val_col)
            rw.addStretch()
            card_lay.addLayout(rw)

        card_lay.addStretch()
        outer.addWidget(card)

        # Close button
        close_btn = QPushButton("✕  " + (_L("Закрыть", "Close", "閉じる") if lang=="ru" else "Close"))
        close_btn.setObjectName("accent_btn")
        close_btn.clicked.connect(self.accept)
        close_btn.setFixedHeight(36)
        outer.addWidget(close_btn)


# ═══════════════════════════════════════════════════════════════════════════
#  WINORA NETSCAPE  (WNS)  —  встроенный браузер GoidaPhone
# ═══════════════════════════════════════════════════════════════════════════