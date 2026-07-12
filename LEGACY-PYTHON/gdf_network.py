#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# GoidaPhone NT Server 1.8 — Network & Audio
from gdf_imports import *
from gdf_core import _L, TR, S, get_theme, build_stylesheet, THEMES, AppSettings

class NetworkManager(QObject):
    # Signals
    sig_user_online   = pyqtSignal(dict)          # peer_info dict
    sig_user_offline  = pyqtSignal(str)           # ip
    sig_message       = pyqtSignal(dict)          # message dict
    sig_call_request  = pyqtSignal(str, str)      # username, ip
    sig_call_accepted = pyqtSignal(str, str)      # username, ip
    sig_call_rejected = pyqtSignal(str)           # ip
    sig_call_ended    = pyqtSignal(str)           # ip
    sig_voice_data    = pyqtSignal(bytes)         # raw audio (legacy)
    sig_voice_data_from = pyqtSignal(str, bytes)  # ip, raw audio
    sig_file_meta     = pyqtSignal(dict)          # file transfer meta
    sig_file_chunk    = pyqtSignal(str, bytes)    # transfer_id, chunk
    sig_group_invite  = pyqtSignal(str, str, str) # group_id, name, from_ip
    sig_error         = pyqtSignal(str)
    sig_typing        = pyqtSignal(str, str)      # username, chat_id
    sig_send_file_req = pyqtSignal(str, bytes, str, str)  # to_ip_or_empty, data_b64, fname, tid

    # Voice TCP
    sig_voice_connected   = pyqtSignal(str)   # ip
    sig_voice_disconnected = pyqtSignal(str)  # ip

    def __init__(self):
        super().__init__()
        self.host_ip = get_local_ip()
        self._udp: QUdpSocket | None = None
        self._tcp_srv: QTcpServer | None = None
        self._voice_cons: dict[str, QTcpSocket] = {}   # ip → socket (voice)
        self._file_cons:  dict[str, QTcpSocket] = {}   # ip → socket (file)
        self.peers: dict[str, dict] = {}               # ip → peer_info
        self.running = False
        self._bcast_timer = QTimer()
        self._bcast_timer.timeout.connect(self._broadcast)
        # Refresh local IPs every 30s (VPN adapters can connect/disconnect)
        self._ip_refresh_timer = QTimer()
        self._ip_refresh_timer.timeout.connect(self._refresh_local_ips)
        self._ip_refresh_timer.start(30_000)
        self._voice_tcp_port = TCP_PORT_DEFAULT
        self._voice_mgr = None  # будет установлен извне
        self._relay_sock = None  # постоянный TCP до VDS
        self._relay_recv_thread = None
        self._relay_buf = b""

    # ── startup / shutdown ──────────────────────────────────────────────
    def _refresh_local_ips(self):
        """Refresh IP cache. Called from main thread via QTimer — Qt API safe."""
        self._local_ips = get_all_local_ips() | {self.host_ip}
        new_primary = get_local_ip()
        if new_primary != self.host_ip:
            self.host_ip = new_primary

    def start(self) -> bool:
        udp_port = S().udp_port
        tcp_port = S().tcp_port
        self._voice_tcp_port = tcp_port

        # ── UDP socket ────────────────────────────────────────────────
        self._udp = QUdpSocket(self)
        # Пробуем с ReusePort, fallback без него (некоторые ОС не поддерживают)
        bound = self._udp.bind(
            QHostAddress.SpecialAddress.Any, udp_port,
            QUdpSocket.BindFlag.ShareAddress | QUdpSocket.BindFlag.ReuseAddressHint)
        if not bound:
            # Второй шанс — другой порт
            bound = self._udp.bind(
                QHostAddress.SpecialAddress.Any, udp_port + 1,
                QUdpSocket.BindFlag.ShareAddress | QUdpSocket.BindFlag.ReuseAddressHint)
        if not bound:
            self.sig_error.emit(f"UDP bind failed on port {udp_port}")
            return False
        self._udp.readyRead.connect(self._on_udp)

        # Вступаем в multicast группу для mDNS-like discovery
        try:
            self._udp.joinMulticastGroup(QHostAddress("224.0.0.251"))
        except Exception:
            pass

        # ── TCP server ────────────────────────────────────────────────
        self._tcp_srv = QTcpServer(self)
        if not self._tcp_srv.listen(QHostAddress.SpecialAddress.Any, tcp_port):
            # Попробуем любой свободный порт
            if not self._tcp_srv.listen(QHostAddress.SpecialAddress.Any, 0):
                self.sig_error.emit(f"TCP listen failed on port {tcp_port}")
                return False
        self._tcp_srv.newConnection.connect(self._on_new_connection)

        self.running = True
        self._internet_mode = S().connection_mode == "internet"
        if self._internet_mode:
            print(f"[net] Internet mode: relay to goidaphone.ru")
        self._local_ips = get_all_local_ips() | {self.host_ip}
        self._last_scan  = 0.0
        self._last_arp   = 0.0

        # Broadcast каждые 2с — быстрое обнаружение
        self._bcast_timer.start(2000)
        if self._internet_mode:
            self._start_internet_tunnel()
        # Первый broadcast немедленно
        self._broadcast()

        # ARP-таблица через 3с после старта
        QTimer.singleShot(3000, self._scan_arp_table)
        return True

    def _scan_arp_table(self):
        """Читаем ARP кэш ОС и шлём presence всем соседям из таблицы."""
        if not self.running:
            return
        import platform as _plt
        ips = set()
        try:
            if _plt.system() == "Linux":
                arp_file = '/proc/net/arp'
                if __import__('os').path.exists(arp_file):
                    with open(arp_file) as f:
                        for line in f.readlines()[1:]:
                            parts = line.split()
                            if len(parts) >= 4 and parts[2] != '0x0':
                                ips.add(parts[0])
            else:
                # Windows/macOS: arp -a
                result = __import__('subprocess').run(
                    ['arp', '-a'], capture_output=True, text=True, timeout=3)
                import re as _re
                ips.update(_re.findall(r'\d+\.\d+\.\d+\.\d+', result.stdout))
        except Exception:
            pass

        if not ips:
            return

        payload = self._presence_payload()
        data = __import__('json').dumps(payload, ensure_ascii=False).encode('utf-8')
        port = S().udp_port
        my_ip = self.host_ip

        sent = 0
        for ip in ips:
            if ip != my_ip and not ip.startswith('169.254'):
                self._udp.writeDatagram(data, QHostAddress(ip), port)
                sent += 1

        if sent:
            print(f"[net] ARP scan: sent presence to {sent} hosts")

        # Повторяем каждые 60с
        QTimer.singleShot(60000, self._scan_arp_table)

    def stop(self):
        self.running = False
        self._bcast_timer.stop()
        for sock in list(self._voice_cons.values()):
            try: sock.disconnectFromHost()
            except Exception: pass
        self._voice_cons.clear()
        if self._internet_mode and self._relay_sock:
            try: self._relay_sock.close()
            except: pass
            self._relay_sock = None
        if self._udp:
            self._udp.close(); self._udp = None
        if self._tcp_srv:
            self._tcp_srv.close(); self._tcp_srv = None

    # ── internal broadcast ──────────────────────────────────────────────
    def _broadcast(self):
        # В интернет-режиме не шлём broadcast в LAN
        if S().connection_mode == "internet":
            return
        if not self.running or not self._udp:
            return
        payload = self._presence_payload()
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        port = S().udp_port

        # ── 1. Global broadcast (LAN) ────────────────────────────────
        self._udp.writeDatagram(data, QHostAddress.SpecialAddress.Broadcast, port)

        # ── 2. Per-interface subnet broadcasts ───────────────────────
        sent_bcasts = set()
        try:
            for iface in QNetworkInterface.allInterfaces():
                flags = iface.flags()
                if not (flags & QNetworkInterface.InterfaceFlag.IsUp):
                    continue
                if flags & QNetworkInterface.InterfaceFlag.IsLoopBack:
                    continue
                for entry in iface.addressEntries():
                    addr = entry.ip()
                    if addr.protocol() != QHostAddress.NetworkLayerProtocol.IPv4Protocol:
                        continue
                    bcast = entry.broadcast()
                    bs = bcast.toString()
                    if bs and bs not in ("", "0.0.0.0") and bs not in sent_bcasts:
                        self._udp.writeDatagram(data, bcast, port)
                        sent_bcasts.add(bs)
                    # Также шлём на .255 адрес если broadcast пустой
                    my_ip = addr.toString()
                    if my_ip and my_ip != "127.0.0.1":
                        parts = my_ip.split(".")
                        if len(parts) == 4:
                            guess_bcast = f"{parts[0]}.{parts[1]}.{parts[2]}.255"
                            if guess_bcast not in sent_bcasts:
                                self._udp.writeDatagram(
                                    data, QHostAddress(guess_bcast), port)
                                sent_bcasts.add(guess_bcast)
        except Exception:
            pass

        # ── 3. mDNS multicast (224.0.0.251) для обнаружения в LAN ───
        try:
            self._udp.writeDatagram(data, QHostAddress("224.0.0.251"), port)
        except Exception:
            pass

        # ── 4. Unicast subnet scan (если broadcast не работает) ───────
        #    Сканируем всю /24 подсеть unicast раз в 30с
        now = time.time()
        if now - getattr(self, '_last_scan', 0) > 30:
            self._last_scan = now
            try:
                my_ip = get_local_ip()
                if my_ip and my_ip != "127.0.0.1":
                    parts = my_ip.split(".")
                    if len(parts) == 4:
                        prefix = f"{parts[0]}.{parts[1]}.{parts[2]}."
                        # Шлём unicast на все IP в подсети кроме своего
                        for last in range(1, 255):
                            target = prefix + str(last)
                            if target != my_ip:
                                self._udp.writeDatagram(
                                    data, QHostAddress(target), port)
            except Exception:
                pass

        # ── 5. Unicast to manually-added static peers ─────────────────
        try:
            raw = S().get("static_peers", "[]", t=str)
            for peer_ip in json.loads(raw):
                peer_ip = str(peer_ip).strip()
                if peer_ip:
                    self._udp.writeDatagram(data, QHostAddress(peer_ip), port)
        except Exception:
            pass

        # ── 6. Relay server unicast ───────────────────────────────────
        if S().relay_enabled and S().relay_server:
            try:
                parts = S().relay_server.strip().split(":")
                r_host = parts[0]
                r_port = int(parts[1]) if len(parts) > 1 else port
                self._udp.writeDatagram(data, QHostAddress(r_host), r_port)
            except Exception:
                pass

        # ── 7. Prune stale peers (>25s no presence) ──────────────────
        now2 = time.time()
        for ip in list(self.peers):
            if now2 - self.peers[ip].get("last_seen", 0) > 25:
                del self.peers[ip]
                self.sig_user_offline.emit(ip)


    def _presence_payload(self) -> dict:
        cfg = S()
        all_ips = list(self._local_ips or get_all_local_ips())
        payload = {
            "type":             MSG_PRESENCE,
            "username":         cfg.username,
            "ip":               self.host_ip,
            "all_ips":          all_ips,          # all interfaces incl. VPN
            "premium":          cfg.premium,
            "nickname_color":   cfg.nickname_color,
            "custom_emoji":     cfg.custom_emoji,
            "bio":              cfg.bio,
            "avatar_b64":       cfg.avatar_b64[:200] if cfg.avatar_b64 else "",
            "os":               get_os_name(),
            "version":          APP_VERSION,
            "protocol_version": PROTOCOL_VERSION,
            "ts":               time.time(),
            "nonce":            secrets.token_hex(8),   # replay guard
            "status":           S().user_status,           # presence status
            "loyalty_months":   S().get("loyalty_months", 0, t=int),
        }
        # Append ECDH handshake keys so session key is established on first contact
        payload.update(CRYPTO.get_handshake_payload())
        return payload

    # ── UDP receive ─────────────────────────────────────────────────────
    def _on_udp(self):
        while self._udp and self._udp.hasPendingDatagrams():
            dg = self._udp.receiveDatagram()
            host = dg.senderAddress().toString()
            # strip IPv4-mapped prefix
            if host.startswith("::ffff:"):
                host = host[7:]
            try:
                msg = json.loads(dg.data().data().decode("utf-8"))
                self._dispatch(host, msg)
            except Exception as e:
                print(f"UDP parse error from {host}: {e}")

    def _dispatch(self, host: str, msg: dict):
        # Rate limiting — drop excess packets silently
        if not CRYPTO.check_rate(host, max_per_sec=200):
            return
        # Never process our own echoed messages (except presence)
        # Check by IP first (more reliable), then username
        _msg_from_ip = msg.get('from_ip', '')
        _my_ips = self._local_ips or {self.host_ip}  # cached — no Qt calls in thread
        if msg.get('type') not in (MSG_PRESENCE, None):
            if _msg_from_ip and _msg_from_ip in _my_ips:
                return   # own message echoed back
            if host in _my_ips:
                return   # own broadcast echoed back
            # Fallback: username match (less reliable — someone could share username)
            if msg.get('username') == S().username and not _msg_from_ip:
                return
        # HMAC verification (if session key established)
        sig = msg.pop("_sig", "")
        if sig and CRYPTO.has_session(host):
            try:
                payload_bytes = json.dumps(
                    {k: v for k, v in msg.items() if k != "_sig"},
                    ensure_ascii=False, sort_keys=True).encode("utf-8")
                if not CRYPTO.verify_packet(host, payload_bytes, sig):
                    print(f"[security] HMAC failed from {host} — dropping")
                    return
            except Exception:
                pass  # don't crash on malformed
        t = msg.get("type")
        if t == MSG_PRESENCE:
            self._handle_presence(host, msg)
        elif t == MSG_CHAT:
            self.sig_message.emit(msg)
        elif t == MSG_PRIVATE:
            if msg.get("to") == self.host_ip:
                self.sig_message.emit(msg)
        elif t == MSG_GROUP:
            self.sig_message.emit(msg)
        elif t == MSG_CALL_REQ:
            caller = msg.get("username","?")
            # Если мы уже в звонке — отбиваем "занято"
            if hasattr(self, '_voice_mgr') and self._voice_mgr and self._voice_mgr.active:
                self.send_udp({"type": MSG_CALL_BUSY, "username": S().username,
                               "to": caller}, host)
            else:
                self.sig_call_request.emit(caller, host)
        elif t == MSG_CALL_ACCEPT:
            self.sig_call_accepted.emit(msg.get("username","?"), host)
        elif t == MSG_CALL_BUSY:
            print(f"[net] Call busy: {host}")
            self.sig_call_rejected.emit(host)  # показываем как отклонённый
        elif t == MSG_CALL_REJECT:
            self.sig_call_rejected.emit(host)
        elif t == MSG_CALL_END:
            self.sig_call_ended.emit(host)
        elif t == MSG_FILE_META:
            self.sig_file_meta.emit(msg)
        elif t == MSG_GROUP_INV:
            self.sig_group_invite.emit(msg.get("gid",""), msg.get("gname",""), host)
        elif t == MSG_TYPING:
            chat_id = msg.get("chat_id", "public")
            self.sig_typing.emit(msg.get("username", ""), chat_id)
        elif t == MSG_REACTION:
            # Route reactions through message signal for simplicity
            self.sig_message.emit(msg)
        elif t == MSG_EDIT:
            self.sig_message.emit(msg)
        elif t == MSG_DELETE:
            self.sig_message.emit(msg)
        elif t == MSG_READ:
            # Read receipt — route through message signal
            self.sig_message.emit(msg)

    def _handle_presence(self, host: str, msg: dict):
        # Accept packet IP or any self-reported IP
        ip = msg.get("ip", host)
        _my_ips = self._local_ips or {self.host_ip}  # cached
        if ip in _my_ips or host in _my_ips:
            return
        # Also index by the actual packet source (useful for NAT/VPN scenarios)
        if host != ip and host not in ("", "0.0.0.0"):
            msg.setdefault("source_ip", host)
        # Replay guard on presence packets
        nonce = msg.get("nonce", "")
        ts    = msg.get("ts", 0.0)
        if nonce and not CRYPTO.check_replay(ip, nonce, ts):
            return   # replayed presence packet
        # Establish / refresh ECDH session key if peer sends DH keys
        if "dh_pub" in msg and "id_pub" in msg:
            CRYPTO.process_handshake(ip, msg)
        is_new = ip not in self.peers
        msg["conn_type"] = detect_connection_type(ip)
        msg["last_seen"] = time.time()
        msg["e2e"] = CRYPTO.has_session(ip)   # flag for UI
        self.peers[ip] = msg
        if is_new:
            self.sig_user_online.emit(msg)

    # ── TCP (voice) ──────────────────────────────────────────────────────
    def _on_new_connection(self):
        while self._tcp_srv and self._tcp_srv.hasPendingConnections():
            sock = self._tcp_srv.nextPendingConnection()
            ip = sock.peerAddress().toString()
            if ip.startswith("::ffff:"):
                ip = ip[7:]
            self._voice_cons[ip] = sock
            sock.readyRead.connect(lambda s=sock, i=ip: self._on_voice_data(s, i))
            sock.disconnected.connect(lambda i=ip: self._on_voice_disc(i))
            self.sig_voice_connected.emit(ip)

    def _on_voice_data(self, sock: QTcpSocket, ip: str):
        while sock.bytesAvailable() > 0:
            data = bytes(sock.read(2048))
            if data:
                self.sig_voice_data.emit(data)          # legacy (single call)
                self.sig_voice_data_from.emit(ip, data) # mixer (group call)

    def _on_voice_disc(self, ip: str):
        self._voice_cons.pop(ip, None)
        self.sig_voice_disconnected.emit(ip)
        self.sig_call_ended.emit(ip)

    # ── public send methods ─────────────────────────────────────────────
    def _start_internet_tunnel(self):
        """Открыть TCP-туннель до VDS для всего трафика."""
        import struct
        try:
            self._relay_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._relay_sock.settimeout(30)
            self._relay_sock.connect(("goidaphone.ru", 9090))
            self._relay_sock.setblocking(True)
            self._relay_connected = True
            print("[net] Internet tunnel: goidaphone.ru:9090")
            self._relay_recv_thread = threading.Thread(target=self._tunnel_recv_loop, daemon=True)
            self._relay_recv_thread.start()
            self._broadcast()
        except Exception as e:
            print(f"[net] Tunnel connect failed: {e}")
            self._relay_sock = None
            self._relay_connected = False

    def _tunnel_recv_loop(self):
        """Читает фреймы из TCP-туннеля и эмулирует UDP-приём."""
        import struct
        while self._relay_sock and self._relay_connected:
            try:
                header = self._relay_sock.recv(4)
                if not header or len(header) < 4:
                    break
                length = struct.unpack("!I", header)[0]
                data = b''
                while len(data) < length:
                    chunk = self._relay_sock.recv(length - len(data))
                    if not chunk:
                        break
                    data += chunk
                if len(data) == length:
                    msg = json.loads(data.decode("utf-8"))
                    sender_ip = msg.get("from_ip", msg.get("ip", "relay"))
                    self._dispatch(sender_ip, msg)
            except Exception as e:
                if self._relay_connected:
                    print(f"[net] Tunnel recv error: {e}")
                break
        print("[net] Tunnel disconnected, reconnecting in 3s...")
        self._relay_connected = False
        try: self._relay_sock.close()
        except: pass
        self._relay_sock = None
        QTimer.singleShot(3000, self._start_internet_tunnel)

    def send_udp(self, payload: dict, target_ip: str | None = None):
        if not self._udp:
            return
        # Add replay-guard fields to non-presence packets
        msg_type = payload.get("type", "")
        if msg_type != MSG_PRESENCE:
            payload = dict(payload)
            payload["nonce"] = secrets.token_hex(8)
            payload["ts"]    = time.time()
            # HMAC sign if we have a session key with target
            if target_ip and CRYPTO.has_session(target_ip):
                try:
                    payload_bytes = json.dumps(
                        payload, ensure_ascii=False, sort_keys=True).encode("utf-8")
                    payload["_sig"] = CRYPTO.sign_packet(target_ip, payload_bytes)
                except Exception:
                    pass
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        if S().connection_mode == "internet":
            if self._relay_sock and self._relay_connected:
                try:
                    import struct as _st
                    raw = json.dumps(payload, ensure_ascii=False).encode("utf-8")
                    hdr = _st.pack("!I", len(raw))
                    self._relay_sock.sendall(hdr + raw)
                    t = payload.get("type","?")
                    if t != "presence":
                        print(f"[net] Sent via tunnel: {t} ({len(raw)}B)")
                except Exception as _e:
                    print(f"[net] Tunnel send error: {_e}")
        elif target_ip:
            self._udp.writeDatagram(data, QHostAddress(target_ip), S().udp_port)
        else:
            self._udp.writeDatagram(data, QHostAddress.SpecialAddress.Broadcast, S().udp_port)

    def send_chat(self, text: str):
        cfg = S()
        encrypted = False
        if cfg.encryption_enabled and cfg.encryption_passphrase:
            text = CRYPTO.encrypt(text, cfg.encryption_passphrase)
            encrypted = True
        self.send_udp({"type": MSG_CHAT, "username": cfg.username,
                       "text": text, "from_ip": self.host_ip,
                       "encrypted": encrypted})

    def send_private(self, text: str, to_ip: str):
        cfg = S()
        # E2E: prefer ECDH session key; fall back to passphrase; then plaintext
        if CRYPTO.has_session(to_ip) or (cfg.encryption_enabled and cfg.encryption_passphrase):
            text = CRYPTO.encrypt(text, cfg.encryption_passphrase, peer_ip=to_ip)
            encrypted = True
        else:
            encrypted = False
        payload = {"type": MSG_PRIVATE, "username": cfg.username,
                   "text": text, "to": to_ip, "from_ip": self.host_ip,
                   "encrypted": encrypted}
        # Primary: send to known IP
        self.send_udp(payload, to_ip)
        # Also send to any alternate IPs peer reported (VPN/LAN redundancy)
        peer_info = self.peers.get(to_ip, {})
        for alt_ip in peer_info.get("all_ips", []):
            if alt_ip != to_ip and alt_ip not in (self._local_ips or {self.host_ip}):
                self.send_udp(payload, alt_ip)

    def send_group_msg(self, gid: str, text: str, members: list[str]):
        cfg = S()
        if cfg.encryption_enabled and cfg.encryption_passphrase:
            text = CRYPTO.encrypt(text, cfg.encryption_passphrase)
        payload = {"type": MSG_GROUP, "username": cfg.username,
                   "gid": gid, "text": text, "ts": time.time(),
                   "encrypted": cfg.encryption_enabled}
        for ip in members:
            if ip != self.host_ip:
                self.send_udp(payload, ip)

    def send_typing(self, chat_id: str, target_ip: str | None = None):
        p = {"type": MSG_TYPING, "username": S().username, "chat_id": chat_id}
        self.send_udp(p, target_ip)

    def send_sticker(self, to_ip: str, b64: str, ts: float):
        """Send sticker to specific peer."""
        msg = {"type": MSG_STICKER, "username": S().username,
               "sticker_b64": b64, "ts": ts}
        self._send_tcp(msg, to_ip)

    def broadcast_sticker(self, b64: str, ts: float, gid: str = ""):
        """Broadcast sticker to public chat or group."""
        msg = {"type": MSG_STICKER, "username": S().username,
               "sticker_b64": b64, "ts": ts, "gid": gid}
        self._broadcast_udp(msg)

    def send_call_request(self, to_ip: str):
        self.send_udp({"type": MSG_CALL_REQ, "username": S().username,
                       "avatar_b64": S().avatar_b64[:200] if S().avatar_b64 else ""}, to_ip)

    def send_call_accept(self, to_ip: str):
        self.send_udp({"type": MSG_CALL_ACCEPT, "username": S().username,
                       "avatar_b64": S().avatar_b64[:200] if S().avatar_b64 else ""}, to_ip)
        self.connect_voice(to_ip)

    def send_call_reject(self, to_ip: str):
        self.send_udp({"type": MSG_CALL_REJECT, "username": S().username}, to_ip)

    def send_call_end(self, to_ip: str):
        self.send_udp({"type": MSG_CALL_END, "username": S().username}, to_ip)

    def send_reaction(self, to_ip: str | None, chat_id: str, ts: float,
                      emoji: str, added: bool):
        """Send emoji reaction to a message. to_ip=None → broadcast."""
        payload = {
            "type":    MSG_REACTION,
            "username": S().username,
            "chat_id": chat_id,
            "msg_ts":  ts,
            "emoji":   emoji,
            "added":   added,
        }
        self.send_udp(payload, to_ip)

    def send_message_edit(self, to_ip: str | None, chat_id: str,
                          ts: float, new_text: str):
        """Notify peer(s) that a message was edited."""
        payload = {
            "type":     MSG_EDIT,
            "username": S().username,
            "chat_id":  chat_id,
            "msg_ts":   ts,
            "new_text": new_text,
        }
        self.send_udp(payload, to_ip)

    def send_message_delete(self, to_ip: str | None, chat_id: str, ts: float):
        """Notify peer(s) that a message was deleted."""
        payload = {
            "type":    MSG_DELETE,
            "username": S().username,
            "chat_id": chat_id,
            "msg_ts":  ts,
        }
        self.send_udp(payload, to_ip)

    def send_read_receipt(self, to_ip: str, chat_id: str):
        """Send a read receipt to a peer."""
        payload = {
            "type":    MSG_READ,
            "username": S().username,
            "chat_id": chat_id,
        }
        self.send_udp(payload, to_ip)

    def connect_voice(self, ip: str) -> bool:
        if ip in self._voice_cons:
            return True
        sock = QTcpSocket(self)
        # Интернет-режим: цепляемся к VDS, сервер сам свяжет пиров
        if S().connection_mode == "internet":
            sock.connectToHost(QHostAddress("157.22.199.8"), 9091)
        else:
            peer_info = self.peers.get(ip, {})
            sock.connectToHost(QHostAddress(ip), self._voice_tcp_port)
        if sock.waitForConnected(3000):
            self._voice_cons[ip] = sock
            sock.readyRead.connect(lambda s=sock, i=ip: self._on_voice_data(s, i))
            sock.disconnected.connect(lambda i=ip: self._on_voice_disc(i))
            self.sig_voice_connected.emit(ip)
            return True
        sock.close()
        return False

    def send_voice(self, ip: str, data: bytes) -> bool:
        sock = self._voice_cons.get(ip)
        if sock and sock.state() == QTcpSocket.SocketState.ConnectedState:
            sock.write(data)
            return True
        return False

    def disconnect_voice(self, ip: str):
        sock = self._voice_cons.pop(ip, None)
        if sock:
            try: sock.disconnectFromHost()
            except Exception: pass

    def send_group_invite(self, gid: str, gname: str, to_ip: str):
        self.send_udp({"type": MSG_GROUP_INV, "gid": gid, "gname": gname,
                       "from": S().username}, to_ip)

    # File transfer over UDP — must be called from main thread
    def send_file(self, to_ip: str | None, filepath: str | None,
                  raw_bytes: bytes | None = None, filename: str = "file"):
        """Send file. If to_ip is None → broadcast (group).
           MUST be called from the Qt main thread (uses QUdpSocket)."""
        try:
            if raw_bytes is not None:
                data = raw_bytes
                fname = filename
                ext = Path(fname).suffix.lower()
            else:
                path = Path(filepath)
                if not path.exists():
                    return
                data = path.read_bytes()
                fname = path.name
                ext = path.suffix.lower()
            is_image = ext in {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp"}
            tid = secrets.token_hex(8)
            meta = {
                "type":     MSG_FILE_META,
                "tid":      tid,
                "filename": fname,
                "size":     len(data),
                "is_image": is_image,
                "from":     S().username,
                "from_ip":  self.host_ip,
                "to":       to_ip or "public",
                "ts":       time.time(),
            }
            self.send_udp(meta, to_ip)
            CHUNK = 60000
            total = len(data)
            # Send chunks with a QTimer so we don't block the event loop
            chunks = []
            for idx, offset in enumerate(range(0, total, CHUNK)):
                chunk = data[offset:offset+CHUNK]
                chunks.append({
                    "type":  MSG_FILE_DATA,
                    "tid":   tid,
                    "idx":   idx,
                    "total": (total + CHUNK - 1) // CHUNK,
                    "data":  base64.b64encode(chunk).decode(),
                })
            self._send_chunks_queued(chunks, to_ip, 0)
        except Exception as e:
            self.sig_error.emit(f"File send error: {e}")

    def _send_chunks_queued(self, chunks: list, to_ip, idx: int, batch: int = 3):
        """Send chunks in small batches to avoid blocking the event loop.
        Sends `batch` chunks per timer tick, yielding control between batches."""
        if idx >= len(chunks):
            return
        # Send a small batch per tick (faster throughput, still non-blocking)
        end = min(idx + batch, len(chunks))
        for i in range(idx, end):
            try:
                self.send_udp(chunks[i], to_ip)
            except Exception as e:
                print(f"[chunk send] {e}")
        # Adaptive delay: more chunks = slightly longer pause to keep UI smooth
        total = len(chunks)
        delay = 8 if total > 100 else (5 if total > 20 else 2)
        QTimer.singleShot(delay, lambda: self._send_chunks_queued(chunks, to_ip, end, batch))

# ═══════════════════════════════════════════════════════════════════════════
#  VOICE CALL MANAGER
# ═══════════════════════════════════════════════════════════════════════════
class VoiceCallManager(QObject):
    """
    Manages P2P voice calls.
    Group calls: each peer gets its own jitter-buffered slot in the mixer.
    VAD: mic audio is only transmitted when speech is detected.
    """
    call_started = pyqtSignal(str)    # ip
    call_ended   = pyqtSignal(str)    # ip

    def __init__(self, net: 'NetworkManager'):
        super().__init__()
        self.net   = net
        self.audio = AudioEngine()
        self.audio.audio_captured.connect(self._on_captured)
        self.active: set[str] = set()
        self._muted: bool = False   # ← must exist before set_mute is called
        # Route incoming audio by IP → mixer jitter buffer
        self.net.sig_voice_data_from.connect(self._on_peer_audio)
        # Speaking indicator signal (pass-through from AudioEngine)
        self.audio.sig_speaking.connect(self._on_speaking)

    def call(self, ip: str) -> bool:
        if ip in self.active:
            return True
        if not self.audio.running:
            if not self.audio.start_capture():
                return False
        self.active.add(ip)
        pb = self.audio.mixer.add_peer(ip)
        # Apply jitter target from settings
        jt = S().get("jitter_frames", 6, t=int)
        if pb: pb.TARGET = max(2, min(20, jt))
        # Apply VAD setting
        self.audio.vad_enabled = S().get("vad_enabled", WEBRTCVAD_AVAILABLE, t=bool)
        self.call_started.emit(ip)
        return True

    def hangup(self, ip: str):
        self.active.discard(ip)
        self.audio.mixer.remove_peer(ip)
        self.net.send_call_end(ip)
        self.net.disconnect_voice(ip)
        if not self.active:
            self.audio.stop_all()
        self.call_ended.emit(ip)

    def hangup_all(self):
        for ip in list(self.active):
            self.hangup(ip)

    def set_mute(self, muted: bool):
        """Set mute state directly."""
        self._muted = muted
        self.audio.muted = muted

    def toggle_mute(self) -> bool:
        self._muted = not self._muted
        self.audio.muted = self._muted
        return self._muted

    @property
    def is_muted(self) -> bool:
        return self._muted

    # Speaking detection — forwarded from AudioEngine VAD
    sig_local_speaking = None   # set dynamically below
    def _on_speaking(self, active: bool):
        if hasattr(self, '_speaking_cbs'):
            for cb in list(self._speaking_cbs):
                try: cb(active)
                except Exception: pass

    def subscribe_speaking(self, cb):
        if not hasattr(self, '_speaking_cbs'):
            self._speaking_cbs = []
        self._speaking_cbs.append(cb)

    def unsubscribe_speaking(self, cb):
        if hasattr(self, '_speaking_cbs'):
            try: self._speaking_cbs.remove(cb)
            except ValueError: pass

    def set_vad(self, enabled: bool):
        self.audio.vad_enabled = enabled

    def _on_captured(self, data: bytes):
        """Send mic audio to all active peers."""
        for ip in list(self.active):
            self.net.send_voice(ip, data)

    def _on_peer_audio(self, ip: str, data: bytes):
        """Incoming audio from a peer → push into their jitter buffer."""
        if ip in self.active:
            self.audio.push_peer_audio(ip, data)

    def cleanup(self):
        self.hangup_all()
        self.audio.cleanup()


# ═══════════════════════════════════════════════════════════════════════════
#  FILE TRANSFER HANDLER
# ═══════════════════════════════════════════════════════════════════════════
class FileTransferHandler(QObject):
    file_received = pyqtSignal(dict, bytes)   # meta, data

    def __init__(self):
        super().__init__()
        self._pending: dict[str, dict] = {}   # tid → {meta, chunks}

    def on_meta(self, meta: dict):
        tid = meta.get("tid","")
        if tid:
            self._pending[tid] = {"meta": meta, "chunks": {}, "total": None}

    def on_chunk(self, tid: str, raw: bytes):
        # raw is a JSON-encoded chunk message as bytes — already decoded
        pass  # handled below

    def on_chunk_msg(self, msg: dict):
        tid   = msg.get("tid","")
        idx   = msg.get("idx", 0)
        total = msg.get("total", 1)
        data  = msg.get("data","")
        if tid not in self._pending:
            self._pending[tid] = {"meta": {}, "chunks": {}, "total": total}
        rec = self._pending[tid]
        rec["total"] = total
        try:
            rec["chunks"][idx] = base64.b64decode(data)
        except Exception:
            return
        if len(rec["chunks"]) == total:
            assembled = b"".join(rec["chunks"][i] for i in range(total))
            meta      = rec["meta"]
            del self._pending[tid]
            self.file_received.emit(meta, assembled)

FT = FileTransferHandler()

# ═══════════════════════════════════════════════════════════════════════════
#  LAUNCHER SCREEN  (п.15 — выбор GUI/CMD режима)
# ═══════════════════════════════════════════════════════════════════════════
class LauncherScreen(QDialog):
    """
    Startup launcher — choose GUI or CMD mode.
    - Adapts colours to current theme
    - Keyboard navigation (arrows + Enter)
    - "Remember my choice" checkbox
    - Hover shows description
    """
    mode_selected = pyqtSignal(str)   # "gui" or "cmd"

    _MODES = [
        ("gui", "🖥  GUI mode",
         "Full graphical interface.\nChats, calls, files, settings.\nRecommended for regular use."),
        ("cmd", "⌨  CMD mode",
         "Console mode without GUI.\nFor servers, diagnostics, automation.\nCommands: /peers /ping /send /quit"),
        ("gcc", "🔨  GC++",
         "GoidaConstruct++ builder.\nEnable/disable modules, embed plugins,\nbuild EXE or AppImage."),
    ]

    def __init__(self, parent=None):
        super().__init__(parent,
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setFixedSize(640, 460)
        sg = QApplication.primaryScreen().geometry()
        self.move((sg.width()-640)//2, (sg.height()-460)//2)
        self._choice = "gui"
        self._selected_idx = 0   # 0=gui 1=cmd
        self._btn_widgets: list[QPushButton] = []
        self._setup()

    def _setup(self):
        t = get_theme(S().theme)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)

        container = QWidget()
        container.setObjectName("launcher_screen")
        container.setStyleSheet(f"""
            #launcher_screen {{
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                    stop:0 {t['bg3']}, stop:0.5 {t['bg2']}, stop:1 {t['bg']});
                border-radius: 22px;
                border: 2px solid {t['accent']};
            }}
        """)
        cl = QVBoxLayout(container)
        cl.setContentsMargins(48, 38, 48, 32)
        cl.setSpacing(0)

        # Header
        hdr = QLabel(f"📱  {APP_NAME}")
        hdr.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hdr.setStyleSheet(f"""
            color: {t['text']}; font-size: 28px; font-weight: bold;
            background: transparent; letter-spacing: 2px;
        """)
        cl.addWidget(hdr)
        cl.addSpacing(4)

        sub = QLabel(TR("launcher_choose"))
        sub.setAlignment(Qt.AlignmentFlag.AlignCenter)
        sub.setStyleSheet(f"color:{t['text_dim']};font-size:13px;background:transparent;")
        cl.addWidget(sub)
        cl.addSpacing(24)

        # Mode cards row
        btn_row = QHBoxLayout()
        btn_row.setSpacing(18)

        for i, (mode_id, label, desc) in enumerate(self._MODES):
            card = QPushButton()
            card.setObjectName(f"launcher_card_{i}")
            card.setCheckable(True)
            card.setChecked(i == 0)
            card.setMinimumSize(230, 130)
            card.setCursor(Qt.CursorShape.PointingHandCursor)

            # Build card layout with icon + text
            card_lay = QVBoxLayout(card)
            card_lay.setContentsMargins(16, 16, 16, 16)
            card_lay.setSpacing(8)

            lbl_main = QLabel(label)
            lbl_main.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl_main.setStyleSheet(
                f"font-size:15px;font-weight:bold;color:{t['text']};"
                "background:transparent;")
            card_lay.addWidget(lbl_main)

            lbl_hint = QLabel(desc)
            lbl_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lbl_hint.setWordWrap(True)
            lbl_hint.setStyleSheet(
                f"font-size:9px;color:{t['text_dim']};background:transparent;")
            card_lay.addWidget(lbl_hint)

            self._apply_card_style(card, t, selected=(i==0))
            card.toggled.connect(lambda checked, idx=i: self._on_card_toggled(idx, checked))
            self._btn_widgets.append(card)
            btn_row.addWidget(card)

        cl.addLayout(btn_row)
        cl.addSpacing(20)

        # Arrow hint
        arrow_hint = QLabel(TR("launcher_arrows"))
        arrow_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        arrow_hint.setStyleSheet(
            f"color:{t['text_dim']};font-size:9px;background:transparent;")
        cl.addWidget(arrow_hint)
        cl.addStretch()

        # Bottom bar
        bot = QHBoxLayout()
        bot.setSpacing(12)

        self._no_show = QCheckBox(
            "Remember my choice" if S().language=="ru" else "Remember my choice")
        self._no_show.setStyleSheet(
            f"color:{t['text_dim']};background:transparent;font-size:10px;")
        self._no_show.setCursor(Qt.CursorShape.PointingHandCursor)
        bot.addWidget(self._no_show)
        bot.addStretch()

        help_btn = QPushButton(TR("launcher_help"))
        help_btn.setFixedHeight(36)
        help_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        help_btn.setStyleSheet(f"""
            QPushButton {{
                background: {t['bg3']}; color: {t['text_dim']};
                border: 1px solid {t['border']}; border-radius: 10px;
                padding: 0 16px; font-size: 11px;
            }}
            QPushButton:hover {{ background: {t['btn_hover']}; color: {t['text']}; }}
        """)
        help_btn.clicked.connect(self._show_help)
        bot.addWidget(help_btn)

        go_btn = QPushButton(TR("launcher_run"))
        go_btn.setFixedHeight(36)
        go_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        go_btn.setStyleSheet(f"""
            QPushButton {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['accent']}, stop:1 {t['accent2']});
                color: white; border: none; border-radius: 10px;
                padding: 0 28px; font-size: 13px; font-weight: bold;
                border-bottom: 3px solid rgba(0,0,0,76);
            }}
            QPushButton:hover {{
                background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                    stop:0 {t['btn_hover']}, stop:1 {t['accent']});
            }}
            QPushButton:pressed {{ border-bottom-width: 1px; }}
        """)
        go_btn.clicked.connect(self._go)
        bot.addWidget(go_btn)

        close_btn = QPushButton("×")
        close_btn.setFixedSize(32, 32)
        close_btn.setCursor(Qt.CursorShape.PointingHandCursor)
        close_btn.setStyleSheet(f"""
            QPushButton {{
                background:{t['btn_bg']};color:{t['text_dim']};
                border:1px solid {t['border']};border-radius:16px;
                font-size:16px;font-weight:bold;
            }}
            QPushButton:hover{{background:#CC2222;color:white;border-color:#CC2222;}}
        """)
        close_btn.clicked.connect(lambda: self._select("gui"))
        bot.addWidget(close_btn)

        cl.addLayout(bot)
        lay.addWidget(container)

    def _apply_card_style(self, card: QPushButton, t: dict, selected: bool):
        if selected:
            card.setStyleSheet(f"""
                QPushButton {{
                    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 {t['accent']}, stop:1 {t['accent2']});
                    border: 2px solid {t['accent']};
                    border-radius: 14px;
                    border-bottom: 4px solid rgba(0,0,0,89);
                    color: white;
                }}
                QPushButton:hover {{ border-color: white; }}
                QPushButton QLabel {{ color: white !important; }}
            """)
            # Force all child QLabels to white so they stay readable
            for lbl in card.findChildren(QLabel):
                lbl.setStyleSheet(lbl.styleSheet()
                    .replace(f"color:{t['text_dim']}", "color:rgba(255,255,255,191)")
                    .replace(f"color:{t['text']}",     "color:white"))
        else:
            card.setStyleSheet(f"""
                QPushButton {{
                    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 {t['bg3']}, stop:1 {t['bg2']});
                    border: 2px solid {t['border']};
                    border-radius: 14px;
                    border-bottom: 4px solid rgba(0,0,0,63);
                    color: {t['text']};
                }}
                QPushButton:hover {{
                    border-color: {t['accent']};
                    background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                        stop:0 {t['btn_hover']}, stop:1 {t['bg2']});
                }}
            """)
            for lbl in card.findChildren(QLabel):
                lbl.setStyleSheet(lbl.styleSheet()
                    .replace("color:rgba(255,255,255,191)", f"color:{t['text_dim']}")
                    .replace("color:white", f"color:{t['text']}"))

    def _on_card_toggled(self, idx: int, checked: bool):
        if checked:
            self._select_card(idx)

    def keyPressEvent(self, event):
        key = event.key()
        if key in (Qt.Key.Key_Left, Qt.Key.Key_Right):
            new_idx = (self._selected_idx + (1 if key == Qt.Key.Key_Right else -1)) % len(self._MODES)
            self._select_card(new_idx)
        elif key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self._go()
        elif key == Qt.Key.Key_Escape:
            self._select("gui")
        elif key == Qt.Key.Key_H or key == Qt.Key.Key_F1:
            self._show_help()
        else:
            super().keyPressEvent(event)

    def _select_card(self, idx: int):
        """Programmatically select card by index — works from both keyboard and mouse."""
        t = get_theme(S().theme)
        self._selected_idx = idx
        self._choice = self._MODES[idx][0]
        for i, b in enumerate(self._btn_widgets):
            b.blockSignals(True)
            b.setChecked(i == idx)
            b.blockSignals(False)
            self._apply_card_style(b, t, selected=(i == idx))

    def _show_help(self):
        t = get_theme(S().theme)
        dlg = QDialog(self)
        dlg.setWindowTitle(TR("help_title"))
        dlg.setFixedSize(480, 420)
        dlg.setStyleSheet(f"""
            QDialog {{ background:{t['bg2']}; border-radius:16px; }}
            QLabel  {{ color:{t['text']}; background:transparent; }}
        """)
        vl = QVBoxLayout(dlg)
        vl.setContentsMargins(28, 24, 28, 20)
        vl.setSpacing(12)

        title = QLabel(TR("help_modes_title"))
        title.setStyleSheet(
            f"font-size:15px;font-weight:bold;color:{t['accent']};")
        vl.addWidget(title)

        txt = QLabel(
            "<b>🖥 GUI mode</b><br>"
            "Полноценный графический интерфейс GoidaPhone.<br>"
            "Доступны: публичный чат, личные сообщения, группы,<br>"
            "голосовые и групповые звонки, передача файлов,<br>"
            "стикеры, темы, настройки, GoidaTerminal, WNS-браузер.<br><br>"
            "<b>⌨ CMD режим</b><br>"
            "Консольный (терминальный) режим — без GUI.<br>"
            "Полезен для серверов, слабых машин, диагностики.<br><br>"
            "<b>Хоткеи выбора:</b><br>"
            "← → — переключение карточек<br>"
            "Enter — запуск выбранного режима<br>"
            "F1 или H — this help<br>"
            "Esc — запуск GUI (по умолчанию)<br><br>"
            "<b>Запомнить выбор:</b><br>"
            "Отметь чекбокс «Запомнить мой выбор» чтобы это окно<br>"
            "не показывалось при следующем запуске.<br>"
            "Включить снова: Настройки → Специалист → «Показывать<br>"
            "выбор режима при каждом запуске»."
        )
        txt.setWordWrap(True)
        txt.setTextFormat(Qt.TextFormat.RichText)
        txt.setStyleSheet(f"font-size:11px;color:{t['text']};line-height:1.5;")
        vl.addWidget(txt)
        vl.addStretch()

        close = QPushButton(_L("Закрыть", "Close", "閉じる"))
        close.setObjectName("accent_btn")
        close.setFixedHeight(34)
        close.clicked.connect(dlg.accept)
        vl.addWidget(close)
        dlg.exec()

    def _go(self):
        self._select(self._choice)

    def _select(self, mode: str):
        self._choice = mode
        if self._no_show.isChecked():
            S().show_launcher = False
            S().set("launcher_default", mode)
        self.mode_selected.emit(mode)
        self.accept()

    def get_choice(self) -> str:
        return self._choice
