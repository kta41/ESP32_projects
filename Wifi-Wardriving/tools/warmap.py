#!/usr/bin/env python3
"""
warmap.py — Plataforma SIGINT & Analizador de Capturas JSONL de Wardrive.

- Resuelve fabricantes mediante la base de datos IEEE OUI completa (40.355 prefijos,
  `oui_es_db.json.gz`), incluyendo todos los fabricantes de móviles, routers de
  operadoras españolas (Movistar, Orange, Vodafone, Digi, MásMóvil) e IoT, más
  detección de BSSIDs virtuales (bit LAA invertido sobre OUI real) y MACs aleatorias.
- Correlaciona Redes (APs / SSIDs), Dispositivos Clientes (STAs + PNL Probe Requests),
  Persistencia Multi-Sesión, GPS vinculado a la sesión y etiquetas custom (`tags.json`).
- Genera informes en terminal, CSV, WiGLE CSV v1.4 y la Consola Web SIGINT (`--html`).
"""

import argparse
from collections import Counter, defaultdict
import csv
from datetime import datetime
import gzip
import json
from pathlib import Path
import sys

from dashboard_html import render_sigint_dashboard

_OUI_DB = None


def _load_oui_db() -> dict:
    global _OUI_DB
    if _OUI_DB is not None:
        return _OUI_DB
    db_path = Path(__file__).resolve().parent / "oui_es_db.json.gz"
    if db_path.exists():
        try:
            with gzip.open(db_path, "rt", encoding="utf-8") as gz:
                _OUI_DB = json.load(gz)
                return _OUI_DB
        except Exception:
            pass
    _OUI_DB = {}
    return _OUI_DB


def is_randomized_mac(mac: str) -> bool:
    """Devuelve True si tiene el bit Locally-Administered activo y NO corresponde a un BSSID virtual de un OUI real."""
    clean = mac.replace(":", "").replace("-", "").upper()
    if len(clean) < 6:
        return False
    db = _load_oui_db()
    p6 = clean[:6]
    if p6 in db:
        return False
    b0 = int(p6[:2], 16)
    if (b0 & 0x02) == 0:
        return False
    # Comprobar si los 2 bytes centrales del OUI coinciden con algún OUI real cuando el router
    # crea un BSSID virtual modificando el primer octeto (Arcadyan, Sagemcom, ZTE, TP-Link, etc.)
    for mask in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x20, 0x40, 0x80, 0xC0, 0xCE):
        cand_b0 = (b0 & ~0x02) ^ mask
        if f"{cand_b0:02X}{p6[2:]}" in db:
            return False
    return True


def resolve_vendor(mac: str) -> str:
    clean = mac.replace(":", "").replace("-", "").upper()
    if len(clean) < 6:
        return "Desconocido"
    db = _load_oui_db()
    p6 = clean[:6]
    if p6 in db and db[p6] != "Private":
        return db[p6]
    b0 = int(p6[:2], 16)
    if b0 & 0x02:
        # Muchos routers (Arcadyan, ZTE, Sagemcom, TP-Link, Xiaomi) crean BSSIDs virtuales
        # activando el bit 0x02 (LAA) y variando el nibble superior del primer octeto:
        for mask in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x20, 0x40, 0x80, 0xC0, 0xCE):
            cand_b0 = (b0 & ~0x02) ^ mask
            cand = f"{cand_b0:02X}{p6[2:]}"
            if cand in db and db[cand] != "Private":
                return f"{db[cand]} (Virtual)"
        return "MAC Aleatoria (iOS/Android Privacy)"
    if p6 in db:
        return db[p6]
    return "OUI No Registrado"


