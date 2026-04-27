/* APIgame big-screen WebSocket client */

(function () {
  "use strict";

  var statusEl = document.getElementById("status");
  var outputEl = document.getElementById("output");
  var ws;
  var reconnectDelay = 1000;

  function connect() {
    var protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    var url = protocol + "//" + window.location.host + "/apigame/ws";

    ws = new WebSocket(url);

    ws.onopen = function () {
      statusEl.textContent = "Connected";
      statusEl.className = "connected";
      reconnectDelay = 1000;
    };

    ws.onmessage = function (event) {
      try {
        var msg = JSON.parse(event.data);
        outputEl.textContent = JSON.stringify(msg, null, 2);
      } catch (e) {
        outputEl.textContent = event.data;
      }
    };

    ws.onclose = function () {
      statusEl.textContent = "Disconnected — reconnecting...";
      statusEl.className = "disconnected";
      setTimeout(connect, reconnectDelay);
      reconnectDelay = Math.min(reconnectDelay * 2, 10000);
    };

    ws.onerror = function () {
      ws.close();
    };
  }

  connect();
})();
