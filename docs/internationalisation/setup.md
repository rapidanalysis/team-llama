# Internationalisation: Setup

The admin panel can be shown in **English** or **Spanish** (Español). Every tab, the Ask Xerxes AI chat and the recommended model notes are translated. Nothing needs installing or compiling: the translations are plain files served with the panel.

## Choosing a language

1. Open the admin panel at `http://<pi-address>:8000`.
2. Pick a language from the menu in the top bar.

The whole page switches straight away, without reloading. The choice is saved **in your browser**, so each person (and each browser) can use their own language, and it is remembered next time.

The first time someone opens the panel, it uses their browser's preferred language if the panel supports it (for example a browser set to `es-MX` gets Spanish), otherwise English.

## What is and isn't translated

| Translated | Shown as-is (English) |
| --- | --- |
| All buttons, labels, descriptions, placeholders and tooltips | Command output (e.g. the output of `df -h`, Docker or Newt logs) |
| Status words, badges and the panel's own success and error messages | Most error messages that come from the backend itself (e.g. Newt's Docker fix steps, RFID file errors) |
| OLED display errors (the backend sends a code that the page translates) | Text from Linux and Docker (service states the panel doesn't recognise) |
| Ask Xerxes AI's greeting and messages; the model is told to **reply** in the panel's language | Text drawn on the OLED screen itself |
| Notes on the Llama tab's recommended models | |

## Setting up a development copy

The translations are in the `team-llama` repo, under `public/`. After building and running the panel (see [admin-panel/setup.md](../admin-panel/setup.md)), changes to the translation files only need a browser reload: no rebuild. On a deployed device, run `sudo ./deploy_systemd.sh` again to copy the changed files to `/opt/xerxes/public/`.

How to add a string or a whole new language is in [commands.md](commands.md).
