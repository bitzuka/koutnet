#!/usr/bin/env python3
import json
from pathlib import Path

I18N_DIR = Path("i18n")

NEW_KEYS = {
    "tab_wns_keenly": "Keenly",
    "tab_player_violla": "Violla",
    "msg_reply": "Ответить",
    "msg_copy": "Копировать",
    "msg_forward": "Переслать",
    "msg_delete": "Удалить",
    "msg_reactions": "Реакция",
    "edited_label": "изменено",
    "chat.back": "Назад",
    "chat.attach_title": "Прикрепить файл",
    "chat.pick_custom_emoji": "Выбрать изображение для эмодзи",
    "chat.placeholder": "Сообщение...",
    "chat.send": "Отправить",
    "chat.last_seen": "был(а) в сети",
    "chat.online_now": "в сети",
    "call.button": "Позвонить",
    "menu.file": "Файл",
    "menu.view": "Вид",
    "menu.calls": "Звонки",
    "menu.help": "Справка",
    "menu.my_profile": "Мой профиль",
    "menu.settings": "Настройки",
    "menu.quit": "Выход",
    "menu.themes": "Темы",
    "menu.fullscreen": "Полноэкранный режим",
    "menu.mute_toggle": "Выключить микрофон",
    "menu.hangup_all": "Завершить все звонки",
    "menu.about": "О программе",
    "menu.tutorial": "Обучение",
    "status.searching": "Поиск пиров...",
    "status.no_calls": "Нет звонков",
    "mic.on": "Микрофон вкл",
    "mic.off": "Микрофон выкл",
    "tab_main_chat": "Чат",
    "tab_main_notes": "Заметки",
    "tab_main_calls": "Звонки",
    "notes_writesmth": "Напишите что-нибудь",
    "notes.new_sheet": "Лист",
    "notes.preview_mode": "Просмотр",
    "notes.edit_mode": "Редактор",
    "no_calls": "Нет звонков",
    "lang_choose": "Выбор языка",
    "copied_notice": "Скопировано!",
    "profile_not_ported": "Редактор профиля ещё не перенесён из legacy-версии.",
    "tutorial_not_ported": "Интерактивный туториал ещё не перенесён.",
    "forward_not_ported": "Пересылка сообщений ещё не реализована в ChatModel.",
    "delete_not_ported": "Удаление сообщений ещё не реализовано в ChatModel.",
}

def main():
    if not I18N_DIR.is_dir():
        raise SystemExit(f"i18n dir not found at {I18N_DIR.resolve()}")

    files = sorted(I18N_DIR.glob("*.json"))
    if not files:
        raise SystemExit("no i18n/*.json files found")

    for f in files:
        data = json.loads(f.read_text(encoding="utf-8"))
        changed = False
        for key, val in NEW_KEYS.items():
            if key not in data or data[key] != val:
                data[key] = val
                changed = True
        if changed:
            f.write_text(
                json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
            print(f"{f.name}: updated")
        else:
            print(f"{f.name}: nothing to add")

if __name__ == "__main__":
    main()
