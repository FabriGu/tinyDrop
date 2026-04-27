"""Google Calendar API client for APIgame.

Standalone module — no game logic, no MQTT. Just calendar CRUD foundation.
Loads OAuth2 credentials from .env and exposes a CalendarClient class.

CLI usage:
    python calendar_client.py list
    python calendar_client.py seed [COUNT]
    python calendar_client.py insert
    python calendar_client.py patch
    python calendar_client.py delete
"""

import json
import logging
import os
import random
import sys
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

from dotenv import load_dotenv
from google.auth.exceptions import RefreshError
from google.oauth2.credentials import Credentials
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError

logger = logging.getLogger(__name__)

ENV_PATH = Path(__file__).resolve().parent.parent / "config" / ".env"

SCOPES = ["https://www.googleapis.com/auth/calendar.events"]
TOKEN_URI = "https://oauth2.googleapis.com/token"
TIMEZONE = "America/New_York"

EVENT_TITLE_POOL = [
    "Coffee with Sam",
    "Dentist",
    "Standup",
    "Lunch meeting",
    "Team sync",
    "1:1 with Jordan",
    "Code review",
    "Design crit",
    "Office hours",
    "Workshop",
]

# Offsets for seeded events (relative to now)
_SEED_OFFSETS = [
    timedelta(minutes=30),
    timedelta(hours=2),
    timedelta(days=1),
]


def _load_credentials() -> Credentials:
    """Build Credentials from environment variables."""
    load_dotenv(ENV_PATH)

    client_id = os.environ["GOOGLE_CLIENT_ID"]
    client_secret = os.environ["GOOGLE_CLIENT_SECRET"]
    refresh_token = os.environ["GOOGLE_REFRESH_TOKEN"]

    return Credentials(
        token=None,
        refresh_token=refresh_token,
        token_uri=TOKEN_URI,
        client_id=client_id,
        client_secret=client_secret,
        scopes=SCOPES,
    )


def _make_event_body(summary: str, start: datetime) -> dict:
    """Build a Calendar API event body."""
    end = start + timedelta(hours=1)
    return {
        "summary": summary,
        "start": {"dateTime": start.isoformat(), "timeZone": TIMEZONE},
        "end": {"dateTime": end.isoformat(), "timeZone": TIMEZONE},
    }


