#include "sdcard.h"
#include "common.h"
#include <cstring>
#include <filesystem>

namespace m8 {

#define FIELD(reg, f, o, l, r) static_assert(o / 32 == (o + l - 1) / 32); (reg + o / 32)->fields[#f] = { .offset = o % 32, .length = l, .readCallback = r }
#define R(x) [this]() -> u32 { return x; }

const int HighCapacityBlockSize = 512;

enum BlockLength {
    Block512 = 9,
};

enum CSDVersion {
    Version1 = 0,
    Version2 = 1,
};

SDCard::SDCard(const std::string& path) : stream(path)
{
    cmdHandler[0] = [this](u32 arg) { return HandleCMD0_GoIdleState(); };
    cmdHandler[2] = [this](u32 arg) { return HandleCMD2_SendCID(); };
    cmdHandler[3] = [this](u32 arg) { return HandleCMD3_SendRelativeAddress(); };
    cmdHandler[6] = [this](u32 arg) { return HandleCMD6_CheckSwitchableFunction(arg); };
    cmdHandler[7] = [this](u32 arg) { return HandleCMD7_SelectDeselectCard(arg); };
    cmdHandler[8] = [this](u32 arg) { return HandleCMD8_SendInterfaceCondition(arg); };
    cmdHandler[9] = [this](u32 arg) { return HandleCMD9_SendCSD(); };
    cmdHandler[10] = [this](u32 arg) { return HandleCMD10_SendCID(); };
    cmdHandler[12] = [this](u32 arg) { return HandleCMD12_StopTransmission(); };
    cmdHandler[13] = [this](u32 arg) { return HandleCMD13_SendStatus(); };
    cmdHandler[17] = [this](u32 arg) { return HandleCMD17_ReadSingleBlock(arg); };
    cmdHandler[18] = [this](u32 arg) { return HandleCMD18_ReadMultipleBlocks(arg); };
    cmdHandler[24] = [this](u32 arg) { return HandleCMD24_WriteSingleBlock(arg); };
    cmdHandler[25] = [this](u32 arg) { return HandleCMD25_WriteMultipleBlocks(arg); };
    cmdHandler[32] = [this](u32 arg) { return HandleCMD32_EraseBlockStart(arg); };
    cmdHandler[33] = [this](u32 arg) { return HandleCMD33_EraseBlockEnd(arg); };
    cmdHandler[38] = [this](u32 arg) { return HandleCMD38_Erase(); };
    cmdHandler[55] = [this](u32 arg) { return HandleCMD55_AppCommand(arg); };

    acmdHandler[6] = [this](u32 arg) { return HandleACMD6_SetBusWidth(arg); };
    acmdHandler[41] = [this](u32 arg) { return HandleACMD41_SendOperatingCondition(arg); };

    operatingCondition = 0x80000000;
    operatingCondition |= highCapacity ? 0x40000000 : 0;

    FIELD(&cardStatus, APP_CMD, 5, 1, R(waitingACMD));
    FIELD(&cardStatus, READY_FOR_DATA, 8, 1, R(1));

    FIELD(cid, MDT, 0, 12, R(24 << 4 | 12));
    FIELD(cid, PNM4, 88, 8, R('m'));
    FIELD(cid, PNM3, 80, 8, R('8'));
    FIELD(cid, PNM2, 72, 8, R('e'));
    FIELD(cid, PNM1, 64, 8, R('m'));
    FIELD(cid, PNM0, 56, 8, R('u'));

    auto size = std::filesystem::file_size(path) / 1024 / 512 - 1;
    if (highCapacity) {
        auto device_size= [this, size] () { return size; };
        FIELD(csd, WRITE_BL_LEN, 14, 4, R(BlockLength::Block512));
        FIELD(csd, C_SIZE, 40, 22, device_size);
        FIELD(csd, READ_BL_LEN, 72, 4, R(BlockLength::Block512));
        FIELD(csd, TRAN_SPEED, 88, 8, R(0x32));
        FIELD(csd, CSD_STRUCTURE, 118, 2, R(CSDVersion::Version2));
    }
    // TODO: non high capacity
}

int SDCard::HandleCommand(u8 cmd, u32 arg, u32* response)
{
    SDResponse resp;
    if (waitingACMD) {
        waitingACMD = false;
        resp = HandleAppCommand(cmd, arg);
    } else {
        resp = HandleNormalCommand(cmd, arg);
    }

    int length = 0;
    switch (resp) {
    case SDResponse::R0:
        length = 0;
        break;
    case SDResponse::R1:
    case SDResponse::R1b:
        length = 4;
        response[0] = cardStatus.Read32();
        break;
    case SDResponse::R2_Identification:
        length = 16;
        response[0] = cid[0].Read32();
        response[1] = cid[1].Read32();
        response[2] = cid[2].Read32();
        response[3] = cid[3].Read32();
        break;
    case SDResponse::R2_Specific:
        length = 16;
        response[0] = csd[0].Read32();
        response[1] = csd[1].Read32();
        response[2] = csd[2].Read32();
        response[3] = csd[3].Read32();
        break;
    case SDResponse::R3:
        length = 4;
        response[0] = operatingCondition;
        break;
    case SDResponse::R6:
        length = 4;
        *(uint16_t*)response = cardAddress;
        break;
    case SDResponse::R7:
        length = 4;
        response[0] = checkPattern;
        break;
    default:
        break;
    }
    return length;
}

int SDCard::ReadData(u8* buffer, int size)
{
    stream.read((char*)buffer, size);
    return stream.gcount();
}

int SDCard::WriteData(u8* buffer, int size)
{
    stream.write((char*)buffer, size);
    return size;
}

SDResponse SDCard::HandleAppCommand(u8 cmd, u32 arg)
{
    auto iter = acmdHandler.find(cmd);
    if (iter != acmdHandler.end()) {
        return iter->second(arg);
    }
    return SDResponse::R0;
}

SDResponse SDCard::HandleNormalCommand(u8 cmd, u32 arg)
{
    auto iter = cmdHandler.find(cmd);
    if (iter != cmdHandler.end()) {
        return iter->second(arg);
    }
    return SDResponse::R0;
}

SDResponse SDCard::HandleCMD0_GoIdleState()
{
    state = SDState::Idle;
    return SDResponse::R0;
}

SDResponse SDCard::HandleCMD2_SendCID()
{
    state = SDState::Identification;
    return SDResponse::R2_Identification;
}

SDResponse SDCard::HandleCMD3_SendRelativeAddress()
{
    state = SDState::Standby;
    return SDResponse::R6;
}

SDResponse SDCard::HandleCMD6_CheckSwitchableFunction(u32 arg)
{
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD7_SelectDeselectCard(u32 arg)
{
    switch (state) {
    case SDState::Standby:
        state = SDState::Transfer;
        break;
    case SDState::Transfer:
    case SDState::Programming:
        state = SDState::Standby;
        break;
    default: break;
    }
    return SDResponse::R1b;
}

SDResponse SDCard::HandleCMD8_SendInterfaceCondition(u32 arg)
{
    checkPattern = arg;
    return SDResponse::R7;
}

SDResponse SDCard::HandleCMD9_SendCSD()
{
    return SDResponse::R2_Specific;
}

SDResponse SDCard::HandleCMD10_SendCID()
{
    return SDResponse::R2_Identification;
}

SDResponse SDCard::HandleCMD12_StopTransmission()
{
    switch (state) {
    case SDState::SendingData:
        state = SDState::Transfer;
        break;
    case SDState::ReceivingData:
        state = SDState::Programming;
        break;
    default: break;
    }
    return SDResponse::R1b;
}

SDResponse SDCard::HandleCMD13_SendStatus()
{
    return SDResponse::R1;
}

static u64 GetAddress(bool highCapacity, u32 arg)
{
    return highCapacity ? arg * HighCapacityBlockSize : arg;
}

SDResponse SDCard::HandleCMD17_ReadSingleBlock(u32 arg)
{
    if (state == SDState::Transfer) {
        state = SDState::SendingData;
    }
    currentOffset = GetAddress(highCapacity, arg);
    stream.seekg(currentOffset);
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD18_ReadMultipleBlocks(u32 arg)
{
    if (state == SDState::Transfer) {
        state = SDState::SendingData;
    }
    currentOffset = GetAddress(highCapacity, arg);
    stream.seekg(currentOffset);
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD24_WriteSingleBlock(u32 arg)
{
    if (state == SDState::Transfer) {
        state = SDState::ReceivingData;
    }
    currentOffset = GetAddress(highCapacity, arg);
    stream.seekp(currentOffset);
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD25_WriteMultipleBlocks(u32 arg)
{
    if (state == SDState::Transfer) {
        state = SDState::ReceivingData;
    }
    currentOffset = GetAddress(highCapacity, arg);
    stream.seekp(currentOffset);
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD32_EraseBlockStart(u32 arg)
{
    eraseBegin = arg;
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD33_EraseBlockEnd(u32 arg)
{
    eraseEnd = arg;
    return SDResponse::R1;
}

SDResponse SDCard::HandleCMD38_Erase()
{
    // TODO: implement erase
    return SDResponse::R1b;
}

SDResponse SDCard::HandleCMD55_AppCommand(u32 arg)
{
    waitingACMD = true;
    return SDResponse::R1;
}

SDResponse SDCard::HandleACMD6_SetBusWidth(u32 arg)
{
    return SDResponse::R1;
}

SDResponse SDCard::HandleACMD41_SendOperatingCondition(u32 arg)
{
    return SDResponse::R3;
}

} // namespace m8
