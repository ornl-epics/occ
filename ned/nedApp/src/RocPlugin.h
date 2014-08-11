#ifndef ROC_PLUGIN_H
#define ROC_PLUGIN_H

#include "BaseModulePlugin.h"

#include <list>

/**
 * Plugin for ROC module.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * HwDate        | asynOctet       | ""       | RO   | Hardware date as YYYY/MM/DD
 * HwVer         | asynParamInt32  | 0        | RO   | Hardware version
 * HwRev         | asynParamInt32  | 0        | RO   | Hardware revision
 * FwVer         | asynParamInt32  | 0        | RO   | Firmware version
 * FwRev         | asynParamInt32  | 0        | RO   | Firmware revision
 */
class RocPlugin : public BaseModulePlugin {
    public: // variables
        static const int defaultInterfaceMask = BaseModulePlugin::defaultInterfaceMask | asynOctetMask;
        static const int defaultInterruptMask = BaseModulePlugin::defaultInterruptMask | asynOctetMask;

    private: // structures and definitions
        static const unsigned NUM_ROCPLUGIN_DYNPARAMS;      //!< Maximum number of asyn parameters, including the status and configuration parameters
        static const unsigned NUM_CHANNELS = 8;             //!< Number of channels connected to ROC
        static const float    NO_RESPONSE_TIMEOUT;          //!< Timeout to wait for response from ROC, in seconds

    private: // variables
        std::string m_version;                              //!< Version string as passed to constructor
        std::list<char> m_hvRecvBuffer;                     //!< FIFO queue for data received from HV module but not yet processed
        epicsMutex m_hvRecvMutex;                           //!< Mutex protecting the FIFO

    public: // functions

        /**
         * Constructor for RocPlugin
         *
         * Constructor will create and populate PVs with default values.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] hardwareId Hardware ID of the ROC module, can be in IP format (xxx.xxx.xxx.xxx) or
         *                       in hex number string in big-endian byte order (0x15FACB2D equals to IP 21.250.203.45)
         * @param[in] version ROC HW&SW version, ie. V5_50
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        RocPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking=0);

        /**
         * Process RS232 packets only, let base implementation do the rest.
         */
        bool processResponse(const DasPacket *packet);

        /**
         * Send string/byte data to PVs
         */
        asynStatus readOctet(asynUser *pasynUser, char *value, size_t nChars, size_t *nActual, int *eomReason);

        /**
         * Receive string/byte data to PVs
         */
        asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);

        /**
         * Try to parse the ROC version response packet an populate the structure.
         *
         * Function will parse all known ROC version responses and populate the
         * version structure. If the function returns false, it does not recognize
         * the response.
         *
         * All ROC boards except for v5.4 have the same response. v5.4 adds an extra
         * vendor field which the function disregards.
         *
         * When expectedLen parameter is non-zero, the function will only accept
         * the response that matches the size. This is useful when the version
         * is known in advance and this function can be used to verify that returned
         * version matches configured one. If the parsed version length doesn't match
         * the expected length, funtion returns false.
         *
         * @param[in] packet to be parsed
         * @param[out] version structure to be populated
         * @param[in] expectedLen expected size of the version response, used to
         *                        verify the parsed packet matches this one
         * @return true if succesful, false if version response packet could not be parsed.
         */
        static bool parseVersionRsp(const DasPacket *packet, BaseModulePlugin::Version &version, size_t expectedLen=0);

        /**
         * Handle READ_CONFIG response from v5.4.
         *
         * Function handles workaround for broken v5.4 firmware version which
         * appends 4 unexpected bytes at the end of the payload. Packet is copied
         * to internal buffer and the length is modified. Then BaseModulePlugin::rspReadConfig()
         * is invoked with the modified packet.
         * For non-v5.4 firmwares the function simply invokes BaseModulePlugin::rspReadConfig()
         * passing it the original packet.
         */
        bool rspReadConfig(const DasPacket *packet);

    private: // functions
        /**
         * Verify the DISCOVER response is from ROC.
         *
         * @param[in] packet with response to DISCOVER
         * @return true if packet was parsed and type of module is ROC.
         */
        bool rspDiscover(const DasPacket *packet);

        /**
         * Overrided READ_VERSION handler dispatches real work to one of rspReadVersion_*
         *
         * @param[in] packet with response to READ_VERSION
         * @return true if packet was parsed and module version verified.
         */
        bool rspReadVersion(const DasPacket *packet);

        /**
         * Pass user command to HighVoltage module through RS232.
         *
         * HV command string is packed into OCC packet and sent to the
         * HV module. HV module does not have its own hardware id,
         * instead the ROC board is used as a router.
         *
         * @param[in] data String representing HV command, must include '\r'
         * @param[in] length Length of the string
         */
        void reqHvCmd(const char *data, uint32_t length);

        /**
         * Receive and join responses from HV module.
         *
         * ROC will send one OCC packet for each character from HV module
         * response. This function concatenates characters back together
         * and keep them in internal buffer until the user reads them.
         *
         * @param[in] packet with HV module response
         * @return true if packet was processed
         */
        bool rspHvCmd(const DasPacket *packet);

        /**
         * Read HV response from internal buffer.
         *
         * Dequeue first response or part of it from internal buffer and return
         * it. Reads up to `size' characters and waits at most `timeout' seconds.
         * Return as soon as some characters are available, not necessarily the
         * entire response. If there are more than one response in the internal
         * buffer, only return first one.
         *
         * @param[out] response Buffer to be written to
         * @param[in] size of the buffer
         * @param[in] timeout Maximum time in seconds to wait before giving up
         * @return Number of characters returned or 0 if timeout.
         */
        size_t getHvResponse(char *response, size_t size, double timeout);

        /**
         * Create and register all status ROC v5.2 parameters to be exposed to EPICS.
         */
        void createStatusParams_v51();

        /**
         * Create and register all config ROC v5.2 parameters to be exposed to EPICS.
         */
        void createConfigParams_v51();

        /**
         * Create and register all status ROC v5.2 parameters to be exposed to EPICS.
         */
        void createStatusParams_v52();

        /**
         * Create and register all config ROC v5.2 parameters to be exposed to EPICS.
         */
        void createConfigParams_v52();
};

#endif // DSP_PLUGIN_H
