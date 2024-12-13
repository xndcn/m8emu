#pragma once

#include <set>
#include <vector>
#include <tuple>
#include <thread>
#include <condition_variable>
#include "m8emu.h"
#include "timer.h"

namespace m8 {

struct AudioPipeline {
    int index;
    u32 this_ptr;
    u32 update_func;
    std::set<std::tuple<u32, int>> inputs;
    std::set<std::tuple<u32, int>> outputs;
};

class M8AudioProcessor {
public:
    M8AudioProcessor(M8Emulator& emu);
    void Setup();
    void Process();

    void LockAudioBlock();
    void UnlockAudioBlock();

private:
    void ParseConnections(u32 first_update);
    void ProcessLoop();

private:
    M8Emulator& emu;
    bool running = true;
    std::mutex workMutex;
    std::condition_variable workReady;
    std::condition_variable workDone;
    std::vector<std::thread> pool;
    std::recursive_mutex audioMutex;
    Timer timer;

    std::vector<u32> pipelines;
    std::map<u32, AudioPipeline> pipelineMap;

private:
    std::vector<bool> pipelineFinished;
    std::set<u32> readyPipelines;
    std::set<u32> visitedPipelines;
    std::set<u32> finishedPipelines;
};

} // namespace m8
