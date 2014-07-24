#ifndef ROC_PLUGIN_H
#define ROC_PLUGIN_H

#include "BaseModulePlugin.h"

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
        std::string m_version;              //!< Version string as passed to constructor
        std::list<char> m_lastHvRsp;        //!< Last received RS232 response

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
         * @return true if succesful, false if version response packet could not be parsed.
         */
        static bool parseVersionRsp(const DasPacket *packet, BaseModulePlugin::Version &version);

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
         * Send command to HighVoltage module through RS232
         */
        void reqHvCmd(const char *data, uint32_t length);

        /**
         * Handler for RS232 response.
         */
        bool rspHvCmd(const DasPacket *packet);

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
