#!/usr/bin/env python3
"""
listener.py — Receptor HTTP en el móvil (Termux) para el sensor Wardrive ESP32-S3 (ROADMAP §3.9).

Uso rápido en Termux:
    termux-setup-storage      # solo la primera vez
    termux-wake-lock          # mantener activo en bolsillo/mochila
    python tools/listener.py --port 47471 --out ./wardrive_data --gps
"""

import argparse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import threading
import time

SESSION_RE = re.compile(r"^[0-9a-fA-F]{8}$")
BSSID_RE = re.compile(r"^(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$")
HEX_RE = re.compile(r"^(?:[0-9a-fA-F]{2})+$")
MAX_BODY_BYTES = 512 * 1024

REQUIRED_HEADER_KEYS = {
    "v",
    "mode",
    "scan_type",
    "session_id",
    "scan_id",
    "device_ts_us",
    "scan_duration_ms",
    "total_detected",
    "stored",
    "dropped",
    "lost_before",
    "buffered",
    "devices",
}

REQUIRED_DEVICE_KEYS = {
    "ssid",
    "bssid",
    "rssi",
    "primary_channel",
    "secondary_channel",
    "auth_mode",
    "pairwise_cipher",
    "group_cipher",
    "proto",
    "band_mhz",
    "wps",
    "hidden",
}


def now_iso_ms() -> str:
    return datetime.now().astimezone().isoformat(timespec="milliseconds")


class GpsProvider:
    """Obtiene coordenadas de `termux-location -p network` con caché mínima de 10 s (§3.9)."""

    def __init__(self, enabled: bool, cache_ttl_s: float = 10.0):
        self.enabled = enabled
        self.cache_ttl_s = cache_ttl_s
        self._lock = threading.Lock()
        self._cached_at = 0.0
        self._cached_val = None

    def get(self):
        if not self.enabled:
            return None
        with self._lock:
            now = time.monotonic()
            if self._cached_at > 0.0 and (now - self._cached_at) < self.cache_ttl_s:
                return self._cached_val
            val = self._fetch()
            self._cached_at = now
            self._cached_val = val
            return val

    @staticmethod
    def _fetch():
        try:
            proc = subprocess.run(
                ["termux-location", "-p", "network"],
                capture_output=True,
                text=True,
                timeout=4.0,
                check=False,
            )
            if proc.returncode != 0 or not proc.stdout.strip():
                return None
            data = json.loads(proc.stdout)
            lat = data.get("latitude")
            lon = data.get("longitude")
            acc = data.get("accuracy")
            if not isinstance(lat, (int, float)) or not isinstance(lon, (int, float)):
                return None
            return {
                "lat": float(lat),
                "lon": float(lon),
                "acc_m": float(acc) if isinstance(acc, (int, float)) else None,
                "src": "phone",
            }
        except Exception:
            return None


