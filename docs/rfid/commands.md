# RFID Commands

*The RFID reader is managed from the **I2C** tab of the admin panel (`http://<pi-address>:8000`), under **RFID Reader**. It replaces the old `xerxes-rfid.py` and `tag-management.py` programs; the table at the end shows where each of their features went.*

## Run Tag Commands
The toggle at the top of **RFID Reader** turns the reader on and off.

- **On:** tapping a registered tag runs its command on the Xerxes Pi. Unknown tags are noted and ignored.
- **Off:** tags don't run commands.
- The setting is remembered, so the reader turns back on by itself after the panel or the Pi restarts.

**NOTE: commands run with the admin panel's permissions, which is root when it is deployed as a service.**

The small boxes underneath show:
- **Module:** **Connected**, **Not found** (with the reason) or **Idle**. *Idle* is normal: the module is only used while the reader is on, a scan is waiting, or live scan is running.
- **Bus / Address:** where the panel looks for the module (normally `/dev/i2c-1`, `0x2C`).
- **Last Tag:** the ID of the last tag read.
- **Registered Tags:** how many tags are registered.

**Using the reader**  
Once on, hold a tag against the reader and it will:
1. Show the tag's ID (e.g. `DC:BD:6A:03`) in **Reader Output**
2. If the tag is registered: show `Tag known, running command: <command>` and run that tag's command. Its output and exit code are shown once it finishes (up to 16 KB of output per run).
3. If the tag is not registered: show `Tag unknown, no commands run`
4. Wait 1 second before looking for the next tag

**Clear** empties **Reader Output**. The same lines also go to the panel's log (`sudo journalctl -u xerxes -f`), starting with `[RFID]`.

**NOTE: changes to tags apply straight away, with no restart needed.**  
**NOTE: remove the tag from the reader once it has been read. A tag left on the reader is read again every second and its command will run again each time.**  
**NOTE: while a command is running, no other tags are read. A command ending in `&` runs in the background and doesn't hold up the reader.**

## Registered Tags
Lists every registered tag and its command, and which file they are stored in. **Refresh** reloads the list.

- **Edit:** change a tag's command in place. Click **Save** (or press `enter`) to keep it, **Cancel** (or press `esc`) to leave it unchanged.
- **Delete:** removes the tag after you confirm.

If `tags.json` can't be read (for example it contains invalid JSON), the list shows the error and the panel won't change the file until it is fixed or removed, so nothing is lost.

## Add Tag
1. Click **Scan Tag** and hold a tag against the reader within 30 seconds. `Scanned: <ID>` is shown and the **Tag ID** box is filled in. Click **Cancel** to stop waiting.  
   *Or* type the ID into **Tag ID** yourself (e.g. `DC:BD:6A:03`).
2. Type the command to run in **Command to run** (e.g. `apt update`).
3. Click **Add Tag**. `Tag added.` confirms it was saved.

If the tag is already registered, its current command is shown with an **Edit it instead** link.

**Scanning a tag to register it never runs that tag's command**, even while the reader is on.

Rules for what you enter:
- **Tag ID:** 4 to 10 bytes in hex. Letters can be upper or lower case, and the bytes can be separated by `:`, `-`, spaces or nothing (`dcbd6a03` becomes `DC:BD:6A:03`).
- **Command:** one line, up to 1024 characters, not empty.

## Live Scan
**Start Live Scan** shows the ID of every tag held to the reader, whether it is registered (and its command), and its raw read details, including `type`: `classic` for Mifare Classic tags, `ntag` for NTAG tags. **Stop Live Scan** stops it and **Clear** empties the list.

```
DC:BD:6A:03  registered → "apt update"
  detail: {'success': True, 'id_integers': [220, 189, 106, 3], 'id_formatted': 'DC:BD:6A:03', 'type': 'classic'}
04:30:AD:00:64:1C:03  not registered
  detail: {'success': True, 'id_integers': [4, 48, 173, 0, 100, 28, 3], 'id_formatted': '04:30:AD:00:64:1C:03', 'type': 'ntag'}
```

