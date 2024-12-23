#include "m8usb-host.h"
#include "config.h"
#include <cstring>
#include <ext/log.h>

namespace m8 {

M8USBHost::M8USBHost(USBDevice& device) : device(device)
{
}

void M8USBHost::Setup()
{
    auto& config = FirmwareConfig::GlobalConfig();
    serialMaxPacketSize = config.GetValue<u32>("serial_max_packet_size");
    serialInputEndpoint = config.GetValue<u32>("serial_input_endpoint");
    serialOutputEndpoint = config.GetValue<u32>("serial_output_endpoint");

    USB_SETUP_BYTES setup;
    setup.wRequestAndType = 0x900;
    setup.wValue = 1;
    device.HandleSetupPacket(setup, nullptr, 0, [this](uint8_t*, std::size_t) {
        initialized = true;
        ext::LogInfo("USBHost: initialized");
    });
}

int M8USBHost::SerialRead(u8* buffer, int size)
{
    int read = 0;
    if (initialized) {
        device.HandleDataRead(serialInputEndpoint, 0, serialMaxPacketSize, [&read, buffer, size](uint8_t* data, std::size_t length) {
            memcpy(buffer, data, length);
            read = length;
        });
    }
    return read;
}

int M8USBHost::SerialWrite(u8* buffer, int size)
{
    int write = 0;
    if (initialized) {
        while (size > 0) {
            int len = std::min(size, serialMaxPacketSize);
            device.HandleDataWrite(serialOutputEndpoint, 0, buffer + write, len);
            write += len;
            size -= len;
        }
    }
    return write;
}

} // namespace m8
