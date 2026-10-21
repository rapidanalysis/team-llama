/* ============================================================
   XERXES PI ADMIN — app.js
   Handles: localisation, theme, status check, tab switching,
            output popup, system commands, network info & config,
            hostname rename, Tailscale dashboard, ngrok dashboard,
            service badges, reboot/shutdown, Llama (local LLM).
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
    if (target === "newt")    loadNewtStatus();
    if (target === "llama")   { loadLlamaStatus(); loadLlamaModels(); }
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
        updateBadge("badge-newt",      data.newt);
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
// Only buttons that carry a data-command. Docker buttons and the Newt Disable
// button have their own handlers and must not also post an empty command here.
document.querySelectorAll(".cmd-btn[data-command]:not(.sys-cmd)").forEach(function (btn) {
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

/* ── NEWT (PANGOLIN) DASHBOARD ── */
function loadNewtStatus() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", API_BASE + "/api/newt/status", true);
  xhr.timeout = 5000;
  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      document.getElementById("newt-endpoint").textContent = data.endpoint || "—";
      document.getElementById("newt-id").textContent       = data.id || "—";
      document.getElementById("newt-state").textContent    =
        data.state === "not_installed" ? "not set up" : (data.state || "—");
    } catch (e) {}
  };
  xhr.send();
}

document.getElementById("newt-enable-btn").addEventListener("click", function () {
  var endpoint = document.getElementById("newt-endpoint-input").value.trim();
  var id       = document.getElementById("newt-id-input").value.trim();
  var secretEl = document.getElementById("newt-secret-input");
  var secret   = secretEl.value.trim();
  var feedback = document.getElementById("newt-feedback");
  var btn      = this;

  // Same rules as the backend; the backend checks again.
  if (!/^https:\/\/[A-Za-z0-9.\-]+(:[0-9]{1,5})?\/?$/.test(endpoint)) {
    showFeedback(feedback, "Endpoint must look like https://pangolin.example.com", "error");
    return;
  }
  if (!/^[A-Za-z0-9\-_]{1,128}$/.test(id) || !/^[A-Za-z0-9\-_]{1,128}$/.test(secret)) {
    showFeedback(feedback, "Site ID and secret may only contain letters, numbers, hyphens and underscores.", "error");
    return;
  }

  showOutputPopup("Newt — Enable", "Starting Newt...");
  btn.disabled    = true;
  btn.textContent = "Enabling...";
  showFeedback(feedback, "", "");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/newt/up", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  // The first start pulls the image, which can take a while on a slow link.
  xhr.timeout = 120000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.success ? (data.output || "Newt started.")
                                  : ((data.error || "Failed.") + (data.output ? "\n\n" + data.output : "")));
      showFeedback(feedback, data.success ? "Newt started." : (data.error || "Failed."),
                   data.success ? "success" : "error");
      if (data.success) secretEl.value = "";  // do not leave the secret in the page
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled    = false;
    btn.textContent = "Enable";
    loadServices();
    loadNewtStatus();
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach backend."); btn.disabled = false; btn.textContent = "Enable"; };
  xhr.ontimeout = function () { setPopupOutput("Error: Request timed out. The image may still be downloading; check View Logs."); btn.disabled = false; btn.textContent = "Enable"; };
  xhr.send(JSON.stringify({ endpoint: endpoint, id: id, secret: secret }));
});

document.getElementById("newt-disable-btn").addEventListener("click", function () {
  if (!confirm("Disable Newt? This stops the tunnel and deletes the stored site secret.")) return;
  var btn = this;
  showOutputPopup("Newt — Disable", "Stopping Newt...");
  btn.disabled = true;

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/newt/down", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 30000;
  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.output || "Done.");
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled = false;
    loadServices();
    loadNewtStatus();
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach backend."); btn.disabled = false; };
  xhr.ontimeout = function () { setPopupOutput("Error: Request timed out.");       btn.disabled = false; };
  xhr.send("{}");
});

document.getElementById("newt-update-btn").addEventListener("click", function () {
  var btn = this;
  showOutputPopup("Newt — Update", "Checking for a newer Newt image...");
  btn.disabled = true;

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/newt/update", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  // Pulling the image can take a few minutes on a slow link.
  xhr.timeout = 300000;
  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.success ? (data.output || "Done.")
                                  : ((data.error || "Update failed.") + (data.output ? "\n\n" + data.output : "")));
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled = false;
    loadServices();
    loadNewtStatus();
  };
  xhr.onerror   = function () { setPopupOutput("Error: Could not reach backend."); btn.disabled = false; };
  xhr.ontimeout = function () { setPopupOutput("Error: Request timed out. The image may still be downloading; check View Logs."); btn.disabled = false; };
  xhr.send("{}");
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

/* ── INIT ── */
loadLang();
loadDashboard();
loadServices();
