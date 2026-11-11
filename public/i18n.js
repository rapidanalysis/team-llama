/* ============================================================
   XERXES PI ADMIN — i18n.js
   Small, dependency-free localisation layer.

   Translations live in public/i18n/<code>.json as flat
   { "key": "text" } maps. Text may contain {name} placeholders,
   filled in from the params object passed to t().

   Markup hooks (re-applied whenever the language changes):
     data-i18n="key"             -> element textContent
     data-i18n-args='{"n":1}'    -> params for data-i18n (JSON)
     data-i18n-html="key"        -> element innerHTML (trusted, first-party
                                    strings only; used for <code> in copy)
     data-i18n-placeholder="key" -> placeholder attribute
     data-i18n-title="key"       -> title attribute
     data-i18n-aria-label="key"  -> aria-label attribute

   From JS:
     I18N.t(key, params)           -> translated string
     I18N.setText(el, key, params) -> set text AND remember the key, so
                                      the element re-translates on switch
     I18N.clear(el)                -> forget the key (use before writing
                                      raw text such as command output)
     I18N.setLanguage(code)        -> load, apply, save, fire "i18n:change"
   ============================================================ */

var I18N = (function () {
  // Add a language: drop public/i18n/<code>.json in place and list it here.
  // Names are shown in their own language so anyone can find theirs.
  var LANGUAGES = [
    { code: "en",    name: "English" },
    { code: "es",    name: "Español" }
  ];
  var DEFAULT_LANG = "en";
  var STORAGE_KEY  = "xerxes-lang";

  var fallback = {};   // English — used for any key a translation is missing
  var strings  = {};   // current language
  var current  = DEFAULT_LANG;
  var cache    = {};   // code -> parsed JSON, so switching back is instant

  function isSupported(code) {
    for (var i = 0; i < LANGUAGES.length; i++) if (LANGUAGES[i].code === code) return true;
    return false;
  }

  function storedLang() {
    try { return localStorage.getItem(STORAGE_KEY); } catch (e) { return null; }
  }

  function saveLang(code) {
    try { localStorage.setItem(STORAGE_KEY, code); } catch (e) {}
  }

  // Saved choice first, then the browser's preferred languages, then English.
  function initialLang() {
    var saved = storedLang();
    if (saved && isSupported(saved)) return saved;
    var prefs = navigator.languages || [navigator.language || ""];
    for (var i = 0; i < prefs.length; i++) {
      var p = String(prefs[i] || "");
      if (isSupported(p)) return p;
      var base = p.split("-")[0].toLowerCase();
      if (isSupported(base)) return base;
    }
    return DEFAULT_LANG;
  }

  function load(code, cb) {
    if (cache[code]) { cb(null, cache[code]); return; }
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/i18n/" + encodeURIComponent(code) + ".json", true);
    xhr.timeout = 8000;
    xhr.onload = function () {
      if (xhr.status !== 200) { cb("HTTP " + xhr.status); return; }
      try { cache[code] = JSON.parse(xhr.responseText); cb(null, cache[code]); }
      catch (e) { cb("parse error: " + e.message); }
    };
    xhr.onerror   = function () { cb("network error"); };
    xhr.ontimeout = function () { cb("timeout"); };
    xhr.send();
  }

  // A param may itself be a translatable message { i18nKey, params }, so
  // composite text ("{hint} ({detail})") re-translates fully on a switch.
  function format(text, params) {
    if (!params) return text;
    return text.replace(/\{(\w+)\}/g, function (m, name) {
      if (!Object.prototype.hasOwnProperty.call(params, name)) return m;
      var v = params[name];
      if (v && typeof v === "object" && v.i18nKey) return t(v.i18nKey, v.params);
      return v == null ? "" : String(v);
    });
  }

  function t(key, params) {
    var text = strings[key];
    if (typeof text !== "string" || text === "") text = fallback[key];
    if (typeof text !== "string") return key;   // makes a missing key obvious
    return format(text, params);
  }

  function setText(el, key, params) {
    if (!el) return;
    el.setAttribute("data-i18n", key);
    if (params) el.setAttribute("data-i18n-args", JSON.stringify(params));
    else el.removeAttribute("data-i18n-args");
    el.textContent = t(key, params);
  }

  function clear(el) {
    if (!el) return;
    el.removeAttribute("data-i18n");
    el.removeAttribute("data-i18n-args");
  }

  function apply(root) {
    root = root || document;
    root.querySelectorAll("[data-i18n]").forEach(function (el) {
      var args = null;
      var raw  = el.getAttribute("data-i18n-args");
      if (raw) { try { args = JSON.parse(raw); } catch (e) {} }
      el.textContent = t(el.getAttribute("data-i18n"), args);
    });
    root.querySelectorAll("[data-i18n-html]").forEach(function (el) {
      el.innerHTML = t(el.getAttribute("data-i18n-html"));
    });
    [["data-i18n-placeholder", "placeholder"],
     ["data-i18n-title",       "title"],
     ["data-i18n-aria-label",  "aria-label"]].forEach(function (pair) {
      root.querySelectorAll("[" + pair[0] + "]").forEach(function (el) {
        el.setAttribute(pair[1], t(el.getAttribute(pair[0])));
      });
    });
  }

  function syncSelect() {
    var sel = document.getElementById("lang-select");
    if (sel) sel.value = current;
  }

  function activate(code, data) {
    current = code;
    strings = data;
    document.documentElement.setAttribute("lang", code);
    apply(document);
    document.title = t("page_title");
    syncSelect();
  }

  function setLanguage(code, cb) {
    if (!isSupported(code)) code = DEFAULT_LANG;
    var sel = document.getElementById("lang-select");
    if (sel) sel.disabled = true;
    load(code, function (err, data) {
      if (sel) sel.disabled = false;
      if (err) {
        console.warn("i18n: could not load " + code + " (" + err + ")");
        syncSelect();               // put the dropdown back on the active language
        if (cb) cb(err);
        return;
      }
      activate(code, data);
      saveLang(code);
      document.dispatchEvent(new CustomEvent("i18n:change", { detail: { lang: code } }));
      if (cb) cb(null);
    });
  }

  function buildSelect() {
    var sel = document.getElementById("lang-select");
    if (!sel) return;
    sel.textContent = "";
    LANGUAGES.forEach(function (l) {
      var opt = document.createElement("option");
      opt.value = l.code;
      opt.textContent = l.name;
      opt.setAttribute("lang", l.code);
      sel.appendChild(opt);
    });
    sel.value = current;
    sel.addEventListener("change", function () { setLanguage(sel.value); });
  }

  // Loads English (the fallback) plus the starting language, applies it,
  // then calls cb. The page still works in English if loading fails.
  function init(cb) {
    var code = initialLang();
    current = code;
    buildSelect();
    load(DEFAULT_LANG, function (err, en) {
      if (err) console.warn("i18n: could not load English strings (" + err + ")");
      fallback = en || {};
      if (code === DEFAULT_LANG) { activate(code, fallback); cb(); return; }
      load(code, function (err2, data) {
        if (err2) {
          console.warn("i18n: could not load " + code + " (" + err2 + "), using English");
          code = DEFAULT_LANG;
          data = fallback;
        }
        activate(code, data);
        cb();
      });
    });
  }

  return {
    languages: LANGUAGES,
    t: t,
    setText: setText,
    clear: clear,
    apply: apply,
    init: init,
    setLanguage: setLanguage,
    lang: function () { return current; },
    languageName: function (code) {
      code = code || current;
      for (var i = 0; i < LANGUAGES.length; i++) if (LANGUAGES[i].code === code) return LANGUAGES[i].name;
      return code;
    }
  };
})();

var t = I18N.t;
