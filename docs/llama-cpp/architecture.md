# llama.cpp Architecture

The llama.cpp component provides local Large Language Model (LLM) inference for the Xerxes Pi Administration System. It is built into the C++ backend, while the actual inference runs in a separate **llama-server** process that the backend starts and stops.

This separation means the rest of the admin panel keeps working whether or not a model is running, and a model that runs out of memory can't take the panel down with it.

## Components

### llama.cpp (submodule)

`third_party/llama.cpp` is the official [ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) source, included as a git submodule and pinned to release **b11062**. `make llama` builds only its `llama-server` program (plus the shared libraries it needs) into `third_party/llama.cpp/build/bin/`. llama-server runs models on the CPU.

### Xerxes Backend

The main Xerxes backend listens on port 8000 and exposes the HTTP API used by the admin panel. The LLM routes are registered in **router.cpp**:

| Route | Purpose |
| --- | --- |
| `POST /api/llama/enable` | Start llama-server with an installed model |
| `POST /api/llama/disable` | Stop llama-server |
| `GET /api/llama/status` | Whether llama-server is running, its process ID and which model it loaded |
| `POST /api/llama/download` | Download a GGUF model from Hugging Face |
| `GET /api/llama/models` | List installed models |
| `POST /api/llama/delete` | Delete an installed model (not the one in use) |
| `POST /api/llama/v1/chat/completions` | Forward a chat request to llama-server |

### llama_manager

**llama_manager** is the single entry point between the routes and the other llama.cpp components. It:

- checks model filenames (no `/` or `..`, so requests can't reach files outside `models/`);
- starts and stops llama-server and remembers which model is loaded;
- refuses to start a second llama-server, or to delete the model that is loaded;
- lists, downloads and deletes models in the **models/** folder;
- forwards chat requests, but only while llama-server is running.

A lock ensures only one of these actions happens at a time.

### llama_process

**llama_process** manages the llama-server Linux process. When an administrator enables a model, it starts:

**llama-server → selected GGUF model → 127.0.0.1:8081, context size 4096**

The server is bound to 127.0.0.1, so it can only be reached from the Pi itself, through the Xerxes backend. To stop it, llama_process sends `SIGTERM` and waits up to 5 seconds before forcing it with `SIGKILL`.

### llama_proxy

**llama_proxy** forwards HTTP requests from the Xerxes backend to the local llama-server process, then returns the reply. The current chat flow is:

<img width="3520" height="2256" alt="llama_cpp_integration_architecture (1)" src="https://github.com/user-attachments/assets/4a046ef5-a677-45bd-a0d8-8320d915fd7b" />

The proxy forwards chat requests to llama-server's OpenAI-compatible chat completion endpoint and returns the full generated response once it is finished.

### model_downloader

**model_downloader** downloads GGUF model files with libcurl. It only accepts HTTPS links on `huggingface.co`, saves the file into **models/** under the name at the end of the link, and deletes the file if the download fails rather than leaving an incomplete model behind.

### Llama tab and recommended models

The **Llama** tab (`public/index.html`, `public/app.js`) has four cards: **Enable Llama**, **Recommended models**, **Model source** and **Installed models**.

The **Recommended models** list is a static file, `public/llama-models.json`, so adding a tested model needs no code change. The browser fetches it, checks each link, and shows a **Download** button or an **Installed** badge for each model.

The browser also tidies pasted links: a Hugging Face page link (`/blob/`) is turned into the direct download link (`/resolve/`), and `?download=true` is removed.

### Ask Xerxes AI (chat panel)

The chat panel is part of the page rather than a tab, so it stays open and keeps the conversation while you move between tabs. For each message it sends llama-server, through the backend:

- a short English **system prompt** telling the model it is the Xerxes Pi assistant, and to reply in the panel's current language;
- the **last 10 messages** of the conversation (the model's context is only 4096 tokens);
- a limit of **512 tokens** for the reply.

It waits up to 3 minutes for an answer. Failed turns are removed from the history and the message is put back in the input box so it can be retried. The conversation is kept only in the browser and is lost on reload.

## Model Management Flow

Installed models are stored as GGUF files inside the **models/** folder (`/opt/xerxes/models` on a deployed device). When the panel asks for the model list, **llama_manager** returns the names of the `.gguf` files in that folder.

To start inference:

1. The administrator selects an installed model and turns on **Enable Llama**.
2. The Xerxes backend sends the model filename to **llama_manager**.
3. **llama_manager** checks the name and that the file exists, and that llama-server isn't already running.
4. **llama_process** starts llama-server with the selected model.
5. llama-server loads the model and listens locally on port 8081.
6. After 4 seconds, the panel checks llama-server is still running. If it has already exited (usually not enough memory for the model), the panel turns the toggle off and says so.
7. Chat requests can now be forwarded through the Xerxes backend.

## Chat Request Flow

Chat requests are sent to the Xerxes backend rather than directly to llama-server. The backend passes them through **llama_manager** to **llama_proxy**, which connects to llama-server on 127.0.0.1:8081 and forwards the request to its OpenAI-compatible chat completion endpoint. The response comes back along the same path.

If llama-server is still loading the model, it answers with status `503`, which the chat shows as "still loading".

## Deployment

On a deployed device, `deploy_systemd.sh` (or the `.deb`) copies llama-server and its `.so` libraries to `/opt/xerxes/third_party/llama.cpp/build/bin/`, mirroring the repo layout, because the backend starts llama-server from that path relative to its working folder. The `xerxes` service sets `LD_LIBRARY_PATH` to the same folder, so llama-server finds its libraries even if the repo it was built in is moved or deleted.

llama-server is a child process of the backend, so stopping or restarting the `xerxes` service also stops it.

## Technical Limitations

| Limitation | Effect | Possible improvement |
| --- | --- | --- |
| Llama doesn't come back after a restart | After the panel or the Pi restarts, Llama is off and must be turned on again. | Save the enabled state and model, and start llama-server at boot if it was on. |
| No streaming | The chat shows nothing until the whole reply is generated, which can take a minute or more on a Pi. | Stream tokens as they are generated (llama-server supports this). |
| Downloads block a request | The download request stays open until the file is complete (up to an hour in the panel), with no progress bar. If the browser gives up first, the download carries on, but the panel can't show it. | Download in the background and report progress. |
| No memory or disk check | Nothing stops you downloading a model that won't fit on the disk, or starting one too big for the RAM; the latter is only noticed when llama-server exits. | Check free disk space and RAM against the model size first. |
| Fixed settings | Context size (4096), port (8081) and reply length (512 tokens) can't be changed from the panel. | Add them to the Llama tab. |
| Only `/v1/chat/completions` is forwarded | Other llama-server endpoints (e.g. embeddings) can't be used through the panel. | Forward more endpoints as needed. |
| Hugging Face only | Models from other sites must be copied into `models/` by hand. | Allow other trusted sources. |
| One model at a time | Switching models means turning Llama off, choosing another, and turning it on. | Usually enough for a Pi. |
| No login | Like the rest of the panel, anyone on the network can start, stop, download and delete models. | See [admin-panel/to-improve.md](../admin-panel/to-improve.md). |
