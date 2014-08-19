#ifndef PLUGIN_DRIVER_H
#define PLUGIN_DRIVER_H

#include "DasPacketList.h"
#include "EpicsRegister.h"

#include <stdint.h>
#include <string>
#include <functional>
#include <list>
#include <memory>
#include <asynPortDriver.h>
#include <epicsMessageQueue.h>
#include <epicsThread.h>

/**
 * Valid reasons when sending data from dispatchers to plugins.
 */
enum {
    // Numbers should be unique within the asyn driver. All asynPortDriver parameters
    // share the same space. asynPortDriver start with 0 and increments the index for
    // each parameter. Pick reasonably high unique numbers here.
    REASON_OCCDATA          = 10000,
    REASON_OCCDATADIAG      = 10001,
};

class Timer;

/**
 * Registers plugin with EPICS system.
 *
 * Each plugin class should call this macro somewhere in the .c/.cpp file. The macro
 * creates a C function called \<plugin name\>Configure and exports it through EPICS
 * to be used from EPICS shell (for example st.cmd).
 *
 * Registering plugin with EPICS from the C code is first step in EPICS registration.
 * User must manually add support for registered class into nedSupport.dbd file. For
 * example, append the following line:
 * registrar("ExamplePluginRegister")
 *
 * There are several macros depending on the number and type of arguments required by
 * the plugin.
 *
 * Example usage for ExamplePlugin(const char *portName, int queueSize) :
 * EPICS_REGISTER_PLUGIN(ExamplePlugin, 2, "Example port", string, "Queue size", int);
 *
 * @param[in] name Name of the plugin class (ie. ExamplePlugin), a static C function
 *            is created called ExamplePluginConfigure which creates new object of
 *            ExamplePlugin whenever called.
 * @param[in] numargs Number of plugin parameters supported by plugin constructor.
 * @param[in] ... Pairs of (argument name, argument type) parameters. For each parameter
 *            there should be exactly one such pair. Supported argument types are
 *            string, int.
 */
#define EPICS_REGISTER_PLUGIN(name, numargs, ...) EPICS_REGISTER(name, name, numargs, __VA_ARGS__)

/**
 * Abstract base plugin class.
 *
 * The class provides basis for all plugins. It provides connection to the
 * dispatcher. It's derived from asynPortDriver for the callback mechanism
 * and ease of parameters<->PV translation.
 *
 * Designer of the new plugin should pay special attention to parameters that
 * configure the plugin. There are some parameters which need to be static
 * during runtime, like the buffer size or dispatcher port. Those should be
 * made parameters to the plugin constructor.
 * However, most parameters should be made visible through PVs. Those need to
 * be created in the plugin constructor using asynPortDriver::createParam()
 * function.
 *
 * Plugin instances can be loaded at compile time or at run time. For compile
 * time inclusion simply instantiate a new object of the plugin class somewhere
 * in the code. For runtime loaded plugins, the plugin class implementation must
 * export its functionality through EPICS. New instances can then be created from
 * the EPICS shell. Use #EPICS_REGISTER_PLUGIN macro to export plugin to EPICS.
 *
 * BasePlugin provides following asyn parameters:
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * Enable        | asynParamInt32  | 0        | RW   | Enable or disable plugin
 * RxCount       | asynParamInt32  | 0        | RO   | Number of packets received
 * ProcCount     | asynParamInt32  | 0        | RO   | Number of packets processed
 */
class BasePlugin : public asynPortDriver {
	public:
	    static const int defaultInterfaceMask = asynInt32Mask | asynGenericPointerMask | asynDrvUserMask;
	    static const int defaultInterruptMask = asynInt32Mask;

	    /**
	     * Constructor
	     *
	     * Initialize internal state of the class. This includes calling asynPortDriver
	     * constructor, creating and setting default values for class parameters and creating
	     * worker thread for received data callbacks. It does not, however, register to the
	     * dispatcher messages. It needs to be manually enabled by the derived classes after
         * BasePlugin object has been fully constructed or through Enable asyn parameter.
	     *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         * @param[in] reason Type of the messages to receive callbacks for.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         * @param[in] numParams The number of parameters that the derived class supports.
         * @param[in] maxAddr The maximum  number of asyn addr addresses this driver supports. 1 is minimum.
         * @param[in] interfaceMask Bit mask defining the asyn interfaces that this driver supports.
         * @param[in] interruptMask Bit mask definining the asyn interfaces that can generate interrupts (callbacks)
         * @param[in] asynFlags Flags when creating the asyn port driver; includes ASYN_CANBLOCK and ASYN_MULTIDEVICE.
         * @param[in] autoConnect The autoConnect flag for the asyn port driver.
         * @param[in] priority The thread priority for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
         * @param[in] stackSize The stack size for the asyn port driver thread if ASYN_CANBLOCK is set in asynFlags.
	     */
        BasePlugin(const char *portName, const char *dispatcherPortName, int reason, int blocking=0,
                   int numParams=0, int maxAddr=1, int interfaceMask=BasePlugin::defaultInterfaceMask,
                   int interruptMask=BasePlugin::defaultInterruptMask, int asynFlags=0, int autoConnect=1,
                   int priority=0, int stackSize=0);

