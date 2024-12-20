#include "usdhc.h"

namespace m8 {

#define REG32(r, a) Register r { .addr = a }
#define FIELD(reg, f, o, l, r, w) reg.fields[#f] = { .offset = o, .length = l, .readCallback = r, .writeCallback = w }
#define R(x) [this]() -> u32 { return x; }
#define W(x) [this](u32 v) { x = v; }
#define W1C(x) [this](u32 v) { x = x & (~v); }
#define N() [](u32 v) {}

using namespace std::chrono_literals;
#define DMA_DELAY_INTERVAL 100us

USDHC::USDHC(CoreCallbacks& callbacks, u32 baseAddr, u32 size) : RegisterDevice(baseAddr, size), callbacks(callbacks)
{
    REG32(DS_ADDR, 0x00);
    FIELD(DS_ADDR, VALUE, 0, 32, R(dmaAddress), W(dmaAddress));

    REG32(BLK_ATT, 0x04);
    FIELD(BLK_ATT, BLKSIZE, 0, 12, R(blockSize), W(blockSize));
    FIELD(BLK_ATT, BLKCNT, 16, 16, R(blockCount), W(blockCount));

    REG32(CMD_ARG, 0x08);
    FIELD(CMD_ARG, VALUE, 0, 32, R(cmdArgument), W(cmdArgument));

    REG32(CMD_XFR_TYP, 0x0C);
    FIELD(CMD_XFR_TYP, CMDINX, 24, 6, R(cmdIndex), W(cmdIndex));
    FIELD(CMD_XFR_TYP, CMDTYP, 22, 2, R(cmdType), W(cmdType));
    FIELD(CMD_XFR_TYP, DPSEL, 21, 1, R(dataPresent), W(dataPresent));
    FIELD(CMD_XFR_TYP, RSPTYP, 16, 2, R(respType), W(respType));
    CMD_XFR_TYP.writeCallback = [this](u32 v) { SendCommand(); };

    for (u32 i = 0; i < cmdResp.size(); i++) {
        REG32(CMD_RSPi, 0x10 + i * 4);
        auto r = [this, i]() { return cmdResp[i]; };
        FIELD(CMD_RSPi, VALUE, 0, 32, r, N());
        BindRegister(CMD_RSPi);
    }

    REG32(DATA_BUFF_ACC_PORT, 0x20);
    FIELD(DATA_BUFF_ACC_PORT, DATCONT, 0, 32, [this]() { return ReadBufferDataContent(); }, [this](u32 v) { WriteBufferDataContent(v); });

    REG32(PRES_STATE, 0x24);
    FIELD(PRES_STATE, SDSTB, 3, 1, R(1), N());
    FIELD(PRES_STATE, BWEN, 10, 1, R(1), N());
    FIELD(PRES_STATE, BREN, 11, 1, R(1), N());
    FIELD(PRES_STATE, CINST, 16, 1, R(sdcard != nullptr), N());
    FIELD(PRES_STATE, CLSL, 23, 1, R(1), N());
    FIELD(PRES_STATE, DLSL, 24, 8, R(7), N());

    REG32(SYS_CTRL, 0x2C);
    FIELD(SYS_CTRL, RSTD, 26, 1, R(0), [this](u32 v) { ResetDataLine(v); });

    REG32(INT_STATUS, 0x30);
    FIELD(INT_STATUS, CC, 0, 1, R(commandComplete), W1C(commandComplete));
    FIELD(INT_STATUS, TC, 1, 1, R(transferComplete), W1C(transferComplete));
    INT_STATUS.writeCallback = [this](u32 v) { UpdateInterrupts(); };

    REG32(INT_STATUS_EN, 0x34);
    INT_STATUS_EN.writeCallback = [this](u32 v) { UpdateInterrupts(); };

    REG32(WTMK_LVL, 0x44);
    FIELD(WTMK_LVL, RD_WML, 0, 8, R(readWatermarkLevel), W(readWatermarkLevel));

    REG32(MIX_CTRL, 0x48);
    FIELD(MIX_CTRL, DMAEN, 0, 1, R(dmaEnable), W(dmaEnable));
    FIELD(MIX_CTRL, DTDSEL, 4, 1, R(dataDirection), W(dataDirection));

    BindRegister(DS_ADDR);
    BindRegister(BLK_ATT);
    BindRegister(CMD_ARG);
    BindRegister(CMD_XFR_TYP);
    BindRegister(DATA_BUFF_ACC_PORT);
    BindRegister(PRES_STATE);
    BindRegister(SYS_CTRL);
    BindRegister(INT_STATUS);
    BindRegister(INT_STATUS_EN);
    BindRegister(WTMK_LVL);
    BindRegister(MIX_CTRL);
}

void USDHC::InsertSDCard(std::shared_ptr<SDCard> card)
{
    sdcard = card;
    timer.SetInterval(DMA_DELAY_INTERVAL, [this](Timer&) {
        callbacks.Lock();
        ReadWriteCard();
        UpdateInterrupts();
        callbacks.Unlock();
    });
}

void USDHC::ResetDataLine(bool reset)
{
    if (reset) {
        blockCount = 0;
        blockSize = 0;
        dataBuffer.pop(dataBuffer.size());
    }
}

void USDHC::UpdateInterrupts()
{
    if (transferComplete) {
        TriggerInterrupt();
    }
}

void USDHC::SendCommand()
{
    uint32_t response[4];
    if (sdcard) {
        int len = sdcard->HandleCommand(cmdIndex, cmdArgument, response);
        // TODO: check response length
        if (len == 4) {
            cmdResp[0] = response[0];
        } else if (len == 16) {
            cmdResp[0] = response[0];
            cmdResp[1] = response[1];
            cmdResp[2] = response[2];
            cmdResp[3] = response[3];
        }
        commandComplete = true;
        if (dataPresent && dmaEnable) {
            timer.SetOneshot(true);
            timer.Start();
        }
    }
}

u32 USDHC::ReadBufferDataContent()
{
    u32 data = 0;
    if (sdcard) {
        sdcard->ReadData((u8*)&data, sizeof(data));
        transferComplete = true;
    }
    return data;
}

void USDHC::WriteBufferDataContent(u32 data)
{
    if (sdcard) {
        sdcard->WriteData((u8*)&data, sizeof(data));
        transferComplete = true;
    }
}

void USDHC::ReadWriteCard()
{
    u32 bytes = blockCount * blockSize;
    u8* dmaBuffer = (u8*)callbacks.MemoryMap(dmaAddress);
    if (dataDirection) {
        sdcard->ReadData(dmaBuffer, bytes);
    } else {
        sdcard->WriteData(dmaBuffer, bytes);
    }
    blockCount = 0;
    transferComplete = true;
}

} // namespace m8
