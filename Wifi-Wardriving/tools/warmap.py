#!/usr/bin/env python3
"""
warmap.py — Analizador de capturas JSONL de Wardrive.

Muestra un informe completo de las redes capturadas (fabricante OUI, canales,
seguridad, redes abiertas/WEP/TKIP, WPS activo, redes ocultas y ranking RSSI)
y opcionalmente exporta a CSV.
"""

import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import sys

OUI_VENDORS = {
    "00:1A:2B": "Ayecom Technology",
    "00:50:56": "VMware",
    "14:CC:20": "TP-Link",
    "18:D6:C7": "TP-Link",
    "24:A4:3C": "Ubiquiti",
    "34:57:60": "Huawei",
    "44:D9:E7": "Ubiquiti",
    "50:C7:BF": "TP-Link",
    "60:45:E8": "Sagemcom",
    "64:64:4A": "ZTE",
    "78:20:51": "TP-Link",
    "78:81:02": "Sercomm",
    "78:8A:20": "Ubiquiti",
    "80:2A:A8": "Ubiquiti",
    "88:D7:F6": "ASUSTek",
    "9C:5C:8E": "ASUSTek",
    "A4:2B:B0": "TP-Link",
    "B0:BE:76": "TP-Link",
    "C0:25:E9": "TP-Link",
    "CC:2D:E0": "MikroTik",
    "D4:CA:6D": "MikroTik",
    "E4:8D:8C": "MikroTik",
    "F4:F2:6D": "TP-Link",
    "F8:AA:3F": "Huawei",
}


def resolve_vendor(bssid: str) -> str:
    prefix = bssid.upper()[:8]
    if prefix in OUI_VENDORS:
        return OUI_VENDORS[prefix]
    first_byte = int(bssid[:2], 16)
    if first_byte & 0x02:
        return "MAC Local/Virtual"
    return "Desconocido"


def load_records(paths):
    aps = {}
    sightings = Counter()
    sessions = set()
    total_scans = 0
    first_seen = {}
    clients_by_bssid = defaultdict(dict)  # bssid -> {sta_mac: info}
    probes_by_sta = defaultdict(set)      # sta_mac -> {ssid, ...}
    probe_best = {}                       # (sta_mac, ssid) -> info
    promisc_totals = Counter()

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
                payload = obj.get("payload", obj)
                if not isinstance(payload, dict) or payload.get("mode") != "scan":
                    continue
                total_scans += 1
                sessions.add(payload.get("session_id", "?"))
                for dev in payload.get("devices", []):
                    bssid = dev.get("bssid")
                    if not bssid:
                        continue
                    sightings[bssid] += 1
                    if bssid not in first_seen:
                        first_seen[bssid] = recv_at[:19].replace("T", " ")
                    if bssid not in aps or dev.get("rssi", -128) > aps[bssid]["rssi"]:
                        entry = dict(dev)
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
                        sta = cl.get("sta_mac")
                        bssid = cl.get("bssid")
                        if not sta or not bssid:
                            continue
                        prev = clients_by_bssid[bssid].get(sta)
                        if prev is None:
                            clients_by_bssid[bssid][sta] = {
                                "sta_mac": sta,
                                "bssid": bssid,
                                "rssi": cl.get("rssi", -128),
                                "channel": cl.get("channel", 0),
                                "frames": int(cl.get("frames", 1)),
                            }
                        else:
                            prev["frames"] += int(cl.get("frames", 1))
                            if cl.get("rssi", -128) > prev["rssi"]:
                                prev["rssi"] = cl.get("rssi", -128)
                                prev["channel"] = cl.get("channel", 0)

                    for pr in prom.get("probes", []):
                        sta = pr.get("sta_mac")
                        pssid = pr.get("ssid", "")
                        if not sta:
                            continue
                        if pssid:
                            probes_by_sta[sta].add(pssid)
                        key = (sta, pssid)
                        prev = probe_best.get(key)
                        if prev is None:
                            probe_best[key] = {
                                "sta_mac": sta,
                                "ssid": pssid,
                                "rssi": pr.get("rssi", -128),
                                "channel": pr.get("channel", 0),
                                "count": int(pr.get("count", 1)),
                            }
                        else:
                            prev["count"] += int(pr.get("count", 1))
                            if pr.get("rssi", -128) > prev["rssi"]:
                                prev["rssi"] = pr.get("rssi", -128)

    promisc_data = {
        "totals": promisc_totals,
        "clients_by_bssid": clients_by_bssid,
        "probes_by_sta": probes_by_sta,
        "probe_best": probe_best,
        "first_seen": first_seen,
    }
    return aps, sightings, sessions, total_scans, promisc_data


