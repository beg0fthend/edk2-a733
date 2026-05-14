# SD RECOVERY BOOT SCRIPT — always boots Linux from SD, no EDK2.
# Write this as boot.scr to an SD card for guaranteed Linux recovery.

setenv load_addr "0x43100000"
setenv overlay_error "false"
setenv verbosity "1"
setenv console "both"
setenv bootlogo "false"
setenv rootfstype "ext4"
setenv docker_optimizations "on"
setenv earlycon "on"

echo "SD RECOVERY boot script loaded"

if test -e ${devtype} ${devnum} ${prefix}orangepiEnv.txt; then
        load ${devtype} ${devnum} ${load_addr} ${prefix}orangepiEnv.txt
        env import -t ${load_addr} ${filesize}
fi

# Force-clear try_edk2 on NVMe (best-effort; ignore errors)
# (nvme 0:1 is the NVMe partition in most BSP U-Boot builds)

if test "${console}" = "display" || test "${console}" = "both"; then setenv consoleargs "console=tty1"; fi
if test "${console}" = "serial" || test "${console}" = "both"; then setenv consoleargs "console=ttyS0,115200 ${consoleargs}"; fi
if test "${earlycon}" = "on"; then setenv consoleargs "earlyprintk=sunxi-uart,0x02500000 initcall_debug=0 ${consoleargs}"; fi

part uuid ${devtype} ${devnum}:1 partuuid
if test -z "${rootdev}"; then rootdev=PARTUUID="${partuuid}"; fi
setenv bootargs "root=${rootdev} rootwait rootfstype=${rootfstype} ${consoleargs} consoleblank=0 loglevel=${verbosity} clk_ignore_unused swiotlb=65536 ${extraargs} ${extraboardargs}"
if test "${docker_optimizations}" = "on"; then setenv bootargs "${bootargs} cgroup_enable=cpuset cgroup_memory=1 cgroup_enable=memory swapaccount=1"; fi

load ${devtype} ${devnum} ${ramdisk_addr_r} ${prefix}uInitrd
load ${devtype} ${devnum} ${kernel_addr_r} ${prefix}uImage
load ${devtype} ${devnum} ${fdt_addr_r} ${prefix}dtb/${fdtfile}
fdt addr ${fdt_addr_r}
fdt resize 65536
bootm ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r}