def validate_scan_payload(obj: dict) -> None:
    if not isinstance(obj, dict):
        raise ValueError("La línea no es un objeto JSON")

    missing = REQUIRED_HEADER_KEYS - set(obj.keys())
    if missing:
        raise ValueError(f"Faltan campos de cabecera: {sorted(missing)}")

    if obj["v"] != 1:
        raise ValueError("v != 1")
    if obj["mode"] != "scan":
        raise ValueError("mode != 'scan'")
    if obj["scan_type"] != "passive":
        raise ValueError("scan_type != 'passive'")
    if not isinstance(obj["session_id"], str) or not SESSION_RE.fullmatch(obj["session_id"]):
        raise ValueError("session_id inválido")
    if not isinstance(obj["scan_id"], int) or isinstance(obj["scan_id"], bool) or obj["scan_id"] < 1:
        raise ValueError("scan_id inválido")

    for k in ("device_ts_us", "scan_duration_ms", "total_detected", "stored", "dropped", "lost_before"):
        if not isinstance(obj[k], int) or isinstance(obj[k], bool) or obj[k] < 0:
            raise ValueError(f"{k} inválido")

    if not isinstance(obj["buffered"], bool):
        raise ValueError("buffered debe ser booleano")

    devices = obj["devices"]
    if not isinstance(devices, list):
        raise ValueError("devices debe ser una lista")
    if len(devices) != obj["stored"]:
        raise ValueError("stored no coincide con len(devices)")
    if obj["dropped"] != max(0, obj["total_detected"] - obj["stored"]):
        raise ValueError("dropped incoherente con total_detected - stored")

    for dev in devices:
        if not isinstance(dev, dict):
            raise ValueError("Elemento de devices no es objeto")
        missing_dev = REQUIRED_DEVICE_KEYS - set(dev.keys())
        if missing_dev:
            raise ValueError(f"Faltan campos en device: {sorted(missing_dev)}")
        if not isinstance(dev["ssid"], str) or len(dev["ssid"]) > 32:
            raise ValueError("ssid inválido")
        if "ssid_hex" in dev:
            if not isinstance(dev["ssid_hex"], str) or not HEX_RE.fullmatch(dev["ssid_hex"]):
                raise ValueError("ssid_hex inválido")
        if not isinstance(dev["bssid"], str) or not BSSID_RE.fullmatch(dev["bssid"]):
            raise ValueError("bssid inválido")
        if not isinstance(dev["rssi"], int) or not (-128 <= dev["rssi"] <= 0):
            raise ValueError("rssi fuera de rango")
        if not isinstance(dev["primary_channel"], int) or not (1 <= dev["primary_channel"] <= 14):
            raise ValueError("primary_channel fuera de rango")
        if not isinstance(dev["secondary_channel"], int) or dev["secondary_channel"] not in (0, 1, 2):
            raise ValueError("secondary_channel inválido")
        for str_key in ("auth_mode", "pairwise_cipher", "group_cipher", "proto"):
            if not isinstance(dev[str_key], str):
                raise ValueError(f"{str_key} no es string")
        if dev["band_mhz"] not in (20, 40):
            raise ValueError("band_mhz inválido")
        if not isinstance(dev["wps"], bool) or not isinstance(dev["hidden"], bool):
            raise ValueError("wps/hidden deben ser booleanos")

    if "promisc" in obj and obj["promisc"] is not None:
        prom = obj["promisc"]
        if not isinstance(prom, dict):
            raise ValueError("promisc debe ser un objeto")
        for k in ("mgmt_frames", "data_frames", "probe_req_frames", "deauth_frames"):
            if not isinstance(prom.get(k), int) or prom[k] < 0:
                raise ValueError(f"promisc.{k} inválido")
        if not isinstance(prom.get("probes"), list) or not isinstance(prom.get("clients"), list):
            raise ValueError("promisc.probes/clients deben ser listas")


