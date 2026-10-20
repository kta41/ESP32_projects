#!/usr/bin/env python3
"""
validate.py — Validador oficial de ficheros JSONL de Wardrive (ROADMAP §3.10).

Acepta tanto líneas envueltas por `listener.py` (`{"recv_at", "gps", "payload"}`)
como líneas crudas del esquema v1 (`{"v": 1, "mode": "scan", ...}`).
"""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys

from listener import validate_scan_payload


def validate_file(path: Path):
    errors = []
    seen = set()
    by_session = defaultdict(list)

    if not path.exists():
        return [f"{path}: fichero no existe"], {}

    with path.open("r", encoding="utf-8") as fh:
        for lineno, raw in enumerate(fh, start=1):
            line = raw.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except Exception as exc:
                errors.append(f"{path}:{lineno}: JSON inválido: {exc}")
                continue

            if isinstance(obj, dict) and "payload" in obj and "recv_at" in obj:
                if not isinstance(obj["recv_at"], str):
                    errors.append(f"{path}:{lineno}: recv_at inválido")
                gps = obj.get("gps")
                if gps is not None:
                    if not isinstance(gps, dict) or "lat" not in gps or "lon" not in gps:
                        errors.append(f"{path}:{lineno}: estructura gps inválida")
                payload = obj["payload"]
            else:
                payload = obj

            try:
                validate_scan_payload(payload)
            except Exception as exc:
                errors.append(f"{path}:{lineno}: {exc}")
                continue

            sid = payload["session_id"]
            scan_id = payload["scan_id"]
            key = (sid, scan_id)
            if key in seen:
                errors.append(f"{path}:{lineno}: duplicado ({sid}, scan_id={scan_id})")
                continue
            seen.add(key)
            by_session[sid].append((lineno, payload))

    summary = {}
    for sid, items in by_session.items():
        prev_scan_id = None
        prev_lost = 0
        total_aps = 0
        for lineno, p in items:
            scan_id = p["scan_id"]
            lost = p["lost_before"]
            total_aps += p["stored"]
            if prev_scan_id is not None:
                if scan_id <= prev_scan_id:
                    errors.append(
                        f"{path}:{lineno}: scan_id no ascendente en sesión {sid} ({prev_scan_id} -> {scan_id})"
                    )
                else:
                    gap = scan_id - prev_scan_id - 1
                    lost_delta = lost - prev_lost
                    if gap > 0 and lost_delta < gap:
                        errors.append(
                            f"{path}:{lineno}: hueco de {gap} scan_id en sesión {sid} "
                            f"no justificado por lost_before ({prev_lost} -> {lost})"
                        )
            prev_scan_id = scan_id
            prev_lost = lost

        summary[sid] = {
            "scans": len(items),
            "first_scan_id": items[0][1]["scan_id"],
            "last_scan_id": items[-1][1]["scan_id"],
            "lost_before_max": items[-1][1]["lost_before"],
            "total_stored_aps": total_aps,
        }

    return errors, summary


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Validador de ficheros JSONL de Wardrive")
    parser.add_argument("files", nargs="+", type=Path, help="Rutas a ficheros .jsonl")
    args = parser.parse_args(argv)

    all_errors = []
    for path in args.files:
        errs, summary = validate_file(path)
        all_errors.extend(errs)
        print(f"=== {path} ===")
        for sid, info in summary.items():
            print(
                f"  sesión {sid}: {info['scans']} escaneos "
                f"(scan_id {info['first_scan_id']}..{info['last_scan_id']}, "
                f"lost_before={info['lost_before_max']}, APs={info['total_stored_aps']})"
            )
    if all_errors:
        print("\nERRORES DE VALIDACIÓN:", file=sys.stderr)
        for err in all_errors:
            print(f"  - {err}", file=sys.stderr)
        return 1

    print("OK: JSONL 100 % válido.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