def load_tags_file(paths) -> tuple[dict, Path]:
    """Busca `tags.json` en el directorio de los JSONL o en `./wardrive_data/tags.json`."""
    candidates = []
    if paths:
        candidates.append(paths[0].resolve().parent / "tags.json")
    candidates.extend([
        Path("./wardrive_data/tags.json"),
        Path("/home/Kta41/wardrive/wardrive_data/tags.json"),
    ])
    for c in candidates:
        if c.exists():
            try:
                data = json.loads(c.read_text(encoding="utf-8"))
                return {
                    "sessions": data.get("sessions", {}),
                    "networks": data.get("networks", {}),
                    "devices": data.get("devices", {}),
                }, c
            except Exception:
                pass
    default_path = candidates[0] if candidates else Path("./wardrive_data/tags.json")
    return {"sessions": {}, "networks": {}, "devices": {}}, default_path


def load_records(paths):
    aps = {}
    sightings = Counter()
    ap_sessions = defaultdict(set)
    sessions_meta = {}
    session_aps = defaultdict(set)
    session_clients = defaultdict(set)
    total_scans = 0
    first_seen = {}

    clients_by_bssid = defaultdict(dict)  # bssid -> {sta_mac: info}
    probes_by_sta = defaultdict(set)      # sta_mac -> {ssid, ...}
    probe_best = {}                       # (sta_mac, ssid) -> info
    devices = {}                          # sta_mac -> aggregated device dossier
    promisc_totals = Counter()

    def ensure_device(sta_mac: str, rssi: int, ch: int, sid: str):
        sta = sta_mac.upper()
        d = devices.get(sta)
        if d is None:
            d = {
                "sta_mac": sta,
                "vendor": resolve_vendor(sta),
                "is_randomized": is_randomized_mac(sta),
                "rssi": rssi,
                "channel": ch,
                "total_frames": 0,
                "sessions": set(),
                "associated_bssids": set(),
                "probed_ssids": set(),
            }
            devices[sta] = d
        else:
            if rssi > d["rssi"]:
                d["rssi"] = rssi
                d["channel"] = ch
        d["sessions"].add(sid)
        session_clients[sid].add(sta)
        return d

    for path in paths:
        if not path.exists():
            continue
        with path.open("r", encoding="utf-8") as fh:
            for raw in fh:
                line = raw.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except Exception:
                    continue
                recv_at = obj.get("recv_at", "1970-01-01T00:00:00") if isinstance(obj, dict) else "1970-01-01T00:00:00"
                gps = obj.get("gps") if isinstance(obj, dict) else None
                env_sname = obj.get("session_name") if isinstance(obj, dict) else None
                payload = obj.get("payload", obj)
                if not isinstance(payload, dict) or payload.get("mode") != "scan":
                    continue

                total_scans += 1
                sid = payload.get("session_id", "unknown")
                ts_short = recv_at[:19].replace("T", " ")

                smeta = sessions_meta.get(sid)
                if smeta is None:
                    smeta = {
                        "session_id": sid,
                        "name": env_sname,
                        "file": path.name,
                        "first_seen": ts_short,
                        "last_seen": ts_short,
                        "scans": 0,
                        "gps": gps if isinstance(gps, dict) else None,
                    }
                    sessions_meta[sid] = smeta
                smeta["scans"] += 1
                smeta["last_seen"] = ts_short
                if smeta["gps"] is None and isinstance(gps, dict):
                    smeta["gps"] = gps
                if not smeta.get("name") and env_sname:
                    smeta["name"] = env_sname

                for dev in payload.get("devices", []):
                    bssid = dev.get("bssid", "").upper()
                    if not bssid:
                        continue
                    sightings[bssid] += 1
                    ap_sessions[bssid].add(sid)
                    session_aps[sid].add(bssid)
                    if bssid not in first_seen:
                        first_seen[bssid] = ts_short
                    if bssid not in aps or dev.get("rssi", -128) > aps[bssid]["rssi"]:
                        entry = dict(dev)
                        entry["bssid"] = bssid
                        if isinstance(gps, dict):
                            entry["gps"] = gps
                        aps[bssid] = entry
                    elif not aps[bssid].get("ssid") and dev.get("ssid"):
                        aps[bssid]["ssid"] = dev["ssid"]
                        aps[bssid]["hidden"] = False

                prom = payload.get("promisc")
                if isinstance(prom, dict):
                    for k in ("mgmt_frames", "data_frames", "probe_req_frames", "deauth_frames"):
                        promisc_totals[k] += int(prom.get(k, 0))
                    for cl in prom.get("clients", []):
                        sta = cl.get("sta_mac", "").upper()
                        bssid = cl.get("bssid", "").upper()
                        if not sta or not bssid:
                            continue
                        rssi = int(cl.get("rssi", -128))
                        ch = int(cl.get("channel", 0))
                        frames = int(cl.get("frames", 1))

                        d_entry = ensure_device(sta, rssi, ch, sid)
                        d_entry["total_frames"] += frames
                        d_entry["associated_bssids"].add(bssid)

                        prev = clients_by_bssid[bssid].get(sta)
                        if prev is None:
                            clients_by_bssid[bssid][sta] = {
                                "sta_mac": sta,
                                "bssid": bssid,
                                "rssi": rssi,
                                "channel": ch,
                                "frames": frames,
                            }
                        else:
                            prev["frames"] += frames
                            if rssi > prev["rssi"]:
                                prev["rssi"] = rssi
                                prev["channel"] = ch

                    for pr in prom.get("probes", []):
                        sta = pr.get("sta_mac", "").upper()
                        pssid = pr.get("ssid", "")
                        if not sta:
                            continue
                        rssi = int(pr.get("rssi", -128))
                        ch = int(pr.get("channel", 0))
                        cnt = int(pr.get("count", 1))

                        d_entry = ensure_device(sta, rssi, ch, sid)
                        d_entry["total_frames"] += cnt
                        if pssid:
                            d_entry["probed_ssids"].add(pssid)
                            probes_by_sta[sta].add(pssid)

                        key = (sta, pssid)
                        prev = probe_best.get(key)
                        if prev is None:
                            probe_best[key] = {
                                "sta_mac": sta,
                                "ssid": pssid,
                                "rssi": rssi,
                                "channel": ch,
                                "count": cnt,
                            }
                        else:
                            prev["count"] += cnt
                            if rssi > prev["rssi"]:
                                prev["rssi"] = rssi

    for sid, smeta in sessions_meta.items():
        smeta["unique_aps"] = len(session_aps[sid])
        smeta["unique_clients"] = len(session_clients[sid])

    promisc_data = {
        "totals": promisc_totals,
        "clients_by_bssid": clients_by_bssid,
        "probes_by_sta": probes_by_sta,
        "probe_best": probe_best,
        "first_seen": first_seen,
        "ap_sessions": ap_sessions,
        "sessions_meta": sessions_meta,
        "devices": devices,
    }
    return aps, sightings, set(sessions_meta.keys()), total_scans, promisc_data


