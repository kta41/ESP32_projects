#!/usr/bin/env python3
"""
dashboard_html.py — Generador de Consola SIGINT / Auditoría Corporativa (HTML autocontenido).

Genera una Single-Page Application (SPA) interactiva 100 % offline con:
- Pestañas: Resumen Ejecutivo, Inteligencia de Redes (APs/SSIDs), Inteligencia de
  Dispositivos (STAs + PNL Probe Requests), Persistencia Multi-Sesión, Sesiones & GPS
  y Gestor de Etiquetas (tags.json).
- Dossiers interactivos clicables para cada Red (BSSID/SSID), Dispositivo (STA) y Sesión.
- Etiquetado en vivo (nombres custom + categoría) para Sesiones, Redes y Dispositivos,
   más asignación de coordenadas GPS por sesión, persistido en localStorage y exportable
  / importable como `tags.json`.
"""

import json
from pathlib import Path


def render_sigint_dashboard(dataset: dict, initial_tags: dict, html_path: Path) -> None:
    dataset_json = json.dumps(dataset, ensure_ascii=False, separators=(",", ":"))
    tags_json = json.dumps(initial_tags, ensure_ascii=False, separators=(",", ":"))

    doc = f"""<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SIGINT Wireless Intelligence Console — Wardrive ESP32-S3</title>
<style>
:root {{
  --bg: #070b14;
  --bg-panel: #0f172a;
  --bg-card: #131c31;
  --bg-hover: #1e293b;
  --border: #1e293b;
  --border-hi: #334155;
  --text: #e2e8f0;
  --muted: #94a3b8;
  --accent: #38bdf8;
  --accent-dim: rgba(56,189,248,0.14);
  --green: #10b981;
  --amber: #f59e0b;
  --red: #ef4444;
  --purple: #a855f7;
  --font-mono: 'JetBrains Mono', 'Fira Code', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}}
* {{ box-sizing: border-box; }}
body {{
  margin: 0;
  font-family: 'Inter', system-ui, -apple-system, sans-serif;
  background: var(--bg);
  color: var(--text);
  font-size: 13.5px;
  line-height: 1.45;
}}
header {{
  background: linear-gradient(180deg, #0f172a 0%, #090e1a 100%);
  border-bottom: 1px solid var(--border-hi);
  padding: 14px 24px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  flex-wrap: wrap;
  position: sticky;
  top: 0;
  z-index: 50;
}}
.brand {{
  display: flex;
  align-items: center;
  gap: 12px;
}}
.brand-badge {{
  background: var(--accent-dim);
  border: 1px solid var(--accent);
  color: var(--accent);
  font-family: var(--font-mono);
  font-weight: 700;
  font-size: 11px;
  padding: 4px 8px;
  border-radius: 4px;
  letter-spacing: 0.08em;
}}
.brand h1 {{
  margin: 0;
  font-size: 17px;
  font-weight: 700;
  letter-spacing: -0.01em;
  color: #f8fafc;
}}
.brand-sub {{
  color: var(--muted);
  font-size: 12px;
  font-family: var(--font-mono);
}}
.top-actions {{
  display: flex;
  gap: 8px;
  align-items: center;
  flex-wrap: wrap;
}}
button, .btn {{
  background: var(--bg-card);
  color: var(--text);
  border: 1px solid var(--border-hi);
  padding: 6px 12px;
  border-radius: 6px;
  font-size: 12.5px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.15s ease;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}}
button:hover, .btn:hover {{
  border-color: var(--accent);
  background: var(--bg-hover);
}}
.btn-primary {{
  background: #0284c7;
  border-color: #38bdf8;
  color: #fff;
}}
.btn-primary:hover {{
  background: #0369a1;
}}
nav.tabs {{
  display: flex;
  gap: 4px;
  padding: 0 24px;
  background: #090e1a;
  border-bottom: 1px solid var(--border-hi);
  overflow-x: auto;
}}
.tab-btn {{
  background: transparent;
  border: none;
  border-bottom: 2px solid transparent;
  border-radius: 0;
  padding: 11px 16px;
  color: var(--muted);
  font-weight: 600;
  font-size: 13px;
}}
.tab-btn:hover {{
  color: #fff;
  background: rgba(255,255,255,0.02);
}}
.tab-btn.active {{
  color: var(--accent);
  border-bottom-color: var(--accent);
  background: rgba(56,189,248,0.06);
}}
main {{
  padding: 20px 24px 60px;
  max-width: 1680px;
  margin: 0 auto;
}}
.tab-pane {{ display: none; }}
.tab-pane.active {{ display: block; }}
.kpi-grid {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(185px, 1fr));
  gap: 12px;
  margin-bottom: 20px;
}}
.kpi {{
  background: var(--bg-panel);
  border: 1px solid var(--border-hi);
  border-radius: 8px;
  padding: 14px 16px;
}}
.kpi-label {{
  color: var(--muted);
  font-size: 11.5px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  margin-bottom: 6px;
}}
.kpi-val {{
  font-size: 24px;
  font-weight: 700;
  font-family: var(--font-mono);
  color: #f8fafc;
}}
.kpi-sub {{
  color: var(--muted);
  font-size: 11.5px;
  margin-top: 4px;
}}
.grid-2 {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(460px, 1fr));
  gap: 16px;
  margin-bottom: 20px;
}}
.panel {{
  background: var(--bg-panel);
  border: 1px solid var(--border-hi);
  border-radius: 8px;
  padding: 16px 18px;
  margin-bottom: 18px;
}}
.panel-title {{
  margin: 0 0 12px 0;
  font-size: 14px;
  font-weight: 700;
  color: #f8fafc;
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}}
.toolbar {{
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
  align-items: center;
  margin-bottom: 12px;
}}
input[type="text"], input[type="number"], select, textarea {{
  background: #080d19;
  color: var(--text);
  border: 1px solid var(--border-hi);
  border-radius: 6px;
  padding: 7px 11px;
  font-size: 13px;
}}
input[type="text"]:focus, select:focus, textarea:focus {{
  outline: none;
  border-color: var(--accent);
}}
.chip-group {{
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
}}
.chip {{
  font-size: 11.5px;
  padding: 4px 10px;
  border-radius: 999px;
  border: 1px solid var(--border-hi);
  background: var(--bg-card);
  color: var(--muted);
  cursor: pointer;
}}
.chip.active {{
  background: var(--accent-dim);
  border-color: var(--accent);
  color: var(--accent);
  font-weight: 600;
}}
.table-wrap {{
  overflow-x: auto;
  max-height: 68vh;
  overflow-y: auto;
  border: 1px solid var(--border);
  border-radius: 6px;
}}
table {{
  width: 100%;
  border-collapse: collapse;
  font-size: 12.8px;
}}
th, td {{
  padding: 9px 10px;
  border-bottom: 1px solid var(--border);
  text-align: left;
  vertical-align: middle;
}}
th {{
  background: #0b1120;
  color: var(--muted);
  font-weight: 600;
  font-size: 11.5px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  cursor: pointer;
  user-select: none;
  position: sticky;
  top: 0;
  z-index: 10;
  box-shadow: 0 1px 0 var(--border-hi);
}}
tr:hover td {{
  background: rgba(56,189,248,0.04);
}}
code, .mono {{
  font-family: var(--font-mono);
  font-size: 12px;
}}
.entity-link {{
  color: var(--accent);
  cursor: pointer;
  text-decoration: none;
  font-weight: 600;
}}
.entity-link:hover {{
  text-decoration: underline;
}}
.badge {{
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 7px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
  font-family: var(--font-mono);
  border: 1px solid transparent;
}}
.b-own {{ background: rgba(16,185,129,0.16); color: #34d399; border-color: rgba(16,185,129,0.4); }}
.b-known {{ background: rgba(56,189,248,0.16); color: #38bdf8; border-color: rgba(56,189,248,0.4); }}
.b-target {{ background: rgba(245,158,11,0.18); color: #fbbf24; border-color: rgba(245,158,11,0.45); }}
.b-suspicious {{ background: rgba(239,68,68,0.18); color: #f87171; border-color: rgba(239,68,68,0.45); }}
.b-multi {{ background: rgba(168,85,247,0.18); color: #c084fc; border-color: rgba(168,85,247,0.45); }}
.b-neutral {{ background: #1e293b; color: #cbd5e1; border-color: #334155; }}
.rssi-bar {{
  display: inline-block;
  width: 54px;
  height: 6px;
  background: #1e293b;
  border-radius: 3px;
  overflow: hidden;
  vertical-align: middle;
  margin-right: 6px;
}}
.rssi-fill {{ height: 100%; }}
.bar-row {{
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 6px;
  font-size: 12px;
}}
.bar-label {{ width: 90px; color: var(--muted); font-family: var(--font-mono); }}
.bar-track {{ flex: 1; height: 10px; background: #090e1a; border-radius: 4px; overflow: hidden; }}
.bar-fill {{ height: 100%; background: linear-gradient(90deg, #0284c7, #38bdf8); }}
.bar-count {{ width: 48px; text-align: right; font-family: var(--font-mono); }}
/* Modal Dossier / Editor de Tags */
.modal-backdrop {{
  position: fixed;
  inset: 0;
  background: rgba(3,7,18,0.78);
  display: none;
  align-items: center;
  justify-content: center;
  z-index: 200;
  padding: 20px;
}}
.modal-backdrop.open {{ display: flex; }}
.modal {{
  background: var(--bg-panel);
  border: 1px solid var(--border-hi);
  border-radius: 10px;
  max-width: 880px;
  width: 100%;
  max-height: 88vh;
  overflow-y: auto;
  padding: 20px 24px;
  box-shadow: 0 24px 48px rgba(0,0,0,0.6);
}}
.modal-header {{
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  border-bottom: 1px solid var(--border-hi);
  padding-bottom: 12px;
  margin-bottom: 16px;
}}
.kv-grid {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 10px;
  margin-bottom: 16px;
}}
.kv {{
  background: var(--bg-card);
  padding: 10px 12px;
  border-radius: 6px;
  border: 1px solid var(--border);
}}
.kv-k {{ color: var(--muted); font-size: 11px; text-transform: uppercase; }}
.kv-v {{ font-family: var(--font-mono); font-size: 13px; font-weight: 600; margin-top: 2px; }}
@media print {{
  header, nav.tabs, .toolbar, .no-print {{ display: none !important; }}
  .tab-pane {{ display: block !important; page-break-after: always; }}
  body {{ background: #fff; color: #000; }}
  .panel, .kpi {{ border: 1px solid #ccc; background: #fff; }}
}}
</style>
</head>
<body>

<header>
  <div class="brand">
    <span class="brand-badge">SIGINT // 802.11</span>
    <div>
      <h1>Wireless Intelligence &amp; Correlation Console</h1>
      <div class="brand-sub" id="hdr-summary">Cargando telemetría...</div>
    </div>
  </div>
  <div class="top-actions no-print">
    <button onclick="openTagManagerModal()">🏷️ Nuevo Tag / Alias</button>
    <button onclick="exportTagsJson()" class="btn-primary">💾 Exportar tags.json</button>
    <label class="btn">📂 Importar tags.json <input type="file" accept=".json" style="display:none" onchange="importTagsJson(event)"></label>
    <button onclick="window.print()">🖨️ Informe PDF</button>
  </div>
</header>

<nav class="tabs no-print">
  <button class="tab-btn active" onclick="switchTab('overview', this)">1. Resumen Ejecutivo &amp; Riesgos</button>
  <button class="tab-btn" onclick="switchTab('networks', this)">2. Inteligencia de Redes (APs / SSIDs)</button>
  <button class="tab-btn" onclick="switchTab('devices', this)">3. Inteligencia de Dispositivos (STAs &amp; PNL)</button>
  <button class="tab-btn" onclick="switchTab('cross', this)">4. Cruce Multi-Sesión (Persistencia)</button>
  <button class="tab-btn" onclick="switchTab('sessions', this)">5. Sesiones &amp; GPS</button>
  <button class="tab-btn" onclick="switchTab('tags', this)">6. Registro de Identidades (Tags)</button>
</nav>

<main>
  <!-- TAB 1: OVERVIEW -->
  <section id="tab-overview" class="tab-pane active">
    <div class="kpi-grid" id="kpi-container"></div>
    <div class="grid-2">
      <div class="panel">
        <h3 class="panel-title">Ocupación del Espectro 2,4 GHz (Canales 1–14)</h3>
        <div id="chart-channels"></div>
      </div>
      <div class="panel">
        <h3 class="panel-title">Postura Criptográfica &amp; Superficie de Exposición</h3>
        <div id="chart-security"></div>
      </div>
    </div>
    <div class="panel">
      <h3 class="panel-title">Hallazgos de Auditoría &amp; Correlación Automática (Audit Findings)</h3>
      <div id="findings-list"></div>
    </div>
  </section>

  <!-- TAB 2: NETWORKS -->
  <section id="tab-networks" class="tab-pane">
    <div class="panel">
      <h3 class="panel-title">
        <span>Catálogo de Redes e Infraestructura (BSSIDs / Clusters SSID)</span>
        <span class="mono" id="net-count-label"></span>
      </h3>
      <div class="toolbar">
        <input type="text" id="net-search" placeholder="Filtrar por SSID, BSSID, Tag, Fabricante, Canal..." style="min-width:290px" oninput="renderNetworksTable()">
        <select id="net-session-filter" onchange="renderNetworksTable()">
          <option value="">Todas las sesiones</option>
        </select>
        <div class="chip-group" id="net-chips">
          <span class="chip active" onclick="setNetFilter('all', this)">Todas</span>
          <span class="chip" onclick="setNetFilter('insecure', this)" style="border-color:rgba(239,68,68,0.45);color:#f87171">⚠️ Redes No Seguras (OPEN/WEP/TKIP/WPS)</span>
          <span class="chip" onclick="setNetFilter('weak', this)">Cifrado Débil (OPEN/WEP/TKIP)</span>
          <span class="chip" onclick="setNetFilter('wps', this)">WPS Expuesto</span>
          <span class="chip" onclick="setNetFilter('multi', this)">Multi-Sesión (&ge;2)</span>
          <span class="chip" onclick="setNetFilter('clients', this)">Con Clientes Activos</span>
          <span class="chip" onclick="setNetFilter('probed', this)">Buscadas por STAs</span>
          <span class="chip" onclick="setNetFilter('hidden', this)">SSID Oculto</span>
          <span class="chip" onclick="setNetFilter('tagged', this)">Etiquetadas</span>
        </div>
      </div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>RSSI Máx</th>
              <th>CH</th>
              <th>SSID / Cluster</th>
              <th>BSSID</th>
              <th>Tag / Alias</th>
              <th>Seguridad</th>
              <th>WPS</th>
              <th>Clientes (STAs)</th>
              <th>Buscada por</th>
              <th>Sesiones</th>
              <th>Fabricante OUI</th>
              <th class="no-print">Acción</th>
            </tr>
          </thead>
          <tbody id="net-tbody"></tbody>
        </table>
      </div>
    </div>
  </section>

  <!-- TAB 3: DEVICES (STAs & PNL) -->
  <section id="tab-devices" class="tab-pane">
    <div class="panel">
      <h3 class="panel-title">
        <span>Inteligencia de Dispositivos Clientes (Asociaciones + Huella PNL de Redes Buscadas)</span>
        <span class="mono" id="dev-count-label"></span>
      </h3>
      <div class="toolbar">
        <input type="text" id="dev-search" placeholder="Buscar por STA MAC, Tag, SSID buscado, AP conectado, OUI..." style="min-width:320px" oninput="renderDevicesTable()">
        <div class="chip-group" id="dev-chips">
          <span class="chip active" onclick="setDevFilter('all', this)">Todos</span>
          <span class="chip" onclick="setDevFilter('insecure_ap', this)" style="border-color:rgba(239,68,68,0.45);color:#f87171">⚠️ En Red No Segura</span>
          <span class="chip" onclick="setDevFilter('multi', this)">Recurrentes (&ge;2 sesiones)</span>
          <span class="chip" onclick="setDevFilter('associated', this)">Asociados a un AP</span>
          <span class="chip" onclick="setDevFilter('probing', this)">Buscando Redes (PNL)</span>
          <span class="chip" onclick="setDevFilter('matched', this)">Busca Red Presente Aquí</span>
          <span class="chip" onclick="setDevFilter('global_mac', this)">MAC Real IEEE (No aleatoria)</span>
          <span class="chip" onclick="setDevFilter('tagged', this)">Etiquetados</span>
        </div>
      </div>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>RSSI</th>
              <th>Dispositivo (STA MAC)</th>
              <th>Tag / Alias</th>
              <th>Tipo MAC / Fabricante</th>
              <th>Asociado a AP (BSSID / SSID)</th>
              <th>Redes que Busca (Probe Requests PNL)</th>
              <th>Tramas</th>
              <th>Sesiones Visto</th>
              <th class="no-print">Acción</th>
            </tr>
          </thead>
          <tbody id="dev-tbody"></tbody>
        </table>
      </div>
    </div>
  </section>

  <!-- TAB 4: CROSS-SESSION PERSISTENCE -->
  <section id="tab-cross" class="tab-pane">
    <div class="panel">
      <h3 class="panel-title">Correlación Multi-Sesión — Dispositivos y Redes Recurrentes</h3>
      <p style="color:var(--muted);margin-top:0">
        Identifica qué dispositivos (<code>STA MAC</code>) o puntos de acceso (<code>BSSID</code>) han aparecido en múltiples sesiones distintas, o qué dispositivos con MAC rotada comparten la misma huella de redes buscadas (PNL).
      </p>
      <div id="cross-content"></div>
    </div>
  </section>

  <!-- TAB 5: SESSIONS & GPS -->
  <section id="tab-sessions" class="tab-pane">
    <div class="panel">
      <h3 class="panel-title">Gestión de Sesiones de Captura &amp; Geolocalización (GPS Vinculado a la Sesión)</h3>
      <p style="color:var(--muted);margin-top:0">
        El GPS se asocia a nivel de <b>Sesión</b>. Desde aquí puedes nombrar cada sesión (ej. <i>"Auditoría Planta 2"</i>, <i>"Paseo Centro"</i>) y asignarle o corregir sus coordenadas GPS.
      </p>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Session ID</th>
              <th>Nombre de Sesión (Tag)</th>
              <th>Fichero Origen</th>
              <th>Inicio → Fin</th>
              <th>Escaneos</th>
              <th>APs Únicos</th>
              <th>Clientes (STAs)</th>
              <th>GPS de la Sesión (Lat, Lon)</th>
              <th class="no-print">Editar</th>
            </tr>
          </thead>
          <tbody id="sessions-tbody"></tbody>
        </table>
      </div>
    </div>
  </section>

  <!-- TAB 6: TAGS REGISTRY -->
  <section id="tab-tags" class="tab-pane">
    <div class="panel">
      <h3 class="panel-title">
        <span>Registro Maestro de Identidades, Nombres Custom y Etiquetas (<code>tags.json</code>)</span>
        <button class="btn-primary" onclick="exportTagsJson()">💾 Descargar tags.json</button>
      </h3>
      <p style="color:var(--muted);margin-top:0">
        Guarda el archivo descargado como <code>wardrive_data/tags.json</code> para que <code>warmap</code> y <code>warpull</code> lo incrusten automáticamente en futuros informes y CSVs.
      </p>
      <div id="tags-registry-content"></div>
    </div>
  </section>
</main>

<!-- MODAL DOSSIER / TAG EDITOR -->
<div class="modal-backdrop" id="modal-backdrop" onclick="if(event.target===this)closeModal()">
  <div class="modal" id="modal-body"></div>
</div>

<script>
const DATA = {dataset_json};
const INITIAL_TAGS = {tags_json};
const STORAGE_KEY = "wardrive_sigint_tags_v1";

let TAGS = loadTags();
let netFilter = "all";
let devFilter = "all";

function loadTags() {{
  const base = {{
    sessions: Object.assign({{}}, INITIAL_TAGS.sessions || {{}}),
    networks: Object.assign({{}}, INITIAL_TAGS.networks || {{}}),
    devices: Object.assign({{}}, INITIAL_TAGS.devices || {{}})
  }};
  try {{
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) {{
      const saved = JSON.parse(raw);
      Object.assign(base.sessions, saved.sessions || {{}});
      Object.assign(base.networks, saved.networks || {{}});
      Object.assign(base.devices, saved.devices || {{}});
    }}
  }} catch (e) {{}}
  return base;
}}

function saveTags() {{
  try {{
    localStorage.setItem(STORAGE_KEY, JSON.stringify(TAGS));
  }} catch (e) {{}}
  renderAll();
}}

function esc(s) {{
  if (s === null || s === undefined) return "";
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}}

function catBadgeClass(cat) {{
  switch ((cat || "").toUpperCase()) {{
    case "PROPIO": return "b-own";
    case "CONOCIDO": return "b-known";
    case "OBJETIVO": return "b-target";
    case "SOSPECHOSO": return "b-suspicious";
    default: return "b-neutral";
  }}
}}

function getSessionTag(sid) {{
  return TAGS.sessions[sid] || {{}};
}}

function getSessionDisplay(sid) {{
  const t = getSessionTag(sid);
  return t.name ? `${{t.name}} (${{sid}})` : sid;
}}

function getNetworkTag(bssid, ssid) {{
  if (bssid && TAGS.networks[bssid.toUpperCase()]) return TAGS.networks[bssid.toUpperCase()];
  if (ssid && TAGS.networks["SSID:" + ssid]) return TAGS.networks["SSID:" + ssid];
  return null;
}}

function getDeviceTag(mac) {{
  if (!mac) return null;
  return TAGS.devices[mac.toUpperCase()] || null;
}}

function renderTagBadge(tagObj) {{
  if (!tagObj || !tagObj.label) return '<span style="color:var(--muted)">—</span>';
  const cls = catBadgeClass(tagObj.category);
  const cat = tagObj.category ? `[${{esc(tagObj.category)}}] ` : "";
  return `<span class="badge ${{cls}}">${{cat}}${{esc(tagObj.label)}}</span>`;
}}

function rssiBar(rssi) {{
  const pct = Math.max(5, Math.min(100, Math.round(((rssi + 100) / 70) * 100)));
  const color = rssi >= -55 ? "#10b981" : (rssi >= -75 ? "#f59e0b" : "#64748b");
  return `<span class="rssi-bar"><span class="rssi-fill" style="width:${{pct}}%;background:${{color}}"></span></span><span class="mono">${{rssi}} dBm</span>`;
}}

function switchTab(id, btn) {{
  document.querySelectorAll(".tab-pane").forEach(el => el.classList.remove("active"));
  document.querySelectorAll(".tab-btn").forEach(el => el.classList.remove("active"));
  document.getElementById("tab-" + id).classList.add("active");
  if (btn) btn.classList.add("active");
}}

function renderOverview() {{
  const sCount = DATA.sessions.length;
  const apCount = DATA.aps.length;
  const devCount = DATA.devices.length;
  const multiDevs = DATA.devices.filter(d => d.sessions.length >= 2).length;
  const multiAps = DATA.aps.filter(a => a.sessions.length >= 2).length;
  const weakAps = DATA.aps.filter(a => a.is_weak).length;
  const wpsAps = DATA.aps.filter(a => a.wps).length;

  // Calcular postura de riesgo (0-100)
  const riskScore = Math.min(100, Math.round(
    (weakAps * 6) + (wpsAps * 1.5) + (DATA.totals.deauth_frames * 5) + (DATA.ssid_collisions.length * 4)
  ));
  const riskColor = riskScore >= 60 ? "var(--red)" : (riskScore >= 30 ? "var(--amber)" : "var(--green)");

  document.getElementById("hdr-summary").textContent =
    `${{sCount}} sesiones | ${{DATA.total_scans}} barridos | ${{apCount}} BSSIDs | ${{devCount}} dispositivos (STAs) | Generado: ${{DATA.generated_at}}`;

  document.getElementById("kpi-container").innerHTML = `
    <div class="kpi">
      <div class="kpi-label">Índice de Exposición RF</div>
      <div class="kpi-val" style="color:${{riskColor}}">${{riskScore}} / 100</div>
      <div class="kpi-sub">${{weakAps}} redes débiles · ${{wpsAps}} con WPS</div>
    </div>
    <div class="kpi">
      <div class="kpi-label">Infraestructura (BSSIDs)</div>
      <div class="kpi-val">${{apCount}}</div>
      <div class="kpi-sub">${{DATA.unique_ssids}} SSIDs únicos · ${{multiAps}} en &ge;2 sesiones</div>
    </div>
    <div class="kpi">
      <div class="kpi-label">Dispositivos (STAs)</div>
      <div class="kpi-val">${{devCount}}</div>
      <div class="kpi-sub">${{multiDevs}} vistos en múltiples sesiones</div>
    </div>
    <div class="kpi">
      <div class="kpi-label">Redes Buscadas (PNL)</div>
      <div class="kpi-val">${{DATA.probed_ssids_count}}</div>
      <div class="kpi-sub">${{DATA.matched_probes_count}} coinciden con APs presentes</div>
    </div>
    <div class="kpi">
      <div class="kpi-label">Sesiones Auditadas</div>
      <div class="kpi-val">${{sCount}}</div>
      <div class="kpi-sub">${{DATA.total_scans}} escaneos pasivos + promiscuos</div>
    </div>
    <div class="kpi">
      <div class="kpi-label">Tramas 802.11 / Deauth</div>
      <div class="kpi-val">${{DATA.totals.mgmt_frames + DATA.totals.data_frames}}</div>
      <div class="kpi-sub">Deauth/Disassoc vistas: <b style="color:${{DATA.totals.deauth_frames ? 'var(--red)' : 'var(--green)'}}">${{DATA.totals.deauth_frames}}</b></div>
    </div>
  `;

  // Canales
  const maxCh = Math.max(1, ...Object.values(DATA.channel_counts));
  let chHtml = "";
  for (let ch = 1; ch <= 13; ch++) {{
    const cnt = DATA.channel_counts[ch] || 0;
    const pct = Math.round((cnt / maxCh) * 100);
    chHtml += `
      <div class="bar-row">
        <div class="bar-label">Canal ${{String(ch).padStart(2, " ")}}</div>
        <div class="bar-track"><div class="bar-fill" style="width:${{pct}}%"></div></div>
        <div class="bar-count">${{cnt}} APs</div>
      </div>`;
  }}
  document.getElementById("chart-channels").innerHTML = chHtml;

  // Seguridad
  const maxSec = Math.max(1, ...Object.values(DATA.auth_counts));
  let secHtml = "";
  Object.entries(DATA.auth_counts).forEach(([k, cnt]) => {{
    const pct = Math.round((cnt / maxSec) * 100);
    const col = (k === "OPEN" || k === "WEP" || k.includes("WPA_PSK")) ? "#f59e0b" : "#10b981";
    secHtml += `
      <div class="bar-row">
        <div class="bar-label" style="width:140px">${{esc(k)}}</div>
        <div class="bar-track"><div class="bar-fill" style="width:${{pct}}%;background:${{col}}"></div></div>
        <div class="bar-count">${{cnt}} APs</div>
      </div>`;
  }});
  document.getElementById("chart-security").innerHTML = secHtml;

  // Findings
  let fHtml = "";
  if (weakAps > 0) {{
    fHtml += `<div class="kv" style="margin-bottom:10px;border-left:3px solid var(--amber)">
      <div class="kv-k" style="color:var(--amber)">RIESGO ALTO · Cifrado Obsoleto o Mixto (WPA1 / TKIP / OPEN)</div>
      <div style="margin-top:4px">Se han detectado <b>${{weakAps}} puntos de acceso</b> que anuncian soporte para TKIP, WPA1 o red abierta. Haz clic en la pestaña <i>Inteligencia de Redes → Cifrado Débil</i> para inspeccionarlos.</div>
    </div>`;
  }}
  if (wpsAps > 0) {{
    fHtml += `<div class="kv" style="margin-bottom:10px;border-left:3px solid var(--accent)">
      <div class="kv-k" style="color:var(--accent)">SUPERFICIE EXPUESTA · WPS Activo en ${{wpsAps}} Puntos de Acceso</div>
      <div style="margin-top:4px">${{wpsAps}} routers exponen Wi-Fi Protected Setup (WPS) en sus balizas 802.11.</div>
    </div>`;
  }}
  if (DATA.ssid_collisions.length > 0) {{
    fHtml += `<div class="kv" style="margin-bottom:10px;border-left:3px solid var(--purple)">
      <div class="kv-k" style="color:#c084fc">INFRAESTRUCTURA MULTI-BSSID / ROAMING / POSIBLE EVIL TWIN</div>
      <div style="margin-top:4px">SSIDs emitidos desde múltiples BSSIDs físicos: ` +
      DATA.ssid_collisions.map(c =>
        `<a class="entity-link mono" onclick="openSsidDossier('${{esc(c.ssid)}}')">${{esc(c.ssid)}} (${{c.bssids.length}} BSSIDs)</a>`
      ).join(" · ") + `</div></div>`;
  }}
  if (!fHtml) {{
    fHtml = `<div class="kv">Sin alertas críticas detectadas en las sesiones actuales.</div>`;
  }}
  document.getElementById("findings-list").innerHTML = fHtml;
}}

function setNetFilter(f, el) {{
  netFilter = f;
  document.querySelectorAll("#net-chips .chip").forEach(c => c.classList.remove("active"));
  if (el) el.classList.add("active");
  renderNetworksTable();
}}

function renderNetworksTable() {{
  const q = (document.getElementById("net-search").value || "").toLowerCase().trim();
  const sesFilter = document.getElementById("net-session-filter").value;

  const filtered = DATA.aps.filter(ap => {{
    const tag = getNetworkTag(ap.bssid, ap.ssid);
    if (sesFilter && !ap.sessions.includes(sesFilter)) return false;
    if (netFilter === "insecure" && !ap.is_weak && !ap.wps) return false;
    if (netFilter === "multi" && ap.sessions.length < 2) return false;
    if (netFilter === "clients" && ap.clients.length === 0) return false;
    if (netFilter === "probed" && ap.probing_stas.length === 0) return false;
    if (netFilter === "weak" && !ap.is_weak) return false;
    if (netFilter === "wps" && !ap.wps) return false;
    if (netFilter === "hidden" && !ap.hidden) return false;
    if (netFilter === "tagged" && (!tag || !tag.label)) return false;

    if (!q) return true;
    const hay = [
      ap.ssid, ap.bssid, ap.vendor, ap.auth_mode, ap.pairwise_cipher,
      "ch" + ap.primary_channel, tag ? tag.label : "", tag ? tag.category : ""
    ].join(" ").toLowerCase();
    return hay.includes(q);
  }});

  document.getElementById("net-count-label").textContent = `${{filtered.length}} / ${{DATA.aps.length}} BSSIDs`;

  const rows = filtered.map(ap => {{
    const tag = getNetworkTag(ap.bssid, ap.ssid);
    const ssidDisp = ap.hidden
      ? `<span class="badge b-suspicious">&lt;OCULTA&gt;</span>`
      : `<a class="entity-link" onclick="openSsidDossier('${{esc(ap.ssid)}}')">${{esc(ap.ssid)}}</a>`;
    const clusterBadge = ap.cluster_size > 1
      ? ` <span class="badge b-multi" title="Mismo SSID en varios BSSIDs">${{ap.cluster_size}} APs</span>` : "";
    const wpsBadge = ap.wps ? `<span class="badge b-target">WPS</span>` : `<span style="color:var(--muted)">no</span>`;
    const stasHtml = ap.clients.length > 0
      ? `<a class="entity-link mono" onclick="openApDossier('${{ap.bssid}}')">${{ap.clients.length}} STAs</a>`
      : `<span style="color:var(--muted)">0</span>`;
    const probedHtml = ap.probing_stas.length > 0
      ? `<span class="badge b-known">${{ap.probing_stas.length}} STAs</span>`
      : `<span style="color:var(--muted)">—</span>`;
    const sesBadge = ap.sessions.length >= 2
      ? `<span class="badge b-multi">${{ap.sessions.length}} sesiones</span>`
      : `<span class="mono" style="color:var(--muted)">${{esc(getSessionDisplay(ap.sessions[0] || ""))}}</span>`;

    const secStyle = ap.is_weak ? 'color:#f87171;font-weight:700' : '';
    return `<tr>
      <td>${{rssiBar(ap.rssi)}}</td>
      <td class="mono">${{ap.primary_channel}} <span style="color:var(--muted)">(${{ap.band_mhz}})</span></td>
      <td>${{ssidDisp}}${{clusterBadge}}</td>
      <td><a class="entity-link mono" onclick="openApDossier('${{ap.bssid}}')">${{ap.bssid}}</a></td>
      <td>${{renderTagBadge(tag)}}</td>
      <td class="mono" style="${{secStyle}}">${{esc(ap.auth_mode)}} / ${{esc(ap.pairwise_cipher)}}</td>
      <td>${{wpsBadge}}</td>
      <td>${{stasHtml}}</td>
      <td>${{probedHtml}}</td>
      <td>${{sesBadge}}</td>
      <td>${{esc(ap.vendor)}}</td>
      <td class="no-print">
        <button onclick="openTagEditor('network', '${{ap.bssid}}', '${{esc(ap.ssid || ap.bssid)}}')">🏷️ Tag</button>
      </td>
    </tr>`;
  }});

  document.getElementById("net-tbody").innerHTML = rows.join("") ||
    `<tr><td colspan="12" style="text-align:center;color:var(--muted);padding:24px">Sin resultados con los filtros actuales.</td></tr>`;
}}

function setDevFilter(f, el) {{
  devFilter = f;
  document.querySelectorAll("#dev-chips .chip").forEach(c => c.classList.remove("active"));
  if (el) el.classList.add("active");
  renderDevicesTable();
}}

function renderDevicesTable() {{
  const q = (document.getElementById("dev-search").value || "").toLowerCase().trim();

  const filtered = DATA.devices.filter(d => {{
    const tag = getDeviceTag(d.sta_mac);
    if (devFilter === "insecure_ap") {{
      const onInsecure = d.associated_bssids.some(b => {{
        const ap = DATA.ap_by_bssid[b];
        return ap && (ap.is_weak || ap.wps);
      }});
      if (!onInsecure) return false;
    }}
    if (devFilter === "multi" && d.sessions.length < 2) return false;
    if (devFilter === "associated" && d.associated_bssids.length === 0) return false;
    if (devFilter === "probing" && d.probed_ssids.length === 0) return false;
    if (devFilter === "matched" && d.matched_local_ssids.length === 0) return false;
    if (devFilter === "global_mac" && d.is_randomized) return false;
    if (devFilter === "tagged" && (!tag || !tag.label)) return false;

    if (!q) return true;
    const hay = [
      d.sta_mac, d.vendor,
      ...d.probed_ssids,
      ...d.associated_bssids,
      ...d.associated_ssids,
      tag ? tag.label : "", tag ? tag.category : ""
    ].join(" ").toLowerCase();
    return hay.includes(q);
  }});

  document.getElementById("dev-count-label").textContent = `${{filtered.length}} / ${{DATA.devices.length}} dispositivos`;

  const rows = filtered.map(d => {{
    const tag = getDeviceTag(d.sta_mac);
    const macType = d.is_randomized
      ? `<span class="badge b-neutral" title="Bit Locally-Administered activo">MAC Aleatoria</span>`
      : `<span class="badge b-own" title="OUI IEEE global">${{esc(d.vendor)}}</span>`;

    const assocHtml = d.associated_bssids.length > 0
      ? d.associated_bssids.map(b => {{
          const ap = DATA.ap_by_bssid[b];
          const s = ap && ap.ssid ? ap.ssid : b;
          return `<a class="entity-link mono" onclick="openApDossier('${{b}}')">${{esc(s)}}</a>`;
        }}).join(", ")
      : `<span style="color:var(--muted)">Sin asociar (solo Probe)</span>`;

    const probesHtml = d.probed_ssids.length > 0
      ? d.probed_ssids.map(ssid => {{
          const existsHere = DATA.matched_ssids_set.includes(ssid);
          const cls = existsHere ? "b-target" : "b-known";
          const title = existsHere ? "Esta red está presente en el entorno" : "Red remota en la PNL del dispositivo";
          return `<span class="badge ${{cls}}" title="${{title}}" style="cursor:pointer" onclick="openSsidDossier('${{esc(ssid)}}')">${{esc(ssid)}}</span>`;
        }}).join(" ")
      : `<span style="color:var(--muted)">— (Broadcast / Datos)</span>`;

    const sesHtml = d.sessions.length >= 2
      ? `<span class="badge b-multi">${{d.sessions.length}} sesiones</span>`
      : `<span class="mono" style="color:var(--muted)">${{esc(getSessionDisplay(d.sessions[0] || ""))}}</span>`;

    return `<tr>
      <td>${{rssiBar(d.rssi)}}</td>
      <td><a class="entity-link mono" onclick="openDeviceDossier('${{d.sta_mac}}')">${{d.sta_mac}}</a></td>
      <td>${{renderTagBadge(tag)}}</td>
      <td>${{macType}}</td>
      <td>${{assocHtml}}</td>
      <td>${{probesHtml}}</td>
      <td class="mono">${{d.total_frames}}</td>
      <td>${{sesHtml}}</td>
      <td class="no-print">
        <button onclick="openTagEditor('device', '${{d.sta_mac}}', '${{d.sta_mac}}')">🏷️ Tag</button>
      </td>
    </tr>`;
  }});

  document.getElementById("dev-tbody").innerHTML = rows.join("") ||
    `<tr><td colspan="9" style="text-align:center;color:var(--muted);padding:24px">Todavía no hay dispositivos clientes que coincidan con este filtro. Deja correr el modo promiscuo unos ciclos y ejecuta <code>warpull</code>.</td></tr>`;
}}

function renderCrossSession() {{
  const multiDevs = DATA.devices.filter(d => d.sessions.length >= 2);
  const multiAps = DATA.aps.filter(a => a.sessions.length >= 2);

  let html = `<h4>Dispositivos Clientes Vistos en Múltiples Sesiones (${{multiDevs.length}})</h4>`;
  if (multiDevs.length === 0) {{
    html += `<div class="kv" style="margin-bottom:18px;color:var(--muted)">Ningún cliente STA ha repetido en más de una sesión todavía.</div>`;
  }} else {{
    html += `<table><thead><tr><th>STA MAC</th><th>Tag</th><th>Fabricante / Tipo</th><th>Sesiones donde apareció</th><th>Redes Buscadas / Asociadas</th></tr></thead><tbody>`;
    multiDevs.forEach(d => {{
      html += `<tr>
        <td><a class="entity-link mono" onclick="openDeviceDossier('${{d.sta_mac}}')">${{d.sta_mac}}</a></td>
        <td>${{renderTagBadge(getDeviceTag(d.sta_mac))}}</td>
        <td>${{esc(d.vendor)}}</td>
        <td>${{d.sessions.map(s => `<span class="badge b-multi">${{esc(getSessionDisplay(s))}}</span>`).join(" ")}}</td>
        <td>${{[...d.associated_ssids, ...d.probed_ssids].map(esc).join(", ") || "—"}}</td>
      </tr>`;
    }});
    html += `</tbody></table>`;
  }}

  html += `<h4 style="margin-top:24px">Puntos de Acceso Presentes en Múltiples Sesiones (${{multiAps.length}})</h4>`;
  html += `<table><thead><tr><th>BSSID</th><th>SSID</th><th>Tag</th><th>CH</th><th>RSSI Máx</th><th>Sesiones</th></tr></thead><tbody>`;
  multiAps.forEach(ap => {{
    html += `<tr>
      <td><a class="entity-link mono" onclick="openApDossier('${{ap.bssid}}')">${{ap.bssid}}</a></td>
      <td><b>${{ap.hidden ? "&lt;OCULTA&gt;" : esc(ap.ssid)}}</b></td>
      <td>${{renderTagBadge(getNetworkTag(ap.bssid, ap.ssid))}}</td>
      <td class="mono">${{ap.primary_channel}}</td>
      <td>${{rssiBar(ap.rssi)}}</td>
      <td>${{ap.sessions.map(s => `<span class="badge b-known">${{esc(getSessionDisplay(s))}}</span>`).join(" ")}}</td>
    </tr>`;
  }});
  html += `</tbody></table>`;

  document.getElementById("cross-content").innerHTML = html;
}}

function renderSessionsTable() {{
  const sel = document.getElementById("net-session-filter");
  const curVal = sel.value;
  sel.innerHTML = `<option value="">Todas las sesiones (${{DATA.sessions.length}})</option>` +
    DATA.sessions.map(s => `<option value="${{s.session_id}}">${{esc(getSessionDisplay(s.session_id))}}</option>`).join("");
  sel.value = curVal;

  const rows = DATA.sessions.map(s => {{
    const st = getSessionTag(s.session_id);
    const gps = st.gps || s.gps;
    const gpsHtml = (gps && gps.lat !== undefined && gps.lat !== null)
      ? `<a class="entity-link mono" target="_blank" href="https://www.openstreetmap.org/?mlat=${{gps.lat}}&mlon=${{gps.lon}}#map=17/${{gps.lat}}/${{gps.lon}}">📍 ${{Number(gps.lat).toFixed(5)}}, ${{Number(gps.lon).toFixed(5)}}</a>`
      : `<span style="color:var(--muted)">Sin fijar</span>`;

    return `<tr>
      <td class="mono"><b>${{s.session_id}}</b></td>
      <td>${{st.name ? `<span class="badge b-own">${{esc(st.name)}}</span>` : '<span style="color:var(--muted)">Sin nombre</span>'}}</td>
      <td class="mono">${{esc(s.file)}}</td>
      <td class="mono">${{esc(s.first_seen)}} → ${{esc(s.last_seen)}}</td>
      <td class="mono">${{s.scans}}</td>
      <td class="mono">${{s.unique_aps}}</td>
      <td class="mono">${{s.unique_clients}}</td>
      <td>${{gpsHtml}}</td>
      <td class="no-print">
        <button onclick="openSessionEditor('${{s.session_id}}')">✏️ Nombrar / GPS</button>
      </td>
    </tr>`;
  }});
  document.getElementById("sessions-tbody").innerHTML = rows.join("");
}}

function renderTagsRegistry() {{
  let html = `<h4>Sesiones Nombradas</h4><table><thead><tr><th>Session ID</th><th>Nombre</th><th>GPS Asociado</th><th>Notas</th><th>Acción</th></tr></thead><tbody>`;
  const sEntries = Object.entries(TAGS.sessions);
  if (sEntries.length === 0) {{
    html += `<tr><td colspan="5" style="color:var(--muted)">Sin sesiones etiquetadas aún.</td></tr>`;
  }} else {{
    sEntries.forEach(([sid, info]) => {{
      const gpsTxt = info.gps && info.gps.lat !== undefined ? `${{info.gps.lat}}, ${{info.gps.lon}}` : "—";
      html += `<tr>
        <td class="mono">${{esc(sid)}}</td>
        <td><b>${{esc(info.name || "—")}}</b></td>
        <td class="mono">${{esc(gpsTxt)}}</td>
        <td>${{esc(info.notes || "")}}</td>
        <td><button onclick="deleteTag('sessions','${{esc(sid)}}')">🗑️ Borrar</button></td>
      </tr>`;
    }});
  }}
  html += `</tbody></table>`;

  html += `<h4 style="margin-top:20px">Redes Etiquetadas (BSSID / SSID)</h4><table><thead><tr><th>Clave</th><th>Etiqueta / Alias</th><th>Categoría</th><th>Notas</th><th>Acción</th></tr></thead><tbody>`;
  const nEntries = Object.entries(TAGS.networks);
  if (nEntries.length === 0) {{
    html += `<tr><td colspan="5" style="color:var(--muted)">Sin redes etiquetadas aún.</td></tr>`;
  }} else {{
    nEntries.forEach(([k, info]) => {{
      html += `<tr>
        <td class="mono">${{esc(k)}}</td>
        <td>${{renderTagBadge(info)}}</td>
        <td class="mono">${{esc(info.category || "")}}</td>
        <td>${{esc(info.notes || "")}}</td>
        <td><button onclick="deleteTag('networks','${{esc(k)}}')">🗑️ Borrar</button></td>
      </tr>`;
    }});
  }}
  html += `</tbody></table>`;

  html += `<h4 style="margin-top:20px">Dispositivos Etiquetados (STA MAC)</h4><table><thead><tr><th>STA MAC</th><th>Etiqueta / Alias</th><th>Categoría</th><th>Notas</th><th>Acción</th></tr></thead><tbody>`;
  const dEntries = Object.entries(TAGS.devices);
  if (dEntries.length === 0) {{
    html += `<tr><td colspan="5" style="color:var(--muted)">Sin dispositivos etiquetados aún.</td></tr>`;
  }} else {{
    dEntries.forEach(([k, info]) => {{
      html += `<tr>
        <td class="mono">${{esc(k)}}</td>
        <td>${{renderTagBadge(info)}}</td>
        <td class="mono">${{esc(info.category || "")}}</td>
        <td>${{esc(info.notes || "")}}</td>
        <td><button onclick="deleteTag('devices','${{esc(k)}}')">🗑️ Borrar</button></td>
      </tr>`;
    }});
  }}
  html += `</tbody></table>`;

  document.getElementById("tags-registry-content").innerHTML = html;
}}

function deleteTag(group, key) {{
  delete TAGS[group][key];
  saveTags();
}}

/* ---------------- DOSSIERS INTERACTIVOS ---------------- */

function openApDossier(bssid) {{
  const ap = DATA.ap_by_bssid[bssid];
  if (!ap) return;
  const tag = getNetworkTag(ap.bssid, ap.ssid);
  const stas = ap.clients.map(mac => DATA.device_by_mac[mac]).filter(Boolean);

  let html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge" style="display:inline-block;margin-bottom:6px">DOSSIER DE PUNTO DE ACCESO (BSSID)</div>
        <h2 style="margin:0">${{ap.hidden ? "&lt;RED OCULTA&gt;" : esc(ap.ssid)}} <span class="mono" style="color:var(--accent)">(${{ap.bssid}})</span></h2>
      </div>
      <div style="display:flex;gap:8px">
        <button onclick="openTagEditor('network', '${{ap.bssid}}', '${{esc(ap.ssid || ap.bssid)}}')">🏷️ Etiquetar AP</button>
        <button onclick="closeModal()">✕ Cerrar</button>
      </div>
    </div>
    <div class="kv-grid">
      <div class="kv"><div class="kv-k">Tag / Identidad</div><div class="kv-v">${{renderTagBadge(tag)}}</div></div>
      <div class="kv"><div class="kv-k">Fabricante (OUI)</div><div class="kv-v">${{esc(ap.vendor)}}</div></div>
      <div class="kv"><div class="kv-k">Canal / Ancho</div><div class="kv-v">CH ${{ap.primary_channel}} (${{ap.band_mhz}} MHz · ${{ap.proto}})</div></div>
      <div class="kv"><div class="kv-k">Seguridad / Cifrado</div><div class="kv-v">${{esc(ap.auth_mode)}} / ${{esc(ap.pairwise_cipher)}}</div></div>
      <div class="kv"><div class="kv-k">WPS / Oculta</div><div class="kv-v">WPS: ${{ap.wps ? "SÍ" : "NO"}} · Oculta: ${{ap.hidden ? "SÍ" : "NO"}}</div></div>
      <div class="kv"><div class="kv-k">Señal Máx / Visto</div><div class="kv-v">${{ap.rssi}} dBm (${{ap.sightings}} veces)</div></div>
    </div>
    <h4>Sesiones donde se detectó (${{ap.sessions.length}})</h4>
    <div style="margin-bottom:16px">${{ap.sessions.map(s => `<span class="badge b-known" style="margin-right:6px">${{esc(getSessionDisplay(s))}}</span>`).join("")}}</div>
    <h4>Estaciones Clientes Asociadas a este BSSID (${{stas.length}})</h4>
  `;

  if (stas.length === 0) {{
    html += `<div class="kv" style="color:var(--muted)">No se han capturado tramas de datos de clientes asociados a este BSSID todavía.</div>`;
  }} else {{
    html += `<table><thead><tr><th>STA MAC</th><th>Tag</th><th>Fabricante</th><th>RSSI</th><th>Redes que busca (PNL)</th></tr></thead><tbody>`;
    stas.forEach(d => {{
      html += `<tr>
        <td><a class="entity-link mono" onclick="openDeviceDossier('${{d.sta_mac}}')">${{d.sta_mac}}</a></td>
        <td>${{renderTagBadge(getDeviceTag(d.sta_mac))}}</td>
        <td>${{esc(d.vendor)}}</td>
        <td>${{d.rssi}} dBm</td>
        <td>${{d.probed_ssids.map(esc).join(", ") || "—"}}</td>
      </tr>`;
    }});
    html += `</tbody></table>`;
  }}

  showModal(html);
}}

function openSsidDossier(ssid) {{
  const matchingAps = DATA.aps.filter(a => a.ssid === ssid);
  const probingDevs = DATA.devices.filter(d => d.probed_ssids.includes(ssid));

  let html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge" style="display:inline-block;margin-bottom:6px">DOSSIER DE RED (SSID CLUSTER)</div>
        <h2 style="margin:0">${{esc(ssid)}}</h2>
      </div>
      <div style="display:flex;gap:8px">
        <button onclick="openTagEditor('network', 'SSID:${{esc(ssid)}}', '${{esc(ssid)}}')">🏷️ Etiquetar SSID</button>
        <button onclick="closeModal()">✕ Cerrar</button>
      </div>
    </div>
    <h4>Puntos de Acceso (BSSIDs) que emiten este SSID (${{matchingAps.length}})</h4>
  `;

  if (matchingAps.length === 0) {{
    html += `<div class="kv" style="margin-bottom:14px;color:var(--amber)">Esta red NO está presente físicamente en el entorno, pero ha sido solicitada por dispositivos cercanos mediante Probe Requests (red remota guardada en sus perfiles).</div>`;
  }} else {{
    html += `<table><thead><tr><th>BSSID</th><th>Canal</th><th>RSSI</th><th>Seguridad</th><th>Fabricante</th><th>Clientes</th></tr></thead><tbody>`;
    matchingAps.forEach(ap => {{
      html += `<tr>
        <td><a class="entity-link mono" onclick="openApDossier('${{ap.bssid}}')">${{ap.bssid}}</a></td>
        <td class="mono">${{ap.primary_channel}}</td>
        <td>${{ap.rssi}} dBm</td>
        <td class="mono">${{esc(ap.auth_mode)}}/${{esc(ap.pairwise_cipher)}}</td>
        <td>${{esc(ap.vendor)}}</td>
        <td class="mono">${{ap.clients.length}}</td>
      </tr>`;
    }});
    html += `</tbody></table>`;
  }}

  html += `<h4 style="margin-top:18px">Dispositivos que buscan "${{esc(ssid)}}" en Probe Requests (${{probingDevs.length}})</h4>`;
  if (probingDevs.length === 0) {{
    html += `<div class="kv" style="color:var(--muted)">Ningún cliente ha emitido un Probe Request dirigido hacia este SSID durante la escucha.</div>`;
  }} else {{
    html += `<table><thead><tr><th>STA MAC</th><th>Tag</th><th>Fabricante</th><th>RSSI</th><th>Otros SSIDs que busca</th></tr></thead><tbody>`;
    probingDevs.forEach(d => {{
      html += `<tr>
        <td><a class="entity-link mono" onclick="openDeviceDossier('${{d.sta_mac}}')">${{d.sta_mac}}</a></td>
        <td>${{renderTagBadge(getDeviceTag(d.sta_mac))}}</td>
        <td>${{esc(d.vendor)}}</td>
        <td>${{d.rssi}} dBm</td>
        <td>${{d.probed_ssids.filter(x => x !== ssid).map(esc).join(", ") || "—"}}</td>
      </tr>`;
    }});
    html += `</tbody></table>`;
  }}

  showModal(html);
}}

function openDeviceDossier(mac) {{
  const d = DATA.device_by_mac[mac];
  if (!d) return;
  const tag = getDeviceTag(d.sta_mac);

  let html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge" style="display:inline-block;margin-bottom:6px">DOSSIER DE DISPOSITIVO CLIENTE (STA)</div>
        <h2 class="mono" style="margin:0">${{d.sta_mac}}</h2>
      </div>
      <div style="display:flex;gap:8px">
        <button onclick="openTagEditor('device', '${{d.sta_mac}}', '${{d.sta_mac}}')">🏷️ Etiquetar Dispositivo</button>
        <button onclick="closeModal()">✕ Cerrar</button>
      </div>
    </div>
    <div class="kv-grid">
      <div class="kv"><div class="kv-k">Nombre / Tag</div><div class="kv-v">${{renderTagBadge(tag)}}</div></div>
      <div class="kv"><div class="kv-k">Tipo MAC / OUI</div><div class="kv-v">${{d.is_randomized ? "MAC Aleatoria (Privacidad)" : esc(d.vendor)}}</div></div>
      <div class="kv"><div class="kv-k">Señal Máx / Canal</div><div class="kv-v">${{d.rssi}} dBm (CH ${{d.channel}})</div></div>
      <div class="kv"><div class="kv-k">Tramas Capturadas</div><div class="kv-v">${{d.total_frames}} tramas</div></div>
      <div class="kv"><div class="kv-k">Persistencia</div><div class="kv-v">${{d.sessions.length}} sesiones (${{d.sessions.map(getSessionDisplay).map(esc).join(", ")}})</div></div>
    </div>
    <h4>Puntos de Acceso a los que se ha asociado (${{d.associated_bssids.length}})</h4>
  `;

  if (d.associated_bssids.length === 0) {{
    html += `<div class="kv" style="margin-bottom:14px;color:var(--muted)">No se le ha visto asociado transmitiendo datos con ningún AP; detectado únicamente por emisiones de Probe Request.</div>`;
  }} else {{
    html += `<table><thead><tr><th>BSSID</th><th>SSID</th><th>Canal</th><th>Seguridad</th><th>Fabricante AP</th></tr></thead><tbody>`;
    d.associated_bssids.forEach(b => {{
      const ap = DATA.ap_by_bssid[b];
      html += `<tr>
        <td><a class="entity-link mono" onclick="openApDossier('${{b}}')">${{b}}</a></td>
        <td><b>${{ap ? (ap.hidden ? "&lt;OCULTA&gt;" : esc(ap.ssid)) : "AP Externo"}}</b></td>
        <td class="mono">${{ap ? ap.primary_channel : "—"}}</td>
        <td class="mono">${{ap ? esc(ap.auth_mode) : "—"}}</td>
        <td>${{ap ? esc(ap.vendor) : "—"}}</td>
      </tr>`;
    }});
    html += `</tbody></table>`;
  }}

  html += `<h4 style="margin-top:18px">Huella PNL — Redes que este dispositivo busca (${{d.probed_ssids.length}})</h4>`;
  if (d.probed_ssids.length === 0) {{
    html += `<div class="kv" style="color:var(--muted)">Solo ha emitido Probe Requests broadcast (wildcard) o tráfico de datos.</div>`;
  }} else {{
    html += `<div style="display:flex;gap:8px;flex-wrap:wrap">` +
      d.probed_ssids.map(s => {{
        const local = DATA.matched_ssids_set.includes(s);
        return `<span class="badge ${{local ? 'b-target' : 'b-known'}}" style="cursor:pointer;padding:6px 10px" onclick="openSsidDossier('${{esc(s)}}')">${{esc(s)}} ${{local ? '(Presente aquí)' : '(Red remota)'}}</span>`;
      }}).join("") + `</div>`;
  }}

  showModal(html);
}}

/* ---------------- EDITORES DE TAGS Y SESIONES ---------------- */

function openTagEditor(targetType, key, displayTitle) {{
  const group = targetType === "device" ? "devices" : "networks";
  const cur = TAGS[group][key.toUpperCase()] || TAGS[group][key] || {{ label: "", category: "CONOCIDO", notes: "" }};
  const normKey = key.startsWith("SSID:") ? key : key.toUpperCase();

  const html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge">ETIQUETAR ENTIDAD</div>
        <h3 style="margin:6px 0 0 0">${{esc(displayTitle)}} <span class="mono" style="color:var(--muted)">(${{esc(normKey)}})</span></h3>
      </div>
      <button onclick="closeModal()">✕</button>
    </div>
    <div style="display:grid;gap:12px">
      <label>Nombre personalizado / Alias (ej. "Mi Móvil", "Router Vecino", "Portátil Auditoría"):<br>
        <input type="text" id="tag-label" value="${{esc(cur.label || "")}}" style="width:100%;margin-top:4px" placeholder="Escribe un alias...">
      </label>
      <label>Categoría:<br>
        <select id="tag-cat" style="width:100%;margin-top:4px">
          ${{["PROPIO", "CONOCIDO", "OBJETIVO", "SOSPECHOSO", "IGNORAR"].map(c =>
            `<option value="${{c}}" ${{cur.category === c ? "selected" : ""}}>${{c}}</option>`
          ).join("")}}
        </select>
      </label>
      <label>Notas de inteligencia:<br>
        <textarea id="tag-notes" rows="3" style="width:100%;margin-top:4px">${{esc(cur.notes || "")}}</textarea>
      </label>
      <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:8px">
        <button onclick="closeModal()">Cancelar</button>
        <button class="btn-primary" onclick="saveEntityTag('${{group}}', '${{esc(normKey)}}')">Guardar Etiqueta</button>
      </div>
    </div>
  `;
  showModal(html);
}}

function saveEntityTag(group, key) {{
  const label = document.getElementById("tag-label").value.trim();
  const category = document.getElementById("tag-cat").value;
  const notes = document.getElementById("tag-notes").value.trim();
  if (!label) {{
    delete TAGS[group][key];
  }} else {{
    TAGS[group][key] = {{ label, category, notes }};
  }}
  saveTags();
  closeModal();
}}

function openSessionEditor(sid) {{
  const sObj = DATA.sessions.find(x => x.session_id === sid) || {{}};
  const cur = getSessionTag(sid);
  const gps = cur.gps || sObj.gps || {{}};

  const html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge">CONFIGURAR SESIÓN &amp; GPS</div>
        <h3 class="mono" style="margin:6px 0 0 0">Sesión ${{esc(sid)}}</h3>
      </div>
      <button onclick="closeModal()">✕</button>
    </div>
    <div style="display:grid;gap:12px">
      <label>Nombre descriptivo de la sesión (ej. "Oficina Planta 1", "Calle Mayor"):<br>
        <input type="text" id="ses-name" value="${{esc(cur.name || "")}}" style="width:100%;margin-top:4px">
      </label>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
        <label>Latitud GPS de la sesión:<br>
          <input type="number" step="any" id="ses-lat" value="${{gps.lat !== undefined && gps.lat !== null ? gps.lat : ""}}" placeholder="ej. 40.416775" style="width:100%;margin-top:4px">
        </label>
        <label>Longitud GPS de la sesión:<br>
          <input type="number" step="any" id="ses-lon" value="${{gps.lon !== undefined && gps.lon !== null ? gps.lon : ""}}" placeholder="ej. -3.703790" style="width:100%;margin-top:4px">
        </label>
      </div>
      <label>Notas de la sesión:<br>
        <textarea id="ses-notes" rows="2" style="width:100%;margin-top:4px">${{esc(cur.notes || "")}}</textarea>
      </label>
      <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:8px">
        <button onclick="closeModal()">Cancelar</button>
        <button class="btn-primary" onclick="saveSessionMeta('${{esc(sid)}}')">Guardar Sesión</button>
      </div>
    </div>
  `;
  showModal(html);
}}

function saveSessionMeta(sid) {{
  const name = document.getElementById("ses-name").value.trim();
  const latRaw = document.getElementById("ses-lat").value.trim();
  const lonRaw = document.getElementById("ses-lon").value.trim();
  const notes = document.getElementById("ses-notes").value.trim();
  const entry = TAGS.sessions[sid] || {{}};
  entry.name = name;
  entry.notes = notes;
  if (latRaw !== "" && lonRaw !== "") {{
    entry.gps = {{ lat: parseFloat(latRaw), lon: parseFloat(lonRaw), src: "manual" }};
  }}
  TAGS.sessions[sid] = entry;
  saveTags();
  closeModal();
}}

function openTagManagerModal() {{
  const sessionOpts = DATA.sessions.map(s =>
    `<option value="${{esc(s.session_id)}}">${{esc(getSessionDisplay(s.session_id))}} (${{esc(s.first_seen)}})</option>`
  ).join("");

  const html = `
    <div class="modal-header">
      <div>
        <div class="brand-badge">NUEVA ETIQUETA / ALIAS</div>
        <h3 style="margin:6px 0 0 0">Nombrar Sesión, Dispositivo (MAC) o Red (BSSID / SSID)</h3>
      </div>
      <button onclick="closeModal()">✕</button>
    </div>
    <div style="display:grid;gap:12px">
      <label>Tipo de Entidad:<br>
        <select id="new-tag-group" style="width:100%;margin-top:4px" onchange="toggleTagModalFields()">
          <option value="sessions">Sesión de Captura (Session ID + GPS opcional)</option>
          <option value="devices">Dispositivo Cliente (STA MAC)</option>
          <option value="networks">Punto de Acceso / Red (BSSID o SSID:Nombre)</option>
        </select>
      </label>

      <div id="new-tag-session-wrap">
        <label>Seleccionar Sesión capturada (o escribir ID debajo):<br>
          <select id="new-tag-session-select" style="width:100%;margin-top:4px" onchange="onModalSessionSelect()">
            <option value="">-- Seleccionar sesión existente --</option>
            ${{sessionOpts}}
          </select>
        </label>
      </div>

      <label id="new-tag-key-label">Identificador (Session ID / MAC / BSSID / SSID:Nombre):<br>
        <input type="text" id="new-tag-key" placeholder="Ej. 029feefe, AA:BB:CC:DD:EE:FF o SSID:MiRed" style="width:100%;margin-top:4px">
      </label>

      <label>Nombre personalizado / Alias:<br>
        <input type="text" id="new-tag-label" placeholder="Ej. Auditoría Planta 1 / Móvil de Juan / Router Casa" style="width:100%;margin-top:4px">
      </label>

      <div id="new-tag-cat-wrap" style="display:none">
        <label>Categoría:<br>
          <select id="new-tag-cat" style="width:100%;margin-top:4px">
            <option value="PROPIO">PROPIO</option>
            <option value="CONOCIDO">CONOCIDO</option>
            <option value="OBJETIVO">OBJETIVO</option>
            <option value="SOSPECHOSO">SOSPECHOSO</option>
            <option value="IGNORAR">IGNORAR</option>
          </select>
        </label>
      </div>

      <div id="new-tag-gps-wrap" style="display:grid;grid-template-columns:1fr 1fr;gap:10px">
        <label>Latitud GPS de la sesión (opcional):<br>
          <input type="number" step="any" id="new-tag-lat" placeholder="ej. 40.416775" style="width:100%;margin-top:4px">
        </label>
        <label>Longitud GPS de la sesión (opcional):<br>
          <input type="number" step="any" id="new-tag-lon" placeholder="ej. -3.703790" style="width:100%;margin-top:4px">
        </label>
      </div>

      <label>Notas:<br>
        <textarea id="new-tag-notes" rows="2" style="width:100%;margin-top:4px" placeholder="Notas opcionales..."></textarea>
      </label>

      <div style="display:flex;justify-content:flex-end;gap:8px">
        <button onclick="closeModal()">Cancelar</button>
        <button class="btn-primary" onclick="saveManualNewTag()">Guardar</button>
      </div>
    </div>
  `;
  showModal(html);
  if (DATA.sessions.length > 0) {{
    document.getElementById("new-tag-session-select").value = DATA.sessions[DATA.sessions.length - 1].session_id;
    onModalSessionSelect();
  }}
}}

function toggleTagModalFields() {{
  const grp = document.getElementById("new-tag-group").value;
  const isSes = (grp === "sessions");
  document.getElementById("new-tag-session-wrap").style.display = isSes ? "block" : "none";
  document.getElementById("new-tag-gps-wrap").style.display = isSes ? "grid" : "none";
  document.getElementById("new-tag-cat-wrap").style.display = isSes ? "none" : "block";
  if (!isSes) {{
    document.getElementById("new-tag-key").value = "";
    document.getElementById("new-tag-label").value = "";
    document.getElementById("new-tag-notes").value = "";
  }} else if (DATA.sessions.length > 0) {{
    onModalSessionSelect();
  }}
}}

function onModalSessionSelect() {{
  const sid = document.getElementById("new-tag-session-select").value;
  if (!sid) return;
  document.getElementById("new-tag-key").value = sid;
  const cur = getSessionTag(sid);
  const sObj = DATA.sessions.find(x => x.session_id === sid) || {{}};
  const gps = cur.gps || sObj.gps || {{}};
  document.getElementById("new-tag-label").value = cur.name || "";
  document.getElementById("new-tag-notes").value = cur.notes || "";
  document.getElementById("new-tag-lat").value = (gps.lat !== undefined && gps.lat !== null) ? gps.lat : "";
  document.getElementById("new-tag-lon").value = (gps.lon !== undefined && gps.lon !== null) ? gps.lon : "";
}}

function saveManualNewTag() {{
  const grp = document.getElementById("new-tag-group").value;
  let key = document.getElementById("new-tag-key").value.trim();
  const label = document.getElementById("new-tag-label").value.trim();
  const notes = document.getElementById("new-tag-notes").value.trim();
  if (!key || !label) return;

  if (grp === "sessions") {{
    const entry = TAGS.sessions[key] || {{}};
    entry.name = label;
    entry.notes = notes;
    const latRaw = document.getElementById("new-tag-lat").value.trim();
    const lonRaw = document.getElementById("new-tag-lon").value.trim();
    if (latRaw !== "" && lonRaw !== "") {{
      entry.gps = {{ lat: parseFloat(latRaw), lon: parseFloat(lonRaw), src: "manual" }};
    }}
    TAGS.sessions[key] = entry;
  }} else {{
    const category = document.getElementById("new-tag-cat").value;
    if (!key.startsWith("SSID:")) key = key.toUpperCase();
    TAGS[grp][key] = {{ label, category, notes }};
  }}
  saveTags();
  closeModal();
}}

function exportTagsJson() {{
  const blob = new Blob([JSON.stringify(TAGS, null, 2) + "\\n"], {{ type: "application/json" }});
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "tags.json";
  a.click();
}}

function importTagsJson(ev) {{
  const file = ev.target.files[0];
  if (!file) return;
  const reader = new FileReader();
  reader.onload = () => {{
    try {{
      const obj = JSON.parse(reader.result);
      Object.assign(TAGS.sessions, obj.sessions || {{}});
      Object.assign(TAGS.networks, obj.networks || {{}});
      Object.assign(TAGS.devices, obj.devices || {{}});
      saveTags();
    }} catch (e) {{
      alert("Archivo JSON inválido");
    }}
  }};
  reader.readAsText(file);
}}

function showModal(innerHtml) {{
  document.getElementById("modal-body").innerHTML = innerHtml;
  document.getElementById("modal-backdrop").classList.add("open");
}}

function closeModal() {{
  document.getElementById("modal-backdrop").classList.remove("open");
}}

function renderAll() {{
  renderOverview();
  renderSessionsTable();
  renderNetworksTable();
  renderDevicesTable();
  renderCrossSession();
  renderTagsRegistry();
}}

renderAll();
</script>
</body>
</html>
"""
    html_path.write_text(doc, encoding="utf-8")
