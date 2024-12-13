#include "m8audio.h"
#include <cstddef>
#include <ext/log.h>

using namespace std::chrono_literals;
#define SOFTWARE_IRQ (70 + 16)
#define AUDIO_BLOCK_SAMPLES 64
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_PROCESS_INTERVAL ((1000000 * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE) * 1us)

namespace m8 {

M8AudioProcessor::M8AudioProcessor(M8Emulator& emu) : emu(emu)
{
}

void M8AudioProcessor::Setup()
{
    timer.SetInterval(AUDIO_PROCESS_INTERVAL, [this](Timer&) {
        auto& callbacks = emu.Callbacks();
        callbacks.Lock();
        Process();
        callbacks.Unlock();
    });
    timer.Start();
}

void M8AudioProcessor::Process()
{
    auto now = std::chrono::steady_clock::now();
    u32 function = emu.VectorAddress(SOFTWARE_IRQ);
    emu.CallFunction(function, 0);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - now).count();
    if (duration > AUDIO_PROCESS_INTERVAL.count()) {
        ext::LogWarn("AudioProcessor: process duration = %d us > AUDIO_PROCESS_INTERVAL (%d)", duration, AUDIO_PROCESS_INTERVAL.count());
    }
}

} // namespace m8