- Live scan itself never runs commands. If **Run Tag Commands** is on, the reader still does.
- A tag held on the reader is shown repeatedly (about 10 times a second) until it is removed.
- Live scan stops by itself when you leave the **I2C** tab or close the page.

## Reader Settings
- **I2C Bus:** the bus number, e.g. `1` for `/dev/i2c-1`.
- **Address:** the module's I2C address, e.g. `0x2C`.
- **Tag File:** the full path of the tags file, ending in `.json`. Leave it empty for the default (`/root/.xerxes/tags.json` when deployed).
- **Save** applies the settings. **Defaults** fills in the defaults (click **Save** to apply them).
- **Test Connection** checks the module answers at that bus and address.
- The badge in the corner shows the reader version (`2.0.0`).

## tags.json
Tags are stored in `tags.json`, in the same format as the old Python version. It is created when the first tag is added. It is recommended to manage tags from the panel, but the file can be edited by hand if needed; changes apply on the next tap.
```json
{
    "tags": [
        {
            "id": "DC:BD:6A:03",
            "command": "apt update"
        },
        {
            "id": "BE:8C:6C:03",
            "command": "echo hello"
        }
    ]
}
```
- `id`: the tag's ID, exactly as shown by *Live Scan* (capital or lowercase letters both work)
- `command`: the Bash command to run when the tag is read

**NOTE: if the same `id` appears more than once, only the last entry is used.**

## Saved settings
Settings are saved in `rfid.conf` in the panel's settings folder (`/root/.xerxes/` when deployed, `~/.xerxes/` when run by hand, or `$XERXES_CONFIG_DIR` if set):
```
bus=1
address=0x2C
tags=/root/.xerxes/tags.json
run=1
```

## API
The **I2C** tab uses these endpoints, which can also be called with `curl` (replace `localhost` with the Pi's address from another computer). Every reply includes `success`, and `error` when something went wrong.

| Command | What it does |
| --- | --- |
| `curl localhost:8000/api/rfid/status` | Settings, whether the reader and live scan are on, module state, last tag, and any command running |
| `curl -X POST localhost:8000/api/rfid/run -d '{"enabled":true}'` | Turns **Run Tag Commands** on (or off with `false`) |
| `curl localhost:8000/api/rfid/tags` | Lists registered tags and the tag file path |
| `curl -X POST localhost:8000/api/rfid/tags -d '{"id":"DC:BD:6A:03","command":"apt update","mode":"add"}'` | Adds a tag (`"mode":"edit"` changes an existing one) |
| `curl -X POST localhost:8000/api/rfid/tags/delete -d '{"id":"DC:BD:6A:03"}'` | Deletes a tag |
| `curl -X POST localhost:8000/api/rfid/scan/start` | Waits up to 30 seconds for a tag to register; then poll `GET /api/rfid/scan` (`/api/rfid/scan/cancel` stops it) |
| `curl -X POST localhost:8000/api/rfid/live/start` | Starts live scan (`/api/rfid/live/stop` stops it) |
| `curl 'localhost:8000/api/rfid/events?since=0'` | Reader and live scan events since event number `since`, and the `next` number to ask for |
| `curl -X POST localhost:8000/api/rfid/settings -d '{"bus":1,"address":"0x2C","tags_path":""}'` | Changes the reader settings |
| `curl -X POST localhost:8000/api/rfid/probe` | Tests the connection to the module |

## Where the old programs' features went

| `xerxes-rfid` (Python) | I2C tab |
|---------------|---------|
| `python xerxes-rfid.py` | **Run Tag Commands** toggle. Its output appears in **Reader Output** and the panel's log |
| `tag-management.py` → List tags | **Registered Tags** |
| `tag-management.py` → Add tag | **Add Tag** → **Scan Tag** (you can also type the ID) |
| `tag-management.py` → Edit / Delete tag | **Edit** / **Delete** on each registered tag |
| "already registered, edit instead?" | **Edit it instead** link |
| `tag-management.py` → Live scan | **Live Scan** (leaving the tab stops it, like `control` + `c`) |
| Bus, address and tag file options | **Reader Settings** (plus **Test Connection**) |
