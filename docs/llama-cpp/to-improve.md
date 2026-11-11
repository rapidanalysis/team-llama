# llama.cpp Improvements

The llama.cpp integration provides working local model management and inference through the Xerxes backend, with a chat panel available on every tab. Status as of 11 November 2026 (`team-llama` branch `developG39`).

## Done so far

- **Deployment.** `deploy_systemd.sh` now builds llama-server if needed, deploys it with its libraries to `/opt/xerxes`, creates the models folder and checks llama-server runs. `make deb` packages it too.
- **llama.cpp pinned.** The official llama.cpp repo is a git submodule pinned to release b11062, built with `make llama`.
- **Recommended models.** A list of models tested on the Xerxes Pi, downloadable in one click, kept in `public/llama-models.json`.
- **Deleting models** from the panel, with the model in use protected.
- **Easier links.** Hugging Face page links are converted to download links automatically.
- **Chat panel.** **Ask Xerxes AI** talks to the local model from any tab, in the panel's language.
- **Translations.** The Llama tab and chat are available in English and Spanish.

## Model Downloading

Models can be downloaded from Hugging Face with a link, or from the recommended list.

Future improvements could include:

- Displaying download progress in the admin panel (the backend's downloader already supports a progress callback, but nothing uses it yet).
- Downloading in the background, so the request doesn't stay open for the whole download.
- Showing the model size and available storage before beginning a download.
- Checking that a downloaded file really is a GGUF model before listing it.
- Allowing users to search Hugging Face instead of pasting a link.

## Model Selection and Hardware Limits

The CM4 has limited memory and CPU performance compared with desktop systems, so model size and quantisation have a big effect on usability.

Future improvements could include:

- Checking available RAM before loading a model, and warning when a model may be too large.
- Showing why llama-server stopped (its error message) instead of only that it stopped.
- Testing more models and quantisation levels and adding the good ones to the recommended list.
- Re-running the benchmark on the pinned llama.cpp release (b11062), and on a CM5.

Testing with Qwen2.5 0.5B Instruct Q4_K_M produced about 21.44 tokens/s for prompt processing and 8.10 tokens/s for token generation on the CM4.

## API and Chat Improvements

Possible future improvements include:

- Streaming generated tokens to the chat as they are produced.
- Keeping the chat history across page reloads.
- Supporting additional OpenAI-compatible endpoints where useful.
- Adding settings such as context size, reply length and temperature to the Llama tab.
- Giving the assistant real information about the device (e.g. the dashboard values) so it can answer questions about it.

## Cloud Provider Support

The current implementation focuses on local inference through **llama.cpp**.

Optional cloud LLM providers could be added so that an administrator can choose between a local model and an external provider, for access to larger models when local hardware is too slow.

## Process and Service Management

llama-server is started and stopped by the Xerxes backend.

Future development could improve this by:

- Remembering that Llama was on, and starting it again after a backend restart or reboot.
- Providing more detailed health information (e.g. memory used, model still loading).
- Restarting llama-server automatically after an unexpected failure, where appropriate.
