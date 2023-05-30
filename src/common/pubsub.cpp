#include "pubsub.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <future>

#ifdef USE_PUBSUB

#define _BSD_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <algorithm>

#endif // USE_PUBSUB

#include <stubengine.h>

std::string __PubsubName = prettyEvent("pubsub");
const char *__PubsubNameCStr = __PubsubName.c_str();
#define SERVICE_NAME __PubsubNameCStr

namespace stub::pubsub
{
    void PubSubManager::Client::start()
    {
        if (mIsStarted)
            return;
#ifdef USE_PUBSUB

        mThread = std::thread(std::bind(&stub::pubsub::PubSubManager::Client::loop, this));
        mThread.detach();
#endif // USE_PUBSUB
        mIsStarted = true;
    }
    void PubSubManager::Client::stop()
    {
        mThreadRunning = false;
    }
    void PubSubManager::Client::loop()
    {
#ifdef USE_PUBSUB
        mThreadRunning = true;
        mFds[0].fd = mSockfd;
        mFds[0].events = POLLIN | POLLPRI;
        static char buff[5000];
        while (mThreadRunning)
        {
            int rv = poll(mFds, 1, 100);

            if (rv > 0)
            {
                if (mFds[0].revents & POLLIN)
                {
                    int n = read(mSockfd, buff, 5000);
                    //
                    if (n > 0 && n < 5000)
                    {
                        // parse command
                        std::string str;
                        str.resize(n);
                        memcpy(str.data(), buff, n);

                        printf("[%s] read from client %s\\n", SERVICE_NAME, str.c_str());
                        // also broadcast to self
                        mParent.addSendMessage(str);

                        std::vector split = StringUtil::split(str, "$");
                        if (split.size() >= 3)
                        {
                            uint32_t messageAppID = StringUtil::parseUInt32(split[0]);
                            if (messageAppID != UINT32_MAX && messageAppID != mParent.mAppID)
                            {
                                mParent.broadcastMessage(split[1], split[2]);
                            }
                        }
                    }
                }

                if (mFds[0].revents & POLLOUT)
                {
                    sendPop();
                }
            }
            else
            {
                sendPop();
            }
        }
        if (mSockfd != -1)
        {
            close(mSockfd);
        }

#endif // USE_PUBSUB
    }
    void PubSubManager::Client::sendPop()
    {
        if (!mIsStarted)
        {
            return;
        }

#ifdef USE_PUBSUB
        std::unique_lockstd::mutex lock(mMuxMessages);

        if (mWaitingMessages.size() > 0)
        {
            std::string sendMessage = mWaitingMessages.front();
            mWaitingMessages.erase(mWaitingMessages.begin());

            printf("[%s] server write to client:%s mess:%s\\n",
                   SERVICE_NAME,
                   PRETTY_VAL(mSockfd),
                   PRETTY_VAL(sendMessage));

            int n = write(mSockfd, sendMessage.data(), sendMessage.size());
            if (n < 0)
            {
                // close socket
            }
        }

#endif // USE_PUBSUB
    }

    void PubSubManager::Client::send(std::string message)
    {

#ifdef USE_PUBSUB
        std::unique_lockstd::mutex lock(mMuxMessages);
        mWaitingMessages.push_back(message);
#endif // USE_PUBSUB
    }
}

namespace stub::pubsub
{
    void PubSubManager::Topic::broadcast(std::string message)
    {
#ifdef USE_PUBSUB
        if (message != mLastBroadcast)
        {

            mLastBroadcast = message;

            std::async(std::launch::async, [=, this]
                       {
            std::unique_lock<std::mutex> lock(mMuxSubcriber);

            for(auto sub: mSubcribers)
            {
                sub->onMessage(mLastBroadcast);
            } });
        }

#endif // USE_PUBSUB
    }

