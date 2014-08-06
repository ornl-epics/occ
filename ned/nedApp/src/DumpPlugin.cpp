#include "DumpPlugin.h"
#include "Log.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#define NUM_DUMPPLUGIN_PARAMS ((int)(&LAST_DUMPPLUGIN_PARAM - &FIRST_DUMPPLUGIN_PARAM + 1))

EPICS_REGISTER_PLUGIN(DumpPlugin, 3, "Port name", string, "Dispatcher port name", string, "Blocking", int);

DumpPlugin::DumpPlugin(const char *portName, const char *dispatcherPortName, int blocking)
    : BasePlugin(portName, dispatcherPortName, REASON_OCCDATA, blocking, NUM_DUMPPLUGIN_PARAMS, 1, asynOctetMask)
    , m_fd(-1)
{
    createParam("FilePath",     asynParamOctet, &FilePath);
    setCallbacks(false);
}

DumpPlugin::~DumpPlugin()
{
    closeFile();
}

void DumpPlugin::processData(const DasPacketList * const packetList)
{
    int nReceived = 0;
    int nProcessed = 0;
    getIntegerParam(RxCount,    &nReceived);
    getIntegerParam(ProcCount,  &nProcessed);

    for (const DasPacket *packet = packetList->first(); packet != 0; packet = packetList->next(packet)) {
        nReceived++;

        if (m_fd == -1)
            continue;

        ssize_t ret = write(m_fd, packet, packet->length());
        if (ret != static_cast<ssize_t>(packet->length())) {
            if (ret == -1) {
                LOG_ERROR("Abort dumping to file due to an error: %s", strerror(errno));
                closeFile();
                break;
            } else {
                off_t offset = lseek(m_fd, 0, SEEK_CUR) - ret;
                LOG_WARN("Only dumped %zuB out of %uB at offset %lu", ret, packet->length(), offset);
                continue;
            }
        }

        nProcessed++;
    }

    setIntegerParam(RxCount,    nReceived);
    setIntegerParam(ProcCount,  nProcessed);
    callParamCallbacks();
}

asynStatus DumpPlugin::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
    if (pasynUser->reason == Enable) {
        if (value > 0) {
            char path[1024];
            if (m_fd == -1 && getStringParam(FilePath, sizeof(path), path) == asynSuccess && openFile(path) == false)
                return asynError;
        } else {
            closeFile();
        }
    }
    return BasePlugin::writeInt32(pasynUser, value);
}

asynStatus DumpPlugin::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
    if (pasynUser->reason == FilePath) {
        int enabled = 0;
        // If plugin is enabled, switch file now, otherwise postpone until enabled
        if (getIntegerParam(Enable, &enabled) == asynSuccess && enabled == 1) {
            closeFile();
            if (openFile(std::string(value, nChars)) == false) {
                return asynError;
            }
        }
    }
    return BasePlugin::writeOctet(pasynUser, value, nChars, nActual);
}

bool DumpPlugin::openFile(const std::string &path)
{
    m_fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR);
    if (m_fd == -1) {
        LOG_ERROR("Can not open dump file '%s': %s", path.c_str(), strerror(errno));
        return false;
    }

    LOG_INFO("Switched dump file to '%s'", path.c_str());
    return true;
}

void DumpPlugin::closeFile()
{
    if (m_fd != -1) {
        (void)fcntl(m_fd, F_SETFL, O_NONBLOCK); // best we can do, if fcntl() fails, close() could stall for a while
        (void)close(m_fd);
        m_fd = -1;
    }
}
