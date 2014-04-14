#include "AdaraPlugin.h"

#include <poll.h>
#include <osiSock.h>
#include <string.h> // memcpy

#include <iostream>
using namespace std;

EPICS_REGISTER_PLUGIN(AdaraPlugin, 3, "port name", string, "dispatcher port", string, "blocking callbacks", int);

#define NUM_ADARAPLUGIN_PARAMS      ((int)(&LAST_ADARAPLUGIN_PARAM - &FIRST_ADARAPLUGIN_PARAM + 1))
#define DEFAULT_LISTEN_IP_PORT      5656

#define ADARA_MAX_PACKET_SIZE       0x3000
#define ADARA_HEADER_SIZE           (4*sizeof(int))
#define ADARA_CODE_DAS_DATA         0x00000000
#define ADARA_CODE_DAS_RTDL         0x00000100

AdaraPlugin::AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_ADARAPLUGIN_PARAMS, 1,
                 asynOctetMask, asynOctetMask | asynInt32Mask)
    , m_listenSock(-1)
    , m_clientSock(-1)
    , m_nTransmitted(0)
    , m_nProcessed(0)
    , m_nReceived(0)
{
    createParam("LISTEN_IP",            asynParamOctet,     &ListenIP);
    createParam("LISTEN_PORT",          asynParamInt32,     &ListenPort);
    createParam("CLIENT_IP",            asynParamOctet,     &ClientIP);
    createParam("TRANSMITTED_COUNT",    asynParamInt32,     &TransmittedCount);

    setStringParam(ListenIP,            "");
    setStringParam(ClientIP,            "");
    setIntegerParam(ListenPort,         DEFAULT_LISTEN_IP_PORT);
    setIntegerParam(TransmittedCount,   m_nTransmitted);
    setIntegerParam(ProcessedCount,     m_nProcessed);
    setIntegerParam(ReceivedCount,      m_nReceived);
}

AdaraPlugin::~AdaraPlugin()
{
}

void AdaraPlugin::processData(const DasPacketList * const packetList)
{
    uint32_t outpacket[ADARA_MAX_PACKET_SIZE];

    // Do we need to connect the client? There's no extra thread that would wait
    // for client, instead we rely on the incoming data rate to trigger this function
    // quite often.
    if (m_clientSock == -1) {
        (void)connectClient();
    }

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_nReceived++;

        // Don't even bother with packet inspection if there's noone interested
        if (m_clientSock == -1)
            continue;

        if (packet->isRtdl()) {
            uint32_t len = 2*sizeof(uint32_t)+32*sizeof(uint32_t);
            memset(outpacket, 0, len);
            outpacket[0] = 30*sizeof(uint32_t);
            outpacket[1] = ADARA_CODE_DAS_RTDL;
            // The RTDL packet contents is just what ADARA expects.
            // Copy what we have, 32 words at most, leave the rest as 0.
            memcpy(&outpacket[2], packet->data, sizeof(uint32_t)*min(packet->payload_length, static_cast<uint32_t>(32)));

            if (send(outpacket, len))
                m_nTransmitted++;
            m_nProcessed++;

        } else if (packet->isData()) {
            const DasPacket::RtdlHeader *rtdl = packet->getRtdlHeader();
            if (rtdl != 0) {
                uint32_t neutronsCount;
                const DasPacket::NeutronEvent *neutrons = packet->getNeutronData(&neutronsCount);

                outpacket[0] = 24 + sizeof(DasPacket::NeutronEvent)*neutronsCount;
                outpacket[1] = ADARA_CODE_DAS_DATA;
                outpacket[2] = rtdl->timestamp_high;
                outpacket[3] = rtdl->timestamp_low;
                outpacket[4] = packet->source;
                // Based on ADARA System Architecture 3.4.1 I think it should be like this
                outpacket[5] = (packet->datainfo.subpacket_end << 31) +
                               ((packet->datainfo.subpacket_count & 0x7FFF) << 15) +
                               (m_nTransmitted+1) % 0xFFFF;
                // but legacy code from dcomserver looks like this - go with that for now
                outpacket[5] = packet->info & 0x3 | ((packet->info << 8) & 0xFFFF0000);
                outpacket[6] = rtdl->charge;
                outpacket[7] = rtdl->general_info;
                outpacket[8] = rtdl->tsync_width;
                outpacket[9] = rtdl->tsync_delay;
                memcpy(&outpacket[10], neutrons, 8*neutronsCount); // help compiler optimize for 64bit copies

                if (send(outpacket, sizeof(int)*10 + sizeof(DasPacket::NeutronEvent)*neutronsCount))
                    m_nTransmitted++;
                m_nProcessed++;
            }
        }
    }

    // Update parameters
    setIntegerParam(TransmittedCount,   m_nTransmitted);
    setIntegerParam(ProcessedCount,     m_nProcessed);
    setIntegerParam(ReceivedCount,      m_nReceived);
    callParamCallbacks();
}

