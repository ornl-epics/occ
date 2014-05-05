#ifndef ROC_PLUGIN_H
#define ROC_PLUGIN_H

#include "BaseModulePlugin.h"
#include "StateMachine.h"

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
 * Status        | asynParamInt32  | 0        | RO   | Status of RocPlugin            (0=not initialized)
 * Command       | asynParamInt32  | 0        | RW   | Issue RocPlugin command        (1=initialize,2=read status,3=write config to module,4=read config from module)
 */
class RocPlugin : public BaseModulePlugin {
    private: // structures and definitions

        /**
         * Structure describing the status parameters.
         */
        struct StatusParamDesc {
            uint32_t offset;        //!< An 4-byte offset within the payload
            uint32_t shift;         //!< Position of the field bits within 32 bits
            uint32_t width;         //!< Number of bits used for the value
        };

        /**
         * Valid statuses of the DspPlugin, the communication link with ROC or the ROC itself.
         */
        enum Status {
            STAT_NOT_INITIALIZED    = 0,    //!< RocPlugin has not yet been initialized
            STAT_TIMEOUT            = 1,    //!< Module is not responding to commands
            STAT_TYPE_MISMATCH      = 2,    //!< Module with given address is not ROC board
            STAT_VERSION_MISMATCH   = 3,    //!< Actual module version does not match configured one
            STAT_TYPE_VERIFIED      = 10,   //!< Module type has been verified to be ROC board
            STAT_VERSION_VERIFIED   = 11,   //!< Module version has been verified to match configured one
            STAT_READY              = 12,   //!< Module is ready
        };

        /**
         * Valid state machine actions.
         */
        enum Action {
            DISCOVER_OK,                    //!< Received valid DISCOVER response which matches ROC type
            DISCOVER_MISMATCH,              //!< Received invalid DISCOVER response which doesn't match ROC type
            VERSION_READ_OK,                //!< Received valid READ_STATUS response which matches configured version
            VERSION_READ_MISMATCH,          //!< Received invalid READ_STATUS response which doesn't match configured version
            TIMEOUT,                        //!< Timeout occurred
        };

        static const unsigned NUM_ROCPLUGIN_STATUSPARAMS;   //!< This is used as a runtime assert check and must match number of status parameters
        static const unsigned NUM_CHANNELS = 8;             //!< Number of channels connected to ROC
        static const float    NO_RESPONSE_TIMEOUT = 1.0;    //!< Timeout to wait for response from ROC, in seconds

    private: // variables
        uint32_t m_statusPayloadSize;       //!< Size of the payload of the READ_STATUS command, based on selected ROC hw and sw version
        std::string m_version;              //!< Version string as passed to constructor
        StateMachine<enum Status, int> m_stateMachine;  //!< State machine for the current status
        //std::function<void()> createStatusParams;       //!< Pointer to a function which creates ROC parameters based on selected version

        /**
         * Handler for READ_VERSION response.
         *
         * It's a function pointer which gets assigned based on the ROC version to
         * one of the following:
         * - rspReadVersion_V5_5x()
         */
         std::function<void(const DasPacket *packet)> rspReadVersion;

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

    private: // asynPortDriver related functions
        /**
         * Process incoming packets.
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Overloaded function called by asynPortDriver when the PV should change value.
         */
        asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

    private: // functions
        /**
         * Handler for DISCOVER response from ROC.
         *
         * Verify the response is from ROC.
         */
        void rspDiscover(const DasPacket *packet);

        /**
         * Handler for READ_VERSION response from ROC V5/5.x
         *
         * Populate hardware info parameters, like HwVer, HwRev, FwVer etc.
         * @relates rspReadVersion
         */
        void rspReadVersion_V5_5x(const DasPacket *packet);

        /**
         * Handler for READ_STATUS response from ROC.
         *
         * Populate all params with new values as read from status response packet.
         */
        void rspReadStatus(const DasPacket *packet);

        /**
         * Create and register all status ROC V5 parameters to be exposed to EPICS.
         */
        void createStatusParams_V5();

        /**
         * Create and register all status ROC V2 firmware 5.x parameters to be exposed to EPICS.
         */
        void createStatusParams_V2_5x();

        /**
         * Create and register all status ROC V2 firmware 4.5 parameters to be exposed to EPICS.
         */
        void createStatusParams_V2_45();

        /**
         * Create and register all status ROC V2 firmware 4.1 parameters to be exposed to EPICS.
         */
        void createStatusParams_V2_41();

        /**
         * Create and register single integer status parameter.
         */
        void createChannelStatusParam(const char *name, unsigned channel, uint32_t offset, uint32_t nBits, uint32_t shift);

        /**
         * Based on current state machine state, detect whether the response has been
         * handled. Move to timeout state otherwise.
         */
        void timeout(DasPacket::CommandType command);

    protected:
        #define FIRST_ROCPLUGIN_PARAM Status
        int Status;         //!< Status of the DSP plugin
        int Command;        //!< Command to plugin, like initialize the module, read configuration, verify module version etc.
    private:
        int HardwareVer;    //!< Module hardware version
        int HardwareRev;    //!< Module hardware revision
        int HardwareDate;   //!< Module hardware date
        int FirmwareVer;    //!< Module firmware version
        int FirmwareRev;    //!< Module firmware revision
        int FirmwareDate;   //!< Module firmware date
        #define LAST_ROCPLUGIN_PARAM FirmwareDate

};

#endif // DSP_PLUGIN_H
