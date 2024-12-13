#pragma once

#include <set>
#include <vector>
#include "m8emu.h"
#include "timer.h"

namespace m8 {

class M8AudioProcessor {
public:
    M8AudioProcessor(M8Emulator& emu);
    void Setup();
    void Process();

private:
    M8Emulator& emu;
    Timer timer;
};

} // namespace m8
