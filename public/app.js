/* ============================================================
   XERXES PI ADMIN — app.js
   Handles: localisation, theme, status check, tab switching,
            output popup, system commands, network info & config,
            hostname rename, Tailscale dashboard, ngrok dashboard,
            service badges, reboot/shutdown.
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
    if (target === "tunnels") loadTailscaleDetails();
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
document.querySelectorAll(".cmd-btn:not(.sys-cmd)").forEach(function (btn) {
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
        dockerOutput.textContent = data.error || data.output || xhr.responseText;
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

/* ── INIT ── */
loadLang();
loadDashboard();
loadServices();
