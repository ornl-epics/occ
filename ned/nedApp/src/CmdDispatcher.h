#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "BasePlugin.h"

/**
 * Plugin for receiving all packets but dispatching only commands to connected plugins.
 *
 * This plugin will register to receive all packets and will forward only those
 * that are packets. Plugin tries to minimize number of callbacks forwarded by
 * grouping successive command packets. It will dispatch the callbacks under the
 * same asyn reason as OccPortDriver does, allowing rest of plugins to choose
 * who to subscribe to.
 *
 * The main purpose of this class is to allow processing of all command packets
 * in one thread, while the rest of the data can be processed in different thread.
 * Plugins handling specific commands can be left non-blocking and while connected
 * to this plugin, their processing will be done independently of rest of data in
 * separate thread.
 *
 * Plugin is always non-blocking, thus each instance creates a processing thread.
 */
class CmdDispatcher : public BasePlugin {
    public:
        /**
         * Constructor
         */
        CmdDispatcher(const char *portName, const char *connectPortName);

    private:
        uint32_t m_nReceived;
        uint32_t m_nProcessed;

        /**
         * Process only command data and send it to connected plugins.
         */
        void processData(const DasPacketList * const packetList);

        /**
         * Send selected consequtive packets to connected plugins.
         *
         * @param[in] first Pointer to the address in memory of the first packet to be sent.
         * @param[in] last Pointer to the address in memory of the last packet to be sent.
         */
        void sendToPlugins(const DasPacket *first, const DasPacket *last);

        /**
         * Overloaded method used when plugins send data to us.
         */
        asynStatus writeGenericPointer(asynUser *pasynUser, void *pointer);
};

#endif // CMD_DISPATCHER_H
