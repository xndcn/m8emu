# m8emu
Dirtywave M8 Tracker Emulator

**WORK IN PROGRESS**

## Building
```
git submodule update --init --recursive
mkdir build && cd build && cmake ../ && make -j6
```

## Usage
```
./m8emu /path/to/M8_V3_3_3_HEADLESS.hex &

sudo modprobe vhci-hcd
sudo usbip attach -r localhost -b 1-0
```

![Screenshot](https://github.com/user-attachments/assets/24ad97b3-6bf3-46e9-9288-f9df54c7b5ca)

## TODO
- support M8_V4_0_0_HEADLESS.hex firmware
- support usdhc
