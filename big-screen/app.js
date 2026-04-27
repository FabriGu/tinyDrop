/* APIgame big-screen — WebSocket client
 *
 * Connects to the apigame web server, receives state and leaderboard
 * messages, and renders the calendar view, live log, and leaderboard.
 */

(function () {
  "use strict";

  // ── DOM refs ──────────────────────────────────────────────
  var connStatus      = document.getElementById("conn-status");
  var playerBar       = document.getElementById("player-bar");
  var playerName      = document.getElementById("player-name");
  var playerScore     = document.getElementById("player-score");
  var playerTimer     = document.getElementById("player-timer");
  var mainArea        = document.getElementById("main-area");
  var calendarDate    = document.getElementById("calendar-date");
  var calendarEvents  = document.getElementById("calendar-events");
  var logEntries      = document.getElementById("log-entries");
  var leaderboardBar  = document.getElementById("leaderboard-entries");
  var idleOverlay     = document.getElementById("idle-overlay");
  var idleLeaderboard = document.getElementById("idle-leaderboard");

  var MAX_LOG_ENTRIES = 20;
  var ws;
  var reconnectDelay = 1000;
  var lastLeaderboard = [];
  var isPlayerActive = false;

  // ── WebSocket ─────────────────────────────────────────────

  function connect() {
    var protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    var url = protocol + "//" + window.location.host + "/apigame/ws";

    ws = new WebSocket(url);

    ws.onopen = function () {
      connStatus.textContent = "CONNECTED";
      connStatus.className = "connected";
      reconnectDelay = 1000;
    };

    ws.onmessage = function (event) {
      try {
        var msg = JSON.parse(event.data);
        if (msg.topic === "state") {
          handleState(msg.data);
        } else if (msg.topic === "leaderboard") {
          handleLeaderboard(msg.data);
        }
      } catch (e) {
        // ignore malformed messages
      }
    };

    ws.onclose = function () {
      connStatus.textContent = "DISCONNECTED";
      connStatus.className = "disconnected";
      setTimeout(connect, reconnectDelay);
      reconnectDelay = Math.min(reconnectDelay * 2, 10000);
    };

    ws.onerror = function () {
      ws.close();
    };
  }

  // ── State handler ─────────────────────────────────────────

  function handleState(data) {
    // data: { calendar: [...], last_request: {...}, current_player: {...} }

    // Update player info
    var player = data.current_player;
    if (player && player.name) {
      showActiveGame(player);
    } else {
      showIdle();
    }

    // Update calendar
    if (data.calendar) {
      renderCalendar(data.calendar);
    }

    // Update live log
    if (data.last_request && data.last_request.method) {
      addLogEntry(data.last_request);
    }
  }

  // ── Active / Idle toggle ──────────────────────────────────

  function showActiveGame(player) {
    isPlayerActive = true;
    idleOverlay.classList.remove("visible");
    mainArea.classList.remove("dimmed");
    playerBar.classList.remove("hidden");

    playerName.textContent = player.name;
    playerScore.textContent = player.score;

    var secs = player.time_left_s;
    if (typeof secs === "number" && secs >= 0) {
      var min = Math.floor(secs / 60);
      var sec = secs % 60;
      playerTimer.textContent = min + ":" + (sec < 10 ? "0" : "") + sec;
      playerTimer.classList.toggle("urgent", secs <= 10);
    } else {
      playerTimer.textContent = "--:--";
      playerTimer.classList.remove("urgent");
    }
  }

  function showIdle() {
    if (isPlayerActive) {
      // Player just left — clear log for next session
      isPlayerActive = false;
    }
    idleOverlay.classList.add("visible");
    mainArea.classList.add("dimmed");
    playerBar.classList.add("hidden");
    renderIdleLeaderboard();
  }

  // ── Calendar rendering ────────────────────────────────────

  function renderCalendar(events) {
    // Sort by start time
    var sorted = events.slice().sort(function (a, b) {
      return (a.start || "").localeCompare(b.start || "");
    });

    // Update date header
    var now = new Date();
    calendarDate.textContent = formatDateHeader(now);

    if (sorted.length === 0) {
      calendarEvents.innerHTML = '<div class="empty-state">No events</div>';
      return;
    }

    // Build new HTML — track existing IDs for transition
    var html = "";
    for (var i = 0; i < sorted.length; i++) {
      var ev = sorted[i];
      var timeStr = formatEventTime(ev.start);
      var title = escapeHtml(ev.summary || "Untitled");
      html += '<div class="cal-event" data-id="' + escapeHtml(ev.id || "") + '">'
            + '<span class="cal-time">' + timeStr + '</span>'
            + '<span class="cal-title">' + title + '</span>'
            + '</div>';
    }
    calendarEvents.innerHTML = html;
  }

  function formatDateHeader(d) {
    var opts = { weekday: "long", month: "long", day: "numeric" };
    return d.toLocaleDateString("en-US", opts);
  }

  function formatEventTime(isoStr) {
    if (!isoStr) return "--:--";
    try {
      var d = new Date(isoStr);
      var h = d.getHours();
      var m = d.getMinutes();
      var ampm = h >= 12 ? "PM" : "AM";
      h = h % 12 || 12;
      return h + ":" + (m < 10 ? "0" : "") + m + " " + ampm;
    } catch (e) {
      return "--:--";
    }
  }

  // ── Live log ──────────────────────────────────────────────

  function addLogEntry(req) {
    // req: { method, path, status, ts }
    var method = req.method || "???";
    var path = shortenPath(req.path || "");
    var status = req.status || "???";

    var el = document.createElement("div");
    el.className = "log-entry method-" + method;
    el.innerHTML = '<span class="log-method">' + escapeHtml(method) + '</span> '
                 + '<span class="log-path">' + escapeHtml(path) + '</span>'
                 + '<span class="log-status">&rarr; ' + escapeHtml(String(status)) + '</span>';

    // Prepend (newest at top)
    logEntries.insertBefore(el, logEntries.firstChild);

    // Trim old entries
    while (logEntries.children.length > MAX_LOG_ENTRIES) {
      logEntries.removeChild(logEntries.lastChild);
    }
  }

  function shortenPath(path) {
    // Shorten Google Calendar API paths for readability
    // "/calendar/v3/calendars/primary/events" → "/events"
    // "/calendar/v3/calendars/primary/events/abc123" → "/events/abc123"
    return path.replace(/^\/calendar\/v3\/calendars\/primary/, "");
  }

  // ── Leaderboard ───────────────────────────────────────────

  function handleLeaderboard(data) {
    // data: array of { name, score } or { entries: [...] }
    var entries = Array.isArray(data) ? data : (data.entries || []);
    lastLeaderboard = entries.slice(0, 10);
    renderBottomLeaderboard();
    if (!isPlayerActive) {
      renderIdleLeaderboard();
    }
  }

  function renderBottomLeaderboard() {
    if (lastLeaderboard.length === 0) {
      leaderboardBar.innerHTML = '<span class="lb-empty">No scores yet</span>';
      return;
    }

    var html = "";
    for (var i = 0; i < lastLeaderboard.length; i++) {
      var e = lastLeaderboard[i];
      if (i > 0) {
        html += '<span class="lb-sep">\u00b7</span>';
      }
      var rankClass = "lb-rank";
      if (i < 3) rankClass += " lb-rank-" + (i + 1);
      html += '<span class="lb-entry">'
            + '<span class="' + rankClass + '">#' + (i + 1) + '</span>'
            + '<span class="lb-name">' + escapeHtml(e.name || "???") + '</span>'
            + '<span class="lb-score">' + (e.score || 0) + '</span>'
            + '</span>';
    }
    leaderboardBar.innerHTML = html;
  }

  function renderIdleLeaderboard() {
    if (lastLeaderboard.length === 0) {
      idleLeaderboard.innerHTML = "";
      return;
    }

    var html = '<h3>TOP SCORES</h3><div class="idle-lb-list">';
    for (var i = 0; i < lastLeaderboard.length; i++) {
      var e = lastLeaderboard[i];
      var rankClass = "lb-rank";
      if (i < 3) rankClass += " lb-rank-" + (i + 1);
      html += '<div class="idle-lb-row">'
            + '<span class="' + rankClass + '">#' + (i + 1) + '</span>'
            + '<span class="lb-name">' + escapeHtml(e.name || "???") + '</span>'
            + '<span class="lb-score">' + (e.score || 0) + '</span>'
            + '</div>';
    }
    html += '</div>';
    idleLeaderboard.innerHTML = html;
  }

  // ── Helpers ───────────────────────────────────────────────

  function escapeHtml(str) {
    var div = document.createElement("div");
    div.appendChild(document.createTextNode(str));
    return div.innerHTML;
  }

  // ── Init ──────────────────────────────────────────────────

  connect();
})();
