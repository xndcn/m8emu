#include "m8emu.h"
#include "m8audio.h"
#include "usbipd.h"
#include "sdcard.h"
#include <thread>
#include "config.h"

using namespace m8;

int main(int argc, char* argv[]) {
    auto firmware = argv[1];
    if (!FirmwareConfig::GlobalConfig().LoadConfig({}, firmware)) {
        return 1;
    }

    M8Emulator m8emu;
    m8emu.LoadHEX(firmware);

    auto sdcard = std::make_shared<SDCard>(argv[2]);
    m8emu.SDDevice().InsertSDCard(sdcard);

    auto loop = uvw::loop::get_default();
    std::thread uvloop([loop]() {
        while (true) {
            loop->run();
        }
    });

    USBIPServer server(*loop, m8emu.USBDevice());
    M8AudioProcessor m8audio(m8emu);

    m8emu.AttachInitializeCallback([&]() {
        m8audio.Setup();
        server.Start();
    });

    while (true) {
        m8emu.Run();
    }
    return 0;
}
