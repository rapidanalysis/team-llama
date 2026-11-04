/* ============================================================
   XERXES PI ADMIN — app.js
   Handles: localisation, theme, status check, tab switching,
            output popup, system commands, network info & config,
            hostname rename, Tailscale dashboard, ngrok dashboard,
            service badges, reboot/shutdown, Llama (local LLM),
            I2C (PiicoDev RFID reader).
   ============================================================ */

var API_BASE = "";  // same origin — served by the C++ backend

/* ── LOCALISATION ── */
function loadLang() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/lang.json", true);
  xhr.onload = function () {
    if (xhr.status === 200) {
      try {
        var strings = JSON.parse(xhr.responseText);
        document.querySelectorAll("[data-i18n]").forEach(function (el) {
          var key = el.getAttribute("data-i18n");
          if (strings[key]) el.textContent = strings[key];
        });
      } catch (e) { console.warn("lang.json parse error:", e); }
    }
  };
  xhr.send();
}

/* ── THEME TOGGLE ── */
var themeToggle = document.getElementById("theme-toggle");
var themeLabel  = document.getElementById("theme-label");
var html        = document.documentElement;

var savedTheme = localStorage.getItem("xerxes-theme") || "light";
html.setAttribute("data-theme", savedTheme);
themeLabel.textContent = savedTheme === "dark" ? "Light" : "Dark";

themeToggle.addEventListener("click", function () {
  var current = html.getAttribute("data-theme");
  var next    = current === "dark" ? "light" : "dark";
  html.setAttribute("data-theme", next);
  localStorage.setItem("xerxes-theme", next);
  themeLabel.textContent = next === "dark" ? "Light" : "Dark";
});

/* ── BACKEND STATUS CHECK ── */
var statusDot   = document.getElementById("status-dot");
var statusLabel = document.getElementById("status-label");

function checkStatus() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API_BASE + "/api/status", true);
  xhr.timeout = 4000;
  xhr.onload = function () {
    if (xhr.status === 200) {
      statusDot.className     = "status-dot online";
      statusLabel.textContent = "Backend Online";
    } else { setOffline(); }
  };
  xhr.onerror   = setOffline;
  xhr.ontimeout = setOffline;
  xhr.send();
}

function setOffline() {
  statusDot.className     = "status-dot offline";
  statusLabel.textContent = "Backend Offline";
}

checkStatus();
setInterval(checkStatus, 15000);

/* ── TAB SWITCHING ── */
var navLinks = document.querySelectorAll(".nav-link[data-section]");
var sections = document.querySelectorAll(".section");

navLinks.forEach(function (link) {
  link.addEventListener("click", function (e) {
    e.preventDefault();
    var target = link.getAttribute("data-section");

    navLinks.forEach(function (l) { l.classList.remove("active"); });
    link.classList.add("active");

    sections.forEach(function (s) { s.classList.remove("active"); });
    var sec = document.getElementById(target);
    if (sec) sec.classList.add("active");

    // Lazy-load section data
    if (target === "network") loadNetworkInfo();
    if (target === "tunnels") { loadTailscaleDetails(); loadNewtStatus(); }
    if (target === "llama")   { loadLlamaStatus(); loadLlamaModels(); }
    if (target === "i2c") rfidOnShow(); else rfidOnHide();
  });
});

/* ── OUTPUT POPUP ── */
var outputPopup     = document.getElementById("output-popup");
var popupBody       = document.getElementById("popup-body");
var popupTitle      = document.getElementById("popup-title");
var closeAnimHandler = null;          // tracks in-flight close animation handler
var consoleHistory  = "No commands have been run yet.";  // last real output for Console button

// Sets popup output text AND saves it to the console history log
function setPopupOutput(text) {
  popupBody.textContent = text;
  if (text && text !== "Running..." && text !== "Enabling Tailscale...") {
    consoleHistory = text;
  }
}

function showOutputPopup(title, text) {
  // If a close animation is mid-flight, cancel it before re-opening so the
  // stale handler doesn't fire on this open animation's animationend
  if (closeAnimHandler) {
    outputPopup.removeEventListener("animationend", closeAnimHandler);
    closeAnimHandler = null;
  }
  outputPopup.classList.remove("popup-closing");
  popupTitle.textContent = title || "Output";
  popupBody.textContent  = text  || "Running...";
  outputPopup.hidden = false;
}

function closeOutputPopup() {
  if (outputPopup.hidden) return;
  outputPopup.classList.add("popup-closing");
  // Use a named inner function so the removeEventListener call has a stable reference.
  // If we did closeAnimHandler = null first and then removeEventListener(closeAnimHandler),
  // we'd be passing null and the listener would never be removed — causing the next
  // open animation's animationend to instantly re-hide the popup.
  function handler(e) {
    if (e.target !== outputPopup) return;   // ignore bubbled events from .popup-box
    outputPopup.removeEventListener("animationend", handler);  // remove first, while ref is valid
    closeAnimHandler = null;
    outputPopup.hidden = true;
    outputPopup.classList.remove("popup-closing");
  }
  closeAnimHandler = handler;
  outputPopup.addEventListener("animationend", handler);
}

document.getElementById("popup-close").addEventListener("click", closeOutputPopup);

outputPopup.addEventListener("click", function (e) {
  if (e.target === outputPopup) closeOutputPopup();
});

/* ── CONSOLE BUTTON (navbar) ── */
document.getElementById("console-open-btn").addEventListener("click", function () {
  showOutputPopup("Console", consoleHistory);
});

/* ── DASHBOARD CARDS ── */
function loadDashboard() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API_BASE + "/api/dashboard", true);
  xhr.timeout = 6000;
  xhr.onload = function () {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        if (data.hostname) {
          document.getElementById("val-hostname").textContent = data.hostname;
          var badge = document.getElementById("badge-hostname");
          if (badge) badge.textContent = data.hostname;
        }
        if (data.uptime) document.getElementById("val-uptime").textContent = data.uptime;
        if (data.disk)   document.getElementById("val-disk").textContent   = data.disk;
        if (data.ip)     document.getElementById("val-ip").textContent     = data.ip;
      } catch (e) { console.warn("Dashboard parse error:", e); }
    }
  };
  xhr.onerror   = function () { console.warn("Dashboard fetch failed."); };
  xhr.ontimeout = function () { console.warn("Dashboard fetch timed out."); };
  xhr.send();
}

/* ── SERVICE STATUS BADGES ── */
function loadServices() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API_BASE + "/api/services", true);
  xhr.timeout = 6000;
  xhr.onload = function () {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        updateBadge("badge-samba",     data.smbd);
        updateBadge("badge-tailscale", data.tailscaled);
        updateBadge("badge-ngrok",     data.ngrok);
      } catch (e) { console.warn("Services parse error:", e); }
    }
  };
  xhr.send();
}

function updateBadge(id, status) {
  var el = document.getElementById(id);
  if (!el) return;
  el.textContent = status || "unknown";
  el.className   = "service-badge";
  if (status === "active")        el.classList.add("badge-active");
  else if (status === "failed")   el.classList.add("badge-failed");
  else                            el.classList.add("badge-inactive");
}

/* ── SYSTEM COMMANDS (write to inline output panel) ── */
var outputBody = document.getElementById("output-body");

document.querySelectorAll(".sys-cmd").forEach(function (btn) {
  btn.addEventListener("click", function () {
    runSystemCommand(btn.getAttribute("data-command"), btn);
  });
});

function runSystemCommand(command, btn) {
  btn.disabled = true;
  btn.classList.add("running");
  outputBody.textContent = "Running: " + command + "...\n";

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 15000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      outputBody.textContent = data.output || data.message || xhr.responseText;
    } catch (e) { outputBody.textContent = xhr.responseText; }
    btn.disabled = false;
    btn.classList.remove("running");
  };
  xhr.onerror   = function () { outputBody.textContent = "Error: Could not reach the backend."; btn.disabled = false; btn.classList.remove("running"); };
  xhr.ontimeout = function () { outputBody.textContent = "Error: Request timed out.";           btn.disabled = false; btn.classList.remove("running"); };
  xhr.send(JSON.stringify({ command: command }));
}

document.getElementById("clear-output").addEventListener("click", function () {
  outputBody.textContent = "Command output will appear here...";
});

/* ── GENERIC CMD-BTN (non-system — shows output popup) ── */
document.querySelectorAll(".cmd-btn:not(.sys-cmd):not(.newt-btn)").forEach(function (btn) {
  btn.addEventListener("click", function () {
    runPopupCommand(btn.getAttribute("data-command"), btn);
  });
});

function runPopupCommand(command, btn) {
  var label = command.replace(/_/g, " ");
  showOutputPopup(label, "Running...");
  btn.disabled = true;
  btn.classList.add("running");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = (command === "tailscale_up") ? 20000 : 15000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.output || data.message || xhr.responseText);
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled = false;
    btn.classList.remove("running");

    var serviceCommands = ["samba_start","samba_stop","samba_status",
                           "tailscale_down","tailscale_status",
                           "ngrok_stop","ngrok_status"];
    if (serviceCommands.indexOf(command) !== -1) {
      loadServices();
      if (command.indexOf("tailscale") !== -1) loadTailscaleDetails();
    }
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach the backend."); btn.disabled = false; btn.classList.remove("running"); };
  xhr.ontimeout = function () { setPopupOutput("Error: Request timed out.");           btn.disabled = false; btn.classList.remove("running"); };
  xhr.send(JSON.stringify({ command: command }));
}

/* ── NETWORK INFO (auto-loaded on Network tab open) ── */
function loadNetworkInfo() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API_BASE + "/api/netinfo", true);
  xhr.timeout = 8000;
  xhr.onload = function () {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        document.getElementById("net-iface-card").textContent    = data.interface || "—";
        document.getElementById("net-ip-card").textContent       = data.ip        || "—";
        document.getElementById("net-gateway-card").textContent  = data.gateway   || "—";
        var dns = Array.isArray(data.dns) ? data.dns.join(", ") : (data.dns || "—");
        document.getElementById("net-dns-card").textContent = dns || "—";

        // Populate interface dropdown
        var select = document.getElementById("net-iface-select");
        select.innerHTML = "";
        var ifaces = data.interfaces || (data.interface ? [data.interface] : []);
        ifaces.forEach(function (iface) {
          var opt = document.createElement("option");
          opt.value = iface;
          opt.textContent = iface;
          select.appendChild(opt);
        });
        if (ifaces.length === 0) {
          var opt = document.createElement("option");
          opt.value = "";
          opt.textContent = "No interfaces found";
          select.appendChild(opt);
        }
      } catch (e) { console.warn("netinfo parse error:", e); }
    }
  };
  xhr.onerror   = function () { console.warn("netinfo fetch failed."); };
  xhr.ontimeout = function () { console.warn("netinfo fetch timed out."); };
  xhr.send();
}

