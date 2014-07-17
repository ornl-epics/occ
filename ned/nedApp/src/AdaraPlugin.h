#ifndef ADARA_PLUGIN_H
#define ADARA_PLUGIN_H

#include "BaseSocketPlugin.h"

#include <vector>

#define ADARA_MAX_NUM_DSPS  10  //!< Maximum number of DSPs supported by a single AdaraPlugin instance

/**
 * Plugin that forwards Neutron Event data to ADARA SMS over TCP/IP.
 *
 * The plugin registers to receive all packets, but will only process and
 * forward neutron event data packets which have RTDL header attached and
 * the RTDL packets.
 *
 * After setting at least LISTEN_ADDR parameter, the plugin will start listening
 * for incoming TCP/IP connections. LISTEN_ADDR and LISTEN_PORT can be changed
 * at any time, which will reconfigure listening socket and also disconnect
 * ADARA SMS client, if connected.
 *
 * When enabled, processing of packets will only occur if there's a client
 * which accepts data. In this case, the Neutron Event data packets and RTDL
 * packets are transformed into ADARA format and sent over socket, one packet
 * at a time. Client socket is disconnected on any error. ADARA is supposed
 * to reconnect immediately.
 *
 * AdaraPlugin provides following asyn parameters:
 * asyn param    | asyn param type | init val | mode | Description
 * ------------- | --------------- | -------- | ---- | -----------
 * BadPulseDrops | asynParamInt32  | 0        | RO   | Num dropped packets associated to already completed pulse
 * BadDspDrops   | asynParamInt32  | 0        | RO   | Num dropped packets from unexpected DSP
 */
class AdaraPlugin : public BaseSocketPlugin {
    private:
        uint64_t m_nTransmitted;    //!< Number of packets sent to BASESOCKET
        uint64_t m_nProcessed;      //!< Number of processed packets
        uint64_t m_nReceived;       //!< Number of packets received from dispatcher
        uint64_t m_nUnexpectedDspDrops;     //!< Number of packets dropped because originating from unexpected DSP
        uint64_t m_nPacketsPrevPulse;       //!< Number of dropped packets associated to already complete pulse
        epicsTimeStamp m_lastSentTimestamp; //!< Timestamp of last packet sent to Adara
        epicsTimeStamp m_lastRtdlTimestamp; //!< Timestamp of last RTDL packet sent to Adara

        /**
         * Structure describing output packets sequence for a given source.
         */
        struct SourceSequence {
            uint32_t sourceId;          //!< Source id for output packets
            DasPacket::RtdlHeader rtdl; //!< RTDL header of the current pulse
            uint32_t pulseSeq;          //!< Packet sequence number within one pulse
            uint32_t totalSeq;          //!< Overall packet sequence number
            SourceSequence(uint32_t sourceId_)
                : sourceId(sourceId_)
                , pulseSeq(0)
                , totalSeq(0)
            {
                rtdl.timestamp_sec  = 0;
                rtdl.timestamp_nsec = 0;
            }
        };

        /**
         * Structure for mapping DSP ids into SMS expected source ids
         *
         * We split neutron and metadata from a given DSP into two streams and
         * tag each of them with source ID which is unique within one SMS
         * connection.
         *
         * The expected number of DSPs is low, usually 1. std::vector is more
         * efficient than std::map.
         */
        struct DspSource {
            uint32_t dspId;
            SourceSequence neutronSeq;
            SourceSequence metadataSeq;
            DspSource(uint32_t dspId_, uint32_t nSrcId, uint32_t mSrcId)
                : dspId(dspId_)
                , neutronSeq(nSrcId)
                , metadataSeq(mSrcId)
            {}
        };
        std::vector<DspSource> m_dspSources;

    public:
        /**
         * Constructor
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] blocking Should processing of callbacks block execution of caller thread or not.
	     * @param[in] numDsps Number of DSPs that will be sending data
         */
        AdaraPlugin(const char *portName, const char *dispatcherPortName, int blocking, int numDsps);

        /**
         * Destructor
         */
        ~AdaraPlugin();

        /**
         * Process RTDL and NeutronData packets only, skip rest.
         *
         * @param[in] packetList List of packets to be processed
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Overloaded periodic function to send ADARA Heartbeat packet.
         *
         * Send Heartbeat packet and call base BaseSocketPlugin::checkClient()
         * function for timer upkeep.
         *
         * @return Number returned from BaseSocketPlugin::checkClient()
         */
        float checkClient();

        /**
         * Overloaded signal function invoked after new client has been connected.
         */
        void clientConnected();

    private:
        /**
         * Find the SourceSequence structure for a given DSP id and data type.
         *
         * The map is only partially populated at construct time. If DSP is not
         * found in the table and there's room, it will be added to the map.
         */
        SourceSequence* findSourceSequence(uint32_t dspId, bool isNeutron);

    protected:
        #define FIRST_ADARAPLUGIN_PARAM BadPulseDrops
        int BadPulseDrops;
        int BadDspDrops;
        #define LAST_ADARAPLUGIN_PARAM BadDspDrops
};

#endif // ADARA_PLUGIN_H
