#!/bin/bash

# ================= Configure Region =================
# Your Usr name
USER_NAME="heqizheng"

# Worker machines IP list (space-separated) Very important!!!
WORKER_IPS=("10.112.100.112" "10.29.32.115")
# ===========================================

# 1. Check if there is already a key, if not, generate one
if [ ! -f ~/.ssh/id_ed25519.pub ]; then
    echo "[Master] Key not found, generating..."
    # -N "" means no passphrase, -f specifies the file path
    ssh-keygen -t ed25519 -N "" -f ~/.ssh/id_ed25519
else
    echo "[Master] Key already exists, skipping generation."
fi

# 2. Loop to distribute to all Workers
echo "=== Starting to distribute public keys ==="
echo "Note: You will need to enter the password for each machine once (this is the last time!)"

for IP in "${WORKER_IPS[@]}"; do
    echo "----------------------------------------"
    echo "Processing node: $IP"
    
    # Automatically append the public key to the remote machine's authorized_keys
    # -o StrictHostKeyChecking=no prevents the 'yes/no' confirmation on first connection
    ssh-copy-id -i ~/.ssh/id_ed25519.pub -o StrictHostKeyChecking=no "$USER_NAME@$IP"
    
    if [ $? -eq 0 ]; then
        echo "? [Success] Passwordless login configured for $IP"
    else
        echo "? [Failure] Unable to connect to $IP, please check network or password"
    fi
done

echo "========================================"
echo "All nodes processed, please run 'ssh username@IP' to test."