class ListenerState:
    def __init__(self, out_dir: Path, gps_enabled: bool = False, session_name: str = None):
        self.out_dir = Path(out_dir)
        self.out_dir.mkdir(parents=True, exist_ok=True)
        self.gps = GpsProvider(enabled=gps_enabled)
        self.session_name = session_name
        self.lock = threading.Lock()
        self.seen = set()  # (session_id, scan_id)
        self.sessions = []
        self.session_gps = {}  # session_id -> gps dict fijado a nivel de sesión
        self.lines_written = 0
        self.current_session_id = None
        self.current_file_path = None

    def _register_session_metadata(self, session_id: str, gps_val):
        tags_path = self.out_dir / "tags.json"
        try:
            if tags_path.exists():
                data = json.loads(tags_path.read_text(encoding="utf-8"))
            else:
                data = {"sessions": {}, "networks": {}, "devices": {}}
        except Exception:
            data = {"sessions": {}, "networks": {}, "devices": {}}

        sessions_map = data.setdefault("sessions", {})
        entry = sessions_map.setdefault(session_id, {})
        if self.session_name and not entry.get("name"):
            entry["name"] = self.session_name
        if gps_val and not entry.get("gps"):
            entry["gps"] = gps_val
        if "created_at" not in entry:
            entry["created_at"] = now_iso_ms()

        try:
            tags_path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        except Exception:
            pass

    def _ensure_file_for_session(self, session_id: str) -> Path:
        is_new_session = session_id not in self.sessions
        if is_new_session:
            self.sessions.append(session_id)
            # GPS vinculado a la sesión (se consulta una vez al abrir la sesión)
            self.session_gps[session_id] = self.gps.get()
            self._register_session_metadata(session_id, self.session_gps[session_id])
        if self.current_file_path is None or self.current_session_id != session_id:
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            candidate = self.out_dir / f"wardrive_{stamp}_{session_id}.jsonl"
            # También mantenemos el nombre canónico wardrive_YYYYMMDD_HHMM.jsonl si está libre
            canonical = self.out_dir / f"wardrive_{datetime.now().strftime('%Y%m%d_%H%M')}.jsonl"
            if not canonical.exists() or self.current_file_path is None:
                self.current_file_path = canonical if not canonical.exists() else candidate
            else:
                self.current_file_path = candidate
            self.current_session_id = session_id
        return self.current_file_path

    def ingest_lines(self, raw_body: str):
        raw_lines = [ln for ln in raw_body.splitlines() if ln.strip()]
        if not raw_lines:
            return 400, {"error": "body vacío", "first_bad_scan_id": 0}

        accepted = 0
        last_scan_id = 0

        with self.lock:
            for idx, line in enumerate(raw_lines):
                candidate_scan_id = 0
                try:
                    payload = json.loads(line)
                    if isinstance(payload, dict) and isinstance(payload.get("scan_id"), int):
                        candidate_scan_id = payload["scan_id"]
                    validate_scan_payload(payload)
                except Exception as exc:
                    return 400, {
                        "error": f"línea {idx + 1} inválida: {exc}",
                        "first_bad_scan_id": candidate_scan_id,
                        "accepted": accepted,
                        "last_scan_id": last_scan_id,
                    }

                key = (payload["session_id"], payload["scan_id"])
                last_scan_id = payload["scan_id"]
                accepted += 1

                if key in self.seen:
                    # Deduplicado: se cuenta como aceptado pero no se duplica en disco (§3.9)
                    continue

                target_file = self._ensure_file_for_session(payload["session_id"])
                gps_snapshot = self.session_gps.get(payload["session_id"])
                envelope = {
                    "recv_at": now_iso_ms(),
                    "session_name": self.session_name,
                    "gps": gps_snapshot,
                    "payload": payload,
                }
                with target_file.open("a", encoding="utf-8") as fh:
                    fh.write(json.dumps(envelope, ensure_ascii=False, separators=(",", ":")) + "\n")
                    fh.flush()

                self.seen.add(key)
                self.lines_written += 1
                self._print_live_scan(payload)

        return 200, {"accepted": accepted, "last_scan_id": last_scan_id}

    def _print_live_scan(self, payload: dict) -> None:
        ts_short = datetime.now().strftime("%H:%M:%S")
        devices = sorted(payload["devices"], key=lambda d: d["rssi"], reverse=True)
        buf_tag = " [BUFFERED]" if payload["buffered"] else ""
        lost_tag = f" [LOST={payload['lost_before']}]" if payload["lost_before"] else ""
        print(
            f"\n\033[1;36m[{ts_short}] Sesion {payload['session_id']} | "
            f"Escaneo #{payload['scan_id']} ({payload['scan_duration_ms']} ms) | "
            f"{payload['stored']}/{payload['total_detected']} APs{buf_tag}{lost_tag}\033[0m"
        )
        print(f"  {'RSSI':>5}  {'CH':>2}  {'BW/PHY':<7}  {'AUTH':<15}  {'CIPHER':<9}  {'WPS':<3}  {'BSSID':<17}  SSID")
        for d in devices:
            ssid_disp = "<OCULTA>" if d["hidden"] else d["ssid"]
            wps_disp = "SI" if d["wps"] else "no"
            bw_phy = f"{d['band_mhz']}/{d['proto']}"
            rssi = d["rssi"]
            if rssi >= -55:
                c = "\033[1;32m"
            elif rssi >= -75:
                c = "\033[1;33m"
            else:
                c = "\033[0;37m"
            print(
                f"  {c}{rssi:>4}dBm\033[0m  {d['primary_channel']:>2}  {bw_phy:<7}  "
                f"{d['auth_mode']:<15}  {d['pairwise_cipher']:<9}  {wps_disp:<3}  "
                f"{d['bssid']}  {ssid_disp}"
            )
        prom = payload.get("promisc")
        if isinstance(prom, dict):
            probes = prom.get("probes", [])
            clients = prom.get("clients", [])
            deauth = prom.get("deauth_frames", 0)
            deauth_str = f"\033[1;31m{deauth} DEAUTH!\033[0m" if deauth > 0 else "0 deauth"
            print(
                f"  \033[1;35m[PROMISC]\033[0m mgmt={prom.get('mgmt_frames', 0)} "
                f"data={prom.get('data_frames', 0)} probes={len(probes)} "
                f"clientes={len(clients)} | {deauth_str}"
            )
            for cl in sorted(clients, key=lambda x: x.get("rssi", -128), reverse=True)[:8]:
                print(
                    f"    \033[0;36m[STA]\033[0m {cl['sta_mac']} -> AP {cl['bssid']} "
                    f"(ch {cl['channel']:>2}, {cl['rssi']:>4}dBm, {cl['frames']} tramas)"
                )
            for pr in sorted(probes, key=lambda x: x.get("rssi", -128), reverse=True)[:8]:
                pssid = pr.get("ssid") or "<BROADCAST>"
                print(
                    f"    \033[0;33m[PROBE]\033[0m {pr['sta_mac']} busca '{pssid}' "
                    f"(ch {pr['channel']:>2}, {pr['rssi']:>4}dBm, x{pr['count']})"
                )
        print(f"\033[0;90m  -> Guardado en {self.current_file_path.name} (total sesion: {self.lines_written} escaneos)\033[0m", flush=True)

    def health(self) -> dict:
        with self.lock:
            return {
                "status": "ok",
                "sessions": list(self.sessions),
                "lines": self.lines_written,
            }