class CalendarClient:
    """Thin wrapper around the Google Calendar v3 events API."""

    def __init__(self) -> None:
        creds = _load_credentials()
        self._service = build("calendar", "v3", credentials=creds)
        self._calendar_id = os.getenv("CALENDAR_ID", "primary")
        self._event_ids: list[str] = []

    # ------------------------------------------------------------------
    # Read
    # ------------------------------------------------------------------

    def list_events(self) -> list[dict]:
        """Return events from now to now+2 days on the primary calendar."""
        now = datetime.now(timezone.utc)
        time_min = now.isoformat()
        time_max = (now + timedelta(days=2)).isoformat()

        try:
            result = (
                self._service.events()
                .list(
                    calendarId=self._calendar_id,
                    timeMin=time_min,
                    timeMax=time_max,
                    singleEvents=True,
                    orderBy="startTime",
                    timeZone=TIMEZONE,
                )
                .execute()
            )
            return result.get("items", [])

        except HttpError as exc:
            if exc.resp.status == 401:
                logger.warning("401 Unauthorized — token may need manual refresh")
            raise

    def refresh_event_ids(self) -> list[str]:
        """Re-list events and update self._event_ids."""
        events = self.list_events()
        self._event_ids = [e["id"] for e in events]
        return list(self._event_ids)

    # ------------------------------------------------------------------
    # Seed (clean slate + insert N)
    # ------------------------------------------------------------------

    def seed(self, count: int = 3) -> list[str]:
        """Delete all events, then insert *count* fresh ones. Return new IDs."""
        # Delete everything currently on the calendar
        existing = self.list_events()
        for ev in existing:
            try:
                self._service.events().delete(
                    calendarId=self._calendar_id, eventId=ev["id"]
                ).execute()
            except HttpError as exc:
                if exc.resp.status != 404:
                    raise

        # Insert new events
        now = datetime.now(timezone.utc)
        titles = random.sample(EVENT_TITLE_POOL, k=min(count, len(EVENT_TITLE_POOL)))
        offsets = _SEED_OFFSETS[:count]
        # If count > len(_SEED_OFFSETS), generate extra random offsets
        while len(offsets) < count:
            offsets.append(timedelta(hours=random.randint(1, 24)))

        created_ids: list[str] = []
        for title, offset in zip(titles, offsets):
            body = _make_event_body(title, now + offset)
            result = (
                self._service.events()
                .insert(calendarId=self._calendar_id, body=body)
                .execute()
            )
            created_ids.append(result["id"])

        self._event_ids = created_ids
        return created_ids

    # ------------------------------------------------------------------
    # Insert
    # ------------------------------------------------------------------

    def insert_random(self) -> dict:
        """Insert one random event. Return result dict with status/latency."""
        now = datetime.now(timezone.utc)
        summary = random.choice(EVENT_TITLE_POOL)
        offset = timedelta(hours=random.randint(1, 24))
        body = _make_event_body(summary, now + offset)

        t0 = time.time()
        result = (
            self._service.events()
            .insert(calendarId=self._calendar_id, body=body)
            .execute()
        )
        latency_ms = round((time.time() - t0) * 1000)

        event_id = result["id"]
        self._event_ids.append(event_id)
        return {
            "status": 201,
            "event_id": event_id,
            "summary": summary,
            "latency_ms": latency_ms,
        }

    # ------------------------------------------------------------------
    # Patch
    # ------------------------------------------------------------------

    def patch_random(self) -> dict:
        """Patch a random existing event with a new title."""
        if not self._event_ids:
            return {"status": 404, "error": "no events to patch"}

        event_id = random.choice(self._event_ids)
        new_summary = random.choice(EVENT_TITLE_POOL)
        body = {"summary": new_summary}

        t0 = time.time()
        self._service.events().patch(
            calendarId=self._calendar_id, eventId=event_id, body=body
        ).execute()
        latency_ms = round((time.time() - t0) * 1000)

        return {
            "status": 200,
            "event_id": event_id,
            "summary": new_summary,
            "latency_ms": latency_ms,
        }

    # ------------------------------------------------------------------
    # Delete
    # ------------------------------------------------------------------

    def delete_random(self) -> dict:
        """Delete a random existing event. Retry once on 404."""
        if not self._event_ids:
            return {"status": 404, "error": "no events to delete"}

        event_id = random.choice(self._event_ids)

        try:
            t0 = time.time()
            self._service.events().delete(
                calendarId=self._calendar_id, eventId=event_id
            ).execute()
            latency_ms = round((time.time() - t0) * 1000)
        except HttpError as exc:
            if exc.resp.status != 404:
                raise
            # Stale ID — refresh and retry once
            logger.info("404 on delete — refreshing event list and retrying")
            self.refresh_event_ids()
            if not self._event_ids:
                return {"status": 404, "error": "no events to delete after refresh"}
            event_id = random.choice(self._event_ids)
            t0 = time.time()
            self._service.events().delete(
                calendarId=self._calendar_id, eventId=event_id
            ).execute()
            latency_ms = round((time.time() - t0) * 1000)

        self._event_ids = [eid for eid in self._event_ids if eid != event_id]
        return {
            "status": 204,
            "event_id": event_id,
            "latency_ms": latency_ms,
        }


# ======================================================================
# CLI
# ======================================================================

def _cli() -> None:
    logging.basicConfig(level=logging.INFO)

    usage = (
        "Usage: python calendar_client.py <command>\n"
        "Commands: list, seed [COUNT], insert, patch, delete"
    )
    if len(sys.argv) < 2:
        print(usage)
        sys.exit(1)

    cmd = sys.argv[1]
    client = CalendarClient()

    if cmd == "list":
        events = client.list_events()
        print(json.dumps(events, indent=2, default=str))

    elif cmd == "seed":
        count = int(sys.argv[2]) if len(sys.argv) > 2 else 3
        ids = client.seed(count)
        print(json.dumps({"seeded": len(ids), "event_ids": ids}, indent=2))

    elif cmd == "insert":
        result = client.insert_random()
        print(json.dumps(result, indent=2))

    elif cmd == "patch":
        # Need existing IDs — refresh first
        client.refresh_event_ids()
        result = client.patch_random()
        print(json.dumps(result, indent=2))

    elif cmd == "delete":
        # Need existing IDs — refresh first
        client.refresh_event_ids()
        result = client.delete_random()
        print(json.dumps(result, indent=2))

    else:
        print(f"Unknown command: {cmd}")
        print(usage)
        sys.exit(1)


if __name__ == "__main__":
    _cli()
