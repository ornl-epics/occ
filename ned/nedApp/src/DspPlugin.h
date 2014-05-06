#ifndef DSP_PLUGIN_H
#define DSP_PLUGIN_H

#include "BaseModulePlugin.h"

/**
 * Plugin for DSP module.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * HwDate        | asynOctet       | ""       | RO   | Hardware date as YYYY/MM/DD
 * HwVer         | asynParamInt32  | 0        | RO   | Hardware version
 * HwRev         | asynParamInt32  | 0        | RO   | Hardware revision
 * FwDate        | asynOctet       | ""       | RO   | Firmware date as YYYY/MM/DD
 * FwVer         | asynParamInt32  | 0        | RO   | Firmware version
 * FwRev         | asynParamInt32  | 0        | RO   | Firmware revision
 * Status        | asynParamInt32  | 0        | RO   | Status of RocPlugin            (0=not initialized)
 * Command       | asynParamInt32  | 0        | RW   | Issue RocPlugin command        (1=initialize,2=read status,3=write config to module,4=read config from module)

 */
class DspPlugin : public BaseModulePlugin {
    private: // structures and definitions
        struct ParamDesc {
            char section;           //!< Section name
            uint32_t offset;        //!< An 4-byte offset within the section
            uint32_t mask;          //!< The mask specifies allowed range of values and position within the parameter bitfield, for example mask 0x30 allows values in range 0-3 and the actual value will be shifted by 4 when put in section parameter
            int initVal;            //!< Initial value after object is created or configuration reset is being requested
        };

        /**
         * Valid statuses of the DspPlugin, the communication link with Dsp or the DSP itself.
         */
        enum Status {
            STAT_NOT_INITIALIZED    = 0,    //!< DspPlugin has not yet been initialized
            STAT_DSP_TIMEOUT        = 10,   //!< DSP has not respond in expected time to the last command
        };

        static const unsigned NUM_DSPPLUGIN_CONFIGPARAMS;   //!< This is used as a runtime assert check and must match number of configuration parameters
        static const unsigned NUM_DSPPLUGIN_STATUSPARAMS;   //!< This is used as a runtime assert check and must match number of status parameters
        static const double DSP_RESPONSE_TIMEOUT;           //!< Default DSP response timeout, in seconds

    public:

        /**
         * Constructor for DspPlugin
         *
         * Constructor will create and populate PVs with default values.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] hardwareId Hardware ID of the DSP module, can be in IP format (xxx.xxx.xxx.xxx) or
         *                       in hex number string in big-endian byte order (0x15FACB2D equals to IP 21.250.203.45)
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        DspPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, int blocking);

    private:
        std::map<int, struct ParamDesc> m_configParams;
        std::map<char, uint32_t> m_configSectionSizes;

        /**
         * Handle READ_VERSION from DSP.
         *
         * @param[in] packet DAS packet with verified READ_VERSION response.
         */
        void rspVersionRead(const DasPacket *packet);

        /**
         * Construct configuration data and send it to module.
         *
         * Configuration data is gathered from configuration parameters
         * and their current values. Configuration packet is created with
         * configuration data attached and sent to OCC, but not waited for
         * response.
         * Access to this function should be locked before calling it or
         * otherwise configuration data inconsistencies can occur. Luckily,
         * this function gets triggered from parameter update, which is
         * already locked.
         *
         * This function is asynchronous but does not wait for response.
         */
        void reqCfgWrite();

        /**
         * Handle READ_CONFIG response from DSP.
         *
         * Populate all PVs with the values from current DSP configuration.
         *
         * @param[in] packet DAS packet with verified READ_CONFIG response.
         */
        void rspCfgRead(const DasPacket *packet);

        /**
         * Reset configuration PVs to initial values.
         */
        void cfgReset();

        /**
         * Overloaded function called by asynPortDriver when the PV should change value.
         */
        asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Overloaded function called by asynPortDriver to get PV value.
         */
        asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);

        /**
         * Process incoming packets.
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Create and register all configuration parameters to be exposed to EPICS.
         */
        void createConfigParams();

        /**
         * Create and register single integer configuration parameter.
         */
        void createConfigParam(const char *name, char section, uint32_t offset, uint32_t nBits, uint32_t shift, int value);
        using asynPortDriver::createParam;

        /**
         * Create and register all status parameters to be exposed to EPICS.
         */
        void createStatusParams();

        /**
         * Setup the expected next response and a timeout routine.
         *
         * When the next command is received, it must be the one specified in
         * command parameter. In that case the callback is invoked.
         * If the received command does not match expected one, error is
         * reported and next expected command is cleared.
         * If no command is received during the specified time, the
         * noRespondeCleanup() function is called.
         *
         * @param[in] cmd Command to wait for.
         * @param[in] cb Callback to run when command is received.
         * @param[in] timeout The time to wait for response before calling the cleanup function.
         */
        void expectResponse(DasPacket::CommandType cmd, std::function<void(const DasPacket *)> &cb, double timeout=DSP_RESPONSE_TIMEOUT);

        /**
         * Cleanup function called from Timer.
         *
         * It gets called in any case, whether the response ... TODO
         */
        void noResponseCleanup(DasPacket::CommandType cmd);

        /**
         * Construct particular section of configuration data.
         */
        uint32_t configureSection(char section, uint32_t *data, uint32_t count);

        /**
         * Return the absolute offset of the configuration section within all configuration structure.
         */
        uint32_t getCfgSectionOffset(char section);

    private:
        #define FIRST_DSPPLUGIN_PARAM Status
        int Status;         //!< Status of the DSP plugin
        int Command;        //!< Command to plugin, like initialize the module, read configuration, verify module version etc.
        int HardwareId;     //!< Hardare ID that this object is controlling
        int HardwareVer;    //!< Module hardware version
        int HardwareRev;    //!< Module hardware revision
        int HardwareDate;   //!< Module hardware date
        int FirmwareVer;    //!< Module firmware version
        int FirmwareRev;    //!< Module firmware revision
        int FirmwareDate;   //!< Module firmware date
        #define LAST_DSPPLUGIN_PARAM FirmwareDate

};

#endif // DSP_PLUGIN_H
