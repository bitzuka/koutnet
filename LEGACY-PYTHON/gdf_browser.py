#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — WNS Browser
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings
from gdf_network  import *
from gdf_ui_base  import *      # TextFormatter, ImageViewer, HoverCard      # VoiceCallManager, play_system_sound, S

# ═══════════════════════════════════════════════════════════════════════════
#  WINORA NETSCAPE  (WNS 2.0) — full-featured embedded browser
# ═══════════════════════════════════════════════════════════════════════════


# ════════════════════════════════════════════════════════════════════════════
#  WINORA NETSCAPE 3.0
#  Тотальный реврайт: sidebar, reader mode, devtools, extensions, pip bar,
#  find bar встроенный, bokmrk manager, история с группировкой по дням,
#  мультипоиск, dark-reader js injection, zoom, screenshot, picture-in-picture
# ════════════════════════════════════════════════════════════════════════════

# ── Dark Reader JS (инжектируется в каждую страницу) ─────────────────────────
_WNS_DARK_READER_JS = """
(function(){
  if(document.getElementById('_wns_dr'))return;
  var s=document.createElement('style');s.id='_wns_dr';
  s.textContent=`
    html{filter:invert(1) hue-rotate(180deg)!important;}
    img,video,canvas,svg{filter:invert(1) hue-rotate(180deg)!important;}
  `;document.documentElement.appendChild(s);
})();
"""

_WNS_READER_JS = """
(function(){
  // Simple reader mode: extract article text
  var content = '';
  var sel = ['article','main','[role=main]','.post-content','.entry-content','.article-body','#content'];
  for(var i=0;i<sel.length;i++){
    var el=document.querySelector(sel[i]);
    if(el&&el.innerText.length>200){content=el.innerHTML;break;}
  }
  if(!content){content=document.body.innerHTML;}
  var title=document.title||'';
  document.open();document.write(`<!DOCTYPE html><html><head>
  <meta charset=utf-8><title>${title}</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box;}
    body{background:#12121F;color:#D0D0E8;font-family:'Georgia',serif;
         max-width:720px;margin:40px auto;padding:0 24px 80px;font-size:17px;line-height:1.8;}
    h1,h2,h3{color:#C0A8FF;margin:1em 0 .4em;}
    a{color:#7C6DFF;}img{max-width:100%;border-radius:8px;margin:12px 0;}
    p{margin:.6em 0;}pre,code{background:#1E1E34;padding:2px 6px;border-radius:4px;font-size:14px;}
    blockquote{border-left:3px solid #7C4DFF;margin:1em 0;padding-left:1em;color:#A0A0C0;}
    #_wns_reader_hdr{background:#1A1A2E;padding:12px 20px;border-radius:12px;
                     margin-bottom:24px;display:flex;align-items:center;gap:12px;}
  </style></head><body>
  <div id="_wns_reader_hdr">
    <span style="font-size:20px">📖</span>
    <div><div style="font-weight:bold;font-size:15px">${title}</div>
    <div style="font-size:11px;color:#606080">Режим чтения · Winora NetScape 3.0</div></div>
  </div>
  ${content}
  </body></html>`);document.close();
})();
"""