    void PubSubManager::Topic::addSubcriber(Subcriber *sub)
    {

#ifdef USE_PUBSUB
        if (sub == nullptr)
            return;

        std::unique_lock<std::mutex> lock(mMuxSubcriber);

        if (std::find(mSubcribers.begin(), mSubcribers.end(), sub) != mSubcribers.end())
            return;

        mSubcribers.push_back(sub);

#endif // USE_PUBSUB
    }

    void PubSubManager::Topic::removeSubcriber(Subcriber *sub)
    {

#ifdef USE_PUBSUB
        if (sub == nullptr)
            return;

        std::unique_lock<std::mutex> lock(mMuxSubcriber);

        mSubcribers.erase(std::remove(mSubcribers.begin(), mSubcribers.end(), sub), mSubcribers.end());

#endif // USE_PUBSUB
    }
}

namespace stub::pubsub
{
    PubSubManager::PubSubManager()
    {
        StubEngine::instance()->addCustomable("pubsub", this);
    }

    PubSubManager::~PubSubManager()
    {
        mThreadRunning = false;

        StubEngine::instance()->removeCustomable("pubsub", this);

        for (auto pair : mTopics)
        {
            delete pair.second;
        }
    }
    PubSubManager &PubSubManager::instance()
    {
        static PubSubManager ins;
        return ins;
    }

    void PubSubManager::init()
    {
    }

    void PubSubManager::custom(const std::map<std::string, std::string> &customTable)
    {
        uint32_t appID = UINT32_MAX;
        bool isServer = false;
        bool hasRoleConfig = false;
        for (auto pair : customTable)
        {

            if (pair.first == "appID")
            {
                appID = StringUtil::parseUInt32(pair.second);
                printf("[%s] appID:%s\\n", SERVICE_NAME, PRETTY_VAL(appID));
            }
            else if (pair.first == "role")
            {
                isServer = pair.second == "server";
                hasRoleConfig = true;
                printf("[%s] role:%s\\n", SERVICE_NAME, PRETTY_VAL(pair.second));
            }
        }
        if (mAppID == SIZE_MAX && appID != UINT32_MAX)
        {
            mAppID = appID;
        }
        if (hasRoleConfig && !mIsStarted && mAppID != SIZE_MAX)
        {
            if (isServer)
            {
                startServer();
            }
            else
            {
                startClient();
            }
        }
    }

