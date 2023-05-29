#ifndef LOOPENGINE_H
#define LOOPENGINE_H

#include <chrono>
#include <string>

class LoopEngine
{
public:
    virtual void processLoop(std::chrono::microseconds delta) = 0;
};

#endif