		/**
		 * Destructor.
		 */
		virtual ~BasePlugin() = 0;

        /**
         * Process the DAS packets received from the dispatcher.
         *
         * The derived processData() implementation should process as much packets
         * as possible from the DAS packets list, ideally all packets that it
         * knows how to handle. Only packets that are not processed by any
         * plugin will be delivered again through this function.
         *
         * No packet can be modified in place, if the processData implementation
         * wants to modify data it must copy it to new memory and work there.
         *
         * BasePlugin guarantees to put a lock around this function.
         *
         * @param[in] packetList List of received packets.
         * @return Last packet from the list that this function processed, or 0 for none.
         */
        virtual void processData(const DasPacketList * const packetList) {};

        /**
         * Unlocked version of processData()
         *
         * Sometimes locking the entire processData() isn't desirable. Derived
         * class should implement either one but not both.
         */
        virtual void processDataUnlocked(const DasPacketList * const packetList);

        /**
         * Handle integer parameter value change.
         *
         * Derived plugins might reimplement this function to get notified about the
         * value change for the registered parameter. Parameter index can be obtained
         * from pasynUser->reason.
         *
         * The derived implementations should invoke parents writeInt32 before returning.
         *
         * @param[in] pasynUser asyn port handle.
         * @param[in] value New value.
         * @return asynSuccess on success
         */
        virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Send single packet to the dispatcher to transmit it through optics.
         *
         * This is always blocking call, it will only return when the dispatcher
         * is done with the data.
         *
         * @param[in] packet Packet to be sent out.
         */
        void sendToDispatcher(const DasPacket *packet);

        /**
         * Request a custom callback function to be called at some time in the future.
         *
         * Using this function, the plugin can request asynchronous task to be
         * scheduled at some relative time.
         * All tasks are run from a background thread and before they're
         * executed, the plugin thread safety is guaranteed through its lock.
         *
         * When the timer expires, it invokes user defined function with no parameters.
         * User defined function should return delay in seconds when the
         * next invocation should occur, or 0 to stop the timer.
         *
         * @param[in] callback Function to be called after delay expires.
         * @param[in] delay Delay from now when to invoke the function, in seconds.
         * @return active or inactive timer
         */
        std::shared_ptr<Timer> scheduleCallback(std::function<float(void)> &callback, double delay);

        /**
         * Return the name of the asyn parameter.
         *
         * @param[in] index asyn parameter index
         * @return Name of the parameter used when parameter was registered.
         */
        const char *getParamName(int index);

        /**
         * Enable or disable callbacks from dispatcher.
         *
         * Connecting to dispatcher means connecting to asyn port that the dispatcher
         * provides in order to receive asynPortDriver callbacks from that port.
         * By default callbacks are disabled. They can be enabled through the Enable
         * asyn parameter or using this function.
         *
         * @param[in] enable Enable callbacks when true, disable otherwise.
         * @return true if operation succeeded.
         */
        bool enableCallbacks(bool enable);

    protected:
        asynUser *m_pasynuser;                      //!< asynUser handler for asyn management
        std::string m_portName;                     //!< Port name
        std::string m_dispatcherPortName;           //!< Dispatcher port name

    private:
        void *m_asynGenericPointerInterrupt;        //!< Generic pointer interrupt handler
        epicsMessageQueue m_messageQueue;           //!< Message queue for non-blocking mode
        epicsThreadId m_threadId;                   //!< Thread ID if created during constructor, 0 otherwise
        bool m_shutdown;                            //!< Flag to shutdown the thread, used in conjunction with messageQueue wakeup
        std::list<std::shared_ptr<Timer> > m_timers;//!< List of timers currently scheduled

        /**
         * Called from epicsTimer when timer expires.
         */
        float timerExpire(std::shared_ptr<Timer> &timer, std::function<float(void)> callback);

    public: // public only for C linkage, don't use outside the class
        /**
         * Called from dispatcher in its thread context.
         *
         * Should processing block, do it in separate thread.
         */
        void dispatcherCallback(asynUser *pasynUser, void *genericPointer);

        /**
         * Background worker thread main function.
         *
         * Thread will automatically stop when PluginBlockingCallbacks is set to 0.
         */
        void processDataThread();

    protected:
        #define FIRST_BASEPLUGIN_PARAM Enable
        int Enable;
        int RxCount;
        int ProcCount;
        #define LAST_BASEPLUGIN_PARAM ProcCount
};

#endif // PLUGIN_DRIVER_H
