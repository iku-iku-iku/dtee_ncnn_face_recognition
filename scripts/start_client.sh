#!/bin/bash


BR=br0
TAP=tap1

sudo tunctl -d $TAP
sudo tunctl -t $TAP -u $(whoami)
sudo ip link set $TAP up
sudo ip link set $TAP master $BR

qemu-system-riscv64 \
-bios ./Penglai-Enclave-sPMP/opensbi-1.2/build-oe/qemu-virt/platform/generic/firmware/fw_dynamic.bin \
-nographic -machine virt -machine acpi=off \
-m 4G -smp 4 \
-object rng-random,filename=/dev/urandom,id=rng0 \
-device virtio-vga -device virtio-rng-device,rng=rng0 \
-drive file=./RISCV_VIRT.fd,if=pflash,format=raw,unit=1 \
-device virtio-blk-device,drive=hd0 \
-drive file=./openEuler-23.03-V1-base-qemu-preview2.qcow2,format=qcow2,id=hd0 \
-netdev tap,id=net0,ifname=$TAP,script=no,downscript=no -device virtio-net-device,netdev=net0,mac=52:54:00:11:34:58