/* ── NETWORK CONFIG (static IP / DHCP) ── */
document.getElementById("net-mode-select").addEventListener("change", function () {
  document.getElementById("static-ip-fields").hidden = (this.value !== "static");
});

document.getElementById("net-config-btn").addEventListener("click", function () {
  var iface    = document.getElementById("net-iface-select").value;
  var mode     = document.getElementById("net-mode-select").value;
  var feedback = document.getElementById("net-config-feedback");
  var btn      = this;

  if (!iface) {
    showFeedback(feedback, "Please select a network interface.", "error");
    return;
  }

  var body = { interface: iface, mode: mode };

  if (mode === "static") {
    body.ip      = document.getElementById("net-static-ip").value.trim();
    body.gateway = document.getElementById("net-static-gateway").value.trim();
    body.dns     = document.getElementById("net-static-dns").value.trim();

    if (!body.ip || !body.gateway) {
      showFeedback(feedback, "IP address and gateway are required for static mode.", "error");
      return;
    }
  }

  btn.disabled    = true;
  btn.textContent = "Applying...";
  showFeedback(feedback, "", "");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/network", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 10000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.status === "ok") {
        showFeedback(feedback, data.output || "Configuration updated. Reboot to apply.", "success");
      } else {
        showFeedback(feedback, data.message || "Failed to apply configuration.", "error");
      }
    } catch (e) {
      showFeedback(feedback, "Unexpected response from backend.", "error");
    }
    btn.disabled    = false;
    btn.textContent = "Apply";
  };
  xhr.onerror   = function () { showFeedback(feedback, "Error: Could not reach backend.", "error"); btn.disabled = false; btn.textContent = "Apply"; };
  xhr.ontimeout = function () { showFeedback(feedback, "Error: Request timed out.", "error");       btn.disabled = false; btn.textContent = "Apply"; };
  xhr.send(JSON.stringify(body));
});

/* ── HOSTNAME RENAME ── */
var hostnameBtn      = document.getElementById("hostname-btn");
var hostnameInput    = document.getElementById("hostname-input");
var hostnameReset    = document.getElementById("hostname-reset");
var hostnameFeedback = document.getElementById("hostname-feedback");

hostnameReset.addEventListener("click", function () {
  var currentHostname = document.getElementById("val-hostname").textContent;
  if (currentHostname && currentHostname !== "—") {
    hostnameInput.value = currentHostname;
    hostnameInput.focus();
    showFeedback(hostnameFeedback, "Current hostname pre-filled — click Rename to apply.", "");
  } else {
    showFeedback(hostnameFeedback, "Could not read current hostname — try refreshing.", "error");
  }
});

hostnameBtn.addEventListener("click", function () {
  var newName = hostnameInput.value.trim();
  if (!newName) {
    showFeedback(hostnameFeedback, "Please enter a hostname.", "error");
    return;
  }

  hostnameBtn.disabled    = true;
  hostnameBtn.textContent = "Renaming...";
  showFeedback(hostnameFeedback, "", "");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/hostname", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 8000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.output && data.output.indexOf("[error]") !== -1) {
        showFeedback(hostnameFeedback, data.output, "error");
      } else {
        showFeedback(hostnameFeedback, "Hostname updated successfully. Reboot to apply fully.", "success");
        hostnameInput.value = "";
        loadDashboard();
      }
    } catch (e) {
      showFeedback(hostnameFeedback, "Unexpected response from backend.", "error");
    }
    hostnameBtn.disabled    = false;
    hostnameBtn.textContent = "Rename";
  };
  xhr.onerror   = function () { showFeedback(hostnameFeedback, "Error: Could not reach backend.", "error"); hostnameBtn.disabled = false; hostnameBtn.textContent = "Rename"; };
  xhr.ontimeout = function () { showFeedback(hostnameFeedback, "Error: Request timed out.", "error");       hostnameBtn.disabled = false; hostnameBtn.textContent = "Rename"; };
  xhr.send(JSON.stringify({ hostname: newName }));
});

/* ── TAILSCALE DASHBOARD ── */
function loadTailscaleDetails() {
  // Get Tailscale IP
  var xhrIP = new XMLHttpRequest();
  xhrIP.open("POST", API_BASE + "/api/command", true);
  xhrIP.setRequestHeader("Content-Type", "application/json");
  xhrIP.timeout = 5000;
  xhrIP.onload = function () {
    try {
      var data = JSON.parse(xhrIP.responseText);
      var ip = (data.output || "").trim();
      document.getElementById("ts-ip").textContent =
        (ip && ip.indexOf("[") === -1) ? ip : "—";
    } catch (e) {}
  };
  xhrIP.send(JSON.stringify({ command: "tailscale_ip" }));

  // Parse node name from tailscale status
  var xhrSt = new XMLHttpRequest();
  xhrSt.open("POST", API_BASE + "/api/command", true);
  xhrSt.setRequestHeader("Content-Type", "application/json");
  xhrSt.timeout = 5000;
  xhrSt.onload = function () {
    try {
      var data = JSON.parse(xhrSt.responseText);
      var output = (data.output || "").trim();
      var lines = output.split("\n").filter(function (l) { return l.trim(); });
      // First line of tailscale status: "<ip>  <node-name>  <user>  <os>  -"
      if (lines.length > 0) {
        var parts = lines[0].trim().split(/\s+/);
        document.getElementById("ts-node").textContent = parts[1] || "—";
      }
    } catch (e) {}
  };
  xhrSt.send(JSON.stringify({ command: "tailscale_status" }));
}

// Tailscale enable with optional auth key
document.getElementById("ts-enable-btn").addEventListener("click", function () {
  var authkey  = document.getElementById("ts-authkey").value.trim();
  var feedback = document.getElementById("ts-feedback");
  var btn      = this;

  // Auth keys are alphanumeric + hyphens + underscores only
  if (authkey && !/^[a-zA-Z0-9\-_]+$/.test(authkey)) {
    showFeedback(feedback, "Invalid auth key format.", "error");
    return;
  }

  showOutputPopup("Tailscale — Enable", "Enabling Tailscale...");
  btn.disabled    = true;
  btn.textContent = "Enabling...";
  showFeedback(feedback, "", "");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/tailscale/up", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 20000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.output || data.message || xhr.responseText);
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled    = false;
    btn.textContent = "Enable";
    loadServices();
    loadTailscaleDetails();
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach backend."); btn.disabled = false; btn.textContent = "Enable"; };
  xhr.ontimeout = function () { setPopupOutput("Tailscale up timed out — check if auth is needed."); btn.disabled = false; btn.textContent = "Enable"; };
  xhr.send(JSON.stringify({ authkey: authkey }));
});

/* ── NGROK DASHBOARD ── */
document.getElementById("ngrok-start-btn").addEventListener("click", function () {
  var command   = document.getElementById("ngrok-port-select").value;
  var portLabel = document.getElementById("ngrok-port-select").selectedOptions[0].textContent;
  // Extract the numeric port from the command key e.g. "ngrok_start_8080" → "8080"
  var portNum   = command.replace("ngrok_start_", "");
  var btn = this;

  showOutputPopup("ngrok — Start", "Starting ngrok (" + portLabel + ")...");
  btn.disabled    = true;
  btn.textContent = "Starting...";

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 15000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.output || data.message || xhr.responseText);
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled    = false;
    btn.textContent = "Start";
    loadServices();

    // Show searching state while we wait for ngrok to fully connect
    document.getElementById("ngrok-url").textContent          = "Searching for URL...";
    document.getElementById("ngrok-port-display").textContent = portNum;
    // ngrok takes several seconds to establish the tunnel and start its local API
    // on :4040, so we start after 3 s and retry up to 4 times every 3 s.
    setTimeout(function () { fetchNgrokUrl(portNum, 1); }, 3000);
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach backend."); btn.disabled = false; btn.textContent = "Start"; };
  xhr.ontimeout = function () { setPopupOutput("Error: Request timed out.");       btn.disabled = false; btn.textContent = "Start"; };
  xhr.send(JSON.stringify({ command: command }));
});

// Tries to read the active tunnel URL from ngrok's local API (:4040).
// Retries up to maxAttempts times with a 3-second gap if the API isn't
// ready yet (ngrok takes a few seconds to fully establish the tunnel).
function fetchNgrokUrl(localPort, attempt) {
  var maxAttempts = 4;
  attempt = attempt || 1;

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 5000;

  xhr.onload = function () {
    try {
      var data   = JSON.parse(xhr.responseText);
      var output = data.output || "";

      // Try 1: ngrok local API JSON — "public_url":"https://..."
      var match = output.match(/"public_url"\s*:\s*"([^"]+)"/);

      // Try 2: raw URL extracted from /tmp/ngrok.log (fallback when API isn't ready)
      if (!match) {
        match = output.match(/(https:\/\/[a-z0-9-]+\.ngrok[a-z0-9._-]*)/i);
      }

      if (match) {
        document.getElementById("ngrok-url").textContent          = match[1];
        document.getElementById("ngrok-port-display").textContent = localPort || "—";
      } else if (attempt < maxAttempts) {
        // Neither source had a URL yet — retry
        setTimeout(function () { fetchNgrokUrl(localPort, attempt + 1); }, 3000);
      } else {
        // All retries exhausted — tell user to click Check Status for details
        document.getElementById("ngrok-url").textContent = "URL not found — click Check Status for log";
      }
    } catch (e) {
      if (attempt < maxAttempts) {
        setTimeout(function () { fetchNgrokUrl(localPort, attempt + 1); }, 3000);
      }
    }
  };

  xhr.onerror = xhr.ontimeout = function () {
    if (attempt < maxAttempts) {
      setTimeout(function () { fetchNgrokUrl(localPort, attempt + 1); }, 3000);
    }
  };

  xhr.send(JSON.stringify({ command: "ngrok_url" }));
}

/* ── NEWT (PANGOLIN) DASHBOARD ── */
// Newt runs as the "xerxes-newt" Docker container, managed by /api/newt/*.
// The secret is sent once on Enable and is never returned by the backend.
var newtBadge        = document.getElementById("badge-newt");
var newtStateEl      = document.getElementById("newt-state");
var newtEndpointShow = document.getElementById("newt-endpoint-display");
var newtIdShow       = document.getElementById("newt-id-display");
var newtVersionShow  = document.getElementById("newt-version-display");
var newtEndpointIn   = document.getElementById("newt-endpoint");
var newtIdIn         = document.getElementById("newt-id");
var newtSecretIn     = document.getElementById("newt-secret");
var newtEnableBtn    = document.getElementById("newt-enable-btn");
var newtFeedback     = document.getElementById("newt-feedback");
var newtPollTimer    = null;

