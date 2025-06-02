#!/bin/bash

setup_macos() {
    # Install Homebrew if not installed
    if ! command -v brew &> /dev/null; then
        echo "Installing Homebrew..."
        /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    fi

    # Install Colima if not installed
    if ! command -v colima &> /dev/null; then
        echo "Installing Colima..."
        brew install colima
    fi

    # Install Docker CLI if not installed
    if ! command -v docker &> /dev/null; then
        echo "Installing Docker CLI..."
        brew install docker
    fi

    # Check if Colima is running with our desired configuration
    if colima status &>/dev/null; then
        current_config=$(colima status --json | jq -r '"\(.cpu),\(.memory),\(.disk),\(.vm_type),\(.arch),\(.mount_type)"')
        desired_config="4,8589934592,21474836480,null,aarch64,virtiofs"
        
        if [ "$current_config" = "$desired_config" ]; then
            echo "Colima is already running with desired configuration"
            return
        else
            echo "Colima is running with different configuration, restarting..."
            colima stop
        fi
    fi

    # Start Colima with Ubuntu 24.04 and appropriate settings
    echo "Starting Colima with Ubuntu 24.04..."
    colima start --cpu 4 --memory 8 --disk 20 --vm-type vz --arch aarch64 --mount-type virtiofs

    # Wait for Docker daemon to be ready
    echo "Waiting for Docker daemon..."
    while ! docker info &>/dev/null; do
        sleep 1
    done
}

setup_linux() {
    # Check if Docker is installed
    if ! command -v docker &> /dev/null; then
        echo "Installing Docker..."
        sudo apt-get update
        sudo apt-get install -y docker.io

        # Add user to docker group
        sudo usermod -aG docker $USER
        echo "Please log out and back in for docker group changes to take effect"
    fi

    # Install development tools
    echo "Installing development tools..."
    sudo apt-get update
    sudo apt-get install -y build-essential gcc g++ make python3 curl wget git
}

# Detect OS and run appropriate setup
case "$(uname)" in
    "Darwin")
        echo "Setting up macOS environment..."
        setup_macos
        ;;
    "Linux")
        echo "Setting up Linux environment..."
        setup_linux
        ;;
    *)
        echo "Unsupported operating system"
        exit 1
        ;;
esac

echo "Setup complete! You can now open this folder in VS Code and start development."
echo "Use 'code .' to open VS Code with the current directory." 