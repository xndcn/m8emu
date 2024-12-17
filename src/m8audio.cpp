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

struct __attribute__ ((packed)) _AudioStream
{
    u32 vtable;                // 0x00, _AudioStream::update()
    u16 cpu_cycles;            // 0x04
    u16 cpu_cycles_max;        // 0x06
    u32 next_update_ptr;       // 0x08, _AudioStream*
    bool active;               // 0x0c
    u8 num_inputs;             // 0x0d
    u8 numConnections;         // 0x0e
    u8 padding;
    u32 destination_list_ptr;  // 0x10, _AudioConnection*
    u32 inputQueue_ptr;        // 0x14, audio_block_t**
};

struct __attribute__ ((packed)) _AudioStream_F32
{
    _AudioStream root;
    u8 num_inputs;             // 0x18
    u8 padding[3];
    u32 destination_list_ptr;  // 0x1c, _AudioConnection*
    u32 inputQueue_ptr;        // 0x20, audio_block_t**
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

static_assert(offsetof(_AudioStream, cpu_cycles) == 0x04);
static_assert(offsetof(_AudioStream, cpu_cycles_max) == 0x06);
static_assert(offsetof(_AudioStream, next_update_ptr) == 0x08);
static_assert(offsetof(_AudioStream, active) == 0x0c);
static_assert(offsetof(_AudioStream, num_inputs) == 0x0d);
static_assert(offsetof(_AudioStream, numConnections) == 0x0e);
static_assert(offsetof(_AudioStream, destination_list_ptr) == 0x10);
static_assert(offsetof(_AudioStream, inputQueue_ptr) == 0x14);

static_assert(offsetof(_AudioStream_F32, num_inputs) == 0x18);
static_assert(offsetof(_AudioStream_F32, destination_list_ptr) == 0x1c);
static_assert(offsetof(_AudioStream_F32, inputQueue_ptr) == 0x20);

static_assert(offsetof(_AudioConnection, src_ptr) == 0x00);
static_assert(offsetof(_AudioConnection, dst_ptr) == 0x04);
static_assert(offsetof(_AudioConnection, src_index) == 0x08);
static_assert(offsetof(_AudioConnection, dest_index) == 0x09);
static_assert(offsetof(_AudioConnection, next_dest_ptr) == 0x0c);
static_assert(offsetof(_AudioConnection, isConnected) == 0x10);

M8AudioProcessor::M8AudioProcessor(M8Emulator& emu) : emu(emu)
{
}

static void LockBlockWrapper(u64 ptr)
{
    M8AudioProcessor* audio = (M8AudioProcessor*)ptr;
    audio->LockAudioBlock();
}

static void UnlockBlockWrapper(u64 ptr)
{
    M8AudioProcessor* audio = (M8AudioProcessor*)ptr;
    audio->UnlockAudioBlock();
}

static void AddLockHook(M8AudioProcessor& audio, M8Emulator& emu, u32 begin, u32 end)
{
    auto& callbacks = emu.Callbacks();
    callbacks.AddTranslationHook(begin, [&audio](u32 pc, Dynarmic::A32::IREmitter& ir) {
        ext::CallHostFunction(ir, LockBlockWrapper, (u64)&audio);
    });
    u8* code = (u8*)emu.Callbacks().MemoryMap(begin);
    ext::DisassembleIter(code, begin, end - begin, [&callbacks, &audio](u32 addr, const std::string& mnemonic, const std::string& op) {
        if (ext::IsCodeExit(mnemonic, op)) {
            callbacks.AddTranslationHook(addr, [&audio](u32 pc, Dynarmic::A32::IREmitter& ir) {
                ext::CallHostFunction(ir, UnlockBlockWrapper, (u64)&audio);
            });
        }
    });
}

void M8AudioProcessor::LockAudioBlock()
{
    blockMutex.lock();
}

void M8AudioProcessor::UnlockAudioBlock()
{
    blockMutex.unlock();
}

void M8AudioProcessor::Setup()
{
    auto& config = FirmwareConfig::GlobalConfig();
    u32 processorNums = config.GetValue<u32>("audio_processor_nums");
    ext::LogInfo("AudioProcessor: multi threading nums = %d", processorNums);

    if (processorNums > 0) {
        for (int i = 0; i < processorNums; i++) {
            pool.emplace_back([this]() { ProcessLoop(); });
        }
        u32 first_update = emu.Callbacks().MemoryRead32(config.GetSymbolAddress("AudioStream::first_update"));
        ParseConnections(first_update);

        std::vector<std::tuple<u32, u32>> ranges = {
            config.GetEntryRange("AudioStream_F32::transmit"),
            config.GetEntryRange("AudioStream_F32::receiveWritable_f32"),
            config.GetEntryRange("AudioStream_F32::allocate"),
            config.GetEntryRange("AudioStream_F32::release"),
        };
        for (const auto& range : ranges) {
            auto [begin, end] = range;
            AddLockHook(*this, emu, begin, end);
        }
    }

    timer.SetInterval(AUDIO_PROCESS_INTERVAL, [this](Timer&) {
        auto& callbacks = emu.Callbacks();
        callbacks.Lock();
        Process();
        callbacks.Unlock();
    });
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
        u32 update_func = callbacks.MemoryRead32(stream->vtable);
        ext::LogDebug("_AudioStream(0x%x): update(0x%x), num_inputs = %d", ptr, update_func, stream->num_inputs);
        if (!stream->active) {
            ptr = stream->next_update_ptr;
            continue;
        }
        auto& pipeline = PIPELINE(ptr);
        pipeline.index = pipelines.size();
        pipeline.this_ptr = ptr;
        pipeline.update_func = update_func;
        pipelines.push_back(ptr);
        if (stream->destination_list_ptr) {
            ptr = stream->destination_list_ptr;
        } else {
            ptr = ((_AudioStream_F32*)stream)->destination_list_ptr;
        }
        while (ptr) {
            auto* connection = (_AudioConnection*)callbacks.MemoryMap(ptr);
            ext::LogDebug("\t_AudioConnection(0x%x): 0x%x(%d) -> 0x%x(%d) %s", ptr, connection->src_ptr, connection->src_index, connection->dst_ptr, connection->dest_index, connection->isConnected ? "connected" : "");
            if (connection->isConnected) {
                pipeline.outputs.emplace((u32)connection->dst_ptr, connection->dest_index);
                PIPELINE(connection->dst_ptr).inputs.emplace((u32)connection->src_ptr, connection->src_index);
            }
            ptr = connection->next_dest_ptr;
        }
        ptr = stream->next_update_ptr;
    }
    pipelineFinished.resize(pipelines.size());
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
        for (auto [dst_ptr, _] : pipeline.outputs) {
            bool ready = !visitedPipelines.count(dst_ptr) && !readyPipelines.count(dst_ptr);
            if (ready) {
                for (auto [src_ptr, __] : PIPELINE(dst_ptr).inputs) {
                    if (PIPELINE(src_ptr).index < PIPELINE(dst_ptr).index && !finishedPipelines.count(src_ptr)) {
                        ready = false;
                        break;
                    }
                }
            }
            if (ready) {
                for (auto [out_ptr, __] : PIPELINE(dst_ptr).outputs) {
                    if (PIPELINE(out_ptr).index < PIPELINE(dst_ptr).index && !finishedPipelines.count(out_ptr)) {
                        ready = false;
                        break;
                    }
                }
            }
            if (ready) {
                readyPipelines.insert(dst_ptr);
                workReady.notify_all();
            }
        }
        pipelineFinished[pipeline.index] = true;
        for (int i = 0; i < pipelineFinished.size(); i++) {
            if (!pipelineFinished[i]) {
                if (!visitedPipelines.count(pipelines[i]) && !readyPipelines.count(pipelines[i])) {
                    readyPipelines.insert(pipelines[i]);
                    workReady.notify_all();
                }
                break;
            }
        }
        workDone.notify_one();
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
        ext::LogWarn("AudioProcessor: process duration = %d us > AUDIO_PROCESS_INTERVAL (%d)", duration, AUDIO_PROCESS_INTERVAL.count());
    }
}

} // namespace m8
