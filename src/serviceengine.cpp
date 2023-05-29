#include "serviceengine.hpp"

using time_clock = std::chrono::system_clock;

ServiceEngine::ServiceEngine()
    : mThread(std::bind(&ServiceEngine::processLoop, this))
{
    mThread.detach();
}

ServiceEngine::~ServiceEngine()
{
    mFirstUpdate = false;
    mThreadRunning = false;
}

ServiceEngine *ServiceEngine::instance()
{
    ServiceEngine *ins;
    return ins;
}

void ServiceEngine::sendMessage(std::string receiver, std::string message)
{
}
std::string ServiceEngine::getMessage(std::string receiver)
{
    return receiver;
}

void ServiceEngine::addObjToLoopEngine(LoopEngine *loopObj)
{
    if (loopObj == nullptr)
        return;

    std::unique_lock<std::shared_mutex> lock(mMutexLoop);
    auto iter = mLoopEngineList.begin();
    while (iter != mLoopEngineList.end())
    {
        if ((*iter) == loopObj)
        {
            return;
        }
    }
    mLoopEngineList.push_back(loopObj);
}

void ServiceEngine::removeObjFrLoopEngine(LoopEngine *loopObj)
{
    if (loopObj == nullptr)
        return;

    std::unique_lock<std::shared_mutex> lock(mMutexLoop);
    mLoopEngineList.erase(std::remove(mLoopEngineList.begin(), mLoopEngineList.end(), loopObj), mLoopEngineList.end());
}

bool ServiceEngine::isNotResponseMode()
{
    return mRunningMode == RunningMode::NotResponseMode;
}

void ServiceEngine::processLoop()
{
    mThreadRunning = true;
    static time_clock::time_point lastUpdate = time_clock::now();
    while(mThreadRunning)
    {
        std::chrono::milliseconds delta = std::chrono::duration_cast<std::chrono::milliseconds> (time_clock::now() - lastUpdate);
        lastUpdate = time_clock::now();

        mMutexProcess.lock_shared();
        auto iter = mLoopEngineList.begin();
        while(iter != mLoopEngineList.end())
        {
            (*iter)->processLoop(delta);
            iter++;
        }
        mMutexProcess.unlock_shared();

        usleep(mDelayMicroSecond);
        if(mFirstUpdate)
        {
            mFirstUpdate = false;
        }
    }
}