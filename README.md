# Distributed Face Recognition Demo

This project implements Distributed Face Recognition within the Penglai enclave. It utilizes RetinaNet to detect faces from input images and MobileNet to extract facial features. The neural networks are deployed using the NCNN framework, showcasing how to develop complex AI applications in a distributed environment within the Penglai enclave.

## How To Build

### 1. Fetch The Source Code And Build The App

```sh
git clone git@github.com:iku-iku-iku/dtee_ncnn_face_recognition.git
cd dtee_ncnn_face_recognition
make
```

### 2. Fetch The Image

```sh
wget https://ipads.se.sjtu.edu.cn:1313/f/5ff83d8960e747a8b8aa/?dl=1 -O openEuler-23.03-V1-base-qemu-preview.qcow2.zst
zstd -d openEuler-23.03-V1-base-qemu-preview.qcow2.zst
wget https://ipads.se.sjtu.edu.cn:1313/f/26b1de6d2d1f463aa321/?dl=1 -O RISCV_VIRT.fd
```

### 3. Build u-boot and opensbi

```sh
git clone https://github.com/iku-iku-iku/Penglai-Enclave-sPMP.git
cd Penglai-Enclave-sPMP
# build u-boot
git submodule update --init --recursive
./docker_cmd.sh docker
cd ./u-boot
make qemu-riscv64_smode_defconfig
make ARCH=riscv CROSS_COMPILE=riscv64-unknown-linux-gnu- -j$(nproc)
# exit docker and build opensbi
./docker_cmd.sh opensbi-1.2
cd ..
```


### 4. Launch QEMU
```sh
qemu-system-riscv64 \
-bios ./Penglai-Enclave-sPMP/opensbi-1.2/build-oe/qemu-virt/platform/generic/firmware/fw_dynamic.bin \
-nographic -machine virt -machine acpi=off \
-m 4G -smp 4 \
-object rng-random,filename=/dev/urandom,id=rng0 \
-device virtio-vga -device virtio-rng-device,rng=rng0 \
-drive file=./RISCV_VIRT.fd,if=pflash,format=raw,unit=1 \
-device virtio-blk-device,drive=hd0 \
-drive file=./openEuler-23.03-V1-base-qemu-preview.qcow2,format=qcow2,id=hd0 \
-device virtio-net-device,netdev=usernet -netdev user,id=usernet,hostfwd=tcp::12055-:22 -device qemu-xhci -usb -device usb-kbd -device usb-tablet

# username: root
# password: openEuler12#$
```

### 5. Build Penglai Driver In openEuler

```sh
# In host
cd Penglai-Enclave-sPMP
scp -P 12055 -r penglai-enclave-driver root@localhost:~/
cd ..
# In qemu
dnf install -y kernel-devel kernel-source
cd ~/penglai-enclave-driver
sed -i '/#define DEFAULT_UNTRUSTED_SIZE/c\#define DEFAULT_UNTRUSTED_SIZE 512*1024' penglai-config.h
sed -i 's|make -C ../openeuler-kernel/ ARCH=riscv M=$(PWD) modules|make -C /usr/lib/modules/$(shell uname -r)/build ARCH=riscv M=$(PWD) modules|' Makefile > /dev/null 2>&1
sed -i '/^obj-m/a CFLAGS_MODULE += -fno-stack-protector' Makefile > /dev/null 2>&1
make -j$(nproc)
insmod penglai.ko
```

For simplicity, we have built the `penglai.ko` in the image for you to simply use `insmod` to load it.

```sh
cd ~/penglai-enclave-driver
insmod penglai.ko
```

### 6. Send The Files To openEuler

```sh
scp -P 12055 -r build/ faces/ root@localhost:~/ 
# password: openEuler12#$
```

### 7. Run The App

```sh
cd ~/build
cp libpenglai_0.so /lib64
./client record ../faces/trump1.jpg 1 # record first person with id 1
./client record ../faces/biden1.jpg 2 # record second person with id 2
./client verify ../faces/trump2.jpg # verify that the first person's id is 1
./client verify ../faces/biden2.jpg # verify that the first person's id is 2
```
