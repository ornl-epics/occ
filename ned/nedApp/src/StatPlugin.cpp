#include "StatPlugin.h"

#include <climits>

#define NUM_STATPLUGIN_PARAMS ((int)(&LAST_STATPLUGIN_PARAM - &FIRST_STATPLUGIN_PARAM + 1))

EPICS_REGISTER_PLUGIN(StatPlugin, 3, "Port name", string, "Dispatcher port name", string, "Blocking", int);

#define CALC_RATE_INTERVAL       1.0

StatPlugin::StatPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_STATPLUGIN_PARAMS, 1, asynOctetMask)
    , m_receivedCount(0)
    , m_receivedBytes(0)
    , m_cmdCount(0)
    , m_cmdBytes(0)
    , m_dataCount(0)
    , m_dataBytes(0)
    , m_metaCount(0)
    , m_metaBytes(0)
    , m_rtdlCount(0)
    , m_rtdlBytes(0)
    , m_badCount(0)
    , m_badBytes(0)
    , m_lastReceivedCount(0)
    , m_lastReceivedBytes(0)
    , m_lastCmdCount(0)
    , m_lastCmdBytes(0)
    , m_lastDataCount(0)
    , m_lastDataBytes(0)
    , m_lastMetaCount(0)
    , m_lastMetaBytes(0)
    , m_lastRtdlCount(0)
    , m_lastRtdlBytes(0)
    , m_lastBadCount(0)
    , m_lastBadBytes(0)
{
    createParam("RxCountRate",   asynParamInt32, &RxCountRate);
    createParam("RxByteRate",    asynParamInt32, &RxByteRate);
    createParam("CmdCount",      asynParamInt32, &CmdCount);
    createParam("CmdCountRate",  asynParamInt32, &CmdCountRate);
    createParam("CmdByteRate",   asynParamInt32, &CmdByteRate);
    createParam("DataCount",     asynParamInt32, &DataCount);
    createParam("DataCountRate", asynParamInt32, &DataCountRate);
    createParam("DataByteRate",  asynParamInt32, &DataByteRate);
    createParam("MetaCount",     asynParamInt32, &MetaCount);
    createParam("MetaCountRate", asynParamInt32, &MetaCountRate);
    createParam("MetaByteRate",  asynParamInt32, &MetaByteRate);
    createParam("RtdlCount",     asynParamInt32, &RtdlCount);
    createParam("RtdlCountRate", asynParamInt32, &RtdlCountRate);
    createParam("RtdlByteRate",  asynParamInt32, &RtdlByteRate);
    createParam("TsyncCount",    asynParamInt32, &TsyncCount);
    createParam("TsyncCountRate",asynParamInt32, &TsyncCountRate);
    createParam("TsyncByteRate", asynParamInt32, &TsyncByteRate);
    createParam("BadCount",      asynParamInt32, &BadCount);
    createParam("BadCountRate",  asynParamInt32, &BadCountRate);
    createParam("BadByteRate",   asynParamInt32, &BadByteRate);

    setIntegerParam(ProcCount,      0);
    setIntegerParam(RxCount,        0);
    setIntegerParam(RxCountRate,    0);
    setIntegerParam(RxByteRate,     0);
    setIntegerParam(CmdCount,       0);
    setIntegerParam(CmdCountRate,   0);
    setIntegerParam(CmdByteRate,    0);
    setIntegerParam(DataCount,      0);
    setIntegerParam(DataCountRate,  0);
    setIntegerParam(DataByteRate,   0);
    setIntegerParam(MetaCount,      0);
    setIntegerParam(MetaCountRate,  0);
    setIntegerParam(MetaByteRate,   0);
    setIntegerParam(RtdlCount,      0);
    setIntegerParam(RtdlCountRate,  0);
    setIntegerParam(RtdlByteRate,   0);
    setIntegerParam(TsyncCount,     0);
    setIntegerParam(TsyncCountRate, 0);
    setIntegerParam(TsyncByteRate,  0);
    setIntegerParam(BadCount,       0);
    setIntegerParam(BadCountRate,   0);
    setIntegerParam(BadByteRate,    0);

    callParamCallbacks();

    epicsTimeGetCurrent(&m_lastTime);

    std::function<float(void)> rateCalc = std::bind(&StatPlugin::calculateRate, this);
    scheduleCallback(rateCalc, CALC_RATE_INTERVAL);
}