// Docker access fixes (D-01)
var newtFixBox       = document.getElementById("newt-docker-fix");
var newtFixMessage   = document.getElementById("newt-fix-message");
var newtFixOptions   = document.getElementById("newt-fix-options");

// Updates
var newtAutoToggle   = document.getElementById("newt-auto-toggle");
var newtUpdateSummary = document.getElementById("newt-update-summary");
var newtCheckBtn     = document.getElementById("newt-check-btn");
var newtUpdateBtn    = document.getElementById("newt-update-btn");
var newtRollbackBtn  = document.getElementById("newt-rollback-btn");
var newtUpdateFeedback = document.getElementById("newt-update-feedback");
var newtUpdateBusy   = false;
var newtConfigured   = false;   // automatic updates can only be turned on once Newt is set up

var NEWT_STATES = {
  connected:          { badge: "connected",   cls: "badge-active",   label: "Connected" },
  connecting:         { badge: "connecting",  cls: "badge-neutral",  label: "Connecting..." },
  auth_failed:        { badge: "rejected",    cls: "badge-failed",   label: "Rejected" },
  error:              { badge: "error",       cls: "badge-failed",   label: "Can't reach server" },
  stopped:            { badge: "stopped",     cls: "badge-inactive", label: "Stopped" },
  not_configured:     { badge: "not set up",  cls: "badge-inactive", label: "Not set up" },
  docker_unavailable: { badge: "docker error", cls: "badge-failed",  label: "Docker unavailable" }
};

// Plain-language hints for the states that need the user to do something
var NEWT_HINTS = {
  auth_failed:        "Pangolin rejected the Newt ID or secret. Check them against your Site in Pangolin.",
  error:              "Newt can't reach the Pangolin server. Check the endpoint address and this device's internet connection.",
  docker_unavailable: "Newt needs Docker, and the backend can't use it."
};

function renderNewtStatus(data) {
  var st = NEWT_STATES[data.state] || { badge: data.state || "unknown", cls: "badge-neutral", label: data.state || "Unknown" };
  newtBadge.textContent   = st.badge;
  newtBadge.className     = "service-badge " + st.cls;
  newtStateEl.textContent = st.label;
  newtEndpointShow.textContent = data.endpoint || "—";
  newtIdShow.textContent       = data.newt_id  || "—";
  newtVersionShow.textContent  = data.version  || "—";

  // Pre-fill the form with the saved settings (never the secret)
  if (!newtEndpointIn.value && data.endpoint) newtEndpointIn.value = data.endpoint;
  if (!newtIdIn.value && data.newt_id)        newtIdIn.value       = data.newt_id;
  newtSecretIn.placeholder = data.has_secret ? "Saved — leave blank to keep it" : "Newt secret";

  if (NEWT_HINTS[data.state]) {
    showFeedback(newtFeedback, NEWT_HINTS[data.state] + (data.detail ? " (" + data.detail + ")" : ""), "error");
  } else if (data.state === "connected") {
    showFeedback(newtFeedback, "Tunnel is up. This device is reachable through Pangolin.", "success");
  }

  // Show how to fix Docker access, or hide the box once it works
  if (data.state === "docker_unavailable") loadNewtDockerFix();
  else if (data.state !== "not_configured" || !newtFixBox.hidden) newtFixBox.hidden = true;

  renderNewtUpdates(data);
}

// llamaRequest() (Llama section) is a generic JSON XHR helper — reused here.
function loadNewtStatus(cb) {
  llamaRequest("GET", "/api/newt/status", null, 10000, function (err, data) {
    if (err) {
      newtBadge.textContent = "unknown";
      newtBadge.className   = "service-badge badge-neutral";
      if (cb) cb(null);
      return;
    }
    renderNewtStatus(data);
    if (cb) cb(data.state);
  });
}

// After Enable, Newt needs a few seconds to log in and bring the tunnel up.
// Poll every 3 s for up to ~45 s until it settles.
function pollNewtStatus(triesLeft) {
  clearTimeout(newtPollTimer);
  loadNewtStatus(function (state) {
    if (state === "connecting" && triesLeft > 0) {
      newtPollTimer = setTimeout(function () { pollNewtStatus(triesLeft - 1); }, 3000);
    } else if (state === "connecting") {
      showFeedback(newtFeedback, "Still connecting. Click View Logs to see what Newt is doing.", "error");
    }
  });
}

/* Docker access check (D-01): explain the problem and show copyable fixes */
function loadNewtDockerFix(cb) {
  llamaRequest("GET", "/api/newt/docker-check", null, 15000, function (err, data) {
    if (err) { if (cb) cb(null); return; }
    renderNewtDockerFix(data);
    if (cb) cb(data.state);
  });
}

function renderNewtDockerFix(data) {
  if (data.state === "ok") { newtFixBox.hidden = true; return; }

  newtFixMessage.textContent = data.message + (data.detail ? " Docker said: " + data.detail : "");
  newtFixOptions.textContent = "";

  (data.fixes || []).forEach(function (fix, i) {
    var opt = document.createElement("div");
    opt.className = "newt-fix-option";

    var title = document.createElement("div");
    title.className   = "newt-fix-option-title";
    title.textContent = (data.fixes.length > 1 ? "Option " + (i + 1) + ": " : "") + fix.title;
    opt.appendChild(title);

    var why = document.createElement("div");
    why.className   = "newt-fix-option-why";
    why.textContent = fix.why;
    opt.appendChild(why);

    var row  = document.createElement("div");
    row.className = "newt-fix-commands";
    var pre  = document.createElement("pre");
    pre.textContent = fix.commands.join("\n");
    var copy = document.createElement("button");
    copy.type = "button";
    copy.className   = "secondary-btn";
    copy.textContent = "Copy";
    copy.addEventListener("click", function () { copyNewtCommands(pre, copy); });
    row.appendChild(pre);
    row.appendChild(copy);
    opt.appendChild(row);

    if (fix.after) {
      var after = document.createElement("div");
      after.className   = "newt-fix-option-after";
      after.textContent = fix.after;
      opt.appendChild(after);
    }
    newtFixOptions.appendChild(opt);
  });
  newtFixBox.hidden = false;
}

// The clipboard API only works over HTTPS or localhost; otherwise select the
// text so the user can press Ctrl+C.
function copyNewtCommands(pre, btn) {
  var text = pre.textContent;
  function selected() {
    var range = document.createRange();
    range.selectNodeContents(pre);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
    btn.textContent = "Press Ctrl+C";
  }
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text).then(function () { btn.textContent = "Copied"; }, selected);
  } else {
    selected();
  }
  setTimeout(function () { btn.textContent = "Copy"; }, 2500);
}

document.getElementById("newt-fix-recheck").addEventListener("click", function () {
  var btn = this;
  btn.disabled = true;
  loadNewtDockerFix(function (state) {
    btn.disabled = false;
    if (state === "ok") {
      showFeedback(newtFeedback, "Docker is working now. Click Enable to start Newt.", "success");
      loadNewtStatus();
    }
  });
});

/* Enable: checks Docker access and other copies of Newt (D-05) first */
function enableNewt(force) {
  var endpoint = newtEndpointIn.value.trim();
  var newtId   = newtIdIn.value.trim();
  var secret   = newtSecretIn.value.trim();
  var hasSaved = newtSecretIn.placeholder.indexOf("Saved") === 0;

  if (!/^https?:\/\/[A-Za-z0-9][A-Za-z0-9.\-:\/_]*$/.test(endpoint)) {
    showFeedback(newtFeedback, "Enter the Pangolin endpoint, e.g. https://pangolin.example.com", "error");
    return;
  }
  if (!/^[A-Za-z0-9]{1,64}$/.test(newtId)) {
    showFeedback(newtFeedback, "The Newt ID should be letters and numbers only.", "error");
    return;
  }
  if (secret ? !/^[A-Za-z0-9]{1,128}$/.test(secret) : !hasSaved) {
    showFeedback(newtFeedback, secret ? "The secret should be letters and numbers only." : "Enter the Newt secret.", "error");
    return;
  }

  clearTimeout(newtPollTimer);
  newtEnableBtn.disabled    = true;
  newtEnableBtn.textContent = "Enabling...";
  showFeedback(newtFeedback, "Starting Newt... the first time can take a minute while it downloads.", "");

  llamaRequest("POST", "/api/newt/enable",
    { endpoint: endpoint, newt_id: newtId, secret: secret, force: !!force }, 300000,
    function (err, data) {
      newtEnableBtn.disabled    = false;
      newtEnableBtn.textContent = "Enable";
      if (err) { showFeedback(newtFeedback, err, "error"); return; }

      if (data.code === "conflict") {
        var list = (data.conflicts || []).map(function (c) {
          return "  • " + c.name + " (" + c.detail + ")";
        }).join("\n");
        var ok = confirm(
          "Another copy of Newt is already running on this device:\n\n" + list + "\n\n" +
          "If it uses the same Pangolin Site, the two will keep disconnecting each other, " +
          "and anything reached through that tunnel (such as a web SSH session) may drop.\n\n" +
          "Start Newt anyway?");
        if (ok) enableNewt(true);
        else showFeedback(newtFeedback, "Not started. Another copy of Newt is running on this device.", "error");
        return;
      }
      if (data.code === "docker_access") {
        showFeedback(newtFeedback, data.error + " See below for how to fix it.", "error");
        loadNewtDockerFix();
        return;
      }
      if (!data.success) {
        showFeedback(newtFeedback, data.error || "Could not start Newt.", "error");
        if (data.output) showOutputPopup("Newt — Enable", data.output);
        return;
      }
      newtSecretIn.value = "";
      newtFixBox.hidden  = true;
      showFeedback(newtFeedback, "Newt started. Waiting for the tunnel to connect...", "");
      pollNewtStatus(15);
    });
}

newtEnableBtn.addEventListener("click", function () { enableNewt(false); });

document.getElementById("newt-disable-btn").addEventListener("click", function () {
  var btn = this;
  clearTimeout(newtPollTimer);
  btn.disabled = true;
  btn.classList.add("running");
  llamaRequest("POST", "/api/newt/disable", null, 30000, function (err, data) {
    btn.disabled = false;
    btn.classList.remove("running");
    if (err)               showFeedback(newtFeedback, err, "error");
    else if (!data.success) showFeedback(newtFeedback, data.error || "Could not stop Newt.", "error");
    else                   showFeedback(newtFeedback, "Newt stopped. Your settings are saved for next time.", "success");
    if (data && data.code === "docker_access") loadNewtDockerFix();
    loadNewtStatus();
  });
});

