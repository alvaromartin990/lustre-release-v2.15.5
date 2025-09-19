#!/bin/bash
# test_cxl_alloc.sh
# Script to insert, test, and remove the cxl_alloc kernel module

MOD_NAME="cxl_alloc"
KO_PATH="./cxl_alloc.ko"   # Adjust if module is in a different path
DAX_SIZE_MB=131072         # 128 GiB (from daxctl list)
DAX_PHYS="0x1000000000"    # Replace with actual physical base from /proc/iomem

echo "[*] Clearing old kernel logs..."
sudo dmesg -C

echo "[*] Removing module if it is already loaded..."
if lsmod | grep -q "^${MOD_NAME}"; then
    sudo rmmod ${MOD_NAME}
fi

echo "[*] Inserting ${MOD_NAME} with dax_phys=${DAX_PHYS}, dax_size_mb=${DAX_SIZE_MB}"
sudo insmod ${KO_PATH} dax_phys=${DAX_PHYS} dax_size_mb=${DAX_SIZE_MB}
if [ $? -ne 0 ]; then
    echo "[!] Failed to insert module"
    exit 1
fi

sleep 1

echo "[*] Checking dmesg logs for allocator init and test results..."
sudo dmesg | grep ${MOD_NAME}

echo "[*] Verifying module is loaded..."
lsmod | grep ${MOD_NAME}

echo "[*] Removing ${MOD_NAME}..."
sudo rmmod ${MOD_NAME}
if [ $? -eq 0 ]; then
    echo "[+] Module removed successfully"
else
    echo "[!] Failed to remove module"
fi

echo "[*] Final logs:"
sudo dmesg | grep ${MOD_NAME}
