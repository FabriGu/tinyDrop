"""Google Calendar API client for APIgame.

Standalone module — no game logic, no MQTT. Just calendar CRUD foundation.
Loads OAuth2 credentials from .env and exposes a CalendarClient class.
"""

import json
import logging
import os
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


class CalendarClient:
    """Thin wrapper around the Google Calendar v3 events API."""

    def __init__(self) -> None:
        creds = _load_credentials()
        self._service = build("calendar", "v3", credentials=creds)
        self._calendar_id = os.getenv("CALENDAR_ID", "primary")

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


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    client = CalendarClient()
    events = client.list_events()
    print(json.dumps(events, indent=2, default=str))