document.getElementById("newt-status-btn").addEventListener("click", function () {
  showFeedback(newtFeedback, "", "");
  loadNewtStatus();
});

document.getElementById("newt-logs-btn").addEventListener("click", function () {
  showOutputPopup("Newt — Logs", "Loading...");
  llamaRequest("GET", "/api/newt/logs", null, 10000, function (err, data) {
    if (err) { setPopupOutput(err); return; }
    setPopupOutput(data.output && data.output.indexOf("No such container") === -1
      ? data.output
      : "No logs yet. Newt isn't running. Fill in the details above and click Enable.");
  });
});

/* Updates: version, daily automatic update, manual update and roll back */
function formatNewtCheckTime(unixSeconds) {
  if (!unixSeconds) return "never";
  var d = new Date(unixSeconds * 1000);
  return d.toLocaleString([], { dateStyle: "medium", timeStyle: "short" });
}

function renderNewtUpdates(data) {
  var configured = data.state !== "not_configured";
  var running    = data.container === "running";

  newtConfigured = configured;
  setNewtAutoToggleReady(configured);
  newtAutoToggle.checked = configured && data.auto_update !== false;
  newtUpdateSummary.textContent = configured
    ? "Last checked: " + formatNewtCheckTime(data.last_check) + (data.last_result ? ". " + data.last_result : "")
    : "Set up Newt above first. Updates are checked once it is running.";

  if (newtUpdateBusy) return;
  newtCheckBtn.disabled    = !configured;
  newtUpdateBtn.disabled   = !running;
  newtRollbackBtn.disabled = !configured || !data.rollback_available;
  newtUpdateBtn.classList.toggle("update-ready", !!data.update_available && running);
}

// The automatic-update switch stays off and locked until Newt is set up.
function setNewtAutoToggleReady(ready) {
  newtAutoToggle.disabled = !ready;
  newtAutoToggle.closest(".newt-auto").classList.toggle("not-ready", !ready);
  newtAutoToggle.closest(".newt-auto").title = ready ? "" : "Set up Newt above first.";
  if (!ready) newtAutoToggle.checked = false;
}

function setNewtUpdateBusy(busy, btn, label) {
  newtUpdateBusy = busy;
  [newtCheckBtn, newtUpdateBtn, newtRollbackBtn].forEach(function (b) { b.disabled = busy; });
  if (busy) { btn.dataset.label = btn.textContent; btn.textContent = label; btn.classList.add("running"); }
  else      { btn.textContent = btn.dataset.label || btn.textContent; btn.classList.remove("running"); }
}

// Runs one update action and shows its result. The tunnel may drop while
// Newt restarts; if this page is open through Pangolin it may lose the reply,
// but the backend finishes (and rolls back if needed) on its own.
function runNewtUpdateAction(path, btn, busyLabel, waitingText) {
  setNewtUpdateBusy(true, btn, busyLabel);
  showFeedback(newtUpdateFeedback, waitingText, "");
  llamaRequest("POST", path, null, 600000, function (err, data) {
    setNewtUpdateBusy(false, btn);
    if (err) {
      showFeedback(newtUpdateFeedback, err + " If you opened this page through the tunnel, reload it in a minute.", "error");
    } else if (data.code === "docker_access") {
      showFeedback(newtUpdateFeedback, data.error, "error");
      loadNewtDockerFix();
    } else if (!data.success) {
      showFeedback(newtUpdateFeedback, data.error || "Something went wrong.", "error");
      if (data.output && data.code !== "rolled_back") showOutputPopup("Newt — Update", data.output);
    } else {
      showFeedback(newtUpdateFeedback, data.output, "success");
    }
    loadNewtStatus();
  });
}

newtCheckBtn.addEventListener("click", function () {
  runNewtUpdateAction("/api/newt/update/check", newtCheckBtn, "Checking...",
    "Downloading the latest Newt image to compare. This can take a minute.");
});

newtUpdateBtn.addEventListener("click", function () {
  if (!confirm("Update Newt now?\n\nNewt restarts, so the tunnel drops for a few seconds. " +
               "If the new version doesn't reconnect within a minute, the previous version is put back automatically.")) return;
  runNewtUpdateAction("/api/newt/update", newtUpdateBtn, "Updating...",
    "Updating Newt and waiting for the tunnel to reconnect (up to a minute)...");
});

newtRollbackBtn.addEventListener("click", function () {
  if (!confirm("Go back to the previous version of Newt?\n\nThe tunnel drops for a few seconds while Newt restarts.")) return;
  runNewtUpdateAction("/api/newt/rollback", newtRollbackBtn, "Rolling back...", "Restarting Newt on the previous version...");
});

newtAutoToggle.addEventListener("change", function () {
  var enabled = newtAutoToggle.checked;
  if (enabled && !newtConfigured) {
    setNewtAutoToggleReady(false);
    showFeedback(newtUpdateFeedback, "Set up Newt above first, then turn on automatic updates.", "error");
    return;
  }
  newtAutoToggle.disabled = true;
  llamaRequest("POST", "/api/newt/auto-update", { enabled: enabled }, 10000, function (err, data) {
    newtAutoToggle.disabled = false;
    if (err || !data.success) {
      newtAutoToggle.checked = !enabled;
      showFeedback(newtUpdateFeedback, err || data.error || "Could not change the setting.", "error");
      // Newt was removed in another tab or session: lock the switch again
      if (data && data.code === "not_configured") loadNewtStatus();
      return;
    }
    showFeedback(newtUpdateFeedback, data.output, "success");
  });
});

/* ── SHARED FEEDBACK HELPER ── */
function showFeedback(el, msg, type) {
  el.textContent = msg;
  el.className   = "control-feedback" + (type ? " " + type : "");
}

/* ── REBOOT / SHUTDOWN ── */
document.getElementById("btn-reboot").addEventListener("click", function () {
  if (confirm("Are you sure you want to reboot the device?")) {
    showOutputPopup("Reboot", "Sending reboot command...");
    var xhr = new XMLHttpRequest();
    xhr.open("POST", API_BASE + "/api/command", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.timeout = 8000;
    xhr.onload = function () {
      try { setPopupOutput(JSON.parse(xhr.responseText).output || "Rebooting..."); }
      catch (e) { setPopupOutput("Reboot command sent."); }
    };
    xhr.onerror = function () { setPopupOutput("Reboot command sent (connection lost)."); };
    xhr.send(JSON.stringify({ command: "reboot" }));
  }
});

document.getElementById("btn-shutdown").addEventListener("click", function () {
  if (confirm("Are you sure you want to shut down the device? You will need physical access to turn it back on.")) {
    showOutputPopup("Shutdown", "Sending shutdown command...");
    var xhr = new XMLHttpRequest();
    xhr.open("POST", API_BASE + "/api/command", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.timeout = 8000;
    xhr.onload = function () {
      try { setPopupOutput(JSON.parse(xhr.responseText).output || "Shutting down..."); }
      catch (e) { setPopupOutput("Shutdown command sent."); }
    };
    xhr.onerror = function () { setPopupOutput("Shutdown command sent (connection lost)."); };
    xhr.send(JSON.stringify({ command: "shutdown" }));
  }
});

/* ── DOCKER ── */
var dockerOutput   = document.getElementById("docker-output-body");
var dockerFeedback = document.getElementById("docker-feedback");

document.querySelectorAll(".docker-btn").forEach(function (btn) {
  btn.addEventListener("click", function () {
    var command   = btn.getAttribute("data-docker-command");
    var needsName = btn.getAttribute("data-needs-name") === "true";
    var needsConfirm = btn.getAttribute("data-confirm") === "true";
    var name      = document.getElementById("container-input").value.trim();

    if (needsName && !name) {
      showFeedback(dockerFeedback, "Please enter a container name.", "error");
      document.getElementById("container-input").focus();
      return;
    }

    if (needsConfirm && !confirm("Remove container \"" + name + "\"? This cannot be undone.")) {
      return;
    }

    showFeedback(dockerFeedback, "", "");
    runDockerCommand(command, needsName ? name : "");
  });
});

function runDockerCommand(command, param) {
  document.querySelectorAll(".docker-btn").forEach(function (b) {
    b.classList.add("running");
    b.disabled = true;
  });

  dockerOutput.textContent = "Running: " + command + (param ? " " + param : "") + "...\n";

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = (command === "container_restart") ? 65000 : 20000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      dockerOutput.textContent = data.output || data.message || xhr.responseText;
    } catch (e) {
      dockerOutput.textContent = xhr.responseText;
    }
    resetDockerButtons();
  };

  xhr.onerror   = function () { dockerOutput.textContent = "Error: Could not reach the backend."; resetDockerButtons(); };
  xhr.ontimeout = function () { dockerOutput.textContent = "Error: Request timed out."; resetDockerButtons(); };

  var payload = { command: command };
  if (param) payload.param = param;
  xhr.send(JSON.stringify(payload));
}

function resetDockerButtons() {
  document.querySelectorAll(".docker-btn").forEach(function (b) {
    b.classList.remove("running");
    b.disabled = false;
  });
}

document.getElementById("clear-docker-output").addEventListener("click", function () {
  dockerOutput.textContent = "Docker output will appear here...";
});

/* ── RUN CONTAINER FORM ── */
var runFeedback = document.getElementById("run-feedback");

// ── Dynamic row builders ──────────────────────────────────────────────────────

function makeDynamicRow(list, innerHtml) {
  var row = document.createElement("div");
  row.className = "dynamic-row";
  row.innerHTML = innerHtml;
  row.querySelector(".remove-btn").addEventListener("click", function () {
    list.removeChild(row);
  });
  list.appendChild(row);
  return row;
}

document.getElementById("add-port-btn").addEventListener("click", function () {
  var list = document.getElementById("port-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" placeholder="8080" maxlength="5" />' +
    '<span class="row-sep">:</span>' +
    '<input class="text-input" type="text" placeholder="80" maxlength="5" />' +
    '<button class="remove-btn" type="button" aria-label="Remove">&times;</button>'
  );
});

document.getElementById("add-volume-btn").addEventListener("click", function () {
  var list = document.getElementById("volume-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" placeholder="/host/path" />' +
    '<span class="row-sep">:</span>' +
    '<input class="text-input" type="text" placeholder="/container/path" />' +
    '<select class="select-input select-input--narrow">' +
      '<option value="">rw</option>' +
      '<option value="ro">ro</option>' +
    '</select>' +
    '<button class="remove-btn" type="button" aria-label="Remove">&times;</button>'
  );
});