def print_report(aps, sightings, sessions, total_scans, promisc_data):
    print(f"\n\033[1;36m================ WARMAP — RESUMEN DE AUDITORÍA ================\033[0m")
    print(f"Sesiones analizadas : {len(sessions)} ({', '.join(sorted(sessions))})")
    print(f"Escaneos totales    : {total_scans}")
    print(f"Puntos de acceso    : {len(aps)} BSSIDs únicos")
    totals = promisc_data["totals"]
    clients_by_bssid = promisc_data["clients_by_bssid"]
    unique_clients = {sta for stas in clients_by_bssid.values() for sta in stas}
    directed_probes = [v for v in promisc_data["probe_best"].values() if v["ssid"]]
    if totals:
        print(
            f"Tráfico promiscuo   : {totals['mgmt_frames']} MGMT, {totals['data_frames']} DATA, "
            f"{len(unique_clients)} clientes únicos, {len(directed_probes)} SSIDs buscados, "
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

    if unique_clients or directed_probes:
        print(f"\n\033[1;35m[Modo Promiscuo — Clientes y Probe Requests]\033[0m")
        all_clients = [info for stas in clients_by_bssid.values() for info in stas.values()]
        print(f"  • Estaciones clientes activas ({len(all_clients)} asociaciones):")
        for c in sorted(all_clients, key=lambda x: x["frames"], reverse=True)[:12]:
            ap_ssid = aps.get(c["bssid"], {}).get("ssid") or "<AP no balizado>"
            print(
                f"      STA {c['sta_mac']} ({resolve_vendor(c['sta_mac']):<16.16}) -> "
                f"AP {c['bssid']} [{ap_ssid}] (ch {c['channel']:>2}, {c['rssi']:>4}dBm, {c['frames']} tramas)"
            )
        if directed_probes:
            print(f"  • Probe Requests dirigidos (PNL de clientes buscando SSIDs):")
            for pr in sorted(directed_probes, key=lambda x: x["count"], reverse=True)[:12]:
                print(
                    f"      STA {pr['sta_mac']} busca '{pr['ssid']}' "
                    f"(ch {pr['channel']:>2}, {pr['rssi']:>4}dBm, x{pr['count']})"
                )

    print(f"\n\033[1;32m[Catálogo Completo de Redes ({len(aps)} BSSIDs por potencia RSSI)]\033[0m")
    print(f"  {'RSSI':>5}  {'CH':>2}  {'VECES':>5}  {'STAs':>4}  {'WPS':<3}  {'AUTH':<15}  {'CIPHER':<9}  {'BSSID':<17}  {'SSID':<24}  FABRICANTE")
    for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
        bssid = d["bssid"]
        ssid = "<OCULTA>" if d["hidden"] else d["ssid"]
        wps = "SI" if d.get("wps") else "no"
        vendor = resolve_vendor(bssid)
        seen = sightings[bssid]
        n_stas = len(clients_by_bssid.get(bssid, {}))
        print(
            f"  {d['rssi']:>4}dBm  {d['primary_channel']:>2}  {seen:>4}x  {n_stas:>4}  {wps:<3}  "
            f"{d['auth_mode']:<15}  {d['pairwise_cipher']:<9}  {bssid}  {ssid:<24.24}  {vendor}"
        )


def export_csv(aps, sightings, promisc_data, csv_path: Path):
    clients_by_bssid = promisc_data["clients_by_bssid"]
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["bssid", "ssid", "hidden", "rssi_max", "channel", "band_mhz", "proto", "auth_mode", "pairwise_cipher", "group_cipher", "wps", "sightings", "clients_count", "vendor"])
        for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
            bssid = d["bssid"]
            writer.writerow([
                bssid,
                d["ssid"],
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
                len(clients_by_bssid.get(bssid, {})),
                resolve_vendor(bssid),
            ])
    print(f"\nCSV exportado en: {csv_path}")