def make_handler(state: ListenerState):
    class Handler(BaseHTTPRequestHandler):
        def _send_json(self, status: int, body: dict):
            raw = json.dumps(body, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            if self.path == "/health":
                self._send_json(200, state.health())
            else:
                self._send_json(404, {"error": "not found"})

        def do_POST(self):
            if self.path != "/scans":
                self._send_json(404, {"error": "not found"})
                return

            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0 or length > MAX_BODY_BYTES:
                    self._send_json(400, {"error": "Content-Length inválido", "first_bad_scan_id": 0})
                    return
                raw_body = self.rfile.read(length).decode("utf-8")
            except Exception as exc:
                self._send_json(400, {"error": f"lectura fallida: {exc}", "first_bad_scan_id": 0})
                return

            status, resp = state.ingest_lines(raw_body)
            self._send_json(status, resp)

        def log_message(self, fmt, *args):
            # Silenciar el "POST /scans HTTP/1.1 200 -" para mostrar solo el contenido real de cada escaneo
            return

    return Handler


def main():
    parser = argparse.ArgumentParser(description="Listener HTTP Wardrive para Termux (ROADMAP §3.9)")
    parser.add_argument("--host", default="0.0.0.0", help="IP de escucha (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=47471, help="Puerto TCP (default: 47471)")
    parser.add_argument("--out", type=Path, default=Path("./wardrive_data"), help="Directorio de salida JSONL")
    parser.add_argument("--gps", action="store_true", help="Fijar coordenadas de termux-location al iniciar cada sesión")
    parser.add_argument("--name", default=None, help="Nombre descriptivo para etiquetar la sesión actual")
    args = parser.parse_args()

    state = ListenerState(out_dir=args.out, gps_enabled=args.gps, session_name=args.name)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    print(f"Wardrive listener escuchando en http://{args.host}:{server.server_address[1]}/scans (dir={args.out})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nCerrando listener...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