document.getElementById("add-env-btn").addEventListener("click", function () {
  var list = document.getElementById("env-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" placeholder="KEY" />' +
    '<span class="row-sep">=</span>' +
    '<input class="text-input" type="text" placeholder="value" />' +
    '<button class="remove-btn" type="button" aria-label="Remove">&times;</button>'
  );
});

// ── Form collection + submission ─────────────────────────────────────────────

document.getElementById("run-container-btn").addEventListener("click", function () {
  var image = document.getElementById("run-image").value.trim();
  if (!image) {
    showFeedback(runFeedback, "Image name is required.", "error");
    document.getElementById("run-image").focus();
    return;
  }

  var payload = {
    image:   image,
    name:    document.getElementById("run-name").value.trim(),
    restart: document.getElementById("run-restart").value,
    detach:  true,
    ports:   [],
    volumes: [],
    env:     []
  };

  // Collect port rows — skip incomplete rows silently
  document.querySelectorAll("#port-list .dynamic-row").forEach(function (row) {
    var inputs = row.querySelectorAll("input");
    var host = inputs[0].value.trim();
    var ctr  = inputs[1].value.trim();
    if (host && ctr) payload.ports.push(host + ":" + ctr);
  });

  // Collect volume rows
  document.querySelectorAll("#volume-list .dynamic-row").forEach(function (row) {
    var inputs = row.querySelectorAll("input");
    var sel    = row.querySelector("select");
    var host = inputs[0].value.trim();
    var ctr  = inputs[1].value.trim();
    if (host && ctr) {
      payload.volumes.push(host + ":" + ctr + (sel.value ? ":" + sel.value : ""));
    }
  });

  // Collect env rows
  document.querySelectorAll("#env-list .dynamic-row").forEach(function (row) {
    var inputs = row.querySelectorAll("input");
    var key = inputs[0].value.trim();
    var val = inputs[1].value.trim();
    if (key) payload.env.push(key + "=" + val);
  });

  submitRunContainer(payload);
});

function submitRunContainer(payload) {
  var btn = document.getElementById("run-container-btn");
  btn.disabled    = true;
  btn.textContent = "Pulling & starting...";
  showFeedback(runFeedback, "", "");
  dockerOutput.textContent = "Running: docker run " + payload.image + "...\n" +
                             "This may take a while if the image needs to be pulled.\n";

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/docker/run", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 300000; // 5 minutes — image pulls can be slow

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.status === "ok") {
        dockerOutput.textContent = data.output || "[ok] Container started.";
        showFeedback(runFeedback, "Container started successfully.", "success");
      } else {
        // Show the summary AND Docker's own message (e.g. "name already in use"), not just the exit code
        dockerOutput.textContent = [data.error, data.output].filter(Boolean).join("\n\n") || xhr.responseText;
        showFeedback(runFeedback, data.error || "Failed to start container.", "error");
      }
    } catch (e) {
      dockerOutput.textContent = xhr.responseText;
      showFeedback(runFeedback, "Unexpected response from backend.", "error");
    }
    btn.disabled    = false;
    btn.textContent = "Run Container";
  };

  xhr.onerror = function () {
    dockerOutput.textContent = "Error: Could not reach the backend.";
    showFeedback(runFeedback, "Could not reach the backend.", "error");
    btn.disabled    = false;
    btn.textContent = "Run Container";
  };

  xhr.ontimeout = function () {
    dockerOutput.textContent = "Request timed out — the image may still be pulling in the background.\n" +
                               "Use List Containers to check if it started.";
    showFeedback(runFeedback, "Request timed out.", "error");
    btn.disabled    = false;
    btn.textContent = "Run Container";
  };

  xhr.send(JSON.stringify(payload));
}

/* ── LLAMA ── */
var llamaToggle         = document.getElementById("llama-enable-toggle");
var llamaBadge          = document.getElementById("badge-llama");
var llamaEnableFeedback = document.getElementById("llama-enable-feedback");
var llamaUrlInput       = document.getElementById("llama-model-url");
var llamaDownloadBtn    = document.getElementById("llama-download-btn");
var llamaModelFeedback  = document.getElementById("llama-model-feedback");
var llamaInstalledList  = document.getElementById("llama-installed-list");
var llamaInstalledEmpty = document.getElementById("llama-installed-empty");
var llamaInstalledFeedback = document.getElementById("llama-installed-feedback");

var llamaRunning     = false;
var llamaSelected    = "";   // filename of the model chosen in Installed models
var llamaDownloading = "";   // filename being downloaded — hidden from the list until complete

// Small wrapper: every Llama endpoint returns JSON, so parse it once here.
// cb(errorMessage, data, httpStatus) — errorMessage is null when the body parsed.
function llamaRequest(method, path, body, timeout, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, API_BASE + path, true);
  if (body) xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = timeout;
  xhr.onload = function () {
    var data = null;
    try { data = JSON.parse(xhr.responseText); } catch (e) {}
    cb(data === null ? "Unexpected response from backend." : null, data, xhr.status);
  };
  xhr.onerror   = function () { cb("Error: Could not reach backend."); };
  xhr.ontimeout = function () { cb("Error: Request timed out."); };
  xhr.send(body ? JSON.stringify(body) : null);
}

function setLlamaBadge(text, cls) {
  llamaBadge.textContent = text;
  llamaBadge.className   = "service-badge " + cls;
}

function setLlamaRunning(running) {
  llamaRunning        = running;
  llamaToggle.checked = running;
  llamaToggle.disabled = false;
  setLlamaBadge(running ? "running" : "stopped", running ? "badge-active" : "badge-inactive");

  // The model can't be swapped while llama-server has one loaded
  llamaInstalledList.querySelectorAll(".model-row").forEach(function (row) {
    row.classList.toggle("disabled", running);
    row.querySelector("input").disabled = running;
  });
  showFeedback(llamaInstalledFeedback, running && llamaSelected ? "Turn Llama off to switch models." : "", "");
}

function loadLlamaStatus(cb) {
  llamaRequest("GET", "/api/llama/status", null, 5000, function (err, data) {
    if (err) { setLlamaBadge("unknown", "badge-neutral"); }
    else     { setLlamaRunning(!!data.running); }
    if (cb) cb(err ? null : !!data.running);
  });
}

function loadLlamaModels() {
  llamaRequest("GET", "/api/llama/models", null, 5000, function (err, data) {
    if (err || !Array.isArray(data)) {
      showFeedback(llamaInstalledFeedback, err || "Could not load installed models.", "error");
      return;
    }
    renderLlamaModels(data.filter(function (m) { return m !== llamaDownloading; }));
  });
}

function renderLlamaModels(models) {
  llamaInstalledList.textContent = "";
  llamaInstalledEmpty.hidden = models.length > 0;
  llamaInstalledList.hidden  = models.length === 0;
  if (models.length === 0) { llamaSelected = ""; return; }

  if (models.indexOf(llamaSelected) === -1) llamaSelected = models[0];

  models.forEach(function (name) {
    var row   = document.createElement("label");
    row.className = "model-row";
    var radio = document.createElement("input");
    radio.type    = "radio";
    radio.name    = "llama-model";
    radio.value   = name;
    radio.checked = (name === llamaSelected);
    radio.addEventListener("change", function () { llamaSelected = name; });
    var span  = document.createElement("span");
    span.className   = "model-name";
    span.textContent = name;
    row.appendChild(radio);
    row.appendChild(span);
    llamaInstalledList.appendChild(row);
  });

  setLlamaRunning(llamaRunning);  // re-apply the locked state to the new rows
}

llamaToggle.addEventListener("change", function () {
  if (llamaToggle.checked) startLlama(); else stopLlama();
});

function startLlama() {
  var model = llamaSelected;
  if (!model) {
    llamaToggle.checked = false;
    showFeedback(llamaEnableFeedback, "Download a model first, then try again.", "error");
    return;
  }

  llamaToggle.disabled = true;
  setLlamaBadge("starting", "badge-neutral");
  showFeedback(llamaEnableFeedback, "Starting with " + model + "...", "");

  llamaRequest("POST", "/api/llama/enable", { model: model }, 15000, function (err, data) {
    if (err || !data.success) {
      showFeedback(llamaEnableFeedback, err || data.error || "Failed to start Llama.", "error");
      loadLlamaStatus();
      return;
    }
    setLlamaRunning(true);
    showFeedback(llamaEnableFeedback,
      "Loading " + model + ". Small models are ready in a few seconds; larger ones can take a minute.", "success");

    // llama-server exits quickly if the model doesn't fit in memory or the
    // binary is missing — catch that rather than leaving the toggle on.
    setTimeout(function () {
      loadLlamaStatus(function (running) {
        if (running === false) {
          showFeedback(llamaEnableFeedback,
            "Llama stopped right after starting. The model may be too large for this device's memory, " +
            "or llama-server hasn't been built yet (run make llama).", "error");
        }
      });
    }, 4000);
  });
}

function stopLlama() {
  llamaToggle.disabled = true;
  setLlamaBadge("stopping", "badge-neutral");
  showFeedback(llamaEnableFeedback, "Stopping...", "");

  // The backend waits up to 5 s for llama-server to exit cleanly
  llamaRequest("POST", "/api/llama/disable", null, 10000, function (err) {
    if (err) {
      showFeedback(llamaEnableFeedback, err, "error");
      loadLlamaStatus();
      return;
    }
    setLlamaRunning(false);
    showFeedback(llamaEnableFeedback, "Llama stopped. Its memory has been freed.", "success");
  });
}

