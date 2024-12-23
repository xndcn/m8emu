#pragma once

#include "usb.h"
#include <memory>
#include <atomic>

namespace m8 {

class M8USBHost {
public:
    M8USBHost(USBDevice& device);
    void Setup();

    int SerialRead(u8* buffer, int size);
    int SerialWrite(const u8* buffer, int size);

    bool Initialized() { return initialized; }

private:
    USBDevice& device;
    std::atomic<bool> initialized = false;

    int serialMaxPacketSize = 0;
    int serialInputEndpoint = 0;
    int serialOutputEndpoint = 0;
};

} // namespace m8
