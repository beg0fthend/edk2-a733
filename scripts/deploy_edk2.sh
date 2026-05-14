#!/bin/bash
# Deploy a fresh EDK2 build to the Orange Pi 4 Pro NVMe and (optionally)
# request EDK2 boot on next reset via /boot/try_edk2.
#
# Usage:  ./scripts/deploy_edk2.sh [--no-reboot] [--no-edk2]
#   --no-reboot : copy uImage but don't reboot
#   --no-edk2   : copy uImage but don't set try_edk2 (next boot = Linux)
#
# Recovery: if EDK2 hangs, just power-cycle. Linux's clear-try-edk2.service
# removes the flag at boot, so EDK2 won't loop. To force-skip even with
# the flag set, SD-recover and `touch /boot/skip_edk2`.

set -euo pipefail
HOST=${ORANGEPI_HOST:-orangepi@192.168.0.244}
PASS=${ORANGEPI_PASS:-orangepi}
UIMG=${UIMG:-/home/jacob/edk2/Build/OrangePi4Pro/DEBUG_GCC/FV/ORANGEPI4PRO_EFI_arm32.uimg}

DO_REBOOT=1; DO_EDK2=1
for a in "$@"; do
    case "$a" in
        --no-reboot) DO_REBOOT=0 ;;
        --no-edk2)   DO_EDK2=0 ;;
        *) echo "unknown: $a"; exit 1 ;;
    esac
done
[ -f "$UIMG" ] || { echo "missing $UIMG"; exit 1; }

echo "[1/3] copying $UIMG"
sshpass -p "$PASS" scp -q -o StrictHostKeyChecking=no "$UIMG" "$HOST:/tmp/EDK2_NEW.uimg"

REMOTE='set -e
echo '"$PASS"' | sudo -S sh -c "
  mountpoint -q /mnt/nvme || { mkdir -p /mnt/nvme && mount /dev/nvme0n1p1 /mnt/nvme; }
  cp /tmp/EDK2_NEW.uimg /mnt/nvme/boot/ORANGEPI4PRO_EFI.uimg
  sync
  md5sum /mnt/nvme/boot/ORANGEPI4PRO_EFI.uimg
'
if [ '"$DO_EDK2"' -eq 1 ]; then
    echo '"$PASS"' | sudo -S sh -c "touch /mnt/nvme/boot/try_edk2 && sync && echo flag_set"
else
    echo '"$PASS"' | sudo -S sh -c "rm -f /mnt/nvme/boot/try_edk2 && sync && echo flag_cleared"
fi'

echo "[2/3] deploying"
sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$HOST" "$REMOTE"

if [ $DO_REBOOT -eq 1 ]; then
    echo "[3/3] rebooting"
    sshpass -p "$PASS" ssh -o StrictHostKeyChecking=no "$HOST" "echo $PASS | sudo -S sh -c 'sleep 1; reboot' &" || true
else
    echo "[3/3] done (no reboot)"
fi
