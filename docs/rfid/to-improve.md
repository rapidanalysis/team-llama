# RFID To Improve

RFID integration is now part of the admin panel: it is written in C++, compiled into the single `xerxes_backend` program, and managed from the **I2C** tab of the web dashboard. Status as of 11 November 2026 (`team-llama` branch `developG39`).


## Done so far
*These were listed as not yet completed for the MVP.*

**C++ rewrite**  
- The `cpp-rewrite` port has been merged into `xerxes-backend` (`team-llama/src/rfid/`). The driver is tested against the I2C traffic of the original Python driver, using a simulated module.

**Integration with `xerxes-backend`**  
- The **I2C** tab has the same functionality as `tag-management.py` (list, add, edit, delete, live scan), plus reader settings and a connection test.

**Future improvements now done**
- **Logging:** each scan, the command run, its output and exit code are shown in **Reader Output** on the web page and written to the panel's log.
- **Safer tag file:** `tags.json` is written to a temporary file and then renamed, so a crash can't leave it half-written, and a damaged file is never overwritten.

## Known Issues
- Ethernet on the Xerxes Pi board is very flaky, it is recommended to use Wi-Fi wherever possible.
- Holding a tag to the reader for a long time causes it to scan and execute repeatedly (once a second).
- A command that never finishes stops the reader reading tags until that command is killed.
- Messages that come from the backend (for example errors when saving settings or tags) are shown in English even when the panel is set to Spanish.


## Future Improvements
*The following are potential features which would be nice to add to the system and may be added to the finalised system.*
- **Stop repeated execution:** only run a tag's command again once the tag has been removed from the reader (or after a configurable cool-down), instead of every second.
- **Run commands in the background:** so a long command (e.g. `apt update`) doesn't stop other tags being read, with a limit on how many can run at once and a time limit for each.
- **Run commands as a less privileged user:** instead of root, with an option per tag for commands that really need root.
- **Command history:** keep a log file of scans and results that survives a restart.
- **Feedback on the device:** show the scan result on the OLED display or with a buzzer/LED.
- **Use tag data, not just the UID:** for example signed data on the tag, so a copied UID isn't enough to run a command.
