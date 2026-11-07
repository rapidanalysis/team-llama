#include "router.h"
#include "system_ops.h"
#include "network_config.h"
#include "docker_manager.h"
#include "llama_manager.h"
#include "newt_manager.h"
#include "rfid_manager.h"
#include "oled_manager.h"
#include "json.hpp"
#include <iostream>
#include <cctype>
#include <cstdlib>

// Helper: adds CORS headers so the frontend JS can call the API
static void set_cors_headers(httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

void setup_routes(httplib::Server& svr) {

    // Serve static frontend files (index.html, styles.css, app.js)
    // Path is relative to where the binary is run from
    svr.set_mount_point("/", "./public");

    // ------------------------------------------------------------------
    // GET /api/status
    // Returns a simple JSON confirming the backend is online.
    // The frontend dashboard will call this on page load.
    // ------------------------------------------------------------------
    svr.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        nlohmann::json response = {
            {"status", "online"},
            {"message", "Xerxes Pi Admin Backend is running"}
        };

        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/command
    // Accepts a JSON body: { "command": "disk_usage" }
    // Passes the command name to the Command Module (system_ops) for
    // validation and safe execution. Returns stdout/stderr as JSON.
    // ------------------------------------------------------------------
    svr.Post("/api/command", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string command_name = body.value("command", "");
            std::string param        = body.value("param",   "");

            std::cout << "[Command Request] command=" << command_name;
            if (!param.empty()) std::cout << " param=" << param;
            std::cout << "\n";

            std::string output;

            // Docker and container commands go through CommandWrapper (supports params + role checks)
            if (command_name.rfind("docker_", 0) == 0 || command_name.rfind("container_", 0) == 0) {
                CommandResult result = system_ops::execute_command(command_name, param, user_role::ADMIN);
                if (result.success) {
                    output = result.output.empty() ? "[ok] Command completed." : result.output;
                } else {
                    output = "[error] " + result.error;
                }
            } else {
                output = system_ops::run_command(command_name);
            }

            nlohmann::json response = {
                {"status", "ok"},
                {"command", command_name},
                {"output", output}
            };

            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = {
                {"status", "error"},
                {"message", "Invalid request body"}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/dashboard
    // Returns the four status card values in a single JSON response.
    // Called by the frontend on page load to populate the dashboard.
    // ------------------------------------------------------------------
    svr.Get("/api/dashboard", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        nlohmann::json response = {
            {"hostname", system_ops::get_hostname()},
            {"uptime",   system_ops::get_uptime()},
            {"disk",     system_ops::get_disk_usage()},
            {"ip",       system_ops::get_ip_address()}
        };

        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/hostname
    // Accepts { "hostname": "new-name" } and renames the device.
    // Validation is handled inside system_ops::set_hostname().
    // ------------------------------------------------------------------
    svr.Post("/api/hostname", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string new_hostname = body.value("hostname", "");

            std::cout << "[Hostname Request] new=" << new_hostname << "\n";

            std::string result = system_ops::set_hostname(new_hostname);

            nlohmann::json response = {
                {"status", "ok"},
                {"output", result}
            };

            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = {
                {"status", "error"},
                {"message", "Invalid request body"}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/services
    // Returns active/inactive status of smbd, tailscaled, and ngrok.
    // Called on page load and after service control commands.
    // ------------------------------------------------------------------
    svr.Get("/api/services", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        nlohmann::json response = {
            {"smbd",       system_ops::get_service_status("smbd")},
            {"tailscaled", system_ops::get_service_status("tailscaled")},
            {"ngrok",      system_ops::get_service_status("ngrok")}
        };

        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/netinfo
    // Returns primary interface name, IP, gateway, DNS servers, and
    // the full list of non-loopback interfaces for the network config UI.
    // ------------------------------------------------------------------
    svr.Get("/api/netinfo", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        nlohmann::json response = network_config::get_network_info();
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/network
    // Accepts { "interface": "eth0", "mode": "static"|"dhcp",
    //           "ip": "x.x.x.x/y", "gateway": "x.x.x.x", "dns": "x.x.x.x" }
    // Writes or removes a static IP config block in /etc/dhcpcd.conf.
    // ------------------------------------------------------------------
    svr.Post("/api/network", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string iface = body.value("interface", "");
            std::string mode  = body.value("mode", "dhcp");

            std::cout << "[Network Config] iface=" << iface << " mode=" << mode << "\n";

            std::string result;
            if (mode == "dhcp") {
                result = network_config::set_dhcp(iface);
            } else if (mode == "static") {
                std::string ip      = body.value("ip",      "");
                std::string gateway = body.value("gateway", "");
                std::string dns     = body.value("dns",     "");
                result = network_config::set_static(iface, ip, gateway, dns);
            } else {
                nlohmann::json error = { {"status", "error"}, {"message", "Invalid mode — use 'dhcp' or 'static'"} };
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            nlohmann::json response = { {"status", "ok"}, {"output", result} };
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = { {"status", "error"}, {"message", "Invalid request body"} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/tailscale/up
    // Accepts { "authkey": "tskey-auth-..." } (authkey is optional).
    // Auth key is validated here — only alphanumeric, hyphens, and
    // underscores are allowed. The validated key is passed to
    // system_ops::tailscale_up() which builds the safe shell command.
    // ------------------------------------------------------------------
    svr.Post("/api/tailscale/up", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string authkey = body.value("authkey", "");

            for (char c : authkey) {
                if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') {
                    nlohmann::json error = { {"status", "error"}, {"message", "Invalid auth key — only letters, numbers, hyphens, and underscores are allowed"} };
                    res.status = 400;
                    res.set_content(error.dump(), "application/json");
                    return;
                }
            }

            std::cout << "[Tailscale] up requested, authkey=" << (authkey.empty() ? "(none)" : "(provided)") << "\n";

            std::string output = system_ops::tailscale_up(authkey);
            nlohmann::json response = { {"status", "ok"}, {"output", output} };
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = { {"status", "error"}, {"message", "Invalid request body"} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/docker/run
    // Accepts a structured JSON body describing a docker run invocation.
    // Each field is validated independently before the command is built.
    //
    // Body shape:
    //   {
    //     "image":   "nginx:latest",          // required
    //     "name":    "my-nginx",              // optional
    //     "ports":   ["8080:80"],             // optional
    //     "volumes": ["/host/path:/ctr/path"],// optional
    //     "env":     ["KEY=value"],           // optional
    //     "restart": "unless-stopped",        // optional
    //     "detach":  true                     // optional, default true
    //   }
    // ------------------------------------------------------------------
    svr.Post("/api/docker/run", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);

            DockerRunConfig config;
            config.image   = body.value("image",   "");
            config.name    = body.value("name",    "");
            config.restart = body.value("restart", "");
            config.detach  = body.value("detach",  true);

            if (body.contains("ports") && body["ports"].is_array())
                for (const auto& p : body["ports"])
                    config.ports.push_back(p.get<std::string>());

            if (body.contains("volumes") && body["volumes"].is_array())
                for (const auto& v : body["volumes"])
                    config.volumes.push_back(v.get<std::string>());

            if (body.contains("env") && body["env"].is_array())
                for (const auto& e : body["env"])
                    config.env.push_back(e.get<std::string>());

            std::cout << "[Docker Run] image=" << config.image
                      << " name="    << config.name
                      << " ports="   << config.ports.size()
                      << " volumes=" << config.volumes.size()
                      << " env="     << config.env.size() << "\n";

            DockerRunResult result = docker_manager::run(config);

            nlohmann::json response = {
                {"status",  result.success ? "ok" : "error"},
                {"output",  result.output},
                {"error",   result.error}
            };

            res.status = result.success ? 200 : 400;
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = {
                {"status",  "error"},
                {"message", "Invalid request body"}
            };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/newt/enable
    // Accepts { "endpoint": "https://pangolin.example.com",
    //           "newt_id": "...", "secret": "..." } and (re)starts the
    // xerxes-newt container. An empty secret reuses the saved one.
    // The secret is never logged or returned.
    // ------------------------------------------------------------------
    svr.Post("/api/newt/enable", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string endpoint = body.value("endpoint", "");
            std::string newt_id  = body.value("newt_id",  "");
            std::string secret   = body.value("secret",   "");

            std::cout << "[Newt] enable endpoint=" << endpoint << " id=" << newt_id
                      << " secret=" << (secret.empty() ? "(saved)" : "(provided)") << "\n";

            auto result = newt_manager::enable(endpoint, newt_id, secret);
            nlohmann::json response = {
                {"success", result.success},
                {"error",   result.error},
                {"output",  result.output}
            };
            res.status = result.success ? 200 : 400;
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = { {"success", false}, {"error", "Invalid request body"} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/newt/disable
    // Stops and removes the xerxes-newt container (saved config is kept).
    // ------------------------------------------------------------------
    svr.Post("/api/newt/disable", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        std::cout << "[Newt] disable\n";
        auto result = newt_manager::disable();
        nlohmann::json response = {
            {"success", result.success},
            {"error",   result.error},
            {"output",  result.output}
        };
        res.status = result.success ? 200 : 500;
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/newt/status
    // Returns { "state", "container", "endpoint", "newt_id",
    //           "has_secret", "detail" }. state is one of: connected,
    // connecting, auth_failed, error, stopped, not_configured,
    // docker_unavailable.
    // ------------------------------------------------------------------
    svr.Get("/api/newt/status", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        auto s = newt_manager::status();
        nlohmann::json response = {
            {"state",      s.state},
            {"container",  s.container},
            {"endpoint",   s.endpoint},
            {"newt_id",    s.newt_id},
            {"has_secret", s.has_secret},
            {"detail",     s.detail}
        };
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/newt/logs
    // Returns { "output": "<last 100 lines of the Newt container log>" }.
    // ------------------------------------------------------------------
    svr.Get("/api/newt/logs", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        nlohmann::json response = { {"output", newt_manager::logs(100)} };
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/llama/enable
    // Accepts { "model": "file.gguf" } and starts llama-server against
    // models/<model>. Returns { "success": bool, "error": string }.
    // ------------------------------------------------------------------
    svr.Post("/api/llama/enable", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string model = body.value("model", "");

            if (model.empty()) {
                nlohmann::json error = { {"success", false}, {"error", "missing \"model\" field"} };
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            std::cout << "[Llama] enable model=" << model << "\n";

            auto result = llama_manager::enable(model);
            nlohmann::json response = { {"success", result.success}, {"error", result.error} };
            res.status = result.success ? 200 : 500;
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = { {"success", false}, {"error", "Invalid request body"} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // POST /api/llama/disable
    // Stops llama-server if it is running.
    // ------------------------------------------------------------------
    svr.Post("/api/llama/disable", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        std::cout << "[Llama] disable\n";
        llama_manager::disable();

        nlohmann::json response = { {"success", true} };
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // GET /api/llama/status
    // Returns { "running": bool, "pid": int } (pid is -1 when stopped).
    // ------------------------------------------------------------------
    svr.Get("/api/llama/status", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        auto s = llama_manager::status();
        nlohmann::json response = { {"running", s.running}, {"pid", s.pid} };
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/llama/download
    // Accepts { "url": "https://huggingface.co/..." } and downloads the
    // model into models/. Blocks until the download finishes.
    // Returns { "success": bool, "error": string }.
    // ------------------------------------------------------------------
    svr.Post("/api/llama/download", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string url = body.value("url", "");

            if (url.empty()) {
                nlohmann::json error = { {"success", false}, {"error", "missing \"url\" field"} };
                res.status = 400;
                res.set_content(error.dump(), "application/json");
                return;
            }

            std::cout << "[Llama] download url=" << url << "\n";

            auto result = llama_manager::download(url);
            nlohmann::json response = { {"success", result.success}, {"error", result.error} };
            res.status = result.success ? 200 : 500;
            res.set_content(response.dump(), "application/json");

        } catch (const std::exception& e) {
            nlohmann::json error = { {"success", false}, {"error", "Invalid request body"} };
            res.status = 400;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // GET /api/llama/models
    // Returns a JSON array of .gguf filenames found in models/.
    // ------------------------------------------------------------------
    svr.Get("/api/llama/models", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);

        nlohmann::json response = llama_manager::installed_models();
        res.set_content(response.dump(), "application/json");
    });

    // ------------------------------------------------------------------
    // POST /api/llama/v1/chat/completions
    // Forwards the request body unchanged to llama-server's
    // OpenAI-compatible endpoint and relays its response.
    // ------------------------------------------------------------------
    svr.Post("/api/llama/v1/chat/completions", [](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);

        try {
            auto proxied = llama_manager::proxy("/v1/chat/completions", "POST", req.body);
            res.status = proxied.status;
            res.set_content(proxied.body, proxied.content_type);
        } catch (const std::exception& e) {
            nlohmann::json error = { {"error", e.what()} };
            res.status = 502;
            res.set_content(error.dump(), "application/json");
        }
    });

    // ------------------------------------------------------------------
    // RFID reader (I2C tab), see include/rfid_manager.h.
    // Responses are { "success": bool, "error": "...", ... }.
    // ------------------------------------------------------------------
    auto rfid_send = [](httplib::Response& res, const rfid_manager::Reply& r) {
        res.status = r.http_status;
        res.set_content(r.body.dump(), "application/json");
    };
    auto rfid_bad_body = [](httplib::Response& res) {
        nlohmann::json error = { {"success", false}, {"error", "Invalid request body"} };
        res.status = 400;
        res.set_content(error.dump(), "application/json");
    };

    // GET /api/rfid/status
    svr.Get("/api/rfid/status", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::status());
    });

    // POST /api/rfid/run  { "enabled": true|false }
    svr.Post("/api/rfid/run", [rfid_send, rfid_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            rfid_send(res, rfid_manager::set_run_enabled(body.at("enabled").get<bool>()));
        } catch (const std::exception&) { rfid_bad_body(res); }
    });

    // POST /api/rfid/settings  { "bus": 1, "address": "0x2C", "tags_path": "..." }
    // address may be a number or a hex string; empty tags_path = default.
    svr.Post("/api/rfid/settings", [rfid_send, rfid_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            auto as_int = [](const nlohmann::json& v) -> long {
                if (v.is_number_integer()) return v.get<long>();
                std::string s = v.get<std::string>();
                char* end = nullptr;
                long n = std::strtol(s.c_str(), &end, 0);
                if (s.empty() || *end != '\0') return -1;
                return n;
            };
            long bus  = as_int(body.at("bus"));
            long addr = as_int(body.at("address"));
            std::string path = body.value("tags_path", "");
            rfid_send(res, rfid_manager::update_settings(static_cast<int>(bus), static_cast<int>(addr), path));
        } catch (const std::exception&) { rfid_bad_body(res); }
    });

    // POST /api/rfid/probe
    svr.Post("/api/rfid/probe", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::probe());
    });

    // GET /api/rfid/tags
    svr.Get("/api/rfid/tags", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::list_tags());
    });

    // POST /api/rfid/tags  { "id", "command", "mode": "add"|"edit" }
    svr.Post("/api/rfid/tags", [rfid_send, rfid_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string id      = body.value("id", "");
            std::string command = body.value("command", "");
            std::string mode    = body.value("mode", "add");
            std::cout << "[RFID] " << mode << " tag " << id << "\n";
            rfid_send(res, mode == "edit" ? rfid_manager::edit_tag(id, command)
                                          : rfid_manager::add_tag(id, command));
        } catch (const std::exception&) { rfid_bad_body(res); }
    });

    // POST /api/rfid/tags/delete  { "id": "DC:BD:6A:03" }
    svr.Post("/api/rfid/tags/delete", [rfid_send, rfid_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            rfid_send(res, rfid_manager::delete_tag(body.value("id", "")));
        } catch (const std::exception&) { rfid_bad_body(res); }
    });

    // Scan a tag to register it: start, then poll GET /api/rfid/scan until
    // state is no longer "waiting".
    svr.Post("/api/rfid/scan/start", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::start_scan());
    });
    svr.Get("/api/rfid/scan", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::scan_state());
    });
    svr.Post("/api/rfid/scan/cancel", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::cancel_scan());
    });

    // Live scan. Reads arrive through /api/rfid/events.
    svr.Post("/api/rfid/live/start", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::start_live());
    });
    svr.Post("/api/rfid/live/stop", [rfid_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        rfid_send(res, rfid_manager::stop_live());
    });

    // GET /api/rfid/events?since=N
    svr.Get("/api/rfid/events", [rfid_send](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        uint64_t since = 0;
        if (req.has_param("since")) {
            try { since = std::stoull(req.get_param_value("since")); } catch (const std::exception&) { since = 0; }
        }
        rfid_send(res, rfid_manager::events(since));
    });

    // ------------------------------------------------------------------
    // OLED display (I2C tab), see include/oled_manager.h.
    // Responses are { "success": bool, "code": "...", "error": "...", ... }.
    // ------------------------------------------------------------------
    auto oled_send = [](httplib::Response& res, const oled_manager::Reply& r) {
        res.status = r.http_status;
        res.set_content(r.body.dump(), "application/json");
    };
    auto oled_bad_body = [](httplib::Response& res) {
        nlohmann::json error = { {"success", false}, {"code", "bad_request"}, {"error", "Invalid request body"} };
        res.status = 400;
        res.set_content(error.dump(), "application/json");
    };

    // GET /api/oled/status
    svr.Get("/api/oled/status", [oled_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        oled_send(res, oled_manager::status());
    });

    // POST /api/oled/power  { "enabled": true|false }
    svr.Post("/api/oled/power", [oled_send, oled_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            oled_send(res, oled_manager::set_enabled(body.at("enabled").get<bool>()));
        } catch (const std::exception&) { oled_bad_body(res); }
    });

    // POST /api/oled/display  { "mode": "stats"|"qr", "stats": ["hostname", "ip", ...] }
    svr.Post("/api/oled/display", [oled_send, oled_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            std::string mode = body.at("mode").get<std::string>();
            std::vector<std::string> stats = body.value("stats", std::vector<std::string>());
            oled_send(res, oled_manager::update_display(mode, stats));
        } catch (const std::exception&) { oled_bad_body(res); }
    });

    // POST /api/oled/settings  { "bus": 1, "address": "0x3C" }
    // address may be a number or a hex string.
    svr.Post("/api/oled/settings", [oled_send, oled_bad_body](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        try {
            nlohmann::json body = nlohmann::json::parse(req.body);
            auto as_int = [](const nlohmann::json& v) -> long {
                if (v.is_number_integer()) return v.get<long>();
                std::string s = v.get<std::string>();
                char* end = nullptr;
                long n = std::strtol(s.c_str(), &end, 0);
                if (s.empty() || *end != '\0') return -1;
                return n;
            };
            long bus  = as_int(body.at("bus"));
            long addr = as_int(body.at("address"));
            oled_send(res, oled_manager::update_settings(static_cast<int>(bus), static_cast<int>(addr)));
        } catch (const std::exception&) { oled_bad_body(res); }
    });

    // POST /api/oled/probe
    svr.Post("/api/oled/probe", [oled_send](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        oled_send(res, oled_manager::probe());
    });

    // GET /api/oled/preview?mode=stats&stats=ip,dns
    // Without mode, previews the saved settings.
    svr.Get("/api/oled/preview", [oled_send](const httplib::Request& req, httplib::Response& res) {
        set_cors_headers(res);
        std::string mode = req.has_param("mode") ? req.get_param_value("mode") : "";
        std::string stats = req.has_param("stats") ? req.get_param_value("stats") : "";
        oled_send(res, oled_manager::preview(mode, oled_manager::split_stats(stats)));
    });

    // ------------------------------------------------------------------
    // OPTIONS /* (preflight handler for CORS)
    // Browsers send an OPTIONS request before POST — this handles that.
    // ------------------------------------------------------------------
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        res.status = 204;
    });
}
