#ifndef RTDL_PLUGIN_H
#define RTDL_PLUGIN_H

#include "BasePlugin.h"

/**
 * Gather and display statistical information of the incoming data
 *
 * By default, RtdlPlugin will receive and process all incoming data.
 *
 * Available RtdlPlugin parameters (in addition to ones from BasePlugin):
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * Timestamp     | asynParamOctet  | Not init | RO   | Timestamp string of last RTDL
 * BadPulse      | asynParamInt32  | Not init | RO   | Bad pulse indicator (0=no, 1=yes)
 * PulseFlavor   | asynParamInt32  | Not init | RO   | Pulse flavor
 * PulseCharge   | asynParamInt32  | Not init | RO   | Pulse charge
 * BadVetoFrame  | asynParamInt32  | Not init | RO   | Bad veto frame
 * BadCycleFrame | asynParamInt32  | Not init | RO   | Bad cycle frame
 * Tstat         | asynParamInt32  | Not init | RO   | TSTAT
 * Veto          | asynParamInt32  | Not init | RO   | Veto frame
 * Cycle         | asynParamInt32  | Not init | RO   | Cycle frame
 * IntraPulseTime| asynParamInt32  | Not init | RO   | Number of ns between reference pulses
 * TofFullOffset | asynParamInt32  | Not init | RO   | TOF full offset
 * FrameOffset   | asynParamInt32  | Not init | RO   | Frame offset
 * TofFixedOffset| asynParamInt32  | Not init | RO   | TOF fixed offset
 */
class RtdlPlugin : public BasePlugin {
    public: // functions
        /**
         * Constructor
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        RtdlPlugin(const char *portName, const char *dispatcherPortName, int blocking);

        /**
         * Overloaded function to receive all OCC data.
         */
        void processData(const DasPacketList * const packetList);

    private:
        uint64_t m_receivedCount;
        uint64_t m_processedCount;

    private: // asyn parameters
        #define FIRST_RTDLPLUGIN_PARAM Timestamp
        int Timestamp;
        int BadPulse;
        int PulseFlavor;
        int PulseCharge;
        int BadVetoFrame;
        int BadCycleFrame;
        int Tstat;
        int Veto;
        int Cycle;
        int IntraPulseTime;
        int TofFullOffset;
        int FrameOffset;
        int TofFixedOffset;
        #define LAST_RTDLPLUGIN_PARAM TofFixedOffset
};

#endif // RTDL_PLUGIN_H
