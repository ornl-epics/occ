#ifndef PLUGIN_DRIVER_H
#define PLUGIN_DRIVER_H

#include "DasPacketList.h"

#include <asynPortDriver.h>
#include <epicsMessageQueue.h>
#include <stdint.h>

/**
 * Valid reasons when sending data from dispatchers to plugins.
 */
enum {
    // Numbers should be unique within the asyn driver. All asynPortDriver parameters
    // share the same space. asynPortDriver start with 0 and increments the index for
    // each parameter. Pick reasonably high unique numbers here.
    REASON_NORMAL           = 10000,
    REASON_DIAGNOSTIC       = 10001,
};

/**
 * Abstract base plugin class.
 *
 * The class provides basis for all plugins. It provides connection to the
 * dispatcher. It's derived from asynPortDriver for the callback mechanism
 * and ease of parameters<->PV translation.
 */
class BasePlugin : public asynPortDriver {
	public:
	    static const int defaultInterfaceMask = asynGenericPointerMask | asynDrvUserMask;
	    static const int defaultInterruptMask = asynGenericPointerMask;

	    /**
	     * Constructor
	     *
	     * Initialize internal state of the class. This includes calling asynPortDriver
	     * constructor, creating and setting default values for class parameters,
	     * connecting to dispatcher port.
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
	     * @param[in] blockingCallbacks Should the plugin block until callback has been
         *            processed (true) or should it process callback in background thread,
         *            releasing the calling thread immediately (false).
         * @param[in] reason Type of the messages to receive callbacks for.
         * @param[in] maxAddr The maximum  number of asyn addr addresses this driver supports. 1 is minimum.
         * @param[in] numParams The number of parameters that the derived class supports.
         * @param[in] interfaceMask Bit mask defining the asyn interfaces that this driver supports.
         * @param[in] interruptMask Bit mask definining the asyn interfaces that can generate interrupts (callbacks)
         * @param[in] asynFlags Flags when creating the asyn port driver; includes ASYN_CANBLOCK and ASYN_MULTIDEVICE.
         * @param[in] autoConnect The autoConnect flag for the asyn port driver.
         * @param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
         * @param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
	     */
        BasePlugin(const char *portName, const char *dispatcherPortName, bool blockingCallbacks, int reason,
                   int maxAddr=1, int numParams=0, int interfaceMask=BasePlugin::defaultInterfaceMask,
                   int interruptMask=BasePlugin::defaultInterruptMask, int asynFlags=0, int autoConnect=1,
                   int priority=0, int stackSize=0);

		/**
		 * Destructor.
		 */
		virtual ~BasePlugin() { };

        /**
         * Process the DAS packets received from the dispatcher.
         *
         * The processData() implementation should process as much packets
         * as possible from the DAS packets list, ideally all packets that it
         * knows how to handle. Only packets that are not processed by any
         * plugin will be delivered again through this function.
         *
         * No packet can be modified in place, if the processData implementation
         * wants to modify data it must copy it to new memory and work there.
         *
         * @param[in] packetList List of received packets.
         * @return Last packet from the list that this function processed, or 0 for none.
         */
        virtual const DasPacket *processData(const DasPacketList * const packetList) = 0;

    private:
        int PluginDriverBlockingCallbacks;
        #define FIRST_PLUGINDRIVER_PARAM PluginDriverBlockingCallbacks
        int PluginDriverEnableCallbacks;
        #define LAST_PLUGINDRIVER_PARAM PluginDriverEnableCallbacks

    private:
        asynUser *m_pasynuserDispatcher;            //!< asynUser for connecting to dispatcher
        void *m_asynGenericPointerInterruptPvt;     //!< The asyn interfaces we access as a client
        epicsMessageQueue m_messageQueue;           //!< Message queue for non-blocking mode

        asynStatus connectToDispatcherPort(const char *portName);
        asynStatus setCallbacks(bool enableCallbacks);

    public: // public only for C linkage, don't use outside the class
        /**
         * Called from dispatcher in its thread context.
         *
         * Should processing block, do it in separate thread.
         */
        void dispatcherCallback(asynUser *pasynUser, void *genericPointer);
};

#endif // PLUGIN_DRIVER_H
