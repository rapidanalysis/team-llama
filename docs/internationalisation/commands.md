# Internationalisation: Commands

The panel's language is picked from the menu in the top bar, so users never need a terminal. This page is for developers: how to add or change text, and how to add a language. All paths are in the `team-llama` repo.

## Files

| File | What it is |
| --- | --- |
| `public/i18n/en.json` | English. The **source of truth**, and the fallback for any key missing from another language |
| `public/i18n/es.json` | Spanish |
| `public/i18n.js` | The small translation library (no dependencies). The comment at the top lists everything below |
| `public/llama-models.json` | Each recommended model's `note` has its own `en` / `es` text |

Each translation file is a flat list of `"key": "text"` pairs:

```json
{
  "nav_dashboard": "Dashboard",
  "llama_starting_with": "Starting with {model}...",
  "newt_secret_saved_placeholder": "Saved — leave blank to keep it"
}
```

`{name}` is a placeholder filled in by the code. Keep placeholders, and any `<code>` tags, exactly as they are when translating.

## Tagging text in HTML

Add an attribute with the key to the element in `public/index.html`. The English text inside the element is only a fallback while the page loads.

| Attribute | Translates |
| --- | --- |
| `data-i18n="key"` | The element's text |
| `data-i18n-args='{"path":"tags.json"}'` | Placeholder values for `data-i18n` (as JSON) |
| `data-i18n-html="key"` | The element's HTML (for first-party text with `<code>` only, never for user or device text) |
| `data-i18n-placeholder="key"` | An input's placeholder |
| `data-i18n-title="key"` | The tooltip |
| `data-i18n-aria-label="key"` | The screen-reader label |

```html
<button class="action-btn" id="newt-enable-btn" data-i18n="ctrl_newt_enable">Enable</button>
```

## Setting text from JavaScript

Use these in `public/app.js` so the text changes when the user switches language:

| Call | Use it for |
| --- | --- |
| `I18N.setText(el, "key", { param: value })` | Setting an element's text. Remembers the key so it re-translates on a switch |
| `showFeedback(el, msg("key", params), "error")` | The success / error line under a card's buttons |
| `setPopupOutput(msg("key", params))` | The output pop-up |
| `t("key", params)` | Getting the text once, e.g. for `confirm()` |
| `I18N.clear(el)` then `el.textContent = text` | Writing text that must **not** be translated, such as command output |
| `document.addEventListener("i18n:change", fn)` | Re-drawing anything built by hand when the language changes |

A placeholder value can itself be `msg("key")`, so text built from several messages re-translates fully.

## Adding or changing a string

1. Add the key and English text to `public/i18n/en.json`.
2. Add the same key with the translation to `public/i18n/es.json`. If you can't translate it yet, leave it out: English is shown until it is added.
3. Tag the element in HTML or set it from JavaScript as above.
4. Reload the page and switch languages to check it.

A key missing from **both** files shows the key itself (e.g. `llama_new_message`), which makes it easy to spot.

## Adding a language

1. Copy `public/i18n/en.json` to `public/i18n/<code>.json`, where `<code>` is the language code (e.g. `fr`, `pt-BR`).
2. Translate the values. Keep the keys, the `{placeholders}` and any `<code>` tags.
3. Add it to `LANGUAGES` at the top of `public/i18n.js`, with its name written in that language:
   ```js
   var LANGUAGES = [
     { code: "en",    name: "English" },
     { code: "es",    name: "Español" },
     { code: "fr",    name: "Français" }
   ];
   ```
4. Optionally, add a `"<code>"` note to each model in `public/llama-models.json`.
5. Reload the panel. The new language appears in the menu.

No rebuild of `xerxes_backend` is needed.

## Checking the translation files

Both files should have the same keys and the same placeholders in each string. A quick check from the repo root:

```bash
python3 -c "
import json, re
en = json.load(open('public/i18n/en.json')); es = json.load(open('public/i18n/es.json'))
print('missing in es:', sorted(set(en) - set(es)))
print('extra in es:', sorted(set(es) - set(en)))
ph = lambda s: set(re.findall(r'\{\w+\}', s))
print('placeholder mismatch:', [k for k in en if k in es and ph(en[k]) != ph(es[k])])
"
```

As of 11 November 2026 both files have 476 keys and every placeholder matches.
