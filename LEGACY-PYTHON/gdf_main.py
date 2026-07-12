#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Main Window + Entry Point
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings, _get_sound_dirs       # Qt6 + gdf_core (TR, _L, S, themes...)
from gdf_network  import *      # NetworkManager, VoiceCallManager, AudioEngine
from gdf_ui_base  import *      # SplashScreen, LauncherScreen, ImageViewer
from gdf_chat     import *      # ChatPanel, PeerPanel, MessageBubble
from gdf_dialogs  import *      # SettingsDialog, ProfileDialog, call windows
from gdf_browser  import *      # WinoraNetScape, OutgoingCallWindow, IncomingCallDialog
from gdf_apps     import *      # GoidaTerminal, MewaPlayer, _BarVisualizer

# ═══════════════════════════════════════════════════════════════════════════
#  MAIN WINDOW
# ═══════════════════════════════════════════════════════════════════════════

# ═══════════════════════════════════════════════════════════════════════════
#  QUICK SETUP WIZARD
# ═══════════════════════════════════════════════════════════════════════════
class QuickSetupWizard(QDialog):
    """
    Быстрая настройка GoidaPhone — серия простых вопросов.
    Вызывается автоматически после обучения или через Справка → Быстрая настройка.
    """
    done_signal = pyqtSignal()

    # (ключ, вопрос, подсказка, тип, варианты/placeholder, дефолт)
    QUESTIONS = [
        ("username",        "What's your name?",
         "This name will be visible to everyone",
         "text", "Example: pixless", ""),
        ("nickname_color",  "Choose nick color",
         "Your color in user list and chat",
         "color", None, "#E0E0E0"),
        ("theme",           "Choose theme",
         "Can be changed later in Settings → Themes",
         "choice", [
             ("🌑 Dark", "dark"),
             ("☀️ Light", "light"),
             ("🌊 Ocean", "ocean"),
             ("🌌 Aurora", "aurora"),
             ("⚡ Neon", "neon"),
             ("🌸 Sakura", "sakura"),
             ("🌅 Sunset", "sunset"),
             ("🌲 Forest", "forest"),
         ], "dark"),
        ("notification_sounds", "Enable notification sounds?",
         "Sound for new messages, calls and events",
         "yesno", None, True),
        ("save_history",    "Save message history?",
         "History is stored locally on your computer",
         "yesno", None, True),
        ("show_splash",     "Show splash screen on startup?",
         "Экран загрузки с логотипом GoidaPhone",
         "yesno", None, True),
        ("_summary",        "Всё ready!",
         "Settings applied. We recommend restarting GoidaPhone for best results.",
         "summary", None, None),
    ]

    @staticmethod
    def offer(parent):
        """Предложить быструю настройку с диалогом."""
        t = get_theme(S().theme)
        dlg = QDialog(parent)
        dlg.setWindowTitle(_L("Быстрая настройка", "Quick Setup", "クイック設定"))
        dlg.setFixedSize(420, 240)
        dlg.setStyleSheet(
            f"QDialog{{background:{t['bg2']};border-radius:16px;}}"
            f"QLabel{{background:transparent;color:{t['text']};}}")
        lay = QVBoxLayout(dlg)
        lay.setContentsMargins(32, 28, 32, 24)
        lay.setSpacing(14)

        ico = QLabel("⚡")
        ico.setStyleSheet("font-size:36px;background:transparent;")
        ico.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(ico)

        title = QLabel(_L("Хочешь пройти быструю настройку?", "Want to do a quick setup?", "クイック設定を行いますか?"))
        title.setStyleSheet(
            f"font-size:14px;font-weight:700;color:{t['text']};background:transparent;")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(title)

        sub = QLabel("Займёт ~1 минуту. Настроим имя, тему и звуки. Всё можно изменить позже в Настройках.")
        sub.setStyleSheet(
            f"font-size:10pt;color:{t['text_dim']};background:transparent;")
        sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
        sub.setWordWrap(True)
        lay.addWidget(sub)

        btn_row = QHBoxLayout()
        skip = QPushButton(TR("btn_later"))
        skip.setStyleSheet(
            f"QPushButton{{background:transparent;color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:8px;padding:6px 18px;}}"
            f"QPushButton:hover{{border-color:{t['text']};}}")
        skip.clicked.connect(dlg.reject)
        btn_row.addWidget(skip)
        btn_row.addStretch()

        go = QPushButton(_L("Начать →", "Start →", "開始 →"))
        go.setStyleSheet(
            f"QPushButton{{background:{t['accent']};color:white;"
            "border-radius:8px;border:none;padding:6px 24px;font-weight:600;}}"
            f"QPushButton:hover{{background:{t['accent2']};}}")
        go.clicked.connect(dlg.accept)
        btn_row.addWidget(go)
        lay.addLayout(btn_row)

        if dlg.exec() == QDialog.DialogCode.Accepted:
            wiz = QuickSetupWizard(parent)
            wiz.exec()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle(_L("Быстрая настройка GoidaPhone",
                               "GoidaPhone Quick Setup", "クイック設定"))
        self.setModal(False)   # inline в вкладке — не модальный
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self._t = get_theme(S().theme)
        self.setStyleSheet(
            f"QDialog{{background:{self._t['bg2']};}}"
            f"QLabel{{background:transparent;color:{self._t['text']};}}"
            f"QLineEdit{{background:{self._t['bg3']};color:{self._t['text']};"
            f"border:1px solid {self._t['border']};border-radius:8px;padding:6px 10px;}}"
            f"QCheckBox{{color:{self._t['text']};background:transparent;}}"
        )
        self._answers = {}
        self._q_idx   = 0
        self._active_questions = [q for q in self.QUESTIONS if q[0] != "_summary"]
        self._build()
        self._show_question(0)

    def _build(self):
        self._main_lay = QVBoxLayout(self)
        self._main_lay.setContentsMargins(0, 0, 0, 0)
        self._main_lay.setSpacing(0)

        t = self._t

        # ── Прогресс-шапка ───────────────────────────────────────────────
        hdr = QWidget()
        hdr.setStyleSheet(f"background:{t['bg3']};")
        hdr.setFixedHeight(54)
        hl = QHBoxLayout(hdr)
        hl.setContentsMargins(24, 0, 24, 0)

        self._prog_lbl = QLabel()
        self._prog_lbl.setStyleSheet(
            f"font-size:9pt;color:{t['text_dim']};font-weight:500;")
        hl.addWidget(self._prog_lbl)
        hl.addStretch()

        self._prog_bar = QProgressBar()
        self._prog_bar.setFixedWidth(140)
        self._prog_bar.setFixedHeight(5)
        self._prog_bar.setTextVisible(False)
        self._prog_bar.setStyleSheet(
            f"QProgressBar{{background:{t['border']};border-radius:3px;border:none;}}"
            f"QProgressBar::chunk{{background:{t['accent']};border-radius:3px;}}")
        hl.addWidget(self._prog_bar)
        self._main_lay.addWidget(hdr)

        # ── Контент ───────────────────────────────────────────────────────
        self._content = QWidget()
        self._content.setStyleSheet(f"background:{t['bg2']};")
        cl = QVBoxLayout(self._content)
        cl.setContentsMargins(40, 32, 40, 20)
        cl.setSpacing(12)

        self._q_title = QLabel()
        self._q_title.setStyleSheet(
            f"font-size:18px;font-weight:700;color:{t['text']};")
        self._q_title.setWordWrap(True)
        cl.addWidget(self._q_title)

        self._q_hint = QLabel()
        self._q_hint.setStyleSheet(
            f"font-size:10pt;color:{t['text_dim']};")
        self._q_hint.setWordWrap(True)
        cl.addWidget(self._q_hint)

        # Область для виджета ответа
        self._answer_area = QWidget()
        self._answer_area.setStyleSheet("background:transparent;")
        self._answer_lay = QVBoxLayout(self._answer_area)
        self._answer_lay.setContentsMargins(0, 8, 0, 0)
        self._answer_lay.setSpacing(8)
        cl.addWidget(self._answer_area)
        cl.addStretch()

        self._main_lay.addWidget(self._content, 1)

        # ── Навигация ─────────────────────────────────────────────────────
        nav = QWidget()
        nav.setStyleSheet(
            f"background:{t['bg3']};border-top:1px solid {t['border']};")
        nav.setFixedHeight(60)
        nl = QHBoxLayout(nav)
        nl.setContentsMargins(24, 0, 24, 0)

        self._back_btn = QPushButton("← Back")
        self._back_btn.setStyleSheet(
            f"QPushButton{{background:transparent;color:{t['text_dim']};"
            f"border:none;font-size:10pt;padding:6px 12px;}}"
            f"QPushButton:hover{{color:{t['text']};}}")
        self._back_btn.clicked.connect(self._prev)
        self._back_btn.setVisible(False)
        nl.addWidget(self._back_btn)
        nl.addStretch()

        self._next_btn = QPushButton("Next →")
        self._next_btn.setFixedSize(120, 36)
        self._next_btn.setStyleSheet(
            f"QPushButton{{background:{t['accent']};color:white;"
            "border-radius:10px;border:none;font-size:10pt;font-weight:600;}}"
            f"QPushButton:hover{{background:{t['accent2']};}}")
        self._next_btn.clicked.connect(self._next)
        nl.addWidget(self._next_btn)

        self._main_lay.addWidget(nav)
        self._current_widget = None

    def _clear_answer_area(self):
        def _clear_layout(layout):
            while layout.count():
                item = layout.takeAt(0)
                if item.widget():
                    item.widget().deleteLater()
                elif item.layout():
                    _clear_layout(item.layout())
                    item.layout().deleteLater()
        _clear_layout(self._answer_lay)
        self._current_widget = None
        # Форсируем перерисовку чтобы убрать артефакты
        self._answer_area.update()

    def _show_question(self, idx: int):
        t = self._t
        questions = self._active_questions
        total     = len(questions)

        if idx >= total:
            self._show_summary()
            return

        self._q_idx = idx
        key, question, hint, qtype, opts, default = questions[idx]

        # Progress
        self._prog_lbl.setText(f"{_L('Шаг','Step','ステップ')} {idx+1} {_L('из','of','/')} {total}")
        self._prog_bar.setMaximum(total)
        self._prog_bar.setValue(idx + 1)
        self._back_btn.setVisible(idx > 0)
        last = idx == total - 1
        self._next_btn.setText(_L('Готово ✓','Done ✓','完了 ✓') if last else _L('Далее →','Next →','次へ →'))

        self._q_title.setText(question)
        self._q_hint.setText(hint)
        self._clear_answer_area()

        # Текущее сохранённое значение
        cur = self._answers.get(key, S().get(key, default) if key not in
                                ("notification_sounds","save_history","show_splash")
                                else S().get(key, default, t=bool))

        if qtype == "text":
            w = QLineEdit()
            w.setPlaceholderText(opts or "")
            w.setText(str(cur) if cur else "")
            w.setFixedHeight(40)
            self._answer_lay.addWidget(w)
            self._current_widget = ("text", key, w)

        elif qtype == "color":
            row = QHBoxLayout()
            self._color_val = cur or "#E0E0E0"
            preview = QLabel()
            preview.setFixedSize(40, 40)
            preview.setStyleSheet(
                f"background:{self._color_val};border-radius:8px;"
                f"border:2px solid {t['border']};")
            row.addWidget(preview)
            pick = QPushButton("Выбрать цвет…")
            pick.setFixedHeight(40)
            pick.setStyleSheet(
                f"QPushButton{{background:{t['btn_bg']};color:{t['text']};"
                "border-radius:8px;border:none;padding:0 16px;}}"
                f"QPushButton:hover{{background:{t['btn_hover']};}}")
            def _pick_color(_p=preview):
                from PyQt6.QtWidgets import QColorDialog
                col = QColorDialog.getColor(
                    __import__('PyQt6.QtGui', fromlist=['QColor']).QColor(self._color_val),
                    self, "Выбрать цвет ника")
                if col.isValid():
                    self._color_val = col.name()
                    _p.setStyleSheet(
                        f"background:{self._color_val};border-radius:8px;"
                        f"border:2px solid {t['border']};")
            pick.clicked.connect(_pick_color)
            row.addWidget(pick, 1)
            self._answer_lay.addLayout(row)
            self._current_widget = ("color", key, None)

        elif qtype == "choice":
            # Горизонтальная сетка карточек
            grid = QWidget(); grid.setStyleSheet("background:transparent;")
            gl = __import__('PyQt6.QtWidgets', fromlist=['QGridLayout']).QGridLayout(grid)
            gl.setSpacing(8); gl.setContentsMargins(0,0,0,0)
            self._choice_val = cur
            self._choice_btns = []
            cols = 4
            for i, (label, val) in enumerate(opts):
                btn = QPushButton(label)
                btn.setCheckable(True)
                btn.setChecked(val == cur)
                btn.setFixedHeight(44)
                active_style = (
                    f"QPushButton{{background:{t['accent']};color:white;"
                    f"border-radius:8px;border:none;font-size:9pt;font-weight:600;}}")
                inactive_style = (
                    f"QPushButton{{background:{t['bg3']};color:{t['text']};"
                    f"border-radius:8px;border:1px solid {t['border']};font-size:9pt;}}"
                    f"QPushButton:hover{{border-color:{t['accent']};}}")
                btn.setStyleSheet(active_style if val == cur else inactive_style)
                def _sel(checked, _val=val, _label=label, _as=active_style, _is=inactive_style):
                    self._choice_val = _val
                    for b2, v2 in self._choice_btns:
                        b2.setChecked(v2 == _val)
                        b2.setStyleSheet(_as if v2 == _val else _is)
                btn.clicked.connect(_sel)
                self._choice_btns.append((btn, val))
                gl.addWidget(btn, i // cols, i % cols)
            self._answer_lay.addWidget(grid)
            self._current_widget = ("choice", key, None)

        elif qtype == "yesno":
            row2 = QHBoxLayout()
            self._yesno_val = bool(cur)
            yes_btn = QPushButton("✓ Да")
            no_btn  = QPushButton("✕ Нет")
            for btn, val, label in [(yes_btn, True, "Да"), (no_btn, False, "Нет")]:
                btn.setFixedSize(100, 44)
                is_sel = (bool(cur) == val)
                btn.setStyleSheet(
                    f"QPushButton{{background:{'#1a3a1a' if val else t['bg3']};"
                    f"color:{'#27AE60' if val else t['text_dim']};"
                    f"border-radius:10px;border:2px solid "
                    f"{'#27AE60' if (is_sel and val) else '#C0392B' if (is_sel and not val) else t['border']};"
                    "font-size:12pt;font-weight:700;}}"
                    f"QPushButton:hover{{border-color:{t['accent']};}}")
                btn.setCheckable(True)
                btn.setChecked(is_sel)
            def _yes():
                self._yesno_val = True
                yes_btn.setStyleSheet(
                    f"QPushButton{{background:#1a3a1a;color:#27AE60;"
                    "border-radius:10px;border:2px solid #27AE60;font-size:12pt;font-weight:700;}}")
                no_btn.setStyleSheet(
                    f"QPushButton{{background:{t['bg3']};color:{t['text_dim']};"
                    f"border-radius:10px;border:2px solid {t['border']};font-size:12pt;font-weight:700;}}")
            def _no():
                self._yesno_val = False
                no_btn.setStyleSheet(
                    f"QPushButton{{background:#3a1a1a;color:#E74C3C;"
                    "border-radius:10px;border:2px solid #E74C3C;font-size:12pt;font-weight:700;}}")
                yes_btn.setStyleSheet(
                    f"QPushButton{{background:{t['bg3']};color:{t['text_dim']};"
                    f"border-radius:10px;border:2px solid {t['border']};font-size:12pt;font-weight:700;}}")
            yes_btn.clicked.connect(_yes)
            no_btn.clicked.connect(_no)
            row2.addWidget(yes_btn)
            row2.addWidget(no_btn)
            row2.addStretch()
            self._answer_lay.addLayout(row2)
            self._current_widget = ("yesno", key, None)

    def _collect_answer(self):
        """Сохранить текущий ответ."""
        if not self._current_widget:
            return
        kind, key, widget = self._current_widget
        if kind == "text":
            self._answers[key] = widget.text().strip()
        elif kind == "color":
            self._answers[key] = self._color_val
        elif kind == "choice":
            self._answers[key] = self._choice_val
        elif kind == "yesno":
            self._answers[key] = self._yesno_val

    def _next(self):
        self._collect_answer()
        self._show_question(self._q_idx + 1)

    def _prev(self):
        self._collect_answer()
        self._show_question(self._q_idx - 1)

    def _show_summary(self):
        """Итоговый экран с обратным отсчётом и кнопкой перезапуска."""
        t = self._t
        self._prog_lbl.setText("Применяем настройки…")
        self._prog_bar.setValue(self._prog_bar.maximum())
        self._back_btn.setVisible(False)
        self._next_btn.setVisible(False)

        # Применяем все настройки
        self._apply_all()

        self._q_title.setText("✅ Настройка завершена!")
        self._q_hint.setText(
            "Все настройки применены. Для корректной работы рекомендуем перезапустить GoidaPhone.")
        self._clear_answer_area()

        # Список применённых настроек
        summary_lbl = QLabel()
        lines = []
        label_map = {
            "username": "Имя",
            "nickname_color": "Цвет ника",
            "theme": "Тема",
            "notification_sounds": "Звуки",
            "save_history": "History",
            "show_splash": "Splash screen",
        }
        for key, val in self._answers.items():
            lbl = label_map.get(key, key)
            if isinstance(val, bool):
                val = "Вкл" if val else "Выкл"
            lines.append(f"  • {lbl}: {val}")
        summary_lbl.setText("\n".join(lines))
        summary_lbl.setStyleSheet(
            f"font-size:9pt;color:{t['text_dim']};background:transparent;font-family:monospace;")
        self._answer_lay.addWidget(summary_lbl)

        # Обратный отсчёт
        self._countdown = 15
        cdown_row = QHBoxLayout()

        restart_later = QPushButton("Перезапустить позже")
        restart_later.setStyleSheet(
            f"QPushButton{{background:transparent;color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:8px;padding:6px 14px;}}"
            f"QPushButton:hover{{border-color:{t['text']};}}")
        restart_later.clicked.connect(self._close_no_restart)
        cdown_row.addWidget(restart_later)
        cdown_row.addStretch()

        self._restart_btn = QPushButton(f"Перезапустить ({self._countdown}с)")
        self._restart_btn.setFixedHeight(36)
        self._restart_btn.setStyleSheet(
            f"QPushButton{{background:{t['accent']};color:white;"
            "border-radius:8px;border:none;padding:6px 18px;font-weight:600;}}"
            f"QPushButton:hover{{background:{t['accent2']};}}")
        self._restart_btn.clicked.connect(self._do_restart)
        cdown_row.addWidget(self._restart_btn)
        self._answer_lay.addLayout(cdown_row)

        note = QLabel("💡 Всё можно изменить позже в Настройках")
        note.setStyleSheet(
            f"font-size:8pt;color:{t['text_dim']};background:transparent;")
        self._answer_lay.addWidget(note)

        # Таймер обратного отсчёта
        self._timer_cd = QTimer(self)
        self._timer_cd.timeout.connect(self._tick_countdown)
        self._timer_cd.start(1000)

    def _tick_countdown(self):
        self._countdown -= 1
        if self._countdown <= 0:
            self._timer_cd.stop()
            self._do_restart()
        else:
            self._restart_btn.setText(f"Перезапустить ({self._countdown}с)")

    def _apply_all(self):
        """Применить все собранные ответы."""
        cfg = S()
        for key, val in self._answers.items():
            if key == "username":
                if val: cfg.username = val
            elif key == "nickname_color":
                cfg.nickname_color = val
            elif key == "theme":
                cfg.set("theme", val)
                try:
                    QApplication.instance().setStyleSheet(
                        build_stylesheet(get_theme(val)))
                except Exception:
                    pass
            elif key == "notification_sounds":
                cfg.set("notification_sounds", val)
            elif key == "save_history":
                cfg.set("save_history", val)
            elif key == "show_splash":
                cfg.set("show_splash", val)
        cfg.set("quicksetup_done", True)

    def _do_restart(self):
        self._timer_cd.stop() if hasattr(self, '_timer_cd') else None
        self.accept()
        import sys, os
        os.execv(sys.executable, [sys.executable] + sys.argv)

    def _close_no_restart(self):
        if hasattr(self, '_timer_cd'):
            self._timer_cd.stop()
        self.accept()


# ═══════════════════════════════════════════════════════════════════════════
#  TUTORIAL OVERLAY  (step-by-step with arrows)
# ═══════════════════════════════════════════════════════════════════════════
class TutorialOverlay(QWidget):
    """
    Full-window transparent overlay that dims everything except the
    target widget and shows a tooltip bubble with an arrow.
    """
    finished = pyqtSignal()

    STEPS_RU = [
        ("👋 Welcome to GoidaPhone!",
         "GoidaPhone — мессенджер для общения в локальной сети (LAN) и через VPN.\n\n"
         "Здесь no серверов — всё P2P. Твои сообщения не покидают сеть.\n"
         "Press → to start the tutorial.",
         None),
        ("🌐 How it works",
         "GoidaPhone находит других пользователей автоматически через broadcast.\n\n"
         "• Одна сеть (Wi-Fi/LAN) → все видят друг друга сразу\n"
         "• Разные сети → нужен VPN (Radmin, Hamachi, ZeroTier)\n"
         "• Нет интерnoа → всё равно работает внутри LAN",
         None),
        ("👤 Список пользователей",
         "Слева — все online-пользователи в твоей сети.\n\n"
         "• Двойной клик → открыть личный чат\n"
         "• Правый клик → позвонить, посмотреть профиль, закрепить\n"
         "• Зелёный кружок — online, серый — недавно был в сети",
         "peer_list"),
        ("💬 Публичный чат",
         "Вкладка «Чат» — общий канал для всех в сети.\n\n"
         "• Enter → отправить сообщение\n"
         "• Shift+Enter → новая строка\n"
         "• Перетащи файл или картинку → прикрепить\n"
         "• Ctrl+V → вставить скриншот из буфера\n"
         "• / → список команд",
         "input_area"),
        ("📨 Личные сообщения",
         "Нажми на пользователя в списке → откроется личный чат.\n\n"
         "• Сообщения видны только вам двоим\n"
         "• История сохраняется локально\n"
         "• Правый клик на сообщение → ответить, переслать, удалить",
         "peer_list"),
        ("📞 Голосовые звонки",
         "Кнопка 📞 в шапке чата → голосовой звонок.\n\n"
         "• Звонок в отдельном окне с аватаром собеседника\n"
         "• Зелёная рамка мигает когда собеседник говорит\n"
         "• Кнопка 🎤 → отключить микрофон\n"
         "• Можно демонстрировать экран (кнопка 🖥)",
         "call_btn"),
        ("👥 Группы",
         "Вкладка «Группы» — создавай чаты для нескольких человек.\n\n"
         "• Правый клик на пользователя → пригласить в группу\n"
         "• Групповые voice calls с несколькими участниками\n"
         "• Иконка, описание, права участников",
         "groups_tab"),
        ("🎵 Mewa — музыкальный плеер",
         "Вкладка «Mewa» → плеер с плейлистом, эквалайзером и online-радио.\n\n"
         "• Добавляй файлы или папки\n"
         "• 10-полосный эквалайзер с пресетами\n"
         "• Онлайн-радио — список станций встроен\n"
         "• Текст песни — автоматический поиск по имени",
         None),
        ("🌐 WNS — встроенный браузер",
         "Вкладка «WNS» → браузер на движке Chromium.\n\n"
         "• Вкладки, закладки, история, инкогнито\n"
         "• Открывает ссылки из чата без переключения окон\n"
         "• Ctrl+T → новая вкладка, Ctrl+W → закрыть",
         None),
        ("🔐 Безопасность",
         "GoidaPhone поддерживает несколько уровней защиты.\n\n"
         "• Encryption сообщений: Настройки → Сеть → включить\n"
         "• PIN-блокировка: Настройки → Блокировка\n"
         "• GoidaCRYPTO SecureVault: Настройки → Приватность\n"
         "• Все данные хранятся локально, без облака",
         None),
        ("⚙️ Настройки",
         "Файл → Настройки (или иконка шестерёнки) → полный контроль.\n\n"
         "• Аудио: устройства, звуки, эквалайзер\n"
         "• Темы: 13 встроенных + 3 слота своих\n"
         "• Внешний вид: масштаб, иконка, сплеш\n"
         "• Звуковая схема: свой звук для каждого события",
         None),
        ("🎉 Готово!",
         "Ты прошёл полное обучение GoidaPhone!\n\n"
         "Дальше предлагаем пройти быструю настройку — это займёт 1 минуту\n"
         "и поможет сразу настроить самое важное под тебя.\n\n"
         "Всё можно изменить позже в Настройках.",
         None),
    ]

    STEPS_EN = [
        ("Welcome to GoidaPhone!",
         "This is a LAN/VPN messenger for local network communication.\n"
         "Press → to continue the tutorial.",
         None),
        ("User List",
         "All online users in your network are shown here.\n"
         "Double-click to open a private chat. Right-click for actions.",
         "peer_list"),
        ("Chat Tabs",
         "Tabs at the top: Chat (public), Notes, Calls, Mewa player.\n"
         "The Chat tab is a public channel for everyone on the network.",
         "chat_tabs"),
        ("Message Input",
         "Type a message and press Enter or the ▶ button to send.\n"
         "Type / to see the list of commands.",
         "input_area"),
        ("Voice Calls",
         "The 📞 button in the chat header starts a voice call.\n"
         "The call opens in a separate floating window.",
         "call_btn"),
        ("Groups",
         "The Groups tab on the left — create and manage groups.\n"
         "Invite others via the right-click context menu.",
         "groups_tab"),
        ("Profile",
         "File → My Profile (Ctrl+P) — set your avatar, name, bio.\n"
         "Preview shows how others see you.",
         None),
        ("ZLink Terminal",
         "Help → ZLink Terminal — powerful admin terminal.\n"
         "Type /help to see all commands.",
         None),
        ("All done!",
         "You've completed the GoidaPhone tutorial! 🎉\n"
         "If you have questions, check Help → About.",
         None),
    ]

    def __init__(self, main_window, parent=None):
        super().__init__(parent or main_window)
        self._mw = main_window
        self._step = 0
        lang = S().language
        self._steps = (self.STEPS_EN if lang == "en"
                       else self.STEPS_RU)  # ja fallback to ru for now
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setWindowFlags(Qt.WindowType.FramelessWindowHint |
                            Qt.WindowType.WindowStaysOnTopHint)
        self.resize(main_window.size())
        self._build_bubble()
        self._show_step(0)

    def _build_bubble(self):
        t = get_theme(S().theme)
        self._bubble = QWidget(self)
        self._bubble.setObjectName("tut_bubble")
        self._bubble.setStyleSheet(f"""
            QWidget#tut_bubble {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['bg2']}, stop:1 {t['bg3']});
                border: 2px solid {t['accent']};
                border-radius: 14px;
            }}
        """)
        self._bubble.setMinimumWidth(380)
        self._bubble.setMaximumWidth(500)
        bl = QVBoxLayout(self._bubble)
        bl.setContentsMargins(20, 16, 20, 16)
        bl.setSpacing(10)

        # Step indicator
        self._step_lbl = QLabel()
        self._step_lbl.setStyleSheet(
            f"color:{t['accent']};font-size:9px;font-weight:bold;background:transparent;")
        bl.addWidget(self._step_lbl)

        self._title_lbl = QLabel()
        self._title_lbl.setStyleSheet(
            f"color:{t['text']};font-size:14px;font-weight:bold;background:transparent;")
        self._title_lbl.setWordWrap(True)
        self._title_lbl.setSizePolicy(
            QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Minimum)
        bl.addWidget(self._title_lbl)

        self._body_lbl = QLabel()
        self._body_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:11px;background:transparent;")
        self._body_lbl.setWordWrap(True)
        self._body_lbl.setSizePolicy(
            QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Minimum)
        self._body_lbl.setMinimumHeight(60)
        bl.addWidget(self._body_lbl)

        btn_row = QHBoxLayout()
        lang = S().language
        self._skip_btn = QPushButton("✕ " + ("Skip" if lang=="ru" else "Skip"))
        self._skip_btn.setStyleSheet(
            f"QPushButton{{background:transparent;color:{t['text_dim']};"
            "border:none;font-size:11px;}"
            f"QPushButton:hover{{color:{t['text']};}}")
        self._skip_btn.clicked.connect(self._finish)
        btn_row.addWidget(self._skip_btn)
        btn_row.addStretch()
        self._next_btn = QPushButton(("Next →" if lang=="ru" else "Next →"))
        self._next_btn.setObjectName("accent_btn")
        self._next_btn.setFixedHeight(32)
        self._next_btn.clicked.connect(self._next)
        btn_row.addWidget(self._next_btn)
        bl.addLayout(btn_row)

    def _show_step(self, idx: int):
        if idx >= len(self._steps):
            self._finish(); return
        self._step = idx
        title, body, widget_key = self._steps[idx]
        lang = S().language
        total = len(self._steps)
        self._step_lbl.setText(f"{'Шаг' if lang=='ru' else 'Step'} {idx+1} / {total}")
        self._title_lbl.setText(title)
        self._body_lbl.setText(body)
        last = idx == total - 1
        self._next_btn.setText((TR("tutorial_finish") if lang=="ru" else "Finish ✓") if last
                                else ("Next →" if lang=="ru" else "Next →"))

        # Find target widget for arrow highlight
        self._target_rect = None
        if widget_key:
            try:
                target = self._mw.findChild(QWidget, widget_key)
                if target and target.isVisible():
                    tr = target.rect()
                    tl = target.mapTo(self._mw, tr.topLeft())
                    self._target_rect = QRect(tl, tr.size())
            except Exception:
                pass

        # Даём Qt пересчитать размер после установки текста
        self._bubble.setMinimumHeight(0)
        self._bubble.adjustSize()
        # Position bubble: if we have a target, position near it; else center
        bw = self._bubble.width(); bh = self._bubble.height()
        if self._target_rect:
            tr = self._target_rect
            # Try to place bubble below target, or above if too low
            bx = max(8, min(tr.left(), self.width() - bw - 8))
            if tr.bottom() + bh + 20 < self.height():
                by = tr.bottom() + 16
            else:
                by = max(8, tr.top() - bh - 16)
            self._bubble.move(bx, by)
        else:
            self._bubble.move(
                (self.width() - bw) // 2,
                (self.height() - bh) // 2)
        self.update()

    def _draw_arrow(self, painter, from_rect: 'QRect', to_rect: 'QRect'):
        """Draw a pointing arrow from bubble to target widget."""
        from PyQt6.QtGui import QPen, QColor
        pen = QPen(QColor("#6060FF"), 2)
        pen.setStyle(Qt.PenStyle.DashLine)
        painter.setPen(pen)
        # Arrow from center of bubble bottom to center of target top
        bx = from_rect.center().x()
        by = from_rect.bottom()
        tx = to_rect.center().x()
        ty = to_rect.top()
        painter.drawLine(bx, by, tx, ty)
        # Arrowhead
        painter.setPen(QPen(QColor("#6060FF"), 2))
        sz = 8
        painter.drawLine(tx, ty, tx - sz, ty + sz)
        painter.drawLine(tx, ty, tx + sz, ty + sz)

    def _next(self):
        self._show_step(self._step + 1)

    def _finish(self):
        S().set("tutorial_done", True)
        self.hide()
        self.deleteLater()
        self.finished.emit()
        # Предлагаем QuickSetup если первый запуск
        if not S().get("quicksetup_done", False, t=bool):
            QTimer.singleShot(400, lambda: QuickSetupWizard.offer(self._mw))

    def paintEvent(self, event):
        """Draw semi-transparent dark overlay with target highlight and arrow."""
        from PyQt6.QtGui import QPainter, QColor, QPen
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Full dim overlay
        p.fillRect(self.rect(), QColor(0, 0, 0, 155))

        # Spotlight cut-out for target widget
        if getattr(self, '_target_rect', None):
            tr = self._target_rect
            # Bright highlight border
            highlight_rect = tr.adjusted(-6, -6, 6, 6)
            p.setPen(QPen(QColor("#6060FF"), 2))
            p.setBrush(QColor(96, 96, 255, 20))
            p.drawRoundedRect(highlight_rect, 8, 8)
            p.setBrush(Qt.BrushStyle.NoBrush)
            # Arrow from bubble to target
            bub = self._bubble.geometry()
            self._draw_arrow(p, bub, tr)

        p.end()

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Escape, Qt.Key.Key_Return, Qt.Key.Key_Space):
            if event.key() == Qt.Key.Key_Escape:
                self._finish()
            else:
                self._next()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self._call_log_widget: CallLogWidget | None = None
        self._call_start_time: float = 0.0

        # Install sounds to persistent location on first run
        install_sounds()

        # Core objects
        self.net   = NetworkManager()
        self.voice = VoiceCallManager(self.net)
        self.net._voice_mgr = self.voice

        # File transfer assembler
        self._ft_pending: dict[str, dict] = {}   # tid → meta+chunks

        # Hot-swap audio device listener — применяется ко всему приложению
        try:
            from PyQt6.QtMultimedia import QMediaDevices
            self._media_devices = QMediaDevices(self)
            self._media_devices.audioOutputsChanged.connect(
                self._on_system_audio_output_changed)
        except Exception:
            pass

        self._apply_theme_from_settings()
        self._setup_window()
        self._setup_ui()
        self._setup_menubar()
        self._setup_statusbar()
        self._connect_signals()

        # PIN lock overlay
        self._pin_overlay: PinLockScreen | None = None
        if S().get("pin_enabled", False, t=bool) and S().get("pin_hash","",t=str):
            self._show_pin_lock()

        # Auto-lock idle timer
        self._idle_timer = QTimer(self)
        self._idle_timer.timeout.connect(self._check_idle)
        self._idle_timer.start(30_000)  # check every 30s
        self._last_activity = time.time()

        # Start networking
        if not self.net.start():
            QMessageBox.critical(self,TR("tab_network"), TR("network_error"))

        # System tray icon
        self._setup_tray()
        # Loyalty: track continuous monthly usage
        self._update_loyalty()

        # Show tutorial on first run
        if not S().get("tutorial_done", False, t=bool):
            QTimer.singleShot(800, self._show_tutorial)

        # Unread counter callbacks
        UNREAD.on_change(self._on_unread_change)

        # Restore geometry
        geom = S().get("window_geometry")
        if geom:
            self.restoreGeometry(geom)
        state = S().get("window_state")
        if state:
            self.restoreState(state)

    def _show_pin_lock(self):
        """Show PIN lock overlay."""
        if self._pin_overlay and self._pin_overlay.isVisible():
            return
        self._pin_overlay = PinLockScreen(self)
        self._pin_overlay.resize(self.size())
        self._pin_overlay.unlocked.connect(self._on_pin_unlocked)
        self._pin_overlay.show()
        self._pin_overlay.raise_()

    def _on_pin_unlocked(self):
        if self._pin_overlay:
            self._pin_overlay.hide()
            self._pin_overlay = None
        self._last_activity = time.time()

    def _check_idle(self):
        """Check for idle and auto-lock if needed."""
        if not S().get("autolock_enabled", False, t=bool):
            return
        if not S().get("pin_hash","",t=str):
            return
        if self._pin_overlay and self._pin_overlay.isVisible():
            return
        timeout_min = S().get("autolock_timeout", 5, t=int)
        if time.time() - self._last_activity > timeout_min * 60:
            self._show_pin_lock()

    def mousePressEvent(self, event):
        self._last_activity = time.time()
        super().mousePressEvent(event)

    def keyPressEvent(self, event):
        self._last_activity = time.time()
        super().keyPressEvent(event)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if self._pin_overlay and self._pin_overlay.isVisible():
            self._pin_overlay.resize(self.size())
        # Resize terminal if exists
        if hasattr(self, '_terminal_panel') and self._terminal_panel.isVisible():
            p = self
            tp = self._terminal_panel
            tp.move(max(0, p.width() - tp.width() - 8),
                    max(0, p.height() - tp.height() - 8))

    # ── tray icon ──────────────────────────────────────────────────────
    def _setup_tray(self):
        global _TRAY_ICON_REF
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return

        self._tray = QSystemTrayIcon(self)
        # Icon — accent coloured circle with phone emoji
        t = get_theme(S().theme)
        pm = QPixmap(32, 32)
        pm.fill(Qt.GlobalColor.transparent)
        painter = QPainter(pm)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setBrush(QBrush(QColor(t.get('accent', '#0078D4'))))
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(0, 0, 32, 32)
        painter.setPen(QPen(QColor("white")))
        painter.setFont(QFont("Arial", 14))
        painter.drawText(QRect(0, 0, 32, 32), Qt.AlignmentFlag.AlignCenter, "📱")
        painter.end()
        self._tray.setIcon(QIcon(pm))
        self._tray.setToolTip(f"{APP_NAME} v{APP_VERSION} — {S().username or '?'}")

        tray_menu = QMenu()
        tray_menu.setStyleSheet(f"""
            QMenu {{
                background: {t['bg2']};
                color: {t['text']};
                border: 1px solid {t['border']};
                border-radius: 12px;
                padding: 6px 4px;
                font-size: 12px;
            }}
            QMenu::item {{
                padding: 8px 20px 8px 14px;
                border-radius: 6px;
                margin: 1px 4px;
            }}
            QMenu::item:selected {{
                background: {t['accent']};
                color: white;
            }}
            QMenu::separator {{
                height: 1px;
                background: {t['border']};
                margin: 4px 10px;
            }}
        """)

        # Header — имя и статус
        header = QWidgetAction(tray_menu)
        hdr_w = QWidget()
        hdr_w.setStyleSheet(f"background:transparent;")
        hdr_lay = QHBoxLayout(hdr_w)
        hdr_lay.setContentsMargins(14, 8, 14, 4)
        hdr_lay.setSpacing(10)
        # Avatar
        av_lbl = QLabel()
        av_lbl.setFixedSize(36, 36)
        av_b64 = S().avatar_b64
        if av_b64:
            try:
                pm2 = base64_to_pixmap(av_b64)
                av_lbl.setPixmap(make_circle_pixmap(pm2, 36))
            except Exception:
                av_lbl.setPixmap(default_avatar(S().username or "?", 36))
        else:
            av_lbl.setPixmap(default_avatar(S().username or "?", 36))
        av_lbl.setStyleSheet("background:transparent;")
        hdr_lay.addWidget(av_lbl)
        # Name + version
        info_lay = QVBoxLayout()
        info_lay.setSpacing(0)
        name_lbl = QLabel(f"<b>{S().username or 'GoidaPhone'}</b>")
        name_lbl.setStyleSheet(f"color:{t['text']};font-size:12px;background:transparent;")
        ver_lbl = QLabel(f"v{APP_VERSION}")
        ver_lbl.setStyleSheet(f"color:{t['text_dim']};font-size:10px;background:transparent;")
        info_lay.addWidget(name_lbl)
        info_lay.addWidget(ver_lbl)
        hdr_lay.addLayout(info_lay)
        hdr_lay.addStretch()
        # Premium badge
        if S().premium:
            prem = QLabel("✦")
            prem.setStyleSheet(f"color:#FFD700;font-size:14px;background:transparent;")
            hdr_lay.addWidget(prem)
        header.setDefaultWidget(hdr_w)
        tray_menu.addAction(header)
        tray_menu.addSeparator()

        # Actions
        open_act = QAction("🖥  " + _L("Открыть", "Open", "開く"), self)
        open_act.triggered.connect(self._restore_from_tray)
        tray_menu.addAction(open_act)

        settings_act = QAction("⚙  " + _L("Настройки", "Settings", "設定"), self)
        settings_act.triggered.connect(self._show_settings)
        tray_menu.addAction(settings_act)

        mute_act = QAction("🎤  " + _L("Вкл/Выкл микрофон", "Toggle Mic", "マイク切替"), self)
        mute_act.triggered.connect(self._toggle_mute)
        tray_menu.addAction(mute_act)

        tray_menu.addSeparator()

        terminal_act = QAction("⌨  ZLink Terminal", self)
        terminal_act.triggered.connect(self._open_terminal)
        tray_menu.addAction(terminal_act)

        tray_menu.addSeparator()

        def _quit():
            self._force_quit = True
            self.close()
        quit_act = QAction("❌  " + _L("Выход", "Quit", "終了"), self)
        quit_act.triggered.connect(_quit)
        tray_menu.addAction(quit_act)

        self._tray.setContextMenu(tray_menu)
        self._tray.activated.connect(self._on_tray_activated)
        self._tray.show()

        _TRAY_ICON_REF = self._tray

    def _on_tray_activated(self, reason):
        if reason == QSystemTrayIcon.ActivationReason.DoubleClick:
            self._restore_from_tray()

    def _restore_from_tray(self):
        self.show()
        self.raise_()
        self.activateWindow()

    def _on_unread_change(self, chat_id: str, count: int):
        """Update window title with total unread count."""
        total = UNREAD.total()
        if total > 0:
            self.setWindowTitle(
                f"({total}) {APP_NAME} v{APP_VERSION}  —  {COMPANY_NAME}")
            if hasattr(self, '_tray'):
                self._tray.setToolTip(
                    f"{APP_NAME} — {total} непрочитанных")
        else:
            self.setWindowTitle(
                f"{APP_NAME} v{APP_VERSION}  —  {COMPANY_NAME}")
            if hasattr(self, '_tray'):
                self._tray.setToolTip(f"{APP_NAME} v{APP_VERSION}")
        key = S().theme
        if key.startswith("__custom_"):
            slot = int(key.split("_")[-1])
            data = S().custom_theme(slot)
            if data and "colors" in data:
                t = {**get_theme("dark"), **data["colors"]}
                QApplication.instance().setStyleSheet(build_stylesheet(t))
                return
            key = "dark"
        t = get_theme(key)
        QApplication.instance().setStyleSheet(build_stylesheet(t))

    # ── theme ──────────────────────────────────────────────────────────
    def _apply_theme_from_settings(self):
        """Apply the current theme from AppSettings to the QApplication."""
        key = S().theme
        if key.startswith("__custom_"):
            try:
                slot = int(key.split("_")[-1])
                data = S().custom_theme(slot)
                if data and "colors" in data:
                    t = {**get_theme("dark"), **data["colors"]}
                    QApplication.instance().setStyleSheet(build_stylesheet(t))
                    self._retheme_all_displays()
                    return
            except Exception:
                pass
            key = "dark"
        t = get_theme(key)
        QApplication.instance().setStyleSheet(build_stylesheet(t))
        self._retheme_all_displays()

    def _retheme_all_displays(self):
        """Force all ChatDisplay widgets to re-apply theme to bubbles."""
        if hasattr(self, 'chat_panel'):
            try:
                for d in self.chat_panel.findChildren(ChatDisplay):
                    d.retheme()
                # Also retheme current display directly
                if hasattr(self.chat_panel, '_display'):
                    self.chat_panel._display.retheme()
            except Exception:
                pass

    # ── window setup ───────────────────────────────────────────────────
    def _setup_window(self):
        self.setWindowTitle(f"{APP_NAME} v{APP_VERSION}  —  {COMPANY_NAME}")
        self.setMinimumSize(900, 620)
        self.resize(1100, 700)

    def keyPressEvent(self, event):
        """Handle global key shortcuts."""
        mods = event.modifiers()
        key  = event.key()
        if key == Qt.Key.Key_F11:
            self._toggle_fullscreen()
        elif key == Qt.Key.Key_Escape and self.isFullScreen():
            self.showNormal()
        elif key == Qt.Key.Key_F10 and (mods & Qt.KeyboardModifier.ShiftModifier):
            self._toggle_terminal()
        elif key == Qt.Key.Key_F9 and (mods & Qt.KeyboardModifier.ShiftModifier):
            self._toggle_terminal()
        else:
            super().keyPressEvent(event)

    def _on_tab_changed(self, idx: int):
        """Trigger repaint on tab switch (safe — no opacity effect on container)."""
        w = self._tabs.widget(idx)
        if w is not None:
            w.update()
            w.repaint()

    def _open_wns(self):
        """Switch to the WNS tab."""
        for i in range(self._tabs.count()):
            if "WNS" in self._tabs.tabText(i):
                self._tabs.setCurrentIndex(i)
                return
        # Fallback: create if missing
        if not hasattr(self, '_wns_player') or self._wns_player is None:
            self._wns_player = WinoraNetScape()
        idx = self._tabs.addTab(self._wns_player, "🌐 WNS")
        self._tabs.setCurrentIndex(idx)

    def _open_terminal(self):
        """Open a new ZLink Terminal tab — multiple terminals supported."""
        t = get_theme(S().theme)
        # Создаём новый терминал
        term = GoidaTerminal(self.net, self)
        term.setWindowFlags(Qt.WindowType.Widget)

        # Считаем сколько уже открыто
        term_count = sum(1 for i in range(self._tabs.count())
                        if "Terminal" in self._tabs.tabText(i) or
                           "ZLink" in self._tabs.tabText(i))
        tab_label = f"⌨ Terminal" if term_count == 0 else f"⌨ Terminal {term_count + 1}"

        idx = self._tabs.addTab(term, tab_label)
        self._tabs.setCurrentIndex(idx)
        self._add_tab_close_btn(idx)

        # Плавный fade-in
        from PyQt6.QtWidgets import QGraphicsOpacityEffect
        eff = QGraphicsOpacityEffect(term)
        eff.setOpacity(0.0)
        term.setGraphicsEffect(eff)
        fade = QPropertyAnimation(eff, b"opacity", term)
        fade.setDuration(300)
        fade.setStartValue(0.0)
        fade.setEndValue(1.0)
        fade.setEasingCurve(QEasingCurve.Type.OutCubic)
        def _fade_done():
            term.setGraphicsEffect(None)
        fade.finished.connect(_fade_done)
        fade.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)

        QTimer.singleShot(50, term._print_header)

    def _start_tutorial(self):
        """Start the interactive tutorial."""
        lang = S().language
        reply = QMessageBox.question(
            self,
            "Учебник" if lang == "ru" else "Tutorial",
            ("Запустить учебник GoidaPhone?\nОн покажет основные функции приложения."
             if lang == "ru" else
             "Start the GoidaPhone tutorial?\nIt will walk you through the main features."),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            S().set("tutorial_done", False)
            self._show_tutorial()

    def _start_quicksetup(self):
        """Запустить быструю настройку как inline вкладку."""
        for i in range(self._tabs.count()):
            if "настройка" in self._tabs.tabText(i).lower():
                self._tabs.setCurrentIndex(i)
                return
        wiz = QuickSetupWizard(self)
        wiz.setWindowFlags(Qt.WindowType.Widget)
        wiz.setModal(False)
        idx = self._tabs.addTab(wiz, TR("quicksetup_title"))
        self._tabs.setCurrentIndex(idx)
        self._add_tab_close_btn(idx)

    def _toggle_terminal(self):
        """Toggle the floating GoidaPhone terminal panel with animation."""
        if not hasattr(self, '_terminal_panel'):
            return
        tp = self._terminal_panel
        if tp.isVisible():
            # Fade out then hide, clear effect
            from PyQt6.QtWidgets import QGraphicsOpacityEffect
            eff = QGraphicsOpacityEffect(tp)
            tp.setGraphicsEffect(eff)
            a = QPropertyAnimation(eff, b"opacity", tp)
            a.setDuration(160)
            a.setStartValue(1.0)
            a.setEndValue(0.0)
            a.setEasingCurve(QEasingCurve.Type.InCubic)
            a.finished.connect(tp.hide)
            a.finished.connect(lambda: tp.setGraphicsEffect(None))
            a.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)
        else:
            tp.show()
            tp.raise_()
            # Fade in then clear effect
            from PyQt6.QtWidgets import QGraphicsOpacityEffect
            eff = QGraphicsOpacityEffect(tp)
            eff.setOpacity(0.0)
            tp.setGraphicsEffect(eff)
            a = QPropertyAnimation(eff, b"opacity", tp)
            a.setDuration(200)
            a.setStartValue(0.0)
            a.setEndValue(1.0)
            a.setEasingCurve(QEasingCurve.Type.OutCubic)
            a.finished.connect(lambda: tp.setGraphicsEffect(None))
            a.finished.connect(lambda: tp.update())
            a.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)

    def _toggle_fullscreen(self):
        """п.34 — F11 полноэкранный режим."""
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def _apply_tab_visibility(self):
        """Show/hide permanent tabs based on settings (can't delete, only hide)."""
        show_notes = S().get("tab_show_notes", True, t=bool)
        show_calls = S().get("tab_show_calls", True, t=bool)
        # Find and set visibility
        for i in range(self._tabs.count()):
            text = self._tabs.tabText(i)
            if "Заметки" in text:
                self._tabs.setTabVisible(i, show_notes)
            elif "Звонки" in text:
                self._tabs.setTabVisible(i, show_calls)

    # ── UI ─────────────────────────────────────────────────────────────
    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_lay = QHBoxLayout(central)
        main_lay.setContentsMargins(0,0,0,0)
        main_lay.setSpacing(0)

        # Left: peer panel
        self.peer_panel = PeerPanel(self.net)
        self.peer_panel.setFixedWidth(280)

        # Right: tab area — permanent tabs have no close button
        self._tabs = QTabWidget()
        self._tabs.setDocumentMode(True)
        self._tabs.setTabsClosable(False)   # default off; per-tab via button
        self._tabs.tabBar().setFixedHeight(28)   # prevent tab bar height growth
        self._tabs.tabBar().setExpanding(False)
        self._tabs.currentChanged.connect(self._on_tab_changed)

        # Permanent tabs
        self.chat_panel = ChatPanel(self.net, self.voice)
        self._tabs.addTab(self.chat_panel, TR("tab_main_chat"))

        self.notes_widget = NotesWidget()
        self._tabs.addTab(self.notes_widget, TR("tab_main_notes"))

        self._call_log_widget = CallLogWidget()
        self._tabs.addTab(self._call_log_widget, TR("tab_main_calls"))

        self._mewa_player = MewaPlayer()
        self._tabs.addTab(self._mewa_player, "♫ Mewa")

        # WNS as inline tab (not separate window)
        self._wns_player = WinoraNetScape()
        self._tabs.addTab(self._wns_player, "🌐 WNS")
        self._wns_window = None  # no longer used as window

        # Mark permanent count
        self._permanent_tab_count = 5

        # Splitter
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self.peer_panel)
        splitter.addWidget(self._tabs)
        splitter.setStretchFactor(0,0)
        splitter.setStretchFactor(1,1)
        main_lay.addWidget(splitter)

        # Floating terminal panel (hidden by default, Shift+F10)
        self._terminal_panel = GoidaTerminal(self.net, parent=central)
        self._terminal_panel.hide()

    # ── Menubar ────────────────────────────────────────────────────────
    def _setup_menubar(self):
        mb = self.menuBar()

        def act(text, slot, shortcut=None):
            a = QAction(text, self)
            a.triggered.connect(slot)
            if shortcut:
                a.setShortcut(shortcut)
                # ApplicationShortcut works on Windows even when menu not focused
                a.setShortcutContext(Qt.ShortcutContext.ApplicationShortcut)
            return a

        # File
        fm = mb.addMenu(TR("menu_file"))
        fm.addAction(act(TR("menu_my_profile"),          self._show_profile,       "Ctrl+P"))
        fm.addAction(act(TR("menu_settings"),             self._show_settings))
        fm.addSeparator()
        fm.addAction(act(TR("menu_check_updates"),  self._check_updates_quick))
        fm.addSeparator()
        fm.addAction(act(TR("menu_quit"),                 self.close,               "Ctrl+Q"))

        # View
        vm = mb.addMenu(TR("menu_view"))
        tm = vm.addMenu(TR("menu_themes"))
        for key, td in THEMES.items():
            tm.addAction(act(td["label"], lambda _, k=key: self._switch_theme(k)))
        vm.addSeparator()
        vm.addAction(act(TR("menu_public_chat"), self._go_public))
        vm.addAction(act("♫ Mewa 1-2-3", lambda: self._tabs.setCurrentWidget(self._mewa_player)))
        vm.addSeparator()
        vm.addAction(act(TR("menu_fullscreen"), self._toggle_fullscreen, "F11"))
        vm.addAction(act(TR("menu_lang_ru"),  lambda: self._switch_language("ru")))
        vm.addAction(act(TR("menu_lang_en"),  lambda: self._switch_language("en")))
        vm.addAction(act(TR("menu_lang_ja"),   lambda: self._switch_language("ja")))

        # Calls
        cm = mb.addMenu(TR("menu_calls"))
        cm.addAction(act(TR("menu_mute_toggle"),    self._toggle_mute,        "Ctrl+M"))
        cm.addAction(act(TR("menu_hangup_all"),  self.voice.hangup_all))

        # Help
        hm = mb.addMenu(TR("menu_help"))
        hm.addAction(act(TR("menu_about"), self._about))
        hm.addAction(act("📊 Отчёт для Winora", self._generate_report))
        hm.addSeparator()
        hm.addAction(act(TR("menu_terminal"), self._open_terminal, "Ctrl+`"))
        hm.addAction(act(TR("menu_wns"), self._open_wns, "Ctrl+B"))
        hm.addSeparator()
        hm.addAction(act(TR("menu_tutorial"), self._start_tutorial))
        hm.addAction(act(TR("quicksetup_title"), self._start_quicksetup))

        # Extra QShortcuts — fallback for Windows where QAction shortcuts may not fire
        from PyQt6.QtGui import QKeySequence, QShortcut as _QSC
        for _keys, _cb in [
            ("Ctrl+`",  self._open_terminal),
            ("Ctrl+B",  self._open_wns),
            ("Ctrl+P",  self._show_profile),
            ("Ctrl+Q",  self.close),
            ("Ctrl+M",  self._toggle_mute),
            ("F11",     self._toggle_fullscreen),
        ]:
            _sc = _QSC(QKeySequence(_keys), self)
            _sc.setContext(Qt.ShortcutContext.ApplicationShortcut)
            _sc.activated.connect(_cb)



    def _switch_theme(self, key: str):
        prev = S().theme
        S().theme = key
        self._apply_theme_from_settings()
        if prev != key:
            dlg = QMessageBox(self)
            dlg.setWindowTitle(_L("Тема изменена","Theme changed","テーマ変更"))
            dlg.setText(_L(
                f"Тема «{key}» применена. Нужен перезапуск для полного применения.",
                f"Theme «{key}» applied. Restart for full effect.",
                f"テーマ «{key}» を適用。完全適用には再起動が必要。"))
            back = dlg.addButton(_L("← Вернуть","← Revert","← 元に戻す"),
                                 QMessageBox.ButtonRole.RejectRole)
            restart = dlg.addButton(_L("↺ Перезапустить","↺ Restart","↺ 再起動"),
                                    QMessageBox.ButtonRole.AcceptRole)
            dlg.addButton(_L("Позже","Later","後で"),
                          QMessageBox.ButtonRole.DestructiveRole)
            dlg.setDefaultButton(restart)
            dlg.exec()
            if dlg.clickedButton() == back:
                S().theme = prev
                self._apply_theme_from_settings()
            elif dlg.clickedButton() == restart:
                import os as _os, sys as _sys
                _os.execv(_sys.executable, [_sys.executable] + _sys.argv)

    def _switch_language(self, lang: str):
        if S().language == lang:
            return
        S().language = lang
        # Немедленный перезапуск — единственный способ полного применения
        import os, sys
        os.execv(sys.executable, [sys.executable] + sys.argv)

    # ── Statusbar ──────────────────────────────────────────────────────
    def _setup_statusbar(self):
        sb = self.statusBar()

        self._status_main = QLabel(TR("searching"))
        sb.addWidget(self._status_main)

        self._status_ip = StatusWidget(f"IP: {get_local_ip()}", "#8090B0")
        sb.addPermanentWidget(self._status_ip)

        self._status_mic = StatusWidget(TR("mic_on"), "#80FF80")
        self._status_mic.setToolTip("Кликните для переключения")
        self._status_mic.mousePressEvent = lambda e: self._toggle_mute()
        sb.addPermanentWidget(self._status_mic)

        self._status_call = StatusWidget(TR("no_calls"), "#A0A0A0")
        sb.addPermanentWidget(self._status_call)

        self._status_prem = StatusWidget("👑 Premium", "#FFD700")
        self._status_prem.setVisible(S().premium)
        sb.addPermanentWidget(self._status_prem)

    # ── Signals ────────────────────────────────────────────────────────
    def _connect_signals(self):
        # Network
        self.net.sig_user_online.connect(self._on_user_online)
        self.net.sig_user_offline.connect(self._on_user_offline)
        self.net.sig_message.connect(self._on_message)
        self.net.sig_call_request.connect(self._on_call_request)
        self.net.sig_call_accepted.connect(self._on_call_accepted)
        self.net.sig_call_rejected.connect(self._on_call_rejected)
        self.net.sig_call_ended.connect(self._on_call_ended)
        self.net.sig_file_meta.connect(self._on_file_meta)
        self.net.sig_group_invite.connect(self._on_group_invite)
        self.net.sig_error.connect(self._on_net_error)
        self.net.sig_typing.connect(self.chat_panel.show_typing)

        # File chunks via UDP — handle in dispatch
        # We intercept MSG_FILE_DATA in the dispatcher below:
        self.net.sig_message.connect(self._on_file_chunk_msg)

        # Voice
        self.voice.call_started.connect(self._on_call_started)
        self.voice.call_ended.connect(self._on_call_ended)

        # Peer panel
        self.peer_panel.chat_requested.connect(self._open_chat)
        self.peer_panel.call_requested.connect(self._call_peer)
        self.peer_panel.group_selected.connect(self._open_group)
        self.peer_panel.invite_message_requested.connect(self._on_invite_message_requested)

    # ── Call state trackers ─────────────────────────────────────────────
    _outgoing_call_win: 'OutgoingCallWindow | None' = None
    _incoming_call_dlg: 'IncomingCallDialog | None' = None
    _active_call_win:   'ActiveCallWindow | None'   = None
    _group_call_win:    'GroupCallWindow | None'     = None

    # ── Event handlers ─────────────────────────────────────────────────
    def _on_user_online(self, peer: dict):
        self.peer_panel.add_peer(peer)
        count = len(self.net.peers)
        self._status_main.setText(f"🟢 Онлайн: {count}")
        if S().notification_sounds:
            play_system_sound("online")

    def _on_user_offline(self, ip: str):
        play_system_sound("offline")
        self.peer_panel.remove_peer(ip)
        count = len(self.net.peers)
        self._status_main.setText(f"🟢 Онлайн: {count}")

    def _on_message(self, msg: dict):
        mtype = msg.get("type","")
        if mtype in (MSG_CHAT, MSG_PRIVATE, MSG_GROUP):
            self.chat_panel.receive_message(msg)

    def _on_file_chunk_msg(self, msg: dict):
        """Handle file data chunks."""
        if msg.get("type") != MSG_FILE_DATA:
            return
        FT.on_chunk_msg(msg)

    def _on_file_meta(self, meta: dict):
        FT.on_meta(meta)
        FT.file_received.connect(self._on_file_assembled)

    def _on_file_assembled(self, meta: dict, data: bytes):
        self.chat_panel.receive_file(meta, data)

    # ── INCOMING: remote is calling us ──────────────────────────────────
    def _on_call_request(self, caller: str, ip: str):
        # Get avatar from peer info if available
        peer    = self.net.peers.get(ip, {})
        av_b64  = peer.get("avatar_b64", "")
        dlg = IncomingCallDialog(caller, ip, av_b64)
        self._incoming_call_dlg = dlg
        dlg.accepted_call.connect(lambda: self._accept_call(caller, ip))
        dlg.rejected_call.connect(lambda: self._reject_call(ip))
        dlg.show()

    def _accept_call(self, caller: str, ip: str):
        self._incoming_call_dlg = None
        # Tell caller we accepted
        self.net.send_call_accept(ip)
        # Start voice
        self.voice.call(ip)
        self._call_start_time = time.time()
        peer = self.net.peers.get(ip, {"username": caller, "ip": ip})
        # Show active call window
        av_b64 = peer.get("avatar_b64", "")
        self._active_call_win = ActiveCallWindow(caller, ip, av_b64, voice_mgr=self.voice)
        self._active_call_win.sig_hangup.connect(lambda: self._hangup_call(ip))
        self._active_call_win.sig_mute.connect(self.voice.set_mute)
        self._active_call_win.show()
        self._on_call_started(ip)
        self._open_chat(peer)

    def _reject_call(self, ip: str):
        self._incoming_call_dlg = None
        self.net.send_call_reject(ip)

    # ── OUTGOING: we are calling someone ────────────────────────────────
    def _call_peer(self, peer: dict):
        ip   = peer.get("ip", "")
        name = peer.get("username", ip)
        av   = peer.get("avatar_b64", "")
        if not ip:
            return
        # Show outgoing call screen
        win = OutgoingCallWindow(name, ip, av)
        self._outgoing_call_win = win
        win.sig_cancelled.connect(lambda: self._cancel_outgoing(ip))
        win.show()
        # Send ring signal (no voice yet — wait for accept)
        self.net.send_call_request(ip)
        if self._call_log_widget:
            self._call_log_widget.add_call(name, True)

    def _cancel_outgoing(self, ip: str):
        self._outgoing_call_win = None
        self.net.send_call_end(ip)

    # Remote accepted our call
    def _on_call_accepted(self, caller: str, ip: str):
        if self._outgoing_call_win:
            self._outgoing_call_win.call_answered()
            self._outgoing_call_win = None
        # Start voice NOW (they accepted)
        self.net.connect_voice(ip)
        self.voice.call(ip)
        self._call_start_time = time.time()
        peer  = self.net.peers.get(ip, {"username": caller, "ip": ip})
        av_b64 = peer.get("avatar_b64", "")
        self._active_call_win = ActiveCallWindow(caller, ip, av_b64, voice_mgr=self.voice)
        self._active_call_win.sig_hangup.connect(lambda: self._hangup_call(ip))
        self._active_call_win.sig_mute.connect(self.voice.set_mute)
        self._active_call_win.show()
        self._on_call_started(ip)

    # Remote rejected our call
    def _on_call_rejected(self, ip: str):
        if self._outgoing_call_win:
            self._outgoing_call_win.call_rejected()
            self._outgoing_call_win = None
        play_system_sound("error")

    def _hangup_call(self, ip: str):
        self.voice.hangup(ip)
        self._active_call_win = None

    def _on_call_started(self, ip: str):
        peer = self.net.peers.get(ip, {})
        name = peer.get("username", ip)
        self._status_call.setText(f"📞 {name}")
        t = get_theme(S().theme)
        self._status_call.setStyleSheet(
            f"color:#80FF80; background:{t['bg2']}; border:1px solid #2A5A2A;"
            "border-radius:3px; padding:1px 7px; font-size:10px;")
        self._call_start_time = time.time()
        self.chat_panel.on_call_ended  # ensure chat knows

    def _on_call_ended(self, ip: str):
        # Guard: предотвращаем двойной вызов (net + voice оба эмитируют)
        if getattr(self, '_call_ending', False):
            return
        self._call_ending = True
        try:
            play_system_sound("call_end")
            # Завершаем аудио через VCM (закрывает pyaudio streams)
            try:
                if ip in self.voice.active:
                    self.voice.hangup(ip)
            except Exception:
                pass
            # Close any active call windows
            if self._active_call_win:
                try:
                    self._active_call_win._timer.stop()
                    self._active_call_win.hide()
                    self._active_call_win.deleteLater()
                except Exception:
                    pass
                self._active_call_win = None
            if self._outgoing_call_win:
                try:
                    self._outgoing_call_win.call_rejected()
                except Exception:
                    pass
                self._outgoing_call_win = None

            dur = time.time() - self._call_start_time if self._call_start_time else 0
            peer = self.net.peers.get(ip, {})
            name = peer.get("username", ip)
            if self._call_log_widget and dur > 0:
                self._call_log_widget.add_call(name, False, dur)
            self._call_start_time = 0.0
            t = get_theme(S().theme)
            self._status_call.setText(TR("no_calls"))
            self._status_call.setStyleSheet(
                f"color:{t['text_dim']}; background:{t['bg2']}; border:1px solid {t['border']};"
                "border-radius:3px; padding:1px 7px; font-size:10px;")
            self.chat_panel.on_call_ended(ip)
        finally:
            # Сбрасываем флаг через небольшую задержку чтобы все сигналы успели пройти
            QTimer.singleShot(300, lambda: setattr(self, '_call_ending', False))

    # ── GROUP CALL ───────────────────────────────────────────────────────
    def _start_group_call(self, gid: str):
        """Start or join a group call — opens GroupCallWindow."""
        group = GROUPS.groups.get(gid)
        if not group:
            return
        gname  = group.get("name", gid)
        # Build participant list from online peers in group
        members = group.get("members", [])
        participants = [
            p for ip, p in self.net.peers.items()
            if ip in members
        ]
        # Start voice with all online members
        for p in participants:
            self.voice.call(p.get("ip", ""))

        win = GroupCallWindow(gname, participants, self.voice)
        self._group_call_win = win
        win.sig_leave.connect(self._leave_group_call)
        win.show()
        # Broadcast call request to group members
        for p in participants:
            self.net.send_call_request(p.get("ip", ""))

    def _leave_group_call(self):
        self.voice.hangup_all()
        self._group_call_win = None

    def _on_group_invite(self, gid: str, gname: str, from_ip: str):
        from_name = self.net.peers.get(from_ip, {}).get("username", from_ip)
        reply = QMessageBox.question(self,"Приглашение в группу",
            f"<b>{from_name}</b> приглашает вас в группу «{gname}».\nВступить?",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply == QMessageBox.StandardButton.Yes:
            GROUPS.add_member(gid, get_local_ip())
            self.peer_panel._refresh_groups()

    def _on_invite_message_requested(self, dest_type: str, dest_data, invite_json: str):
        """Send a group invite bubble into the chosen chat."""
        import json
        try:
            inv = json.loads(invite_json)
        except Exception:
            return
        gid   = inv.get("gid", "")
        gname = inv.get("gname", "?")
        host  = inv.get("host", "")

        invite_text = f"__GROUP_INVITE__:{invite_json}"

        ts = time.time()
        if dest_type == "public":
            self.chat_panel.open_public()
            self._tabs.setCurrentWidget(self.chat_panel)
            self.chat_panel._send_text_raw(invite_text)
        elif dest_type == "peer" and dest_data:
            self.chat_panel.open_peer(dest_data)
            self._tabs.setCurrentWidget(self.chat_panel)
            self.chat_panel._send_text_raw(invite_text)
        elif dest_type == "group" and dest_data:
            self.chat_panel.open_group(dest_data)
            self._tabs.setCurrentWidget(self.chat_panel)
            self.chat_panel._send_text_raw(invite_text)

    def _on_net_error(self, err: str):
        self._status_main.setText(f"❌ {err}")
        QTimer.singleShot(5000, lambda: self._status_main.setText(
            f"🟢 Онлайн: {len(self.net.peers)}"))

    # ── Navigation ──────────────────────────────────────────────────────
    def _open_chat(self, peer: dict):
        self.chat_panel.open_peer(peer)
        self._tabs.setCurrentWidget(self.chat_panel)

    def _open_group(self, gid: str):
        self.chat_panel.open_group(gid)
        self._tabs.setCurrentWidget(self.chat_panel)

    def _go_public(self):
        self.chat_panel.open_public()
        self._tabs.setCurrentWidget(self.chat_panel)

    # ── Actions ─────────────────────────────────────────────────────────
    def _show_tutorial(self):
        """Launch interactive tutorial overlay."""
        overlay = TutorialOverlay(self, parent=self)
        overlay.resize(self.size())
        overlay.show()
        overlay.raise_()

    def _update_loyalty(self):
        """Track continuous monthly usage for loyalty level."""
        try:
            last_month = S().get("loyalty_last_month", "", t=str)
            start_str  = S().get("loyalty_start_date", "", t=str)
            now        = datetime.now()
            cur_month  = now.strftime("%Y-%m")
            if not start_str:
                S().set("loyalty_start_date", now.isoformat())
            if last_month != cur_month:
                S().set("loyalty_last_month", cur_month)
                # Compute full months since start
                try:
                    start = datetime.fromisoformat(S().get("loyalty_start_date",""))
                    months = (now.year - start.year) * 12 + (now.month - start.month)
                    S().set("loyalty_months", max(0, months))
                except Exception:
                    pass
        except Exception:
            pass

    def _show_profile(self):
        """Show profile editor as inline tab (with close button)."""
        for i in range(self._tabs.count()):
            if self._tabs.tabText(i).strip() == "👤 Профиль":
                self._tabs.setCurrentIndex(i)
                return
        dlg = ProfileDialog(self)
        dlg.profile_saved.connect(self._on_profile_saved)
        dlg.setWindowFlags(Qt.WindowType.Widget)
        idx = self._tabs.addTab(dlg, "👤 Профиль")
        self._tabs.setCurrentIndex(idx)
        self._add_tab_close_btn(idx)

    def _show_settings(self):
        """Show settings as inline tab (with close button)."""
        for i in range(self._tabs.count()):
            if self._tabs.tabText(i).strip() == "⚙ Настройки":
                self._tabs.setCurrentIndex(i)
                return
        dlg = SettingsDialog(self)
        dlg.settings_saved.connect(self._on_settings_saved)
        dlg.settings_saved.connect(lambda: self._remove_tab_by_title("⚙ Настройки"))
        dlg.setWindowFlags(Qt.WindowType.Widget)
        idx = self._tabs.addTab(dlg, "⚙ Настройки")
        self._tabs.setCurrentIndex(idx)
        self._add_tab_close_btn(idx)

    def _add_tab_close_btn(self, idx: int):
        """Add a small ✕ button to a specific tab (finds tab by widget ref)."""
        t = get_theme(S().theme)
        widget_ref = self._tabs.widget(idx)  # capture widget, not index
        btn = QPushButton("✕")
        btn.setFixedSize(18, 18)
        btn.setFlat(True)
        btn.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setStyleSheet(f"""
            QPushButton {{
                color: {t['text_dim']}; background: transparent;
                border: none; border-radius: 9px;
                font-size: 11px; font-weight: bold;
            }}
            QPushButton:hover {{ color: #FF6060; background: rgba(255,80,80,51); }}
        """)
        def _close_by_widget():
            # Find tab by its widget reference — survives reordering
            for i in range(self._tabs.count()):
                if self._tabs.widget(i) is widget_ref:
                    if i >= self._permanent_tab_count:
                        self._tabs.removeTab(i)
                    return
        btn.clicked.connect(_close_by_widget)
        self._tabs.tabBar().setTabButton(idx,
            self._tabs.tabBar().ButtonPosition.RightSide, btn)

    def _remove_tab_by_title(self, title: str):
        for i in range(self._tabs.count()):
            if self._tabs.tabText(i).strip() == title.strip():
                self._tabs.removeTab(i)
                return

    def _remove_tab_by_index(self, idx: int):
        if idx >= self._permanent_tab_count:
            self._tabs.removeTab(idx)

    def _close_tab(self, idx: int):
        if idx >= self._permanent_tab_count:
            self._tabs.removeTab(idx)

    def _on_profile_saved(self):
        self.net._broadcast()
        self._status_prem.setVisible(S().premium)
        # Close profile tab
        for i in range(self._tabs.count()):
            if self._tabs.tabText(i) == "👤 Профиль":
                self._tabs.removeTab(i)
                break

    def _on_settings_saved(self):
        """Apply ALL settings to the live UI after SettingsDialog saves."""
        t = get_theme(S().theme)

        # 1. Theme
        self._apply_theme_from_settings()

        # 2. Premium badge
        self._status_prem.setVisible(S().premium)

        # 3. App scale — apply immediately
        try:
            _scale = S().get("app_scale", 100, t=int)
            _pt = max(7.0, 9.0 * _scale / 100.0)
            _font = QApplication.instance().font()
            _font.setPointSizeF(_pt)
            QApplication.instance().setFont(_font)
        except Exception:
            pass

        # 4. Tab visibility
        self._apply_tab_visibility()

        # 5. Reload status bar info
        try:
            ip = get_local_ip()
            self._status_ip.setText(f"IP: {ip}")
        except Exception:
            pass

        # 6. Window title update (username may have changed via ProfileEditor)
        try:
            self.setWindowTitle(
                f"{APP_NAME} v{APP_VERSION} — {COMPANY_NAME}")
        except Exception:
            pass

        # 7. Sidebar weather refresh
        try:
            if hasattr(self, '_refresh_weather'):
                self._refresh_weather()
        except Exception:
            pass

        # 8. Broadcast updated presence (shows new status/avatar to peers)
        try:
            if hasattr(self, 'net') and self.net.running:
                self.net._broadcast()
        except Exception:
            pass

        # 9. GoidaCRYPTO layers — применяем активные
        # Layer 6: Clipboard auto-clear
        try:
            if S().get("crypto_layer6_clipboard", False, t=bool):
                if not hasattr(self, '_cb_clear_timer'):
                    self._cb_clear_timer = QTimer(self)
                    self._cb_clear_timer.timeout.connect(
                        lambda: QApplication.clipboard().clear())
                self._cb_clear_timer.start(30_000)
            elif hasattr(self, '_cb_clear_timer'):
                self._cb_clear_timer.stop()
        except Exception:
            pass

        # Layer 4: Stealth Mode
        try:
            flags = self.windowFlags()
            if S().get("crypto_layer4_stealth", False, t=bool):
                self.setWindowFlags(flags | Qt.WindowType.Tool)
                self.show()
            else:
                self.setWindowFlags(flags & ~Qt.WindowType.Tool)
                self.show()
        except Exception:
            pass

        # Layer 5: Screenshot protection
        try:
            self.setAttribute(
                Qt.WidgetAttribute.WA_NoSystemBackground,
                S().get("crypto_layer5_screenshot", False, t=bool))
        except Exception:
            pass

        # Layer 20: Paranoid — включает все защитные слои
        if S().get("crypto_layer20_paranoid", False, t=bool):
            for _pk in ["crypto_layer3_wipe", "crypto_layer4_stealth",
                        "crypto_layer5_screenshot", "crypto_layer6_clipboard",
                        "crypto_layer7_idle_lock", "crypto_layer15_msg_hmac",
                        "crypto_layer16_replay", "crypto_layer17_rate_limit"]:
                S().set(_pk, True)

        # Notify user what requires restart
        _restart_needed = []
        if S().get("udp_port", 17385, t=int) != getattr(self.net, '_udp_port_at_start', S().get("udp_port", 17385, t=int)):
            _restart_needed.append("UDP/TCP порты")

    def _toggle_mute(self):
        muted = self.voice.toggle_mute()
        if muted:
            self._status_mic.setText(TR("mic_off"))
            self._status_mic.setStyleSheet(
                "color:#FF6060; background:#3A1010; border:1px solid #5A2020;"
                "border-radius:3px; padding:1px 7px; font-size:10px;")
        else:
            self._status_mic.setText(TR("mic_on"))
            self._status_mic.setStyleSheet(
                "color:#80FF80; background:#103010; border:1px solid #205020;"
                "border-radius:3px; padding:1px 7px; font-size:10px;")

    def _generate_report(self):
        """Собрать детальный отчёт для Winora — ВСЁ что может помочь."""
        import platform, os, sys, time, json, socket, subprocess
        from datetime import datetime
        
        report = []
        report.append("=" * 60)
        report.append("GoidaPhone NT Server 1.8 - Winora Company")
        now_str = datetime.now().strftime("%d.%m.%Y %H:%M:%S")
        report.append(f"Отчёт сгенерирован: {now_str}")
        report.append("=" * 60)
        
        cfg = S()
        
        report.append("\n[СИСТЕМА]")
        report.append(f"  OS: {platform.system()} {platform.release()}")
        report.append(f"  Архитектура: {platform.machine()}")
        report.append(f"  Python: {sys.version}")
        report.append(f"  Имя хоста: {platform.node()}")
        report.append(f"  Аргументы: {sys.argv}")
        try:
            load = os.getloadavg()
            report.append(f"  Load: {load[0]:.1f} {load[1]:.1f} {load[2]:.1f}")
        except:
            pass
        
        report.append("\n[GOIDAPHONE]")
        report.append(f"  Версия: {APP_VERSION}")
        report.append(f"  Протокол: v{PROTOCOL_VERSION}")
        report.append(f"  Пользователь: {cfg.username}")
        report.append(f"  Тема: {cfg.theme}")
        report.append(f"  Язык: {cfg.language}")
        report.append(f"  Premium: {cfg.premium}")
        report.append(f"  Масштаб: {cfg.get('app_scale', 100)}%")
        report.append(f"  Уведомления: {cfg.notification_sounds}")
        report.append(f"  История: {cfg.save_history}")
        report.append(f"  PIN: {cfg.get('pin_enabled', False)}")
        report.append(f"  GoidaID: {cfg.safe_mode}")
        report.append(f"  Статус: {cfg.user_status}")
        report.append(f"  Аватар: {'есть' if cfg.avatar_b64 else 'нет'}")
        report.append(f"  Баннер: {'есть' if cfg.banner_b64 else 'нет'}")
        
        report.append("\n[СЕТЬ]")
        report.append(f"  Режим: {cfg.connection_mode}")
        report.append(f"  UDP: {cfg.udp_port} TCP: {cfg.tcp_port}")
        report.append(f"  IP: {get_local_ip()}")
        all_ips = get_all_local_ips()
        report.append(f"  Все IP: {', '.join(all_ips) if all_ips else 'none'}")
        report.append(f"  Relay: {cfg.relay_enabled}")
        report.append(f"  Туннель: {'активен' if cfg.connection_mode == 'internet' else 'выключен'}")
        report.append(f"  MTU: {cfg.get('mtu_size', 1400)}")
        
        try:
            from PyQt6.QtNetwork import QNetworkInterface
            for iface in QNetworkInterface.allInterfaces():
                if iface.flags() & QNetworkInterface.InterfaceFlag.IsUp:
                    ips = [e.ip().toString() for e in iface.addressEntries()]
                    report.append(f"  {iface.name()}: {iface.hardwareAddress()} {', '.join(ips)}")
        except:
            pass
        
        if hasattr(self, 'net') and self.net:
            nm = self.net
            report.append(f"\n[ПИРЫ] ({len(nm.peers)} online)")
            for ip, peer in nm.peers.items():
                report.append(f"  {peer.get('username','?')} @ {ip} E2E:{peer.get('e2e',False)}")
            report.append(f"  Voice conns: {len(nm._voice_cons)}")
            report.append(f"  Relay sock: {nm._relay_sock is not None if hasattr(nm, '_relay_sock') else 'N/A'}")
            report.append(f"  Relay connected: {getattr(nm, '_relay_connected', False)}")
        
        report.append("\n[АУДИО]")
        report.append(f"  pyaudio: {PYAUDIO_AVAILABLE}")
        report.append(f"  cryptography: {CRYPTO_AVAILABLE}")
        report.append(f"  VAD: {cfg.get('vad_enabled', True)}")
        report.append(f"  Jitter: {cfg.get('jitter_frames', 6)}")
        if hasattr(self, 'voice'):
            report.append(f"  Звонки: {len(self.voice.active)}")
            report.append(f"  Микрофон: {'выкл' if self.voice.is_muted else 'вкл'}")
        
        report.append("\n[КРИПТОГРАФИЯ]")
        report.append(f"  Статус: {CRYPTO.status()}")
        report.append(f"  Отпечаток: {CRYPTO.fingerprint()}")
        report.append(f"  Шифрование: {'вкл' if cfg.encryption_enabled else 'выкл'}")
        report.append(f"  Vault: {'OK' if VAULT.vault_exists() else 'нет'}")
        
        report.append("\n[ГРУППЫ]")
        report.append(f"  Всего: {len(GROUPS.groups)}")
        
        report.append("\n[ЗВУКИ]")
        try:
            n = 0
            for d in _get_sound_dirs():
                if d.exists():
                    n += len(list(d.glob("*.wav"))) + len(list(d.glob("*.mp3")))
            report.append(f"  Файлов: {n}")
        except:
            pass
        
        report.append("\n[ПУТИ]")
        report.append(f"  Данные: {DATA_DIR}")
        report.append(f"  Файлы: {RECEIVED_DIR}")
        report.append(f"  История: {HISTORY_DIR}")
        
        report.append("\n" + "=" * 60)
        report.append("Конец отчёта")
        
        report_text = "\n".join(report)
        fn = f"goidaphone_report_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
        fp = os.path.join(os.path.expanduser("~"), fn)
        with open(fp, "w", encoding="utf-8") as f:
            f.write(report_text)
        
        from PyQt6.QtWidgets import QMessageBox
        QMessageBox.information(self, "Отчёт", f"Отчёт сохранён:\n{fp}\n\nОтправьте его команде Winora для анализа.")

        report.append(f"  Все IP: {', '.join(all_ips)}")
    def _about(self):
        """Open about page as inline tab with scroll."""
        for i in range(self._tabs.count()):
            if TR("menu_about") in self._tabs.tabText(i):
                self._tabs.setCurrentIndex(i); return

        t = get_theme(S().theme)

        # Outer widget with scroll
        outer = QWidget()
        outer_lay = QVBoxLayout(outer)
        outer_lay.setContentsMargins(0, 0, 0, 0)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setStyleSheet("QScrollArea{background:transparent;border:none;}"
                             "QScrollBar:vertical{width:6px;background:transparent;}"
                             f"QScrollBar::handle:vertical{{background:{t['border']};border-radius:3px;}}")

        inner = QWidget()
        inner.setStyleSheet("background:transparent;")
        lay = QVBoxLayout(inner)
        lay.setContentsMargins(32, 24, 32, 32)
        lay.setSpacing(14)

        # Logo
        title_lbl = QLabel(f"{APP_NAME}")
        title_lbl.setStyleSheet(
            f"font-size:26px;font-weight:bold;color:{t['accent']};background:transparent;")
        title_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(title_lbl)

        ver_lbl = QLabel(f"v{APP_VERSION}  •  {COMPANY_NAME}")
        ver_lbl.setStyleSheet(f"font-size:11px;color:{t['text_dim']};background:transparent;")
        ver_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lay.addWidget(ver_lbl)

        sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"color:{t['border']};"); lay.addWidget(sep)

        desc = QLabel(
            "🚀 <b>GoidaPhone</b> — P2P messenger for LAN and VPN.<br>"
            "No servers. No registration. No tracking.<br>"
            "Works on any LAN: home, corporate, Hamachi, Radmin VPN.")
        desc.setTextFormat(Qt.TextFormat.RichText)
        desc.setWordWrap(True)
        desc.setAlignment(Qt.AlignmentFlag.AlignCenter)
        desc.setStyleSheet(f"font-size:12px;color:{t['text']};background:transparent;")
        lay.addWidget(desc)

        # Features
        feat_gb = QGroupBox(_L("Возможности","Features","機能"))
        feat_gb.setStyleSheet(
            f"QGroupBox{{color:{t['accent']};border:1px solid {t['border']};"
            f"border-radius:8px;margin-top:8px;padding:10px;}}"
            f"QGroupBox::title{{subcontrol-origin:margin;left:10px;color:{t['accent']};}}")
        feat_lay = QVBoxLayout(feat_gb)
        feat_lay.setSpacing(3)
        for feat in [
            "💬  " + _L("Текстовые чаты, группы, @упоминания","Text chats, groups, @mentions","チャット、グループ"),
            "📞  " + _L("P2P голосовые звонки (VAD, шумодав)","P2P voice calls (VAD, noise suppression)","P2P音声通話"),
            "📎  " + _L("Передача файлов, изображений, стикеров","File transfer, images, stickers","ファイル転送"),
            "😊  " + _L("Реакции, пересылка, ответы","Reactions, forwarding, replies","リアクション、転送"),
            "📝  " + _L("Заметки с автосохранением","Notes with auto-save","自動保存メモ"),
            f"🎨  {len(THEMES)} " + _L("тем оформления","themes","テーマ"),
            "👑  Premium: " + _L("цвет ника, эмодзи, кастомные темы","nick color, emoji, custom themes","ニック色、絵文字"),
            "🔒  " + _L("PIN-блокировка, права администратора","PIN lock, admin rights","PINロック"),
            "💻  Windows & Linux  •  🌍 RU / EN / JA",
        ]:
            lbl = QLabel(feat)
            lbl.setWordWrap(True)
            lbl.setStyleSheet(f"font-size:11px;color:{t['text']};background:transparent;")
            feat_lay.addWidget(lbl)
        lay.addWidget(feat_gb)

        tech_lbl = QLabel(
            f"Tech: PyQt6 • UDP/TCP • PyAudio • WebRTC VAD<br>"
            f"Protocol: v{PROTOCOL_VERSION}  •  Python {platform.python_version()}")
        tech_lbl.setTextFormat(Qt.TextFormat.RichText)
        tech_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        tech_lbl.setStyleSheet(f"font-size:10px;color:{t['text_dim']};background:transparent;")
        lay.addWidget(tech_lbl)

        sep2 = QFrame(); sep2.setFrameShape(QFrame.Shape.HLine)
        sep2.setStyleSheet(f"color:{t['border']};"); lay.addWidget(sep2)

        # Credits — полностью прокручиваемые
        credits_gb = QGroupBox("💙 Credits")
        credits_gb.setStyleSheet(
            f"QGroupBox{{color:{t['accent']};border:1px solid {t['border']};"
            f"border-radius:10px;margin-top:8px;padding:12px;}}"
            f"QGroupBox::title{{subcontrol-origin:margin;left:12px;color:{t['accent']};}}")
        cred_lay = QVBoxLayout(credits_gb)
        cred_lay.setSpacing(10)

        def _section(title, names, color=None):
            sec = QLabel(f"<b>{title}</b>")
            sec.setTextFormat(Qt.TextFormat.RichText)
            sec.setStyleSheet(
                f"font-size:11px;color:{color or t['accent']};background:transparent;margin-top:4px;")
            sec.setWordWrap(True)
            cred_lay.addWidget(sec)
            for name in names:
                row = QLabel(f"  ◆  {name}")
                row.setStyleSheet(f"font-size:11px;color:{t['text']};background:transparent;")
                cred_lay.addWidget(row)

        _section("🛠 Developers",        ["Щербинин Матвей","Давид Юзефович"], t['accent'])
        _section("💜 Help developing",   ["Андрей Хромов","Демид Черепанов","Давид Юзефович"])
        _section("💰 Financial support", ["Давид Юзефович","Демид Черепанов","Андрей Хромов"])
        _section("📢 Promotion",         ["Матвей Нечаев","Winora Racing Team"])
        _section("🧪 Testers",           [
            "Давид Юзефович","Матвей Щербинин","Матвей Нечаев","Демид Черепанов",
            "Дима Юзефович","Андрей Хромов","Николай Ходырев",
            "Михаил Посланников","Егор Васёв","Алексей Васёв"])
        _section("🙏 Special thanks",    [
            "Николай Ходырев","Егор Васёв","Михаил Посланников","Алексей Васёв"])

        lay.addWidget(credits_gb)
        lay.addStretch()

        scroll.setWidget(inner)
        outer_lay.addWidget(scroll)

        idx = self._tabs.addTab(outer, "ℹ " + _L("О программе","About","概要"))
        self._tabs.setCurrentIndex(idx)
        self._add_tab_close_btn(idx)

    def _check_updates_quick(self):
        """Safe update check — keep reference on self to prevent GC crash."""
        self._upd_checker = UpdateChecker()
        self._upd_checker.update_available.connect(
            lambda v, d: _show_update_dialog(v, d, self))
        self._upd_checker.no_update.connect(
            lambda: QMessageBox.information(self, "Обновления",
                f"✅ У вас актуальная версия {APP_VERSION}.\nОбновлений не найдено."))
        self._upd_checker.check_failed.connect(
            lambda e: QMessageBox.information(self, "Обновления",
                f"ℹ GitHub не настроен или no соединения.\n\n"
                f"Для настройки обновлений: вставьте ваш GitHub repo\n"
                f"в константу GITHUB_REPO в начале файла.\n\n"
                f"Детали: {e[:200]}"))
        self._upd_checker.start()

    # ── Window events ───────────────────────────────────────────────────
    def _refresh_weather(self):
        """Fetch weather from wttr.in."""
        import threading, urllib.request
        def _fetch():
            try:
                req = urllib.request.Request(
                    "https://wttr.in/?format=%l:+%c+%t",
                    headers={"User-Agent": "curl/7.0"})
                with urllib.request.urlopen(req, timeout=5) as resp:
                    txt = resp.read().decode("utf-8").strip()
                QTimer.singleShot(0, lambda t=txt:
                    self._weather_lbl.setText(f"🌍 {t}")
                    if hasattr(self, "_weather_lbl") else None)
            except Exception:
                pass
        threading.Thread(target=_fetch, daemon=True).start()
        QTimer.singleShot(30*60*1000, self._refresh_weather)

    def closeEvent(self, event):
        # Сворачиваем в трей вместо закрытия (если трей доступен)
        if (hasattr(self, '_tray') and self._tray and
                self._tray.isVisible() and
                not getattr(self, '_force_quit', False)):
            event.ignore()
            self.hide()
            self._tray.showMessage(
                APP_NAME,
                "GoidaPhone свёрнут в трей. Двойной клик — открыть. Выход → ПКМ на иконке.",
                QSystemTrayIcon.MessageIcon.Information, 3000)
            return

        # Реальный выход
        S().set("window_geometry", self.saveGeometry())
        S().set("window_state", self.saveState())
        self.notes_widget._save()
        self.voice.cleanup()
        # Layer 3: Secure Wipe — обнуляем RAM перед выходом
        if S().get("crypto_layer3_wipe", False, t=bool):
            try:
                _pp = S().get("encryption_passphrase", "", t=str)
                if _pp:
                    SecureMemory(_pp).wipe()
                VAULT.lock()
            except Exception:
                pass
        self.net.stop()
        event.accept()

