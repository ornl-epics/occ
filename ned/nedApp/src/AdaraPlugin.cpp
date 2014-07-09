#include "AdaraPlugin.h"

#include <poll.h>
#include <osiSock.h>
#include <string.h> // memcpy

#include <iostream>
using namespace std;

EPICS_REGISTER_PLUGIN(AdaraPlugin, 5, "port name", string, "dispatcher port", string, "blocking callbacks", int, "Neutron source id", int, "Meta source id", int);

#define NUM_ADARAPLUGIN_PARAMS      0

#define ADARA_CODE_DAS_DATA         0x00000000
#define ADARA_CODE_DAS_RTDL         0x00000100
#define ADARA_CODE_SOURCE_LIST      0x00000200
#define ADARA_CODE_HEARTBEAT        0x00400900

AdaraPlugin::AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking, int neutronSource, int metaSource)
    : BaseSocketPlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_ADARAPLUGIN_PARAMS)
    , m_nTransmitted(0)
    , m_nProcessed(0)
    , m_nReceived(0)
    , m_neutronSeq(neutronSource)
    , m_metadataSeq(metaSource)
{
    m_lastSentTimestamp = { 0, 0 };
}

AdaraPlugin::~AdaraPlugin()
{
}

void AdaraPlugin::processData(const DasPacketList * const packetList)
{
    uint32_t outpacket[10];

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_nReceived++;

        // Don't even bother with packet inspection if there's noone interested
        if (connectClient() == false)
            break;

        if (packet->isRtdl()) {
            const DasPacket::RtdlHeader *rtdl = packet->getRtdlHeader();
            epicsTimeStamp timestamp = { 0, 0 };
            if (rtdl != 0)
                timestamp = { rtdl->timestamp_sec, rtdl->timestamp_nsec };

            // Pass a single RTDL packet for a given timestamp. DSP in most cases
            // sends two almost identical RTDL packets which differ only in source id
            // and packet/data flag.
            if (epicsTimeEqual(&timestamp, &m_lastRtdlTimestamp) == 0) { // time not equal
                outpacket[0] = 30*sizeof(uint32_t);
                outpacket[1] = ADARA_CODE_DAS_RTDL;

                // The RTDL packet contents is just what ADARA expects.
                // Prefix that with the length and type of packet.
                if (send(outpacket, sizeof(uint32_t)*2) &&
                    send(packet->payload, sizeof(uint32_t)*min(packet->getPayloadLength(), 32U))) {
                    m_nTransmitted++;
                    epicsTimeGetCurrent(&m_lastSentTimestamp);
                    m_lastRtdlTimestamp = timestamp;
                }
            }
            m_nProcessed++;

        } else if (packet->isData()) {
            const DasPacket::RtdlHeader *rtdl = packet->getRtdlHeader();
            if (rtdl != 0) {
                uint32_t eventsCount;
                const DasPacket::Event *events = packet->getEventData(&eventsCount);
                epicsTimeStamp currentTs = { rtdl->timestamp_sec, rtdl->timestamp_nsec };
                SourceSequence *seq = (packet->isNeutronData() ? &m_neutronSeq : &m_metadataSeq);
                epicsTimeStamp prevTs = { seq->rtdl.timestamp_sec, seq->rtdl.timestamp_nsec };

                if (epicsTimeEqual(&currentTs, &prevTs) == 0) {
                    // Transition in time detected. Inject a dummy packet with EOP set and no events
                    if (prevTs.secPastEpoch > 0 && prevTs.nsec > 0) {
                        outpacket[0] = 24;
                        outpacket[1] = ADARA_CODE_DAS_DATA;
                        outpacket[2] = seq->rtdl.timestamp_sec;
                        outpacket[3] = seq->rtdl.timestamp_nsec;
                        outpacket[4] = seq->sourceId;
                        outpacket[5] = (1 << 31) | ((seq->pulseSeq & 0x7FF) << 16) | ((seq->totalSeq++) & 0xFFFF);
                        outpacket[6] = seq->rtdl.charge;
                        outpacket[7] = seq->rtdl.general_info;
                        outpacket[8] = seq->rtdl.tsync_width;
                        outpacket[9] = seq->rtdl.tsync_delay;

                        (void)send(outpacket, sizeof(uint32_t)*10);
                    }
                    seq->pulseSeq = 0;
                    seq->rtdl = *rtdl; // Cache RTDL for the injected packet
                }

                outpacket[0] = 24 + sizeof(DasPacket::Event)*eventsCount;
                outpacket[1] = ADARA_CODE_DAS_DATA;
                outpacket[2] = rtdl->timestamp_sec;
                outpacket[3] = rtdl->timestamp_nsec;
                outpacket[4] = seq->sourceId;
                outpacket[5] = ((seq->pulseSeq++ & 0x7FF) << 16) + (seq->totalSeq++ & 0xFFFF);
                outpacket[6] = rtdl->charge;
                outpacket[7] = rtdl->general_info;
                outpacket[8] = rtdl->tsync_width;
                outpacket[9] = rtdl->tsync_delay;

                if (send(outpacket, sizeof(uint32_t)*10) &&
                    send(reinterpret_cast<const uint32_t*>(events), sizeof(DasPacket::Event)*eventsCount)) {
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

        outpacket[0] = 0;
        outpacket[1] = ADARA_CODE_HEARTBEAT;
        outpacket[2] = now.secPastEpoch;
        outpacket[3] = now.nsec;

        // If sending fails, send() will automatically close the socket
        (void)send(outpacket, sizeof(outpacket));
    }
    return BaseSocketPlugin::checkClient();
}

void AdaraPlugin::clientConnected()
{
    uint32_t outpacket[6];
    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);

    outpacket[0] = sizeof(uint32_t) * 2;
    outpacket[1] = ADARA_CODE_SOURCE_LIST;
    outpacket[2] = now.secPastEpoch;
    outpacket[3] = now.nsec;
    outpacket[4] = m_neutronSeq.sourceId;
    outpacket[5] = m_metadataSeq.sourceId;

    (void)send(outpacket, sizeof(outpacket));
}
