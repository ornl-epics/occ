#include "AdaraPlugin.h"

#include <poll.h>
#include <osiSock.h>
#include <string.h> // memcpy

#include <iostream>
using namespace std;

EPICS_REGISTER_PLUGIN(AdaraPlugin, 3, "port name", string, "dispatcher port", string, "blocking callbacks", int);

#define NUM_ADARAPLUGIN_PARAMS      0

#define ADARA_MAX_PACKET_SIZE       0x3000
#define ADARA_HEADER_SIZE           (4*sizeof(int))
#define ADARA_CODE_DAS_DATA         0x00000000
#define ADARA_CODE_DAS_RTDL         0x00000100
#define ADARA_CODE_HEARTBEAT        0x00400900

AdaraPlugin::AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BaseSocketPlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_ADARAPLUGIN_PARAMS)
    , m_nTransmitted(0)
    , m_nProcessed(0)
    , m_nReceived(0)
{
    m_lastSentTimestamp = { 0, 0 };
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
    bool clientConnected = connectClient();

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_nReceived++;

        // Don't even bother with packet inspection if there's noone interested
        if (!clientConnected)
            continue;

        if (packet->isRtdl()) {
            uint32_t len = 2*sizeof(uint32_t)+32*sizeof(uint32_t);
            memset(outpacket, 0, len);
            outpacket[0] = 30*sizeof(uint32_t);
            outpacket[1] = ADARA_CODE_DAS_RTDL;
            // The RTDL packet contents is just what ADARA expects.
            // Copy what we have, 32 words at most, leave the rest as 0.
            memcpy(&outpacket[2], packet->payload, sizeof(uint32_t)*min(packet->payload_length, static_cast<uint32_t>(32)));

            if (send(outpacket, len)) {
                m_nTransmitted++;
                epicsTimeGetCurrent(&m_lastSentTimestamp);
            }
            m_nProcessed++;

        } else if (packet->isData()) {
            const DasPacket::RtdlHeader *rtdl = packet->getRtdlHeader();
            if (rtdl != 0) {
                uint32_t eventsCount;
                const DasPacket::Event *events = packet->getEventData(&eventsCount);

                outpacket[0] = 24 + sizeof(DasPacket::Event)*eventsCount;
                outpacket[1] = ADARA_CODE_DAS_DATA;
                outpacket[2] = rtdl->timestamp_sec;
                outpacket[3] = rtdl->timestamp_nsec;
                outpacket[4] = packet->source;
                // Based on ADARA System Architecture 3.4.1 I think it should be like this
                outpacket[5] = (packet->datainfo.subpacket_end << 31) +
                               ((packet->datainfo.subpacket_count & 0x7FFF) << 15) +
                               (m_nTransmitted+1) % 0xFFFF;
                // but legacy code from dcomserver looks like this - go with that for now
                outpacket[5] = (packet->info & 0x3) | ((packet->info << 8) & 0xFFFF0000);
                outpacket[6] = rtdl->charge;
                outpacket[7] = rtdl->general_info;
                outpacket[8] = rtdl->tsync_width;
                outpacket[9] = rtdl->tsync_delay;
                memcpy(&outpacket[10], events, 8*eventsCount); // help compiler optimize for 64bit copies

                if (send(outpacket, sizeof(int)*10 + sizeof(DasPacket::Event)*eventsCount)) {
                    m_nTransmitted++;
                    epicsTimeGetCurrent(&m_lastSentTimestamp);
                }
                m_nProcessed++;
            }
        }
    }

    // Update parameters
    setIntegerParam(TxCount,    m_nTransmitted);
    setIntegerParam(ProcCount,  m_nProcessed);
    setIntegerParam(RxCount,    m_nReceived);
    callParamCallbacks();
}

float AdaraPlugin::checkClient()
{
    int heartbeatInt;
    epicsTimeStamp now;

    getIntegerParam(CheckClientDelay, &heartbeatInt);
    epicsTimeGetCurrent(&now);

    if (isClientConnected() && epicsTimeDiffInSeconds(&now, &m_lastSentTimestamp) > heartbeatInt) {
        uint32_t outpacket[4];
        epicsTimeStamp ts;
        getTimeStamp(&ts);

        outpacket[0] = 0;
        outpacket[1] = ADARA_CODE_HEARTBEAT;
        outpacket[2] = ts.secPastEpoch;
        outpacket[3] = ts.nsec;

        // If sending fails, send() will automatically close the socket
        (void)send(outpacket, sizeof(outpacket));
    }
    return BaseSocketPlugin::checkClient();
}