def build_intelligence_dataset(aps, sightings, sessions, total_scans, promisc_data, tags_data) -> dict:
    clients_by_bssid = promisc_data["clients_by_bssid"]
    ap_sessions = promisc_data["ap_sessions"]
    sessions_meta = promisc_data["sessions_meta"]
    devices_map = promisc_data["devices"]

    # Mapear SSID -> lista de BSSIDs
    ssid_to_bssids = defaultdict(list)
    for bssid, ap in aps.items():
        if ap.get("ssid"):
            ssid_to_bssids[ap["ssid"]].append(bssid)

    # Mapear SSID -> STAs que lo buscan
    ssid_probing_stas = defaultdict(set)
    for sta, dev in devices_map.items():
        for pssid in dev["probed_ssids"]:
            ssid_probing_stas[pssid].add(sta)

    known_local_ssids = set(ssid_to_bssids.keys())

    ap_list = []
    ap_by_bssid = {}
    for bssid, d in sorted(aps.items(), key=lambda kv: kv[1]["rssi"], reverse=True):
        ssid = d.get("ssid", "")
        is_weak = (
            d["auth_mode"] in ("OPEN", "WEP", "WPA_PSK", "WPA_WPA2_PSK")
            or "TKIP" in d["pairwise_cipher"]
        )
        item = {
            "bssid": bssid,
            "ssid": ssid,
            "hidden": bool(d.get("hidden")),
            "rssi": int(d["rssi"]),
            "primary_channel": int(d["primary_channel"]),
            "band_mhz": int(d.get("band_mhz", 20)),
            "proto": d.get("proto", "bgn"),
            "auth_mode": d["auth_mode"],
            "pairwise_cipher": d["pairwise_cipher"],
            "group_cipher": d.get("group_cipher", ""),
            "wps": bool(d.get("wps")),
            "is_weak": is_weak,
            "sightings": int(sightings[bssid]),
            "vendor": resolve_vendor(bssid),
            "sessions": sorted(ap_sessions[bssid]),
            "clients": sorted(clients_by_bssid.get(bssid, {}).keys()),
            "probing_stas": sorted(ssid_probing_stas.get(ssid, set())) if ssid else [],
            "cluster_size": len(ssid_to_bssids.get(ssid, [])) if ssid else 1,
        }
        ap_list.append(item)
        ap_by_bssid[bssid] = item

    dev_list = []
    device_by_mac = {}
    all_probed_ssids = set()
    matched_probed_ssids = set()

    for sta, d in sorted(devices_map.items(), key=lambda kv: kv[1]["rssi"], reverse=True):
        assoc_bssids = sorted(d["associated_bssids"])
        assoc_ssids = sorted({aps[b]["ssid"] for b in assoc_bssids if b in aps and aps[b].get("ssid")})
        probed = sorted(d["probed_ssids"])
        matched_local = [s for s in probed if s in known_local_ssids]
        all_probed_ssids.update(probed)
        matched_probed_ssids.update(matched_local)

        item = {
            "sta_mac": sta,
            "vendor": d["vendor"],
            "is_randomized": d["is_randomized"],
            "rssi": int(d["rssi"]),
            "channel": int(d["channel"]),
            "total_frames": int(d["total_frames"]),
            "sessions": sorted(d["sessions"]),
            "associated_bssids": assoc_bssids,
            "associated_ssids": assoc_ssids,
            "probed_ssids": probed,
            "matched_local_ssids": matched_local,
        }
        dev_list.append(item)
        device_by_mac[sta] = item

    ch_counts = Counter(a["primary_channel"] for a in ap_list)
    auth_counts = Counter(a["auth_mode"] for a in ap_list)

    ssid_collisions = [
        {"ssid": ssid, "bssids": sorted(bssids)}
        for ssid, bssids in sorted(ssid_to_bssids.items(), key=lambda kv: len(kv[1]), reverse=True)
        if len(bssids) >= 2
    ]

    return {
        "generated_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "total_scans": total_scans,
        "unique_ssids": len(ssid_to_bssids),
        "probed_ssids_count": len(all_probed_ssids),
        "matched_probes_count": len(matched_probed_ssids),
        "matched_ssids_set": sorted(matched_probed_ssids),
        "totals": {
            "mgmt_frames": int(promisc_data["totals"]["mgmt_frames"]),
            "data_frames": int(promisc_data["totals"]["data_frames"]),
            "probe_req_frames": int(promisc_data["totals"]["probe_req_frames"]),
            "deauth_frames": int(promisc_data["totals"]["deauth_frames"]),
        },
        "channel_counts": {str(k): v for k, v in ch_counts.items()},
        "auth_counts": dict(auth_counts.most_common()),
        "ssid_collisions": ssid_collisions,
        "sessions": [sessions_meta[k] for k in sorted(sessions_meta.keys())],
        "aps": ap_list,
        "ap_by_bssid": ap_by_bssid,
        "devices": dev_list,
        "device_by_mac": device_by_mac,
    }


