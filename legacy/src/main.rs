use axum::{
    routing::get,
    Router,
    Json,
};
use serde::Serialize;
use std::process::Command;
use std::net::SocketAddr;

// 1. Define our JSON response structure.
// The #[derive(Serialize)] macro automatically handles the JSON conversion.
#[derive(Serialize)]
struct SystemStatus {
    device_name: String,
    docker_running: bool,
    uptime: String,
}

#[tokio::main]
async fn main() {
    // 2. Build the router and attach our API endpoint
    let app = Router::new()
        .route("/api/status", get(get_system_status));

    // 3. Bind the server to all network interfaces on port 8080
    let addr = SocketAddr::from(([0, 0, 0, 0], 8080));
    println!("Xerxes Pi Admin Panel starting...");
    println!("Listening on http://{}", addr);

    // 4. Start the server
    let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
    axum::serve(listener, app).await.unwrap();
}

// --- Route Handlers (The CLI Wrappers) ---

async fn get_system_status() -> Json<SystemStatus> {

    // Test commands to verify that we can execute CLI commands from Rust.
    let hostname_output = Command::new("hostname")
        .output()
        .expect("Failed to execute hostname command");
    let device_name = String::from_utf8_lossy(&hostname_output.stdout).trim().to_string();

    // Test Docker status
    let docker_output = Command::new("systemctl")
        .arg("is-active")
        .arg("docker")
        .output()
        .expect("Failed to check docker status");
    let docker_running = String::from_utf8_lossy(&docker_output.stdout).trim() == "active";

    // Test system uptime
    let uptime_output = Command::new("uptime")
        .arg("-p")
        .output()
        .expect("Failed to execute uptime command");
    let uptime = String::from_utf8_lossy(&uptime_output.stdout).trim().to_string();

    // Construct the final response struct
    let status = SystemStatus {
        device_name,
        docker_running,
        uptime,
    };

    // Axum automatically converts this struct to a JSON HTTP response
    Json(status)
}