bool AdaraPlugin::send(const uint32_t *data, uint32_t length)
{
    const char *rest = reinterpret_cast<const char *>(data);
    while (m_clientSock != -1 && length > 0) {
        ssize_t sent = write(m_clientSock, rest, length);
        if (sent == -1) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "AdaraPlugin::%s(%s) Closed socket due to an write error - %s\n",
                      __func__, portName, strerror(errno));
            disconnectClient();
        }
        rest += sent;
        length -= sent;
    }

    return (length == 0);
}

asynStatus AdaraPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    asynStatus status;

    if (pasynUser->reason == ListenPort) {
        uint16_t port = value;
        char host[256];

        if (value < 0 || value > 0xFFFF)
            return asynError;

        status = getStringParam(ListenIP, sizeof(host), host);
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "AdaraPlugin::%s(%s) Error getting ListenIP parameter - %d\n",
                      __func__, portName, status);
            return status;
        }

        if (!setupListeningSocket(host, port))
            return asynError;
    }

    return BasePlugin::writeInt32(pasynUser, value);
}

asynStatus AdaraPlugin::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
    asynStatus status;

    if (pasynUser->reason == ListenIP) {
        int port;
        string host(value, nChars);

        status = setStringParam(ListenIP, value);
        if (status != asynSuccess) {
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "AdaraPlugin::%s(%s) Error setting ListenIP parameter\n",
                      __func__, portName);
            return status;
        }
        *nActual = nChars;

        status = getIntegerParam(ListenPort, &port);
        if (status != asynSuccess) {
            port = DEFAULT_LISTEN_IP_PORT;
            asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                      "AdaraPlugin::%s(%s) Error getting ListenPort parameter, using default value %d\n",
                      __func__, portName, port);
            return status;
        }

        callParamCallbacks();

        if (!setupListeningSocket(host.c_str(), port))
            return asynError;
    }
    return status;
}

bool AdaraPlugin::setupListeningSocket(const string &host, uint16_t port)
{
    struct sockaddr_in sockaddr;
    int sock;
    int optval;

    if (aToIPAddr(host.c_str(), DEFAULT_LISTEN_IP_PORT, &sockaddr) != 0) {
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "AdaraPlugin::%s(%s) Cannot resolve host '%s' to IP address\n",
                  __func__, portName, host.c_str());
        return false;
    }
    sockaddr.sin_port = htons(port);

    sock = epicsSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        char sockErrBuf[64];
        epicsSocketConvertErrnoToString(sockErrBuf, sizeof(sockErrBuf));
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "AdaraPlugin::%s(%s) failed to create stream socket - %s\n",
                  __func__, portName, sockErrBuf);
        return false;
    }

    optval = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) != 0) {
        char sockErrBuf[64];
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "AdaraPlugin::%s(%s) failed to set socket parameters - %s\n",
                  __func__, portName, sockErrBuf);
        close(sock);
        return false;
    }

    if (::bind(sock, (struct sockaddr *)&sockaddr, sizeof(struct sockaddr)) != 0) {
        char sockErrBuf[64];
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "AdaraPlugin::%s(%s) failed to bind to socket - %s\n",
                  __func__, portName, sockErrBuf);
        close(sock);
        return false;
    }

    if (listen(sock, 1) != 0) {
        char sockErrBuf[64];
        asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR,
                  "AdaraPlugin::%s(%s) failed to listen to socket - %s\n",
                  __func__, portName, sockErrBuf);
        close(sock);
        return false;
    }

    this->lock();

    if (m_listenSock != -1)
        close(m_listenSock);
    m_listenSock = sock;

    disconnectClient();

    this->unlock();

    return true;
}

bool AdaraPlugin::connectClient()
{
    char clientip[128];
    struct pollfd fds;

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
    return true;
}

void AdaraPlugin::disconnectClient()
{
    if (m_clientSock != -1)
        close(m_clientSock);
    m_clientSock = -1;
    setStringParam(ClientIP, "");
    callParamCallbacks();
}