void StatPlugin::processData(const DasPacketList * const packetList)
{
    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        m_receivedCount++;
        m_receivedBytes += packet->length();
        if (packet->isResponse()) {
            m_cmdCount++;
            m_cmdBytes += packet->length();
        } else if (packet->isNeutronData()) {
            m_dataCount++;
            m_dataBytes += packet->length();
        } else if (packet->isMetaData()) {
            m_metaCount++;
            m_metaBytes += packet->length();
        } else if (packet->isRtdl()) {
            m_rtdlCount++;
            m_rtdlBytes += packet->length();
        } else if (packet->cmdinfo.is_command && packet->cmdinfo.command == DasPacket::CMD_TSYNC) {
            m_tsyncCount++;
            m_tsyncBytes += packet->length();
        } else if (packet->isBadPacket()) {
            m_badCount++;
            m_badBytes += packet->length();
        }
    }

    setIntegerParam(RxCount,    m_receivedCount % INT_MAX);
    setIntegerParam(ProcCount,  m_receivedCount % INT_MAX);
    setIntegerParam(CmdCount,   m_cmdCount % INT_MAX);
    setIntegerParam(DataCount,  m_dataCount % INT_MAX);
    setIntegerParam(MetaCount,  m_metaCount % INT_MAX);
    setIntegerParam(RtdlCount,  m_rtdlCount % INT_MAX);
    setIntegerParam(TsyncCount, m_tsyncCount % INT_MAX);
    setIntegerParam(BadCount,   m_badCount % INT_MAX);

    callParamCallbacks();
}

float StatPlugin::calculateRate()
{
    double receivedCountRate, cmdCountRate, dataCountRate, metaCountRate, rtdlCountRate, badCountRate, tsyncCountRate;
    double receivedByteRate, cmdByteRate, dataByteRate, metaByteRate, rtdlByteRate, badByteRate, tsyncByteRate;

    epicsTimeStamp now;
    epicsTimeGetCurrent(&now);

    double runtime = epicsTimeDiffInSeconds(&now, &m_lastTime);
    if (runtime > 0.0) {
        m_lastTime = now;

        // Handles single rollover
        receivedCountRate = (m_receivedCount - m_lastReceivedCount) / runtime;
        cmdCountRate      = (m_cmdCount      - m_lastCmdCount)      / runtime;
        dataCountRate     = (m_dataCount     - m_lastDataCount)     / runtime;
        metaCountRate     = (m_metaCount     - m_lastMetaCount)     / runtime;
        rtdlCountRate     = (m_rtdlCount     - m_lastRtdlCount)     / runtime;
        tsyncCountRate    = (m_tsyncCount    - m_lastTsyncCount)    / runtime;
        badCountRate      = (m_badCount      - m_lastBadCount)      / runtime;
        receivedByteRate  = (m_receivedBytes - m_lastReceivedBytes) / runtime;
        cmdByteRate       = (m_cmdBytes      - m_lastCmdBytes)      / runtime;
        dataByteRate      = (m_dataBytes     - m_lastDataBytes)     / runtime;
        metaByteRate      = (m_metaBytes     - m_lastMetaBytes)     / runtime;
        rtdlByteRate      = (m_rtdlBytes     - m_lastRtdlBytes)     / runtime;
        tsyncByteRate     = (m_tsyncBytes    - m_lastTsyncBytes)    / runtime;
        badByteRate       = (m_badBytes      - m_lastBadBytes)      / runtime;

        setIntegerParam(RxCountRate,    receivedCountRate);
        setIntegerParam(CmdCountRate,   cmdCountRate);
        setIntegerParam(DataCountRate,  dataCountRate);
        setIntegerParam(MetaCountRate,  metaCountRate);
        setIntegerParam(RtdlCountRate,  rtdlCountRate);
        setIntegerParam(TsyncCountRate, tsyncCountRate);
        setIntegerParam(BadCountRate,   badCountRate);
        setIntegerParam(RxByteRate,     receivedByteRate);
        setIntegerParam(CmdByteRate,    cmdByteRate);
        setIntegerParam(DataByteRate,   dataByteRate);
        setIntegerParam(MetaByteRate,   metaByteRate);
        setIntegerParam(RtdlByteRate,   rtdlByteRate);
        setIntegerParam(TsyncByteRate,  tsyncByteRate);
        setIntegerParam(BadByteRate,    badByteRate);

        callParamCallbacks();

        m_lastReceivedCount = m_receivedCount;
        m_lastCmdCount      = m_cmdCount;
        m_lastDataCount     = m_dataCount;
        m_lastMetaCount     = m_metaCount;
        m_lastRtdlCount     = m_rtdlCount;
        m_lastTsyncCount    = m_tsyncCount;
        m_lastBadCount      = m_badCount;
        m_lastReceivedBytes = m_receivedBytes;
        m_lastCmdBytes      = m_cmdBytes;
        m_lastDataBytes     = m_dataBytes;
        m_lastMetaBytes     = m_metaBytes;
        m_lastRtdlBytes     = m_rtdlBytes;
        m_lastTsyncBytes    = m_tsyncBytes;
        m_lastBadBytes      = m_badBytes;
    }

    return CALC_RATE_INTERVAL;
}
