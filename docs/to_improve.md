# To Improve
This is a list of features the current system lacks or its technical shortcoming that future team could improve. More ideas can be found in their respective directories.

1. Add TLS to xerxes-backend to mitigate MITM attacks from threat actors on the same network.
2. Implement some form of access control (i.e. login and account system) to gate certain functionality or the whole application from unauthenticated users.
3. `make test` is a bit of a shot in the dark, needs to be rewritten.
4. Add support for right-to-left languages (limitation of current internationalisation implementation).
5. Add a progress bar and traffic (in Mbps) for llama.cpp model download so a user can see the progress of large models.
6. Implement a bridge to allow llama.cpp to communicate with LLMs hosted by cloud providers (e.g. integration with Claude).
7. The ability to scan a smartphone on the Xerxes Pi's RFID reader instead of a separate tag.
8. Show tunnel status on the OLED display.
9. Store data on RFID tags to authenticate them instead of relying on their UID.
10. History/log of commands executed by the RFID reader.
11. Text could be clearer/more concise. E.g. the chatbot mentions says "How can I help you manage your server today?" when it has no abilities to manage said server.
12. Agent-based management, allowing the chatbot to configure the system/install Docker containers.
