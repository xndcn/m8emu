#pragma once

#include "io.h"
#include <string>
#include <map>
#include <functional>
#include <fstream>

namespace m8 {

enum class SDState {
    Idle = 0,
    Identification = 2,
    Standby = 3,
    Transfer = 4,
    SendingData = 5,
    ReceivingData = 6,
    Programming = 7,
};

enum class SDResponse {
    R0,
    R1,
    R1b,
    R2_Identification,
    R2_Specific,
    R3,
    R6,
    R7,
};

class SDCard {
public:
    SDCard(const std::string& path);

    int HandleCommand(u8 cmd, u32 arg, u32* response);
    int ReadData(u8* buffer, int size);
    int WriteData(u8* buffer, int size);

private:
    SDResponse HandleAppCommand(u8 cmd, u32 arg);
    SDResponse HandleNormalCommand(u8 cmd, u32 arg);

private:
    SDResponse HandleCMD0_GoIdleState();
    SDResponse HandleCMD2_SendCID();
    SDResponse HandleCMD3_SendRelativeAddress();
    SDResponse HandleCMD6_CheckSwitchableFunction(u32 arg);
    SDResponse HandleCMD7_SelectDeselectCard(u32 arg);
    SDResponse HandleCMD8_SendInterfaceCondition(u32 arg);
    SDResponse HandleCMD9_SendCSD();
    SDResponse HandleCMD10_SendCID();
    SDResponse HandleCMD12_StopTransmission();
    SDResponse HandleCMD13_SendStatus();
    SDResponse HandleCMD17_ReadSingleBlock(u32 arg);
    SDResponse HandleCMD18_ReadMultipleBlocks(u32 arg);
    SDResponse HandleCMD24_WriteSingleBlock(u32 arg);
    SDResponse HandleCMD25_WriteMultipleBlocks(u32 arg);
    SDResponse HandleCMD32_EraseBlockStart(u32 arg);
    SDResponse HandleCMD33_EraseBlockEnd(u32 arg);
    SDResponse HandleCMD38_Erase();
    SDResponse HandleCMD55_AppCommand(u32 arg);
    SDResponse HandleACMD6_SetBusWidth(u32 arg);
    SDResponse HandleACMD41_SendOperatingCondition(u32 arg);

private:
    std::fstream stream;
    bool highCapacity = true;
    bool waitingACMD = false;
    Register cardStatus;
    SDState state = SDState::Idle;
    u16 cardAddress = 0;
    u32 checkPattern = 0;
    u32 operatingCondition = 0;
    u64 currentOffset = 0;
    u32 eraseBegin = 0;
    u32 eraseEnd = 0;

    std::map<u8, std::function<SDResponse(u32)>> cmdHandler;
    std::map<u8, std::function<SDResponse(u32)>> acmdHandler;

    Register cid[4];
    Register csd[4];
};

} // namespace m8
