# m8emu
Dirtywave M8 Tracker Emulator

**WORK IN PROGRESS**

## Building
```
git submodule update --init --recursive
mkdir build && cd build && cmake ../ && make -j6
```

## Usage
### Create SD Image
```
SIZE="1024" # 1024M
IMAGE="sdcard.img"
dd if=/dev/zero of=${IMAGE} bs=1M count=${SIZE}
parted ${IMAGE} --script mklabel msdos
parted ${IMAGE} --script mkpart primary fat32 1MiB 100%
LOOP=$(sudo losetup -fP --show ${IMAGE})
sudo mkfs.vfat -F 32 ${LOOP}p1
sudo losetup -d ${LOOP}
```
### Run
```
./m8emu /path/to/M8_V4_0_0_HEADLESS.hex /path/to/sdcard.img &

sudo modprobe vhci-hcd
sudo usbip attach -r localhost -b 1-0
```

![Screenshot](https://github.com/user-attachments/assets/24ad97b3-6bf3-46e9-9288-f9df54c7b5ca)

## TODO
- standalone mode
