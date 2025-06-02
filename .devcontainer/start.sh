#!/bin/bash

# Determine workspace directory
if [ -d "/workspaces/jsonk" ]; then
    WORKSPACE_DIR="/workspaces/jsonk"
else
    WORKSPACE_DIR="$(pwd)"
fi

# Add the workspace directory to the safe.directory list
if [ -d "/workspaces/jsonk" ]; then
    git config --global --add safe.directory /workspaces/jsonk
fi