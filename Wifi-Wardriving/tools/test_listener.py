import json
from http.server import ThreadingHTTPServer
from pathlib import Path
import tempfile
from threading import Thread
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from listener import ListenerState, make_handler
from validate import validate_file


def make_scan(scan_id=1, session_id="9f3a21c4", lost_before=0, buffered=False):
    return {
        "v": 1,
        "mode": "scan",
        "scan_type": "passive",
        "session_id": session_id,
        "scan_id": scan_id,
        "device_ts_us": 481234567 + scan_id * 15000000,
        "scan_duration_ms": 4200,
        "total_detected": 1,
        "stored": 1,
        "dropped": 0,
        "lost_before": lost_before,
        "buffered": buffered,
        "devices": [
            {
                "ssid": "MiRouter",
                "bssid": "AA:BB:CC:DD:EE:FF",
                "rssi": -58,
                "primary_channel": 6,
                "secondary_channel": 0,
                "auth_mode": "WPA2_WPA3_PSK",
                "pairwise_cipher": "CCMP",
                "group_cipher": "CCMP",
                "proto": "bgn",
                "band_mhz": 20,
                "wps": False,
                "hidden": False,
            }
        ],
    }


class ListenerAndValidatorTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.out_dir = Path(self.tempdir.name)
        self.state = ListenerState(out_dir=self.out_dir, gps_enabled=False)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.state))
        self.thread = Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tempdir.cleanup()

    def post_ndjson(self, lines):
        body = "\n".join(lines) + "\n"
        req = Request(
            f"{self.url}/scans",
            data=body.encode("utf-8"),
            headers={"Content-Type": "application/x-ndjson"},
        )
        return urlopen(req)

    def test_ingest_batch_dedup_and_validate(self):
        s1 = json.dumps(make_scan(1))
        s2 = json.dumps(make_scan(2))
        with self.post_ndjson([s1, s2]) as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read())
            self.assertEqual(data, {"accepted": 2, "last_scan_id": 2})

        # Retransmisión de s2 + nuevo s3: dedup cuenta 2 aceptados pero solo escribe 1 nuevo
        s3 = json.dumps(make_scan(3))
        with self.post_ndjson([s2, s3]) as resp:
            data = json.loads(resp.read())
            self.assertEqual(data, {"accepted": 2, "last_scan_id": 3})

        with urlopen(f"{self.url}/health") as resp:
            health = json.loads(resp.read())
            self.assertEqual(health["lines"], 3)
            self.assertEqual(health["sessions"], ["9f3a21c4"])

        files = list(self.out_dir.glob("*.jsonl"))
        self.assertEqual(len(files), 1)
        errors, summary = validate_file(files[0])
        self.assertEqual(errors, [])
        self.assertEqual(summary["9f3a21c4"]["scans"], 3)

    def test_partial_batch_with_bad_line_reports_first_bad_scan_id(self):
        good = json.dumps(make_scan(1))
        bad_obj = make_scan(2)
        bad_obj["devices"][0]["rssi"] = 999  # inválido
        bad = json.dumps(bad_obj)

        with self.assertRaises(HTTPError) as ctx:
            self.post_ndjson([good, bad])
        self.assertEqual(ctx.exception.code, 400)
        err_body = json.loads(ctx.exception.read())
        self.assertEqual(err_body["first_bad_scan_id"], 2)
        self.assertEqual(err_body["accepted"], 1)


if __name__ == "__main__":
    unittest.main()
