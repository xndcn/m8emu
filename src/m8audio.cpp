#include "m8audio.h"
#include <cstddef>
#include <ext/disassembler.h>
#include <ext/log.h>
#include <ext/ir.h>
#include "config.h"

using namespace std::chrono_literals;
#define AUDIO_PROCESS_INTERVAL 1451us
#define AUDIO_PROCESSOR_NUMS 2

namespace m8 {

class _AudioStream
{
public:
    u32 vtable() { return *(u32*)this; } // 0x00, _AudioStream::update()
    bool active() { return *(u8*)((u8*)this + active_offset); }
    u32 next_update_ptr() { return *(u32*)((u8*)this + next_update_offset); }

    u8 num_inputs() { return *(u8*)((u8*)this + (is_mod() ? num_inputs_mod_offset : num_inputs_offset)); }
    u32 destination_list_ptr() { return *(u32*)((u8*)this + (is_mod() ? destination_list_mod_offset : destination_list_offset)); }
    u32 inputQueue(int index) { return *(u32*)((u8*)this + (is_mod() ? inputQueue_mod_offset : inputQueue_offset)) + index * sizeof(u32); }

    static void Initialize()
    {
        auto& config = FirmwareConfig::GlobalConfig();
        active_offset = config.GetValue<u32>("AudioStream_offset_active");
        num_inputs_offset = config.GetValue<u32>("AudioStream_offset_num_inputs");
        next_update_offset = config.GetValue<u32>("AudioStream_offset_next_update");
        destination_list_offset = config.GetValue<u32>("AudioStream_offset_destination_list");
        num_inputs_mod_offset = config.GetValue<u32>("AudioStream_offset_num_inputs_mod");
        destination_list_mod_offset = config.GetValue<u32>("AudioStream_offset_destination_list_mod");
        inputQueue_offset = config.GetValue<u32>("AudioStream_offset_inputQueue");
        inputQueue_mod_offset = config.GetValue<u32>("AudioStream_offset_inputQueue_mod");

    }

private:
    bool is_mod() { return is_mod_map.count(vtable()) ? is_mod_map[vtable()] : *(u32*)((u8*)this + destination_list_offset) == 0 && *(u32*)((u8*)this + destination_list_mod_offset) != 0; }
    static inline std::map<u32, bool> is_mod_map;
    static inline u32 active_offset;
    static inline u32 num_inputs_offset;
    static inline u32 num_inputs_mod_offset;
    static inline u32 next_update_offset;
    static inline u32 destination_list_offset;
    static inline u32 destination_list_mod_offset;
    static inline u32 inputQueue_offset;
    static inline u32 inputQueue_mod_offset;
};

struct __attribute__ ((packed)) _AudioConnection
{
    u32 src_ptr;       // 0x00, _AudioStream*
    u32 dst_ptr;       // 0x04, _AudioStream*
    u8 src_index;      // 0x08
    u8 dest_index;     // 0x09
    u16 padding;
    u32 next_dest_ptr; // 0x0c, _AudioConnection*
    bool isConnected;  // 0x10
};

static_assert(offsetof(_AudioConnection, src_ptr) == 0x00);
static_assert(offsetof(_AudioConnection, dst_ptr) == 0x04);
static_assert(offsetof(_AudioConnection, src_index) == 0x08);
static_assert(offsetof(_AudioConnection, dest_index) == 0x09);
static_assert(offsetof(_AudioConnection, next_dest_ptr) == 0x0c);
static_assert(offsetof(_AudioConnection, isConnected) == 0x10);

struct __attribute__ ((packed)) audio_block_t {
    u8  ref_count;
    u8  reserved1;
    u16 memory_pool_index;
    u16 data[0];
};
static_assert(offsetof(audio_block_t, data) == 0x04);

M8AudioProcessor::M8AudioProcessor(M8Emulator& emu) : emu(emu)
{
    for (int i = 0; i < AUDIO_PROCESSOR_NUMS; i++)
        pool.emplace_back([this]() { ProcessLoop(); });

    _AudioStream::Initialize();
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

static void PushUSBAudioWrapper(u64 ptr, u64 param)
{
    M8AudioProcessor* audio = (M8AudioProcessor*)ptr;
    audio->PushUSBAudioBlock(param);
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
    audioMutex.lock();
}

void M8AudioProcessor::UnlockAudioBlock()
{
    audioMutex.unlock();
}

void M8AudioProcessor::Setup()
{
    auto& config = FirmwareConfig::GlobalConfig();
    u32 first_update = emu.Callbacks().MemoryRead32(config.GetSymbolAddress("AudioStream_first_update"));
    ParseConnections(first_update);

    std::vector<std::tuple<u32, u32>> ranges = {
        config.GetEntryRange("AudioStream_transmit"),
        config.GetEntryRange("AudioStream_receiveWritable"),
        config.GetEntryRange("AudioStream_allocate"),
        config.GetEntryRange("AudioStream_release"),
    };
    for (const auto& range : ranges) {
        auto [begin, end] = range;
        AddLockHook(*this, emu, begin, end);
    }

    emu.Callbacks().AddTranslationHook(config.GetSymbolAddress("AudioOutputUSB_update"), [this](u32, Dynarmic::A32::IREmitter& ir) {
	ext::U64 param(ir, ext::Reg::R0);
        ext::CallHostFunction(ir, PushUSBAudioWrapper, (u64)this, param);
    });

    timer.SetInterval(AUDIO_PROCESS_INTERVAL, [this](Timer&) {
        auto& callbacks = emu.Callbacks();
        callbacks.lock();
        Process();
        callbacks.unlock();
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
        emu.CallFunction1(pipeline.update_func, pipeline.this_ptr);

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
    std::unique_lock lock(workMutex);
    for (const auto& [ptr, pipeline] : pipelineMap) {
        if (pipeline.inputs.empty() || pipeline.index == 0) {
            readyPipelines.insert(pipeline.this_ptr);
        }
    }
    workReady.notify_all();
    workDone.wait(lock, [this] () { return finishedPipelines.size() == pipelineMap.size(); });

    readyPipelines.clear();
    finishedPipelines.clear();
    visitedPipelines.clear();
    for (int i = 0; i < pipelineFinished.size(); i++) {
        pipelineFinished[i] = false;
    }
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - now).count();
    ext::LogDebug("AudioProcessor duration = %d us", duration);
}

void M8AudioProcessor::PushUSBAudioBlock(u32 ptr)
{
    auto& callbacks = emu.Callbacks();
    auto* stream = (_AudioStream*)callbacks.MemoryMap(ptr);
    std::lock_guard lock(audioMutex);
    auto* left_audio = (audio_block_t*)callbacks.MemoryMap(callbacks.MemoryRead32(stream->inputQueue(0)));
    auto* right_audio = (audio_block_t*)callbacks.MemoryMap(callbacks.MemoryRead32(stream->inputQueue(1)));
    u16* left = left_audio->data;
    u16* right = right_audio->data;
    std::vector<u32> buffer(64);
    for (int i = 0; i < buffer.size(); i++) {
        buffer[i] = (*right++ << 16) | (*left++ & 0xFFFF);
    }
    emu.USBDevice().PushData(5, (u8*)buffer.data(), buffer.size() * sizeof(u32));
}

} // namespace m8