def print_report(aps, sightings, sessions, total_scans, promisc_data, tags_data=None):
    tags_data = tags_data or {"sessions": {}, "networks": {}, "devices": {}}
    print(f"\n\033[1;36m================ WARMAP — RESUMEN DE INTELIGENCIA SIGINT ================\033[0m")
    print(f"Sesiones analizadas : {len(sessions)} ({', '.join(sorted(sessions))})")
    print(f"Escaneos totales    : {total_scans}")
    print(f"Puntos de acceso    : {len(aps)} BSSIDs únicos")
    totals = promisc_data["totals"]
    clients_by_bssid = promisc_data["clients_by_bssid"]
    devices_map = promisc_data["devices"]
    directed_probes = [v for v in promisc_data["probe_best"].values() if v["ssid"]]
    if totals:
        print(
            f"Tráfico promiscuo   : {totals['mgmt_frames']} MGMT, {totals['data_frames']} DATA, "
            f"{len(devices_map)} dispositivos (STAs), {len(directed_probes)} SSIDs buscados, "
            f"{totals['deauth_frames']} tramas deauth\n"
        )
    else:
        print()

    if not aps:
        print("No hay puntos de acceso registrados.")
        return

    ch_cnt = Counter(d["primary_channel"] for d in aps.values())
    auth_cnt = Counter(d["auth_mode"] for d in aps.values())
    cipher_cnt = Counter(d["pairwise_cipher"] for d in aps.values())

    print("\033[1;33m[Distribución por Canal 2.4 GHz]\033[0m")
    for ch in sorted(ch_cnt):
        bar = "█" * min(40, ch_cnt[ch])
        print(f"  Canal {ch:>2}: {ch_cnt[ch]:>3} APs  {bar}")

    print("\n\033[1;33m[Autenticación y Cifrado]\033[0m")
    for k, v in auth_cnt.most_common():
        print(f"  Auth   {k:<18}: {v:>3} APs")
    for k, v in cipher_cnt.most_common():
        print(f"  Cipher {k:<18}: {v:>3} APs")

    weak = [
        d for d in aps.values()
        if d["auth_mode"] in ("OPEN", "WEP", "WPA_PSK", "WPA_WPA2_PSK")
        or "TKIP" in d["pairwise_cipher"]
    ]
    wps_list = [d for d in aps.values() if d.get("wps")]
    hidden_list = [d for d in aps.values() if d.get("hidden")]

    print(f"\n\033[1;31m[Puntos de Interés de Auditoría]\033[0m")
    print(f"  • Redes abiertas / WEP / WPA1-TKIP : {len(weak)}")
    for d in sorted(weak, key=lambda x: x["rssi"], reverse=True)[:10]:
        ssid = "<OCULTA>" if d["hidden"] else d["ssid"]
        print(f"      {d['rssi']:>4}dBm | ch {d['primary_channel']:>2} | {d['auth_mode']}/{d['pairwise_cipher']} | {d['bssid']} | {ssid}")

    print(f"  • Redes con WPS expuesto           : {len(wps_list)}")
    print(f"  • Redes con SSID oculto            : {len(hidden_list)}")
    for d in sorted(hidden_list, key=lambda x: x["rssi"], reverse=True):
        print(f"      {d['rssi']:>4}dBm | ch {d['primary_channel']:>2} | {d['auth_mode']:<14} | {d['bssid']} ({resolve_vendor(d['bssid'])})")

    if devices_map:
        print(f"\n\033[1;35m[Modo Promiscuo — Dispositivos Clientes ({len(devices_map)} STAs)]\033[0m")
        for sta, d in sorted(devices_map.items(), key=lambda kv: kv[1]["rssi"], reverse=True)[:16]:
            tag_info = tags_data.get("devices", {}).get(sta, {})
            tag_str = f" [{tag_info['label']}]" if tag_info.get("label") else ""
            assoc_str = ", ".join(
                f"{b} ({aps.get(b, {}).get('ssid') or 'AP ext'})" for b in sorted(d["associated_bssids"])
            ) or "Sin asociar"
            probes_str = ", ".join(sorted(d["probed_ssids"])) or "—"
            ses_cnt = len(d["sessions"])
            print(
                f"  {d['rssi']:>4}dBm | {sta}{tag_str} | {d['vendor']:<32.32} | "
                f"{ses_cnt} ses | AP: {assoc_str} | Busca: {probes_str}"
            )

    print(f"\n\033[1;32m[Catálogo Completo de Redes ({len(aps)} BSSIDs por potencia RSSI)]\033[0m")
    print(f"  {'RSSI':>5}  {'CH':>2}  {'SES':>3}  {'STAs':>4}  {'WPS':<3}  {'AUTH':<15}  {'CIPHER':<9}  {'BSSID':<17}  {'SSID':<24}  FABRICANTE")
    ap_sessions = promisc_data["ap_sessions"]
    for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
        bssid = d["bssid"]
        ssid = "<OCULTA>" if d["hidden"] else d["ssid"]
        wps = "SI" if d.get("wps") else "no"
        vendor = resolve_vendor(bssid)
        n_ses = len(ap_sessions[bssid])
        n_stas = len(clients_by_bssid.get(bssid, {}))
        print(
            f"  {d['rssi']:>4}dBm  {d['primary_channel']:>2}  {n_ses:>2}s  {n_stas:>4}  {wps:<3}  "
            f"{d['auth_mode']:<15}  {d['pairwise_cipher']:<9}  {bssid}  {ssid:<24.24}  {vendor}"
        )


