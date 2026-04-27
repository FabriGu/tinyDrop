#!/usr/bin/env bash
set -euo pipefail

SERVER="root@162.243.231.61"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "=== APIgame deploy ==="
echo "Target: $SERVER"
echo ""

# --- 1. Agent Python files ---
echo "[1/6] Deploying agent..."
scp "$REPO_ROOT/server/agent/agent.py" \
    "$REPO_ROOT/server/agent/calendar_client.py" \
    "$REPO_ROOT/server/agent/task_engine.py" \
    "$SERVER:/opt/apigame/agent/"

# --- 2. Web Python files ---
echo "[2/6] Deploying web server..."
scp "$REPO_ROOT/server/web/app.py" "$SERVER:/opt/apigame/web/"

# --- 3. Big-screen static files ---
echo "[3/6] Deploying big-screen static files..."
ssh "$SERVER" "mkdir -p /opt/apigame/web/static"
scp "$REPO_ROOT/big-screen/index.html" \
    "$REPO_ROOT/big-screen/app.js" \
    "$REPO_ROOT/big-screen/styles.css" \
    "$SERVER:/opt/apigame/web/static/"

# --- 4. Systemd units ---
echo "[4/6] Deploying systemd units..."
scp "$REPO_ROOT/deploy/systemd/apigame-agent.service" \
    "$REPO_ROOT/deploy/systemd/apigame-web.service" \
    "$SERVER:/etc/systemd/system/"

# --- 5. Nginx snippet ---
echo "[5/6] Deploying nginx snippet..."
ssh "$SERVER" "mkdir -p /etc/nginx/snippets"
scp "$REPO_ROOT/deploy/nginx/apigame.conf.snippet" \
    "$SERVER:/etc/nginx/snippets/apigame.conf"

# --- 6. Reload and enable services ---
echo "[6/6] Reloading systemd and enabling services..."
ssh "$SERVER" bash -s <<'REMOTE'
set -euo pipefail
systemctl daemon-reload
systemctl enable --now apigame-agent apigame-web
echo "  apigame-agent: $(systemctl is-active apigame-agent)"
echo "  apigame-web:   $(systemctl is-active apigame-web)"
REMOTE

echo ""
echo "=== Deploy complete ==="
echo ""
echo "IMPORTANT — one-time Nginx setup (if not already done):"
echo "  Add this line inside the 'server { listen 443 ... }' block"
echo "  in /etc/nginx/sites-available/tinydrop, just before the closing '}':"
echo ""
echo "    include /etc/nginx/snippets/apigame.conf;"
echo ""
echo "  Then run:  ssh $SERVER 'nginx -t && systemctl reload nginx'"
