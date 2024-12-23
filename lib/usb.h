#pragma once

#include "emu.h"
#include "timer.h"
#include <ext/cqueue.h>
#include <queue>

namespace m8 {

struct __attribute__ ((__packed__)) USBSetupBytes {
    union {
        struct {
            union {
                struct {
                    u8 bmRequestType;
                    u8 bRequest;
                };
                u16 wRequestAndType;
            };
            u16 wValue;
            u16 wIndex;
            u16 wLength;
        };
        struct {
            u32 bytes0;
            u32 bytes1;
        };
    };
};

class USBDevice {
public:
    virtual void HandleSetupPacket(USBSetupBytes setup, const u8* data, std::size_t length, std::function<void(const u8*, std::size_t)> callback) = 0;
    virtual void HandleDataWrite(int ep, int interval, const u8* data, std::size_t length) = 0;
    virtual void HandleDataRead(int ep, int interval, std::size_t limit, std::function<void(const u8*, std::size_t)> callback) = 0;
};

enum class EndpointType {
    Control = 0,
    Isochronous = 1,
    Bulk = 2,
    Interrupt = 3,
};

struct __attribute__ ((__packed__)) EndpointQueueHead {
    u32 config;
    u32 currentPointer;
    u32 nextPointer;
    u32 status;
    u32 bufferPointer0;
    u32 bufferPointer1;
    u32 bufferPointer2;
    u32 bufferPointer3;
    u32 bufferPointer4;
    u32 reserved;
    USBSetupBytes setup;
    u32 padding[4];
};

struct __attribute__ ((__packed__)) EndpointTransferDescriptor {
    u32 nextPointer;
    u8  status;
    u8  multO_IOC;
    u16 totalBytes;
    u32 bufferPointer0;
    u32 bufferPointer1;
    u32 bufferPointer2;
    u32 bufferPointer3;
    u32 bufferPointer4;
};

class USB : public RegisterDevice, public USBDevice {
public:
    USB(CoreCallbacks& callbacks, u32 baseAddr, u32 size);

    void HandleSetupPacket(USBSetupBytes setup, const u8* data, std::size_t length, std::function<void(const u8*, std::size_t)> callback) override;
    void HandleDataWrite(int ep, int interval, const u8* data, std::size_t length) override;
    void HandleDataRead(int ep, int interval, std::size_t limit, std::function<void(const u8*, std::size_t)> callback) override;

private:
    void UpdateInterrupts();
    void UpdateEndpointPrimeTx(u8 tx);
    void UpdateEndpointPrimeRx(u8 rx);
    void UpdateEndpointListAddress(u32 address);

private:
    CoreCallbacks& callbacks;
    ext::cqueue<u8> setupBuffer;
    std::function<void(const u8*, std::size_t)> setupCallback;

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
    std::vector<ext::cqueue<u8>> endpointBuffers;
    std::vector<EndpointType> endpointTxTypes;
    std::vector<EndpointType> endpointRxTypes;
    std::vector<std::queue<std::function<void(const u8*, std::size_t)>>> endpointTxCallbacks;

    std::mutex mutex;
};

} // namespace m8