def export_csv(aps, sightings, promisc_data, csv_path: Path, tags_data=None):
    tags_data = tags_data or {"networks": {}}
    clients_by_bssid = promisc_data["clients_by_bssid"]
    ap_sessions = promisc_data["ap_sessions"]
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow([
            "bssid", "ssid", "tag", "hidden", "rssi_max", "channel", "band_mhz",
            "proto", "auth_mode", "pairwise_cipher", "group_cipher", "wps",
            "sightings", "sessions_count", "clients_count", "vendor",
        ])
        for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
            bssid = d["bssid"]
            net_tags = tags_data.get("networks", {})
            tag_obj = net_tags.get(bssid) or net_tags.get(f"SSID:{d.get('ssid', '')}") or {}
            writer.writerow([
                bssid,
                d["ssid"],
                tag_obj.get("label", ""),
                d["hidden"],
                d["rssi"],
                d["primary_channel"],
                d["band_mhz"],
                d["proto"],
                d["auth_mode"],
                d["pairwise_cipher"],
                d["group_cipher"],
                d["wps"],
                sightings[bssid],
                len(ap_sessions[bssid]),
                len(clients_by_bssid.get(bssid, {})),
                resolve_vendor(bssid),
            ])
    print(f"\nCSV exportado en: {csv_path}")


