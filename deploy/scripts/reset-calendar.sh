#!/usr/bin/env bash
set -euo pipefail
# Wipe and re-seed the dummy Google Calendar with fresh events.
ssh root@162.243.231.61 "/opt/apigame/venv/bin/python /opt/apigame/agent/calendar_client.py seed ${1:-3}"