// Accepts the links people actually copy from Hugging Face and turns them into
// the direct file link the backend needs:
//   .../blob/main/x.gguf   (web page)     -> .../resolve/main/x.gguf
//   ...x.gguf?download=true                -> ...x.gguf
function normaliseModelUrl(raw) {
  var url = raw.trim().split("#")[0].split("?")[0];
  return url.replace(/^(https:\/\/huggingface\.co\/[^\/]+\/[^\/]+)\/blob\//, "$1/resolve/");
}

llamaDownloadBtn.addEventListener("click", function () {
  var url = normaliseModelUrl(llamaUrlInput.value);

  if (!url) {
    showFeedback(llamaModelFeedback, "Paste a Hugging Face download link first.", "error");
    llamaUrlInput.focus();
    return;
  }
  if (url.indexOf("https://huggingface.co/") !== 0 || url.indexOf("/resolve/") === -1 || !/\.gguf$/i.test(url)) {
    showFeedback(llamaModelFeedback,
      "Use the link to a .gguf file, like https://huggingface.co/<user>/<repo>/resolve/main/<file>.gguf", "error");
    llamaUrlInput.focus();
    return;
  }

  var filename = url.substring(url.lastIndexOf("/") + 1);
  llamaUrlInput.value = url;
  llamaDownloading    = filename;
  llamaDownloadBtn.disabled    = true;
  llamaDownloadBtn.textContent = "Downloading...";
  showFeedback(llamaModelFeedback,
    "Downloading " + filename + ". Large models can take several minutes — keep this tab open to see the result.", "");

  function finish() {
    llamaDownloading = "";
    llamaDownloadBtn.disabled    = false;
    llamaDownloadBtn.textContent = "Download";
    loadLlamaModels();
  }

  llamaRequest("POST", "/api/llama/download", { url: url }, 3600000, function (err, data) {
    if (err === "Error: Request timed out.") {
      showFeedback(llamaModelFeedback,
        "Still downloading after an hour. The device keeps going in the background — check Installed models later.", "error");
    } else if (err || !data.success) {
      showFeedback(llamaModelFeedback, err || data.error || "Download failed.", "error");
    } else {
      showFeedback(llamaModelFeedback, "Downloaded " + filename + ".", "success");
      llamaUrlInput.value = "";
      if (!llamaRunning) llamaSelected = filename;
    }
    finish();
  });
});

/* ── ASK XERXES AI (chat panel) ──
   Lives outside the tab sections, so it stays open and keeps the
   conversation when switching tabs. Talks to the local model through
   the backend's OpenAI-compatible proxy. */
var chatLauncher = document.getElementById("chat-launcher");
var chatPanel    = document.getElementById("chat-panel");
var chatClose    = document.getElementById("chat-close");
var chatMessages = document.getElementById("chat-messages");
var chatForm     = document.getElementById("chat-form");
var chatInput    = document.getElementById("chat-input");
var chatSend     = document.getElementById("chat-send");
var chatDot      = document.getElementById("chat-status-dot");

var CHAT_SYSTEM_PROMPT =
  "You are Xerxes Assistant, built into the Xerxes Pi admin panel on a Raspberry Pi " +
  "compute module. Help the user manage and understand their device. Keep answers short and practical.";
var CHAT_MAX_HISTORY = 10;   // messages sent back to the model; its context is only 4096 tokens
var chatHistory = [];        // { role, content } — excludes the greeting and failed turns
var chatBusy    = false;

function openChat() {
  chatPanel.classList.add("open");
  chatPanel.setAttribute("aria-hidden", "false");
  chatLauncher.hidden = true;
  chatLauncher.setAttribute("aria-expanded", "true");
  if (!chatBusy) refreshChatStatus();
  chatInput.focus();
}

function closeChat() {
  chatPanel.classList.remove("open");
  chatPanel.setAttribute("aria-hidden", "true");
  chatLauncher.hidden = false;
  chatLauncher.setAttribute("aria-expanded", "false");
  chatLauncher.focus();
}

chatLauncher.addEventListener("click", openChat);
chatClose.addEventListener("click", closeChat);
document.addEventListener("keydown", function (e) {
  if (e.key === "Escape" && chatPanel.classList.contains("open")) closeChat();
});

function setChatDot(running) {
  chatDot.className = "chat-status-dot" + (running === true ? " on" : running === false ? " off" : "");
  chatDot.title = running === true ? "Llama is running" : running === false ? "Llama is off" : "Llama status unknown";
}

function refreshChatStatus() {
  llamaRequest("GET", "/api/llama/status", null, 5000, function (err, data) {
    setChatDot(err ? null : !!data.running);
  });
}

function appendChatMessage(cls, text) {
  var el = document.createElement("div");
  el.className   = "chat-msg " + cls;
  el.textContent = text;
  chatMessages.appendChild(el);
  chatMessages.scrollTop = chatMessages.scrollHeight;
  return el;
}

function appendChatTyping() {
  var el = document.createElement("div");
  el.className = "chat-msg assistant typing";
  el.setAttribute("aria-label", "Assistant is typing");
  for (var i = 0; i < 3; i++) el.appendChild(document.createElement("span"));
  chatMessages.appendChild(el);
  chatMessages.scrollTop = chatMessages.scrollHeight;
  return el;
}

// Error bubble; when Llama is off, include a button that jumps to the Llama tab.
function appendChatError(text, showLlamaLink) {
  var el = appendChatMessage("error", text);
  if (showLlamaLink) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = "Open Llama settings";
    btn.addEventListener("click", function () {
      closeChat();
      document.querySelector('.nav-link[data-section="llama"]').click();
    });
    el.appendChild(btn);
  }
  chatMessages.scrollTop = chatMessages.scrollHeight;
}

// Turns backend / llama-server failures into something the user can act on.
// Returns { text, llamaOff }.
function describeChatError(err, data, status) {
  if (err === "Error: Request timed out.")
    return { text: "No reply after 3 minutes. The model may be too large for this device. Try a shorter question or a smaller model." };
  if (err === "Error: Could not reach backend.")
    return { text: "Could not reach the backend. Check the panel is still online." };

  var msg = data && data.error ? (typeof data.error === "string" ? data.error : data.error.message || "") : "";
  if (msg.indexOf("not running") !== -1)
    return { text: "Llama is turned off. Turn it on in the Llama tab, then send your message again.", llamaOff: true };
  if (status === 503 || msg.indexOf("not reachable") !== -1 || /loading/i.test(msg))
    return { text: "The model is still loading. Try again in a few seconds." };
  return { text: msg || "The model returned an error (HTTP " + status + ")." };
}

function setChatBusy(busy) {
  chatBusy = busy;
  chatSend.disabled = busy;
}

chatForm.addEventListener("submit", function (e) {
  e.preventDefault();
  var text = chatInput.value.trim();
  if (!text || chatBusy) return;

  chatInput.value = "";
  appendChatMessage("user", text);
  chatHistory.push({ role: "user", content: text });
  setChatBusy(true);
  var typing = appendChatTyping();

  var messages = [{ role: "system", content: CHAT_SYSTEM_PROMPT }]
    .concat(chatHistory.slice(-CHAT_MAX_HISTORY));

  // Generation on a Pi is slow — allow up to 3 minutes for a reply
  llamaRequest("POST", "/api/llama/v1/chat/completions",
               { messages: messages, max_tokens: 512 }, 180000,
               function (err, data, status) {
    typing.remove();
    var choice = !err && data && data.choices && data.choices[0];
    var reply  = choice && choice.message ? choice.message.content : null;

    if (typeof reply === "string") {
      reply = reply.trim() || "(The model returned an empty reply.)";
      chatHistory.push({ role: "assistant", content: reply });
      appendChatMessage("assistant", reply);
      setChatDot(true);
    } else {
      // Drop the failed turn from history and hand the text back for a retry
      chatHistory.pop();
      if (!chatInput.value) chatInput.value = text;
      var problem = describeChatError(err, data, status);
      appendChatError(problem.text, problem.llamaOff);
      if (problem.llamaOff) setChatDot(false);
    }
    setChatBusy(false);
  });
});

/* ── I2C: RFID READER ──
   Web version of `xerxes-rfid run` and `manage`. Polls /api/rfid/events
   while the tab is open. */
var rfidRunToggle    = document.getElementById("rfid-run-toggle");
var rfidBadge        = document.getElementById("badge-rfid");
var rfidModuleState  = document.getElementById("rfid-module-state");
var rfidBusDisplay   = document.getElementById("rfid-bus-display");
var rfidLastTag      = document.getElementById("rfid-last-tag");
var rfidTagCount     = document.getElementById("rfid-tag-count");
var rfidRunFeedback  = document.getElementById("rfid-run-feedback");
var rfidLog          = document.getElementById("rfid-log");
var rfidTagList      = document.getElementById("rfid-tag-list");
var rfidTagsEmpty    = document.getElementById("rfid-tags-empty");
var rfidTagsPath     = document.getElementById("rfid-tags-path");
var rfidTagsFeedback = document.getElementById("rfid-tags-feedback");
var rfidNewId        = document.getElementById("rfid-new-id");
var rfidNewCmd       = document.getElementById("rfid-new-cmd");
var rfidScanBtn      = document.getElementById("rfid-scan-btn");
var rfidScanStatus   = document.getElementById("rfid-scan-status");
var rfidScanText     = document.getElementById("rfid-scan-text");
var rfidAddBtn       = document.getElementById("rfid-add-btn");
var rfidAddFeedback  = document.getElementById("rfid-add-feedback");
var rfidLiveBtn      = document.getElementById("rfid-live-btn");
var rfidLiveBadge    = document.getElementById("badge-rfid-live");
var rfidLiveList     = document.getElementById("rfid-live-list");
var rfidLiveEmpty    = document.getElementById("rfid-live-empty");
var rfidLiveFeedback = document.getElementById("rfid-live-feedback");
var rfidSetBus       = document.getElementById("rfid-set-bus");
var rfidSetAddr      = document.getElementById("rfid-set-addr");
var rfidSetTags      = document.getElementById("rfid-set-tags");
var rfidSetFeedback  = document.getElementById("rfid-settings-feedback");

var RFID_LOG_PLACEHOLDER = rfidLog.textContent;
var RFID_MAX_LIVE_ROWS = 50;

var rfidVisible    = false;
var rfidSeq        = 0;       // last event seq received
var rfidPollTimer  = null;
var rfidScanTimer  = null;
var rfidLive       = false;
var rfidModuleError = "";     // set while the RFID module can't be reached
var rfidRunning    = false;
var rfidTags       = [];
var rfidEditingId  = null;
var rfidDefaultTagsPath = "";
var rfidLogEmpty   = true;

function rfidTime(ms) {
  var d = ms ? new Date(ms) : new Date();
  function p(n) { return (n < 10 ? "0" : "") + n; }
  return p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds());
}