def export_wigle(aps, promisc_data, wigle_path: Path, tags_data=None):
    tags_data = tags_data or {"sessions": {}}
    first_seen = promisc_data["first_seen"]
    ap_sessions = promisc_data["ap_sessions"]
    sessions_meta = promisc_data["sessions_meta"]
    with wigle_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow([
            "WigleWifi-1.4",
            "appRelease=1.0",
            "model=ESP32-S3",
            "release=IDF-6.1",
            "device=Wardrive-S3",
            "display=CLI",
            "board=ESP32S3",
            "brand=Espressif",
        ])
        writer.writerow([
            "MAC", "SSID", "AuthMode", "FirstSeen", "Channel", "RSSI",
            "CurrentLatitude", "CurrentLongitude", "AltitudeMeters", "AccuracyMeters", "Type",
        ])
        for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
            bssid = d["bssid"]
            gps = d.get("gps") or {}
            if not gps:
                for sid in ap_sessions.get(bssid, []):
                    st_gps = tags_data.get("sessions", {}).get(sid, {}).get("gps")
                    if st_gps:
                        gps = st_gps
                        break
                    sm_gps = sessions_meta.get(sid, {}).get("gps")
                    if sm_gps:
                        gps = sm_gps
                        break
            caps = f"[{d['auth_mode']}-{d['pairwise_cipher']}]"
            if d.get("wps"):
                caps += "[WPS]"
            caps += "[ESS]"
            writer.writerow([
                bssid,
                d["ssid"],
                caps,
                first_seen.get(bssid, "1970-01-01 00:00:00"),
                d["primary_channel"],
                d["rssi"],
                gps.get("lat", 0.0),
                gps.get("lon", 0.0),
                0.0,
                gps.get("acc_m", 0.0) or 0.0,
                "WIFI",
            ])
    print(f"\nWiGLE CSV v1.4 exportado en: {wigle_path}")


