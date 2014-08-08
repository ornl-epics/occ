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
 * Next table lists asyn parameters provided and can be used from EPICS PV infrastructure.
 * Some naming restrictions enforced by EPICS records apply:
 * - PV name length is limited to 27 characters in total, where the static prefix
 *   is BLXXX:Det:OccX: long, living 13 characters to asyn param name
 * - PV comment can be 29 characters long (text in brackets may be used to describe EPICS
 *   PV valid values)
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * Status        | asynParamInt32  | 0        | RO   | Status of OccPortDriver       (0=OK,1=buffer full,2=OCC error)
 * Command       | asynParamInt32  | 0        | RW   | Issue OccPortDriver command   (1=optics enable)
 * LastErr       | asynParamInt32  | 0        | RO   | Last error code returned by OCC API
 * BoardType     | asynParamInt32  | 0        | RO   | OCC board type                (1=SNS PCI-X,2=SNS PCIe,15=simulator)
 * BoardFwVer    | asynParamInt32  | 0        | RO   | OCC board firmware version
 * OpticsPresent | asynParamInt32  | 0        | RO   | Is optical cable present      (0=not present,1=present)
 * OpticsEnabled | asynParamInt32  | 0        | RO   | Is optical link enabled       (0=not enabled,1=enabled)
 * RxStalled     | asynParamInt32  | 0        | RO   | Incoming data stalled         (0=not stalled,1=OCC stalled,2=copy stalled,3=both stalled)
 * ErrCrc        | asynParamInt32  | 0        | RO   | Number of CRC errors detected by OCC
 * ErrLength     | asynParamInt32  | 0        | RO   | Number of length errors detected by OCC
 * ErrFrame      | asynParamInt32  | 0        | RO   | Number of frame errors detected by OCC
 * FpgaTemp      | asynParamFloat64| 0.0      | RO   | FPGA temperature in Celsius
 * FpgaCoreVolt  | asynParamFloat64| 0.0      | RO   | FPGA core voltage in Volts
 * FpgaAuxVolt   | asynParamFloat64| 0.0      | RO   | FPGA aux voltage in Volts
 * SfpTemp       | asynParamFloat64| 0.0      | RO   | SFP temperature in Celsius
 * SfpRxPower    | asynParamFloat64| 0.0      | RO   | SFP RX power in uW
 * SfpTxPower    | asynParamFloat64| 0.0      | RO   | SFP TX power in uW
 * SfpVccPower   | asynParamFloat64| 0.0      | RO   | SFP VCC power in Volts
 * SfpTxBiasCur  | asynParamFloat64| 0.0      | RO   | SFP TX bias current in uA
 * StatusInt     | asynParamFloat64| 1.0      | RW   | OCC status refresh interval in s
 * ExtStatusInt  | asynParamFloat64| 60.0     | RW   | OCC extended status refresh interval in s
 * DmaBufUtil    | asynParamInt32  | 0        | RO   | DMA memory utilization [0-100]
 * CopyBufUtil   | asynParamInt32  | 0        | RO   | Virtual buffer utilization [0-100]
 */
class epicsShareFunc OccPortDriver : public asynPortDriver {
    private:
        /**
         * Valid statuses of the OccPortDriver and the OCC infrastructure.
         */
        enum {
            STAT_OK             = 0,    //!< No error
            STAT_BUFFER_FULL    = 1,    //!< Receive buffer is full, acquisition was stopped
            STAT_OCC_ERROR      = 2,    //!< OCC error was detected
        };

        /**
         * Recognized command values through Command parameter.
         */
        enum {
            CMD_OPTICS_ENABLE           = 1,    //!< Enable optical link
            CMD_OPTICS_DISABLE          = 2,    //!< Disable optical link
            CMD_ERROR_PACKETS_ENABLE    = 3,    //!< Report link errors as verbose error packets
            CMD_ERROR_PACKETS_DISABLE   = 4,    //!< Don't report link errors as error packets
        };

	public:
	    /**
	     * Constructor
	     *
	     * @param[in] portName Name of the asyn port to which plugins can connect
	     * @param[in] localBufferSize If not zero, a local buffer will be created
	     *            where all data from OCC DMA buffer will be copied to as soon
	     *            as it is available.
	     */
		OccPortDriver(const char *portName, uint32_t localBufferSize);

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
        epicsThreadId m_occStatusRefreshThreadId;
        static const float DEFAULT_BASIC_STATUS_INTERVAL;
        static const float DEFAULT_EXTENDED_STATUS_INTERVAL;

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
        void processOccDataThread();

        /**
         * Thread refreshing OCC status periodically.
         *
         * Querying for OCC status can block for some time and could interfere with
         * other driver functionality. For this reason it must be in its own thread.
         *
         * The thread is driven by StatusInt and ExtStatusInt parameters. When StatusInt
         * expires, it queries for OCC status and updates all the parameters. If the
         * ExtStatusInt also expired, it will also query for parameters which take longer
         * to update but are not required to refresh frequently.
         *
         * Runs from the worker thread through C static linkage, this function
         * must be public.
         */
        void refreshOccStatusThread();

    private:
        #define FIRST_OCCPORTDRIVER_PARAM Status
        int Status;
        int Command;
        int LastErr;
        int BoardType;
        int BoardFwVer;
        int OpticsPresent;
        int OpticsEnabled;
        int RxStalled;
        int ErrPktsEnabled;
        int FpgaTemp;
        int FpgaCoreVolt;
        int FpgaAuxVolt;
        int ErrCrc;
        int ErrLength;
        int ErrFrame;
        int SfpTemp;
        int SfpRxPower;
        int SfpTxPower;
        int SfpVccPower;
        int SfpTxBiasCur;
        int StatusInt;
        int ExtStatusInt;
        int DmaBufUtil;
        int CopyBufUtil;
        #define LAST_OCCPORTDRIVER_PARAM CopyBufUtil
};

#endif // OCCPORTDRIVER_H
