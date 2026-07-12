#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
GoidaConstruct++ (GC++) v1.1
GoidaPhone NT Server 1.8 — Official Build System

Lang: Russian if OS locale starts with 'ru', else English.
"""

import os, sys, json, shutil, platform, subprocess, time, threading
import urllib.request, zipfile, tempfile
from pathlib import Path

# ─── Language detection ───────────────────────────────────────────────────────
def _detect_lang() -> str:
    for var in ('LANG', 'LC_ALL', 'LC_MESSAGES', 'LANGUAGE'):
        if os.environ.get(var, '').lower().startswith('ru'):
            return 'ru'
    if platform.system() == 'Windows':
        try:
            import ctypes
            if ctypes.windll.kernel32.GetUserDefaultUILanguage() == 0x0419:
                return 'ru'
        except Exception:
            pass
    return 'en'

LANG = _detect_lang()
def T(ru: str, en: str) -> str:
    return ru if LANG == 'ru' else en

# ─── ANSI ─────────────────────────────────────────────────────────────────────
if platform.system() == 'Windows':
    try:
        import ctypes as _ct
        _ct.windll.kernel32.SetConsoleMode(_ct.windll.kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass

R="\033[0m"; G="\033[92m"; Y="\033[93m"; C="\033[96m"
M="\033[95m"; RE="\033[91m"; DIM="\033[2m"; BOLD="\033[1m"
BG_SEL="\033[44m"; BG_GRN="\033[42m"

# ─── Keyboard input ───────────────────────────────────────────────────────────
if platform.system() != 'Windows':
    import tty, termios
    def _getch():
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
            if ch == '\x1b':
                b = sys.stdin.read(2)
                return {'[A':'UP','[B':'DOWN','[C':'RIGHT','[D':'LEFT'}.get(b, b)
            return ch
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
else:
    import msvcrt
    def _getch():
        ch = msvcrt.getwch()
        if ch in ('\x00', '\xe0'):
            c2 = msvcrt.getwch()
            return {'H':'UP','P':'DOWN','M':'RIGHT','K':'LEFT'}.get(c2, c2)
        return ch

def _clr():   os.system('cls' if platform.system()=='Windows' else 'clear')
def _W(n=62): return '═'*n

def _header():
    print(f"\n{M}{_W()}{R}")
    print(f"{BOLD}{C}")
    print(r"  ██████╗  ██████╗ ██╗██████╗  █████╗ ")
    print(r"  ██╔════╝ ██╔═══██╗██║██╔══██╗██╔══██╗")
    print(r"  ██║  ███╗██║   ██║██║██║  ██║███████║")
    print(r"  ██║   ██║██║   ██║██║██║  ██║██╔══██║")
    print(r"  ╚██████╔╝╚██████╔╝██║██████╔╝██║  ██║")
    print(r"   ╚═════╝  ╚═════╝ ╚═╝╚═════╝ ╚═╝  ╚═╝")
    print(f"{R}  {BOLD}{G}GoidaConstruct++ v1.1{R}  "
          f"{DIM}{T('Официальная система сборки','Official Build System')}{R}")
    print(f"{M}{_W()}{R}\n")

def _ok(m):   print(f"  {G}✓{R}  {m}")
def _warn(m): print(f"  {Y}⚠{R}  {m}")
def _err(m):  print(f"  {RE}✗{R}  {m}")
def _info(m): print(f"  {C}→{R}  {m}")

def _progress(done, total, w=42, label=""):
    pct = min(100, int(done*100/total)) if total > 0 else 0
    filled = min(w, int(done*w/total)) if total > 0 else 0
    bar = G+"█"*filled+DIM+"░"*(w-filled)+R
    return f"  [{bar}] {C}{pct:3d}%{R}  {label}"

# ─── Modules definition ───────────────────────────────────────────────────────
MODULES = [
    {"id":"core",    "file":"gdf_core.py",    "required":True,  "size_kb":420,
     "label": T("Ядро (крипто, темы, локализация)","Core (crypto, themes, localization)"),
     "detail":T("Фундамент. Нельзя отключить.","Foundation. Cannot be disabled."),
     "config":{}},
    {"id":"network", "file":"gdf_network.py", "required":False, "size_kb":210,
     "label": T("Сеть и аудио (LAN/VPN, звонки)","Network & Audio (LAN/VPN, calls)"),
     "detail":T("UDP broadcast, ARP scan, P2P звонки.","UDP broadcast, ARP scan, P2P calls."),
     "config":{"broadcast_interval_ms":2000,"arp_scan":True,"multicast":True,"subnet_scan":True}},
    {"id":"ui_base", "file":"gdf_ui_base.py", "required":False, "size_kb":55,
     "label": T("Базовый UI (лаунчер, заставка)","Base UI (launcher, splash)"),
     "detail":T("Экран выбора режима, заставка.","Mode selection screen, splash."),
     "config":{"show_splash":True,"show_launcher":True}},
    {"id":"chat",    "file":"gdf_chat.py",    "required":False, "size_kb":430,
     "label": T("Чат (пузыри, панель пиров)","Chat (bubbles, peer panel)"),
     "detail":T("Полный чат с реакциями и стикерами.","Full chat with reactions and stickers."),
     "config":{"bubble_animations":True,"reactions":True,"stickers":True,"history":True}},
    {"id":"dialogs", "file":"gdf_dialogs.py", "required":False, "size_kb":450,
     "label": T("Диалоги (настройки, профиль)","Dialogs (settings, profile)"),
     "detail":T("Настройки, редактор профиля, журнал звонков.","Settings, profile editor, call log."),
     "config":{}},
    {"id":"browser", "file":"gdf_browser.py", "required":False, "size_kb":340,
     "label": T("WNS Браузер (Chromium)","WNS Browser (Chromium)"),
     "detail":T("Winora NetScape 3.0. Нужен PyQt6-WebEngine.","Winora NetScape 3.0. Needs PyQt6-WebEngine."),
     "config":{"doh_enabled":True,"homepage":"https://winora.xyz"}},
    {"id":"apps",    "file":"gdf_apps.py",    "required":False, "size_kb":460,
     "label": T("Приложения (Terminal, Mewa)","Apps (Terminal, Mewa player)"),
     "detail":T("GoidaTerminal ZLink, Mewa 1-2-3, визуализатор.","GoidaTerminal ZLink, Mewa 1-2-3, visualizer."),
     "config":{"terminal_enabled":True,"mewa_enabled":True}},
    {"id":"main",    "file":"gdf_main.py",    "required":True,  "size_kb":335,
     "label": T("Главное окно + точка входа","Main Window + Entry Point"),
     "detail":T("MainWindow, QuickSetup, Tutorial, BSOD.","MainWindow, QuickSetup, Tutorial, BSOD."),
     "config":{"tutorial":True,"bsod":True,"gcc_mark":True,"gcc_mark_text":"⚡GC++"}},
]

DEFAULT_CFG = {
    "gcc_version":"1.1","app_name":"GoidaPhone NT Server","app_version":"1.8.0",
    "build_id":"gcc-custom","gcc_mark":True,"gcc_mark_text":"⚡GC++",
    "modules":{m["id"]:True for m in MODULES},
    "module_config":{m["id"]:m["config"] for m in MODULES},
    "plugins":[],"output_format":"auto","output_dir":"./dist",
    "icon":"","splash":"","pyinstaller_extra_args":[],
    "metadata":{"author":"","description":"Custom GoidaPhone build via GC++"},
}

CFG_FILE = Path("goida.json")
SOURCES_URL = "https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN/archive/refs/heads/main.zip"

# ─── Config I/O ───────────────────────────────────────────────────────────────
def load_cfg():
    if CFG_FILE.exists():
        try:
            d = json.loads(CFG_FILE.read_text(encoding='utf-8'))
            r = {**DEFAULT_CFG, **d}
            r["modules"]       = {**DEFAULT_CFG["modules"],       **d.get("modules",{})}
            r["module_config"] = {**DEFAULT_CFG["module_config"],  **d.get("module_config",{})}
            return r
        except Exception as e:
            _warn(f"goida.json: {e}")
    return dict(DEFAULT_CFG)

def save_cfg(cfg):
    CFG_FILE.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding='utf-8')
    _ok(f"{T('Сохранено →','Saved →')} {CFG_FILE.resolve()}")

# ─── Download sources ─────────────────────────────────────────────────────────
def cmd_download():
    _clr(); _header()
    print(f"  {BOLD}{T('Загрузка исходников GoidaPhone','Download GoidaPhone Sources')}{R}\n")

    local = list(Path('.').glob('gdf_*.py'))
    if local:
        print(f"  {G}{T('Найдены локальные файлы:','Local files found:')}{R}")
        for f in sorted(local):
            print(f"    {C}•{R} {f.name}  {DIM}({f.stat().st_size//1024} KB){R}")
        print()
        if input(f"  {Y}{T('Использовать локальные? [Y/n] ','Use local? [Y/n] ')}{R}").strip().lower() not in ('n','no','н','нет'):
            _ok(T("Используем локальные исходники.","Using local sources.")); return True

    print(f"  {DIM}{SOURCES_URL}{R}\n")
    if input(f"  {C}{T('Скачать? [Y/n] ','Download? [Y/n] ')}{R}").strip().lower() in ('n','no','н','нет'):
        return False

    zip_path = Path(tempfile.mktemp(suffix='.zip'))
    downloaded = [0]; total_size = [0]; done_ev = threading.Event(); err = [None]

    def _dl():
        try:
            req = urllib.request.Request(SOURCES_URL, headers={'User-Agent':'GoidaConstruct++/1.1'})
            with urllib.request.urlopen(req, timeout=30) as resp:
                total_size[0] = int(resp.headers.get('Content-Length',0))
                with open(zip_path,'wb') as f:
                    while buf := resp.read(8192):
                        f.write(buf); downloaded[0] += len(buf)
        except Exception as e:
            err[0] = str(e)
        finally:
            done_ev.set()

    threading.Thread(target=_dl, daemon=True).start()
    print()
    while not done_ev.is_set():
        d = downloaded[0]; tot = total_size[0] or 1
        lbl = f"{d//1024}KB" + (f"/{tot//1024}KB" if total_size[0] else "")
        print(f"\r{_progress(d, tot, 44, lbl)}", end='', flush=True)
        time.sleep(0.1)
    d = downloaded[0]; tot = total_size[0] or d
    print(f"\r{_progress(d, tot, 44, G+T('ГОТОВО','DONE')+R)}")
    print()

    if err[0]:
        _err(f"{T('Ошибка:','Error:')} {err[0]}"); zip_path.unlink(missing_ok=True); return False

    _info(T("Распаковка...","Extracting..."))
    try:
        tmp = Path(tempfile.mkdtemp())
        with zipfile.ZipFile(zip_path,'r') as zf:
            mbs = zf.namelist()
            for i,mb in enumerate(mbs):
                zf.extract(mb, tmp)
                lbl = DIM+(mb[:44]+"..." if len(mb)>44 else mb)+R
                print(f"\r{_progress(i+1, len(mbs), 44, lbl)}", end='', flush=True)
        print()
        dirs = [d for d in tmp.iterdir() if d.is_dir()]
        if dirs:
            copied = 0
            for f in dirs[0].glob('gdf_*.py'):
                shutil.copy2(f, Path('.')/f.name); _ok(f.name); copied += 1
            for extra in ['gdf.py','gdf_imports.py','goida.json']:
                ef = dirs[0]/extra
                if ef.exists() and not Path(extra).exists():
                    shutil.copy2(ef, Path('.')/extra); _ok(extra)
            _ok(f"{T('Скопировано:','Copied:')} {copied} {T('файлов','files')}")
        shutil.rmtree(tmp, ignore_errors=True); zip_path.unlink(missing_ok=True)
        return True
    except Exception as e:
        _err(str(e)); zip_path.unlink(missing_ok=True); return False

# ─── Module selector (arrow keys + space) ────────────────────────────────────
def cmd_modules(cfg):
    enabled = dict(cfg.get("modules",{m["id"]:True for m in MODULES}))
    cur = 0
    while True:
        _clr(); _header()
        hint = T("↑↓ перемещение  Space/Enter вкл/выкл  D настройки  Q готово",
                 "↑↓ move  Space/Enter toggle  D config  Q done")
        print(f"  {BOLD}{T('Выбор модулей','Module Selection')}{R}  {DIM}{hint}{R}\n")
        print(f"  {DIM}  {'':3} {'Модуль' if LANG=='ru' else 'Module':<38} {T('Размер','Size'):>7}  {T('Статус','Status')}{R}")
        print(f"  {DIM}  {'─'*3} {'─'*38}  {'─'*7}  {'─'*6}{R}")
        for i,mod in enumerate(MODULES):
            on  = enabled.get(mod["id"],True)
            sel = (i==cur)
            req = mod["required"]
            chk = f"{G}[X]{R}" if on else f"{DIM}[ ]{R}"
            req_mark = f" {Y}*{R}" if req else "  "
            pre = f"{BG_SEL}{BOLD}" if sel else ""
            suf = R if sel else ""
            lbl = mod["label"][:38]
            print(f"  {pre}  {chk}{req_mark} {lbl:<38} {DIM}{mod['size_kb']:>5}KB{R}{suf}")
        on_count = sum(1 for k,v in enabled.items() if v)
        tot_kb   = sum(m["size_kb"] for m in MODULES if enabled.get(m["id"],True))
        print(f"\n  {C}{T('Включено:','Enabled:')} {G}{on_count}/{len(MODULES)}{R}  "
              f"~{G}{tot_kb}KB{R}\n  {DIM}{MODULES[cur]['detail']}{R}")
        ch = _getch()
        if ch in ('UP','k'):   cur = (cur-1)%len(MODULES)
        elif ch in ('DOWN','j'):cur = (cur+1)%len(MODULES)
        elif ch in (' ','\r','\n'):
            if not MODULES[cur]["required"]:
                enabled[MODULES[cur]["id"]] = not enabled.get(MODULES[cur]["id"],True)
        elif ch in ('d','D'):  _detail_config(cfg, MODULES[cur])
        elif ch in ('q','Q','\x1b'): break
    cfg["modules"] = enabled; return cfg

def _detail_config(cfg, mod):
    defaults = mod.get("config",{})
    if not defaults:
        _clr(); _header()
        print(f"\n  {T('Нет настроек для этого модуля.','No settings for this module.')}\n")
        print(f"  {DIM}{T('Нажмите любую клавишу...','Press any key...')}{R}"); _getch(); return
    mod_cfg = cfg.setdefault("module_config",{}).setdefault(mod["id"],{})
    keys = list(defaults.keys()); cur = 0
    while True:
        _clr(); _header()
        print(f"  {BOLD}{T('Настройки:','Settings:')} {C}{mod['label']}{R}")
        print(f"  {DIM}{T('↑↓ Enter изменить  Q назад','↑↓ Enter change  Q back')}{R}\n")
        for i,key in enumerate(keys):
            val = mod_cfg.get(key, defaults[key]); sel = (i==cur)
            pre = f"{BG_SEL}{BOLD}" if sel else ""
            suf = R if sel else ""
            if isinstance(val,bool): vs = f"{G}✓ ON{R}" if val else f"{RE}✗ OFF{R}"
            elif isinstance(val,list): vs = f"{DIM}[{len(val)} items]{R}"
            else: vs = f"{C}{val}{R}"
            print(f"  {pre}  {key:<36} {vs}{suf}")
        ch = _getch()
        if ch in ('UP','k'):    cur = (cur-1)%len(keys)
        elif ch in ('DOWN','j'):cur = (cur+1)%len(keys)
        elif ch in ('\r','\n',' '):
            key = keys[cur]; val = mod_cfg.get(key,defaults[key])
            if isinstance(val,bool): mod_cfg[key] = not val
            else:
                print(f"\n  {T('Значение','Value')} [{val}]: ", end=''); sys.stdout.flush()
                try:
                    nv = input().strip()
                    if nv: mod_cfg[key] = type(val)(nv) if isinstance(val,(int,float)) else nv
                except (ValueError,EOFError): pass
        elif ch in ('q','Q','\x1b'): break
    cfg["module_config"][mod["id"]] = mod_cfg

# ─── Build settings ───────────────────────────────────────────────────────────
def cmd_build_settings(cfg):
    items_def = [
        ("output_format", T("Формат (auto/exe/appimage/py):","Format:"),  cfg.get("output_format","auto")),
        ("output_dir",    T("Папка вывода:","Output dir:"),               cfg.get("output_dir","./dist")),
        ("build_id",      T("Build ID:","Build ID:"),                      cfg.get("build_id","gcc-custom")),
        ("gcc_mark",      T("Метка GC++ в нике:","GC++ mark in nick:"),   cfg.get("gcc_mark",True)),
        ("gcc_mark_text", T("Текст метки:","Mark text:"),                  cfg.get("gcc_mark_text","⚡GC++")),
    ]
    items = list(items_def); cur = 0
    while True:
        _clr(); _header()
        print(f"  {BOLD}{T('Настройки сборки','Build Settings')}{R}  {DIM}{T('↑↓ Enter изменить  Q выход','↑↓ Enter change  Q back')}{R}\n")
        for i,(key,lbl,val) in enumerate(items):
            sel = (i==cur); pre = f"{BG_SEL}{BOLD}" if sel else ""
            suf = R if sel else ""
            vs = (f"{G}✓{R}" if val else f"{RE}✗{R}") if isinstance(val,bool) else f"{C}{val}{R}"
            print(f"  {pre}  {lbl:<36} {vs}{suf}")
        ch = _getch()
        if ch in ('UP','k'):    cur = (cur-1)%len(items)
        elif ch in ('DOWN','j'):cur = (cur+1)%len(items)
        elif ch in ('\r','\n',' '):
            key,lbl,val = items[cur]
            if isinstance(val,bool):
                cfg[key]=not val; items[cur]=(key,lbl,cfg[key])
            else:
                print(f"\n  {T('Новое значение','New value')} [{val}]: ",end=''); sys.stdout.flush()
                try:
                    nv=input().strip()
                    if nv: cfg[key]=nv; items[cur]=(key,lbl,nv)
                except EOFError: pass
        elif ch in ('q','Q','\x1b'): break
    return cfg

# ─── Build ────────────────────────────────────────────────────────────────────
def cmd_build(cfg):
    _clr(); _header()
    system = platform.system()
    fmt    = cfg.get("output_format","auto")
    if fmt=="auto": fmt = "exe" if system=="Windows" else "appimage"
    print(f"  {BOLD}{T('Сборка GoidaPhone','Building GoidaPhone')}{R}\n")
    _info(f"{T('Платформа:','Platform:')} {system}")
    _info(f"{T('Формат:','Format:')} {fmt.upper()}")
    mark = cfg.get("gcc_mark_text","⚡GC++") if cfg.get("gcc_mark") else T("выкл","off")
    _info(f"{T('Метка GC++:','GC++ mark:')} {mark}")
    print()
    out_dir  = Path(cfg.get("output_dir","./dist"))
    work_dir = out_dir/"build_tmp"
    out_dir.mkdir(parents=True,exist_ok=True); work_dir.mkdir(exist_ok=True)
    src_dir  = Path(__file__).parent
    to_copy  = ["gdf_imports.py","gdf.py","goida.json"]
    for m in MODULES:
        if cfg["modules"].get(m["id"],True) or m["required"]:
            to_copy.append(m["file"])
    print(f"  {T('Копирование...','Copying...')}\n")
    for i,f in enumerate(to_copy):
        sf = src_dir/f
        if sf.exists(): shutil.copy2(sf,work_dir/f)
        print(f"\r{_progress(i+1,len(to_copy),42,DIM+f+R)}",end='',flush=True)
    print()
    _patch_imports(work_dir, cfg.get("modules",{}))
    for asset in ["imag","gdfsound"]:
        ap = src_dir/asset
        if ap.exists():
            dest = work_dir/asset
            if dest.exists(): shutil.rmtree(dest)
            shutil.copytree(ap,dest)
    if cfg.get("gcc_mark"):
        _inject_mark(work_dir, cfg.get("gcc_mark_text","⚡GC++"))
    for plugin in cfg.get("plugins",[]):
        _inject_plugin(work_dir, plugin)
    if fmt in ("exe","appimage"):
        _info(f"Files in work_dir: {list(work_dir.glob('*.py'))}")
        _pyinstaller(cfg, work_dir, out_dir, fmt, system)
    else:
        final = out_dir/f"GoidaPhone_{cfg['build_id']}"
        if final.exists(): shutil.rmtree(final)
        shutil.rmtree(work_dir, ignore_errors=True)
        shutil.copytree(work_dir,final)
        _ok(f"{T('Готово:','Ready:')} {final}")
    # work_dir cleaned by pyinstaller or kept for debug

# Строки импорта в gdf_main.py, которые нужно убрать при выключении модуля
_MODULE_IMPORTS = {
    "browser": "from gdf_browser  import *",
    "apps":    "from gdf_apps     import *",
    "chat":    "from gdf_chat     import *",
    "dialogs": "from gdf_dialogs  import *",
    "network": "from gdf_network  import *",
    "ui_base": "from gdf_ui_base  import *",
}
# Классы из модулей, которые используются в MainWindow — заменяем на заглушки
_MODULE_STUBS = {
    "browser": {"WinoraNetScape": "QWidget", "OutgoingCallWindow": "QWidget", "IncomingCallDialog": "QWidget"},
    "apps":    {"GoidaTerminal": "QWidget", "GoidaConstruct": "QWidget"},
    "network": {"VoiceCallManager": "object", "FileTransferHandler": "object"},
}
_MODULE_ATTRS = {
    "browser": ["_wns_player", "_wns_tabs"],
    "apps":    ["_mewa_player", "_terminal"],
    "network": ["voice", "net"],
}

def _patch_imports(wd, modules):
    """Uberat importy vyklyuchennyh modulej iz gdf_main.py i gdf.py."""
    import re
    main_file = wd / "gdf_main.py"
    if main_file.exists():
        code = main_file.read_text(encoding='utf-8')
        for mod_id, import_line in _MODULE_IMPORTS.items():
            if not modules.get(mod_id, True):
                code = code.replace(import_line, f"# {import_line}  # [GC++] disabled")
                for attr in _MODULE_ATTRS.get(mod_id, []):
                    pattern = "self." + attr + r"\s*=\s*[^\n]+"
                    code = re.sub(pattern, f"# self.{attr} = None  # [GC++] disabled ({mod_id})", code)
                for cls, stub in _MODULE_STUBS.get(mod_id, {}).items():
                    code = code.replace(cls, stub)
        main_file.write_text(code, encoding='utf-8')
    entry_file = wd / "gdf.py"
    if entry_file.exists():
        code = entry_file.read_text(encoding='utf-8')
        for mod_id, import_line in _MODULE_IMPORTS.items():
            if not modules.get(mod_id, True):
                code = code.replace(import_line, f"# {import_line}  # [GC++] disabled")
        entry_file.write_text(code, encoding='utf-8')

def _inject_mark(wd, mark):
    import re
    f = wd/"gdf_core.py"
    if not f.exists(): return
    s = f.read_text(encoding='utf-8')
    s = re.sub(r'(APP_VERSION\s*=\s*"[^"]+)"',rf'\1 {mark}"',s,count=1)
    s = s.replace('APP_VERSION =',f'GCC_BUILD=True\nGCC_MARK="{mark}"\nAPP_VERSION =',1)
    f.write_text(s,encoding='utf-8')

def _inject_plugin(wd, plugin):
    ep = Path(plugin["entry_point"])
    if not ep.exists(): _warn(f"Plugin missing: {ep}"); return
    code = ep.read_text(encoding='utf-8')
    target = wd/plugin.get("inject_into","gdf_main.py")
    if not target.exists(): _warn(f"Target missing: {target}"); return
    s = target.read_text(encoding='utf-8')
    hdr = f"\n# ── GC++ Plugin: {plugin['name']} v{plugin['version']} ──\n{code}\n"
    after = plugin.get("inject_after","")
    s = s.replace(after, after+hdr, 1) if after and after in s else s+hdr
    target.write_text(s,encoding='utf-8')
    _ok(f"Plugin '{plugin['name']}' injected")

def _pyinstaller(cfg, wd, out, fmt, system):
    if not shutil.which("pyinstaller"):
        _err(T("PyInstaller не найден! pip install pyinstaller","PyInstaller not found! pip install pyinstaller")); return
    name = cfg.get("app_name","GoidaPhone").replace(" ","_")
    icon = ["--icon",cfg["icon"]] if cfg.get("icon") and Path(cfg["icon"]).exists() else []
    cmd  = ["pyinstaller","--onefile","--name",name,"--distpath",str(out),
            "--workpath",str(out/"pw"),"--specpath",str(out),
            *icon,*cfg.get("pyinstaller_extra_args",[]),str(wd/"gdf.py")]
    if fmt=="appimage" and system=="Windows":
        _warn(T("AppImage недоступен на Windows.","AppImage unavailable on Windows."))
    try: subprocess.run(cmd,cwd=str(wd),check=True); _ok(f"{fmt.upper()} → {out}")
    except subprocess.CalledProcessError as e: _err(f"PyInstaller: {e.returncode}")

# ─── Add plugin ───────────────────────────────────────────────────────────────
MANIFEST_TPL = {"name":"my_plugin","version":"1.0.0","author":"","description":"",
                "goidaphone_version":"1.8.0","entry_point":"my_plugin.py",
                "inject_into":"gdf_main.py","inject_after":"class MainWindow(","requires":[]}

def cmd_add_plugin(cfg):
    _clr(); _header()
    print(f"  {BOLD}{T('Добавить плагин','Add Plugin')}{R}\n")
    print(f"  {DIM}{json.dumps(MANIFEST_TPL,indent=4,ensure_ascii=False)}{R}\n")
    path = Path(input(f"  {C}{T('Путь к папке/манифесту: ','Path to folder/manifest: ')}{R}").strip())
    if not path.exists(): _err(T("Не найдено","Not found")); return cfg
    mf = path/"goida_plugin.json" if path.is_dir() else path
    if not mf.exists():
        if input(f"  {Y}{T('Создать шаблон? [y/N] ','Create template? [y/N] ')}{R}").lower() in ('y','д'):
            tpl = path/"goida_plugin.json" if path.is_dir() else Path("goida_plugin.json")
            tpl.write_text(json.dumps(MANIFEST_TPL,indent=4,ensure_ascii=False),encoding='utf-8')
            _ok(str(tpl))
        return cfg
    try: manifest = json.loads(mf.read_text(encoding='utf-8'))
    except Exception as e: _err(str(e)); return cfg
    for f in ("name","version","entry_point"):
        if f not in manifest: _err(f"Missing: {f}"); return cfg
    base = path.parent if path.is_file() else path
    entry = base/manifest["entry_point"]
    pe = {"name":manifest["name"],"version":manifest["version"],
          "author":manifest.get("author",""),"description":manifest.get("description",""),
          "entry_point":str(entry.resolve()),"inject_into":manifest.get("inject_into","gdf_main.py"),
          "inject_after":manifest.get("inject_after",""),"requires":manifest.get("requires",[])}
    cfg["plugins"] = [p for p in cfg.get("plugins",[]) if p["name"]!=pe["name"]]
    cfg["plugins"].append(pe)
    _ok(f"{pe['name']} v{pe['version']}")
    return cfg

# ─── Help ─────────────────────────────────────────────────────────────────────
def cmd_help():
    _clr(); _header()
    print(f"  {BOLD}{T('Справка GoidaConstruct++','GoidaConstruct++ Help')}{R}\n")
    sections = [
        (T("ЗАПУСК","LAUNCH"),[
            ("python3 gdf_gcc.py",          T("Интерактивный режим","Interactive mode")),
            ("python3 gdf_gcc.py --build",   T("Собрать из goida.json","Build from goida.json")),
            ("python3 gdf_gcc.py --init",    T("Создать шаблон goida.json","Create goida.json")),
            ("python3 gdf_gcc.py --download",T("Скачать исходники","Download sources")),
            ("python3 gdf_gcc.py --list",    T("Список модулей","List modules")),
            ("python3 gdf_gcc.py --help",    T("Эта справка","This help")),
        ]),
        (T("УПРАВЛЕНИЕ","NAVIGATION"),[
            ("↑ / ↓ / k / j",  T("Перемещение по списку","Navigate list")),
            ("Space / Enter",   T("Включить/выключить или изменить","Toggle or edit")),
            ("D",               T("Детальные настройки модуля","Module detailed config")),
            ("Q / Esc",         T("Назад / Выход","Back / Quit")),
        ]),
        (T("ФАЙЛЫ","FILES"),[
            ("goida.json",           T("Конфиг сборки","Build config")),
            ("goida_plugin.json",    T("Манифест плагина","Plugin manifest")),
            ("gdf_*.py",             T("Модули GoidaPhone","GoidaPhone modules")),
            ("dist/",                T("Готовая сборка","Output build")),
        ]),
        (T("ПЛАГИНЫ","PLUGINS"),[
            ("name, version",        T("Обязательные поля","Required fields")),
            ("entry_point",          T("Python файл плагина","Plugin Python file")),
            ("inject_into",          T("Целевой модуль","Target module")),
            ("inject_after",         T("После какого класса","After which class")),
        ]),
    ]
    for title,items in sections:
        print(f"\n  {M}{BOLD}{title}{R}")
        for cmd,desc in items:
            print(f"    {C}{cmd:<42}{R} {DIM}{desc}{R}")
    print(f"\n  {DIM}{T('Нажмите любую клавишу...','Press any key...')}{R}"); _getch()

# ─── List ─────────────────────────────────────────────────────────────────────
def cmd_list(cfg):
    _clr(); _header()
    enabled = cfg.get("modules",{})
    print(f"  {BOLD}{T('Доступные модули','Available Modules')}{R}\n")
    kb_total = 0
    for m in MODULES:
        on = enabled.get(m["id"],True)
        if on: kb_total += m["size_kb"]
        req = f" {Y}[{T('обяз','req')}]{R}" if m["required"] else ""
        st  = f"{G}✓ {T('ВКЛ','ON ')}{R}" if on else f"{RE}✗ {T('ВЫКЛ','OFF')}{R}"
        print(f"  {st}  {BOLD}{m['id']:<12}{R} {m['label']}{req}")
        print(f"         {DIM}{m['detail']}  ~{m['size_kb']}KB{R}\n")
    print(f"  {C}~{G}{kb_total}KB{R}  {T('всего','total')}\n")
    print(f"  {DIM}{T('Нажмите любую клавишу...','Press any key...')}{R}"); _getch()

# ─── Main menu ────────────────────────────────────────────────────────────────
MENU = [
    ("modules",   T("🔧  Модули (вкл/выкл + настройка)","🔧  Modules (toggle + configure)")),
    ("plugin",    T("⚡  Добавить плагин","⚡  Add plugin")),
    ("build_cfg", T("⚙   Настройки сборки","⚙   Build settings")),
    ("download",  T("⬇   Скачать / обновить исходники","⬇   Download / update sources")),
    ("build",     T("🔨  Собрать (EXE / AppImage / py)","🔨  Build (EXE / AppImage / py)")),
    ("save",      T("💾  Сохранить конфиг","💾  Save config")),
    ("list",      T("📋  Список модулей","📋  List modules")),
    ("help",      T("❓  Справка","❓  Help")),
    ("quit",      T("✕   Выйти из GC++","✕   Quit GC++")),
]

def main_menu(cfg):
    cur = 0
    while True:
        _clr(); _header()
        system = platform.system()
        fmt = cfg.get("output_format","auto")
        if fmt=="auto": fmt = "EXE" if system=="Windows" else "AppImage"
        on_n   = sum(1 for v in cfg["modules"].values() if v)
        kb     = sum(m["size_kb"] for m in MODULES if cfg["modules"].get(m["id"],True))
        src_ok = bool(list(Path('.').glob('gdf_*.py')))
        print(f"  {C}{T('Модулей','Modules')}: {G}{on_n}/{len(MODULES)}{R}  "
              f"~{G}{kb}KB{R}  {T('Формат','Fmt')}: {G}{fmt}{R}  "
              f"{T('Исходники','Src')}: {G+'✓'+R if src_ok else RE+'✗'+R}\n")
        print(f"  {DIM}{T('↑↓ перемещение  Enter выбрать','↑↓ navigate  Enter select')}{R}\n")
        for i,(action,label) in enumerate(MENU):
            sel = (i==cur)
            pre = f"{BG_SEL}{BOLD}" if sel else "  "
            suf = R if sel else ""
            print(f"  {pre}  {label}{suf}")
        ch = _getch()
        if ch in ('UP','k'):    cur = (cur-1)%len(MENU)
        elif ch in ('DOWN','j'):cur = (cur+1)%len(MENU)
        elif ch in ('\r','\n',' '): return MENU[cur][0], cfg
        elif ch in ('q','Q','\x1b'): return "quit", cfg

# ─── Entry point ──────────────────────────────────────────────────────────────
def run_gcc():
    args = sys.argv[1:]
    if "--help" in args or "-h" in args: _clr(); _header(); cmd_help(); return
    if "--init" in args:
        save_cfg(DEFAULT_CFG)
        _info(f"{T('Создан:','Created:')} {CFG_FILE.resolve()}"); return
    if "--list" in args:
        cfg = load_cfg(); cmd_list(cfg); return
    if "--download" in args:
        cmd_download(); return
    if "--build" in args:
        cfg = load_cfg(); _clr(); _header(); cmd_build(cfg); return

    cfg = load_cfg()
    while True:
        action, cfg = main_menu(cfg)
        if action=="modules":   cfg = cmd_modules(cfg)
        elif action=="plugin":  cfg = cmd_add_plugin(cfg)
        elif action=="build_cfg": cfg = cmd_build_settings(cfg)
        elif action=="download":  cmd_download()
        elif action=="build":
            _clr(); _header()
            if input(f"  {Y}{T('Начать сборку? [y/N] ','Start build? [y/N] ')}{R}").strip().lower() in ('y','д','yes'):
                cmd_build(cfg)
                input(f"\n  {DIM}{T('Enter для продолжения...','Enter to continue...')}{R}")
        elif action=="save":  save_cfg(cfg)
        elif action=="list":  cmd_list(cfg)
        elif action=="help":  cmd_help()
        elif action=="quit":
            _clr(); _header()
            print(f"  {DIM}{T('До свидания!','Goodbye!')}{R}\n"); break

if __name__ == "__main__":
    run_gcc()