def export_html(aps, sightings, sessions, total_scans, promisc_data, html_path: Path, tags_data=None):
    tags_data = tags_data or {"sessions": {}, "networks": {}, "devices": {}}
    dataset = build_intelligence_dataset(aps, sightings, sessions, total_scans, promisc_data, tags_data)
    render_sigint_dashboard(dataset, tags_data, html_path)
    print(f"\nConsola SIGINT HTML exportada en: {html_path}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Plataforma SIGINT y Analizador de capturas JSONL de Wardrive (warmap)")
    parser.add_argument("files", nargs="*", type=Path, help="Ficheros .jsonl (por defecto busca en ./wardrive_data)")
    parser.add_argument("--csv", type=Path, help="Exportar tabla única de BSSIDs a CSV")
    parser.add_argument("--wigle", type=Path, help="Exportar en formato oficial WiGLE CSV v1.4")
    parser.add_argument("--html", type=Path, help="Exportar Consola SIGINT interactiva en HTML")
    parser.add_argument("--tag-session", nargs=2, metavar=("SESSION_ID", "NOMBRE"), help="Asignar nombre a una sesión en tags.json")
    parser.add_argument("--tag-device", nargs=2, metavar=("STA_MAC", "ALIAS"), help="Asignar alias a un dispositivo cliente en tags.json")
    parser.add_argument("--tag-net", nargs=2, metavar=("BSSID_O_SSID", "ALIAS"), help="Asignar alias a un BSSID o SSID:Nombre en tags.json")
    args = parser.parse_args(argv)

    files = list(args.files)
    if not files:
        default_dirs = [Path("/home/Kta41/wardrive/wardrive_data"), Path("./wardrive_data")]
        for d in default_dirs:
            if d.is_dir():
                files.extend(sorted(d.glob("wardrive_*.jsonl")))
                if files:
                    break

    tags_data, tags_path = load_tags_file(files)
    tags_modified = False

    if args.tag_session:
        sid, name = args.tag_session
        tags_data["sessions"].setdefault(sid, {})["name"] = name
        tags_modified = True
    if args.tag_device:
        mac, label = args.tag_device
        tags_data["devices"][mac.upper()] = {"label": label, "category": "CONOCIDO", "notes": ""}
        tags_modified = True
    if args.tag_net:
        key, label = args.tag_net
        norm = key if key.startswith("SSID:") else key.upper()
        tags_data["networks"][norm] = {"label": label, "category": "CONOCIDO", "notes": ""}
        tags_modified = True

    if tags_modified:
        tags_path.parent.mkdir(parents=True, exist_ok=True)
        tags_path.write_text(json.dumps(tags_data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Etiquetas guardadas en: {tags_path}")

    if not files:
        print("No se encontraron ficheros wardrive_*.jsonl.", file=sys.stderr)
        return 1 if not tags_modified else 0

    aps, sightings, sessions, total_scans, promisc_data = load_records(files)
    print_report(aps, sightings, sessions, total_scans, promisc_data, tags_data)
    if args.csv:
        export_csv(aps, sightings, promisc_data, args.csv, tags_data)
    if args.wigle:
        export_wigle(aps, promisc_data, args.wigle, tags_data)
    if args.html:
        export_html(aps, sightings, sessions, total_scans, promisc_data, args.html, tags_data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
