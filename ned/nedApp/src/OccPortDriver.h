#ifndef OCCPORTDRIVER_H
#define OCCPORTDRIVER_H

#include "BaseCircularBuffer.h"
#include "DasPacketList.h"

#include <asynPortDriver.h>
#include <epicsThread.h>

// Forward declaration of the OCC API handler
struct occ_handle;

/**
 * Main class for ned
 *
 * This is a glue class that brings all the main pieces together. It communicates
 * directly to OCC board to initialize it, it creates handler for reading the OCC
 * buffer and serves as the main dispatcher of all OCC data.
 *
 * The following asyn parameters are provided and can be used from EPICS PV infrastructure:
 * asyn param name    | asyn param index     | asyn param type | init val | mode | Description
 * ------------------ | -------------------- | --------------- | -------- | ---- | -----------
 * DEVICE_STATUS      | DeviceStatus         | asynParamInt32  | 0        | RO   | Raw error code as returned from OCC API functions, non-negative number for errors, 0 for success
 * BOARD_TYPE         | BoardType            | asynParamInt32  | 0        | RO   | Type of board based as described by occ_board_type; 1 for SNS PCI-X, 2 for SNS PCIe, 15 for simulator
 * BOARD_FIRMWARE_VER | BoardFirmwareVersion | asynParamInt32  | 0        | RO   | OCC board firmware version
 * OPTICS_PRESENT     | OpticsPresent        | asynParamInt32  | 0        | RO   | 1 when optical link is present, 0 otherwise
 * RX_ENABLED         | RxEnabled            | asynParamInt32  | 0        | RW   | Flag to enable (1) or disable (0) reception on OCC board
 */
class epicsShareFunc OccPortDriver : public asynPortDriver {
	public:
	    /**
	     * Constructor
	     *
	     * @param[in] portName Name of the asyn port to which plugins can connect
	     * @param[in] deviceId unused
	     * @param[in] localBufferSize If not zero, a local buffer will be created
	     *            where all data from OCC DMA buffer will be copied to as soon
	     *            as it is available.
	     */
		OccPortDriver(const char *portName, int deviceId, uint32_t localBufferSize);

		/**
		 * Destructor
		 */
		~OccPortDriver();

    private:
        int m_version;
        int m_test;

        struct occ_handle *m_occ;
        BaseCircularBuffer *m_circularBuffer;
        epicsThreadId m_occBufferReadThreadId;

        /**
         * Send list of packets to the plugins.
         *
         * @param messageType Message type to which plugins are registered to receive.
         * @param packetList List of packets received from OCC.
         */
        void sendToPlugins(int messageType, const DasPacketList *packetList);

        /**
         * Overloaded method.
         */
		asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);

        /**
         * Overloaded method.
         */
		asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Overloaded method.
         */
		asynStatus writeGenericPointer (asynUser *pasynUser, void *pointer);

    public:
        /**
         * Process data from OCC buffer and dispatch it to the registered plugins.
         *
         * Monitor OCC buffer for new data. When it's available, transform it into
         * list of packets and send the list to the registered plugins. Wait for all
         * plugins to complete processing it and than advance OCC buffer consumer
         * index for the amount of bytes processed. Start monitoring again.
         *
         * Runs from the worker thread through C static linkage, this function
         * must be public.
         */
        void processOccData();

    private:
        int DeviceStatus;
        #define FIRST_OCCPORTDRIVER_PARAM DeviceStatus
        int BoardType;
        int BoardFirmwareVersion;
        int OpticsPresent;
        int RxEnabled;
        #define LAST_OCCPORTDRIVER_PARAM RxEnabled
};

#endif // OCCPORTDRIVER_H
