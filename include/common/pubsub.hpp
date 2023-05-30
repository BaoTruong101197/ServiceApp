#ifndef STUB_PUBSUB_H
#define STUB_PUBSUB_H

#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <shared_mutex>
#include <common/customable.h>
#include <poll.h>
#include <functional>

/**

- This is a simple pubsub mechanism which using string for communication
- we are not handling any read/write error.
- the message length is hard set now.
- It should be updated to do: handle errors, verify length of message, using other structures than string for message.
- event string from client-> server format: appID$Topic$Message
*/

/*

- GUIDE to use:
- When need to publish any information to other processes.
- call : stub::pubsub::PubSubManager::instance().publish(topic, message);
- When need to receive information from other processes.
- declare a FunctionalSubcriber or object inheritated from Subcriber
- override onMessage function to handle the message.
- register with topic: stub::pubsub::PubSubManager::instance().registerTopic(topic, &subcriber)
*/

namespace stub::pubsub
{
    class Subcriber
    {
    public:
        Subcriber(){};
        virtual void onMessage(std::string message) = 0;
    };

    class FunctionalSubcriber : virtual public Subcriber
    {
        std::function<void(std::string)> mFunc;

    public:
        FunctionalSubcriber(std::function<void(std::string)> fn = nullptr) : mFunc(fn) {}
        virtual ~FunctionalSubcriber() {}
        void setFunction(std::function<void(std::string)> fn)
        {
            mFunc = fn;
        }
        void onMessage(std::string message) override
        {
            if (mFunc != nullptr)
                mFunc(message);
        }
    };

    class PubSubManager : virtual public Customable
    {
        friend class StubEngine;
        struct Client
        {
            size_t mAppID = SIZE_MAX;
            Client(int fd, PubSubManager &parent) : mSockfd(fd),
                                                    mParent{parent}
            {
            }
            int mSockfd = -1;
            struct pollfd mFds[1];
            std::thread mThread;
            bool mThreadRunning = false;
            bool mIsStarted = false;

            std::vector<std::string> mWaitingMessages;
            std::mutex mMuxMessages;

            PubSubManager &mParent;

            void start();
            void stop();
            void loop();
            void send(std::string message);

        private:
            void sendPop();
        };

        friend class Client;

        class Topic
        {
        public:
            void broadcast(std::string message);
            void addSubcriber(Subcriber *sub);
            void removeSubcriber(Subcriber *sub);

        private:
            std::string mLastBroadcast = "";
            std::vector<Subcriber *> mSubcribers;
            std::mutex mMuxSubcriber;
        };

    private:
        PubSubManager();
        virtual ~PubSubManager();

        void startServer();
        void startClient();

    public:
        static PubSubManager &instance();

        void init();

        void registerTopic(std::string topic, Subcriber *sub);

        void publish(std::string topic, std::string message);

        // Implement customable
        void custom(const std::map<std::string, std::string> &customTable) override;

    private:
        std::map<std::string, Topic *> mTopics;
        std::shared_mutex mMuxTopic;

        bool mIsStarted = false;
        bool mIsReady = false;
        bool mIsServer = false;
        int mSockfd = -1;
        struct pollfd mFds[1];
        size_t mAppID = SIZE_MAX;

        std::thread mThread;
        bool mThreadRunning = false;

        void acceptLoop();
        void readWriteLoopClient();

        std::map<int, Client *> mClients;
        std::shared_mutex mMuxClient;

        std::vector<std::string> mSendMessages;
        std::mutex mMuxSendMessage;

        void broadcastMessage(std::string topic, std::string message);

        void addSendMessage(std::string message);
    };

}
#endif // STUB_PUBSUB_H