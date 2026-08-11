#include "router.h"
#include "system_ops.h"
#include "network_config.h"
#include "docker_manager.h"
#include "json.hpp"
#include <iostream>
#include <cctype>

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
    // OPTIONS /* (preflight handler for CORS)
    // Browsers send an OPTIONS request before POST — this handles that.
    // ------------------------------------------------------------------
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        set_cors_headers(res);
        res.status = 204;
    });
}
