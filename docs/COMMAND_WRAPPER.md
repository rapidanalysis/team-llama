# Command Wrapper Documentation

## Overview
The CommandWrapper is a secure command execution system with allowlist-based access control.

## Usage

```cpp
#include "utils.h"

// Get singleton instance
auto& cmd = CommandWrapper::get_instance();
cmd.initialize_defaults();

// Execute a command
auto result = cmd.execute("command_name", "params", user_role::ADMIN);

// Check result
if (result.success) {
    std::cout << result.output;
}
```

## API

### execute()
```cpp
CommandResult execute(const std::string& name, 
                      const std::string& params = "",
                      user_role::Role role = user_role::VIEWER);
```

### Other methods
- `is_allowed(name)` - Check if command exists
- `get_command_info(name)` - Get command details
- `get_commands_by_category(cat)` - List commands by category
- `get_audit_log()` - Get execution history

## Allowed Commands

### SYSTEM (No admin required)
| Command | Description | Params |
|---------|-------------|--------|
| disk_usage | Show disk usage | - |
| uptime | System uptime | - |
| memory | Memory usage | - |
| processes | Top processes | 15, 20, 30 |
| load | System load avg | - |

### NETWORK (No admin required)
| Command | Description |
|---------|-------------|
| ip_addr | Show IP addresses |
| port_scan | Listening ports |
| ping | Ping host |
| route_table | Routing table |

### DOCKER (No admin required)
| Command | Description |
|---------|-------------|
| docker_ps | List containers |
| docker_images | List images |
| docker_stats | Container stats |

### CONTAINER (Admin required)
| Command | Description |
|---------|-------------|
| container_start | Start container |
| container_stop | Stop container |
| container_restart | Restart container |
| container_logs | View logs |

### ADMIN (Admin required)
| Command | Description |
|---------|-------------|
| reboot | Reboot system |
| shutdown | Shutdown system |
| systemctl | Service manager |

### TUNNEL (Admin required)
| Command | Description |
|---------|-------------|
| ngrok_config | Configure ngrok |
| ngrok_start | Start tunnel |
| ngrok_stop | Stop tunnel |
| tailscale_up | Login Tailscale |
| tailscale_down | Logout Tailscale |

## Roles

| Role | Access |
|------|--------|
| ADMIN | All commands |
| GUEST | Read-only + system commands |
| VIEWER | Status only |

## Security Features

1. **Allowlist only** - Only predefined commands execute
2. **Parameter validation** - Blocks shell metacharacters
3. **Path traversal blocked** - Rejects "../"
4. **Max param length** - 256 chars
5. **Timeout handling** - Prevents hanging
6. **Output sanitization** - Escapes dangerous chars
7. **Audit logging** - All executions logged

## Example

```cpp
// List Docker containers (guest can run)
auto result = cmd.execute("docker_ps", "", user_role::GUEST);

// Reboot system (admin only)
auto result = cmd.execute("reboot", "", user_role::ADMIN);
// Guest gets: "Permission denied"
```
