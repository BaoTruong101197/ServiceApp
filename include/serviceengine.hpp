#ifndef SERVICEENGINE_H
#define SERVICEENGINE_H

#include <string>
#include <vector>
#include <algorithm>
#include <shared_mutex>
#include <thread>
#include <unistd.h>
#include "common/loopengine.hpp"

static constexpr int16_t DelayMicroSeconds = 10000;

class ServiceEngine
{
public:
    enum class RunningMode
    {
        NormalMode = 0,
        NotResponseMode
    };
    static ServiceEngine *instance();

    void addObjToLoopEngine(LoopEngine *loopObj);
    void removeObjFrLoopEngine(LoopEngine *loopObj);

    void sendMessage(std::string receiver, std::string message);
    std::string getMessage(std::string receiver);

    bool isNotResponseMode();

    void processLoop();

private: 
    ServiceEngine();
    ~ServiceEngine();

    std::vector<LoopEngine*> mLoopEngineList;
    useconds_t mDelayMicroSecond = DelayMicroSeconds;
    std::shared_mutex mMutexLoop;
    std::shared_mutex mMutexProcess;

    std::thread mThread;
    bool mThreadRunning{false};

    RunningMode mRunningMode{RunningMode::NormalMode};
    bool mFirstUpdate{false};
};

#endif