function renderRfidReader(s) {
  // Accepts either a /status or an /events response.
  rfidRunning = !!s.run_enabled;
  rfidRunToggle.checked  = rfidRunning;
  rfidRunToggle.disabled = false;

  var mod = s.module || {};
  rfidModuleError = mod.state === "error" ? (mod.error || "RFID module not found.") : "";
  var modText = mod.state === "ok" ? "Connected" : mod.state === "error" ? "Not found" : "Idle";
  rfidModuleState.textContent = modText;
  rfidModuleState.className   = "mini-card-value" + (mod.state === "ok" ? " rfid-ok" : mod.state === "error" ? " rfid-bad" : "");
  rfidModuleState.title       = mod.state === "idle" ? "Not in use: the reader is only polled while it is on, scanning or live scanning." : (mod.error || "");

  if (s.busy_command) {
    setRfidBadge("running command", "badge-neutral");
  } else if (rfidRunning && mod.state === "error") {
    setRfidBadge("error", "badge-failed");
  } else if (rfidRunning) {
    setRfidBadge("running", "badge-active");
  } else {
    setRfidBadge("stopped", "badge-inactive");
  }

  if (mod.state === "error" && mod.error) {
    showFeedback(rfidRunFeedback, "RFID module: " + mod.error, "error");
  } else if (s.busy_command) {
    showFeedback(rfidRunFeedback, "Running: " + s.busy_command, "");
  } else if (rfidRunFeedback.classList.contains("error") && mod.state !== "error") {
    showFeedback(rfidRunFeedback, "", "");
  }

  if (s.last_tag) rfidLastTag.textContent = s.last_tag;
  setRfidLive(typeof s.live_active === "boolean" ? s.live_active : rfidLive);
}

function setRfidBadge(text, cls) {
  rfidBadge.textContent = text;
  rfidBadge.className   = "service-badge " + cls;
}

function loadRfidStatus(fillSettings) {
  llamaRequest("GET", "/api/rfid/status", null, 5000, function (err, data) {
    if (err) { setRfidBadge("unknown", "badge-neutral"); return; }
    renderRfidReader(data);
    rfidBusDisplay.textContent = data.device + " · " + data.address;
    document.getElementById("rfid-version").textContent = "xerxes-rfid v" + data.version;
    rfidDefaultTagsPath = data.default_tags_path || "";
    rfidSetTags.placeholder = rfidDefaultTagsPath || rfidSetTags.placeholder;
    if (fillSettings) {
      rfidSetBus.value  = data.bus;
      rfidSetAddr.value = data.address;
      rfidSetTags.value = data.tags_path;
    }
    if (data.scan && data.scan.state === "waiting" && !rfidScanTimer) rfidPollScan();
  });
}

rfidRunToggle.addEventListener("change", function () {
  var enable = rfidRunToggle.checked;
  rfidRunToggle.disabled = true;
  showFeedback(rfidRunFeedback, enable ? "Starting reader..." : "Stopping reader...", "");
  llamaRequest("POST", "/api/rfid/run", { enabled: enable }, 8000, function (err, data) {
    rfidRunToggle.disabled = false;
    if (err || !data.success) {
      showFeedback(rfidRunFeedback, err || data.error || "Could not change the reader.", "error");
    } else {
      showFeedback(rfidRunFeedback, enable
        ? "Reader on. Tap a registered tag to run its command."
        : "Reader off. Tags will not run commands.", "success");
    }
    loadRfidStatus(false);
    rfidPollEvents();
  });
});

function rfidAppendLog(text) {
  if (rfidLogEmpty) { rfidLog.textContent = ""; rfidLogEmpty = false; }
  var nearBottom = rfidLog.scrollHeight - rfidLog.scrollTop - rfidLog.clientHeight < 40;
  rfidLog.textContent += text;
  if (rfidLog.textContent.length > 60000) rfidLog.textContent = rfidLog.textContent.slice(-50000);
  if (nearBottom) rfidLog.scrollTop = rfidLog.scrollHeight;
}

document.getElementById("rfid-log-clear").addEventListener("click", function () {
  rfidLog.textContent = RFID_LOG_PLACEHOLDER;
  rfidLogEmpty = true;
});

function rfidHandleEvent(ev) {
  var t = "[" + rfidTime(ev.time_ms) + "] ";
  if (ev.kind === "run") {
    if (ev.tags_error) {
      rfidAppendLog(t + ev.id + "\nTag file error, no commands run: " + ev.tags_error + "\n\n");
    } else if (ev.registered) {
      rfidAppendLog(t + ev.id + "\nTag known, running command: " + ev.command + "\n");
    } else {
      rfidAppendLog(t + ev.id + "\nTag unknown, no commands run\n\n");
    }
  } else if (ev.kind === "result") {
    var out = ev.output || "";
    if (out && out.charAt(out.length - 1) !== "\n") out += "\n";
    if (ev.truncated) out += "... (output truncated)\n";
    var secs = (ev.duration_ms / 1000).toFixed(1);
    rfidAppendLog(out + t + "Command finished with exit code " + ev.exit_code + " (" + secs + " s)\n\n");
  } else if (ev.kind === "error") {
    rfidAppendLog(t + "Error: " + ev.message + "\n\n");
  } else if (ev.kind === "info") {
    rfidAppendLog(t + ev.message + "\n\n");
  } else if (ev.kind === "live") {
    rfidAddLiveRow(ev);
  }
}

function rfidPollEvents() {
  clearTimeout(rfidPollTimer);
  if (!rfidVisible) return;
  llamaRequest("GET", "/api/rfid/events?since=" + rfidSeq, null, 5000, function (err, data) {
    if (!err && data && Array.isArray(data.events)) {
      data.events.forEach(rfidHandleEvent);
      if (typeof data.next === "number") rfidSeq = data.next;
      renderRfidReader(data);
    }
    if (rfidVisible) rfidPollTimer = setTimeout(rfidPollEvents, rfidLive ? 500 : 1500);
  });
}

function loadRfidTags() {
  llamaRequest("GET", "/api/rfid/tags", null, 5000, function (err, data) {
    if (err) { showFeedback(rfidTagsFeedback, err, "error"); return; }
    if (data.path) rfidTagsPath.textContent = "Stored in " + data.path + ".";
    if (!data.success) {
      rfidTags = [];
      renderRfidTags();
      rfidTagCount.textContent = "—";
      showFeedback(rfidTagsFeedback, "Error: " + data.error + " The file has not been changed; fix or remove it to manage tags here.", "error");
      return;
    }
    if (rfidTagsFeedback.classList.contains("error")) showFeedback(rfidTagsFeedback, "", "");
    rfidTags = data.tags || [];
    renderRfidTags();
  });
}

function renderRfidTags() {
  rfidTagList.textContent = "";
  rfidTagsEmpty.hidden = rfidTags.length > 0;
  rfidTagList.hidden   = rfidTags.length === 0;
  rfidTagCount.textContent = String(rfidTags.length);

  rfidTags.forEach(function (tag, i) {
    var row = document.createElement("div");
    row.className = "rfid-tag-row";
    row.setAttribute("data-id", tag.id);

    var num = document.createElement("span");
    num.className = "rfid-tag-num";
    num.textContent = (i + 1) + ")";

    var id = document.createElement("span");
    id.className = "rfid-tag-id";
    id.textContent = tag.id;

    var arrow = document.createElement("span");
    arrow.className = "rfid-tag-arrow";
    arrow.textContent = "→";
    arrow.setAttribute("aria-hidden", "true");

    var actions = document.createElement("span");
    actions.className = "rfid-tag-actions";

    row.appendChild(num);
    row.appendChild(id);
    row.appendChild(arrow);

    if (rfidEditingId === tag.id) {
      row.classList.add("editing");
      var input = document.createElement("input");
      input.className = "text-input rfid-tag-cmd-input";
      input.type = "text";
      input.value = tag.command;
      input.maxLength = 1024;
      input.spellcheck = false;
      input.setAttribute("aria-label", "Command for " + tag.id);
      input.addEventListener("keydown", function (e) {
        if (e.key === "Enter") saveRfidEdit(tag.id, input.value, saveBtn);
        if (e.key === "Escape") { rfidEditingId = null; renderRfidTags(); }
      });
      var saveBtn = rfidSmallBtn("Save", "action-btn", function () { saveRfidEdit(tag.id, input.value, saveBtn); });
      var cancelBtn = rfidSmallBtn("Cancel", "secondary-btn", function () { rfidEditingId = null; renderRfidTags(); });
      row.appendChild(input);
      actions.appendChild(saveBtn);
      actions.appendChild(cancelBtn);
      row.appendChild(actions);
      rfidTagList.appendChild(row);
      setTimeout(function () { input.focus(); input.select(); }, 0);
      return;
    }

    var cmd = document.createElement("span");
    cmd.className = "rfid-tag-cmd";
    cmd.textContent = tag.command;
    cmd.title = tag.command;
    row.appendChild(cmd);
    actions.appendChild(rfidSmallBtn("Edit", "secondary-btn", function () {
      rfidEditingId = tag.id;
      showFeedback(rfidTagsFeedback, "", "");
      renderRfidTags();
    }));
    actions.appendChild(rfidSmallBtn("Delete", "secondary-btn rfid-delete-btn", function () { deleteRfidTag(tag); }));
    row.appendChild(actions);
    rfidTagList.appendChild(row);
  });
}

function rfidSmallBtn(label, cls, onClick) {
  var b = document.createElement("button");
  b.type = "button";
  b.className = cls + " rfid-small-btn";
  b.textContent = label;
  b.addEventListener("click", onClick);
  return b;
}

function saveRfidEdit(id, command, btn) {
  command = command.trim();
  if (!command) { showFeedback(rfidTagsFeedback, "Command cannot be empty.", "error"); return; }
  btn.disabled = true;
  llamaRequest("POST", "/api/rfid/tags", { id: id, command: command, mode: "edit" }, 8000, function (err, data) {
    btn.disabled = false;
    if (err || !data.success) {
      showFeedback(rfidTagsFeedback, err || data.error || "Could not update the tag.", "error");
      return;
    }
    rfidEditingId = null;
    showFeedback(rfidTagsFeedback, data.message || ("Tag " + id + " updated."), "success");
    loadRfidTags();
  });
}

function deleteRfidTag(tag) {
  if (!confirm("Delete " + tag.id + " (\"" + tag.command + "\")?")) return;
  llamaRequest("POST", "/api/rfid/tags/delete", { id: tag.id }, 8000, function (err, data) {
    if (err || !data.success) {
      showFeedback(rfidTagsFeedback, err || data.error || "Could not delete the tag.", "error");
    } else {
      if (rfidEditingId === tag.id) rfidEditingId = null;
      showFeedback(rfidTagsFeedback, data.message || ("Tag " + tag.id + " deleted."), "success");
    }
    loadRfidTags();
  });
}

function editRfidTagInstead(id) {
  rfidEditingId = id;
  renderRfidTags();
  var row = rfidTagList.querySelector('.rfid-tag-row[data-id="' + id + '"]');
  if (row) row.scrollIntoView({ behavior: "smooth", block: "center" });
}

document.getElementById("rfid-tags-refresh").addEventListener("click", function () {
  showFeedback(rfidTagsFeedback, "", "");
  loadRfidTags();
});