# ── New Tab Page HTML ─────────────────────────────────────────────────────────
WNS_HOME_HTML = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Новая вкладка</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;}
  body{
    background:#202124;color:#e8eaed;
    font-family:-apple-system,system-ui,"Segoe UI",Arial,sans-serif;
    height:100vh;display:flex;flex-direction:column;
    align-items:center;justify-content:center;
    overflow:hidden;user-select:none;
  }
  .logo-text{
    font-size:96px;font-weight:900;letter-spacing:-2px;
    background:linear-gradient(135deg,var(--accent,#8ab4f8),#a8c7fa 60%,#74a8f8);
    -webkit-background-clip:text;-webkit-text-fill-color:transparent;
    background-clip:text;line-height:1.15;padding:0 8px;margin-bottom:24px;
    filter:drop-shadow(0 2px 20px #8ab4f840);
  }
  .search-wrap{
    width:min(680px,90vw);background:#303134;
    border-radius:24px;border:1px solid #5f6368;
    display:flex;align-items:center;padding:0 16px;
    height:46px;gap:10px;
    transition:box-shadow .15s,border-color .15s;
  }
  .search-wrap:focus-within{
    box-shadow:0 1px 6px #0005;border-color:#8ab4f8;
  }
  .search-icon{color:#9aa0a6;font-size:18px;flex-shrink:0;}
  #q{
    flex:1;background:transparent;border:none;outline:none;
    font-size:16px;color:#e8eaed;caret-color:#e8eaed;
  }
  #q::placeholder{color:#9aa0a6;}
  .se-row{display:flex;gap:8px;margin-top:16px;}
  .se{
    background:#303134;border:1px solid #5f636860;
    border-radius:20px;padding:4px 14px;font-size:12px;
    color:#9aa0a6;cursor:pointer;transition:background .1s,color .1s;
  }
  .se:hover,.se.active{background:#8ab4f820;color:#8ab4f8;border-color:#8ab4f860;}
  .shortcuts{
    display:grid;grid-template-columns:repeat(5,88px);
    gap:12px;margin-top:40px;
  }
  .shortcut{
    display:flex;flex-direction:column;align-items:center;gap:8px;
    text-decoration:none;color:#e8eaed;border-radius:12px;
    padding:14px 8px 10px;transition:background .15s;font-size:12px;
  }
  .shortcut:hover{background:#35363a;}
  .fav{
    width:40px;height:40px;border-radius:50%;background:#303134;
    display:flex;align-items:center;justify-content:center;font-size:20px;
  }
  .shortcut span{
    white-space:nowrap;overflow:hidden;text-overflow:ellipsis;
    max-width:72px;text-align:center;color:#9aa0a6;font-size:11px;
  }
  .clock{position:fixed;top:28px;right:32px;font-size:13px;color:#5f6368;}
</style>
</head>
<body>
<div class="clock" id="clk"></div>
<div class="logo-text">WNS</div>
<div class="search-wrap">
  <span class="search-icon">&#128269;</span>
  <input id="q" placeholder="Search Google or enter address" autofocus
         onkeydown="if(event.key==='Enter')doSearch()">
</div>
<div class="se-row" id="se-row">
  <div class="se active" onclick="setSE(this,'google')">Google</div>
  <div class="se" onclick="setSE(this,'yandex')">Яндекс</div>
  <div class="se" onclick="setSE(this,'bing')">Bing</div>
  <div class="se" onclick="setSE(this,'ddg')">DuckDuckGo</div>
  <div class="se" onclick="setSE(this,'gh')">GitHub</div>
</div>
<div class="shortcuts">
  <a class="shortcut" href="https://www.youtube.com">
    <div class="fav">&#9654;</div><span>YouTube</span></a>
  <a class="shortcut" href="https://github.com/nft1212/GoidaPhone-NT-Server-1.8-OPEN">
    <div class="fav">&#128025;</div><span>GoidaPhone</span></a>
  <a class="shortcut" href="https://www.wikipedia.org">
    <div class="fav">&#128218;</div><span>Wikipedia</span></a>
  <a class="shortcut" href="https://www.reddit.com">
    <div class="fav">&#129418;</div><span>Reddit</span></a>
  <a class="shortcut" href="https://stackoverflow.com">
    <div class="fav">&#128172;</div><span>Stack Overflow</span></a>
  <a class="shortcut" href="https://itch.io">
    <div class="fav">&#127918;</div><span>itch.io</span></a>
  <a class="shortcut" href="https://translate.google.com">
    <div class="fav">&#127758;</div><span>Переводчик</span></a>
  <a class="shortcut" href="https://hastebin.com">
    <div class="fav">&#128203;</div><span>Hastebin</span></a>
  <a class="shortcut" href="https://www.wolframalpha.com">
    <div class="fav">&#8721;</div><span>Wolfram</span></a>
  <a class="shortcut" href="https://news.ycombinator.com">
    <div class="fav">&#128310;</div><span>Hacker News</span></a>
</div>
<script>
var SE='google';
var urls={
  google:'https://www.google.com/search?q=',
  yandex:'https://yandex.ru/search/?text=',
  bing:'https://www.bing.com/search?q=',
  ddg:'https://duckduckgo.com/?q=',
  gh:'https://github.com/search?q='
};
function setSE(el,name){
  document.querySelectorAll('.se').forEach(function(e){e.classList.remove('active');});
  el.classList.add('active'); SE=name;
}
function doSearch(){
  var q=document.getElementById('q').value.trim();
  if(!q)return;
  var url;
  if(q.startsWith('http://')||q.startsWith('https://'))
    url=q;
  else if(q.indexOf('.')>0&&q.indexOf(' ')<0&&q.length>4)
    url='https://'+q;
  else
    url=urls[SE]+encodeURIComponent(q);
  window.location.href=url;
}
function tick(){
  var d=new Date();
  var h=String(d.getHours()).padStart(2,'0');
  var m=String(d.getMinutes()).padStart(2,'0');
  document.getElementById('clk').textContent=h+':'+m;
  setTimeout(tick,10000);
}
tick();
document.getElementById('q').focus();
</script>
</body>
</html>"""


class WinoraNetScape(QWidget):
    """
    Winora NetScape 3.0 — встроенный браузер, работает как вкладка.
    Новое: sidebar (history/bookmarks/downloads), find bar, reader mode,
    dark reader injection, devtools, zoom, screenshot, pip, extensions stub,
    мультипоисковик на новой вкладке, часы, группировка истории по дням.
    """
    WNS_VERSION = "3.1"
    HOME_URL    = "wns://newtab"

    _SEARCH_ENGINES = {
        "Google":    "https://www.google.com/search?q={}",
        "Yandex":    "https://yandex.ru/search/?text={}",
        "Bing":      "https://www.bing.com/search?q={}",
        "DuckDuckGo":"https://duckduckgo.com/?q={}",
        "GitHub":    "https://github.com/search?q={}",
    }
    _DEFAULT_BOOKMARKS = [
        ("🔍 Google",    "https://www.google.com"),
        ("📺 YouTube",   "https://www.youtube.com"),
        ("🐙 GitHub",    "https://github.com"),
        ("📚 Wikipedia", "https://www.wikipedia.org"),
        ("🦊 Reddit",    "https://www.reddit.com"),
        ("🎮 itch.io",   "https://itch.io"),
    ]

    def __init__(self, parent=None):
        super().__init__(parent)
        self._bookmarks  : list = []
        self._history     : list  = []     # [(title, url, timestamp)]
        self._dl_dir      = DATA_DIR / "wns_downloads"
        self._dl_dir.mkdir(parents=True, exist_ok=True)
        self._webengine   = False
        self._dark_reader = False
        self._reader_mode = False
        self._zoom_level  = 1.0
        self._sidebar_vis = False
        self._sidebar_tab = "history"   # "history"|"bookmarks"|"downloads"
        self._find_visible= False
        self._search_engine = "Google"
        self._load_bookmarks()
        self._load_history()
        # Title is shown in tab, not window title

        self._apply_style()
        self._setup_ui()
        self._load_geometry()

    # ── Style ─────────────────────────────────────────────────────────────────
    def _themed_home_html(self) -> str:
        """Inject current theme colors into the home page."""
        t = get_theme(S().theme)
        css_override = f"""
<style id="theme-override">
:root {{
  --bg: {t['bg']};
  --bg2: {t['bg2']};
  --bg3: {t['bg3']};
  --text: {t['text']};
  --dim: {t['text_dim']};
  --accent: {t['accent']};
  --border: {t['border']};
}}
body {{ background: var(--bg) !important; color: var(--text) !important; }}
.search-wrap {{ background: var(--bg3) !important; border-color: var(--border) !important; }}
.search-wrap:focus-within {{ border-color: var(--accent) !important; }}
#q {{ color: var(--text) !important; }}
#q::placeholder {{ color: var(--dim) !important; }}
.se {{ background: var(--bg3) !important; border-color: var(--border) !important;
       color: var(--dim) !important; }}
.se:hover, .se.active {{ background: var(--accent) !important;
                          color: var(--bg) !important; border-color: var(--accent) !important; }}
.shortcut {{ color: var(--text) !important; }}
.shortcut:hover {{ background: var(--bg2) !important; }}
.shortcut span {{ color: var(--dim) !important; }}
.fav {{ background: var(--bg3) !important; border: 1px solid var(--border); }}
.clock {{ color: var(--dim) !important; }}
</style>"""
        # Insert before </head>
        return WNS_HOME_HTML.replace("</head>", css_override + "</head>", 1)

    def _apply_style(self):
        """Chrome-style layout но цвета из текущей темы GoidaPhone."""
        t = get_theme(S().theme)

        # Derive Chrome-like shades from theme palette
        bg      = t['bg']       # main content / active tab
        bg2     = t['bg2']      # toolbar / tab bar background  
        bg3     = t['bg3']      # tab bar (darker strip)
        border  = t['border']
        text    = t['text']
        dim     = t['text_dim']
        accent  = t['accent']
        bhover  = t['btn_hover']
        bbg     = t['btn_bg']

        self.setStyleSheet(f"""
            /* ── Base ───────────────────────────────────────── */
            QWidget {{ background:{bg}; color:{text}; }}

            /* ── Toolbar (address bar row) ───────────────────── */
            QWidget#wns_toolbar {{
                background:{bg2};
                border-bottom:1px solid {border};
            }}

            /* ── Omnibar ─────────────────────────────────────── */
            QLineEdit#wns_urlbar {{
                background:{bg3};
                color:{text};
                border:1px solid {border};
                border-radius:22px;
                padding:5px 16px 5px 36px;
                font-size:13px;
                selection-background-color:{accent};
            }}
            QLineEdit#wns_urlbar:hover {{
                background:{bg2};
                border-color:{dim};
            }}
            QLineEdit#wns_urlbar:focus {{
                background:{bg};
                border:2px solid {accent};
            }}

            /* ── Nav buttons ─────────────────────────────────── */
            QPushButton#wns_nav {{
                background:transparent;
                color:{dim};
                border:none;
                border-radius:17px;
                min-width:34px; max-width:34px;
                min-height:34px; max-height:34px;
                font-size:17px;
            }}
            QPushButton#wns_nav:hover {{
                background:{bhover};
                color:{text};
            }}
            QPushButton#wns_nav:pressed {{
                background:{bbg};
            }}
            QPushButton#wns_nav:disabled {{ color:{border}; }}
            QPushButton#wns_nav:checked {{
                background:{accent}30;
                color:{accent};
            }}
            QPushButton#wns_new_tab {{
                background:transparent;
                color:{dim};
                border:none;
                border-radius:14px;
                min-width:28px; max-width:28px;
                min-height:28px; max-height:28px;
                font-size:18px;
            }}
            QPushButton#wns_new_tab:hover {{
                background:{bhover};
                color:{text};
            }}

            /* ── Tab bar (Chrome pill tabs) ──────────────────── */
            QTabWidget#wns_tabs::pane {{
                border:none;
                background:{bg};
            }}
            QTabBar {{
                background:{bg3};
                border-bottom:1px solid {border};
            }}
            QTabBar::tab {{
                background:transparent;
                color:{dim};
                border:none;
                border-radius:8px 8px 0 0;
                padding:6px 14px;
                margin:4px 1px 0 1px;
                font-size:12px;
                min-width:80px;
                max-width:220px;
            }}
            QTabBar::tab:selected {{
                background:{bg};
                color:{text};
                border-top:2px solid {accent};
            }}
            QTabBar::tab:hover:!selected {{
                background:{bg2};
                color:{text};
            }}
            QTabBar::close-button {{
                subcontrol-position:right;
                margin:2px;
            }}

            /* ── Bookmarks bar ───────────────────────────────── */
            QWidget#wns_bmarks {{
                background:{bg2};
                border-bottom:1px solid {border};
            }}
            QPushButton#wns_bmark_chip {{
                background:transparent;
                color:{dim};
                border:none;
                border-radius:6px;
                padding:2px 10px;
                font-size:11px;
            }}
            QPushButton#wns_bmark_chip:hover {{
                background:{bhover};
                color:{text};
            }}

            /* ── Find bar ────────────────────────────────────── */
            QWidget#wns_findbar {{
                background:{bg2};
                border-bottom:1px solid {border};
            }}
            QLineEdit#wns_find {{
                background:{bg3};
                color:{text};
                border:1px solid {border};
                border-radius:8px;
                padding:4px 10px;
                font-size:12px;
                min-width:220px;
                selection-background-color:{accent};
            }}
            QLineEdit#wns_find:focus {{
                border-color:{accent};
            }}

            /* ── Sidebar ─────────────────────────────────────── */
            QWidget#wns_sidebar {{
                background:{bg2};
                border-left:1px solid {border};
            }}
            QListWidget#wns_slist {{
                background:{bg2};
                color:{text};
                border:none;
                outline:none;
                font-size:11px;
            }}
            QListWidget#wns_slist::item {{
                padding:6px 12px;
                border-bottom:1px solid {border};
            }}
            QListWidget#wns_slist::item:hover {{ background:{bhover}; }}
            QListWidget#wns_slist::item:selected {{
                background:{accent}30;
                color:{accent};
            }}

            /* ── Progress bar ────────────────────────────────── */
            QProgressBar#wns_prog {{
                background:transparent;
                border:none;
                height:3px;
            }}
            QProgressBar#wns_prog::chunk {{
                background:{accent};
                border-radius:1px;
            }}

            /* ── Status bar ──────────────────────────────────── */
            QWidget#wns_statusbar {{
                background:{bg3};
                border-top:1px solid {border};
            }}
            QLabel#wns_stat {{
                color:{dim};
                font-size:9px;
                background:transparent;
                padding:0 8px;
            }}
            QLabel#wns_zoom {{
                color:{accent};
                font-size:10px;
                background:transparent;
                font-weight:bold;
            }}

            /* ── Menu ────────────────────────────────────────── */
            QMenu {{
                background:{bg2};
                color:{text};
                border:1px solid {border};
                border-radius:10px;
                padding:4px 0;
                font-size:12px;
            }}
            QMenu::item {{ padding:8px 22px; }}
            QMenu::item:selected {{
                background:{accent}25;
                color:{accent};
                border-radius:6px;
            }}
            QMenu::separator {{
                background:{border};
                height:1px;
                margin:3px 8px;
            }}
        """)

    def _setup_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(0,0,0,0); root.setSpacing(0)

        # Progress bar must exist BEFORE _mk_toolbar because _new_tab uses it
        self._prog = QProgressBar()
        self._prog.setObjectName("wns_prog"); self._prog.setFixedHeight(3)
        self._prog.setTextVisible(False); self._prog.setRange(0,100)
        self._prog.setVisible(False)

        root.addWidget(self._mk_toolbar())
        root.addWidget(self._mk_bmarks_bar())
        root.addWidget(self._prog)

        # Find bar (hidden by default)
        self._findbar = self._mk_findbar()
        self._findbar.setVisible(False)
        root.addWidget(self._findbar)

        # Main area: tabs + sidebar
        self._main_split = QSplitter(Qt.Orientation.Horizontal)
        self._main_split.setHandleWidth(1)
        self._main_split.setStyleSheet("QSplitter::handle{background:#2D2D4E;}")

        self._tabs = QTabWidget()
        self._tabs.setObjectName("wns_tabs")
        self._tabs.setTabsClosable(True); self._tabs.setMovable(True)
        self._tabs.setDocumentMode(True)  # removes frame, cleaner look
        self._tabs.tabCloseRequested.connect(self._close_tab)
        self._tabs.currentChanged.connect(self._on_tab_switch)
        # Chrome-style: tab bar uses minimal height
        self._tabs.tabBar().setExpanding(False)
        self._tabs.tabBar().setDrawBase(False)
        self._main_split.addWidget(self._tabs)

        self._sidebar = self._mk_sidebar()
        self._sidebar.setVisible(False)
        self._main_split.addWidget(self._sidebar)
        self._main_split.setSizes([1100, 280])

        root.addWidget(self._main_split, stretch=1)
        root.addWidget(self._mk_statusbar())
        self._new_tab()

    # ── Toolbar ───────────────────────────────────────────────────────────────
    def _mk_toolbar(self) -> QWidget:
        t = get_theme(S().theme)
        bar = QWidget(); bar.setObjectName("wns_toolbar"); bar.setFixedHeight(44)
        hl  = QHBoxLayout(bar)
        hl.setContentsMargins(8,5,8,5); hl.setSpacing(3)

        def nb(icon, tip, checkable=False):
            b = QPushButton(icon); b.setObjectName("wns_nav")
            b.setToolTip(tip); b.setCursor(Qt.CursorShape.PointingHandCursor)
            b.setCheckable(checkable)
            return b

        self._btn_back   = nb("←", "Back  Alt+←")
        self._btn_fwd    = nb("→", "Forward  Alt+→")
        self._btn_reload = nb("↻", "Refresh  F5")
        self._btn_home   = nb("⌂", "Home  Ctrl+H")
        self._btn_back.setEnabled(False); self._btn_fwd.setEnabled(False)
        self._btn_back.clicked.connect(self._go_back)
        self._btn_fwd.clicked.connect(self._go_forward)
        self._btn_reload.clicked.connect(self._reload_or_stop)
        self._btn_home.clicked.connect(lambda: self._navigate(self.HOME_URL))
        for b in [self._btn_back, self._btn_fwd, self._btn_reload, self._btn_home]:
            hl.addWidget(b)
        hl.addSpacing(4)

        # Security icon
        self._sec_ico = QLabel("🌐")
        self._sec_ico.setFixedWidth(20)
        self._sec_ico.setStyleSheet("font-size:13px;background:transparent;")
        hl.addWidget(self._sec_ico)

        # URL bar
        self._urlbar = QLineEdit()
        self._urlbar.setObjectName("wns_urlbar")
        self._urlbar.setPlaceholderText("  Search Google or enter URL")
        self._urlbar.returnPressed.connect(self._on_url_enter)
        self._urlbar.focusInEvent = lambda e: (
            super(QLineEdit, self._urlbar).focusInEvent(e),
            QTimer.singleShot(0, self._urlbar.selectAll))[0]
        self._urlbar.textChanged.connect(self._on_urlbar_changed)
        hl.addWidget(self._urlbar, stretch=1)

        # Omnibar dropdown
        self._omni_popup = QListWidget(self)
        self._omni_popup.setWindowFlags(
            Qt.WindowType.Popup | Qt.WindowType.FramelessWindowHint)
        self._omni_popup.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self._omni_popup.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._omni_popup.setObjectName("wns_omni")
        self._omni_popup.itemClicked.connect(self._on_omni_selected)
        self._omni_popup.hide()
        hl.addSpacing(4)

        # Zoom controls: − 100% +
        self._btn_zoom_out = nb("−", "Zoom out  Ctrl+−")
        self._btn_zoom_out.clicked.connect(self._zoom_out)
        hl.addWidget(self._btn_zoom_out)

        self._zoom_lbl = QLabel("100%")
        self._zoom_lbl.setObjectName("wns_zoom")
        self._zoom_lbl.setFixedWidth(40)
        self._zoom_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._zoom_lbl.setToolTip("Reset zoom  Ctrl+0")
        self._zoom_lbl.setCursor(Qt.CursorShape.PointingHandCursor)
        self._zoom_lbl.mousePressEvent = lambda e: self._zoom_reset()
        hl.addWidget(self._zoom_lbl)

        self._btn_zoom_in = nb("+", "Zoom in  Ctrl++")
        self._btn_zoom_in.clicked.connect(self._zoom_in)
        hl.addWidget(self._btn_zoom_in)

        # Bookmark star
        self._btn_star = nb("☆", "Bookmark  Ctrl+D")
        self._btn_star.clicked.connect(self._toggle_bookmark)
        hl.addWidget(self._btn_star)

        # Reader mode
        self._btn_reader = nb("📖", "Reader mode  Ctrl+Shift+R", checkable=True)
        self._btn_reader.clicked.connect(self._toggle_reader_mode)
        hl.addWidget(self._btn_reader)

        # Dark reader
        self._btn_dark = nb("🌙", "Dark Reader  Ctrl+Shift+D", checkable=True)
        self._btn_dark.clicked.connect(self._toggle_dark_reader)
        hl.addWidget(self._btn_dark)

        # New tab
        self._btn_new = nb("+", "New tab  Ctrl+T")
        self._btn_new.setFixedSize(28, 28)
        self._btn_new.setObjectName("wns_new_tab")  # different name to avoid conflict
        self._btn_new.setStyleSheet(
            f"QPushButton#wns_new_tab{{background:transparent;"
            f"color:{t['text_dim']};border:none;border-radius:14px;"
            f"font-size:18px;}}"
            f"QPushButton#wns_new_tab:hover{{background:{t['btn_hover']};"
            f"color:{t['text']};}}")
        self._btn_new.clicked.connect(lambda: self._new_tab())
        hl.addWidget(self._btn_new)

        # Sidebar toggle
        self._btn_sidebar = nb("▤", "Panel  Ctrl+B", checkable=True)
        self._btn_sidebar.clicked.connect(self._toggle_sidebar)
        hl.addWidget(self._btn_sidebar)

        self._btn_ext = nb("🧩", "Расширения  Ctrl+Shift+U")
        self._btn_ext.setFixedSize(30, 30)
        self._btn_ext.clicked.connect(self._open_userscripts)
        hl.addWidget(self._btn_ext)

        # Menu
        btn_menu = nb("⋮", "Меню")
        btn_menu.clicked.connect(self._show_menu)
        hl.addWidget(btn_menu)
        return bar

    # ── Bookmarks bar ─────────────────────────────────────────────────────────
    def _mk_bmarks_bar(self) -> QWidget:
        self._bmarks_bar = QWidget()
        self._bmarks_bar.setObjectName("wns_bmarks")
        self._bmarks_bar.setFixedHeight(28)
        self._bmarks_lay = QHBoxLayout(self._bmarks_bar)
        self._bmarks_lay.setContentsMargins(8,2,8,2); self._bmarks_lay.setSpacing(2)
        self._refresh_bmarks()
        return self._bmarks_bar

    def _refresh_bmarks(self):
        t = get_theme(S().theme)
        lay = self._bmarks_lay
        while lay.count():
            item = lay.takeAt(0)
            if item and item.widget(): item.widget().deleteLater()
        bmarks = self._bookmarks if self._bookmarks else self._DEFAULT_BOOKMARKS
        for name, url in bmarks[:18]:
            b = QPushButton(name[:20]); b.setObjectName("wns_bmark_chip")
            b.setCursor(Qt.CursorShape.PointingHandCursor); b.setToolTip(url)
            b.clicked.connect(lambda _, u=url: self._navigate(u))
            lay.addWidget(b)
        lay.addStretch()

    # ── Find bar ──────────────────────────────────────────────────────────────
    def _mk_findbar(self) -> QWidget:
        bar = QWidget(); bar.setObjectName("wns_findbar"); bar.setFixedHeight(36)
        hl  = QHBoxLayout(bar); hl.setContentsMargins(10,4,10,4); hl.setSpacing(6)
        t   = get_theme(S().theme)

        lbl = QLabel(_L("🔍 Найти:", "🔍 Find:", "🔍 検索:"))
        lbl.setStyleSheet(f"color:{t['text_dim']};font-size:11px;background:transparent;")
        hl.addWidget(lbl)

        self._find_input = QLineEdit(); self._find_input.setObjectName("wns_find")
        self._find_input.setPlaceholderText(_L("Текст для поиска…", "Search text…", "検索テキスト…"))
        self._find_input.returnPressed.connect(self._find_next)
        self._find_input.textChanged.connect(self._find_live)
        hl.addWidget(self._find_input)

        self._find_count = QLabel("")
        self._find_count.setStyleSheet(f"color:{t['text_dim']};font-size:10px;background:transparent;")
        hl.addWidget(self._find_count)

        for icon, tip, fn in [("↑","Предыдущее",self._find_prev),
                               ("↓","Следующее", self._find_next)]:
            b = QPushButton(icon); b.setObjectName("wns_nav")
            b.setFixedSize(28,28); b.setToolTip(tip)
            b.clicked.connect(fn); hl.addWidget(b)

        self._find_case = QPushButton("Aa"); self._find_case.setObjectName("wns_nav")
        self._find_case.setFixedSize(32,28); self._find_case.setCheckable(True)
        self._find_case.setToolTip(_L("Учёт регистра", "Match case", "大文字小文字")); hl.addWidget(self._find_case)

        hl.addStretch()
        close_f = QPushButton("✕"); close_f.setObjectName("wns_nav")
        close_f.setFixedSize(28,28)
        close_f.clicked.connect(self._hide_findbar); hl.addWidget(close_f)
        return bar

    # ── Sidebar ───────────────────────────────────────────────────────────────
    def _mk_sidebar(self) -> QWidget:
        sb = QWidget(); sb.setObjectName("wns_sidebar"); sb.setFixedWidth(300)
        vl = QVBoxLayout(sb); vl.setContentsMargins(0,0,0,0); vl.setSpacing(0)
        t  = get_theme(S().theme)

        # Tab buttons
        tab_row = QWidget()
        tab_row.setStyleSheet(f"background:{t['bg3']};border-bottom:1px solid {t['border']};")
        tr_lay = QHBoxLayout(tab_row); tr_lay.setContentsMargins(0,0,0,0); tr_lay.setSpacing(0)
        for key, label in [("history","🕐 История"),("bookmarks","🔖 Закладки"),("downloads","⬇ Загрузки")]:
            b = QPushButton(label); b.setCheckable(True)
            b.setChecked(key == "history")
            b.setStyleSheet(f"""
                QPushButton{{background:transparent;color:{t['text_dim']};border:none;
                    padding:6px 10px;font-size:10px;border-bottom:2px solid transparent;}}
                QPushButton:checked{{color:{t['text']};border-bottom-color:{t['accent']};}}
                QPushButton:hover{{color:{t['text']};}}
            """)
            b.clicked.connect(lambda _,k=key: self._sidebar_switch(k))
            tr_lay.addWidget(b)
            setattr(self, f'_sb_btn_{key}', b)
        vl.addWidget(tab_row)

        # Search/filter
        self._sb_search = QLineEdit()
        self._sb_search.setPlaceholderText(_L("Фильтр…", "Filter…", "フィルター…"))
        self._sb_search.setStyleSheet(f"""
            background:{t['bg']};color:{t['text']};
            border:none;border-bottom:1px solid {t['border']};
            padding:6px 12px;font-size:11px;
        """)
        self._sb_search.textChanged.connect(self._sb_filter)
        vl.addWidget(self._sb_search)

        # List
        self._sb_list = QListWidget(); self._sb_list.setObjectName("wns_slist")
        self._sb_list.itemDoubleClicked.connect(self._sb_open)
        self._sb_list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self._sb_list.customContextMenuRequested.connect(self._sb_context_menu)
        vl.addWidget(self._sb_list, stretch=1)

        # Bottom actions
        act_row = QWidget()
        act_row.setStyleSheet(f"background:{t['bg3']};border-top:1px solid {t['border']};")
        ar_lay = QHBoxLayout(act_row); ar_lay.setContentsMargins(8,4,8,4); ar_lay.setSpacing(6)
        self._sb_clear_btn = QPushButton("🗑 Clear")
        self._sb_clear_btn.setStyleSheet(
            f"QPushButton{{background:{t['btn_bg']};color:{t['text_dim']};"
            f"border:1px solid {t['border']};border-radius:6px;padding:3px 10px;font-size:10px;}}"
            f"QPushButton:hover{{color:{t['text']};background:{t['btn_hover']};}}")
        self._sb_clear_btn.clicked.connect(self._sb_clear)
        ar_lay.addWidget(self._sb_clear_btn); ar_lay.addStretch()
        self._sb_count_lbl = QLabel("")
        self._sb_count_lbl.setStyleSheet(f"color:{t['text_dim']};font-size:9px;background:transparent;")
        ar_lay.addWidget(self._sb_count_lbl)
        vl.addWidget(act_row)

        self._sb_populate()
        return sb

    def _sidebar_switch(self, key: str):
        self._sidebar_tab = key
        for k in ("history","bookmarks","downloads"):
            btn = getattr(self, f'_sb_btn_{k}', None)
            if btn: btn.setChecked(k == key)
        self._sb_populate()

    def _sb_populate(self, filt: str = ""):
        self._sb_list.clear()
        key = self._sidebar_tab
        t   = get_theme(S().theme)

        if key == "history":
            import datetime as _dt
            shown = 0
            last_date = None
            for title, url, ts in self._history:
                if filt and filt.lower() not in url.lower() and filt.lower() not in title.lower():
                    continue
                day = _dt.datetime.fromtimestamp(ts).strftime("%d.%m.%Y")
                if day != last_date:
                    sep = QListWidgetItem(f"  📅 {day}")
                    sep.setFlags(Qt.ItemFlag.NoItemFlags)
                    sep.setForeground(__import__('PyQt6.QtGui',fromlist=['QColor']).QColor(t['accent']))
                    self._sb_list.addItem(sep)
                    last_date = day
                hm = _dt.datetime.fromtimestamp(ts).strftime("%H:%M")
                item = QListWidgetItem(f"  {hm}  {title[:30] or url[:30]}\n  {url[:50]}")
                item.setData(Qt.ItemDataRole.UserRole, url)
                item.setToolTip(url)
                self._sb_list.addItem(item)
                shown += 1
                if shown >= 200: break
            self._sb_count_lbl.setText(f"{len(self._history)} записей")

        elif key == "bookmarks":
            bmarks = self._bookmarks if self._bookmarks else self._DEFAULT_BOOKMARKS
            for name, url in bmarks:
                if filt and filt.lower() not in url.lower() and filt.lower() not in name.lower():
                    continue
                item = QListWidgetItem(f"  {name}\n  {url[:50]}")
                item.setData(Qt.ItemDataRole.UserRole, url)
                item.setToolTip(url)
                self._sb_list.addItem(item)
            self._sb_count_lbl.setText(f"{len(bmarks)} закладок")

        elif key == "downloads":
            files = sorted(self._dl_dir.iterdir(),
                key=lambda f: f.stat().st_mtime, reverse=True) \
                if self._dl_dir.exists() else []
            for f in files[:100]:
                if filt and filt.lower() not in f.name.lower(): continue
                sz = f.stat().st_size
                sz_str = f"{sz//1048576}MB" if sz>1048576 else f"{sz//1024}KB" if sz>1024 else f"{sz}B"
                item = QListWidgetItem(f"  {f.name}\n  {sz_str}")
                item.setData(Qt.ItemDataRole.UserRole, str(f))
                self._sb_list.addItem(item)
            self._sb_count_lbl.setText(f"{len(files)} файлов")

    def _sb_filter(self, text: str):
        self._sb_populate(text)

    def _sb_open(self, item: 'QListWidgetItem'):
        data = item.data(Qt.ItemDataRole.UserRole)
        if not data: return
        if self._sidebar_tab == "downloads":
            _open_system(data)
        else:
            self._new_tab(data)

    def _sb_context_menu(self, pos):
        item = self._sb_list.itemAt(pos)
        if not item: return
        data = item.data(Qt.ItemDataRole.UserRole)
        if not data: return
        menu = QMenu(self)
        if self._sidebar_tab != "downloads":
            menu.addAction("🆕 Открыть в новой вкладке",
                           lambda: self._new_tab(data))
            menu.addAction("📋 Копировать URL",
                           lambda: QApplication.clipboard().setText(data))
        else:
            menu.addAction("📂 Открыть файл",
                           lambda d=data: _open_system(d))
            menu.addAction("📋 Копировать путь",
                           lambda: QApplication.clipboard().setText(data))
            menu.addSeparator()
            menu.addAction("🗑 Удалить файл", lambda: self._del_download(data))
        menu.addSeparator()
        menu.addAction("✕ Удалить из списка", lambda: self._sb_del_item(item))
        menu.exec(self._sb_list.mapToGlobal(pos))

    def _del_download(self, path: str):
        try:
            import os as _os; _os.remove(path)
            self._sb_populate()
        except Exception as e:
            self._stat_lbl.setText(f"Ошибка удаления: {e}")

    def _sb_del_item(self, item):
        data = item.data(Qt.ItemDataRole.UserRole)
        if self._sidebar_tab == "history":
            self._history = [(t,u,ts) for t,u,ts in self._history if u != data]
            self._save_history()
        elif self._sidebar_tab == "bookmarks":
            self._bookmarks = [(n,u) for n,u in self._bookmarks if u != data]
            self._save_bookmarks(); self._refresh_bmarks()
        self._sb_populate()

    def _sb_clear(self):
        key = self._sidebar_tab
        if key == "history":
            if QMessageBox.question(self, "History", "Очистить историю?",
                    QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                    == QMessageBox.StandardButton.Yes:
                self._history.clear(); self._save_history(); self._sb_populate()
        elif key == "bookmarks":
            if QMessageBox.question(self, "Bookmarks", "Удалить все закладки?",
                    QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                    == QMessageBox.StandardButton.Yes:
                self._bookmarks.clear(); self._save_bookmarks()
                self._refresh_bmarks(); self._sb_populate()
        elif key == "downloads":
            if QMessageBox.question(self, "Downloads",
                    "Удалить все файлы из папки загрузок?",
                    QMessageBox.StandardButton.Yes|QMessageBox.StandardButton.No) \
                    == QMessageBox.StandardButton.Yes:
                import shutil as _sh
                _sh.rmtree(str(self._dl_dir), ignore_errors=True)
                self._dl_dir.mkdir(parents=True, exist_ok=True)
                self._sb_populate()

    def _toggle_sidebar(self):
        self._sidebar_vis = not self._sidebar_vis
        self._sidebar.setVisible(self._sidebar_vis)
        self._btn_sidebar.setChecked(self._sidebar_vis)
        if self._sidebar_vis:
            self._sb_populate()

    # ── Status bar ────────────────────────────────────────────────────────────
    def _mk_statusbar(self) -> QWidget:
        bar = QWidget(); bar.setObjectName("wns_statusbar"); bar.setFixedHeight(20)
        hl  = QHBoxLayout(bar); hl.setContentsMargins(0,0,10,0); hl.setSpacing(0)
        self._stat_lbl = QLabel("Done"); self._stat_lbl.setObjectName("wns_stat")
        hl.addWidget(self._stat_lbl, stretch=1)
        brand = QLabel(f"Winora NetScape {self.WNS_VERSION}")
        brand.setObjectName("wns_stat"); hl.addWidget(brand)
        return bar

    # ── Tab management ────────────────────────────────────────────────────────
    def _new_tab(self, url: str = "") -> int:
        try:
            from PyQt6.QtWebEngineWidgets import QWebEngineView
            from PyQt6.QtWebEngineCore import (QWebEngineProfile,
                QWebEngineSettings, QWebEnginePage)
            self._webengine = True

            profile = QWebEngineProfile.defaultProfile()
            # Полный Chrome User-Agent
            profile.setHttpUserAgent(
                "Mozilla/5.0 (X11; Linux x86_64) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/124.0.0.0 Safari/537.36")
            # Дополнительные заголовки для обхода блокировок
            profile.setHttpAcceptLanguage("en-US,en;q=0.9,ru;q=0.8")
            try:
                profile.downloadRequested.connect(
                    self._on_download, Qt.ConnectionType.UniqueConnection)
            except Exception: pass

            s = profile.settings()
            s.setAttribute(QWebEngineSettings.WebAttribute.JavascriptEnabled, True)
            s.setAttribute(QWebEngineSettings.WebAttribute.LocalStorageEnabled, True)
            s.setAttribute(QWebEngineSettings.WebAttribute.ScrollAnimatorEnabled, True)
            s.setAttribute(QWebEngineSettings.WebAttribute.PluginsEnabled, True)
            s.setAttribute(QWebEngineSettings.WebAttribute.FullScreenSupportEnabled, True)
            # Включаем DNS-over-HTTPS для обхода DNS-блокировок
            try:
                s.setAttribute(QWebEngineSettings.WebAttribute.DnsPrefetchEnabled, True)
            except Exception: pass
            # Разрешаем загрузку с незащищённых источников
            try:
                s.setAttribute(QWebEngineSettings.WebAttribute.AllowRunningInsecureContent, True)
            except Exception: pass

            view = QWebEngineView()
            view.loadStarted.connect(lambda: self._on_load_start(view))
            view.loadProgress.connect(self._prog.setValue)
            view.loadFinished.connect(lambda ok: self._on_load_finish(view, ok))
            view.urlChanged.connect(lambda u: self._on_url_change(view, u))
            view.titleChanged.connect(lambda ttl: self._on_title_change(view, ttl))
            view.iconChanged.connect(lambda ico: self._on_icon_change(view, ico))
            # Zoom wheel
            view.wheelEvent = lambda e, v=view: self._wheel_zoom(e, v)
            try:
                view.page().newWindowRequested.connect(self._on_new_window)
            except Exception: pass

            idx = self._tabs.addTab(view, "New tab")
            self._tabs.setCurrentIndex(idx)

            if url and url not in (self.HOME_URL, "wns://newtab"):
                from PyQt6.QtCore import QUrl
                view.load(QUrl(url))
            else:
                from PyQt6.QtCore import QUrl as QU
                view.setHtml(self._themed_home_html(), QU("wns://newtab"))
            return idx

        except Exception as _wns_err:
            print(f"[WNS] _new_tab failed: {type(_wns_err).__name__}: {_wns_err}")
            import traceback as _wns_tb; _wns_tb.print_exc()
            return self._new_tab_fallback(url)

    def _new_tab_fallback(self, url: str = "") -> int:
        t = get_theme(S().theme)
        w = QWidget(); w.setStyleSheet(f"background:{t['bg']};")
        vl = QVBoxLayout(w); vl.setAlignment(Qt.AlignmentFlag.AlignCenter); vl.setSpacing(16)
        lgo = QLabel("🌐"); lgo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        lgo.setStyleSheet("font-size:64px;background:transparent;"); vl.addWidget(lgo)
        ttl = QLabel("Winora NetScape 3.0")
        ttl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ttl.setStyleSheet(f"font-size:22px;font-weight:bold;color:{t['text']};background:transparent;")
        vl.addWidget(ttl)
        note = QLabel(
            "Встроенный браузер требует PyQt6-WebEngine.\n\n"
            "Установи:\n  pip install PyQt6-WebEngine --break-system-packages\n"
            "или:  emerge -av dev-python/pyqt6-webengine")
        note.setAlignment(Qt.AlignmentFlag.AlignCenter)
        note.setStyleSheet(f"color:{t['text_dim']};font-size:11px;background:transparent;")
        vl.addWidget(note)
        if url and url not in (self.HOME_URL, "wns://newtab"):
            ul = QLabel(url); ul.setAlignment(Qt.AlignmentFlag.AlignCenter)
            ul.setStyleSheet(f"color:{t['accent']};font-size:11px;background:transparent;")
            vl.addWidget(ul)
            ob = QPushButton("🌐 Открыть во внешнем браузере")
            ob.setObjectName("accent_btn"); ob.setFixedHeight(36)
            ob.clicked.connect(lambda: __import__('webbrowser').open(url))
            vl.addWidget(ob)
        idx = self._tabs.addTab(w, "WNS"); self._tabs.setCurrentIndex(idx)
        return idx

    def _close_tab(self, idx: int):
        if self._tabs.count() <= 1:
            # Yandex-style: reset to home instead of closing last tab
            w = self._tabs.widget(0)
            if w and hasattr(w, 'setHtml'):
                from PyQt6.QtCore import QUrl
                w.setHtml(self._themed_home_html(), QUrl("wns://newtab"))
                self._tabs.setTabText(0, "New tab")
                if hasattr(self, '_urlbar'): self._urlbar.clear()
            return
        w = self._tabs.widget(idx)
        self._tabs.removeTab(idx)
        if w: w.deleteLater()

    def _on_tab_switch(self, idx: int):
        w = self._tabs.widget(idx)
        if w and hasattr(w, 'url'):
            try:
                u = w.url().toString()
                show = "" if u.startswith("data:") or "wns://newtab" in u else u
                self._urlbar.setText(show)
                self._update_sec_icon(u)
                self._update_star(u)
                h = w.history() if hasattr(w,'history') else None
                if h:
                    self._btn_back.setEnabled(h.canGoBack())
                    self._btn_fwd.setEnabled(h.canGoForward())
                # Sync zoom
                if hasattr(w, 'zoomFactor'):
                    self._zoom_level = w.zoomFactor()
                    self._zoom_lbl.setText(f"{int(self._zoom_level*100)}%")
            except Exception: pass

    # ── Navigation ────────────────────────────────────────────────────────────
    def _on_url_enter(self):
        self._omni_popup.hide()
        raw = self._urlbar.text().strip()
        if not raw: return
        if raw.startswith(("http://","https://","file://","ftp://")):
            url = raw
        elif "." in raw and " " not in raw and len(raw) > 3 and "/" not in raw.split(".")[0]:
            url = "https://" + raw
        else:
            from urllib.parse import quote_plus
            tpl = self._SEARCH_ENGINES.get(self._search_engine,
                "https://www.google.com/search?q={}")
            url = tpl.format(quote_plus(raw))
        self._navigate(url)

    def _on_urlbar_changed(self, text: str):
        """Hide omni popup — prevents focus steal."""
        self._omni_popup.hide()
    def _on_omni_selected(self, item):
        url = item.data(Qt.ItemDataRole.UserRole)
        self._omni_popup.hide()
        if url.startswith("__search__"):
            from urllib.parse import quote_plus
            q = url[10:]
            tpl = self._SEARCH_ENGINES.get(self._search_engine,
                "https://www.google.com/search?q={}")
            self._navigate(tpl.format(quote_plus(q)))
        else:
            self._urlbar.setText(url)
            self._navigate(url)

    def _navigate(self, url: str):
        if url in (self.HOME_URL, "wns://newtab"):
            w = self._tabs.currentWidget()
            if w and hasattr(w, 'setHtml'):
                from PyQt6.QtCore import QUrl as QU
                w.setHtml(self._themed_home_html(), QU("wns://newtab"))
                self._urlbar.setText("")
            return
        w = self._tabs.currentWidget()
        if w and hasattr(w, 'load'):
            from PyQt6.QtCore import QUrl
            w.load(QUrl(url))
            self._urlbar.setText(url)
        else:
            import webbrowser; webbrowser.open(url)

    def _go_back(self):
        w = self._tabs.currentWidget()
        if w and hasattr(w,'back'): w.back()

    def _go_forward(self):
        w = self._tabs.currentWidget()
        if w and hasattr(w,'forward'): w.forward()

    def _reload_or_stop(self):
        w = self._tabs.currentWidget()
        if not w: return
        if self._btn_reload.text() == "✕":
            if hasattr(w,'stop'): w.stop()
        else:
            if hasattr(w,'reload'): w.reload()

    # ── Zoom ──────────────────────────────────────────────────────────────────
    def _wheel_zoom(self, event, view):
        from PyQt6.QtCore import Qt as _Qt
        if event.modifiers() & _Qt.KeyboardModifier.ControlModifier:
            delta = event.angleDelta().y()
            self._zoom_level = max(0.25, min(5.0, self._zoom_level + (0.1 if delta>0 else -0.1)))
            view.setZoomFactor(self._zoom_level)
            self._zoom_lbl.setText(f"{int(self._zoom_level*100)}%")
        else:
            from PyQt6.QtWebEngineWidgets import QWebEngineView
            QWebEngineView.wheelEvent(view, event)

    def _zoom_in(self):
        self._zoom_level = min(5.0, self._zoom_level + 0.1)
        self._apply_zoom()

    def _zoom_out(self):
        self._zoom_level = max(0.25, self._zoom_level - 0.1)
        self._apply_zoom()

    def _zoom_reset(self):
        self._zoom_level = 1.0; self._apply_zoom()

    def _apply_zoom(self):
        w = self._tabs.currentWidget()
        if w and hasattr(w,'setZoomFactor'):
            w.setZoomFactor(self._zoom_level)
            self._zoom_lbl.setText(f"{int(self._zoom_level*100)}%")

    # ── WebEngine callbacks ───────────────────────────────────────────────────
    def _on_load_start(self, view):
        self._prog.setValue(0); self._prog.setVisible(True)
        self._stat_lbl.setText(_L("Загрузка…", "Loading…", "読み込み中…"))
        self._btn_reload.setText("✕"); self._btn_reload.setToolTip("Stop")

    def _on_load_finish(self, view, ok: bool):
        self._prog.setVisible(False)
        self._btn_reload.setText("↻"); self._btn_reload.setToolTip("Обновить (F5)")
        self._stat_lbl.setText("Done" if ok else "⚠ Ошибка загрузки")
        if hasattr(view,'history'):
            h = view.history()
            self._btn_back.setEnabled(h.canGoBack())
            self._btn_fwd.setEnabled(h.canGoForward())
        # Apply dark reader if active
        if self._dark_reader and ok:
            view.page().runJavaScript(_WNS_DARK_READER_JS)

    def _on_url_change(self, view, qurl):
        if view != self._tabs.currentWidget(): return
        u = qurl.toString()
        is_internal = (not u or u.startswith("data:")
                       or "wns://newtab" in u or u == "about:blank")
        show = "" if is_internal else u
        if not self._urlbar.hasFocus():
            self._urlbar.setText(show)
        self._update_sec_icon(u)
        self._update_star(u)
        # History
        if u and not u.startswith("data:") and "wns://newtab" not in u:
            title = self._tabs.tabText(self._tabs.currentIndex()) or u
            self._history.insert(0, (title, u, time.time()))
            if len(self._history) > 1000:
                self._history = self._history[:1000]
            self._save_history()
            if self._sidebar_vis and self._sidebar_tab == "history":
                self._sb_populate()

    def _on_title_change(self, view, title: str):
        idx = self._tabs.indexOf(view)
        if idx >= 0:
            short = (title[:20]+"…") if len(title)>20 else (title or "Без названия")
            self._tabs.setTabText(idx, short)
            self._tabs.setTabToolTip(idx, title)
            # Update history entry
            if self._history:
                t_old, u, ts = self._history[0]
                if abs(time.time()-ts) < 10:
                    self._history[0] = (title, u, ts)

    def _on_icon_change(self, view, icon):
        idx = self._tabs.indexOf(view)
        if idx >= 0 and not icon.isNull():
            self._tabs.setTabIcon(idx, icon)

    def _on_new_window(self, req):
        try:
            url = req.requestedUrl().toString()
            self._new_tab(url)
        except Exception: pass

    def _update_sec_icon(self, url: str):
        if url.startswith("https://"):
            self._sec_ico.setText("🔒"); self._sec_ico.setToolTip("HTTPS")
            self._sec_ico.setStyleSheet("font-size:13px;background:transparent;color:#80FF80;")
        elif url.startswith("http://"):
            self._sec_ico.setText("🔓"); self._sec_ico.setToolTip("HTTP — не защищено")
            self._sec_ico.setStyleSheet("font-size:13px;background:transparent;color:#FFA060;")
        else:
            self._sec_ico.setText("🌐"); self._sec_ico.setToolTip("")
            self._sec_ico.setStyleSheet("font-size:13px;background:transparent;")

    def _update_star(self, url: str):
        is_bm = any(u == url for _,u in self._bookmarks)
        self._btn_star.setText("★" if is_bm else "☆")

    # ── Find in page ──────────────────────────────────────────────────────────
    def _show_findbar(self):
        self._find_visible = True
        self._findbar.setVisible(True)
        self._find_input.setFocus()
        self._find_input.selectAll()

    def _hide_findbar(self):
        self._find_visible = False
        self._findbar.setVisible(False)
        w = self._tabs.currentWidget()
        if w and hasattr(w,'findText'):
            w.findText("")   # clear highlight

    def _find_live(self, text: str):
        if text: self._find_text(text)

    def _find_text(self, text: str = ""):
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'findText')): return
        text = text or self._find_input.text()
        if not text: return
        try:
            from PyQt6.QtWebEngineCore import QWebEnginePage
            flags = QWebEnginePage.FindFlag(0)
            if self._find_case.isChecked():
                flags |= QWebEnginePage.FindFlag.FindCaseSensitively
            w.findText(text, flags)
        except Exception:
            w.findText(text)

    def _find_next(self):
        self._find_text()

    def _find_prev(self):
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'findText')): return
        text = self._find_input.text()
        if not text: return
        try:
            from PyQt6.QtWebEngineCore import QWebEnginePage
            flags = QWebEnginePage.FindFlag.FindBackward
            if self._find_case.isChecked():
                flags |= QWebEnginePage.FindFlag.FindCaseSensitively
            w.findText(text, flags)
        except Exception:
            w.findText(text)

    # ── Reader mode ───────────────────────────────────────────────────────────
    def _toggle_reader_mode(self):
        self._reader_mode = not self._reader_mode
        self._btn_reader.setChecked(self._reader_mode)
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'page')): return
        if self._reader_mode:
            w.page().runJavaScript(_WNS_READER_JS)
            self._stat_lbl.setText("📖 Режим чтения")
        else:
            if hasattr(w,'reload'): w.reload()
            self._stat_lbl.setText("Режим чтения выключен")

    # ── Dark Reader ───────────────────────────────────────────────────────────
    def _toggle_dark_reader(self):
        self._dark_reader = not self._dark_reader
        self._btn_dark.setChecked(self._dark_reader)
        w = self._tabs.currentWidget()
        if w and hasattr(w,'page'):
            if self._dark_reader:
                w.page().runJavaScript(_WNS_DARK_READER_JS)
                self._stat_lbl.setText("🌙 Dark Reader включён")
            else:
                # Remove by reload
                if hasattr(w,'reload'): w.reload()
                self._stat_lbl.setText("🌙 Dark Reader выключен")

    # ── Screenshot ────────────────────────────────────────────────────────────
    def _screenshot(self):
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'grab')): return
        pix = w.grab()
        path, _ = QFileDialog.getSaveFileName(self, "Сохранить скриншот",
            str(self._dl_dir / f"screenshot_{int(time.time())}.png"),
            "PNG (*.png);;JPEG (*.jpg)")
        if path:
            pix.save(path)
            self._stat_lbl.setText(f"📸 Сохранено: {Path(path).name}")

    # ── DevTools ──────────────────────────────────────────────────────────────
    def _open_devtools(self):
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'page')): return
        try:
            dev_view = __import__('PyQt6.QtWebEngineWidgets',
                fromlist=['QWebEngineView']).QWebEngineView()
            dev_view.resize(1000, 600)
            dev_view.setWindowTitle("DevTools — Winora NetScape")
            w.page().setDevToolsPage(dev_view.page())
            dev_view.show()
            self._stat_lbl.setText("🔧 DevTools открыты")
        except Exception as e:
            self._stat_lbl.setText(f"DevTools: {e}")

    # ── Picture-in-Picture / Mute ─────────────────────────────────────────────
    def _toggle_mute_page(self):
        w = self._tabs.currentWidget()
        if w and hasattr(w,'page'):
            try:
                w.page().setAudioMuted(not w.page().isAudioMuted())
                muted = w.page().isAudioMuted()
                self._stat_lbl.setText("🔇 Звук выключен" if muted else "🔊 Звук включён")
            except Exception: pass

    # ── Downloads ─────────────────────────────────────────────────────────────
    def _on_download(self, item):
        try:
            item.setDownloadDirectory(str(self._dl_dir))
            item.accept()
            fname = item.downloadFileName()
            self._stat_lbl.setText(f"⬇ Загружается: {fname}")
            try:
                item.isFinishedChanged.connect(lambda: (
                    self._stat_lbl.setText(f"✓ Загружено: {fname}"),
                    self._sb_populate() if (self._sidebar_vis and self._sidebar_tab=="downloads") else None
                ))
            except Exception: pass
        except Exception as e:
            self._stat_lbl.setText(f"⚠ Загрузка: {e}")

    # ── Bookmarks ─────────────────────────────────────────────────────────────
    def _bmark_chip_menu(self, idx, btn, pos):
        """Right-click: open/rename/delete bookmark chip."""
        if idx >= len(self._bookmarks): return
        bm = self._bookmarks[idx]
        url = bm.get('url','')
        title = bm.get('title', url[:30])
        m = QMenu(self)
        m.addAction(f"  {url[:48]}")
        m.actions()[0].setEnabled(False)
        m.addSeparator()
        open_a  = m.addAction("↗  Открыть")
        edit_a  = m.addAction("✏  Переименовать")
        del_a   = m.addAction("🗑  Удалить")
        act = m.exec(btn.mapToGlobal(pos))
        if act == open_a:
            self._navigate(url)
        elif act == edit_a:
            new_t, ok = QInputDialog.getText(
                self, "Переименовать закладку", _L("Название:", "Name:", "名前:"), text=title)
            if ok and new_t.strip():
                self._bookmarks[idx]['title'] = new_t.strip()
                self._save_bookmarks(); self._refresh_bmarks()
        elif act == del_a:
            self._bookmarks.pop(idx)
            self._save_bookmarks(); self._refresh_bmarks()

    def _toggle_bookmark(self):
        w = self._tabs.currentWidget()
        if not (w and hasattr(w,'url')): return
        url   = w.url().toString()
        title = self._tabs.tabText(self._tabs.currentIndex())
        if any(u == url for _,u in self._bookmarks):
            self._bookmarks = [(n,u) for n,u in self._bookmarks if u != url]
            self._btn_star.setText("☆"); self._stat_lbl.setText("Закладка удалена")
        else:
            self._bookmarks.insert(0, (title[:22], url))
            self._btn_star.setText("★"); self._stat_lbl.setText("★ Закладка добавлена")
        self._save_bookmarks(); self._refresh_bmarks()
        if self._sidebar_vis and self._sidebar_tab=="bookmarks":
            self._sb_populate()

    def _save_bookmarks(self):
        try:
            import json
            (DATA_DIR/"wns_bookmarks.json").write_text(
                json.dumps(self._bookmarks), encoding="utf-8")
        except Exception: pass

    def _load_bookmarks(self):
        try:
            import json
            f = DATA_DIR/"wns_bookmarks.json"
            if f.exists(): self._bookmarks = json.loads(f.read_text(encoding="utf-8"))
        except Exception: pass

    def _save_history(self):
        try:
            import json
            (DATA_DIR/"wns_history.json").write_text(
                json.dumps(self._history[:500]), encoding="utf-8")
        except Exception: pass

    def _load_history(self):
        try:
            import json
            f = DATA_DIR/"wns_history.json"
            if f.exists(): self._history = json.loads(f.read_text(encoding="utf-8"))
        except Exception: pass

    # ── Menu ──────────────────────────────────────────────────────────────────
    def _show_menu(self):
        menu = QMenu(self)
        menu.addAction("🆕 Новая вкладка         Ctrl+T",   lambda: self._new_tab())
        menu.addAction("🔄 Обновить                 F5",    self._reload_or_stop)
        menu.addAction("✕  Закрыть вкладку      Ctrl+W",    lambda: self._close_tab(self._tabs.currentIndex()))
        menu.addSeparator()
        menu.addAction("🔍 Найти на странице    Ctrl+F",    self._show_findbar)
        menu.addAction("📖 Режим чтения   Ctrl+Shift+R",   self._toggle_reader_mode)
        menu.addAction("🌙 Dark Reader    Ctrl+Shift+D",   self._toggle_dark_reader)
        menu.addSeparator()

        zoom_menu = menu.addMenu("🔎 Масштаб")
        zoom_menu.addAction("➕ Увеличить   Ctrl++", self._zoom_in)
        zoom_menu.addAction("➖ Уменьшить   Ctrl+-", self._zoom_out)
        zoom_menu.addAction("↺  Сбросить     Ctrl+0", self._zoom_reset)
        for pct in [75, 100, 125, 150, 200]:
            zoom_menu.addAction(f"  {pct}%", lambda _,p=pct: self._set_zoom(p))

        menu.addSeparator()
        menu.addAction("📸 Скриншот страницы",            self._screenshot)
        menu.addAction("🔧 Инструменты разработчика  F12",self._open_devtools)
        menu.addAction("🔇 Выкл/вкл звук вкладки",       self._toggle_mute_page)
        menu.addSeparator()
        menu.addAction("💾 Сохранить страницу",            self._save_page)
        menu.addAction("🖨 Печать               Ctrl+P",  self._print_page)
        menu.addSeparator()

        se_menu = menu.addMenu("🔍 Поисковик")
        for name in self._SEARCH_ENGINES:
            act = se_menu.addAction(("✓ " if name==self._search_engine else "   ") + name)
            act.triggered.connect(lambda _,n=name: self._set_search_engine(n))

        menu.addSeparator()
        menu.addAction("🧩 Расширения  Ctrl+Shift+E",     self._open_extensions)
        menu.addAction("🔑 Пароли  Ctrl+Shift+P",         self._open_password_manager)
        menu.addAction("🕵 Инкогнито  Ctrl+Shift+N",      self._new_incognito_tab)
        menu.addSeparator()
        menu.addAction("ℹ О Winora NetScape",              self._show_about)
        menu.addAction("✕ Закрыть WNS",                    self.close)

        btn = self.sender()
        pos = btn.mapToGlobal(btn.rect().bottomLeft()) if btn else self.mapToGlobal(self.rect().center())
        menu.exec(pos)

    def _set_zoom(self, pct: int):
        self._zoom_level = pct / 100
        self._apply_zoom()

    def _set_search_engine(self, name: str):
        self._search_engine = name
        self._stat_lbl.setText(f"Поисковик: {name}")

    def _save_page(self):
        w = self._tabs.currentWidget()
        if not hasattr(w,'page'): return
        path, _ = QFileDialog.getSaveFileName(self, "Сохранить страницу",
            str(self._dl_dir/"page.html"), "HTML (*.html *.mhtml)")
        if path:
            try:
                from PyQt6.QtWebEngineCore import QWebEnginePage
                w.page().save(path, QWebEnginePage.SavePageFormat.MimeHtml)
                self._stat_lbl.setText(f"💾 Сохранено: {Path(path).name}")
            except Exception as e:
                self._stat_lbl.setText(f"Ошибка: {e}")

    def _print_page(self):
        w = self._tabs.currentWidget()
        if not hasattr(w,'page'): return
        try:
            from PyQt6.QtPrintSupport import QPrinter, QPrintDialog
            pr = QPrinter(); dlg = QPrintDialog(pr, self)
            if dlg.exec() == QDialog.DialogCode.Accepted:
                w.page().print(pr, lambda ok: None)
        except ImportError:
            QMessageBox.information(self,"Печать","PyQt6-PrintSupport не установлен.")

    # ── Password Manager ──────────────────────────────────────────────────────
    def _open_password_manager(self):
        import json as _j
        t = get_theme(S().theme)
        dlg = QDialog(self)
        dlg.setWindowTitle("🔑 Пароли")
        dlg.resize(580, 380)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        vl = QVBoxLayout(dlg)
        vl.setContentsMargins(16,16,16,16); vl.setSpacing(10)
        hdr = QLabel("🔑 Сохранённые пароли (двойной клик = копировать)")
        hdr.setStyleSheet(
            f"font-size:13px;font-weight:bold;color:{t['accent']};background:transparent;")
        vl.addWidget(hdr)
        raw = S().get("wns_passwords", "{}", t=str)
        try: pwds = _j.loads(raw)
        except: pwds = {}
        tbl = QTableWidget(len(pwds), 3)
        tbl.setHorizontalHeaderLabels(["Сайт","Логин","Пароль"])
        tbl.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.Stretch)
        tbl.setStyleSheet(
            f"QTableWidget{{background:{t['bg']};color:{t['text']};border:none;}}"
            f"QHeaderView::section{{background:{t['bg3']};color:{t['text_dim']};"
            "border:none;padding:4px;}}")
        for i,(site,creds) in enumerate(pwds.items()):
            tbl.setItem(i,0,QTableWidgetItem(site))
            tbl.setItem(i,1,QTableWidgetItem(creds.get("login","")))
            pi = QTableWidgetItem("••••••••")
            pi.setData(Qt.ItemDataRole.UserRole, creds.get("password",""))
            tbl.setItem(i,2,pi)
        tbl.doubleClicked.connect(lambda idx: QApplication.clipboard().setText(
            tbl.item(idx.row(),2).data(Qt.ItemDataRole.UserRole) or ""
            ) if tbl.item(idx.row(),2) else None)
        vl.addWidget(tbl, stretch=1)
        row = QHBoxLayout()
        site_e = QLineEdit(); site_e.setPlaceholderText("Сайт")
        login_e= QLineEdit(); login_e.setPlaceholderText("Логин")
        pw_e   = QLineEdit(); pw_e.setPlaceholderText("Пароль")
        pw_e.setEchoMode(QLineEdit.EchoMode.Password)
        for w in [site_e, login_e, pw_e]:
            w.setStyleSheet(
                f"background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:6px;padding:4px 8px;")
            row.addWidget(w)
        add_b = QPushButton("➕")
        add_b.setObjectName("accent_btn"); add_b.setFixedSize(32,32)
        def _add():
            s=site_e.text().strip(); l=login_e.text().strip(); p=pw_e.text()
            if s and p:
                pwds[s]={"login":l,"password":p}
                S().set("wns_passwords",_j.dumps(pwds))
                r=tbl.rowCount(); tbl.insertRow(r)
                tbl.setItem(r,0,QTableWidgetItem(s))
                tbl.setItem(r,1,QTableWidgetItem(l))
                pi2=QTableWidgetItem("••••••••")
                pi2.setData(Qt.ItemDataRole.UserRole,p)
                tbl.setItem(r,2,pi2)
                site_e.clear(); login_e.clear(); pw_e.clear()
        add_b.clicked.connect(_add); row.addWidget(add_b)
        vl.addLayout(row)
        dlg.exec()

    def _new_incognito_tab(self):
        try:
            from PyQt6.QtWebEngineWidgets import QWebEngineView
            from PyQt6.QtWebEngineCore import QWebEngineProfile, QWebEnginePage
            from PyQt6.QtCore import QUrl
            profile = QWebEngineProfile(self)
            view = QWebEngineView()
            page = QWebEnginePage(profile, view)
            view.setPage(page)
            view.loadStarted.connect(lambda: self._on_load_start(view))
            view.loadProgress.connect(self._prog.setValue)
            view.loadFinished.connect(lambda ok: self._on_load_finish(view, ok))
            view.urlChanged.connect(lambda u: (
                self._urlbar.setText(u.toString())
                if view == self._tabs.currentWidget() else None))
            view.titleChanged.connect(lambda tt: (
                self._tabs.setTabText(self._tabs.indexOf(view),
                    f"🕵 {tt[:18]}") if tt else None))
            view.wheelEvent = lambda e, v=view: self._wheel_zoom(e, v)
            view.setHtml(WNS_HOME_HTML, QUrl("wns://newtab"))
            idx = self._tabs.addTab(view, "🕵 Инкогнито")
            self._tabs.setCurrentIndex(idx)
            from PyQt6.QtGui import QColor
            self._tabs.tabBar().setTabTextColor(idx, QColor("#A0A0FF"))
        except Exception as e:
            self._stat_lbl.setText(f"Инкогнито: {e}")

    def _open_userscripts(self):
        import json as _j
        t = get_theme(S().theme)
        dlg = QDialog(self)
        dlg.setWindowTitle("📜 Userscripts")
        dlg.resize(700, 480)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        vl = QVBoxLayout(dlg)
        vl.setContentsMargins(16,16,16,16); vl.setSpacing(8)
        raw = S().get("wns_userscripts","[]",t=str)
        try: scripts = _j.loads(raw)
        except: scripts = []
        sp = QSplitter()
        lw = QListWidget()
        lw.setStyleSheet(
            f"QListWidget{{background:{t['bg']};color:{t['text']};border:none;}}"
            f"QListWidget::item{{padding:8px;border-bottom:1px solid {t['border']};}}"
            f"QListWidget::item:selected{{background:{t['accent']};color:white;}}")
        for sc in scripts:
            it = QListWidgetItem(f"{'✅' if sc.get('enabled',True) else '⬜'} {sc.get('name','?')}")
            it.setData(Qt.ItemDataRole.UserRole, sc)
            lw.addItem(it)
        rw = QWidget(); rl = QVBoxLayout(rw)
        rl.setContentsMargins(8,0,0,0)
        name_e = QLineEdit(); name_e.setPlaceholderText("Название")
        match_e= QLineEdit(); match_e.setPlaceholderText("URL паттерн (* = все)")
        code_e = QPlainTextEdit()
        code_e.setPlaceholderText("// JavaScript код")
        code_e.setStyleSheet(
            f"QPlainTextEdit{{background:#0A0A14;color:#90EE90;"
            "font-family:monospace;font-size:11px;"
            f"border:1px solid {t['border']};border-radius:6px;padding:8px;}}")
        for w in [name_e, match_e]:
            w.setStyleSheet(
                f"background:{t['bg3']};color:{t['text']};"
                f"border:1px solid {t['border']};border-radius:6px;padding:4px 8px;")
        def _save():
            n=name_e.text().strip() or "Script"; m=match_e.text().strip() or "*"
            c=code_e.toPlainText()
            sc={"name":n,"match":m,"code":c,"enabled":True}
            scripts.append(sc)
            S().set("wns_userscripts",_j.dumps(scripts))
            it=QListWidgetItem(f"✅ {n}")
            it.setData(Qt.ItemDataRole.UserRole,sc)
            lw.addItem(it)
        def _run():
            c=code_e.toPlainText()
            w2=self._tabs.currentWidget()
            if w2 and hasattr(w2,'page') and c:
                w2.page().runJavaScript(c)
        def _load(it):
            sc=it.data(Qt.ItemDataRole.UserRole)
            if sc:
                name_e.setText(sc.get("name",""))
                match_e.setText(sc.get("match","*"))
                code_e.setPlainText(sc.get("code",""))
        lw.itemClicked.connect(_load)
        save_b=QPushButton(_L("💾 Сохранить", "💾 Save", "💾 保存")); save_b.setObjectName("accent_btn")
        run_b=QPushButton("▶ Запустить на текущей"); run_b.setFixedHeight(32)
        run_b.setStyleSheet(
            f"QPushButton{{background:{t['bg3']};color:{t['text']};"
            f"border:1px solid {t['border']};border-radius:6px;}}"
            f"QPushButton:hover{{background:{t['btn_hover']};}}")
        save_b.clicked.connect(_save); run_b.clicked.connect(_run)
        rl.addWidget(QLabel(_L("Название:", "Name:", "名前:"))); rl.addWidget(name_e)
        rl.addWidget(QLabel("URL (паттерн):")); rl.addWidget(match_e)
        rl.addWidget(QLabel("Код:")); rl.addWidget(code_e,stretch=1)
        br=QHBoxLayout(); br.addWidget(save_b); br.addWidget(run_b)
        rl.addLayout(br)
        sp.addWidget(lw); sp.addWidget(rw); sp.setSizes([200,500])
        vl.addWidget(sp,stretch=1)
        dlg.exec()

    def _run_userscripts_for_url(self, url, view):
        import json as _j, fnmatch
        raw = S().get("wns_userscripts","[]",t=str)
        try: scripts = _j.loads(raw)
        except: return
        for sc in scripts:
            if not sc.get("enabled",True): continue
            if fnmatch.fnmatch(url, sc.get("match","*")) or sc.get("match","*")=="*":
                if sc.get("code") and hasattr(view,"page"):
                    view.page().runJavaScript(sc["code"])

    def _open_extensions(self):
        """Extensions panel: userscripts list + Chrome Web Store link."""
        import json as _j
        t = get_theme(S().theme)
        dlg = QDialog(self)
        dlg.setWindowTitle("🧩 Расширения WNS")
        dlg.resize(720, 500)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        vl = QVBoxLayout(dlg); vl.setContentsMargins(0,0,0,0); vl.setSpacing(0)

        # Header bar
        hdr = QWidget(); hdr.setFixedHeight(52)
        hdr.setStyleSheet(f"background:{t['bg3']};border-bottom:1px solid {t['border']};")
        hl2 = QHBoxLayout(hdr); hl2.setContentsMargins(16,0,16,0); hl2.setSpacing(12)
        ttl = QLabel("🧩  Расширения WNS")
        ttl.setStyleSheet(f"font-size:15px;font-weight:bold;color:{t['text']};background:transparent;")
        hl2.addWidget(ttl); hl2.addStretch()
        store_btn = QPushButton("🏪  Chrome Web Store")
        store_btn.setObjectName("accent_btn"); store_btn.setFixedHeight(34)
        store_btn.clicked.connect(lambda: (
            dlg.accept(),
            self._new_tab("https://chrome.google.com/webstore/category/extensions")))
        hl2.addWidget(store_btn)
        vl.addWidget(hdr)

        # Tabs
        inner_tabs = QTabWidget()
        inner_tabs.setStyleSheet(
            f"QTabWidget::pane{{background:{t['bg2']};border:none;}}"
            f"QTabBar::tab{{background:{t['bg3']};color:{t['text_dim']};"
            "padding:8px 20px;border:none;font-size:12px;}}"
            f"QTabBar::tab:selected{{background:{t['bg2']};color:{t['text']};"
            f"border-bottom:2px solid {t['accent']};}}")
        vl.addWidget(inner_tabs, stretch=1)

        # ── Userscripts tab ──────────────────────────────────────────────────
        us_w = QWidget(); us_l = QVBoxLayout(us_w)
        us_l.setContentsMargins(16,12,16,12); us_l.setSpacing(8)
        us_hdr_row = QHBoxLayout()
        us_hdr_row.addWidget(QLabel("Скрипты запускаются автоматически при загрузке страниц"))
        us_hdr_row.addStretch()
        add_btn = QPushButton("➕ Новый скрипт")
        add_btn.setObjectName("accent_btn"); add_btn.setFixedHeight(30)
        add_btn.clicked.connect(lambda: (dlg.accept(), self._open_userscripts()))
        us_hdr_row.addWidget(add_btn)
        us_l.addLayout(us_hdr_row)

        raw = S().get("wns_userscripts", "[]", t=str)
        try: scripts = _j.loads(raw)
        except: scripts = []

        if not scripts:
            empty = QLabel("Нет установленных скриптов. Нажмите «Новый скрипт».")
            empty.setAlignment(Qt.AlignmentFlag.AlignCenter)
            empty.setStyleSheet(f"color:{t['text_dim']};font-size:12px;background:transparent;")
            us_l.addWidget(empty, stretch=1)
        else:
            for i, sc in enumerate(scripts):
                card = QFrame()
                card.setStyleSheet(
                    f"QFrame{{background:{t['bg3']};border-radius:8px;"
                    f"border:1px solid {t['border']};margin:1px 0;}}")
                cl = QHBoxLayout(card); cl.setContentsMargins(12,8,12,8)
                tog = QCheckBox(); tog.setChecked(sc.get('enabled', True))
                cl.addWidget(tog)
                info_l = QVBoxLayout()
                nm = QLabel(sc.get('name', 'Без названия'))
                nm.setStyleSheet(
                    f"font-size:12px;font-weight:bold;color:{t['text']};background:transparent;")
                mt = QLabel(sc.get('match', '*'))
                mt.setStyleSheet(
                    f"font-size:10px;color:{t['text_dim']};background:transparent;")
                info_l.addWidget(nm); info_l.addWidget(mt)
                cl.addLayout(info_l, stretch=1)
                del_b = QPushButton("🗑")
                del_b.setFixedSize(28,28)
                del_b.setStyleSheet(
                    f"QPushButton{{background:transparent;color:{t['text_dim']};"
                    "border:none;border-radius:4px;}}"
                    f"QPushButton:hover{{background:#FF444430;color:#FF4444;}}")
                def _del(_i=i, _s=scripts):
                    _s.pop(_i); S().set("wns_userscripts",_j.dumps(_s))
                    dlg.accept(); self._open_extensions()
                del_b.clicked.connect(_del)
                cl.addWidget(del_b)
                def _tog(v, _i=i, _s=scripts):
                    _s[_i]['enabled'] = v; S().set("wns_userscripts",_j.dumps(_s))
                tog.toggled.connect(_tog)
                us_l.addWidget(card)
            us_l.addStretch()
        inner_tabs.addTab(us_w, "📜 Userscripts")

        # ── Chrome Extensions tab ────────────────────────────────────────────
        crx_w = QWidget(); crx_l = QVBoxLayout(crx_w)
        crx_l.setContentsMargins(24, 20, 24, 20)
        crx_l.setSpacing(16)

        # Заголовок
        crx_title = QLabel("🧩 Расширения Chrome")
        crx_title.setStyleSheet(
            f"font-size:14px;font-weight:700;color:{t['text']};background:transparent;")
        crx_l.addWidget(crx_title)

        # Объяснение
        info_card = QFrame()
        info_card.setStyleSheet(
            f"QFrame{{background:{t['bg3']};border-radius:10px;"
            f"border:1px solid {t['border']};}}")
        ic_l = QVBoxLayout(info_card)
        ic_l.setContentsMargins(16, 12, 16, 12)
        ic_l.setSpacing(6)
        info_text = QLabel(
            "ℹ WNS работает на Chromium (QtWebEngine).\n\n"
            "Chrome Web Store требует официальный Chrome для установки .crx расширений.\n"
            "Однако ты можешь:\n\n"
            "  • Использовать Userscripts (вкладка слева) — полная замена расширений\n"
            "  • Открыть Chrome Web Store и скопировать нужный скрипт через Tampermonkey\n"
            "  • Найти .user.js версию любого расширения на greasyfork.org")
        info_text.setStyleSheet(
            f"color:{t['text_dim']};font-size:10pt;background:transparent;")
        info_text.setWordWrap(True)
        ic_l.addWidget(info_text)
        crx_l.addWidget(info_card)

        # Кнопки полезных ресурсов
        btn_grid = QHBoxLayout()
        for label, url in [
            ("🏪 Chrome Web Store",   "https://chrome.google.com/webstore/category/extensions"),
            ("📜 GreasyFork Scripts", "https://greasyfork.org/ru/scripts"),
            ("🔧 OpenUserJS",         "https://openuserjs.org"),
        ]:
            btn = QPushButton(label)
            btn.setFixedHeight(36)
            btn.setStyleSheet(
                f"QPushButton{{background:{t['bg3']};color:{t['accent']};"
                f"border:1px solid {t['accent']};border-radius:8px;padding:0 12px;}}"
                f"QPushButton:hover{{background:{t['accent']};color:white;}}")
            btn.clicked.connect(
                lambda checked=False, u=url: self._new_tab(u))
            btn_grid.addWidget(btn)
        crx_l.addLayout(btn_grid)
        crx_l.addStretch()
        inner_tabs.addTab(crx_w, "🧩 Chrome Extensions")

        dlg.exec()

    def _show_about(self):
        t = get_theme(S().theme)
        dlg = QDialog(self); dlg.setWindowTitle("О Winora NetScape 3.0"); dlg.resize(420, 320)
        dlg.setStyleSheet(f"background:{t['bg2']};color:{t['text']};")
        vl = QVBoxLayout(dlg); vl.setContentsMargins(24,20,24,20); vl.setSpacing(12)
        ico = QLabel("🌐"); ico.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ico.setStyleSheet("font-size:48px;background:transparent;")
        vl.addWidget(ico)
        ttl = QLabel("<b>Winora NetScape 3.0</b>")
        ttl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        ttl.setStyleSheet(f"font-size:16px;color:{t['accent']};background:transparent;")
        ttl.setTextFormat(Qt.TextFormat.RichText)
        vl.addWidget(ttl)
        info = QLabel(
            "Встроенный браузер экосистемы Winora.\n"
            "Часть GoidaPhone v1.8.0  ·  Winora Company\n\n"
            "Движок: QtWebEngine (Chromium) + FFmpeg\n"
            "Новое в 3.0: sidebar, reader mode, dark reader,\n"
            "devtools, zoom, screenshot, мультипоиск, история по дням\n\n"
            "Разработчик: pixless  ·  Gentoo Linux")
        info.setAlignment(Qt.AlignmentFlag.AlignCenter)
        info.setStyleSheet(f"font-size:11px;color:{t['text_dim']};background:transparent;")
        vl.addWidget(info)
        close = QPushButton(_L("Закрыть", "Close", "閉じる")); close.setObjectName("accent_btn")
        close.clicked.connect(dlg.accept); vl.addWidget(close)
        dlg.exec()

    # ── Keyboard shortcuts ────────────────────────────────────────────────────
    def keyPressEvent(self, ev):
        k = ev.key(); m = ev.modifiers()
        C  = Qt.KeyboardModifier.ControlModifier
        CS = Qt.KeyboardModifier.ControlModifier | Qt.KeyboardModifier.ShiftModifier
        A  = Qt.KeyboardModifier.AltModifier

        if m == C:
            if k == Qt.Key.Key_T:       self._new_tab()
            elif k == Qt.Key.Key_W:     self._close_tab(self._tabs.currentIndex())
            elif k in (Qt.Key.Key_R, Qt.Key.Key_F5): self._reload_or_stop()
            elif k == Qt.Key.Key_L:
                self._urlbar.setFocus(); self._urlbar.selectAll()
            elif k == Qt.Key.Key_F:     self._show_findbar()
            elif k == Qt.Key.Key_D:     self._toggle_bookmark()
            elif k == Qt.Key.Key_H:     self._navigate(self.HOME_URL)
            elif k == Qt.Key.Key_B:     self._toggle_sidebar()
            elif k == Qt.Key.Key_J:
                self._toggle_sidebar()
                self._sidebar_switch("history")
            elif k == Qt.Key.Key_Equal: self._zoom_in()
            elif k == Qt.Key.Key_Minus: self._zoom_out()
            elif k == Qt.Key.Key_0:     self._zoom_reset()
            elif k == Qt.Key.Key_P:     self._print_page()
            elif k == Qt.Key.Key_S:     self._save_page()
            elif k == Qt.Key.Key_Tab:
                n = (self._tabs.currentIndex()+1) % self._tabs.count()
                self._tabs.setCurrentIndex(n)
        elif m == CS:
            if k == Qt.Key.Key_R:       self._toggle_reader_mode()
            elif k == Qt.Key.Key_D:     self._toggle_dark_reader()
            elif k == Qt.Key.Key_Tab:
                n = (self._tabs.currentIndex()-1) % self._tabs.count()
                self._tabs.setCurrentIndex(n)
        elif m == A:
            if k == Qt.Key.Key_Left:    self._go_back()
            elif k == Qt.Key.Key_Right: self._go_forward()
        elif k == Qt.Key.Key_F5:        self._reload_or_stop()
        elif k == Qt.Key.Key_F12:       self._open_devtools()
        elif k == Qt.Key.Key_Escape:
            if self._omni_popup.isVisible():
                self._omni_popup.hide()
            elif self._find_visible:
                self._hide_findbar()
            else:
                w = self._tabs.currentWidget()
                if w and hasattr(w,'stop'): w.stop()
        else:
            super().keyPressEvent(ev)

    # ── Geometry persistence ──────────────────────────────────────────────────
    def _load_geometry(self):
        pass  # geometry managed by parent QTabWidget

    def closeEvent(self, ev):
        try:
            g = self.geometry()
            S().set("wns_geometry",f"{g.x()},{g.y()},{g.width()},{g.height()}")
        except Exception: pass
        self._save_history()
        super().closeEvent(ev)



# ═══════════════════════════════════════════════════════════════════════════
#  CALL UI  —  Telegram-style
#  OutgoingCallWindow  — показывается звонящему пока ждёт ответа
#  IncomingCallDialog  — показывается вызываемому (принять / отклонить)
#  ActiveCallWindow    — активный звонок 1-на-1 (после принятия)
#  GroupCallWindow     — групповой звонок в стиле Телемост
# ═══════════════════════════════════════════════════════════════════════════

# ── shared helpers ──────────────────────────────────────────────────────────
def _call_dark_window(title: str = "", w: int = 320, h: int = 460):
    """Create a dark frameless always-on-top window for call UI."""
    win = QWidget(None,
        Qt.WindowType.Window |
        Qt.WindowType.FramelessWindowHint |
        Qt.WindowType.WindowStaysOnTopHint)
    win.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
    win.setFixedSize(w, h)
    if title:
        win.setWindowTitle(title)
    return win


def _call_avatar_pixmap(avatar_b64: str, name: str, size: int) -> 'QPixmap':
    if avatar_b64:
        try:
            return make_circle_pixmap(base64_to_pixmap(avatar_b64), size)
        except Exception:
            pass
    return default_avatar(name, size)


def _call_round_btn(icon: str, bg: str, size: int = 64, icon_size: int = 28,
                    hover: str = "") -> QPushButton:
    b = QPushButton(icon)
    b.setFixedSize(size, size)
    b.setCursor(Qt.CursorShape.PointingHandCursor)
    hover_css = f"QPushButton:hover {{ background: {hover}; }}" if hover else ""
    b.setStyleSheet(f"""
        QPushButton {{
            background: {bg};
            border-radius: {size//2}px;
            font-size: {icon_size}px;
            border: none;
        }}
        {hover_css}
    """)
    return b


# ── OutgoingCallWindow ───────────────────────────────────────────────────────

# ═══════════════════════════════════════════════════════════════════════════
#  OUTGOING CALL WINDOW  (Ждём ответа — пульсирующий аватар)
# ═══════════════════════════════════════════════════════════════════════════
class OutgoingCallWindow(QWidget):
    """
    Анимированное окно «Звоним…» пока собеседник не ответил.
    Пульсирующие кольца вокруг аватара, счётчик секунд, кнопка отмены.
    """
    sig_cancelled = pyqtSignal()

    def __init__(self, peer_name: str, peer_ip: str, avatar_b64: str = "",
                 voice_mgr=None, parent=None):
        super().__init__(parent,
            Qt.WindowType.Window |
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self._peer_name  = peer_name
        self._peer_ip    = peer_ip
        self._avatar_b64 = avatar_b64
        self._voice_mgr  = voice_mgr
        self._dot_n      = 0
        self._elapsed    = 0
        self._drag_pos   = None
        self._pulse_r    = 0.0
        self._pulse_dir  = 1
        self.setFixedSize(340, 520)
        self._build()

        # Dots + elapsed timer
        self._dot_timer = QTimer(self)
        self._dot_timer.timeout.connect(self._tick)
        self._dot_timer.start(500)

        # Pulse animation timer
        self._pulse_timer = QTimer(self)
        self._pulse_timer.timeout.connect(self._tick_pulse)
        self._pulse_timer.start(30)

        if S().notification_sounds:
            play_system_sound("call")

        # Center on screen
        screen = QApplication.primaryScreen().geometry()
        self.move((screen.width()-340)//2, (screen.height()-520)//2)
        self.show()
        self.raise_()

    def _build(self):
        t = get_theme(S().theme)
        card = QWidget(self)
        card.setGeometry(0, 0, 340, 520)
        card.setObjectName("ocw_card")
        card.setStyleSheet(f"""
            QWidget#ocw_card {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['bg3']}, stop:1 {t['bg']});
                border-radius: 24px;
                border: 1px solid {t['border']};
            }}
        """)

        lay = QVBoxLayout(card)
        lay.setContentsMargins(30, 50, 30, 40)
        lay.setSpacing(0)

        # Avatar canvas (we paint pulse rings in paintEvent)
        self._avatar_canvas = QLabel()
        self._avatar_canvas.setFixedSize(140, 140)
        self._avatar_canvas.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._avatar_canvas.setStyleSheet("background:transparent;")
        if self._avatar_b64:
            try:
                pm = base64_to_pixmap(self._avatar_b64)
                self._avatar_canvas.setPixmap(make_circle_pixmap(pm, 120))
            except Exception:
                self._avatar_canvas.setPixmap(default_avatar(self._peer_name, 120))
        else:
            self._avatar_canvas.setPixmap(default_avatar(self._peer_name, 120))

        av_wrap = QWidget(); av_wrap.setStyleSheet("background:transparent;")
        av_lay  = QHBoxLayout(av_wrap); av_lay.setContentsMargins(0,0,0,0)
        av_lay.addStretch(); av_lay.addWidget(self._avatar_canvas); av_lay.addStretch()
        lay.addWidget(av_wrap)
        lay.addSpacing(24)

        # Name
        name_lbl = QLabel(self._peer_name)
        name_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        name_lbl.setStyleSheet(
            f"color:{t['text']};font-size:22px;font-weight:700;"
            "background:transparent;")
        lay.addWidget(name_lbl)
        lay.addSpacing(10)

        # Status: "Звоним…" / "Calling…"
        self._status_lbl = QLabel(_L("Звоним", "Calling", "発信中"))
        self._status_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._status_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:14px;background:transparent;")
        lay.addWidget(self._status_lbl)
        lay.addSpacing(6)

        # Elapsed
        self._time_lbl = QLabel("0:00")
        self._time_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._time_lbl.setStyleSheet(
            f"color:{t['accent']};font-size:12px;"
            "font-family:monospace;background:transparent;")
        lay.addWidget(self._time_lbl)
        lay.addStretch()

        # Cancel button
        self._cancel_btn = QPushButton("📵")
        self._cancel_btn.setFixedSize(72, 72)
        self._cancel_btn.setStyleSheet("""
            QPushButton {
                background: #E53935;
                border-radius: 36px;
                font-size: 28px;
                border: none;
            }
            QPushButton:hover  { background: #EF5350; }
            QPushButton:pressed{ background: #C62828; }
        """)
        self._cancel_btn.clicked.connect(self._on_cancel)

        btn_wrap = QWidget(); btn_wrap.setStyleSheet("background:transparent;")
        btn_lay  = QHBoxLayout(btn_wrap); btn_lay.setContentsMargins(0,0,0,0)
        cancel_col = QVBoxLayout()
        cancel_col.addWidget(self._cancel_btn, alignment=Qt.AlignmentFlag.AlignCenter)
        cancel_lbl = QLabel(_L("Отмена","Cancel","キャンセル"))
        cancel_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        cancel_lbl.setStyleSheet(f"color:{get_theme(S().theme)['text_dim']};font-size:10px;background:transparent;")
        cancel_col.addWidget(cancel_lbl)
        btn_lay.addStretch()
        btn_lay.addLayout(cancel_col)
        btn_lay.addStretch()
        lay.addWidget(btn_wrap)

        # Drag support
        card.mousePressEvent   = self._mouse_press
        card.mouseMoveEvent    = self._mouse_move
        card.mouseReleaseEvent = lambda e: setattr(self, '_drag_pos', None)

    def _tick(self):
        self._dot_n = (self._dot_n + 1) % 4
        dots = "." * self._dot_n
        self._elapsed += 1
        m, s = divmod(self._elapsed // 2, 60)
        self._time_lbl.setText(f"{m}:{s:02d}")
        base = _L("Звоним", "Calling", "発信中")
        self._status_lbl.setText(base + dots)

    def _tick_pulse(self):
        self._pulse_r += self._pulse_dir * 0.8
        if self._pulse_r >= 30: self._pulse_dir = -1
        if self._pulse_r <= 0:  self._pulse_dir = 1
        self._avatar_canvas.update()
        self.update()

    def paintEvent(self, event):
        # Draw pulse rings around avatar
        from PyQt6.QtGui import QPainter, QColor, QPen
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        t = get_theme(S().theme)
        cx = self.width() // 2
        cy = 50 + 70  # top margin + half avatar
        r_base = 70
        for i, scale in enumerate([1.0, 1.4, 1.8]):
            r = int(r_base + scale * self._pulse_r * 0.6)
            alpha = max(0, 80 - i * 25 - int(self._pulse_r * 1.5))
            c = QColor(t.get('accent', '#7C4DFF'))
            c.setAlpha(alpha)
            pen = QPen(c, 2)
            p.setPen(pen)
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawEllipse(cx - r, cy - r, r*2, r*2)
        p.end()

    def _on_cancel(self):
        self._dot_timer.stop()
        self._pulse_timer.stop()
        self.sig_cancelled.emit()
        self.close()

    def _mouse_press(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.frameGeometry().topLeft()

    def _mouse_move(self, e):
        if self._drag_pos and e.buttons() == Qt.MouseButton.LeftButton:
            self.move(e.globalPosition().toPoint() - self._drag_pos)

    def call_accepted(self):
        """Called when peer answers."""
        self._dot_timer.stop()
        self._pulse_timer.stop()
        self._status_lbl.setText(_L("Соединяем…", "Connecting…", "接続中…"))


# ═══════════════════════════════════════════════════════════════════════════
#  INCOMING CALL DIALOG  (Красивый экран входящего звонка)
# ═══════════════════════════════════════════════════════════════════════════
class IncomingCallDialog(QWidget):
    """
    Входящий звонок — slide-in снизу, анимированный.
    Кнопки Принять (зелёная) и Отклонить (красная).
    """
    sig_accepted = pyqtSignal()
    sig_rejected = pyqtSignal()

    def __init__(self, caller_name: str, caller_ip: str,
                 avatar_b64: str = "", parent=None):
        super().__init__(parent,
            Qt.WindowType.Window |
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self._caller_name = caller_name
        self._caller_ip   = caller_ip
        self._avatar_b64  = avatar_b64
        self._drag_pos    = None
        self.setFixedSize(340, 200)

        screen = QApplication.primaryScreen().geometry()
        # Start off-screen bottom, slide up
        self._end_y   = screen.height() - 220
        self._start_y = screen.height() + 10
        self.move((screen.width()-340)//2, self._start_y)
        self.show()
        self.raise_()

        self._build()
        self._slide_in()

        # Ring sound loop
        if S().notification_sounds:
            play_system_sound("incoming_call")

    def _build(self):
        t = get_theme(S().theme)
        card = QWidget(self)
        card.setGeometry(0, 0, 340, 200)
        card.setObjectName("icd_card")
        card.setStyleSheet(f"""
            QWidget#icd_card {{
                background: {t['bg2']};
                border-radius: 20px;
                border: 1px solid {t['border']};
            }}
        """)

        lay = QHBoxLayout(card)
        lay.setContentsMargins(18, 18, 18, 18)
        lay.setSpacing(16)

        # Avatar
        av_lbl = QLabel()
        av_lbl.setFixedSize(64, 64)
        av_lbl.setStyleSheet("background:transparent;")
        if self._avatar_b64:
            try:
                pm = base64_to_pixmap(self._avatar_b64)
                av_lbl.setPixmap(make_circle_pixmap(pm, 64))
            except Exception:
                av_lbl.setPixmap(default_avatar(self._caller_name, 64))
        else:
            av_lbl.setPixmap(default_avatar(self._caller_name, 64))
        lay.addWidget(av_lbl, alignment=Qt.AlignmentFlag.AlignVCenter)

        # Info
        info = QVBoxLayout()
        info.setSpacing(4)
        title = QLabel(_L("📞 Входящий звонок", "📞 Incoming call", "📞 着信"))
        title.setStyleSheet(
            f"color:{t['text_dim']};font-size:10px;font-weight:600;background:transparent;")
        name_lbl = QLabel(self._caller_name)
        name_lbl.setStyleSheet(
            f"color:{t['text']};font-size:16px;font-weight:700;background:transparent;")
        ip_lbl = QLabel(self._caller_ip)
        ip_lbl.setStyleSheet(
            f"color:{t['text_dim']};font-size:10px;background:transparent;")
        info.addWidget(title)
        info.addWidget(name_lbl)
        info.addWidget(ip_lbl)
        info.addStretch()

        # Buttons
        btns = QVBoxLayout()
        btns.setSpacing(8)

        accept = QPushButton("✅")
        accept.setFixedSize(52, 52)
        accept.setToolTip(_L("Принять", "Accept", "受話"))
        accept.setStyleSheet("""
            QPushButton{background:#2E7D32;border-radius:26px;font-size:22px;border:none;}
            QPushButton:hover{background:#388E3C;}
            QPushButton:pressed{background:#1B5E20;}
        """)
        accept.clicked.connect(self._on_accept)

        reject = QPushButton("📵")
        reject.setFixedSize(52, 52)
        reject.setToolTip(_L("Отклонить", "Decline", "拒否"))
        reject.setStyleSheet("""
            QPushButton{background:#C62828;border-radius:26px;font-size:22px;border:none;}
            QPushButton:hover{background:#E53935;}
            QPushButton:pressed{background:#B71C1C;}
        """)
        reject.clicked.connect(self._on_reject)

        btns.addWidget(accept)
        btns.addWidget(reject)

        lay.addLayout(info, stretch=1)
        lay.addLayout(btns)

        card.mousePressEvent   = self._mouse_press
        card.mouseMoveEvent    = self._mouse_move
        card.mouseReleaseEvent = lambda e: setattr(self, '_drag_pos', None)

    def _slide_in(self):
        anim = QPropertyAnimation(self, b"pos", self)
        anim.setDuration(350)
        anim.setStartValue(QPoint(self.x(), self._start_y))
        anim.setEndValue(QPoint(self.x(), self._end_y))
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)

    def _slide_out(self, then):
        anim = QPropertyAnimation(self, b"pos", self)
        anim.setDuration(250)
        anim.setStartValue(QPoint(self.x(), self.y()))
        anim.setEndValue(QPoint(self.x(), self._start_y))
        anim.setEasingCurve(QEasingCurve.Type.InCubic)
        anim.finished.connect(then)
        anim.finished.connect(self.close)
        anim.start(QAbstractAnimation.DeletionPolicy.DeleteWhenStopped)

    def _on_accept(self):
        self._slide_out(self.sig_accepted.emit)

    def _on_reject(self):
        self._slide_out(self.sig_rejected.emit)

    def _mouse_press(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.frameGeometry().topLeft()

    def _mouse_move(self, e):
        if self._drag_pos and e.buttons() == Qt.MouseButton.LeftButton:
            self.move(e.globalPosition().toPoint() - self._drag_pos)

    def call_rejected(self):
        self._slide_out(lambda: None)




# ═══════════════════════════════════════════════════════════════════════════
#  ACTIVE CALL WINDOW  (показывается когда звонок принят)
# ═══════════════════════════════════════════════════════════════════════════
class ActiveCallWindow(QWidget):
    """
    Окно активного звонка — Telegram-style.
    Показывается после принятия. Кнопки: мут, динамик, экран, камера, завершить.
    """
    sig_hangup = pyqtSignal()

    def __init__(self, peer_name: str, peer_ip: str,
                 avatar_b64: str = "", voice_mgr=None, parent=None):
        super().__init__(parent,
            Qt.WindowType.Window |
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self._peer_name  = peer_name
        self._peer_ip    = peer_ip
        self._avatar_b64 = avatar_b64
        self._voice_mgr  = voice_mgr
        self._muted      = False
        self._elapsed    = 0
        self._drag_pos   = None
        self.setFixedSize(340, 560)

        self._build()

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._tick)
        self._timer.start(1000)

        screen = QApplication.primaryScreen().geometry()
        self.move((screen.width()-340)//2, (screen.height()-560)//2)
        self.show()
        self.raise_()

    def _build(self):
        t = get_theme(S().theme)
        card = QWidget(self)
        card.setGeometry(0, 0, 340, 560)
        card.setObjectName("acw_card")
        card.setStyleSheet(f"""
            QWidget#acw_card {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['bg3']}, stop:1 {t['bg']});
                border-radius: 24px;
                border: 1px solid {t['border']};
            }}
        """)

        lay = QVBoxLayout(card)
        lay.setContentsMargins(28, 44, 28, 36)
        lay.setSpacing(0)

        # Avatar
        av_lbl = QLabel()
        av_lbl.setFixedSize(130, 130)
        av_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        av_lbl.setStyleSheet("background:transparent;")
        if self._avatar_b64:
            try:
                pm = base64_to_pixmap(self._avatar_b64)
                av_lbl.setPixmap(make_circle_pixmap(pm, 120))
            except Exception:
                av_lbl.setPixmap(default_avatar(self._peer_name, 120))
        else:
            av_lbl.setPixmap(default_avatar(self._peer_name, 120))

        av_wrap = QWidget(); av_wrap.setStyleSheet("background:transparent;")
        avl = QHBoxLayout(av_wrap); avl.setContentsMargins(0,0,0,0)
        avl.addStretch(); avl.addWidget(av_lbl); avl.addStretch()
        lay.addWidget(av_wrap)
        lay.addSpacing(20)

        # Name
        name_lbl = QLabel(self._peer_name)
        name_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        name_lbl.setStyleSheet(
            f"color:{t['text']};font-size:22px;font-weight:700;background:transparent;")
        lay.addWidget(name_lbl)
        lay.addSpacing(8)

        # Timer
        self._time_lbl = QLabel("0:00")
        self._time_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._time_lbl.setStyleSheet(
            f"color:{t['accent']};font-size:14px;font-family:monospace;background:transparent;")
        lay.addWidget(self._time_lbl)
        lay.addStretch()

        # Control buttons
        def _round_btn(icon, size=56, bg="#2A2A46", hover="#3A3A60"):
            b = QPushButton(icon)
            b.setFixedSize(size, size)
            b.setStyleSheet(f"""
                QPushButton{{background:{bg};border-radius:{size//2}px;
                             font-size:{size//2-4}px;border:none;color:white;}}
                QPushButton:hover{{background:{hover};}}
                QPushButton:pressed{{background:#1A1A36;}}
            """)
            return b

        # Row 1: mute, speaker, screen
        row1 = QHBoxLayout(); row1.setSpacing(20)
        row1.addStretch()

        self._mute_btn = _round_btn("🎤")
        self._mute_btn.setToolTip(_L("Микрофон","Microphone","マイク"))
        self._mute_btn.clicked.connect(self._toggle_mute)
        row1.addWidget(self._mute_btn)

        spk_btn = _round_btn("🔊")
        spk_btn.setToolTip(_L("Динамик","Speaker","スピーカー"))
        row1.addWidget(spk_btn)

        screen_btn = _round_btn("🖥")
        screen_btn.setToolTip(_L("Экран","Screen","画面"))
        row1.addWidget(screen_btn)

        row1.addStretch()
        row1_labels = QHBoxLayout(); row1_labels.setSpacing(0)
        row1_labels.addStretch()
        for lbl_text in [_L("Мут","Mute","ミュート"),
                         _L("Динамик","Speaker","スピーカー"),
                         _L("Экран","Screen","画面")]:
            l = QLabel(lbl_text)
            l.setAlignment(Qt.AlignmentFlag.AlignCenter)
            l.setFixedWidth(76)
            l.setStyleSheet(f"color:{t['text_dim']};font-size:10px;background:transparent;")
            row1_labels.addWidget(l)
        row1_labels.addStretch()

        r1w = QWidget(); r1w.setStyleSheet("background:transparent;")
        r1l = QVBoxLayout(r1w); r1l.setContentsMargins(0,0,0,0); r1l.setSpacing(4)
        r1l.addLayout(row1); r1l.addLayout(row1_labels)
        lay.addWidget(r1w)
        lay.addSpacing(24)

        # End call button
        end_btn = QPushButton("📵")
        end_btn.setFixedSize(72, 72)
        end_btn.setStyleSheet("""
            QPushButton{background:#E53935;border-radius:36px;font-size:28px;border:none;}
            QPushButton:hover{background:#EF5350;}
            QPushButton:pressed{background:#B71C1C;}
        """)
        end_btn.clicked.connect(self._on_hangup)

        end_col = QVBoxLayout()
        end_col.addWidget(end_btn, alignment=Qt.AlignmentFlag.AlignCenter)
        end_lbl = QLabel(_L("Завершить","End Call","通話終了"))
        end_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        end_lbl.setStyleSheet(f"color:{t['text_dim']};font-size:10px;background:transparent;")
        end_col.addWidget(end_lbl)

        btn_wrap = QWidget(); btn_wrap.setStyleSheet("background:transparent;")
        bwl = QHBoxLayout(btn_wrap); bwl.setContentsMargins(0,0,0,0)
        bwl.addStretch(); bwl.addLayout(end_col); bwl.addStretch()
        lay.addWidget(btn_wrap)

        card.mousePressEvent   = self._mp
        card.mouseMoveEvent    = self._mm
        card.mouseReleaseEvent = lambda e: setattr(self,'_drag_pos',None)

    def _tick(self):
        self._elapsed += 1
        m, s = divmod(self._elapsed, 60)
        h, m = divmod(m, 60)
        self._time_lbl.setText(
            f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}")

    def _toggle_mute(self):
        self._muted = not self._muted
        if self._voice_mgr:
            try: self._voice_mgr.toggle_mute()
            except Exception: pass
        self._mute_btn.setText("🔇" if self._muted else "🎤")
        self._mute_btn.setStyleSheet(
            self._mute_btn.styleSheet().replace(
                "#2A2A46" if self._muted else "#444444",
                "#444444" if self._muted else "#2A2A46"))

    def _on_hangup(self):
        self._timer.stop()
        self.sig_hangup.emit()
        self.close()

    def _mp(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.frameGeometry().topLeft()

    def _mm(self, e):
        if self._drag_pos and e.buttons() == Qt.MouseButton.LeftButton:
            self.move(e.globalPosition().toPoint() - self._drag_pos)



# Alias for backwards compatibility
FloatingCallWindow = ActiveCallWindow

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


# Keep old name as alias for any remaining references
FloatingCallWindow = ActiveCallWindow


class StatusWidget(QLabel):
    def __init__(self, text: str, color: str = "#A0A0A0"):
        super().__init__(text)
        t = get_theme(S().theme)
        self.setStyleSheet(f"""
            QLabel {{
                color: {color};
                background: {t['bg2']};
                border: 1px solid {t['border']};
                border-radius: 3px;
                padding: 1px 7px;
                font-size: 10px;
            }}
        """)
