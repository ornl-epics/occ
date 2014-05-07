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
 */
class DspPlugin : public BaseModulePlugin {
    private: // structures and definitions
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
        /**
         * Verify the DISCOVER response is from DSP.
         *
         * @param[in] packet with response to DISCOVER
         * @return true if packet was parsed and type of module is DSP.
         */
        bool rspDiscover(const DasPacket *packet);

        /**
         * Overrided READ_VERSION handler.
         *
         * @param[in] packet with response to READ_VERSION
         * @return true if packet was parsed and module version verified.
         */
        bool rspReadVersion(const DasPacket *packet);

        /**
         * Create and register all configuration parameters to be exposed to EPICS.
         */
        void createConfigParams();

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

    private:
        #define FIRST_DSPPLUGIN_PARAM Command
        int HardwareVer;    //!< Module hardware version
        int HardwareRev;    //!< Module hardware revision
        int HardwareDate;   //!< Module hardware date
        int FirmwareVer;    //!< Module firmware version
        int FirmwareRev;    //!< Module firmware revision
        int FirmwareDate;   //!< Module firmware date
        #define LAST_DSPPLUGIN_PARAM FirmwareDate

};

#endif // DSP_PLUGIN_H
