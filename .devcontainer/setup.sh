#!/bin/bash

# Update package list
sudo apt-get update

if [ -n "$CODESPACES" ] ; then
    echo "Running in GitHub Codespaces on Azure kernel, setting up Azure-specific repositories..."
    # Add Azure-specific repositories for Ubuntu 22.04 (jammy)
    cat << EOF | sudo tee /etc/apt/sources.list.d/azure.list
deb http://archive.ubuntu.com/ubuntu/ jammy-updates main restricted
deb http://security.ubuntu.com/ubuntu jammy-security main restricted
deb http://azure.archive.ubuntu.com/ubuntu/ jammy-updates main restricted
deb http://azure.archive.ubuntu.com/ubuntu/ jammy-security main restricted
EOF
    sudo apt-get update

    # Try to install the Azure kernel headers
    if ! sudo apt-get install -y linux-headers-$(uname -r); then
        echo "Failed to install exact kernel headers, attempting to install Azure-specific headers..."
        # Extract the major version (e.g., 6.5.0 from 6.5.0-1025-azure)
        MAJOR_VERSION=$(echo $(uname -r) | cut -d'-' -f1)
        # Try to install the latest Azure headers for this major version
        sudo apt-get install -y linux-headers-${MAJOR_VERSION}*-azure
    fi

    sudo apt-get install -y gcc-11 libc6-dev
else
    echo "Running in local environment, installing standard kernel headers..."
    # Standard installation for local development
    sudo apt-get install -y linux-headers-$(uname -r)
fi