# ═══════════════════════════════════════════════════════════════════════════
#  ENTRY POINT
# ═══════════════════════════════════════════════════════════════════════════
# ═══════════════════════════════════════════════════════════════════════════
#  GOIDA DEATH SCREEN  — синий экран смерти GoidaPhone
# ═══════════════════════════════════════════════════════════════════════════
class GoidaDeathScreen(QWidget):
    """BSOD with QR, auto-restart 30s, halts all windows."""
    ERROR_CODES = {
        "EXCEPTION":     "UNHANDLED_EXCEPTION",
        "MANUAL":        "MANUALLY_TRIGGERED_0xDEADBEEF",
        "THREAD_CRASH":  "THREAD_POOL_EXHAUSTED",
        "AUDIO_FAULT":   "AUDIO_ENGINE_FAULT",
        "NETWORK_FAULT": "NETWORK_SUBSYSTEM_FAULT",
    }
    DEVELOPER_EMAIL = "mymygang0078@gmail.com"

    def __init__(self, exc_type=None, exc_value=None, exc_tb=None,
                 manual: bool = False, parent=None, inside_window: bool = False):
        if inside_window and parent is not None:
            # Show as overlay inside the parent window
            super().__init__(parent)
            self.setWindowFlags(Qt.WindowType.Widget)
            # Resize to fill parent
            self.resize(parent.size())
            parent.resizeEvent = lambda e, _s=self, _p=parent: (
                super(type(parent), parent).resizeEvent(e),
                _s.resize(_p.size()))
        else:
            super().__init__(None)
            self.setWindowFlags(
                Qt.WindowType.FramelessWindowHint |
                Qt.WindowType.WindowStaysOnTopHint |
                Qt.WindowType.Window)
        self._manual = manual
        self._countdown = 30
        self.setAttribute(Qt.WidgetAttribute.WA_StyledBackground)
        self.setStyleSheet("background:#0032A0;")
        self._inside_window = inside_window

        if exc_type is not None:
            tb_str = "".join(traceback.format_exception(exc_type, exc_value, exc_tb))
        else:
            tb_str = "".join(traceback.format_stack()[:-1])

        error_code = self.ERROR_CODES["MANUAL" if manual else "EXCEPTION"]
        ts = time.strftime("%Y-%m-%d %H:%M:%S")
        self._tb_str = tb_str
        self._error_code = error_code
        self._ts = ts

        lay = QVBoxLayout(self)
        lay.setContentsMargins(60, 48, 60, 36)
        lay.setSpacing(0)

        sad = QLabel(":(")
        sad.setStyleSheet(
            "font-size:96px;font-weight:900;color:white;background:transparent;")
        lay.addWidget(sad)
        lay.addSpacing(14)

        title = QLabel("GoidaPhone encountered a problem and stopped responding.")
        title.setStyleSheet(
            "font-size:26px;font-weight:bold;color:white;background:transparent;")
        title.setWordWrap(True)
        lay.addWidget(title)
        lay.addSpacing(14)

        if manual:
            desc_text = "Death screen triggered manually (Ctrl+F12). Press Return."
        else:
            desc_text = ("Collecting error information. "
                         "Scan QR code to send the report to the developer.")
        # Always show QR (even manual) - useful for reporting test crashes
        desc = QLabel(desc_text)
        desc.setStyleSheet("font-size:14px;color:#CCE0FF;background:transparent;")
        desc.setWordWrap(True)
        lay.addWidget(desc)
        lay.addSpacing(18)

        # Content row
        content_row = QHBoxLayout()
        content_row.setSpacing(36)
        left_col = QVBoxLayout()
        left_col.setSpacing(6)

        for lbl_text, style in [
            (f"Error code: {error_code}",
             "font-size:13px;font-weight:bold;color:white;background:transparent;"),
            (f"Time: {ts}",
             "font-size:11px;color:#A0C0FF;background:transparent;"),
            (f"GoidaPhone v{APP_VERSION}  |  Python {sys.version.split()[0]}  |  "
             f"{platform.system()} {platform.release()}",
             "font-size:10px;color:#8AAAEE;background:transparent;"),
        ]:
            lbl_w = QLabel(lbl_text)
            lbl_w.setStyleSheet(style)
            lbl_w.setWordWrap(True)
            left_col.addWidget(lbl_w)

        left_col.addSpacing(10)
        tb_hdr = QLabel("Stack trace:")
        tb_hdr.setStyleSheet(
            "font-size:11px;font-weight:bold;color:#A0C8FF;background:transparent;")
        left_col.addWidget(tb_hdr)

        tb_box = QPlainTextEdit(tb_str)
        tb_box.setReadOnly(True)
        tb_box.setMaximumHeight(160)
        tb_box.setStyleSheet(
            "QPlainTextEdit{background:#001870;color:#A8C8FF;"
            "border:1px solid #4060C0;border-radius:6px;"
            "font-family:monospace;font-size:10px;padding:8px;}")
        left_col.addWidget(tb_box)
        content_row.addLayout(left_col, stretch=1)

        if True:  # Always show QR
            right_col = QVBoxLayout()
            right_col.setAlignment(
                Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignHCenter)
            right_col.setSpacing(8)
            qr_title = QLabel("📧 Send report")
            qr_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
            qr_title.setStyleSheet(
                "font-size:11px;font-weight:bold;color:#CCE0FF;background:transparent;")
            right_col.addWidget(qr_title)
            self._qr_img = QLabel()
            self._qr_img.setFixedSize(160, 160)
            self._qr_img.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self._qr_img.setStyleSheet(
                "background:white;border-radius:8px;border:3px solid #4060C0;"
                "color:#0032A0;font-size:9px;")
            self._qr_img.setText("Generating...")
            right_col.addWidget(self._qr_img)
            qr_hint = QLabel(self.DEVELOPER_EMAIL)
            qr_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
            qr_hint.setStyleSheet("font-size:9px;color:#8AAAEE;background:transparent;")
            right_col.addWidget(qr_hint)
            content_row.addLayout(right_col)
            QTimer.singleShot(200, lambda: self._generate_qr(tb_str, error_code, ts))

        lay.addLayout(content_row)
        lay.addSpacing(16)

        if not manual:
            self._countdown_lbl = QLabel(
                f"⟳  Auto-restart in {self._countdown} sec.")
            self._countdown_lbl.setStyleSheet(
                "font-size:12px;color:#FFD080;background:transparent;font-weight:bold;")
            lay.addWidget(self._countdown_lbl)
            lay.addSpacing(10)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(10)

        def _make_btn(text, bg, hover_bg, cb):
            b = QPushButton(text)
            b.setFixedHeight(40)
            b.setStyleSheet(
                f"QPushButton{{background:{bg};color:white;border-radius:8px;"
                f"font-size:12px;font-weight:bold;padding:0 18px;border:none;}}"
                f"QPushButton:hover{{background:{hover_bg};}}")
            b.clicked.connect(cb)
            return b

        if not manual:
            btn_row.addWidget(
                _make_btn("🔄 Restart", "#005522", "#007733", self._do_restart))
        btn_row.addWidget(
            _make_btn("💾 Save report", "#0050CC", "#0066EE",
                      lambda: self._save_report(tb_str, error_code, ts)))
        btn_row.addWidget(
            _make_btn(_L("📋 Copy", "📋 Copy", "📋 コピー"), "#0050CC", "#0066EE",
                      lambda: QApplication.clipboard().setText(
                          f"GoidaPhone Death Report\n{ts}\n{error_code}\n\n{tb_str}")))
        if manual:
            btn_row.addWidget(
                _make_btn("↩ Return", "#005522", "#007733", self.close))
        else:
            btn_row.addWidget(
                _make_btn("⏻ Close", "#550000", "#880000", QApplication.quit))
        btn_row.addStretch()
        lay.addLayout(btn_row)
        lay.addStretch()

        # Show: inside window or fullscreen
        if getattr(self, '_inside_window', False) and self.parent():
            self.setGeometry(0, 0,
                self.parent().width(), self.parent().height())
        else:
            self.setGeometry(QApplication.primaryScreen().geometry())
        self.show(); self.raise_(); self.activateWindow()
        if not manual:
            # Hide other top-level windows (not the parent)
            _parent = self.parent()
            for w in QApplication.topLevelWidgets():
                if w is not self and w is not _parent:
                    try: w.hide()
                    except Exception: pass
            self._timer = QTimer(self)
            self._timer.timeout.connect(self._tick)
            self._timer.start(1000)

    def _tick(self):
        self._countdown -= 1
        if self._countdown <= 0:
            self._timer.stop()
            self._do_restart()
        else:
            color = "#FF6030" if self._countdown <= 10 else "#FFD080"
            self._countdown_lbl.setText(
                f"⟳  Auto-restart in {self._countdown} sec.")
            self._countdown_lbl.setStyleSheet(
                f"font-size:12px;color:{color};"
                "background:transparent;font-weight:bold;")

    def _do_restart(self):
        try:
            if hasattr(self, '_timer'): self._timer.stop()
        except Exception: pass
        try:
            os.execv(sys.executable, [sys.executable] + sys.argv)
        except Exception:
            QApplication.quit()

    def _generate_qr(self, tb: str, code: str, ts: str):
        import urllib.parse
        subject = f"GoidaPhone Crash — {code}"
        body = (f"GoidaPhone v{APP_VERSION}\n"
                f"Time: {ts}\n"
                f"Platform: {platform.system()} {platform.release()}\n"
                f"Python: {sys.version.split()[0]}\n\n"
                f"Error: {code}\n\nTraceback:\n{tb[:1200]}")
        mailto = (f"mailto:{self.DEVELOPER_EMAIL}"
                  f"?subject={urllib.parse.quote(subject)}"
                  f"&body={urllib.parse.quote(body)}")
        try:
            import qrcode as _qr
            from io import BytesIO
            qr = _qr.QRCode(box_size=4, border=2)
            qr.add_data(mailto)
            qr.make(fit=True)
            img = qr.make_image(fill_color="black", back_color="white")
            buf = BytesIO()
            img.save(buf, format="PNG")
            pm = QPixmap()
            pm.loadFromData(buf.getvalue())
            pm = pm.scaled(152, 152,
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation)
            self._qr_img.setPixmap(pm)
            self._qr_img.setToolTip(mailto)
        except ImportError:
            pm = QPixmap(152, 152)
            pm.fill(QColor("white"))
            from PyQt6.QtGui import QPainter, QFont as _QF
            p = QPainter(pm)
            for rx,ry,rw,rh,rc in [
                (4,4,36,36,"#000"),(8,8,28,28,"#fff"),(12,12,20,20,"#000"),
                (112,4,36,36,"#000"),(116,8,28,28,"#fff"),(120,12,20,20,"#000"),
                (4,112,36,36,"#000"),(8,116,28,28,"#fff"),(12,120,20,20,"#000"),
            ]:
                p.fillRect(rx,ry,rw,rh,QColor(rc))
            p.setPen(QColor("#0032A0"))
            p.setFont(_QF("monospace", 7))
            p.drawText(pm.rect(), Qt.AlignmentFlag.AlignCenter,
                       "pip install\nqrcode Pillow\nдля QR")
            p.end()
            self._qr_img.setPixmap(pm)
            self._qr_img.setToolTip(
                "pip install qrcode Pillow --break-system-packages")
        except Exception as e:
            self._qr_img.setText(f"Err: {e}")

    @staticmethod
    def _save_report(tb: str, code: str, ts: str):
        fn, _ = QFileDialog.getSaveFileName(
            None, "Сохранить отчёт",
            f"goidaphone_crash_{ts.replace(':','-').replace(' ','_')}.txt",
            "Text (*.txt)")
        if fn:
            try:
                open(fn, "w", encoding="utf-8").write(
                    f"GoidaPhone v{APP_VERSION} — Crash Report\n"
                    f"Time: {ts}\nCode: {code}\n"
                    f"Platform: {platform.system()} {platform.release()}\n"
                    f"Python: {sys.version}\n\nTraceback:\n{tb}\n")
                QMessageBox.information(None, "Сохранено", f"Отчёт: {fn}")
            except Exception as e:
                QMessageBox.warning(None, "Error", str(e))


