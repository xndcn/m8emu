#pragma once

#include "io.h"
#include "emu.h"
#include "sdcard.h"
#include "timer.h"
#include <array>
#include <memory>
#include <ext/cqueue.h>

namespace m8 {

class USDHC : public RegisterDevice {
public:
    USDHC(CoreCallbacks& callbacks, u32 baseAddr, u32 size);

    void InsertSDCard(std::shared_ptr<SDCard> card);

private:
    void ResetDataLine(bool reset);
    void UpdateInterrupts();
    void SendCommand();
    u32 ReadBufferDataContent();
    void WriteBufferDataContent(u32 data);
    void ReadWriteCard();

private:
    u32 dmaAddress = 0;
    u32 blockSize = 0;
    u32 blockCount = 0;
    u32 cmdArgument = 0;
    u32 respType = 0;
    u8 cmdType = 0;
    u8 cmdIndex = 0;
    bool dataPresent = false;
    bool commandComplete = false;
    bool transferComplete = false;
    u8 readWatermarkLevel = 0;
    bool dmaEnable = false;
    bool dataDirection = false;

    std::array<u32, 4> cmdResp;
    ext::cqueue<uint8_t> dataBuffer;

    CoreCallbacks& callbacks;
    std::shared_ptr<SDCard> sdcard;
    Timer timer;
};

} // namespace m8