def export_wigle(aps, promisc_data, wigle_path: Path):
    first_seen = promisc_data["first_seen"]
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
            "MAC",
            "SSID",
            "AuthMode",
            "FirstSeen",
            "Channel",
            "RSSI",
            "CurrentLatitude",
            "CurrentLongitude",
            "AltitudeMeters",
            "AccuracyMeters",
            "Type",
        ])
        for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
            bssid = d["bssid"]
            gps = d.get("gps") or {}
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


def export_html(aps, sightings, sessions, total_scans, promisc_data, html_path: Path):
    import html as html_mod

    clients_by_bssid = promisc_data["clients_by_bssid"]
    directed_probes = sorted(
        [v for v in promisc_data["probe_best"].values() if v["ssid"]],
        key=lambda x: x["count"],
        reverse=True,
    )
    all_clients = sorted(
        [info for stas in clients_by_bssid.values() for info in stas.values()],
        key=lambda x: x["frames"],
        reverse=True,
    )

    ap_rows = []
    for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
        bssid = d["bssid"]
        ssid = "<OCULTA>" if d["hidden"] else html_mod.escape(d["ssid"])
        wps = "SÍ" if d.get("wps") else "no"
        n_stas = len(clients_by_bssid.get(bssid, {}))
        ap_rows.append(
            f"<tr><td>{d['rssi']} dBm</td><td>{d['primary_channel']}</td>"
            f"<td>{sightings[bssid]}</td><td>{n_stas}</td><td>{wps}</td>"
            f"<td>{html_mod.escape(d['auth_mode'])}</td>"
            f"<td>{html_mod.escape(d['pairwise_cipher'])}</td>"
            f"<td><code>{bssid}</code></td><td><b>{ssid}</b></td>"
            f"<td>{html_mod.escape(resolve_vendor(bssid))}</td></tr>"
        )

    client_rows = []
    for c in all_clients:
        ap_ssid = html_mod.escape(aps.get(c["bssid"], {}).get("ssid") or "<AP externo>")
        client_rows.append(
            f"<tr><td><code>{c['sta_mac']}</code></td>"
            f"<td>{html_mod.escape(resolve_vendor(c['sta_mac']))}</td>"
            f"<td><code>{c['bssid']}</code> ({ap_ssid})</td>"
            f"<td>{c['channel']}</td><td>{c['rssi']} dBm</td><td>{c['frames']}</td></tr>"
        )

    probe_rows = []
    for pr in directed_probes:
        probe_rows.append(
            f"<tr><td><code>{pr['sta_mac']}</code></td>"
            f"<td><b>{html_mod.escape(pr['ssid'])}</b></td>"
            f"<td>{pr['channel']}</td><td>{pr['rssi']} dBm</td><td>{pr['count']}</td></tr>"
        )

    doc = f"""<!doctype html>
<html lang="es"><head><meta charset="utf-8">
<title>Wardrive Dashboard — {len(aps)} APs</title>
<style>
body {{ font-family: system-ui, sans-serif; background: #0f172a; color: #e2e8f0; margin: 24px; }}
h1, h2 {{ color: #38bdf8; }}
.cards {{ display: flex; gap: 16px; flex-wrap: wrap; margin-bottom: 20px; }}
.card {{ background: #1e293b; padding: 14px 20px; border-radius: 8px; border: 1px solid #334155; }}
table {{ width: 100%; border-collapse: collapse; margin-bottom: 28px; background: #1e293b; }}
th, td {{ padding: 8px 10px; border-bottom: 1px solid #334155; text-align: left; font-size: 14px; }}
th {{ background: #0284c7; color: #fff; }}
code {{ color: #fde047; }}
</style></head><body>
<h1>📡 Wardrive ESP32-S3 — Informe de Auditoría</h1>
<div class="cards">
  <div class="card"><b>Sesiones:</b> {len(sessions)}</div>
  <div class="card"><b>Escaneos:</b> {total_scans}</div>
  <div class="card"><b>APs únicos:</b> {len(aps)}</div>
  <div class="card"><b>Clientes detectados:</b> {len(all_clients)}</div>
  <div class="card"><b>SSIDs en ProbeReq:</b> {len(directed_probes)}</div>
  <div class="card"><b>Deauth vistas:</b> {promisc_data['totals']['deauth_frames']}</div>
</div>
<h2>Puntos de Acceso ({len(aps)})</h2>
<table><thead><tr><th>RSSI</th><th>CH</th><th>Visto</th><th>Clientes</th><th>WPS</th><th>Auth</th><th>Cipher</th><th>BSSID</th><th>SSID</th><th>Fabricante</th></tr></thead>
<tbody>{''.join(ap_rows)}</tbody></table>
<h2>Estaciones Clientes en Modo Promiscuo ({len(all_clients)})</h2>
<table><thead><tr><th>STA MAC</th><th>Fabricante</th><th>Asociado a BSSID</th><th>CH</th><th>RSSI</th><th>Tramas</th></tr></thead>
<tbody>{''.join(client_rows) or '<tr><td colspan="6">Sin clientes capturados aún</td></tr>'}</tbody></table>
<h2>Probe Requests Dirigidos ({len(directed_probes)})</h2>
<table><thead><tr><th>STA MAC</th><th>SSID Buscado</th><th>CH</th><th>RSSI</th><th>Veces</th></tr></thead>
<tbody>{''.join(probe_rows) or '<tr><td colspan="5">Sin probe requests dirigidos aún</td></tr>'}</tbody></table>
</body></html>"""
    html_path.write_text(doc, encoding="utf-8")
    print(f"\nDashboard HTML exportado en: {html_path}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Analizador de capturas JSONL de Wardrive (warmap)")
    parser.add_argument("files", nargs="*", type=Path, help="Ficheros .jsonl (por defecto busca en ./wardrive_data)")
    parser.add_argument("--csv", type=Path, help="Exportar tabla única de BSSIDs a CSV")
    parser.add_argument("--wigle", type=Path, help="Exportar en formato oficial WiGLE CSV v1.4")
    parser.add_argument("--html", type=Path, help="Exportar dashboard interactivo en HTML")
    args = parser.parse_args(argv)

    files = list(args.files)
    if not files:
        default_dirs = [Path("/home/Kta41/wardrive/wardrive_data"), Path("./wardrive_data")]
        for d in default_dirs:
            if d.is_dir():
                files.extend(sorted(d.glob("wardrive_*.jsonl")))
                if files:
                    break

    if not files:
        print("No se encontraron ficheros wardrive_*.jsonl.", file=sys.stderr)
        return 1

    aps, sightings, sessions, total_scans, promisc_data = load_records(files)
    print_report(aps, sightings, sessions, total_scans, promisc_data)
    if args.csv:
        export_csv(aps, sightings, promisc_data, args.csv)
    if args.wigle:
        export_wigle(aps, promisc_data, args.wigle)
    if args.html:
        export_html(aps, sightings, sessions, total_scans, promisc_data, args.html)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