def _install_death_screen_handler(window: "MainWindow"):
    """Install global exception hook + Ctrl+F12 shortcut."""
    _orig_excepthook = sys.excepthook

    def _excepthook(exc_type, exc_value, exc_tb):
        if issubclass(exc_type, KeyboardInterrupt):
            _orig_excepthook(exc_type, exc_value, exc_tb)
            return
        traceback.print_exception(exc_type, exc_value, exc_tb)
        try:
            # Show INSIDE the main window as overlay
            ds = GoidaDeathScreen(exc_type, exc_value, exc_tb,
                                  manual=False, parent=window,
                                  inside_window=True)
            ds.show(); ds.raise_()
        except Exception as e2:
            print(f"[BSOD render error] {e2}")
            _orig_excepthook(exc_type, exc_value, exc_tb)

    sys.excepthook = _excepthook

    # Ctrl+F12 — manual death screen (also inside window, with QR)
    sc = QShortcut(QKeySequence("Ctrl+F12"), window)
    sc.activated.connect(lambda: _show_manual_death(window))


def _show_manual_death(parent):
    ds = GoidaDeathScreen(manual=True, parent=parent, inside_window=True)
    ds.show(); ds.raise_()


def main():
    # ── Suppress ALSA/JACK noise on Linux ────────────────────────────────
    # Use environment variables only — never redirect fd 2 (stderr) because
    # that silences Python tracebacks and makes crashes invisible.
    if platform.system() == "Linux":
        import os as _os
        # ── ALSA: глушим спам про неизвестные PCM устройства ─────────────────
        _os.environ["ALSA_IGNORE_UCM"]       = "1"
        _os.environ["LIBASOUND_THREAD_SAFE"] = "0"
        # ALSA: suppress via env vars and ~/.asoundrc
        _os.environ["ALSA_IGNORE_UCM"]       = "1"
        _os.environ["LIBASOUND_THREAD_SAFE"] = "0"
        # Создаём minimal ~/.asoundrc если no — глушит Unknown PCM спам
        try:
            _asoundrc = Path.home() / ".asoundrc"
            if not _asoundrc.exists():
                _asoundrc.write_text("pcm.!default {type hw;card 0}\nctl.!default {type hw;card 0}\n")
        except Exception:
            pass

        # ── FFmpeg/Vulkan спам ────────────────────────────────────────────────
        _os.environ["FFREPORT"]              = ""
        _os.environ["AV_LOG_FORCE_NOCOLOR"]  = "1"
        _os.environ["LIBVA_MESSAGING_LEVEL"] = "0"
        # Отключаем Vulkan HW decode — no смысла на большинстве систем
        _os.environ.setdefault("LIBVA_DRIVER_NAME", "iHD")

        # ── Qt CSS warnings ("Could not parse stylesheet") ────────────────────
        # Qt6 correct category names:
        _os.environ["QT_LOGGING_RULES"] = (
            "*.debug=false;"
            "qt.qpa.stylesheet=false;"
            "qt.qpa.fonts=false;"
            "qt.widgets.stylesheet=false;"
            "qt.core.logging=false;"
            "qt.multimedia*=false;"
            "ffmpeg*=false;"
        )
        # Also via QLoggingCategory after QApplication:
        # called below after app = QApplication(...)

        # ── Auto-find QtWebEngineProcess (pip user-install) ───────────
        # Must be set BEFORE QApplication is created
        if not _os.environ.get("QTWEBENGINEPROCESS_PATH"):
            import glob as _gl
            _candidates = _gl.glob(
                _os.path.expanduser(
                    "~/.local/lib/python*/site-packages/PyQt6/Qt6/libexec/QtWebEngineProcess"))
            if _candidates:
                _os.environ["QTWEBENGINEPROCESS_PATH"] = _candidates[0]
                # Also set Qt plugin path so WebEngine finds its resources
                _qt6_dir = _os.path.dirname(_os.path.dirname(_candidates[0]))
                if _os.path.isdir(_qt6_dir):
                    _os.environ.setdefault("QTWEBENGINE_CHROMIUM_FLAGS",
                                           "--disable-gpu-sandbox")
                    # Add pip Qt6 to library search path
                    _lib = _os.path.join(_qt6_dir, "lib")
                    if _os.path.isdir(_lib):
                        old_ld = _os.environ.get("LD_LIBRARY_PATH","")
                        _os.environ["LD_LIBRARY_PATH"] = (
                            _lib + (":" + old_ld if old_ld else ""))
                print(f"[WNS] QtWebEngineProcess: {_candidates[0]}")
                # Also set resources path — required for WebEngine to find icudtl.dat etc.
                _qt6_root = _os.path.join(
                    _os.path.dirname(_os.path.dirname(_candidates[0])))
                # Resources are typically in Qt6/resources/
                for _res_sub in ["resources", ".", "translations"]:
                    _res_path = _os.path.join(_qt6_root, _res_sub)
                    if _os.path.isfile(_os.path.join(_res_path, "qtwebengine_resources.pak")) or \
                       _os.path.isfile(_os.path.join(_res_path, "icudtl.dat")):
                        _os.environ.setdefault(
                            "QTWEBENGINE_RESOURCES_PATH", _res_path)
                        print(f"[WNS] resources: {_res_path}")
                        break
                else:
                    _os.environ.setdefault(
                        "QTWEBENGINE_RESOURCES_PATH", _qt6_root)
                    print(f"[WNS] resources fallback: {_qt6_root}")
                # Also set locales path
                for _loc_sub in ["translations/qtwebengine_locales",
                                  "qtwebengine_locales", "translations"]:
                    _loc_path = _os.path.join(_qt6_root, _loc_sub)
                    if _os.path.isdir(_loc_path):
                        _os.environ.setdefault(
                            "QTWEBENGINE_LOCALES_PATH", _loc_path)
                        print(f"[WNS] locales: {_loc_path}")
                        break

    # High-DPI — critical for Windows at 125%/150% scaling
    if hasattr(Qt.ApplicationAttribute, "AA_UseHighDpiPixmaps"):
        QApplication.setAttribute(Qt.ApplicationAttribute.AA_UseHighDpiPixmaps)
    if hasattr(Qt.ApplicationAttribute, "AA_EnableHighDpiScaling"):
        QApplication.setAttribute(Qt.ApplicationAttribute.AA_EnableHighDpiScaling)

    # ── QtWebEngine MUST be imported before QApplication ─────────────────────
    # This is a hard requirement from Qt — violating it gives ImportError
    try:
        from PyQt6.QtWebEngineWidgets import QWebEngineView as _WEV  # noqa
        from PyQt6.QtWebEngineCore import (                           # noqa
            QWebEngineProfile as _WEP,
            QWebEngineSettings as _WES,
        )
        print("[WNS] WebEngine pre-import OK")
    except Exception as _we_err:
        print(f"[WNS] WebEngine pre-import failed: {_we_err}")

    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)

    # ── Восстановить stderr после инициализации Qt/ALSA ─────────────────────
    if platform.system() == "Linux":
        try:
            import ctypes as _ct2
            _libc2 = _ct2.CDLL("libc.so.6")
            # Restore original stderr (Python errors visible again)
            # ALSA already initialized, no more spam
            if hasattr(_ct2, '_saved_stderr_fd'):
                _libc2.dup2(_ct2._saved_stderr_fd, 2)
        except Exception:
            pass

    # ── Заглушить CSS warnings programmatically (Qt6) ─────────────────────────
    try:
        from PyQt6.QtCore import QLoggingCategory
        QLoggingCategory.setFilterRules(
            "*.debug=false;"
            "qt.qpa.stylesheet=false;"
            "qt.qpa.fonts=false;"
            "qt.widgets.stylesheet=false;"
            "qt.core.logging=false;"
            "qt.multimedia*=false;"
            "ffmpeg*=false;"
        )
    except Exception:
        pass
    app.setApplicationVersion(APP_VERSION)
    app.setOrganizationName(COMPANY_NAME)

    # ── Force Fusion style for pixel-perfect cross-platform look ─────────────
    # Without this, Windows uses "windowsvista" style which ignores most QSS rules.
    # Fusion is Qt's own renderer — identical output on Linux, Windows, macOS.
    app.setStyle("Fusion")

    # ── Base font: apply user scale setting ───────────────────────────────────
    import platform as _plat
    _scale = S().get("app_scale", 100, t=int)
    _base_pt = max(7.0, 9.0 * _scale / 100.0)   # 9pt base ≈ old 11px on 96dpi
    _base_font = QFont()
    if _plat.system() == "Windows":
        _base_font.setFamily("Segoe UI")
    else:
        _base_font.setFamily("Ubuntu")
    _base_font.setPointSizeF(_base_pt)
    _base_font.setHintingPreference(QFont.HintingPreference.PreferFullHinting)
    app.setFont(_base_font)

    # ── Emoji font — platform aware ───────────────────────────────────────────

    # Install early crash handler — catches errors even before MainWindow
    def _early_excepthook(exc_type, exc_val, exc_tb):
        import traceback as _tb
        _tb.print_exception(exc_type, exc_val, exc_tb)
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_val, exc_tb)
            return
        try:
            ds = GoidaDeathScreen(exc_type, exc_val, exc_tb, manual=False)
            ds.resize(900, 600)
            ds.show()
            ds.raise_()
            app.exec()
        except Exception as e2:
            print(f"[FATAL] {e2}")
        sys.exit(1)
    sys.excepthook = _early_excepthook
    # Icon: first from settings, then icon.png
    _icon_b64 = S().get("app_icon_b64", "", t=str)
    if _icon_b64:
        try:
            _pm_ico = QPixmap()
            _pm_ico.loadFromData(base64.b64decode(_icon_b64))
            if not _pm_ico.isNull():
                app.setWindowIcon(QIcon(_pm_ico))
        except Exception:
            pass
    else:
        _icon_path = Path(__file__).parent / "icon.png"
        if _icon_path.exists():
            app.setWindowIcon(QIcon(str(_icon_path)))

    # Scale already applied in _base_font above

    # Apply theme before anything
    key = S().theme
    if not key.startswith("__custom"):
        app.setStyleSheet(build_stylesheet(get_theme(key)))

    # ── 1. Splash screen ──────────────────────────────────────────────────
    splash = None
    if S().get("show_splash", True, t=bool):
        splash = SplashScreen()
        splash.show()
        app.processEvents()

    # ── 2. Launcher screen (after splash, п.15) ───────────────────────────
    mode = S().get("launcher_default", "gui", t=str) if not S().show_launcher else "gui"

    def _continue_after_splash():
        nonlocal mode
        if S().show_launcher:
            if splash:
                splash.hide()
            launcher = LauncherScreen()
            launcher.exec()
            mode = launcher.get_choice()
        else:
            if splash:
                splash.close()

        if mode == "gcc":
            # Launch GoidaConstruct++ in terminal
            from gdf_gcc import run_gcc
            app.quit()
            run_gcc()
            return

        if mode == "cmd":
            try:
                import readline   # Linux/macOS — история команд в CMD
            except ImportError:
                pass              # Windows — works without it
            import shutil
            cols = shutil.get_terminal_size().columns

            # Enable ANSI colors on Windows terminal
            if platform.system() == "Windows":
                try:
                    import ctypes as _ct
                    kernel32 = _ct.windll.kernel32
                    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
                except Exception:
                    pass
            CYAN   = "\033[96m"; PURP = "\033[95m"; GRN  = "\033[92m"
            YEL    = "\033[93m"; RED  = "\033[91m"; DIM  = "\033[2m"
            BOLD   = "\033[1m";  RST  = "\033[0m";  BLUE = "\033[94m"

            def _line(ch="═"): return ch * min(cols, 62)
            def _hdr(txt):     print(f"{PURP}{_line()}{RST}\n"
                                     f"{BOLD}{CYAN}  {txt}{RST}\n"
                                     f"{PURP}{_line()}{RST}")

            print(f"\n{PURP}{'═'*62}{RST}")
            print(f"{BOLD}{CYAN}  ██████╗  ██████╗ ██╗██████╗  █████╗ {RST}")
            print(f"{CYAN}  ██╔════╝ ██╔═══██╗██║██╔══██╗██╔══██╗{RST}")
            print(f"{CYAN}  ██║  ███╗██║   ██║██║██║  ██║███████║{RST}")
            print(f"{CYAN}  ██║   ██║██║   ██║██║██║  ██║██╔══██║{RST}")
            print(f"{CYAN}  ╚██████╔╝╚██████╔╝██║██████╔╝██║  ██║{RST}")
            print(f"{CYAN}   ╚═════╝  ╚═════╝ ╚═╝╚═════╝ ╚═╝  ╚═╝{RST}")
            print(f"{PURP}{'═'*62}{RST}")
            print(f"  {BOLD}GoidaPhone v{APP_VERSION} — CMD Mode{RST}  {DIM}Winora Company{RST}")
            print(f"  {DIM}Type /help for commands  •  /quit to exit{RST}")
            print(f"{PURP}{'═'*62}{RST}\n")

            net = NetworkManager()

            # Incoming message handler
            _msg_buf: list[str] = []
            def _on_msg(msg: dict):
                t_msg  = msg.get("type","")
                uname  = msg.get("username","?")
                text   = msg.get("text","")
                ts     = time.strftime("%H:%M", time.localtime(msg.get("ts", time.time())))
                if t_msg in ("chat","msg","private","group") and text:
                    prefix = f"{DIM}[{ts}]{RST} "
                    if msg.get("type") == "private":
                        line = f"{prefix}{YEL}[DM] {BOLD}{uname}{RST}: {text}"
                    else:
                        line = f"{prefix}{GRN}{BOLD}{uname}{RST}: {text}"
                    print(f"\r{line}")
                    print(f"{CYAN}> {RST}", end="", flush=True)
                    _msg_buf.append(line)

            net.sig_message.connect(_on_msg)

            def _on_call(caller, ip):
                print(f"\r{YEL}📞 Incoming call from {BOLD}{caller}{RST}{YEL} ({ip}) — /answer {ip} or /reject {ip}{RST}")
                print(f"{CYAN}> {RST}", end="", flush=True)

            net.sig_call_request.connect(_on_call)

            def _on_online(peer):
                uname = peer.get("username","?")
                ip    = peer.get("ip","?")
                print(f"\r{GRN}● {uname} ({ip}) online{RST}")
                print(f"{CYAN}> {RST}", end="", flush=True)

            def _on_offline(ip):
                peer = net.peers.get(ip, {})
                print(f"\r{DIM}○ {peer.get('username', ip)} offline{RST}")
                print(f"{CYAN}> {RST}", end="", flush=True)

            net.sig_user_online.connect(_on_online)
            net.sig_user_offline.connect(_on_offline)

            net.start()
            print(f"{GRN}✓ Started on {BOLD}{get_local_ip()}{RST}")
            print(f"{GRN}✓ Name: {BOLD}{S().username}{RST}\n")

            _help_text = f"""
{PURP}{'─'*56}{RST}
{BOLD}  GoidaPhone CMD Mode v{APP_VERSION}{RST}  {DIM}Winora Company{RST}
{PURP}{'─'*56}{RST}

{BOLD}{CYAN}GENERAL{RST}
  {CYAN}/help{RST}                — this help
  {CYAN}/quit{RST} · {CYAN}/exit{RST}       — quit
  {CYAN}/clear{RST}               — clear screen
  {CYAN}/me{RST}                  — my info

{BOLD}{CYAN}NETWORK{RST}
  {CYAN}/peers{RST}               — online users
  {CYAN}/ping <ip>{RST}           — ping
  {CYAN}/whois <ip>{RST}          — user details
  {CYAN}/groups{RST}              — list groups

{BOLD}{CYAN}CHAT{RST}
  {CYAN}/pub <text>{RST}         — public chat
  {CYAN}/msg <ip> <text>{RST}    — private message
  {CYAN}/gmsg <gid> <text>{RST}  — group message
  {CYAN}/history [ip]{RST}        — history (public or with ip)

{BOLD}{CYAN}CALLS{RST}
  {CYAN}/call <ip>{RST}           — call
  {CYAN}/hangup [ip]{RST}         — end call
  {CYAN}/answer <ip>{RST}         — accept incoming
  {CYAN}/reject <ip>{RST}         — reject incoming
  {CYAN}/mute{RST}                — mute/unmute mic

{BOLD}{CYAN}PROFILE{RST}
  {CYAN}/status <text>{RST}      — set status
  {CYAN}/nick <name>{RST}          — change nick
  {CYAN}/theme <theme>{RST}        — change theme
  {CYAN}/themes{RST}              — list themes

{BOLD}{CYAN}SYSTEM{RST}
  {CYAN}/stats{RST}               — network stats
  {CYAN}/crypto{RST}              — encryption status
  {CYAN}/uptime{RST}              — uptime
  {CYAN}/log [N]{RST}             — last N log lines

{DIM}Plain text → public chat{RST}
{PURP}{'─'*56}{RST}"""

            voice = VoiceCallManager(net)

            try:
                while True:
                    try:
                        raw = input(f"{CYAN}> {RST}").strip()
                    except EOFError:
                        break
                    if not raw:
                        continue
                    parts = raw.split(None, 2)
                    cmd   = parts[0].lower()

                    if cmd in ("/quit", "/exit", "/q"):
                        break

                    elif cmd == "/help":
                        print(_help_text)

                    elif cmd == "/clear":
                        print("\033[2J\033[H", end="")

                    elif cmd == "/me":
                        print(f"  {BOLD}Name:{RST}     {S().username}")
                        print(f"  {BOLD}IP:{RST}      {get_local_ip()}")
                        print(f"  {BOLD}Version:{RST}  {APP_VERSION}")
                        print(f"  {BOLD}Theme:{RST}    {S().theme}")

                    elif cmd == "/peers":
                        if net.peers:
                            print(f"  {DIM}Пользователи online ({len(net.peers)}):{RST}")
                            for ip, p in net.peers.items():
                                e2e = f"  {GRN}[E2E]{RST}" if p.get("e2e") else ""
                                print(f"  {GRN}●{RST} {BOLD}{p.get('username','?')}{RST} @ {ip}{e2e}")
                        else:
                            print(f"  {DIM}No users online{RST}")

                    elif cmd == "/groups":
                        groups = GROUPS.groups
                        if groups:
                            for gid, g in groups.items():
                                print(f"  {PURP}📂{RST} {g.get('name','?')}  {DIM}({gid}){RST}  "
                                      f"members: {len(g.get('members',[]))}")
                        else:
                            print(f"  {DIM}No groups{RST}")

                    elif cmd == "/msg" and len(parts) >= 3:
                        ip_arg = parts[1]; text = parts[2]
                        net.send_private(text, ip_arg)
                        print(f"  {DIM}→ DM → {ip_arg}: {text}{RST}")

                    elif cmd == "/pub" and len(parts) >= 2:
                        text = raw[5:].strip()
                        net.send_chat(text)
                        print(f"  {DIM}→ public chat: {text}{RST}")

                    elif cmd == "/ping" and len(parts) >= 2:
                        ip_arg = parts[1]
                        start  = time.time()
                        net.send_udp({"type": "ping", "username": S().username}, ip_arg)
                        print(f"  {DIM}Ping → {ip_arg}  (UDP){RST}")

                    elif cmd == "/call" and len(parts) >= 2:
                        ip_arg = parts[1]
                        if voice.call(ip_arg):
                            net.send_call_request(ip_arg)
                            print(f"  {YEL}📞 Calling {ip_arg}…{RST}")
                        else:
                            print(f"  {RED}✗ Failed to start audio{RST}")

                    elif cmd == "/hangup" and len(parts) >= 2:
                        ip_arg = parts[1]
                        voice.hangup(ip_arg)
                        print(f"  {DIM}Call with {ip_arg} ended{RST}")

                    elif cmd == "/answer" and len(parts) >= 2:
                        ip_arg = parts[1]
                        net.send_call_accept(ip_arg)
                        voice.call(ip_arg)
                        print(f"  {GRN}✓ Call accepted from {ip_arg}{RST}")

                    elif cmd == "/reject" and len(parts) >= 2:
                        ip_arg = parts[1]
                        net.send_call_reject(ip_arg)
                        print(f"  {DIM}Call rejected from {ip_arg}{RST}")

                    elif cmd == "/whois" and len(parts) >= 2:
                        ip_arg = parts[1]
                        p = net.peers.get(ip_arg, {})
                        if p:
                            print(f"  {BOLD}{p.get('username','?')}{RST} @ {ip_arg}")
                            print(f"  Status:   {p.get('status','online')}")
                            print(f"  Premium:  {'✦ yes' if p.get('premium') else 'no'}")
                            print(f"  Version:   {p.get('version','?')}")
                            print(f"  E2E:      {'✓' if p.get('e2e') else '✗'}")
                        else:
                            print(f"  {RED}User {ip_arg} not found{RST}")

                    elif cmd == "/nick" and len(parts) >= 2:
                        new_nick = parts[1]
                        S().username = new_nick
                        net._broadcast()
                        print(f"  {GRN}✓ Nick changed: {BOLD}{new_nick}{RST}")

                    elif cmd == "/theme" and len(parts) >= 2:
                        theme_name = parts[1]
                        if theme_name in THEMES:
                            S().set("theme", theme_name)
                            print(f"  {GRN}✓ Theme: {theme_name}{RST}  {DIM}(restart for GUI){RST}")
                        else:
                            print(f"  {RED}Theme not found. /themes — list{RST}")

                    elif cmd == "/themes":
                        print(f"  {CYAN}Available themes:{RST}")
                        for name, td in THEMES.items():
                            cur = " ◄ current" if name == S().theme else ""
                            print(f"  {DIM}•{RST} {name:<16} {td.get('label','')}{GRN}{cur}{RST}")

                    elif cmd == "/gmsg" and len(parts) >= 3:
                        gid = parts[1]; text = parts[2]
                        g = GROUPS.get(gid)
                        if g:
                            for ip in g.get('members', []):
                                if ip != get_local_ip():
                                    net.send_udp({"type": MSG_CHAT, "username": S().username,
                                                  "text": text, "gid": gid,
                                                  "ts": time.time()}, ip)
                            print(f"  {DIM}→ group {g.get('name','?')}: {text}{RST}")
                        else:
                            print(f"  {RED}Group {gid} not found{RST}")

                    elif cmd == "/stats":
                        print(f"  {CYAN}Статистика{RST}")
                        print(f"  Users online:  {len(net.peers)}")
                        print(f"  IP:                    {get_local_ip()}")
                        print(f"  Protocol:              v{PROTOCOL_VERSION}")
                        print(f"  Theme:                  {S().theme}")
                        print(f"  Премиум:               {'да' if S().premium else 'no'}")

                    elif cmd == "/crypto":
                        print(f"  {CYAN}Encryption{RST}")
                        print(f"  Algorithm:   AES-256-GCM + X25519 ECDH + Ed25519")
                        print(f"  Protocol:   v{PROTOCOL_VERSION}")
                        enc = S().encryption_enabled
                        print(f"  Status:     {'✓ enabled' if enc else '✗ disabled'}")
                        for ip, p in net.peers.items():
                            e2e = f"{GRN}[E2E]{RST}" if p.get('e2e') else f"{RED}[plain]{RST}"
                            print(f"  {p.get('username','?'):<16} {e2e}")

                    elif cmd == "/uptime":
                        import time as _tu
                        up = int(_tu.time() - _start_time) if '_start_time' in dir() else 0
                        h, m2 = divmod(up // 60, 60)
                        s2 = up % 60
                        print(f"  Uptime: {h:02d}:{m2:02d}:{s2:02d}")

                    elif cmd == "/log":
                        n_lines = int(parts[1]) if len(parts) >= 2 and parts[1].isdigit() else 20
                        log_file = DATA_DIR / "goidaphone.log"
                        if log_file.exists():
                            lines_list = log_file.read_text(encoding='utf-8', errors='replace').splitlines()
                            for ln in lines_list[-n_lines:]:
                                print(f"  {DIM}{ln}{RST}")
                        else:
                            print(f"  {DIM}Лог-файл не найден{RST}")

                    elif cmd == "/mute":
                        muted = voice.toggle_mute()
                        print(f"  {'🔇 Muted' if muted else '🎤 Microphone on'}")

                    elif cmd == "/status" and len(parts) >= 2:
                        text = raw[8:].strip()
                        S().set("status_text", text)
                        print(f"  {GRN}✓ Status: {text}{RST}")

                    elif cmd == "/history":
                        ip_arg = parts[1] if len(parts) >= 2 else None
                        hfile  = HISTORY_DIR / (f"private_{ip_arg}.json" if ip_arg else "public.json")
                        if hfile.exists():
                            import json as _j
                            msgs = _j.loads(hfile.read_text(encoding="utf-8"))[-20:]
                            for m in msgs:
                                ts  = time.strftime("%H:%M", time.localtime(m.get("ts",0)))
                                u   = m.get("username","?")
                                txt = m.get("text","")
                                print(f"  {DIM}[{ts}]{RST} {GRN}{u}{RST}: {txt}")
                        else:
                            print(f"  {DIM}История не найдена{RST}")

                    elif not cmd.startswith("/"):
                        # Plain text → public chat
                        net.send_chat(raw)
                        print(f"  {DIM}→ {raw}{RST}")

                    else:
                        print(f"  {RED}Unknown command: {cmd}  (введите /help){RST}")

                    app.processEvents()

            except KeyboardInterrupt:
                pass

            voice.cleanup()
            net.stop()
            print(f"\n{PURP}{'═'*62}{RST}")
            print(f"  {DIM}GoidaPhone stopped. Goodbye! 👋{RST}")
            print(f"{PURP}{'═'*62}{RST}")
            app.quit()
            return

        # ── 3. Startup sequence — initramfs/systemd style ─────────────
        import sys as _sys, time as _t, threading as _thr, os as _os2

        # ── ANSI colours ────────────────────────────────────────────────
        _R  = "\033[0m"
        _G  = "\033[32m"      # green
        _Y  = "\033[33m"      # yellow
        _C  = "\033[36m"      # cyan
        _RE = "\033[31m"      # red
        _B  = "\033[1m"       # bold
        _D  = "\033[2m"       # dim
        _W  = "\033[37m"      # white

        def _step(tag, color, msg, delay=0.055):
            _t.sleep(delay)
            print(f"  {color}[ {tag:^6} ]{_R}  {msg}")

        def _ok(msg,   d=0.05): _step(" OK ",  _G,  msg, d)
        def _info(msg, d=0.04): _step("INFO",  _C,  msg, d)
        def _warn(msg, d=0.04): _step(" !! ",  _Y,  msg, d)
        def _fail(msg, d=0.04): _step("FAIL",  _RE, msg, d)
        def _run(msg,  d=0.03): _step("INIT",  _D,  msg, d)

        W = 62   # line width

        # ── Braille spinner ─────────────────────────────────────────────
        _BRAILLE = ["⣾","⣽","⣻","⢿","⡿","⣟","⣯","⣷"]
        _spin_stop = _thr.Event()
        _spin_msg  = ["Initializing..."]

        def _spinner():
            i = 0
            while not _spin_stop.is_set():
                frame = _BRAILLE[i % len(_BRAILLE)]
                line  = f"\r  {_C}{frame}{_R}  {_D}{_spin_msg[0]}{_R}"
                _sys.stdout.write(line)
                _sys.stdout.flush()
                _t.sleep(0.08)
                i += 1
            _sys.stdout.write("\r" + " " * 72 + "\r")
            _sys.stdout.flush()

        _spin_thr = _thr.Thread(target=_spinner, daemon=True)

        # ── ASCII header ────────────────────────────────────────────────
        print()
        print(f"  {_B}{_G}{'─'*W}{_R}")
        print(f"  {_B}{_G}  GoidaPhone™ NT Server 1.8{_R}")
        print(f"  {_D}  Powered by Winora Company  ©  2026{_R}")
        print(f"  {_B}{_G}{'─'*W}{_R}")
        print()
        _t.sleep(0.1)

        # ── Kernel / environment ─────────────────────────────────────────
        _spin_msg[0] = "Проверка окружения..."
        _spin_thr.start()
        _t.sleep(0.3)
        _spin_stop.set(); _spin_thr.join(); _spin_stop.clear()
        _spin_thr = _thr.Thread(target=_spinner, daemon=True)

        import platform as _pl
        _ok(f"Kernel:          Python {_pl.python_version()}  |  {_pl.system()} {_pl.release()}")
        _ok(f"Architecture:    {_pl.machine()}")
        _ok(f"Qt framework:    PyQt6 + Fusion renderer")

        _t.sleep(0.06)
        _ok(f"Build:           GoidaPhone v{APP_VERSION}  [{__import__('time').strftime('%Y-%m-%d')}]")

        # ── Data & config ────────────────────────────────────────────────
        print()
        _run("Mounting data directory...")
        try:
            DATA_DIR.mkdir(parents=True, exist_ok=True)
            RECEIVED_DIR.mkdir(parents=True, exist_ok=True)
            _ok(f"Data dir:        {DATA_DIR}")
            _ok(f"Received files:  {RECEIVED_DIR}")
        except Exception as _e:
            _fail(f"Data dir:        {_e}")

        # ── Security layer ───────────────────────────────────────────────
        print()
        _run("Starting security subsystem...")
        _t.sleep(0.08)
        _ok("Crypto engine:   AES-256-GCM  +  X25519 ECDH  +  Ed25519")
        _ok("Replay guard:    HMAC-SHA256 nonce validation  [ACTIVE]")
        if S().encryption_enabled and S().encryption_passphrase:
            _ok("Passphrase enc:  [ENABLED]")
        else:
            _warn("Passphrase enc:  [DISABLED]  — enable in Settings → Security")
        _t.sleep(0.05)
        _ok(f"Protocol:        PROTOCOL_VERSION={PROTOCOL_VERSION}  COMPAT={PROTOCOL_COMPAT}")

        # ── Sound subsystem ──────────────────────────────────────────────
        print()
        _run("Loading sound subsystem...")
        _spin_msg[0] = "Loading sounds..."
        _spin_stop = _thr.Event()
        _spin_thr = _thr.Thread(target=_spinner, daemon=True)
        _spin_thr.start()
        try:
            install_sounds(force=True)
            _sound_files = []
            for _sd in _get_sound_dirs():
                if _sd.exists():
                    _sound_files = [f for f in _sd.glob("*") if f.is_file()]
                    if _sound_files: break
        except Exception:
            _sound_files = []
        _t.sleep(0.2)
        _spin_stop.set(); _spin_thr.join()

        _audio_exts = {".wav", ".mp3", ".ogg", ".flac", ".aac"}
        _sound_files = [f for f in _sound_files if f.suffix.lower() in _audio_exts]
        if _sound_files:
            _ok(f"Sound engine:    {len(_sound_files)} file(s)  [{_sound_files[0].parent}]")
            for _sf in _sound_files[:8]:
                _info(f"  \u21b3 {_sf.name}")
        else:
            _warn("Sound engine:    no sound files found")
            _info("  \u21b3 place .wav/.mp3 next to gdf.py")

        # ── Network ──────────────────────────────────────────────────────
        print()
        _run("Initializing network stack...")
        _t.sleep(0.08)
        from PyQt6.QtNetwork import QNetworkInterface as _QNI2
        _all_if  = _QNI2.allInterfaces()
        _up_if   = [i for i in _all_if
                    if i.flags() & _QNI2.InterfaceFlag.IsUp
                    and not i.flags() & _QNI2.InterfaceFlag.IsLoopBack]
        _vpn_kw  = ['tun','tap','vpn','hamachi','radmin','wg','zt','zero']
        _phy_if  = [i for i in _up_if if not any(k in i.name().lower() for k in _vpn_kw)]
        _vpn_if  = [i for i in _up_if if any(k in i.name().lower() for k in _vpn_kw)]

        _ok(f"Interfaces:      {len(_all_if)} total  |  {len(_up_if)} up  |  {len(_phy_if)} physical  |  {len(_vpn_if)} VPN")

        _my_ip = get_local_ip()
        _ok(f"Primary IP:      {_my_ip}")

        for _iface in _up_if:
            _addrs = [e.ip().toString() for e in _iface.addressEntries()
                      if ':' not in e.ip().toString() and e.ip().toString() != '0.0.0.0']
            _itype = "VPN" if any(k in _iface.name().lower() for k in _vpn_kw) else "PHY"
            if _addrs:
                _ok(f"  [{_itype}] {_iface.name():<12}  {', '.join(_addrs)}")

        _ok(f"UDP port:        {S().udp_port}   TCP port: {S().tcp_port}")

        try:
            import json as _j2
            _sp = _j2.loads(S().get("static_peers","[]",t=str))
            if _sp:
                _ok(f"Static peers:    {', '.join(_sp)}")
            else:
                _info("Static peers:    none  —  add in Settings → Advanced")
        except Exception:
            pass

        if S().relay_enabled and S().relay_server:
            _ok(f"Relay server:    {S().relay_server}  [ENABLED]")
        else:
            _info("Relay server:    disabled")

        # ── WNS browser ──────────────────────────────────────────────────
        print()
        _run("Loading WNS browser engine...")
        _t.sleep(0.06)
        try:
            from PyQt6.QtWebEngineWidgets import QWebEngineView as _WEV
            _ok("WNS engine:      QtWebEngine  (Chromium)  [READY]")
        except Exception:
            _warn("WNS engine:      QtWebEngine not available")

        # ── History ───────────────────────────────────────────────────────
        print()
        _run("Scanning message history...")
        _t.sleep(0.05)
        try:
            _hfiles = list(DATA_DIR.glob("history_*.json")) if DATA_DIR.exists() else []
            if _hfiles:
                _total_msgs = 0
                for _hf in _hfiles:
                    try:
                        import json as _jh
                        _total_msgs += len(_jh.loads(_hf.read_text(encoding='utf-8')))
                    except Exception:
                        pass
                _ok(f"Message history: {len(_hfiles)} chat(s)  |  {_total_msgs} messages")
            else:
                _info("Message history: empty (first run?)")
        except Exception:
            _warn("Message history: could not read")

        # ── User profile ───────────────────────────────────────────────────
        print()
        _run("Loading user profile...")
        _t.sleep(0.05)
        _uname = S().username or ""
        if _uname:
            _ok(f"Username:        {_uname}")
        else:
            _warn("Username:        not set  —  set in profile")
        _ok(f"Theme:           {S().theme}")
        _ok(f"Premium:         {'YES ✦' if S().premium else 'no'}")
        _ok(f"Language:        {S().language}")
        _ok(f"Scale:           {S().get('app_scale', 100, t=int)}%")
        _ok(f"Encryption:      {'ON' if S().encryption_enabled else 'OFF'}")
        _ok(f"History:         {'saves' if S().save_history else 'disabled'}")
        _ok(f"Sounds:          {'ON' if S().notification_sounds else 'OFF'}")

        # ── Optional deps ────────────────────────────────────────────────
        print()
        _run("Checking optional dependencies...")
        _t.sleep(0.03)
        for _dep, _label, _opt in [
            ("pyaudio",      "pyaudio:         voice calls",    False),
            ("cryptography", "cryptography:    E2E encryption", False),
            ("webrtcvad",    "webrtcvad:       noise gate",     True),
        ]:
            try:
                __import__(_dep)
                _ok(f"{_label}  [OK]")
            except ImportError:
                (_info if _opt else _warn)(f"{_label}  [{'optional' if _opt else 'MISSING'}]")

        # ── Final spinner before window ──────────────────────────────────
        print()
        _spin_msg = ["Starting main window..."]
        _spin_stop2 = _thr.Event()

        def _spinner2():
            i = 0
            while not _spin_stop2.is_set():
                frame = _BRAILLE[i % len(_BRAILLE)]
                _sys.stdout.write(f"\r  {_C}{frame}{_R}  {_D}Starting main window...{_R}")
                _sys.stdout.flush()
                _t.sleep(0.07)
                i += 1
            _sys.stdout.write("\r" + " "*60 + "\r")
            _sys.stdout.flush()

        _spin2_thr = _thr.Thread(target=_spinner2, daemon=True)
        _spin2_thr.start()
        _t.sleep(0.4)

        # ── 3. Main window ───────────────────────────────────────────────
        try:
            window = MainWindow()
            _spin_stop2.set(); _spin2_thr.join()   # stop final spinner
            FT.file_received.connect(window.chat_panel.receive_file)
            _icp = Path(__file__).parent / "icon.png"
            if _icp.exists():
                window.setWindowIcon(QIcon(str(_icp)))
            _ok(f"Main window:     ready")
            print()
            print("  \033[1;32m" + "─"*62 + "\033[0m")
            print(f"  \033[1;32m  Welcome, {S().username or "пользователь"}!\033[0m")
            print("  \033[1;32m" + "─"*62 + "\033[0m")
            print()
            window.show()
            print("  " + "─"*54 + "\n")
            window.raise_()
            window.activateWindow()
            _install_death_screen_handler(window)
            window.update()
            window.repaint()
            app.processEvents()
            QTimer.singleShot(50,  lambda: [window.update(), window.repaint()])
            QTimer.singleShot(150, lambda: [window.update(), app.processEvents()])
        except Exception:
            # Show BSOD even on startup crash (before MainWindow exists)
            import traceback as _tb
            exc_type, exc_val, exc_trace = sys.exc_info()
            # Always print to stderr first so it's visible even if BSOD fails
            print("\n[FATAL STARTUP ERROR]", file=sys.stderr)
            _tb.print_exception(exc_type, exc_val, exc_trace)
            sys.stderr.flush()
            try:
                ds = GoidaDeathScreen(exc_type, exc_val, exc_trace, manual=False)
                ds.resize(900, 600)
                ds.show()
                ds.raise_()
                # Run event loop for crash screen
                app.exec()
            except Exception as e2:
                print(f"[FATAL] Could not show death screen: {e2}")
            sys.exit(1)

    # Delay launcher/main window until after splash
    splash_delay = 2200 if splash else 0
    QTimer.singleShot(splash_delay, _continue_after_splash)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
