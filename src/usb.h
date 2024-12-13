#pragma once

#include "usbip-internal.h"
#include "emu.h"
#include "timer.h"
#include <ext/cqueue.h>
#include <queue>

namespace m8 {

class USBDevice {
public:
    virtual void HandleSetupPacket(USBIP_SETUP_BYTES setup, uint8_t* data, std::size_t length, std::function<void(uint8_t*, std::size_t)> callback) = 0;
    virtual void HandleDataWrite(int ep, int interval, uint8_t* data, std::size_t length) = 0;
    virtual void HandleDataRead(int ep, int interval, std::size_t limit, std::function<void(uint8_t*, std::size_t)> callback) = 0;
};

enum class EndpointType {
    Control = 0,
    Isochronous = 1,
    Bulk = 2,
    Interrupt = 3,
};

struct __attribute__ ((__packed__)) SetupBytes {
    union {
        struct {
            uint32_t bytes0;
            uint32_t bytes1;
        };
        struct {
            uint16_t wRequestAndType;
            uint16_t wValue;
            uint16_t wIndex;
            uint16_t wLength;
        };
    };
};

struct __attribute__ ((__packed__)) EndpointQueueHead {
    uint32_t config;
    uint32_t currentPointer;
    uint32_t nextPointer;
	uint32_t status;
	uint32_t bufferPointer0;
	uint32_t bufferPointer1;
	uint32_t bufferPointer2;
	uint32_t bufferPointer3;
	uint32_t bufferPointer4;
	uint32_t reserved;
	SetupBytes setup;
	uint32_t padding[4];
};

struct __attribute__ ((__packed__)) EndpointTransferDescriptor {
    uint32_t nextPointer;
    uint8_t  status;
    uint8_t  multO_IOC;
    uint16_t totalBytes;
	uint32_t bufferPointer0;
	uint32_t bufferPointer1;
	uint32_t bufferPointer2;
	uint32_t bufferPointer3;
	uint32_t bufferPointer4;
};

class USB : public RegisterDevice, public USBDevice {
public:
    USB(CoreCallbacks& callbacks, u32 baseAddr, u32 size);

    void HandleSetupPacket(USBIP_SETUP_BYTES setup, uint8_t* data, std::size_t length, std::function<void(uint8_t*, std::size_t)> callback) override;
    void HandleDataWrite(int ep, int interval, uint8_t* data, std::size_t length) override;
    void HandleDataRead(int ep, int interval, std::size_t limit, std::function<void(uint8_t*, std::size_t)> callback) override;

private:
    void UpdateInterrupts();
    void UpdateEndpointPrimeTx(u8 tx);
    void UpdateEndpointPrimeRx(u8 rx);
    void UpdateEndpointListAddress(u32 address);

private:
    CoreCallbacks& callbacks;
    ext::cqueue<uint8_t> setupBuffer;
    std::function<void(uint8_t*, std::size_t)> setupCallback;

    bool setupTripWire = false;
    bool addDTDTripWire = false;
    bool portChangeDetect = false;
    bool interrupt = false;
    std::vector<std::shared_ptr<Timer>> gpTimers;
    std::vector<bool> gpTimerInterrupts;
    std::vector<std::shared_ptr<Timer>> endpointIsocTxTimers;

    u8 endpointPrimeTx = 0;
    u8 endpointPrimeRx = 0;
    u8 endpointBufferReadyTx = 0;
    u8 endpointBufferReadyRx = 0;
    u8 endpointCompleteTx = 0;
    u8 endpointCompleteRx = 0;
    u32 endpointListAddress = 0;
    u16 endpointSetupStatus = 0;

    EndpointQueueHead* endpointQueueHead;
    std::vector<ext::cqueue<uint8_t>> endpointBuffers;
    std::vector<EndpointType> endpointTxTypes;
    std::vector<EndpointType> endpointRxTypes;
    std::vector<std::queue<std::function<void(uint8_t*, std::size_t)>>> endpointTxCallbacks;

    std::mutex mutex;
};

} // namespace m8
