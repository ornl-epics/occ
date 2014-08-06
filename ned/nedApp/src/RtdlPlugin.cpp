#include "RtdlPlugin.h"

#include <climits>

#define NUM_RTDLPLUGIN_PARAMS ((int)(&LAST_RTDLPLUGIN_PARAM - &FIRST_RTDLPLUGIN_PARAM + 1))

EPICS_REGISTER_PLUGIN(RtdlPlugin, 3, "Port name", string, "Dispatcher port name", string, "Blocking", int);

#define TIMESTAMP_FORMAT        "%Y/%m/%d %T.%09f"

RtdlPlugin::RtdlPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_RTDLPLUGIN_PARAMS, 1, asynOctetMask, asynOctetMask)
    , m_receivedCount(0)
    , m_processedCount(0)
{
    createParam("Timestamp",        asynParamOctet, &Timestamp);
    createParam("BadPulse",         asynParamInt32, &BadPulse);
    createParam("PulseFlavor",      asynParamInt32, &PulseFlavor);
    createParam("PulseCharge",      asynParamInt32, &PulseCharge);
    createParam("BadVetoFrame",     asynParamInt32, &BadVetoFrame);
    createParam("BadCycleFrame",    asynParamInt32, &BadCycleFrame);
    createParam("Tstat",            asynParamInt32, &Tstat);
    createParam("Veto",             asynParamInt32, &Veto);
    createParam("Cycle",            asynParamInt32, &Cycle);
    createParam("IntraPulseTime",   asynParamInt32, &IntraPulseTime);
    createParam("TofFullOffset",    asynParamInt32, &TofFullOffset);
    createParam("FrameOffset",      asynParamInt32, &FrameOffset);
    createParam("TofFixedOffset",   asynParamInt32, &TofFixedOffset);
}

void RtdlPlugin::processData(const DasPacketList * const packetList)
{
    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_receivedCount++;
        if (packet->isRtdl()) {
            const DasPacket::RtdlHeader *rtdl = reinterpret_cast<const DasPacket::RtdlHeader *>(packet->getPayload());
            epicsTimeStamp rtdlTime;
            char rtdlTimeStr[64];

            // Format time
            rtdlTime.secPastEpoch = rtdl->timestamp_sec;
            rtdlTime.nsec = rtdl->timestamp_nsec;
            epicsTimeToStrftime(rtdlTimeStr, sizeof(rtdlTimeStr), TIMESTAMP_FORMAT, &rtdlTime);

            setStringParam(Timestamp,           rtdlTimeStr);
            setIntegerParam(BadPulse,           rtdl->bad_pulse);
            setIntegerParam(PulseFlavor,        rtdl->pulse_flavor);
            setIntegerParam(PulseCharge,        rtdl->pulse_charge);
            setIntegerParam(BadVetoFrame,       rtdl->bad_veto_frame);
            setIntegerParam(BadCycleFrame,      rtdl->bad_cycle_frame);
            setIntegerParam(Tstat,              rtdl->tstat);
            setIntegerParam(Veto,               rtdl->veto);
            setIntegerParam(Cycle,              rtdl->cycle);
            setIntegerParam(IntraPulseTime,     rtdl->tsync_width * 100);
            setIntegerParam(TofFullOffset,      rtdl->tof_full_offset);
            setIntegerParam(FrameOffset,        rtdl->frame_offset);
            setIntegerParam(TofFixedOffset,     rtdl->tof_fixed_offset);

            m_processedCount++;
        }
    }

    setIntegerParam(RxCount, m_receivedCount % INT_MAX);
    setIntegerParam(ProcCount, m_processedCount % INT_MAX);

    callParamCallbacks();
}
