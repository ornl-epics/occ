#ifndef BASE_DISPATCHER_H
#define BASE_DISPATCHER_H

#include <asynPortDriver.h>
#include <epicsEvent.h>
#include <DasPacketList.h>

/**
 * A base class for all dispatchers.
 *
 * The BaseDispatcher class wraps asynPortDriver functionality into easy to
 * simpler interfaces. It also provides a common ground for all dispatchers,
 * like the parameters common to all dispatchers.
 */
class BaseDispatcher : public asynPortDriver {
	public:

	    /**
	     * Constructor
	     *
	     * @param[in] portName Name of the asyn port to connect to.
	     */
		BaseDispatcher(const char *portName);

		/**
		 * Destructor.
		 */
		virtual ~BaseDispatcher() {}

        /**
         * Send data and length to all plugins listening for specified message type.
         *
         * The function goes through the list of registered plugins and invokes their registered function
         * in the context of the current thread. It's up to plugins when to process the data.
         *
         * @param[in] reason Only plugins registered to this reason will be called.
         * @param[in] data Pointer to arbitrary data. Plugins must be aware of the format of the data.
         * @param[in] length Length of the data in bytes.
         */
		void sendToPlugins(int reason, const DasPacketList *packetList);

    private:
        int DispatcherEnabled;
        #define FIRST_BASEDISPATCHER_PARAM DispatcherEnabled
        int DispatcherEnabled1;
        #define LAST_BASEDISPATCHER_PARAM DispatcherEnabled1
};

#endif // BASE_DISPATCHER_H
