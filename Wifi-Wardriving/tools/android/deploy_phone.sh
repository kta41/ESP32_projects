#!/usr/bin/env bash
# Despliega las herramientas de Wardrive (listener, warmap, validate y atajos
# warwifi / warmap / warcheck) en el móvil Android conectado por ADB/SSH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ADB_BIN="${ADB_BIN:-$HOME/.local/bin/adb}"
SSH_PORT="${SSH_PORT:-8022}"
TERMUX_USER="${TERMUX_USER:-$(ssh -p "$SSH_PORT" 127.0.0.1 whoami 2>/dev/null || echo termux)}"

"$ADB_BIN" forward "tcp:${SSH_PORT}" "tcp:${SSH_PORT}" >/dev/null

scp -P "$SSH_PORT" \
    "$TOOLS_DIR/listener.py" \
    "$TOOLS_DIR/validate.py" \
    "$TOOLS_DIR/warmap.py" \
    "$TOOLS_DIR/dashboard_html.py" \
    "$TOOLS_DIR/oui_es_db.json.gz" \
    "${TERMUX_USER}@127.0.0.1:~/kali-arm64/home/Kta41/wardrive/"

scp -P "$SSH_PORT" \
    "$SCRIPT_DIR/warwifi" \
    "$SCRIPT_DIR/warmap" \
    "$SCRIPT_DIR/warcheck" \
    "${TERMUX_USER}@127.0.0.1:/data/data/com.termux/files/usr/bin/"

ssh -p "$SSH_PORT" "${TERMUX_USER}@127.0.0.1" '
chmod 755 $PREFIX/bin/warwifi $PREFIX/bin/warmap $PREFIX/bin/warcheck
chmod 755 ~/kali-arm64/home/Kta41/wardrive/*.py
cp ~/kali-arm64/home/Kta41/wardrive/*.py ~/kali-arm64/home/Kta41/wardrive/oui_es_db.json.gz ~/wardrive/

cat > ~/kali-arm64/usr/local/bin/warwifi << "EOF"
#!/bin/bash
exec python3 /home/Kta41/wardrive/listener.py --port 47471 --out /home/Kta41/wardrive/wardrive_data "$@"
EOF

cat > ~/kali-arm64/usr/local/bin/warmap << "EOF"
#!/bin/bash
exec python3 /home/Kta41/wardrive/warmap.py "$@"
EOF

cat > ~/kali-arm64/usr/local/bin/warcheck << "EOF"
#!/bin/bash
exec python3 /home/Kta41/wardrive/validate.py /home/Kta41/wardrive/wardrive_data/wardrive_*.jsonl
EOF

chmod 755 ~/kali-arm64/usr/local/bin/warwifi ~/kali-arm64/usr/local/bin/warmap ~/kali-arm64/usr/local/bin/warcheck
nh -r "chown -R Kta41:Kta41 /home/Kta41/wardrive"
'
echo "Despliegue completado: warwifi, warmap y warcheck actualizados en Termux y Kali."
