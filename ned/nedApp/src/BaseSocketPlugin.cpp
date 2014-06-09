#include "BaseSocketPlugin.h"
#include "Log.h"

#include <poll.h>
#include <osiSock.h>
#include <string.h> // strerror

#define NUM_BASESOCKETPLUGIN_PARAMS     ((int)(&LAST_BASESOCKETPLUGIN_PARAM - &FIRST_BASESOCKETPLUGIN_PARAM + 1))
#define DEFAULT_CHECKCLIENT_DELAY       2

BaseSocketPlugin::BaseSocketPlugin(const char *portName, const char *dispatcherPortName, int blocking,
                           int numParams, int maxAddr, int interfaceMask, int interruptMask,
                           int asynFlags, int autoConnect, int priority, int stackSize)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_BASESOCKETPLUGIN_PARAMS + numParams,
                 maxAddr, interfaceMask, interruptMask, asynFlags, autoConnect, priority, stackSize)
    , m_listenSock(-1)
    , m_clientSock(-1)
{
    createParam("ListenIp",     asynParamOctet,     &ListenIP);
    createParam("ListenPort",   asynParamInt32,     &ListenPort);
    createParam("ClientIp",     asynParamOctet,     &ClientIP);
    createParam("TxCount",      asynParamInt32,     &TxCount);
    createParam("CheckClientDel",asynParamInt32,    &CheckClientDelay);

    setStringParam(ClientIP,        "");
    setIntegerParam(TxCount,        0);
    setIntegerParam(CheckClientDelay,  DEFAULT_CHECKCLIENT_DELAY);

    callParamCallbacks();

    std::function<float(void)> watchdogCb = std::bind(&BaseSocketPlugin::checkClient, this);
    scheduleCallback(watchdogCb, DEFAULT_CHECKCLIENT_DELAY);
}

BaseSocketPlugin::~BaseSocketPlugin()
{
}

bool BaseSocketPlugin::send(const uint32_t *data, uint32_t length)
{
    const char *rest = reinterpret_cast<const char *>(data);
    while (m_clientSock != -1 && length > 0) {
        ssize_t sent = write(m_clientSock, rest, length);
        if (sent == -1) {
            LOG_ERROR("Closed socket due to an write error - %s", strerror(errno));
            disconnectClient();
        }
        rest += sent;
        length -= sent;
    }

    return (length == 0);
}

asynStatus BaseSocketPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    asynStatus status;

    if (pasynUser->reason == ListenPort) {
        int prevValue;
        char host[256];

        if (value <= 0 || value > 0xFFFF)
            return asynError;

        status = getIntegerParam(ListenPort, &prevValue);
        if (status == asynSuccess && prevValue == value)
            return asynSuccess;

        status = setIntegerParam(ListenPort, value);
        if (status != asynSuccess) {
            LOG_ERROR("Error setting ListenPort parameter");
            return status;
        }

        status = getStringParam(ListenIP, sizeof(host), host);
        if (status != asynSuccess) {
            LOG_DEBUG("ListenIP not configured, skip configuring listen socket");
            return asynSuccess;
        }

        callParamCallbacks();

        if (!setupListeningSocket(host, value))
            return asynError;

        LOG_INFO("Listening on %s:%d", host, value);
        return asynSuccess;
    } else if (pasynUser->reason == CheckClientDelay) {
        // If we were to add support for canceling the timer through value 0 here,
        // we'd have to implement canceling old timer and scheduling new one.
        if (value < 1)
            value = 1;
        setIntegerParam(CheckClientDelay, value);
        callParamCallbacks();
        return asynSuccess;
    }

    return BasePlugin::writeInt32(pasynUser, value);
}

asynStatus BaseSocketPlugin::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
    asynStatus status = asynError;

    if (pasynUser->reason == ListenIP) {
        int port;
        char prevValue[128];
        std::string host(value, nChars);

        status = getStringParam(ListenIP, sizeof(prevValue), prevValue);
        if (status == asynSuccess && strncmp(prevValue, value, sizeof(prevValue)) == 0)
            return asynSuccess;

        status = setStringParam(ListenIP, value);
        if (status != asynSuccess) {
            LOG_ERROR("Error setting ListenIP parameter");
            return status;
        }
        *nActual = nChars;

        status = getIntegerParam(ListenPort, &port);
        if (status != asynSuccess) {
            LOG_DEBUG("ListenIP not configured, skip configuring listen socket");
            return asynSuccess;
        }

        callParamCallbacks();

        if (!setupListeningSocket(host.c_str(), port))
            return asynError;

        LOG_INFO("Listening on %s:%u", host.c_str(), port);
        return asynSuccess;
    }
    return status;
}

bool BaseSocketPlugin::setupListeningSocket(const std::string &host, uint16_t port)
{
    struct sockaddr_in sockaddr;
    int sock;
    int optval;

    if (aToIPAddr(host.c_str(), 0, &sockaddr) != 0) {
        LOG_ERROR("Cannot resolve host '%s' to IP address", host.c_str());
        return false;
    }
    sockaddr.sin_port = htons(port);

    sock = epicsSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString(sockErrBuf, sizeof(sockErrBuf));
        LOG_ERROR("Failed to create stream socket - %s", sockErrBuf);
        return false;
    }

    optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) != 0) {
        LOG_ERROR("Failed to set socket parameters - %s", strerror(errno));
        close(sock);
        return false;
    }

    if (::bind(sock, (struct sockaddr *)&sockaddr, sizeof(struct sockaddr)) != 0) {
        LOG_ERROR("Failed to bind to socket - %s", strerror(errno));
        close(sock);
        return false;
    }

    if (listen(sock, 1) != 0) {
        LOG_ERROR("Failed to listen to socket - %s", strerror(errno));
        close(sock);
        return false;
    }

    if (m_listenSock != -1)
        close(m_listenSock);
    m_listenSock = sock;

    return true;
}

bool BaseSocketPlugin::isClientConnected()
{
    return (m_clientSock != -1);
}

bool BaseSocketPlugin::connectClient()
{
    char clientip[128];
    struct pollfd fds;

    if (m_clientSock != -1)
        return true;

    fds.fd = m_listenSock; // POSIX allows negative values, revents becomes 0
    fds.events = POLLIN;
    fds.revents = 0;

    // Non-blocking check
    if (poll(&fds, 1, 0) == 0 || fds.revents != POLLIN)
        return false;

    // There should be client waiting now - accept() won't block
    struct sockaddr client;
    socklen_t len = sizeof(struct sockaddr);
    m_clientSock = accept(m_listenSock, &client, &len);
    if (m_clientSock == -1)
        return false;

    sockAddrToDottedIP(&client, clientip, sizeof(clientip));
    setStringParam(ClientIP, clientip);
    callParamCallbacks();

    LOG_INFO("New TCP client from %s", clientip);

    return true;
}

void BaseSocketPlugin::disconnectClient()
{
    if (m_clientSock != -1)
        close(m_clientSock);
    m_clientSock = -1;
    setStringParam(ClientIP, "");
    callParamCallbacks();
}

float BaseSocketPlugin::checkClient()
{
    if (!isClientConnected()) {
	    connectClient();
    }

    int delay;
    getIntegerParam(CheckClientDelay, &delay);
    return delay;
}
