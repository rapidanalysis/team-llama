/* ============================================================
   XERXES PI ADMIN — app.js
   Handles: localisation, theme, status check, tab switching,
            output popup, system commands, network info & config,
            hostname rename, Tailscale dashboard, ngrok dashboard,
            service badges, reboot/shutdown, Llama (local LLM),
            I2C (OLED display, PiicoDev RFID reader).
   ============================================================ */

var API_BASE = "";  // same origin — served by the C++ backend

/* ── LOCALISATION ──
   Strings come from public/i18n/<lang>.json via i18n.js (t, I18N).
   UI text is written with I18N.setText(el, key, params) so it re-translates
   when the language changes. Text from the device itself (command output,
   backend error messages) is shown as-is.

   msg(key, params) wraps a translatable message so helpers such as
   showFeedback() and setPopupOutput() can accept either one of these or a
   plain string. */
function msg(key, params) { return { i18nKey: key, params: params || null }; }

function setMsg(el, m) {
  if (m && typeof m === "object" && m.i18nKey) {
    I18N.setText(el, m.i18nKey, m.params);
  } else {
    I18N.clear(el);
    el.textContent = m == null ? "" : m;
  }
}

function msgText(m) {
  return (m && typeof m === "object" && m.i18nKey) ? t(m.i18nKey, m.params) : (m == null ? "" : String(m));
}

// Status words that come from systemd / Docker etc. Known ones are translated;
// anything else is shown exactly as the device reported it.
function setStatusBadge(el, status, cls) {
  var key = "badge_" + String(status || "unknown").replace(/[^a-z0-9]+/gi, "_").toLowerCase();
  if (t(key) !== key) I18N.setText(el, key);
  else setMsg(el, status);
  if (cls !== undefined) el.className = "service-badge " + cls;
}

/* ── THEME TOGGLE ── */
var themeToggle = document.getElementById("theme-toggle");
var themeLabel  = document.getElementById("theme-label");
var html        = document.documentElement;

function readPref(key, def) { try { return localStorage.getItem(key) || def; } catch (e) { return def; } }
function writePref(key, val) { try { localStorage.setItem(key, val); } catch (e) {} }

function setThemeLabel(theme) {
  I18N.setText(themeLabel, theme === "dark" ? "toggle_light" : "toggle_dark");
}

var savedTheme = readPref("xerxes-theme", "light");
html.setAttribute("data-theme", savedTheme);
setThemeLabel(savedTheme);

themeToggle.addEventListener("click", function () {
  var current = html.getAttribute("data-theme");
  var next    = current === "dark" ? "light" : "dark";
  html.setAttribute("data-theme", next);
  writePref("xerxes-theme", next);
  setThemeLabel(next);
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
      statusDot.className = "status-dot online";
      I18N.setText(statusLabel, "status_online");
    } else { setOffline(); }
  };
  xhr.onerror   = setOffline;
  xhr.ontimeout = setOffline;
  xhr.send();
}

function setOffline() {
  statusDot.className = "status-dot offline";
  I18N.setText(statusLabel, "status_offline");
}

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
    if (target === "llama")   { loadLlamaStatus(); loadLlamaModels(); loadLlamaRecommended(); }
    if (target === "i2c") { rfidOnShow(); oledOnShow(); } else { rfidOnHide(); oledOnHide(); }
  });
});

/* ── OUTPUT POPUP ── */
var outputPopup     = document.getElementById("output-popup");
var popupBody       = document.getElementById("popup-body");
var popupTitle      = document.getElementById("popup-title");
var closeAnimHandler = null;          // tracks in-flight close animation handler
var consoleHistory  = null;          // last real output for Console button (null = nothing yet)

// Sets popup output AND saves it to the console history log.
// text: a string (command output) or msg(key, params).
function setPopupOutput(text) {
  setMsg(popupBody, text);
  if (text) consoleHistory = text;
}

// title / text: a string or msg(key, params). Progress text shown here
// ("Running...") is deliberately not saved to the console history.
function showOutputPopup(title, text) {
  // If a close animation is mid-flight, cancel it before re-opening so the
  // stale handler doesn't fire on this open animation's animationend
  if (closeAnimHandler) {
    outputPopup.removeEventListener("animationend", closeAnimHandler);
    closeAnimHandler = null;
  }
  outputPopup.classList.remove("popup-closing");
  setMsg(popupTitle, title || msg("output_title"));
  setMsg(popupBody,  text  || msg("common_running"));
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
  showOutputPopup(msg("popup_console"), consoleHistory || msg("console_empty"));
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
  setStatusBadge(el, status || "unknown",
    status === "active" ? "badge-active" : status === "failed" ? "badge-failed" : "badge-inactive");
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
  I18N.setText(outputBody, "output_running_cmd", { command: btn.textContent });

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 15000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setMsg(outputBody, data.output || data.message || xhr.responseText);
    } catch (e) { setMsg(outputBody, xhr.responseText); }
    btn.disabled = false;
    btn.classList.remove("running");
  };
  xhr.onerror   = function () { I18N.setText(outputBody, "err_backend_unreachable"); btn.disabled = false; btn.classList.remove("running"); };
  xhr.ontimeout = function () { I18N.setText(outputBody, "err_timeout");             btn.disabled = false; btn.classList.remove("running"); };
  xhr.send(JSON.stringify({ command: command }));
}

document.getElementById("clear-output").addEventListener("click", function () {
  I18N.setText(outputBody, "output_placeholder");
});

/* ── GENERIC CMD-BTN (non-system — shows output popup) ── */
document.querySelectorAll(".cmd-btn:not(.sys-cmd):not(.newt-btn)").forEach(function (btn) {
  btn.addEventListener("click", function () {
    runPopupCommand(btn.getAttribute("data-command"), btn);
  });
});

function runPopupCommand(command, btn) {
  // Popup title: "<card title> — <button>", e.g. "SMB File Sharing — Start"
  var card  = btn.closest(".control-card");
  var head  = card && card.querySelector(".control-card-title");
  var label = (head ? head.textContent.trim() + " — " : "") + btn.textContent.trim();
  showOutputPopup(label, msg("common_running"));
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
  xhr.onerror   = function () { setPopupOutput(msg("err_backend_unreachable")); btn.disabled = false; btn.classList.remove("running"); };
  xhr.ontimeout = function () { setPopupOutput(msg("err_timeout"));             btn.disabled = false; btn.classList.remove("running"); };
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
          I18N.setText(opt, "net_no_interfaces");
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
    showFeedback(feedback, msg("net_err_no_iface"), "error");
    return;
  }

  var body = { interface: iface, mode: mode };

  if (mode === "static") {
    body.ip      = document.getElementById("net-static-ip").value.trim();
    body.gateway = document.getElementById("net-static-gateway").value.trim();
    body.dns     = document.getElementById("net-static-dns").value.trim();

    if (!body.ip || !body.gateway) {
      showFeedback(feedback, msg("net_err_static_required"), "error");
      return;
    }
  }

  btn.disabled = true;
  I18N.setText(btn, "net_applying");
  showFeedback(feedback, "", "");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/network", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 10000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.status === "ok") {
        showFeedback(feedback, data.output || msg("net_success_default"), "success");
      } else {
        showFeedback(feedback, data.message || msg("net_fail_default"), "error");
      }
    } catch (e) {
      showFeedback(feedback, msg("err_unexpected"), "error");
    }
    btn.disabled = false;
    I18N.setText(btn, "ctrl_ipconfig_btn");
  };
  xhr.onerror   = function () { showFeedback(feedback, msg("err_backend_unreachable"), "error"); btn.disabled = false; I18N.setText(btn, "ctrl_ipconfig_btn"); };
  xhr.ontimeout = function () { showFeedback(feedback, msg("err_timeout"), "error");             btn.disabled = false; I18N.setText(btn, "ctrl_ipconfig_btn"); };
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
    showFeedback(hostnameFeedback, msg("hostname_prefilled"), "");
  } else {
    showFeedback(hostnameFeedback, msg("hostname_read_fail"), "error");
  }
});

