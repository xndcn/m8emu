#include "m8audio.h"
#include <cstddef>
#include <ext/disassembler.h>
#include <ext/log.h>
#include <ext/ir.h>
#include "config.h"

using namespace std::chrono_literals;
#define SOFTWARE_IRQ (70 + 16)
#define AUDIO_BLOCK_SAMPLES 64
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_PROCESS_INTERVAL ((1000000 * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE) * 1us)

namespace m8 {

class _AudioStream
{
public:
    u32 vtable() { return *(u32*)this; } // 0x00, _AudioStream::update()
    bool active() { return *(u8*)((u8*)this + active_offset); }
    u32 next_update_ptr() { return *(u32*)((u8*)this + next_update_offset); }

    u8 num_inputs() { return *(u8*)((u8*)this + (is_f32() ? num_inputs_f32_offset : num_inputs_offset)); }
    u32 destination_list_ptr() { return *(u32*)((u8*)this + (is_f32() ? destination_list_f32_offset : destination_list_offset)); }

    static void Initialize()
    {
        auto& config = FirmwareConfig::GlobalConfig();
        active_offset = config.GetValue<u32>("AudioStream_offset_active");
        num_inputs_offset = config.GetValue<u32>("AudioStream_offset_num_inputs");
        next_update_offset = config.GetValue<u32>("AudioStream_offset_next_update");
        destination_list_offset = config.GetValue<u32>("AudioStream_offset_destination_list");
        num_inputs_f32_offset = config.GetValue<u32>("AudioStream_offset_num_inputs_f32");
        destination_list_f32_offset = config.GetValue<u32>("AudioStream_offset_destination_list_f32");
    }

private:
    bool is_f32() { return *(u32*)((u8*)this + destination_list_offset) == 0; }
    static inline u32 active_offset;
    static inline u32 num_inputs_offset;
    static inline u32 num_inputs_f32_offset;
    static inline u32 next_update_offset;
    static inline u32 destination_list_offset;
    static inline u32 destination_list_f32_offset;
};

struct __attribute__ ((packed)) _AudioConnection
{
    u32 src_ptr;       // 0x00, _AudioStream*
    u32 dst_ptr;       // 0x04, _AudioStream*
    u8  src_index;     // 0x08
    u8  dest_index;    // 0x09
    u16 padding;
    u32 next_dest_ptr; // 0x0c, _AudioConnection*
    u8  isConnected;   // 0x10
};

static_assert(offsetof(_AudioConnection, src_ptr) == 0x00);
static_assert(offsetof(_AudioConnection, dst_ptr) == 0x04);
static_assert(offsetof(_AudioConnection, src_index) == 0x08);
static_assert(offsetof(_AudioConnection, dest_index) == 0x09);
static_assert(offsetof(_AudioConnection, next_dest_ptr) == 0x0c);
static_assert(offsetof(_AudioConnection, isConnected) == 0x10);

M8AudioProcessor::M8AudioProcessor(M8Emulator& emu) : emu(emu)
{
}

enum class LockType {
    Block,
    USB,
};

static void LockWrapper(u64 ptr, u64 type)
{
    M8AudioProcessor* audio = (M8AudioProcessor*)ptr;
    if (type == (u64)LockType::Block) {
        audio->LockBlock();
    } else if (type == (u64)LockType::USB) {
        audio->LockUSB();
    }
}

static void UnlockWrapper(u64 ptr, u64 type)
{
    M8AudioProcessor* audio = (M8AudioProcessor*)ptr;
    if (type == (u64)LockType::Block) {
        audio->UnlockBlock();
    } else if (type == (u64)LockType::USB) {
        audio->UnlockUSB();
    }
}

static void AddLockHook(M8AudioProcessor& audio, M8Emulator& emu, u32 begin, u32 end, LockType type)
{
    auto& callbacks = emu.Callbacks();
    callbacks.AddTranslationHook(begin, [&audio, type](u32 pc, Dynarmic::A32::IREmitter& ir) {
        ext::CallHostFunction(ir, LockWrapper, (u64)&audio, (u64)type);
    });
    u8* code = (u8*)emu.Callbacks().MemoryMap(begin);
    ext::DisassembleIter(code, begin, end - begin, [&callbacks, &audio, type](u32 addr, const std::string& mnemonic, const std::string& op) {
        if (ext::IsCodeExit(mnemonic, op)) {
            callbacks.AddTranslationHook(addr, [&audio, type](u32 pc, Dynarmic::A32::IREmitter& ir) {
                ext::CallHostFunction(ir, UnlockWrapper, (u64)&audio, (u64)type);
            });
        }
    });
}

static void AddLockInterruptHook(M8AudioProcessor& audio, M8Emulator& emu, u32 begin, u32 end, LockType type)
{
    auto& callbacks = emu.Callbacks();
    u8* code = (u8*)emu.Callbacks().MemoryMap(begin);
    ext::DisassembleIter(code, begin, end - begin, [&callbacks, &audio, type](u32 addr, const std::string& mnemonic, const std::string& op) {
        if (ext::IsCodeDisableInterrupt(mnemonic, op)) {
            callbacks.AddTranslationHook(addr, [&audio, &callbacks, type](u32 pc, Dynarmic::A32::IREmitter& ir) {
                ext::CallHostFunction(ir, LockWrapper, (u64)&audio, (u64)type);
            });
        } else if (ext::IsCodeEnableInterrupt(mnemonic, op)) {
            callbacks.AddTranslationHook(addr, [&audio, &callbacks, type](u32 pc, Dynarmic::A32::IREmitter& ir) {
                ext::CallHostFunction(ir, UnlockWrapper, (u64)&audio, (u64)type);
            });
        }
    });
}

void M8AudioProcessor::LockBlock()
{
    blockMutex.lock();
}

void M8AudioProcessor::UnlockBlock()
{
    blockMutex.unlock();
}

void M8AudioProcessor::LockUSB()
{
    usbMutex.lock();
}

void M8AudioProcessor::UnlockUSB()
{
    usbMutex.unlock();
}

void M8AudioProcessor::Setup()
{
    _AudioStream::Initialize();
    auto& config = FirmwareConfig::GlobalConfig();
    u32 processorNums = config.GetValue<u32>("audio_processor_nums");
    ext::LogInfo("AudioProcessor: multi threading nums = %d", processorNums);

    if (processorNums > 0) {
        for (int i = 0; i < processorNums; i++) {
            pool.emplace_back([this]() { ProcessLoop(); });
        }
        u32 first_update = emu.Callbacks().MemoryRead32(config.GetSymbolAddress("AudioStream::first_update"));
        ParseConnections(first_update);
        AnalyseSuccessors();

        std::vector<std::tuple<u32, u32>> ranges = {
            config.GetEntryRange("AudioStream_F32::transmit"),
            config.GetEntryRange("AudioStream_F32::receiveWritable_f32"),
            config.GetEntryRange("AudioStream_F32::allocate"),
            config.GetEntryRange("AudioStream_F32::release"),
        };
        for (const auto& range : ranges) {
            auto [begin, end] = range;
            AddLockHook(*this, emu, begin, end, LockType::Block);
        }
    }
    if (config.GetValue<bool>("audio_processor_fine_grained_lock"))
    {
        useUSBLock = true;
        ext::LogInfo("AudioProcessor: fine grained lock enabled");
        auto [usb_callback_begin, usb_callback_end] = config.GetEntryRange("usb_audio_transmit_callback");
        AddLockHook(*this, emu, usb_callback_begin, usb_callback_end, LockType::USB);
        std::vector<std::tuple<u32, u32>> ranges = {
            config.GetEntryRange("AudioOutputUSB::update"),
            config.GetEntryRange("AudioStream::allocate"),
            config.GetEntryRange("AudioStream::release"),
        };
        for (const auto& range : ranges) {
            auto [begin, end] = range;
            AddLockInterruptHook(*this, emu, begin, end, LockType::USB);
        }
    }
    emu.Callbacks().AddTranslationHook(config.GetSymbolAddress("usb_audio_transmit_callback_underrun"), [this](u32 pc, Dynarmic::A32::IREmitter&) {
        ext::LogWarn("AudioProcessor: usb audio buffer underrun");
    });

    if (useUSBLock) {
        timer.SetInterval(AUDIO_PROCESS_INTERVAL, [this](Timer&) {
            Process();
        });
    } else {
        timer.SetInterval(AUDIO_PROCESS_INTERVAL, [this](Timer&) {
            auto& callbacks = emu.Callbacks();
            callbacks.Lock();
            Process();
            callbacks.Unlock();
        });
    }
    timer.Start();
}

#define PIPELINE(ptr) pipelineMap[ptr]

void M8AudioProcessor::ParseConnections(u32 first_update)
{
    if (!pipelineMap.empty()) {
        return;
    }
    auto& callbacks = emu.Callbacks();
    u32 ptr = first_update;
    while (ptr) {
        auto* stream = (_AudioStream*)callbacks.MemoryMap(ptr);
        u32 update_func = callbacks.MemoryRead32(stream->vtable());
        ext::LogDebug("_AudioStream(0x%x): update(0x%x), num_inputs = %d, active = %s", ptr, update_func, stream->num_inputs(), stream->active() ? "true" : "false");
        if (!stream->active()) {
            ptr = stream->next_update_ptr();
            continue;
        }
        auto& pipeline = PIPELINE(ptr);
        pipeline.index = pipelines.size();
        pipeline.this_ptr = ptr;
        pipeline.update_func = update_func;
        pipelines.push_back(ptr);
        ptr = stream->destination_list_ptr();
        while (ptr) {
            auto* connection = (_AudioConnection*)callbacks.MemoryMap(ptr);
            ext::LogDebug("\t_AudioConnection(0x%x): 0x%x(%d) -> 0x%x(%d) %s", ptr, connection->src_ptr, connection->src_index, connection->dst_ptr, connection->dest_index, connection->isConnected ? "connected" : "");
            if (connection->isConnected) {
                pipeline.outputs.emplace((u32)connection->dst_ptr, connection->dest_index);
                PIPELINE(connection->dst_ptr).inputs.emplace((u32)connection->src_ptr, connection->src_index);
            }
            ptr = connection->next_dest_ptr;
        }
        ptr = stream->next_update_ptr();
    }
    pipelineFinished.resize(pipelines.size());
}

void M8AudioProcessor::AnalyseSuccessors()
{
    std::set<u32> current;
    for (const auto& [ptr, pipeline] : pipelineMap) {
        if (pipeline.inputs.empty() || pipeline.index == 0) {
            current.insert(pipeline.this_ptr);
        }
    }

    std::set<u32> visited = current;
    while (!current.empty()) {
        std::set<u32> next;
        for (auto ptr : current) {
            auto& pipeline = PIPELINE(ptr);
            pipeline.successors.clear();

            for (auto [dst_ptr, _] : pipeline.outputs) {
                bool ready = true;
                if (ready) {
                    for (auto [src_ptr, __] : PIPELINE(dst_ptr).inputs) {
                        if (PIPELINE(src_ptr).index < PIPELINE(dst_ptr).index) {
                            ready = false;
                            break;
                        }
                    }
                }
                if (ready) {
                    for (auto [out_ptr, __] : PIPELINE(dst_ptr).outputs) {
                        if (PIPELINE(out_ptr).index < PIPELINE(dst_ptr).index) {
                            ready = false;
                            break;
                        }
                    }
                }
                if (ready) {
                    pipeline.successors.insert(dst_ptr);
                    if (!visited.count(dst_ptr)) {
                        visited.insert(dst_ptr);
                        next.insert(dst_ptr);
                    }
                }
            }
        }
        current = next;
    }
}

void M8AudioProcessor::ProcessLoop()
{
    while (running) {
        u32 ptr = 0;
        {
            std::unique_lock lock(workMutex);
            workReady.wait(lock, [this]() { return !readyPipelines.empty(); });
            ptr = *readyPipelines.begin();
            readyPipelines.erase(ptr);
            visitedPipelines.insert(ptr);
        }

        auto& pipeline = PIPELINE(ptr);
        emu.CallFunction(pipeline.update_func, pipeline.this_ptr);

        std::unique_lock lock(workMutex);
        finishedPipelines.insert(ptr);
        pipelineFinished[pipeline.index] = true;
        bool ready = false;
        for (auto next : pipeline.successors) {
            if (!visitedPipelines.count(next) && !readyPipelines.count(next)) {
                readyPipelines.insert(next);
                ready = true;
            }
        }
        for (int i = 0; i < pipelineFinished.size(); i++) {
            if (!pipelineFinished[i]) {
                if (!visitedPipelines.count(pipelines[i]) && !readyPipelines.count(pipelines[i])) {
                    readyPipelines.insert(pipelines[i]);
                    ready = true;
                }
                break;
            }
        }
        if (ready) {
            workReady.notify_all();
        } else {
            workDone.notify_one();
        }
    }
}

void M8AudioProcessor::Process()
{
    auto now = std::chrono::steady_clock::now();
    if (pool.empty()) {
        u32 function = emu.VectorAddress(SOFTWARE_IRQ);
        emu.CallFunction(function, 0);
    } else {
        std::unique_lock lock(workMutex);
        readyPipelines.clear();
        finishedPipelines.clear();
        visitedPipelines.clear();
        for (int i = 0; i < pipelineFinished.size(); i++) {
            pipelineFinished[i] = false;
        }

        for (const auto& [ptr, pipeline] : pipelineMap) {
            if (pipeline.inputs.empty() || pipeline.index == 0) {
                readyPipelines.insert(pipeline.this_ptr);
            }
        }
        workReady.notify_all();
        workDone.wait(lock, [this] () { return finishedPipelines.size() == pipelineMap.size(); });
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - now).count();
    if (duration > AUDIO_PROCESS_INTERVAL.count()) {
        ext::LogDebug("AudioProcessor: process duration = %d us > AUDIO_PROCESS_INTERVAL (%d)", duration, AUDIO_PROCESS_INTERVAL.count());
    }
}

} // namespace m8
