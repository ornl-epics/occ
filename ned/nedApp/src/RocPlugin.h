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
    private: // structures and definitions
        static const unsigned NUM_ROCPLUGIN_STATUSPARAMS;   //!< This is used as a runtime assert check and must match number of status parameters
        static const unsigned NUM_CHANNELS = 8;             //!< Number of channels connected to ROC
        static const float    NO_RESPONSE_TIMEOUT;          //!< Timeout to wait for response from ROC, in seconds

    private: // variables
        std::string m_version;              //!< Version string as passed to constructor

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
         * Handler for READ_VERSION response from ROC V5/5.x
         *
         * Populate hardware info parameters, like HwVer, HwRev, FwVer etc.
         * @relates rspReadVersion
         */
        bool rspReadVersion_V5_5x(const DasPacket *packet);

        /**
         * Create and register all status ROC V5 parameters to be exposed to EPICS.
         */
        void createStatusParams_V5_52();

        /**
         * Based on current state machine state, detect whether the response has been
         * handled. Move to timeout state otherwise.
         */
        void timeout(DasPacket::CommandType command);

    private: // asyn parameters
        #define FIRST_ROCPLUGIN_PARAM HardwareVer
        int HardwareVer;    //!< Module hardware version
        int HardwareRev;    //!< Module hardware revision
        int HardwareDate;   //!< Module hardware date
        int FirmwareVer;    //!< Module firmware version
        int FirmwareRev;    //!< Module firmware revision
        #define LAST_ROCPLUGIN_PARAM FirmwareRev

};

#endif // DSP_PLUGIN_H
