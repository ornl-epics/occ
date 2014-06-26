#ifndef STAT_PLUGIN_H
#define STAT_PLUGIN_H

#include "BasePlugin.h"

/**
 * Gather and display statistical information of the incoming data
 *
 * By default, StatPlugin will receive and process all incoming data.
 *
 * Available StatPlugin parameters (in addition to ones from BasePlugin):
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * RxCountRate   | asynParamInt32  | 0        | RO   | Total count rate
 * RxByteRate    | asynParamInt32  | 0        | RO   | Total byte rate
 * CmdCount      | asynParamInt32  | 0        | RO   | Commands packet count
 * CmdCountRate  | asynParamInt32  | 0        | RO   | Commands packet count rate
 * CmdByteRate   | asynParamInt32  | 0        | RO   | Commands byte rate
 * DataCount     | asynParamInt32  | 0        | RO   | Neutron data packet count
 * DataCountRate | asynParamInt32  | 0        | RO   | Neutron data packet count rate
 * DataByteRate  | asynParamInt32  | 0        | RO   | Neutron data byte rate
 * MetaCount     | asynParamInt32  | 0        | RO   | Meta data packet count
 * MetaCountRate | asynParamInt32  | 0        | RO   | Meta data packet count rate
 * MetaByteRate  | asynParamInt32  | 0        | RO   | Meta data byte rate
 * RtdlCount     | asynParamInt32  | 0        | RO   | RTDL packet count
 * RtdlCountRate | asynParamInt32  | 0        | RO   | RTDL packet count rate
 * RtdlByteRate  | asynParamInt32  | 0        | RO   | RTDL byte rate
 * BadCount      | asynParamInt32  | 0        | RO   | Bad packet count
 * BadCountRate  | asynParamInt32  | 0        | RO   | Bad packet count rate
 * BadByteRate   | asynParamInt32  | 0        | RO   | Bad byte rate
 */
class StatPlugin : public BasePlugin {
    public: // functions
        /**
         * Constructor
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        StatPlugin(const char *portName, const char *dispatcherPortName, int blocking);

        /**
         * Overloaded function to receive all OCC data.
         */
        void processData(const DasPacketList * const packetList);

    private:
        /**
         * Calculate count and byte rates since previous run.
         *
         * Called periodically based on its return value.
         *
         * @return Delay in seconds when to call the function again.
         */
        float calculateRate();

    private:
        uint64_t m_receivedCount;
        uint64_t m_receivedBytes;
        uint64_t m_cmdCount;
        uint64_t m_cmdBytes;
        uint64_t m_dataCount;
        uint64_t m_dataBytes;
        uint64_t m_metaCount;
        uint64_t m_metaBytes;
        uint64_t m_rtdlCount;
        uint64_t m_rtdlBytes;
        uint64_t m_badCount;
        uint64_t m_badBytes;
        uint64_t m_lastReceivedCount;
        uint64_t m_lastReceivedBytes;
        uint64_t m_lastCmdCount;
        uint64_t m_lastCmdBytes;
        uint64_t m_lastDataCount;
        uint64_t m_lastDataBytes;
        uint64_t m_lastMetaCount;
        uint64_t m_lastMetaBytes;
        uint64_t m_lastRtdlCount;
        uint64_t m_lastRtdlBytes;
        uint64_t m_lastBadCount;
        uint64_t m_lastBadBytes;
        epicsTimeStamp m_lastTime;

    private: // asyn parameters
        #define FIRST_STATPLUGIN_PARAM RxCountRate
        int RxCountRate;        //!< Total count rate in packets/second
        int RxByteRate;         //!< Total byte rate in bytes/second
        int CmdCount;           //!< Number of command response packets
        int CmdCountRate;       //!< Commands count rate in packets/second
        int CmdByteRate;        //!< Command byte rate in bytes/second
        int DataCount;          //!< Number of data packets
        int DataCountRate;      //!< Data count rate in packets/second
        int DataByteRate;       //!< Data byte rate in bytes/second
        int MetaCount;          //!< Number of data packets
        int MetaCountRate;      //!< Data count rate in packets/second
        int MetaByteRate;       //!< Data byte rate in bytes/second
        int RtdlCount;          //!< Number of RTDL packets
        int RtdlCountRate;      //!< RTDL count rate in packets/second
        int RtdlByteRate;       //!< RTDL byte rate in bytes/second
        int BadCount;           //!< Number of bad packets
        int BadCountRate;       //!< Bad packet count rate in packets/second
        int BadByteRate;        //!< Bad packet byte rate in bytes/second
        #define LAST_STATPLUGIN_PARAM BadByteRate
};

#endif // STAT_PLUGIN_H
