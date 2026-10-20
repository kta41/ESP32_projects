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
                    if bssid not in aps or dev.get("rssi", -128) > aps[bssid]["rssi"]:
                        aps[bssid] = dict(dev)
                    elif not aps[bssid].get("ssid") and dev.get("ssid"):
                        aps[bssid]["ssid"] = dev["ssid"]
                        aps[bssid]["hidden"] = False

    return aps, sightings, sessions, total_scans


def print_report(aps, sightings, sessions, total_scans):
    print(f"\n\033[1;36m================ WARMAP — RESUMEN DE AUDITORÍA ================\033[0m")
    print(f"Sesiones analizadas : {len(sessions)} ({', '.join(sorted(sessions))})")
    print(f"Escaneos totales    : {total_scans}")
    print(f"Puntos de acceso    : {len(aps)} BSSIDs únicos\n")

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

    print(f"\n\033[1;32m[Catálogo Completo de Redes ({len(aps)} BSSIDs por potencia RSSI)]\033[0m")
    print(f"  {'RSSI':>5}  {'CH':>2}  {'VECES':>5}  {'WPS':<3}  {'AUTH':<15}  {'CIPHER':<9}  {'BSSID':<17}  {'SSID':<24}  FABRICANTE")
    for d in sorted(aps.values(), key=lambda x: x["rssi"], reverse=True):
        bssid = d["bssid"]
        ssid = "<OCULTA>" if d["hidden"] else d["ssid"]
        wps = "SI" if d.get("wps") else "no"
        vendor = resolve_vendor(bssid)
        seen = sightings[bssid]
        print(
            f"  {d['rssi']:>4}dBm  {d['primary_channel']:>2}  {seen:>4}x  {wps:<3}  "
            f"{d['auth_mode']:<15}  {d['pairwise_cipher']:<9}  {bssid}  {ssid:<24.24}  {vendor}"
        )


def export_csv(aps, sightings, csv_path: Path):
    with csv_path.open("w", newline="", encoding="utf-8") as fh:
        writer = csv.writer(fh)
        writer.writerow(["bssid", "ssid", "hidden", "rssi_max", "channel", "band_mhz", "proto", "auth_mode", "pairwise_cipher", "group_cipher", "wps", "sightings", "vendor"])
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
                resolve_vendor(bssid),
            ])
    print(f"\nCSV exportado en: {csv_path}")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Analizador de capturas JSONL de Wardrive (warmap)")
    parser.add_argument("files", nargs="*", type=Path, help="Ficheros .jsonl (por defecto busca en ./wardrive_data)")
    parser.add_argument("--csv", type=Path, help="Exportar tabla única de BSSIDs a CSV")
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

    aps, sightings, sessions, total_scans = load_records(files)
    print_report(aps, sightings, sessions, total_scans)
    if args.csv:
        export_csv(aps, sightings, args.csv)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
