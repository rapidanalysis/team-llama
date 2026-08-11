# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|
  # 1. Base Box: Debian 13 - The foundation of current Raspberry Pi OS
  config.vm.box = "bento/debian-13"
  config.vm.box_version = "202510.26.0"

  # 2. Networking: Forward port 8080 to the host machine.
  # When your backend runs in the VM, you can view it at http://localhost:7272 on your native browser.
  # Additionally, forward port 8081 for any additional services you may want to run.
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
    export DEBIAN_FRONTEND=noninteractive

    echo "Updating apt repositories..."
    apt-get update -y

    echo "Installing core C++ tools, Samba, Docker, Nmap, and ARM Cross-Compiler..."
    apt-get install -y build-essential gdb curl git wget samba docker.io nmap gcc-aarch64-linux-gnu g++-aarch64-linux-gnu

    # Allow the vagrant user to run Docker without sudo
    usermod -aG docker vagrant

    echo "Installing header-only C++ libraries globally..."
    wget -q https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h -O /usr/local/include/httplib.h
    wget -q https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp -O /usr/local/include/json.hpp

    echo "Installing Tailscale..."
    curl -fsSL https://tailscale.com/install.sh | sh

    echo "Installing Ngrok..."
    curl -s https://ngrok-agent.s3.amazonaws.com/ngrok.asc \
      | sudo tee /etc/apt/trusted.gpg.d/ngrok.asc >/dev/null \
      && echo "deb https://ngrok-agent.s3.amazonaws.com buster main" \
      | sudo tee /etc/apt/sources.list.d/ngrok.list \
      && sudo apt update && sudo apt install ngrok

    echo "======================================================"
    echo " Xerxes Pi C++ Vagrant Environment is URS Compliant!"
    echo " 1. Type 'vagrant ssh' to enter."
    echo " 2. Navigate to 'cd /workspace'."
    echo "======================================================"
  SHELL
end

