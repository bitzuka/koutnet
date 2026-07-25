#!/usr/bin/env python3
import json
from pathlib import Path

I18N_DIR = Path("i18n")

# key -> russian text (used as placeholder for every language that
# doesn't have this key yet — translate later, this just stops the UI
# from showing raw key strings like "tab_wns_keenly").
NEW_KEYS = {
    "tab_wns_keenly": "🌐 Keenly",
    "tab_player_violla": "♫ Violla",
    "msg_reply": "↩ Ответить",
    "msg_copy": "📋 Копировать",
    "msg_forward": "↪ Переслать",
    "msg_delete": "🗑 Удалить",
    "msg_reactions": "😊 Реакция",
    "edited_label": "изменено",
    "chat.back": "Назад",
    "chat.attach_title": "Прикрепить файл",
    "chat.image_viewer_title": "Просмотр изображения",
    "menu.file": "Файл",
    "menu.view": "Вид",
    "menu.calls": "Звонки",
    "menu.help": "Справка",
    "menu.my_profile": "Мой профиль",
    "menu.check_updates": "Проверить обновления",
    "menu.quit": "Выход",
    "menu.themes": "Темы",
    "menu.public_chat": "Публичный чат",
    "menu.fullscreen": "Полноэкранный режим",
    "menu.lang_ru": "Русский",
    "menu.lang_en": "English",
    "menu.lang_ja": "日本語",
    "menu.mute_toggle": "Выключить микрофон",
    "menu.hangup_all": "Завершить все звонки",
    "menu.about": "О программе",
    "menu.terminal": "Терминал",
    "menu.tutorial": "Обучение",
    "status.searching": "Поиск пиров...",
    "status.no_calls": "Нет звонков",
    "mic.on": "Микрофон вкл",
    "mic.off": "Микрофон выкл",
    "tab_main_chat": "Чат",
    "tab_main_notes": "Заметки",
    "tab_main_calls": "Звонки",
    "profile_not_ported": "Редактор профиля ещё не перенесён из legacy-версии.",
    "updates_not_ported": "Автообновление ещё не перенесено.",
    "terminal_not_ported": "Терминал ещё не перенесён.",
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
        added = []
        for key, val in NEW_KEYS.items():
            if key not in data:
                data[key] = val
                added.append(key)
        if added:
            f.write_text(
                json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
            print(f"{f.name}: added {len(added)} key(s): {', '.join(added)}")
        else:
            print(f"{f.name}: nothing to add")

if __name__ == "__main__":
    main()