hostnameBtn.addEventListener("click", function () {
  var newName = hostnameInput.value.trim();
  if (!newName) {
    showFeedback(hostnameFeedback, msg("hostname_empty"), "error");
    return;
  }

  hostnameBtn.disabled = true;
  I18N.setText(hostnameBtn, "hostname_renaming");
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
        showFeedback(hostnameFeedback, msg("hostname_success"), "success");
        hostnameInput.value = "";
        loadDashboard();
      }
    } catch (e) {
      showFeedback(hostnameFeedback, msg("err_unexpected"), "error");
    }
    hostnameBtn.disabled = false;
    I18N.setText(hostnameBtn, "ctrl_hostname_btn");
  };
  xhr.onerror   = function () { showFeedback(hostnameFeedback, msg("err_backend_unreachable"), "error"); hostnameBtn.disabled = false; I18N.setText(hostnameBtn, "ctrl_hostname_btn"); };
  xhr.ontimeout = function () { showFeedback(hostnameFeedback, msg("err_timeout"), "error");             hostnameBtn.disabled = false; I18N.setText(hostnameBtn, "ctrl_hostname_btn"); };
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
    showFeedback(feedback, msg("ts_err_authkey"), "error");
    return;
  }

  showOutputPopup(msg("ts_popup_title"), msg("ts_enabling_output"));
  btn.disabled = true;
  I18N.setText(btn, "common_enabling");
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
    btn.disabled = false;
    I18N.setText(btn, "ctrl_tailscale_up");
    loadServices();
    loadTailscaleDetails();
  };
  xhr.onerror   = function () { setPopupOutput(msg("err_backend_unreachable")); btn.disabled = false; I18N.setText(btn, "ctrl_tailscale_up"); };
  xhr.ontimeout = function () { setPopupOutput(msg("ts_timeout"));              btn.disabled = false; I18N.setText(btn, "ctrl_tailscale_up"); };
  xhr.send(JSON.stringify({ authkey: authkey }));
});

/* ── NGROK DASHBOARD ── */
document.getElementById("ngrok-start-btn").addEventListener("click", function () {
  var command   = document.getElementById("ngrok-port-select").value;
  var portLabel = document.getElementById("ngrok-port-select").selectedOptions[0].textContent;
  // Extract the numeric port from the command key e.g. "ngrok_start_8080" → "8080"
  var portNum   = command.replace("ngrok_start_", "");
  var btn = this;

  showOutputPopup(msg("ngrok_popup_title"), msg("ngrok_starting_output", { port: portLabel }));
  btn.disabled = true;
  I18N.setText(btn, "common_starting");

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 15000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setPopupOutput(data.output || data.message || xhr.responseText);
    } catch (e) { setPopupOutput(xhr.responseText); }
    btn.disabled = false;
    I18N.setText(btn, "ctrl_ngrok_start");
    loadServices();

    // Show searching state while we wait for ngrok to fully connect
    I18N.setText(document.getElementById("ngrok-url"), "ngrok_searching");
    document.getElementById("ngrok-port-display").textContent = portNum;
    // ngrok takes several seconds to establish the tunnel and start its local API
    // on :4040, so we start after 3 s and retry up to 4 times every 3 s.
    setTimeout(function () { fetchNgrokUrl(portNum, 1); }, 3000);
  };
  xhr.onerror   = function () { setPopupOutput(msg("err_backend_unreachable")); btn.disabled = false; I18N.setText(btn, "ctrl_ngrok_start"); };
  xhr.ontimeout = function () { setPopupOutput(msg("err_timeout"));             btn.disabled = false; I18N.setText(btn, "ctrl_ngrok_start"); };
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
        setMsg(document.getElementById("ngrok-url"), match[1]);
        document.getElementById("ngrok-port-display").textContent = localPort || "—";
      } else if (attempt < maxAttempts) {
        // Neither source had a URL yet — retry
        setTimeout(function () { fetchNgrokUrl(localPort, attempt + 1); }, 3000);
      } else {
        // All retries exhausted — tell user to click Check Status for details
        I18N.setText(document.getElementById("ngrok-url"), "ngrok_not_found");
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

var newtHasSecret    = false;   // a secret is saved on the device
var newtLastStatus   = null;    // last /api/newt/status reply, to re-render dates on a language switch

// badge / label are translation keys (badge_* and newt_state_*)
var NEWT_STATES = {
  connected:          { badge: "badge_connected",    cls: "badge-active",   label: "newt_state_connected" },
  connecting:         { badge: "badge_connecting",   cls: "badge-neutral",  label: "newt_state_connecting" },
  auth_failed:        { badge: "badge_rejected",     cls: "badge-failed",   label: "newt_state_auth_failed" },
  error:              { badge: "badge_error",        cls: "badge-failed",   label: "newt_state_error" },
  stopped:            { badge: "badge_stopped",      cls: "badge-inactive", label: "newt_state_stopped" },
  not_configured:     { badge: "badge_not_set_up",   cls: "badge-inactive", label: "newt_state_not_configured" },
  docker_unavailable: { badge: "badge_docker_error", cls: "badge-failed",   label: "newt_state_docker_unavailable" }
};

// Plain-language hints (translation keys) for states that need the user to do something
var NEWT_HINTS = {
  auth_failed:        "newt_hint_auth_failed",
  error:              "newt_hint_error",
  docker_unavailable: "newt_hint_docker_unavailable"
};

function renderNewtStatus(data) {
  var st = NEWT_STATES[data.state];
  if (st) {
    I18N.setText(newtBadge, st.badge);
    newtBadge.className = "service-badge " + st.cls;
    I18N.setText(newtStateEl, st.label);
  } else {
    setStatusBadge(newtBadge, data.state || "unknown", "badge-neutral");
    setMsg(newtStateEl, data.state || msg("newt_state_unknown"));
  }
  newtEndpointShow.textContent = data.endpoint || "—";
  newtIdShow.textContent       = data.newt_id  || "—";
  newtVersionShow.textContent  = data.version  || "—";

  // Pre-fill the form with the saved settings (never the secret)
  if (!newtEndpointIn.value && data.endpoint) newtEndpointIn.value = data.endpoint;
  if (!newtIdIn.value && data.newt_id)        newtIdIn.value       = data.newt_id;
  newtHasSecret = !!data.has_secret;
  newtSecretIn.setAttribute("data-i18n-placeholder", newtHasSecret ? "newt_secret_saved_placeholder" : "newt_secret_placeholder");
  newtSecretIn.placeholder = t(newtSecretIn.getAttribute("data-i18n-placeholder"));

  if (NEWT_HINTS[data.state]) {
    showFeedback(newtFeedback, data.detail
      ? msg("newt_hint_with_detail", { hint: msg(NEWT_HINTS[data.state]), detail: data.detail })
      : msg(NEWT_HINTS[data.state]), "error");
  } else if (data.state === "connected") {
    showFeedback(newtFeedback, msg("newt_connected_msg"), "success");
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
      setStatusBadge(newtBadge, "unknown", "badge-neutral");
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
      showFeedback(newtFeedback, msg("newt_still_connecting"), "error");
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

  setMsg(newtFixMessage, data.detail
    ? msg("newt_docker_said", { message: data.message, detail: data.detail })
    : data.message);
  newtFixOptions.textContent = "";

  (data.fixes || []).forEach(function (fix, i) {
    var opt = document.createElement("div");
    opt.className = "newt-fix-option";

    var title = document.createElement("div");
    title.className   = "newt-fix-option-title";
    setMsg(title, data.fixes.length > 1 ? msg("newt_fix_option", { n: i + 1, title: fix.title }) : fix.title);
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
    I18N.setText(copy, "common_copy");
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
    I18N.setText(btn, "common_press_ctrl_c");
  }
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text).then(function () { I18N.setText(btn, "common_copied"); }, selected);
  } else {
    selected();
  }
  setTimeout(function () { I18N.setText(btn, "common_copy"); }, 2500);
}

document.getElementById("newt-fix-recheck").addEventListener("click", function () {
  var btn = this;
  btn.disabled = true;
  loadNewtDockerFix(function (state) {
    btn.disabled = false;
    if (state === "ok") {
      showFeedback(newtFeedback, msg("newt_docker_ok"), "success");
      loadNewtStatus();
    }
  });
});

/* Enable: checks Docker access and other copies of Newt (D-05) first */
function enableNewt(force) {
  var endpoint = newtEndpointIn.value.trim();
  var newtId   = newtIdIn.value.trim();
  var secret   = newtSecretIn.value.trim();
  var hasSaved = newtHasSecret;

  if (!/^https?:\/\/[A-Za-z0-9][A-Za-z0-9.\-:\/_]*$/.test(endpoint)) {
    showFeedback(newtFeedback, msg("newt_err_endpoint"), "error");
    return;
  }
  if (!/^[A-Za-z0-9]{1,64}$/.test(newtId)) {
    showFeedback(newtFeedback, msg("newt_err_id"), "error");
    return;
  }
  if (secret ? !/^[A-Za-z0-9]{1,128}$/.test(secret) : !hasSaved) {
    showFeedback(newtFeedback, msg(secret ? "newt_err_secret_chars" : "newt_err_secret_missing"), "error");
    return;
  }

  clearTimeout(newtPollTimer);
  newtEnableBtn.disabled = true;
  I18N.setText(newtEnableBtn, "common_enabling");
  showFeedback(newtFeedback, msg("newt_starting"), "");

  llamaRequest("POST", "/api/newt/enable",
    { endpoint: endpoint, newt_id: newtId, secret: secret, force: !!force }, 300000,
    function (err, data) {
      newtEnableBtn.disabled = false;
      I18N.setText(newtEnableBtn, "ctrl_newt_enable");
      if (err) { showFeedback(newtFeedback, err, "error"); return; }

      if (data.code === "conflict") {
        var list = (data.conflicts || []).map(function (c) {
          return "  • " + c.name + " (" + c.detail + ")";
        }).join("\n");
        var ok = confirm(t("newt_conflict_confirm", { list: list }));
        if (ok) enableNewt(true);
        else showFeedback(newtFeedback, msg("newt_conflict_cancelled"), "error");
        return;
      }
      if (data.code === "docker_access") {
        showFeedback(newtFeedback, msg("newt_docker_access", { error: data.error }), "error");
        loadNewtDockerFix();
        return;
      }
      if (!data.success) {
        showFeedback(newtFeedback, data.error || msg("newt_start_failed"), "error");
        if (data.output) showOutputPopup(msg("newt_popup_enable"), data.output);
        return;
      }
      newtSecretIn.value = "";
      newtFixBox.hidden  = true;
      showFeedback(newtFeedback, msg("newt_started_waiting"), "");
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
    else if (!data.success) showFeedback(newtFeedback, data.error || msg("newt_stop_failed"), "error");
    else                   showFeedback(newtFeedback, msg("newt_stopped_msg"), "success");
    if (data && data.code === "docker_access") loadNewtDockerFix();
    loadNewtStatus();
  });
});

