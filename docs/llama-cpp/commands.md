# llama.cpp Commands

Day-to-day, Llama is controlled from the **Llama** tab of the admin panel and used through the **Ask Xerxes AI** chat, so you never need a terminal. This page lists the commands for when you do: checking on it, scripting, adding recommended models and updating llama.cpp.

Replace `127.0.0.1` with the Pi's address when running `curl` from another computer.

## Using the Llama tab

| Control | What it does |
| --- | --- |
| **Enable Llama** toggle | Starts llama-server with the model selected under **Installed models**. Turning it off stops llama-server and frees its memory. Llama is off after every restart of the panel or the Pi |
| **Recommended models** → **Download** | Downloads a model tested on the Xerxes Pi. Shows **Installed** once it's on the device |
| **Model source** → **Download** | Downloads the `.gguf` file at a Hugging Face link you paste in |
| **Installed models** | Choose which model to run. The choice is locked while Llama is on: turn it off to switch |
| **×** next to an installed model | Deletes the model file after asking you to confirm. The model that is running can't be deleted |

Only one download runs at a time.

## Using Ask Xerxes AI

Click **Ask Xerxes AI** at the bottom right of any tab. The chat stays open, and keeps the conversation, when you change tabs.

- The dot next to the title shows whether Llama is running.
- Replies come in the panel's current language.
- The last 10 messages are sent with each question so the model remembers the conversation. Older messages are forgotten (the model can only hold 4096 tokens).
- A reply can take up to 3 minutes on a Pi. If Llama is off, the chat says so and offers a button to open the Llama tab.

The conversation is lost if you reload the page.

## Check LLM status

```bash
curl http://127.0.0.1:8000/api/llama/status
```

Example while running:

```json
{
  "running": true,
  "pid": 65930,
  "model": "qwen2.5-0.5b-instruct-q4_k_m.gguf"
}
```

When stopped, `running` is `false`, `pid` is `-1` and `model` is `""`.

## List installed models

```bash
curl http://127.0.0.1:8000/api/llama/models
```

Example response:

```json
[
  "qwen2.5-0.5b-instruct-q4_k_m.gguf"
]
```

## Start llama-server

```bash
curl -X POST http://127.0.0.1:8000/api/llama/enable \
  -H "Content-Type: application/json" \
  -d '{"model":"qwen2.5-0.5b-instruct-q4_k_m.gguf"}'
```

The model must be a file in the `models/` folder. Returns `{"success": true, "error": ""}`, or `"success": false` with the reason (for example `llama-server is already running` or `Model not found`).

## Stop llama-server

```bash
curl -X POST http://127.0.0.1:8000/api/llama/disable
```

The backend asks llama-server to exit and waits up to 5 seconds before forcing it.

## Download a model

```bash
curl -X POST http://127.0.0.1:8000/api/llama/download \
  -H "Content-Type: application/json" \
  -d '{"url":"https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf"}'
```

- The backend only accepts links starting with `https://huggingface.co/`. When calling the API directly, use the `/resolve/` link (the panel converts `/blob/` links for you).
- The file is saved in `models/` with the name at the end of the link.
- The request doesn't answer until the download has finished. A failed download is deleted rather than left half-written.

## Delete a model

```bash
curl -X POST http://127.0.0.1:8000/api/llama/delete \
  -H "Content-Type: application/json" \
  -d '{"model":"qwen2.5-0.5b-instruct-q4_k_m.gguf"}'
```

Refused with `This model is in use. Turn Llama off before deleting it.` if llama-server has that model loaded.

## Send a chat request

Once llama-server is running, send an OpenAI-style chat request through the panel:

```bash
curl -X POST http://127.0.0.1:8000/api/llama/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "messages": [
      { "role": "user", "content": "Hello" }
    ],
    "max_tokens": 512
  }'
```

The backend forwards the request unchanged to llama-server's `/v1/chat/completions` endpoint and returns its answer. The reply text is in `choices[0].message.content`. If llama-server isn't running, it returns status `502` with `{"error": "llama-server is not running"}`.

## Add a recommended model

The **Recommended models** list comes from `public/llama-models.json` in `team-llama`. Once a model has been tested and runs well on the Xerxes Pi, add an entry there. No code change or rebuild is needed; on a deployed device, run `sudo ./deploy_systemd.sh` to copy the new file across.

```json
{
  "models": [
    {
      "name": "Qwen2.5 0.5B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf",
      "size": "491 MB",
      "note": {
        "en": "Small and quick to load. A good first model for short questions and simple chat.",
        "es": "Pequeño y rápido de cargar. Un buen primer modelo para preguntas cortas y conversaciones sencillas."
      }
    }
  ]
}
```

- `name` and `url` are required. The URL must be a Hugging Face `.gguf` link; a `/blob/` link copied from the Hugging Face page also works.
- `size` and `note` are optional. A missing translation in `note` falls back to `en`.
- An entry with a bad URL is skipped (and noted in the browser's console).

## Update llama.cpp to a newer release

To move to another [llama.cpp release](https://github.com/ggml-org/llama.cpp/releases), e.g. `b12119`:

```bash
cd third_party/llama.cpp
git fetch --tags origin && git checkout b12119
cd ../..
git add third_party/llama.cpp   # then update the tag in .gitmodules' comment and the README
git commit -m "Update llama.cpp to b12119"
```

Then rebuild llama-server and retest the recommended models on the Xerxes Pi:

```bash
rm -rf third_party/llama.cpp/build && make llama
```

Everyone else picks up the new release with `git pull && git submodule update`. On a deployed device, delete `third_party/llama.cpp/build` before running `sudo ./deploy_systemd.sh` so llama-server is rebuilt.

## Check running processes

```bash
pgrep -af llama-server
```

## Check listening ports

```bash
ss -ltnp | grep -E ':8000|:8081'
```

The expected ports are:

- **8000** - Xerxes backend (admin panel)
- **8081** - local llama-server (only while Llama is on)

## Check the panel's log

On a deployed device, Llama actions appear in the service log as lines starting with `[Llama]`:

```bash
sudo journalctl -u xerxes -f
```