function showRfidAlreadyRegistered(id, existing) {
  rfidAddFeedback.textContent = "";
  rfidAddFeedback.className = "control-feedback error";
  rfidAddFeedback.appendChild(document.createTextNode(
    "Tag " + id + " is already registered with command: \"" + existing + "\" "));
  var btn = document.createElement("button");
  btn.type = "button";
  btn.className = "clear-btn rfid-inline-btn";
  btn.textContent = "Edit it instead";
  btn.addEventListener("click", function () {
    showFeedback(rfidAddFeedback, "", "");
    rfidNewId.value = "";
    rfidNewCmd.value = "";
    editRfidTagInstead(id);
  });
  rfidAddFeedback.appendChild(btn);
}

function setRfidScanning(on, text) {
  rfidScanStatus.hidden = !on;
  rfidScanBtn.disabled  = on;
  rfidScanBtn.textContent = on ? "Scanning..." : "Scan Tag";
  if (text) rfidScanText.textContent = text;
}

rfidScanBtn.addEventListener("click", function () {
  showFeedback(rfidAddFeedback, "", "");
  setRfidScanning(true, "Place an RFID tag near the Xerxes Pi...");
  llamaRequest("POST", "/api/rfid/scan/start", null, 5000, function (err, data) {
    if (err || !data.success) {
      setRfidScanning(false);
      showFeedback(rfidAddFeedback, err || data.error || "Could not start scanning.", "error");
      return;
    }
    rfidPollScan();
  });
});

function rfidPollScan() {
  clearTimeout(rfidScanTimer);
  setRfidScanning(true);
  llamaRequest("GET", "/api/rfid/scan", null, 5000, function (err, data) {
    rfidScanTimer = null;
    if (err) { setRfidScanning(false); showFeedback(rfidAddFeedback, err, "error"); return; }

    if (data.state === "waiting") {
      var mod = data.module || {};
      if (mod.state === "error") {
        rfidScanText.textContent = "Can't reach the RFID module: " + mod.error + " (retrying, " + data.seconds_left + " s left)";
      } else {
        rfidScanText.textContent = "Place an RFID tag near the Xerxes Pi... (" + data.seconds_left + " s)";
      }
      rfidScanTimer = setTimeout(rfidPollScan, 400);
      return;
    }
    setRfidScanning(false);
    if (data.state === "done") {
      rfidNewId.value = data.id;
      rfidLastTag.textContent = data.id;
      if (data.registered) {
        showRfidAlreadyRegistered(data.id, data.existing_command);
      } else {
        showFeedback(rfidAddFeedback, "Scanned: " + data.id + (data.type ? " (" + data.type + ")" : "") +
                     ". Enter the command to run, then click Add Tag.", "success");
        rfidNewCmd.focus();
      }
    } else if (data.state === "timeout") {
      showFeedback(rfidAddFeedback, "No tag detected. Hold the tag flat against the reader and try again.", "error");
    } else if (data.state === "cancelled") {
      showFeedback(rfidAddFeedback, "Scan cancelled.", "");
    }
  });
}

document.getElementById("rfid-scan-cancel").addEventListener("click", function () {
  clearTimeout(rfidScanTimer);
  rfidScanTimer = null;
  llamaRequest("POST", "/api/rfid/scan/cancel", null, 5000, function () {
    setRfidScanning(false);
    showFeedback(rfidAddFeedback, "Scan cancelled.", "");
  });
});

function addRfidTag() {
  var id  = rfidNewId.value.trim();
  var cmd = rfidNewCmd.value.trim();
  if (!id)  { showFeedback(rfidAddFeedback, "Scan a tag or enter its ID first.", "error"); rfidNewId.focus(); return; }
  if (!cmd) { showFeedback(rfidAddFeedback, "Command cannot be empty.", "error"); rfidNewCmd.focus(); return; }

  rfidAddBtn.disabled = true;
  llamaRequest("POST", "/api/rfid/tags", { id: id, command: cmd, mode: "add" }, 8000, function (err, data, status) {
    rfidAddBtn.disabled = false;
    if (!err && status === 409 && data.existing_command !== undefined) {
      showRfidAlreadyRegistered(data.id, data.existing_command);
      return;
    }
    if (err || !data.success) {
      showFeedback(rfidAddFeedback, err || data.error || "Could not add the tag.", "error");
      return;
    }
    rfidNewId.value = "";
    rfidNewCmd.value = "";
    showFeedback(rfidAddFeedback, data.message || "Tag added.", "success");
    loadRfidTags();
  });
}

rfidAddBtn.addEventListener("click", addRfidTag);
rfidNewCmd.addEventListener("keydown", function (e) { if (e.key === "Enter") addRfidTag(); });

function setRfidLive(on) {
  rfidLive = on;
  var failing = on && rfidModuleError;
  rfidLiveBtn.textContent = on ? "Stop Live Scan" : "Start Live Scan";
  rfidLiveBadge.textContent = failing ? "error" : on ? "scanning" : "off";
  rfidLiveBadge.className = "service-badge " + (failing ? "badge-failed" : on ? "badge-active" : "badge-inactive");
  showFeedback(rfidLiveFeedback, failing ? "Not found: " + rfidModuleError : "", failing ? "error" : "");
  if (rfidLiveList.childElementCount === 0) {
    rfidLiveEmpty.textContent = failing ? "No tags can be read until the RFID module is connected."
      : on ? "Live scan running: place tags near the reader."
      : "Start a live scan, then place tags near the reader.";
  }
}

rfidLiveBtn.addEventListener("click", function () {
  var start = !rfidLive;
  rfidLiveBtn.disabled = true;
  llamaRequest("POST", start ? "/api/rfid/live/start" : "/api/rfid/live/stop", null, 5000, function (err, data) {
    rfidLiveBtn.disabled = false;
    if (err || !data.success) { alert(err || data.error || "Could not change live scan."); return; }
    setRfidLive(start);
    rfidPollEvents();
  });
});

document.getElementById("rfid-live-clear").addEventListener("click", function () {
  rfidLiveList.textContent = "";
  rfidLiveList.hidden = true;
  rfidLiveEmpty.hidden = false;
  setRfidLive(rfidLive);
});

function rfidAddLiveRow(ev) {
  rfidLiveEmpty.hidden = true;
  rfidLiveList.hidden  = false;

  var row = document.createElement("div");
  row.className = "rfid-live-row";

  var head = document.createElement("div");
  head.className = "rfid-live-head";
  var time = document.createElement("span");
  time.className = "rfid-live-time";
  time.textContent = rfidTime(ev.time_ms);
  var id = document.createElement("span");
  id.className = "rfid-tag-id";
  id.textContent = ev.id;
  var type = document.createElement("span");
  type.className = "service-badge badge-neutral";
  type.textContent = ev.type || "tag";
  var status = document.createElement("span");
  status.className = "rfid-live-status " + (ev.registered ? "registered" : "unregistered");
  status.textContent = ev.registered ? "registered → \"" + ev.command + "\"" : "not registered";
  head.appendChild(time);
  head.appendChild(id);
  head.appendChild(type);
  head.appendChild(status);

  var detail = document.createElement("div");
  detail.className = "rfid-live-detail";
  detail.textContent = "detail: " + ev.detail + (ev.tags_error ? "\ntag file error: " + ev.tags_error : "");

  row.appendChild(head);
  row.appendChild(detail);
  rfidLiveList.insertBefore(row, rfidLiveList.firstChild);
  while (rfidLiveList.childElementCount > RFID_MAX_LIVE_ROWS) rfidLiveList.removeChild(rfidLiveList.lastChild);
  rfidLastTag.textContent = ev.id;
}

document.getElementById("rfid-settings-save").addEventListener("click", function () {
  var btn  = this;
  var bus  = rfidSetBus.value.trim();
  var addr = rfidSetAddr.value.trim();
  if (!/^\d{1,4}$/.test(bus)) { showFeedback(rfidSetFeedback, "I2C bus must be a number, e.g. 1 for /dev/i2c-1.", "error"); return; }
  if (!/^(0x[0-9a-fA-F]{1,2}|\d{1,3})$/.test(addr)) { showFeedback(rfidSetFeedback, "Address must look like 0x2C.", "error"); return; }
  btn.disabled = true;
  llamaRequest("POST", "/api/rfid/settings",
    { bus: parseInt(bus, 10), address: addr, tags_path: rfidSetTags.value.trim() }, 8000,
    function (err, data) {
      btn.disabled = false;
      if (err || !data.success) { showFeedback(rfidSetFeedback, err || data.error || "Could not save settings.", "error"); return; }
      showFeedback(rfidSetFeedback, "Settings saved.", "success");
      loadRfidStatus(true);
      loadRfidTags();
    });
});

document.getElementById("rfid-settings-defaults").addEventListener("click", function () {
  rfidSetBus.value  = "1";
  rfidSetAddr.value = "0x2C";
  rfidSetTags.value = rfidDefaultTagsPath;
  showFeedback(rfidSetFeedback, "Defaults filled in. Click Save to apply.", "");
});

document.getElementById("rfid-probe-btn").addEventListener("click", function () {
  var btn = this;
  btn.disabled = true;
  showFeedback(rfidSetFeedback, "Checking the RFID module...", "");
  llamaRequest("POST", "/api/rfid/probe", null, 10000, function (err, data) {
    btn.disabled = false;
    if (err) showFeedback(rfidSetFeedback, err, "error");
    else if (!data.success) showFeedback(rfidSetFeedback, "Not found: " + data.error, "error");
    else showFeedback(rfidSetFeedback, data.message, "success");
    loadRfidStatus(false);
  });
});

function rfidOnShow() {
  if (rfidVisible) return;
  rfidVisible = true;
  loadRfidStatus(true);
  loadRfidTags();
  rfidPollEvents();
}

function rfidOnHide() {
  if (!rfidVisible) return;
  rfidVisible = false;
  clearTimeout(rfidPollTimer);
  // Leaving the tab ends live scan and any pending scan, like Ctrl+C in `manage`
  if (rfidLive) { llamaRequest("POST", "/api/rfid/live/stop", null, 5000, function () {}); setRfidLive(false); }
  if (rfidScanTimer) {
    clearTimeout(rfidScanTimer);
    rfidScanTimer = null;
    llamaRequest("POST", "/api/rfid/scan/cancel", null, 5000, function () {});
    setRfidScanning(false);
  }
}

window.addEventListener("pagehide", function () {
  if (rfidLive && navigator.sendBeacon) navigator.sendBeacon(API_BASE + "/api/rfid/live/stop");
  if (rfidScanTimer && navigator.sendBeacon) navigator.sendBeacon(API_BASE + "/api/rfid/scan/cancel");
});

/* ── INIT ── */
loadLang();
loadDashboard();
loadServices();
loadNewtStatus();