    void PubSubManager::startServer()
    {
        if (mIsStarted)
            return;

#ifdef USE_PUBSUB
        int portno = 8080;
        struct sockaddr_in serv_addr;
        int n;

        // create a socket
        // socket(int domain, int type, int protocol)
        mSockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (mSockfd < 0)
        {
            logError("ERROR opening socket\\n");
            return;
        }
        int opt = 1;

        if (setsockopt(mSockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            logError("setsockopt fail\\n");
            close(mSockfd);
            return;
        }

        if (setsockopt(mSockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
        {
            logError("setsockopt fail\\n");
            close(mSockfd);
            return;
        }

        // clear address structure
        bzero((char *)&serv_addr, sizeof(serv_addr));

        /* setup the host_addr structure for use in bind call */
        // server byte order
        serv_addr.sin_family = AF_INET;

        // automatically be filled with current host's IP address
        serv_addr.sin_addr.s_addr = INADDR_ANY;

        // convert short integer value for port must be converted into network byte order
        serv_addr.sin_port = htons(portno);

        // bind(int fd, struct sockaddr *local_addr, socklen_t addr_length)
        // bind() passes file descriptor, the address structure,
        // and the length of the address structure
        // This bind() call will bind  the socket to the current IP address on port, portno
        if (bind(mSockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            logError("ERROR on binding\\n");
            close(mSockfd);
            return;
        }

        // This listen() call tells the socket to listen to the incoming connections.
        // The listen() function places all incoming connection into a backlog queue
        // until accept() call accepts the connection.
        // Here, we set the maximum size for the backlog queue to 5.
        listen(mSockfd, 15);
        printf("[%s] listening at:%s\\n", SERVICE_NAME, PRETTY_VAL(portno));

        mIsStarted = true;
        mIsServer = true;
        mIsReady = true;

        mThread = std::thread(std::bind(&stub::pubsub::PubSubManager::acceptLoop, this));
        mThread.detach();

#else
        mIsStarted = true;
        mIsServer = true;
        mIsReady = true;
#endif // USE_PUBSUB
    }

    void PubSubManager::startClient()
    {
        if (mIsStarted)
            return;

#ifdef USE_PUBSUB
        struct sockaddr_in serv_addr;
        struct hostent *server;

        char buffer[256];

        int portno = 8080;
        mSockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (mSockfd < 0)
        {
            logError("ERROR opening socket\\n");
            return;
        }

        server = gethostbyname("localhost");
        if (server == NULL)
        {
            logError("ERROR opening socket\\n");
            close(mSockfd);
            return;
        }
        bzero((char *)&serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;

        bcopy((char *)server->h_addr,
              (char *)&serv_addr.sin_addr.s_addr,
              server->h_length);

        serv_addr.sin_port = htons(portno);

        if (connect(mSockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        {
            logError("ERROR connecting\\n");
            close(mSockfd);
            return;
        }

        printf("[%s] connected at:%s\\n", SERVICE_NAME, PRETTY_VAL(portno));
        mIsStarted = true;
        mIsServer = false;
        mIsReady = true;

        mThread = std::thread(std::bind(&stub::pubsub::PubSubManager::readWriteLoopClient, this));
        mThread.detach();

#else
        mIsStarted = true;
        mIsServer = false;
        mIsReady = true;
#endif // USE_PUBSUB
    }
    /**
     * @brief: acceptLoop for server only
     */
    void PubSubManager::acceptLoop()
    {
#ifdef USE_PUBSUB
        mThreadRunning = true;

        mFds[0].fd = mSockfd;
        mFds[0].events = POLLIN | POLLPRI;

        while (mThreadRunning)
        {
            static struct sockaddr_in clientAddr;
            static socklen_t clientStructLen = sizeof(clientAddr);

            if (poll(mFds, 1, 100))
            {
                int newsockfd = accept(
                    mSockfd,
                    (struct sockaddr *)&clientAddr,
                    &clientStructLen);

                if (newsockfd < 0)
                {
                    continue;
                }

                printf("[%s] got connection from %s port %d\\n",
                       SERVICE_NAME,
                       inet_ntoa(clientAddr.sin_addr),
                       ntohs(clientAddr.sin_port));

                Client *client = new Client(newsockfd, *this);

                mMuxClient.lock();
                mClients[newsockfd] = client;
                mMuxClient.unlock();

                client->start();
            }
            else
            {
                // write phase
                std::string sendMessage = "";
                mMuxSendMessage.lock();
                if (mSendMessages.size() > 0)
                {
                    sendMessage = mSendMessages.front();
                    printf("[%s] push phrase\\n", SERVICE_NAME);
                    mSendMessages.erase(mSendMessages.begin());
                }
                mMuxSendMessage.unlock();

                if (sendMessage.size() > 0)
                {
                    mMuxClient.lock();
                    for (auto clientPair : mClients)
                    {
                        clientPair.second->send(sendMessage);
                    }
                    mMuxClient.unlock();
                }
            }
        }

        if (mSockfd > -1)
        {
            close(mSockfd);
        }

#endif // USE_PUBSUB
    }

    void PubSubManager::readWriteLoopClient()
    {

#ifdef USE_PUBSUB
        mThreadRunning = true;
        mFds[0].fd = mSockfd;
        mFds[0].events = POLLIN | POLLPRI;
        static char buff[5000];

        while (mThreadRunning)
        {
            int rv = poll(mFds, 1, 100);
            if (rv > 0)
            {
                if (mFds[0].revents & POLLIN)
                {
                    int n = read(mSockfd, buff, 5000);
                    if (n > 0 && n < 5000)
                    {
                        // parse command
                        std::string str;
                        str.resize(n);
                        memcpy(str.data(), buff, n);
                        // broadcast to self
                        printf("[%s] read from client:%s\\n", SERVICE_NAME, PRETTY_VAL(str));
                        std::vector split = StringUtil::split(str, "$");
                        if (split.size() >= 3)
                        {
                            uint32_t messageAppID = StringUtil::parseUInt32(split[0]);
                            if (messageAppID != UINT32_MAX && messageAppID != mAppID)
                            {

                                broadcastMessage(split[1], split[2]);
                            }
                        }
                    }
                }
                if (mFds[0].revents & POLLOUT)
                {
                    // write phase
                    std::string sendMessage = "";
                    mMuxSendMessage.lock();
                    if (mSendMessages.size() > 0)
                    {
                        sendMessage = mSendMessages.front();
                        printf("[%s] write phrase\\n", SERVICE_NAME);
                        mSendMessages.erase(mSendMessages.begin());
                    }
                    mMuxSendMessage.unlock();

                    if (sendMessage.size() > 0)
                    {
                        printf("[%s] write to server:%s mess:%s\\n",
                               SERVICE_NAME,
                               PRETTY_VAL(mSockfd),
                               PRETTY_VAL(sendMessage));

                        int n = write(mSockfd, sendMessage.data(), sendMessage.size());
                        if (n < 0)
                        {
                            // close socket
                        }
                    }
                }
            }
            else
            {
                std::string sendMessage = "";
                mMuxSendMessage.lock();
                if (mSendMessages.size() > 0)
                {
                    sendMessage = mSendMessages.front();
                    printf("[%s] write phrase\\n", SERVICE_NAME);
                    mSendMessages.erase(mSendMessages.begin());
                }
                mMuxSendMessage.unlock();

                if (sendMessage.size() > 0)
                {
                    printf("[%s] write to server:%s mess:%s\\n",
                           SERVICE_NAME,
                           PRETTY_VAL(mSockfd),
                           PRETTY_VAL(sendMessage));

                    int n = write(mSockfd, sendMessage.data(), sendMessage.size());
                    if (n < 0)
                    {
                        // close socket
                    }
                }
            }
        }

#endif // USE_PUBSUB
    }

    void PubSubManager::registerTopic(std::string topic, Subcriber *sub)
    {

#ifdef USE_PUBSUB
        Topic *topicPublisher = nullptr;

        mMuxTopic.lock();

        auto iter = mTopics.find(topic);

        if (iter == mTopics.end())
        {
            topicPublisher = new Topic();
            mTopics[topic] = topicPublisher;
        }
        else
        {
            topicPublisher = iter->second;
        }
        mMuxTopic.unlock();

        topicPublisher->addSubcriber(sub);

#endif // USE_PUBSUB
    }

    // self broadcast
    void PubSubManager::broadcastMessage(std::string topic, std::string message)
    {

#ifdef USE_PUBSUB
        printf("[%s] broadcast app:%s topic:%s mess:%s\n", SERVICE_NAME,
               PRETTY_VAL(mAppID),
               PRETTY_VAL(topic),
               PRETTY_VAL(message));

        mMuxTopic.lock();
        auto iter = mTopics.find(topic);

        if (iter != mTopics.end())
        {
            iter->second->broadcast(message);
        }
        mMuxTopic.unlock();

#endif // USE_PUBSUB
    }

    // publish for other clients
    void PubSubManager::publish(std::string topic, std::string message)
    {
        if (!mIsReady)
            return;

#ifdef USE_PUBSUB
        char *buff = logGetBuffer();
        buff[0] = '\0';
        sprintf(buff, "%ld$%s$%s", mAppID, topic.c_str(), message.c_str());

        addSendMessage(std::string(buff));

#endif // USE_PUBSUB
    }

    void PubSubManager::addSendMessage(std::string message)
    {

#ifdef USE_PUBSUB
        mMuxSendMessage.lock();

        mSendMessages.push_back(message);

        printf("[%s] publish mess:%s\\n",
               SERVICE_NAME,
               PRETTY_VAL(message));

        mMuxSendMessage.unlock();

#endif // USE_PUBSUB
    }
}