document.getElementById("newt-status-btn").addEventListener("click", function () {
  showFeedback(newtFeedback, "", "");
  loadNewtStatus();
});

document.getElementById("newt-logs-btn").addEventListener("click", function () {
  showOutputPopup(msg("newt_popup_logs"), msg("common_loading"));
  llamaRequest("GET", "/api/newt/logs", null, 10000, function (err, data) {
    if (err) { setPopupOutput(err); return; }
    setPopupOutput(data.output && data.output.indexOf("No such container") === -1
      ? data.output
      : msg("newt_no_logs"));
  });
});

/* Updates: version, daily automatic update, manual update and roll back */
function formatNewtCheckTime(unixSeconds) {
  if (!unixSeconds) return msg("newt_never");
  var d = new Date(unixSeconds * 1000);
  try { return d.toLocaleString(I18N.lang(), { dateStyle: "medium", timeStyle: "short" }); }
  catch (e) { return d.toLocaleString(); }
}

function renderNewtUpdates(data) {
  var configured = data.state !== "not_configured";
  var running    = data.container === "running";

  newtConfigured = configured;
  setNewtAutoToggleReady(configured);
  newtAutoToggle.checked = configured && data.auto_update !== false;
  newtLastStatus = data;
  setMsg(newtUpdateSummary, !configured ? msg("newt_setup_first_updates")
    : data.last_result
      ? msg("newt_last_checked_result", { time: formatNewtCheckTime(data.last_check), result: data.last_result })
      : msg("newt_last_checked", { time: formatNewtCheckTime(data.last_check) }));

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
  var wrap = newtAutoToggle.closest(".newt-auto");
  if (ready) { wrap.removeAttribute("data-i18n-title"); wrap.title = ""; }
  else       { wrap.setAttribute("data-i18n-title", "newt_setup_first"); wrap.title = t("newt_setup_first"); }
  if (!ready) newtAutoToggle.checked = false;
}

// labelKey: translation key shown on btn while busy
function setNewtUpdateBusy(busy, btn, labelKey) {
  newtUpdateBusy = busy;
  [newtCheckBtn, newtUpdateBtn, newtRollbackBtn].forEach(function (b) { b.disabled = busy; });
  if (busy) {
    btn.dataset.idleKey = btn.getAttribute("data-i18n") || "";
    I18N.setText(btn, labelKey);
    btn.classList.add("running");
  } else {
    if (btn.dataset.idleKey) I18N.setText(btn, btn.dataset.idleKey);
    btn.classList.remove("running");
  }
}

// Runs one update action and shows its result. The tunnel may drop while
// Newt restarts; if this page is open through Pangolin it may lose the reply,
// but the backend finishes (and rolls back if needed) on its own.
function runNewtUpdateAction(path, btn, busyKey, waitingKey) {
  setNewtUpdateBusy(true, btn, busyKey);
  showFeedback(newtUpdateFeedback, msg(waitingKey), "");
  llamaRequest("POST", path, null, 600000, function (err, data) {
    setNewtUpdateBusy(false, btn);
    if (err) {
      showFeedback(newtUpdateFeedback, msg("newt_update_err_tunnel", { error: err }), "error");
    } else if (data.code === "docker_access") {
      showFeedback(newtUpdateFeedback, data.error, "error");
      loadNewtDockerFix();
    } else if (!data.success) {
      showFeedback(newtUpdateFeedback, data.error || msg("common_something_wrong"), "error");
      if (data.output && data.code !== "rolled_back") showOutputPopup(msg("newt_popup_update"), data.output);
    } else {
      showFeedback(newtUpdateFeedback, data.output, "success");
    }
    loadNewtStatus();
  });
}

newtCheckBtn.addEventListener("click", function () {
  runNewtUpdateAction("/api/newt/update/check", newtCheckBtn, "newt_checking", "newt_check_waiting");
});

newtUpdateBtn.addEventListener("click", function () {
  if (!confirm(t("newt_update_confirm"))) return;
  runNewtUpdateAction("/api/newt/update", newtUpdateBtn, "newt_updating", "newt_update_waiting");
});

newtRollbackBtn.addEventListener("click", function () {
  if (!confirm(t("newt_rollback_confirm"))) return;
  runNewtUpdateAction("/api/newt/rollback", newtRollbackBtn, "newt_rolling_back", "newt_rollback_waiting");
});

newtAutoToggle.addEventListener("change", function () {
  var enabled = newtAutoToggle.checked;
  if (enabled && !newtConfigured) {
    setNewtAutoToggleReady(false);
    showFeedback(newtUpdateFeedback, msg("newt_auto_setup_first"), "error");
    return;
  }
  newtAutoToggle.disabled = true;
  llamaRequest("POST", "/api/newt/auto-update", { enabled: enabled }, 10000, function (err, data) {
    newtAutoToggle.disabled = false;
    if (err || !data.success) {
      newtAutoToggle.checked = !enabled;
      showFeedback(newtUpdateFeedback, err || data.error || msg("newt_auto_change_failed"), "error");
      // Newt was removed in another tab or session: lock the switch again
      if (data && data.code === "not_configured") loadNewtStatus();
      return;
    }
    showFeedback(newtUpdateFeedback, data.output, "success");
  });
});

/* ── SHARED FEEDBACK HELPER ── */
// message: a plain string (e.g. from the backend) or msg(key, params)
function showFeedback(el, message, type) {
  setMsg(el, message);
  el.className = "control-feedback" + (type ? " " + type : "");
}

/* ── REBOOT / SHUTDOWN ── */
document.getElementById("btn-reboot").addEventListener("click", function () {
  if (confirm(t("reboot_confirm"))) {
    showOutputPopup(msg("btn_reboot"), msg("reboot_sending"));
    var xhr = new XMLHttpRequest();
    xhr.open("POST", API_BASE + "/api/command", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.timeout = 8000;
    xhr.onload = function () {
      try { setPopupOutput(JSON.parse(xhr.responseText).output || msg("reboot_rebooting")); }
      catch (e) { setPopupOutput(msg("reboot_sent")); }
    };
    xhr.onerror = function () { setPopupOutput(msg("reboot_sent_lost")); };
    xhr.send(JSON.stringify({ command: "reboot" }));
  }
});

document.getElementById("btn-shutdown").addEventListener("click", function () {
  if (confirm(t("shutdown_confirm"))) {
    showOutputPopup(msg("btn_shutdown"), msg("shutdown_sending"));
    var xhr = new XMLHttpRequest();
    xhr.open("POST", API_BASE + "/api/command", true);
    xhr.setRequestHeader("Content-Type", "application/json");
    xhr.timeout = 8000;
    xhr.onload = function () {
      try { setPopupOutput(JSON.parse(xhr.responseText).output || msg("shutdown_shutting_down")); }
      catch (e) { setPopupOutput(msg("shutdown_sent")); }
    };
    xhr.onerror = function () { setPopupOutput(msg("shutdown_sent_lost")); };
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
      showFeedback(dockerFeedback, msg("docker_err_name"), "error");
      document.getElementById("container-input").focus();
      return;
    }

    if (needsConfirm && !confirm(t("docker_remove_confirm", { name: name }))) {
      return;
    }

    showFeedback(dockerFeedback, "", "");
    runDockerCommand(command, needsName ? name : "", btn.textContent.trim());
  });
});

