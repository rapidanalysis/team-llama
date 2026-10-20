# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
  # 1. Base Box: Debian 13 - The foundation of current Raspberry Pi OS
  config.vm.box = "bento/debian-13"
  config.vm.box_version = "202510.26.0"

  # 2. Networking: the backend listens on port 8000 (src/main.cpp).
  # When your backend runs in the VM, open http://localhost:8000 in your native browser.
  # 8080 -> 7272 is kept for the older (main branch) build; 8081-8084 are spare for other services.
  config.vm.network "forwarded_port", guest: 8000, host: 8000
  config.vm.network "forwarded_port", guest: 8080, host: 7272
  config.vm.network "forwarded_port", guest: 8081, host: 8081
  config.vm.network "forwarded_port", guest: 8082, host: 8082
  config.vm.network "forwarded_port", guest: 8083, host: 8083
  config.vm.network "forwarded_port", guest: 8084, host: 8084

  # 3. File Sharing: Map the host project folder to /workspace inside the VM.
  config.vm.synced_folder ".", "/workspace"

  # 4. VM Hardware Configuration (VirtualBox)
  config.vm.provider "virtualbox" do |vb|
    vb.name = "xerxes-pi-dev-env-cpp"
    vb.memory = "2048" # 2GB RAM
    vb.cpus = 4 # Quadcore to simulate CM5 module
  end

  # Automated Provisioning: URS Compliant C++ Toolset
  config.vm.provision "shell", inline: <<-SHELL
    # Stop on the first error so a failed install can't be reported as success
    set -euo pipefail
    export DEBIAN_FRONTEND=noninteractive

    echo "Updating apt repositories..."
    apt-get update -y

    echo "Installing core C++ tools, Samba, Docker, Nmap, ARM Cross-Compiler and build deps..."
    # libcurl4-openssl-dev: model downloader (-lcurl) | xxd: embeds public/ into the binary | cmake: llama.cpp
    apt-get install -y build-essential gdb curl git wget samba docker.io nmap \
      gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libcurl4-openssl-dev xxd cmake

    # Start Docker now and on every boot
    systemctl enable --now docker

    # Allow the vagrant user to run Docker without sudo (applies from the next login)
    usermod -aG docker vagrant

    echo "Installing header-only C++ libraries globally..."
    wget -q https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -O /usr/local/include/httplib.h
    wget -q https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -O /usr/local/include/json.hpp

    echo "Installing Tailscale..."
    curl -fsSL https://tailscale.com/install.sh | sh

    echo "Installing Ngrok..."
    curl -fsSL https://ngrok-agent.s3.amazonaws.com/ngrok.asc > /etc/apt/trusted.gpg.d/ngrok.asc
    echo "deb https://ngrok-agent.s3.amazonaws.com buster main" > /etc/apt/sources.list.d/ngrok.list
    apt-get update -y && apt-get install -y ngrok

    echo "Checking Docker..."
    systemctl is-active --quiet docker || { echo "ERROR: Docker daemon is not running"; exit 1; }
    id -nG vagrant | grep -qw docker || { echo "ERROR: vagrant user is not in the docker group"; exit 1; }
    echo "Docker OK: $(docker --version)"

    echo "======================================================"
    echo " Xerxes Pi C++ Vagrant Environment is URS Compliant!"
    echo " 1. Type 'vagrant ssh' to enter."
    echo " 2. Navigate to 'cd /workspace'."
    echo " 3. Build with 'make', run './xerxes_backend', open http://localhost:8000"
    echo "======================================================"
  SHELL
end

