# Internationalisation: To Improve

What is still missing from the panel's translations, measured against the SRS (FR-11) and the sponsor's requests, plus ideas for later. Status as of 11 November 2026 (`team-llama` branch `developG39`).

## Done so far

- **Translation system** (`public/i18n.js`) with no dependencies, English fallback, switching without a reload, and the choice saved in the browser.
- **English and Spanish** for every tab, the Ask Xerxes AI chat, the OLED error messages and the recommended model notes. Both files have the same 476 keys with matching placeholders.
- **Browser language detection** for first-time visitors.
- **The old `lang.json`** (English only) has been replaced by `public/i18n/<lang>.json`, and the deploy script removes it from deployed devices.
- **Dates and times** (e.g. Newt's *Last checked*) are formatted for the chosen language.
- **Newt's status hints and error messages**, which were previously written into `app.js`, are now translated.

## Not yet complete

| Item | What's missing | Next step |
| --- | --- | --- |
| Backend messages | Errors and results written by the backend (Newt, including the Docker fix steps; RFID; Llama; system commands) are English only. | Return a code plus parameters from the backend, as the OLED does, and add the text to the translation files. |
| Review by a Spanish speaker | The Spanish text has not been checked by a native speaker, as far as the repo shows. | Have the sponsor or a native speaker review `es.json`, especially technical terms. |
| Which languages the sponsor wants | Earlier work included more languages, which were removed (11 November) to keep two well-maintained ones. | Confirm with the sponsor which languages the product needs. |

## Ideas for later

- A check in `make test` that every language has the same keys and placeholders as English (a script is in [commands.md](commands.md#checking-the-translation-files)).
- Save each user's language on the device once the panel has logins.
- Support right-to-left languages.