function runDockerCommand(command, param, label) {
  document.querySelectorAll(".docker-btn").forEach(function (b) {
    b.classList.add("running");
    b.disabled = true;
  });

  I18N.setText(dockerOutput, "output_running_cmd", { command: (label || command) + (param ? " " + param : "") });

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/command", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = (command === "container_restart") ? 65000 : 20000;

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      setMsg(dockerOutput, data.output || data.message || xhr.responseText);
    } catch (e) {
      setMsg(dockerOutput, xhr.responseText);
    }
    resetDockerButtons();
  };

  xhr.onerror   = function () { I18N.setText(dockerOutput, "err_backend_unreachable"); resetDockerButtons(); };
  xhr.ontimeout = function () { I18N.setText(dockerOutput, "err_timeout");             resetDockerButtons(); };

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
  I18N.setText(dockerOutput, "docker_output_placeholder");
});

/* ── RUN CONTAINER FORM ── */
var runFeedback = document.getElementById("run-feedback");

// ── Dynamic row builders ──────────────────────────────────────────────────────

function makeDynamicRow(list, innerHtml) {
  var row = document.createElement("div");
  row.className = "dynamic-row";
  row.innerHTML = innerHtml;
  I18N.apply(row);   // fill in data-i18n-* placeholders / labels
  row.querySelector(".remove-btn").addEventListener("click", function () {
    list.removeChild(row);
  });
  list.appendChild(row);
  return row;
}

var REMOVE_BTN_HTML = '<button class="remove-btn" type="button" data-i18n-aria-label="common_remove">&times;</button>';

document.getElementById("add-port-btn").addEventListener("click", function () {
  var list = document.getElementById("port-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" placeholder="8080" maxlength="5" data-i18n-aria-label="docker_port_host_aria" />' +
    '<span class="row-sep">:</span>' +
    '<input class="text-input" type="text" placeholder="80" maxlength="5" data-i18n-aria-label="docker_port_container_aria" />' +
    REMOVE_BTN_HTML
  );
});

document.getElementById("add-volume-btn").addEventListener("click", function () {
  var list = document.getElementById("volume-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" data-i18n-placeholder="docker_volume_host_placeholder" />' +
    '<span class="row-sep">:</span>' +
    '<input class="text-input" type="text" data-i18n-placeholder="docker_volume_container_placeholder" />' +
    '<select class="select-input select-input--narrow" data-i18n-aria-label="docker_volume_mode_aria">' +
      '<option value="">rw</option>' +
      '<option value="ro">ro</option>' +
    '</select>' +
    REMOVE_BTN_HTML
  );
});

document.getElementById("add-env-btn").addEventListener("click", function () {
  var list = document.getElementById("env-list");
  makeDynamicRow(list,
    '<input class="text-input" type="text" placeholder="KEY" data-i18n-aria-label="docker_env_key_aria" />' +
    '<span class="row-sep">=</span>' +
    '<input class="text-input" type="text" data-i18n-placeholder="docker_env_value_placeholder" />' +
    REMOVE_BTN_HTML
  );
});

// ── Form collection + submission ─────────────────────────────────────────────

