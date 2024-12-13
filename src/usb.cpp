#include "usb.h"
#include <ext/log.h>
#include <cassert>

namespace m8 {

#define REG32(r, a) Register r { .addr = a }
#define FIELD(reg, f, o, l, r, w) reg.fields[#f] = { .offset = o, .length = l, .readCallback = r, .writeCallback = w }
#define R(x) [this]() -> u32 { return x; }
#define W(x) [this](u32 v) { x = v; }
#define W1C(x) [this](u32 v) { x = x & (~v); }
#define N() [](u32 v) {}

#define NUMS_GPTIMER  2
#define NUMS_ENDPOINT 8
#define ENDPOINT_BUFFER_SIZE (64*1024)

static_assert(sizeof(EndpointQueueHead) == 64);

using namespace std::chrono_literals;

USB::USB(CoreCallbacks& cb, u32 baseAddr, u32 size) : RegisterDevice(baseAddr, size), callbacks(cb)
{
    for (int i = 0; i < NUMS_ENDPOINT; i++) {
        gpTimers.push_back(std::make_shared<Timer>());
    }
    gpTimerInterrupts.resize(NUMS_GPTIMER);
    endpointBuffers.resize(NUMS_ENDPOINT);
    endpointTxTypes.resize(NUMS_ENDPOINT);
    endpointRxTypes.resize(NUMS_ENDPOINT);
    endpointIsocTxTimers.resize(NUMS_ENDPOINT);
    endpointTxCallbacks.resize(NUMS_ENDPOINT);

    for (u32 i = 0; i < NUMS_GPTIMER; i++) {
        REG32(GPTIMERiLD, 0x80 + i * 8);
        auto callback = [i, this](u32 v) {
            gpTimers[i]->SetInterval((v + 1) * 1us, [i, this](Timer&) {
                gpTimerInterrupts[i] = true;
                UpdateInterrupts();
            });
        };
        FIELD(GPTIMERiLD, VALUE, 0, 24, R(0), callback);
        REG32(GPTIMERiCTRL, 0x84 + i * 8);
        GPTIMERiCTRL.writeCallback = [i, this](u32 v) {
            bool oneshot = !(v & (1 << 24));
            gpTimers[i]->SetOneshot(oneshot);
            if (v & (1 << 31)) {
                gpTimers[i]->Start();
            } else {
                gpTimers[i]->Stop();
            }
        };
        BindRegister(GPTIMERiLD);
        BindRegister(GPTIMERiCTRL);
    }

    REG32(USBCMD, 0x140);
    FIELD(USBCMD, SUTW, 13, 1, R(setupTripWire), W(setupTripWire));
    FIELD(USBCMD, ATDTW, 14, 1, R(addDTDTripWire), W(addDTDTripWire));

    REG32(USBSTS, 0x144);
    FIELD(USBSTS, UI, 0, 1, R(interrupt), W1C(interrupt));
    FIELD(USBSTS, TI1, 25, 1, R(gpTimerInterrupts[1]), W1C(gpTimerInterrupts[1]));
    FIELD(USBSTS, TI0, 24, 1, R(gpTimerInterrupts[0]), W1C(gpTimerInterrupts[0]));
    FIELD(USBSTS, PCI, 2, 1, R(portChangeDetect), W1C(portChangeDetect));
    USBSTS.writeCallback = [this](u32 v) { UpdateInterrupts(); };

    REG32(ENDPTLISTADDR, 0x158);
    FIELD(ENDPTLISTADDR, EPBASE, 11, 20, R(endpointListAddress), [this](u32 addr) { UpdateEndpointListAddress(addr << 11); });

    REG32(PORTSC1, 0x184);
    FIELD(PORTSC1, PSPD, 26, 2, R(2), N()); // High Speed
    FIELD(PORTSC1, HSP, 9, 1, R(1), N()); // High Speed

    REG32(ENDPTSETUPSTAT, 0x1AC);
    FIELD(ENDPTSETUPSTAT, ENDPTSETUPSTAT, 0, 16, R(endpointSetupStatus), W1C(endpointSetupStatus));

    REG32(ENDPTPRIME, 0x1B0);
    FIELD(ENDPTPRIME, PETB, 16, 8, R(endpointPrimeTx), [this](u32 tx) { UpdateEndpointPrimeTx(tx); });
    FIELD(ENDPTPRIME, PERB, 0, 8, R(endpointPrimeRx), [this](u32 rx) { UpdateEndpointPrimeRx(rx); });

    REG32(ENDPTSTAT, 0x1B8);
    FIELD(ENDPTSTAT, ETBR, 16, 8, R(endpointBufferReadyTx), N());
    FIELD(ENDPTSTAT, ERBR, 0, 8, R(endpointBufferReadyRx), N());

    REG32(ENDPTCOMPLETE, 0x1BC);
    FIELD(ENDPTCOMPLETE, ETCE, 16, 8, R(endpointCompleteTx), W1C(endpointCompleteTx));
    FIELD(ENDPTCOMPLETE, ERCE, 0, 8, R(endpointCompleteRx), W1C(endpointCompleteRx));

    for (u32 i = 0; i < NUMS_ENDPOINT; i++) {
        REG32(ENDPTCTRLi, 0x1C0 + 4 * i);
        auto readTx = [i, this]() { return (u32)endpointTxTypes[i]; };
        auto readRx = [i, this]() { return (u32)endpointRxTypes[i]; };
        auto writeTx = [i, this](u32 v) { endpointTxTypes[i] = (EndpointType)v; };
        auto writeRx = [i, this](u32 v) { endpointRxTypes[i] = (EndpointType)v; };
        FIELD(ENDPTCTRLi, TXT, 18, 2, readTx, writeTx);
        FIELD(ENDPTCTRLi, RXT, 2, 2, readRx, writeRx);
        BindRegister(ENDPTCTRLi);
    }

    // BindRegister(GPTIMER0LD);
    BindRegister(USBCMD);
    BindRegister(USBSTS);
    BindRegister(ENDPTLISTADDR);
    BindRegister(PORTSC1);
    BindRegister(ENDPTSETUPSTAT);
    BindRegister(ENDPTPRIME);
    BindRegister(ENDPTSTAT);
    BindRegister(ENDPTCOMPLETE);
}

void USB::UpdateInterrupts()
{
    bool irq = interrupt;
    for (bool v : gpTimerInterrupts) {
        irq = irq || v;
    }
    if (irq) {
        TriggerInterrupt();
    }
}

void USB::UpdateEndpointPrimeTx(u8 tx)
{
    endpointPrimeTx = tx;
    for (int i = 0; i < 8; i++) {
        if (endpointPrimeTx & (1 << i)) {
            EndpointQueueHead* endpointQueueHeadTx = endpointQueueHead + i * 2 + 1;
            if ((endpointQueueHeadTx->nextPointer & 1) == 0) {
                u32 address = endpointQueueHeadTx->nextPointer;
                while ((address & 1) == 0) {
                    EndpointTransferDescriptor* td = (EndpointTransferDescriptor*)callbacks.MemoryMap(address);
                    if (td->status & (1 << 7)) { // Active
                        uint8_t* data = (uint8_t*)callbacks.MemoryMap(td->bufferPointer0);
                        if (i == 0) {
                            assert(setupCallback != nullptr);
                            setupCallback(data, td->totalBytes);
                            setupCallback = nullptr;
                        } else {
                            std::lock_guard lock(mutex);
                            endpointBuffers[i].push(data, td->totalBytes);
                            if (endpointBuffers[i].size() > ENDPOINT_BUFFER_SIZE) {
                                endpointBuffers[i].pop(endpointBuffers[i].size() - ENDPOINT_BUFFER_SIZE);
                            }
                        }
                        td->status = 0;
                        td->totalBytes = 0;
                    }
                    address = td->nextPointer;
                }
            }
            endpointPrimeTx &= ~(1 << i);
            // endpointCompleteTx |= (ulong)(1<<i);
        }
    }
}

void USB::UpdateEndpointPrimeRx(u8 rx)
{
    endpointPrimeRx = rx;
    for (int i = 0; i < 8; i++) {
        if (endpointPrimeRx & (1 << i)) {
            endpointBufferReadyRx |= 1 << i;
            EndpointQueueHead* endpointQueueHeadRx = endpointQueueHead + i * 2;
            if ((endpointQueueHeadRx->nextPointer & 1) == 0 && i == 0) {
                u32 address = endpointQueueHeadRx->nextPointer;
                if ((address & 1) == 0) {
                    EndpointTransferDescriptor* td = (EndpointTransferDescriptor*)callbacks.MemoryMap(address);
                    if (td->status & (1<<7)) { // Active
                        assert(setupBuffer.size() >= td->totalBytes);
                        setupBuffer.pop(callbacks.MemoryMap(td->bufferPointer0), td->totalBytes);
                    }
                    address = td->nextPointer;
                }
            }
            endpointPrimeRx &= ~(1 << i);
        }
    }
}

void USB::UpdateEndpointListAddress(u32 address)
{
    endpointBufferReadyRx = 0;
    endpointBufferReadyTx = 0;
    endpointListAddress = address;
    ext::LogInfo("USB: UpdateEndpointListAddress 0x%x", address);
    endpointQueueHead = (EndpointQueueHead*)callbacks.MemoryMap(address);
}

void USB::HandleSetupPacket(USBIP_SETUP_BYTES setup, uint8_t* data, std::size_t length, std::function<void(uint8_t*, std::size_t)> callback)
{
    callbacks.Lock();
    assert(endpointQueueHead != nullptr);
    assert(setupCallback == nullptr);

    setupCallback = callback;
    endpointQueueHead[0].setup.bytes0 = setup.bytes0;
    endpointQueueHead[0].setup.bytes1 = setup.bytes1;
    endpointSetupStatus = 1 << 0;
    if (data && length) {
        setupBuffer.push(data, length);
    }

    portChangeDetect = true;
    interrupt = true;
    callbacks.Unlock();
    UpdateInterrupts();
}

void USB::HandleDataWrite(int ep, int interval, uint8_t* data, std::size_t length)
{
    callbacks.Lock();

    endpointCompleteRx |= 1 << ep;
    EndpointQueueHead* endpointQueueHeadRx = endpointQueueHead + ep * 2;
    if ((endpointQueueHeadRx->nextPointer & 1) == 0) {
        uint32_t address = endpointQueueHeadRx->nextPointer;
        if ((address & 1) == 0) {
            EndpointTransferDescriptor* td = (EndpointTransferDescriptor*)callbacks.MemoryMap(address);
            // FIXME: add buffer here for data more than totalBytes
            length = std::min(td->totalBytes, (uint16_t)length);
            td->status = 0;
            td->totalBytes -= length;
            callbacks.MemoryWrite(td->bufferPointer0, data, length);
            endpointQueueHeadRx->nextPointer = td->nextPointer;
            endpointQueueHeadRx->currentPointer = address;
        }
    }
    interrupt = true;
    callbacks.Unlock();
    UpdateInterrupts();
}

void USB::HandleDataRead(int ep, int interval, std::size_t limit, std::function<void(uint8_t*, std::size_t)> callback)
{
    if (endpointTxTypes[ep] == EndpointType::Isochronous) {
        if (!endpointIsocTxTimers[ep]) {
            endpointIsocTxTimers[ep] = std::make_shared<Timer>();
            endpointIsocTxTimers[ep]->SetInterval(interval * 125us, [ep, this, limit](Timer&) {
                mutex.lock();
                if (endpointTxCallbacks[ep].empty()) {
                    mutex.unlock();
                } else {
                    mutex.unlock();
                    endpointCompleteTx |= 1 << ep;
                    interrupt = true;
                    UpdateInterrupts();
                    mutex.lock();
                    auto callback = endpointTxCallbacks[ep].front();
                    endpointTxCallbacks[ep].pop();
                    auto size = std::min(limit, endpointBuffers[ep].size());
                    std::vector<uint8_t> buffer(size);
                    endpointBuffers[ep].pop(buffer.data(), size);
                    mutex.unlock();
                    callback(buffer.data(), size);
                }
            });
            endpointIsocTxTimers[ep]->Start();
        }
        std::lock_guard lock(mutex);
        endpointTxCallbacks[ep].push(callback);
    } else {
        endpointCompleteTx |= 1 << ep;
        interrupt = true;
        UpdateInterrupts();
        mutex.lock();
        auto size = std::min(limit, endpointBuffers[ep].size());
        std::vector<uint8_t> buffer(size);
        endpointBuffers[ep].pop(buffer.data(), size);
        mutex.unlock();
        callback(buffer.data(), size);
    }
}

} // namespace m8
