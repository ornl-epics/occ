#ifndef ACPC_FEM_PLUGIN_H
#define ACPC_FEM_PLUGIN_H

#include "BaseModulePlugin.h"

/**
 * Plugin for ACPC FEM module.
 *
 * General plugin parameters:
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 */
class AcpcFemPlugin : public BaseModulePlugin {
    private: // structures and definitions
        static const unsigned NUM_ACPCFEMPLUGIN_DYNPARAMS;  //!< Maximum number of asyn parameters, including the status and configuration parameters

    private: // variables
        std::string m_version;              //!< Version string as passed to constructor

    public: // functions

        /**
         * Constructor for AcpcFemPlugin
         *
         * Constructor will create and populate PVs with default values.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] hardwareId Hardware ID of the ROC module, can be in IP format (xxx.xxx.xxx.xxx) or
         *                       in hex number string in big-endian byte order (0x15FACB2D equals to IP 21.250.203.45)
         * @param[in] version ACPC FEM HW&SW version, ie. V10_50
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        AcpcFemPlugin(const char *portName, const char *dispatcherPortName, const char *hardwareId, const char *version, int blocking=0);

        /**
         * Try to parse the ACPC FEM version response packet an populate the structure.
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
         * Handler for READ_VERSION response from FEM V10
         *
         * Populate hardware info parameters, like HwVer, HwRev, FwVer etc.
         * @relates rspReadVersion
         */
        bool rspReadVersion_V10(const DasPacket *packet);

        /**
         * Create and register all status FEM V10 parameters to be exposed to EPICS.
         */
        void createStatusParams();

        /**
         * Create and register all config FEM V10 parameters to be exposed to EPICS.
         */
        void createConfigParams();
};

#endif // ACPC_FEM_PLUGIN_H