document.getElementById("run-container-btn").addEventListener("click", function () {
  var image = document.getElementById("run-image").value.trim();
  if (!image) {
    showFeedback(runFeedback, msg("docker_err_image"), "error");
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
  btn.disabled = true;
  I18N.setText(btn, "docker_pulling");
  showFeedback(runFeedback, "", "");
  I18N.setText(dockerOutput, "docker_run_output_start", { image: payload.image });

  var xhr = new XMLHttpRequest();
  xhr.open("POST", API_BASE + "/api/docker/run", true);
  xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = 300000; // 5 minutes — image pulls can be slow

  xhr.onload = function () {
    try {
      var data = JSON.parse(xhr.responseText);
      if (data.status === "ok") {
        setMsg(dockerOutput, data.output || msg("docker_started_default"));
        showFeedback(runFeedback, msg("docker_started"), "success");
      } else {
        // Show the summary AND Docker's own message (e.g. "name already in use"), not just the exit code
        setMsg(dockerOutput, [data.error, data.output].filter(Boolean).join("\n\n") || xhr.responseText);
        showFeedback(runFeedback, data.error || msg("docker_start_failed"), "error");
      }
    } catch (e) {
      setMsg(dockerOutput, xhr.responseText);
      showFeedback(runFeedback, msg("err_unexpected"), "error");
    }
    btn.disabled = false;
    I18N.setText(btn, "docker_run_btn");
  };

  xhr.onerror = function () {
    I18N.setText(dockerOutput, "err_backend_unreachable");
    showFeedback(runFeedback, msg("docker_unreachable"), "error");
    btn.disabled = false;
    I18N.setText(btn, "docker_run_btn");
  };

  xhr.ontimeout = function () {
    I18N.setText(dockerOutput, "docker_timeout_output");
    showFeedback(runFeedback, msg("docker_timeout"), "error");
    btn.disabled = false;
    I18N.setText(btn, "docker_run_btn");
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
var llamaRecList        = document.getElementById("llama-recommended-list");
var llamaRecEmpty       = document.getElementById("llama-recommended-empty");
var llamaRecFeedback    = document.getElementById("llama-recommended-feedback");

var llamaRunning     = false;
var llamaSelected    = "";   // filename of the model chosen in Installed models
var llamaDownloading = "";   // filename being downloaded — hidden from the list until complete
var llamaInstalled   = [];   // filenames from the last /api/llama/models response
var llamaRecommended = null; // entries from llama-models.json (null until loaded)

// Small wrapper: every Llama endpoint returns JSON, so parse it once here.
// cb(error, data, httpStatus, errorCode)
//   error     — null when the body parsed, otherwise msg(key) ready to display
//   errorCode — "parse" | "network" | "timeout" (null on success)
function llamaRequest(method, path, body, timeout, cb) {
  var xhr = new XMLHttpRequest();
  xhr.open(method, API_BASE + path, true);
  if (body) xhr.setRequestHeader("Content-Type", "application/json");
  xhr.timeout = timeout;
  xhr.onload = function () {
    var data = null;
    try { data = JSON.parse(xhr.responseText); } catch (e) {}
    if (data === null) cb(msg("err_unexpected"), null, xhr.status, "parse");
    else               cb(null, data, xhr.status, null);
  };
  xhr.onerror   = function () { cb(msg("err_backend_unreachable"), null, 0, "network"); };
  xhr.ontimeout = function () { cb(msg("err_timeout"), null, 0, "timeout"); };
  xhr.send(body ? JSON.stringify(body) : null);
}

// key: badge_* translation key
function setLlamaBadge(key, cls) {
  I18N.setText(llamaBadge, key);
  llamaBadge.className = "service-badge " + cls;
}

function setLlamaRunning(running) {
  llamaRunning        = running;
  llamaToggle.checked = running;
  llamaToggle.disabled = false;
  setLlamaBadge(running ? "badge_running" : "badge_stopped", running ? "badge-active" : "badge-inactive");

  // The model can't be swapped while llama-server has one loaded
  llamaInstalledList.querySelectorAll(".model-row").forEach(function (row) {
    row.classList.toggle("disabled", running);
    row.querySelector("input").disabled = running;
  });
  showFeedback(llamaInstalledFeedback, running && llamaSelected ? msg("llama_switch_off_to_change") : "", "");
}

function loadLlamaStatus(cb) {
  llamaRequest("GET", "/api/llama/status", null, 5000, function (err, data) {
    if (err) { setLlamaBadge("badge_unknown", "badge-neutral"); }
    else     { setLlamaRunning(!!data.running); }
    if (cb) cb(err ? null : !!data.running);
  });
}

function loadLlamaModels() {
  llamaRequest("GET", "/api/llama/models", null, 5000, function (err, data) {
    if (err || !Array.isArray(data)) {
      showFeedback(llamaInstalledFeedback, err || msg("llama_models_load_fail"), "error");
      return;
    }
    llamaInstalled = data.filter(function (m) { return m !== llamaDownloading; });
    renderLlamaModels(llamaInstalled);
    renderLlamaRecommended();
  });
}

/* Recommended models — models tested on the Xerxes Pi. The list is a static
   file, public/llama-models.json, so adding a model needs no code change:
     { "name": "...", "url": "https://huggingface.co/.../resolve/main/x.gguf",
       "size": "491 MB", "note": { "en": "...", "es": "...", ... } }
   "size" and "note" are optional; a note falls back to English. */
function modelFilename(url) { return url.substring(url.lastIndexOf("/") + 1); }

function isValidModelUrl(url) {
  return url.indexOf("https://huggingface.co/") === 0 && url.indexOf("/resolve/") !== -1 && /\.gguf$/i.test(url);
}

function loadLlamaRecommended() {
  if (llamaRecommended) { renderLlamaRecommended(); return; }
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/llama-models.json", true);
  xhr.setRequestHeader("Cache-Control", "no-cache");   // pick up newly added models
  xhr.timeout = 8000;
  function fail() {
    llamaRecList.hidden  = true;
    llamaRecEmpty.hidden = true;
    showFeedback(llamaRecFeedback, msg("llama_recommended_load_fail"), "error");
  }
  xhr.onload = function () {
    var data = null;
    try { data = JSON.parse(xhr.responseText); } catch (e) {}
    if (xhr.status !== 200 || !data || !Array.isArray(data.models)) { fail(); return; }
    llamaRecommended = data.models.filter(function (m) {
      if (!m || typeof m.name !== "string") return false;
      m.url = normaliseModelUrl(String(m.url || ""));
      if (!isValidModelUrl(m.url)) { console.warn("llama-models.json: skipping bad url for " + m.name); return false; }
      return true;
    });
    showFeedback(llamaRecFeedback, "", "");
    renderLlamaRecommended();
  };
  xhr.onerror = fail;
  xhr.ontimeout = fail;
  xhr.send();
}

function recNote(note) {
  if (!note) return "";
  if (typeof note === "string") return note;
  return note[I18N.lang()] || note.en || "";
}

function renderLlamaRecommended() {
  if (!llamaRecommended) return;
  llamaRecList.textContent = "";
  llamaRecList.hidden  = llamaRecommended.length === 0;
  llamaRecEmpty.hidden = llamaRecommended.length > 0;
  if (llamaRecommended.length === 0) { I18N.setText(llamaRecEmpty, "llama_recommended_empty"); return; }

  llamaRecommended.forEach(function (m) {
    var file = modelFilename(m.url);
    var row  = document.createElement("div");
    row.className = "rec-model-row";

    var info = document.createElement("div");
    info.className = "rec-model-info";
    var name = document.createElement("div");
    name.className   = "rec-model-name";
    name.textContent = m.name;
    var meta = document.createElement("div");
    meta.className   = "rec-model-meta";
    meta.textContent = file + (m.size ? " · " + m.size : "");
    info.appendChild(name);
    info.appendChild(meta);
    var noteText = recNote(m.note);
    if (noteText) {
      var note = document.createElement("div");
      note.className   = "rec-model-note";
      note.textContent = noteText;
      info.appendChild(note);
    }
    row.appendChild(info);

    if (llamaInstalled.indexOf(file) !== -1) {
      var badge = document.createElement("span");
      badge.className = "service-badge badge-active";
      I18N.setText(badge, "llama_recommended_installed");
      row.appendChild(badge);
    } else {
      var btn = document.createElement("button");
      btn.className = "action-btn";
      btn.disabled  = !!llamaDownloading;   // one download at a time
      I18N.setText(btn, llamaDownloading === file ? "common_downloading" : "ctrl_llama_download_btn");
      btn.addEventListener("click", function () {
        downloadLlamaModel(m.url, llamaRecFeedback);
      });
      row.appendChild(btn);
    }
    llamaRecList.appendChild(row);
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
    showFeedback(llamaEnableFeedback, msg("llama_need_model"), "error");
    return;
  }

  llamaToggle.disabled = true;
  setLlamaBadge("badge_starting", "badge-neutral");
  showFeedback(llamaEnableFeedback, msg("llama_starting_with", { model: model }), "");

  llamaRequest("POST", "/api/llama/enable", { model: model }, 15000, function (err, data) {
    if (err || !data.success) {
      showFeedback(llamaEnableFeedback, err || data.error || msg("llama_start_failed"), "error");
      loadLlamaStatus();
      return;
    }
    setLlamaRunning(true);
    showFeedback(llamaEnableFeedback, msg("llama_loading", { model: model }), "success");

    // llama-server exits quickly if the model doesn't fit in memory or the
    // binary is missing — catch that rather than leaving the toggle on.
    setTimeout(function () {
      loadLlamaStatus(function (running) {
        if (running === false) {
          showFeedback(llamaEnableFeedback, msg("llama_stopped_early"), "error");
        }
      });
    }, 4000);
  });
}

function stopLlama() {
  llamaToggle.disabled = true;
  setLlamaBadge("badge_stopping", "badge-neutral");
  showFeedback(llamaEnableFeedback, msg("common_stopping"), "");

  // The backend waits up to 5 s for llama-server to exit cleanly
  llamaRequest("POST", "/api/llama/disable", null, 10000, function (err) {
    if (err) {
      showFeedback(llamaEnableFeedback, err, "error");
      loadLlamaStatus();
      return;
    }
    setLlamaRunning(false);
    showFeedback(llamaEnableFeedback, msg("llama_stopped"), "success");
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
    showFeedback(llamaModelFeedback, msg("llama_err_no_url"), "error");
    llamaUrlInput.focus();
    return;
  }
  if (!isValidModelUrl(url)) {
    showFeedback(llamaModelFeedback, msg("llama_err_bad_url"), "error");
    llamaUrlInput.focus();
    return;
  }

  llamaUrlInput.value = url;
  downloadLlamaModel(url, llamaModelFeedback, function () { llamaUrlInput.value = ""; });
});

// Shared by the Model source box and the Recommended models list.
// feedbackEl: where progress/result messages go; onSuccess: optional extra step.
function downloadLlamaModel(url, feedbackEl, onSuccess) {
  if (llamaDownloading) return;   // one download at a time
  var filename = modelFilename(url);
  llamaDownloading = filename;
  llamaDownloadBtn.disabled = true;
  I18N.setText(llamaDownloadBtn, "common_downloading");
  renderLlamaRecommended();
  showFeedback(feedbackEl, msg("llama_downloading_msg", { file: filename }), "");

  function finish() {
    llamaDownloading = "";
    llamaDownloadBtn.disabled = false;
    I18N.setText(llamaDownloadBtn, "ctrl_llama_download_btn");
    renderLlamaRecommended();
    loadLlamaModels();
  }

  llamaRequest("POST", "/api/llama/download", { url: url }, 3600000, function (err, data, status, code) {
    if (code === "timeout") {
      showFeedback(feedbackEl, msg("llama_still_downloading"), "error");
    } else if (err || !data.success) {
      showFeedback(feedbackEl, err || data.error || msg("llama_download_failed"), "error");
    } else {
      showFeedback(feedbackEl, msg("llama_downloaded", { file: filename }), "success");
      if (onSuccess) onSuccess();
      if (!llamaRunning) llamaSelected = filename;
    }
    finish();
  });
}

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

// The prompt stays in English (small models follow English instructions best);
// only the reply language follows the panel's language.
function chatSystemPrompt() {
  return CHAT_SYSTEM_PROMPT + " " + "Reply in " + I18N.languageName() + " (" + I18N.lang() + ").";
}
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
  var key = running === true ? "chat_dot_running" : running === false ? "chat_dot_off" : "chat_dot_unknown";
  chatDot.setAttribute("data-i18n-title", key);
  chatDot.title = t(key);
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
  el.setAttribute("data-i18n-aria-label", "chat_typing_aria");
  el.setAttribute("aria-label", t("chat_typing_aria"));
  for (var i = 0; i < 3; i++) el.appendChild(document.createElement("span"));
  chatMessages.appendChild(el);
  chatMessages.scrollTop = chatMessages.scrollHeight;
  return el;
}

// Error bubble; when Llama is off, include a button that jumps to the Llama tab.
function appendChatError(text, showLlamaLink) {
  var el = appendChatMessage("error", msgText(text));
  if (showLlamaLink) {
    var btn = document.createElement("button");
    btn.type = "button";
    I18N.setText(btn, "chat_open_llama");
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
function describeChatError(err, data, status, code) {
  if (code === "timeout") return { text: msg("chat_err_timeout") };
  if (code === "network") return { text: msg("chat_err_backend") };

  var detail = data && data.error ? (typeof data.error === "string" ? data.error : data.error.message || "") : "";
  if (detail.indexOf("not running") !== -1)
    return { text: msg("chat_err_llama_off"), llamaOff: true };
  if (status === 503 || detail.indexOf("not reachable") !== -1 || /loading/i.test(detail))
    return { text: msg("chat_err_loading") };
  return { text: detail || msg("chat_err_http", { status: status }) };
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

  var messages = [{ role: "system", content: chatSystemPrompt() }]
    .concat(chatHistory.slice(-CHAT_MAX_HISTORY));

  // Generation on a Pi is slow — allow up to 3 minutes for a reply
  llamaRequest("POST", "/api/llama/v1/chat/completions",
               { messages: messages, max_tokens: 512 }, 180000,
               function (err, data, status, code) {
    typing.remove();
    var choice = !err && data && data.choices && data.choices[0];
    var reply  = choice && choice.message ? choice.message.content : null;

    if (typeof reply === "string") {
      reply = reply.trim() || t("chat_empty_reply");
      chatHistory.push({ role: "assistant", content: reply });
      appendChatMessage("assistant", reply);
      setChatDot(true);
    } else {
      // Drop the failed turn from history and hand the text back for a retry
      chatHistory.pop();
      if (!chatInput.value) chatInput.value = text;
      var problem = describeChatError(err, data, status, code);
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
  rfidModuleError = mod.state === "error" ? (mod.error || t("rfid_module_not_found_default")) : "";
  I18N.setText(rfidModuleState, mod.state === "ok" ? "rfid_mod_connected" : mod.state === "error" ? "rfid_mod_not_found" : "rfid_mod_idle");
  rfidModuleState.className   = "mini-card-value" + (mod.state === "ok" ? " rfid-ok" : mod.state === "error" ? " rfid-bad" : "");
  if (mod.state === "idle") {
    rfidModuleState.setAttribute("data-i18n-title", "rfid_mod_idle_title");
    rfidModuleState.title = t("rfid_mod_idle_title");
  } else {
    rfidModuleState.removeAttribute("data-i18n-title");
    rfidModuleState.title = mod.error || "";
  }

  if (s.busy_command) {
    setRfidBadge("badge_running_command", "badge-neutral");
  } else if (rfidRunning && mod.state === "error") {
    setRfidBadge("badge_error", "badge-failed");
  } else if (rfidRunning) {
    setRfidBadge("badge_running", "badge-active");
  } else {
    setRfidBadge("badge_stopped", "badge-inactive");
  }

  if (mod.state === "error" && mod.error) {
    showFeedback(rfidRunFeedback, msg("rfid_module_error", { error: mod.error }), "error");
  } else if (s.busy_command) {
    showFeedback(rfidRunFeedback, msg("output_running_cmd", { command: s.busy_command }), "");
  } else if (rfidRunFeedback.classList.contains("error") && mod.state !== "error") {
    showFeedback(rfidRunFeedback, "", "");
  }

  if (s.last_tag) rfidLastTag.textContent = s.last_tag;
  setRfidLive(typeof s.live_active === "boolean" ? s.live_active : rfidLive);
}

// key: badge_* translation key
function setRfidBadge(key, cls) {
  I18N.setText(rfidBadge, key);
  rfidBadge.className = "service-badge " + cls;
}

function loadRfidStatus(fillSettings) {
  llamaRequest("GET", "/api/rfid/status", null, 5000, function (err, data) {
    if (err) { setRfidBadge("badge_unknown", "badge-neutral"); return; }
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
  showFeedback(rfidRunFeedback, msg(enable ? "rfid_starting" : "rfid_stopping"), "");
  llamaRequest("POST", "/api/rfid/run", { enabled: enable }, 8000, function (err, data) {
    rfidRunToggle.disabled = false;
    if (err || !data.success) {
      showFeedback(rfidRunFeedback, err || data.error || msg("rfid_change_failed"), "error");
    } else {
      showFeedback(rfidRunFeedback, msg(enable ? "rfid_on" : "rfid_off"), "success");
    }
    loadRfidStatus(false);
    rfidPollEvents();
  });
});

function rfidAppendLog(text) {
  if (rfidLogEmpty) { I18N.clear(rfidLog); rfidLog.textContent = ""; rfidLogEmpty = false; }
  var nearBottom = rfidLog.scrollHeight - rfidLog.scrollTop - rfidLog.clientHeight < 40;
  rfidLog.textContent += text;
  if (rfidLog.textContent.length > 60000) rfidLog.textContent = rfidLog.textContent.slice(-50000);
  if (nearBottom) rfidLog.scrollTop = rfidLog.scrollHeight;
}

document.getElementById("rfid-log-clear").addEventListener("click", function () {
  I18N.setText(rfidLog, "rfid_log_placeholder");
  rfidLogEmpty = true;
});

// Log lines are a running transcript, so they stay in the language they
// were written in (like a terminal) rather than re-translating.
function rfidHandleEvent(ev) {
  var ts = "[" + rfidTime(ev.time_ms) + "] ";
  if (ev.kind === "run") {
    if (ev.tags_error) {
      rfidAppendLog(ts + ev.id + "\n" + t("rfid_log_tagfile_error", { error: ev.tags_error }) + "\n\n");
    } else if (ev.registered) {
      rfidAppendLog(ts + ev.id + "\n" + t("rfid_log_known", { command: ev.command }) + "\n");
    } else {
      rfidAppendLog(ts + ev.id + "\n" + t("rfid_log_unknown") + "\n\n");
    }
  } else if (ev.kind === "result") {
    var out = ev.output || "";
    if (out && out.charAt(out.length - 1) !== "\n") out += "\n";
    if (ev.truncated) out += t("rfid_log_truncated") + "\n";
    var secs = (ev.duration_ms / 1000).toFixed(1);
    rfidAppendLog(out + ts + t("rfid_log_finished", { code: ev.exit_code, secs: secs }) + "\n\n");
  } else if (ev.kind === "error") {
    rfidAppendLog(ts + t("rfid_log_error", { message: ev.message }) + "\n\n");
  } else if (ev.kind === "info") {
    rfidAppendLog(ts + ev.message + "\n\n");
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
    if (data.path) I18N.setText(rfidTagsPath, "rfid_tags_path", { path: data.path });
    if (!data.success) {
      rfidTags = [];
      renderRfidTags();
      rfidTagCount.textContent = "—";
      showFeedback(rfidTagsFeedback, msg("rfid_tags_file_error", { error: data.error }), "error");
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
      input.setAttribute("aria-label", t("rfid_cmd_aria", { id: tag.id }));
      input.addEventListener("keydown", function (e) {
        if (e.key === "Enter") saveRfidEdit(tag.id, input.value, saveBtn);
        if (e.key === "Escape") { rfidEditingId = null; renderRfidTags(); }
      });
      var saveBtn = rfidSmallBtn("common_save", "action-btn", function () { saveRfidEdit(tag.id, input.value, saveBtn); });
      var cancelBtn = rfidSmallBtn("common_cancel", "secondary-btn", function () { rfidEditingId = null; renderRfidTags(); });
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
    actions.appendChild(rfidSmallBtn("common_edit", "secondary-btn", function () {
      rfidEditingId = tag.id;
      showFeedback(rfidTagsFeedback, "", "");
      renderRfidTags();
    }));
    actions.appendChild(rfidSmallBtn("common_delete", "secondary-btn rfid-delete-btn", function () { deleteRfidTag(tag); }));
    row.appendChild(actions);
    rfidTagList.appendChild(row);
  });
}

// labelKey: translation key for the button text
function rfidSmallBtn(labelKey, cls, onClick) {
  var b = document.createElement("button");
  b.type = "button";
  b.className = cls + " rfid-small-btn";
  I18N.setText(b, labelKey);
  b.addEventListener("click", onClick);
  return b;
}

function saveRfidEdit(id, command, btn) {
  command = command.trim();
  if (!command) { showFeedback(rfidTagsFeedback, msg("rfid_cmd_empty"), "error"); return; }
  btn.disabled = true;
  llamaRequest("POST", "/api/rfid/tags", { id: id, command: command, mode: "edit" }, 8000, function (err, data) {
    btn.disabled = false;
    if (err || !data.success) {
      showFeedback(rfidTagsFeedback, err || data.error || msg("rfid_update_failed"), "error");
      return;
    }
    rfidEditingId = null;
    showFeedback(rfidTagsFeedback, data.message || msg("rfid_tag_updated", { id: id }), "success");
    loadRfidTags();
  });
}

function deleteRfidTag(tag) {
  if (!confirm(t("rfid_delete_confirm", { id: tag.id, command: tag.command }))) return;
  llamaRequest("POST", "/api/rfid/tags/delete", { id: tag.id }, 8000, function (err, data) {
    if (err || !data.success) {
      showFeedback(rfidTagsFeedback, err || data.error || msg("rfid_delete_failed"), "error");
    } else {
      if (rfidEditingId === tag.id) rfidEditingId = null;
      showFeedback(rfidTagsFeedback, data.message || msg("rfid_tag_deleted", { id: tag.id }), "success");
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
  I18N.clear(rfidAddFeedback);
  rfidAddFeedback.textContent = "";
  rfidAddFeedback.className = "control-feedback error";
  var text = document.createElement("span");
  I18N.setText(text, "rfid_already_registered", { id: id, command: existing });
  rfidAddFeedback.appendChild(text);
  rfidAddFeedback.appendChild(document.createTextNode(" "));
  var btn = document.createElement("button");
  btn.type = "button";
  btn.className = "clear-btn rfid-inline-btn";
  I18N.setText(btn, "rfid_edit_instead");
  btn.addEventListener("click", function () {
    showFeedback(rfidAddFeedback, "", "");
    rfidNewId.value = "";
    rfidNewCmd.value = "";
    editRfidTagInstead(id);
  });
  rfidAddFeedback.appendChild(btn);
}

// text: optional msg(key, params) for the status line
function setRfidScanning(on, text) {
  rfidScanStatus.hidden = !on;
  rfidScanBtn.disabled  = on;
  I18N.setText(rfidScanBtn, on ? "rfid_scanning_btn" : "rfid_scan_btn");
  if (text) setMsg(rfidScanText, text);
}

rfidScanBtn.addEventListener("click", function () {
  showFeedback(rfidAddFeedback, "", "");
  setRfidScanning(true, msg("rfid_scan_place"));
  llamaRequest("POST", "/api/rfid/scan/start", null, 5000, function (err, data) {
    if (err || !data.success) {
      setRfidScanning(false);
      showFeedback(rfidAddFeedback, err || data.error || msg("rfid_scan_start_failed"), "error");
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
        I18N.setText(rfidScanText, "rfid_scan_module_err", { error: mod.error, secs: data.seconds_left });
      } else {
        I18N.setText(rfidScanText, "rfid_scan_place_secs", { secs: data.seconds_left });
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
        showFeedback(rfidAddFeedback, data.type
          ? msg("rfid_scanned_type", { id: data.id, type: data.type })
          : msg("rfid_scanned", { id: data.id }), "success");
        rfidNewCmd.focus();
      }
    } else if (data.state === "timeout") {
      showFeedback(rfidAddFeedback, msg("rfid_scan_timeout"), "error");
    } else if (data.state === "cancelled") {
      showFeedback(rfidAddFeedback, msg("rfid_scan_cancelled"), "");
    }
  });
}

document.getElementById("rfid-scan-cancel").addEventListener("click", function () {
  clearTimeout(rfidScanTimer);
  rfidScanTimer = null;
  llamaRequest("POST", "/api/rfid/scan/cancel", null, 5000, function () {
    setRfidScanning(false);
    showFeedback(rfidAddFeedback, msg("rfid_scan_cancelled"), "");
  });
});

function addRfidTag() {
  var id  = rfidNewId.value.trim();
  var cmd = rfidNewCmd.value.trim();
  if (!id)  { showFeedback(rfidAddFeedback, msg("rfid_err_need_id"), "error"); rfidNewId.focus(); return; }
  if (!cmd) { showFeedback(rfidAddFeedback, msg("rfid_cmd_empty"), "error"); rfidNewCmd.focus(); return; }

  rfidAddBtn.disabled = true;
  llamaRequest("POST", "/api/rfid/tags", { id: id, command: cmd, mode: "add" }, 8000, function (err, data, status) {
    rfidAddBtn.disabled = false;
    if (!err && status === 409 && data.existing_command !== undefined) {
      showRfidAlreadyRegistered(data.id, data.existing_command);
      return;
    }
    if (err || !data.success) {
      showFeedback(rfidAddFeedback, err || data.error || msg("rfid_add_failed"), "error");
      return;
    }
    rfidNewId.value = "";
    rfidNewCmd.value = "";
    showFeedback(rfidAddFeedback, data.message || msg("rfid_tag_added"), "success");
    loadRfidTags();
  });
}

rfidAddBtn.addEventListener("click", addRfidTag);
rfidNewCmd.addEventListener("keydown", function (e) { if (e.key === "Enter") addRfidTag(); });

function setRfidLive(on) {
  rfidLive = on;
  var failing = on && rfidModuleError;
  I18N.setText(rfidLiveBtn, on ? "rfid_live_stop" : "rfid_live_start");
  I18N.setText(rfidLiveBadge, failing ? "badge_error" : on ? "badge_scanning" : "badge_off");
  rfidLiveBadge.className = "service-badge " + (failing ? "badge-failed" : on ? "badge-active" : "badge-inactive");
  showFeedback(rfidLiveFeedback, failing ? msg("rfid_not_found", { error: rfidModuleError }) : "", failing ? "error" : "");
  if (rfidLiveList.childElementCount === 0) {
    I18N.setText(rfidLiveEmpty, failing ? "rfid_live_empty_failing" : on ? "rfid_live_empty_running" : "rfid_live_empty");
  }
}

rfidLiveBtn.addEventListener("click", function () {
  var start = !rfidLive;
  rfidLiveBtn.disabled = true;
  llamaRequest("POST", start ? "/api/rfid/live/start" : "/api/rfid/live/stop", null, 5000, function (err, data) {
    rfidLiveBtn.disabled = false;
    if (err || !data.success) { alert(msgText(err || data.error || msg("rfid_live_change_failed"))); return; }
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
  if (ev.type) type.textContent = ev.type;
  else I18N.setText(type, "rfid_live_tag");
  var status = document.createElement("span");
  status.className = "rfid-live-status " + (ev.registered ? "registered" : "unregistered");
  if (ev.registered) I18N.setText(status, "rfid_live_registered", { command: ev.command });
  else               I18N.setText(status, "rfid_live_unregistered");
  head.appendChild(time);
  head.appendChild(id);
  head.appendChild(type);
  head.appendChild(status);

  var detail = document.createElement("div");
  detail.className = "rfid-live-detail";
  if (ev.tags_error) I18N.setText(detail, "rfid_live_detail_tagfile", { detail: ev.detail, error: ev.tags_error });
  else               I18N.setText(detail, "rfid_live_detail", { detail: ev.detail });

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
  if (!/^\d{1,4}$/.test(bus)) { showFeedback(rfidSetFeedback, msg("rfid_err_bus"), "error"); return; }
  if (!/^(0x[0-9a-fA-F]{1,2}|\d{1,3})$/.test(addr)) { showFeedback(rfidSetFeedback, msg("rfid_err_addr"), "error"); return; }
  btn.disabled = true;
  llamaRequest("POST", "/api/rfid/settings",
    { bus: parseInt(bus, 10), address: addr, tags_path: rfidSetTags.value.trim() }, 8000,
    function (err, data) {
      btn.disabled = false;
      if (err || !data.success) { showFeedback(rfidSetFeedback, err || data.error || msg("rfid_settings_failed"), "error"); return; }
      showFeedback(rfidSetFeedback, msg("rfid_settings_saved"), "success");
      loadRfidStatus(true);
      loadRfidTags();
    });
});

document.getElementById("rfid-settings-defaults").addEventListener("click", function () {
  rfidSetBus.value  = "1";
  rfidSetAddr.value = "0x2C";
  rfidSetTags.value = rfidDefaultTagsPath;
  showFeedback(rfidSetFeedback, msg("rfid_defaults_filled"), "");
});

document.getElementById("rfid-probe-btn").addEventListener("click", function () {
  var btn = this;
  btn.disabled = true;
  showFeedback(rfidSetFeedback, msg("rfid_probing"), "");
  llamaRequest("POST", "/api/rfid/probe", null, 10000, function (err, data) {
    btn.disabled = false;
    if (err) showFeedback(rfidSetFeedback, err, "error");
    else if (!data.success) showFeedback(rfidSetFeedback, msg("rfid_not_found", { error: data.error }), "error");
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

/* ── I2C: OLED DISPLAY ──
   What the OLED shows (see include/oled_manager.h). While the tab is open it
   polls the display state and redraws the preview, so values like CPU
   temperature stay current. */
var oledPowerToggle     = document.getElementById("oled-power-toggle");
var oledBadge           = document.getElementById("badge-oled");
var oledDisplayState    = document.getElementById("oled-display-state");
var oledBusDisplay      = document.getElementById("oled-bus-display");
var oledModeDisplay     = document.getElementById("oled-mode-display");
var oledPowerFeedback   = document.getElementById("oled-power-feedback");
var oledModeRadios      = document.querySelectorAll('input[name="oled-mode"]');
var oledStatList        = document.getElementById("oled-stat-list");
var oledPreview         = document.getElementById("oled-preview");
var oledPreviewNote     = document.getElementById("oled-preview-note");
var oledContentSave     = document.getElementById("oled-content-save");
var oledContentFeedback = document.getElementById("oled-content-feedback");
var oledSetBus          = document.getElementById("oled-set-bus");
var oledSetAddr         = document.getElementById("oled-set-addr");
var oledSetFeedback     = document.getElementById("oled-settings-feedback");

var OLED_POLL_MS = 3000;

var oledVisible    = false;
var oledPollTimer  = null;
var oledSaved      = null;  // last /api/oled/status reply
var oledPreviewSeq = 0;     // drops preview replies that arrive out of order

// Message for an error code from the backend, with the code itself (FR-6)
// Returns a msg() so the text re-translates if the language changes
function oledErrorText(code) {
  var vars = { device: oledSaved ? oledSaved.device : "", address: oledSaved ? oledSaved.address : "" };
  var key  = "oled_err_" + code;
  var message = code && t(key) !== key ? msg(key, vars) : msg("oled_err_unknown");
  return code ? msg("oled_err_with_code", { message: message, code_note: msg("oled_err_code", { code: code }) })
              : message;
}

function setOledBadge(key, cls) {
  I18N.setText(oledBadge, key);
  oledBadge.className = "service-badge " + cls;
}

function oledFormMode() {
  var checked = document.querySelector('input[name="oled-mode"]:checked');
  return checked ? checked.value : "stats";
}

function oledFormStats() {
  return Array.prototype.map.call(oledStatList.querySelectorAll("input:checked"), function (box) {
    return box.value;
  });
}

function oledUnsaved() {
  return !!oledSaved && (oledFormMode() !== oledSaved.mode ||
                         oledFormStats().join(",") !== oledSaved.stats.join(","));
}

function renderOledStatus(s) {
  oledSaved = s;
  oledPowerToggle.checked  = !!s.enabled;
  oledPowerToggle.disabled = false;

  var d = s.display || {};
  var state = d.state || "idle";
  I18N.setText(oledDisplayState, "oled_state_" + state);
  oledDisplayState.className = "mini-card-value" + (state === "ok" ? " rfid-ok" : state === "error" ? " rfid-bad" : "");
  if (state === "idle") {
    oledDisplayState.setAttribute("data-i18n-title", "oled_state_idle_hint");
    oledDisplayState.title = t("oled_state_idle_hint");
  } else {
    oledDisplayState.removeAttribute("data-i18n-title");
    oledDisplayState.title = d.error || "";   // from the device, shown as-is
  }
  setMsg(oledBusDisplay, s.device + " · " + s.address);
  I18N.setText(oledModeDisplay, !s.enabled ? "oled_badge_off"
                              : s.mode === "qr" ? "oled_showing_qr" : "oled_showing_stats");

  if (s.enabled && state === "error") setOledBadge("oled_badge_error", "badge-failed");
  else if (s.enabled)                 setOledBadge("oled_badge_on", "badge-active");
  else                                setOledBadge("oled_badge_off", "badge-inactive");

  if (state === "error") {
    showFeedback(oledPowerFeedback, oledErrorText(d.code), "error");
  } else if (oledPowerFeedback.classList.contains("error")) {
    showFeedback(oledPowerFeedback, "", "");
  }
  updateOledPreviewNote();
}

function renderOledStatList(available, selected) {
  oledStatList.innerHTML = "";
  available.forEach(function (key) {
    var row = document.createElement("label");
    row.className = "model-row";
    var box = document.createElement("input");
    box.type    = "checkbox";
    box.value   = key;
    box.checked = selected.indexOf(key) >= 0;
    box.addEventListener("change", oledFormChanged);
    var name = document.createElement("span");
    I18N.setText(name, "oled_stat_" + key);
    row.appendChild(box);
    row.appendChild(name);
    oledStatList.appendChild(row);
  });
}

function fillOledForm(s) {
  oledModeRadios.forEach(function (r) { r.checked = r.value === s.mode; });
  renderOledStatList(s.available_stats || [], s.stats || []);
  oledSetBus.value  = s.bus;
  oledSetAddr.value = s.address;
  oledFormChanged();
}

// Stats only apply in stats mode, so grey them out for the QR code
function oledFormChanged() {
  var qr = oledFormMode() === "qr";
  oledStatList.querySelectorAll(".model-row").forEach(function (row) {
    row.classList.toggle("disabled", qr);
    row.querySelector("input").disabled = qr;
  });
  showFeedback(oledContentFeedback, "", "");
  loadOledPreview();
}

function updateOledPreviewNote() {
  if (oledUnsaved()) {
    I18N.setText(oledPreviewNote, "oled_preview_unsaved");
  } else if (oledFormMode() === "qr" && oledSaved && oledSaved.panel_url) {
    I18N.setText(oledPreviewNote, "oled_preview_qr_note", { url: oledSaved.panel_url });
  } else {
    setMsg(oledPreviewNote, "");
  }
}

// The preview shows the form, not the saved settings, so changes can be seen before saving
function loadOledPreview() {
  var path = "/api/oled/preview?mode=" + encodeURIComponent(oledFormMode()) +
             "&stats=" + encodeURIComponent(oledFormStats().join(","));
  var seq = ++oledPreviewSeq;
  llamaRequest("GET", path, null, 5000, function (err, data) {
    if (seq !== oledPreviewSeq || err || !data.success) return;
    drawOledPreview(data);
  });
  updateOledPreviewNote();
}

// Each row is hex, the top bit of each byte is the leftmost of its 8 pixels
function drawOledPreview(p) {
  var ctx = oledPreview.getContext("2d");
  ctx.fillStyle = "#000";
  ctx.fillRect(0, 0, p.width, p.height);
  ctx.fillStyle = "#fff";
  p.rows.forEach(function (hex, y) {
    for (var i = 0; i < hex.length; i += 2) {
      var byte = parseInt(hex.substr(i, 2), 16);
      for (var bit = 0; bit < 8; bit++) {
        if (byte & (0x80 >> bit)) ctx.fillRect(i * 4 + bit, y, 1, 1);
      }
    }
  });
}

function loadOledStatus(fillForm) {
  llamaRequest("GET", "/api/oled/status", null, 5000, function (err, data) {
    if (err) { setOledBadge("oled_badge_unknown", "badge-neutral"); return; }
    renderOledStatus(data);
    if (fillForm) fillOledForm(data);
    else loadOledPreview();
  });
}

function oledPoll() {
  clearTimeout(oledPollTimer);
  if (!oledVisible) return;
  loadOledStatus(false);
  oledPollTimer = setTimeout(oledPoll, OLED_POLL_MS);
}

oledPowerToggle.addEventListener("change", function () {
  var enable = oledPowerToggle.checked;
  oledPowerToggle.disabled = true;
  llamaRequest("POST", "/api/oled/power", { enabled: enable }, 8000, function (err, data) {
    oledPowerToggle.disabled = false;
    if (err || !data.success) {
      showFeedback(oledPowerFeedback, err ? msg("oled_err_backend") : oledErrorText(data.code), "error");
    }
    loadOledStatus(false);
  });
});

oledModeRadios.forEach(function (r) { r.addEventListener("change", oledFormChanged); });

oledContentSave.addEventListener("click", function () {
  var mode = oledFormMode(), stats = oledFormStats();
  if (mode === "stats" && !stats.length) {
    showFeedback(oledContentFeedback, msg("oled_err_no_stats"), "error");
    return;
  }
  oledContentSave.disabled = true;
  showFeedback(oledContentFeedback, msg("oled_saving"), "");
  llamaRequest("POST", "/api/oled/display", { mode: mode, stats: stats }, 8000, function (err, data) {
    oledContentSave.disabled = false;
    if (err) { showFeedback(oledContentFeedback, msg("oled_err_backend"), "error"); return; }
    if (!data.success) { showFeedback(oledContentFeedback, oledErrorText(data.code), "error"); return; }
    var offline = oledSaved && oledSaved.display && oledSaved.display.state === "error";
    showFeedback(oledContentFeedback, msg(offline ? "oled_content_saved_offline" : "oled_content_saved"), "success");
    loadOledStatus(false);
  });
});

document.getElementById("oled-settings-save").addEventListener("click", function () {
  var body = { bus: oledSetBus.value.trim(), address: oledSetAddr.value.trim() };
  showFeedback(oledSetFeedback, msg("oled_saving"), "");
  llamaRequest("POST", "/api/oled/settings", body, 8000, function (err, data) {
    if (err) { showFeedback(oledSetFeedback, msg("oled_err_backend"), "error"); return; }
    if (!data.success) { showFeedback(oledSetFeedback, oledErrorText(data.code), "error"); return; }
    showFeedback(oledSetFeedback, msg("oled_settings_saved"), "success");
    loadOledStatus(false);
  });
});

document.getElementById("oled-probe-btn").addEventListener("click", function () {
  var btn = this;
  btn.disabled = true;
  showFeedback(oledSetFeedback, msg("oled_probe_testing"), "");
  llamaRequest("POST", "/api/oled/probe", null, 10000, function (err, data) {
    btn.disabled = false;
    if (err) { showFeedback(oledSetFeedback, msg("oled_err_backend"), "error"); return; }
    if (!data.success) {
      showFeedback(oledSetFeedback, oledErrorText(data.code), "error");
    } else {
      showFeedback(oledSetFeedback, msg("oled_probe_ok", { device: data.device, address: data.address }), "success");
    }
    loadOledStatus(false);
  });
});

document.getElementById("oled-settings-defaults").addEventListener("click", function () {
  oledSetBus.value  = oledSaved ? oledSaved.default_bus : 1;
  oledSetAddr.value = oledSaved ? oledSaved.default_address : "0x3C";
  showFeedback(oledSetFeedback, "", "");
});

function oledOnShow() {
  if (oledVisible) return;
  oledVisible = true;
  loadOledStatus(true);
  oledPollTimer = setTimeout(oledPoll, OLED_POLL_MS);
}

function oledOnHide() {
  oledVisible = false;
  clearTimeout(oledPollTimer);
}

/* ── LANGUAGE SWITCH ──
   I18N re-applies every data-i18n* attribute itself; this only handles
   text that is formatted in JS rather than looked up by key. */
document.addEventListener("i18n:change", function () {
  if (newtLastStatus) renderNewtUpdates(newtLastStatus);   // dates use the new locale
  renderLlamaRecommended();                                // model notes are per-language
});

/* ── INIT ──
   Wait for the translations so nothing flashes in the wrong language;
   I18N.init falls back to English if they can't be loaded. */
I18N.init(function () {
  checkStatus();
  setInterval(checkStatus, 15000);
  loadDashboard();
  loadServices();
  loadNewtStatus();
});
