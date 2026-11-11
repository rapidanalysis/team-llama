# Xerxes Documentation

## Contents
| Directory | Covers | Admin panel |
| --- | --- | --- |
| [`admin-panel/`](admin-panel/) | Installing, building, deploying and running the admin panel as a whole. **Start here**: the other directories assume the panel is installed | All tabs |
| [`newt/`](newt/) | Newt, the Pangolin tunnel client (open-source alternative to ngrok) | **Tunnels** tab |
| [`llama-cpp/`](llama-cpp/) | Running local AI models with llama.cpp, and the Ask Xerxes AI chat | **Llama** tab, **Ask Xerxes AI** button |
| [`rfid/`](rfid/) | The PiicoDev RFID reader: tapping tags to run commands | **I2C** tab |
| [`oled-display/`](oled-display/) | The SSD1306 OLED display: device information and a QR code to the panel | **I2C** tab |
| [`internationalisation/`](internationalisation/) | Panel languages (English and Spanish) and how to add more | Language menu in the top bar |

## Template
Each directory in this repo should follow the following format for its documentation.  
  
1. `setup.md`: getting the Xerxes Pi environment configured for installing/running the application, installing dependencies, and compiling the application (if needed).
2. `commands.md`: all commands and flags a user may interact with the CLI, and the controls and API endpoints of the feature in the admin panel.
3. `to-improve.md`: anything listed in the SRS or specified by the project sponsor that is not completed at present. Features that you might want to add in the future should be listed here too.
4. `architecture.md`: how the application is design and potential technical limitations of said design.

Shared steps (cloning `team-llama`, `install_deps.sh`, `make`, `deploy_systemd.sh`) are documented once in [`admin-panel/setup.md`](admin-panel/setup.md); each feature's `setup.md` links there and covers only what is specific to that feature.
