#ifndef OCCPORTDRIVER_H
#define OCCPORTDRIVER_H

#include "BaseCircularBuffer.h"

#include <asynPortDriver.h>

struct occ_handle;

class epicsShareFunc OccPortDriver : public asynPortDriver {
	public:
		OccPortDriver(const char *portName, int deviceId, uint32_t localBufferSize);
		~OccPortDriver();

		//asynStatus readOctet(asynUser *pasynUser, char *value, size_t size, size_t *length);
		asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);

    private:
        int m_version;
        int m_test;

        struct occ_handle *m_occ;
        BaseCircularBuffer *m_circularBuffer;

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
