#ifndef DUMP_PLUGIN_H
#define DUMP_PLUGIN_H

#include "BasePlugin.h"

#include <string>

/**
 * Dummy dump plugin writes all received OCC data into a file.
 *
 * The plugin dumps raw data received from OCC to a file. File path needs to be
 * set through FilePath parameter and plugin needs to be enabled to actually
 * open a file and save data. When plugin is disabled, file gets closed.
 *
 * File blocking mode is used for opening and writing to file. This is to
 * asure that all the data is written to file without having to buffer it
 * locally. It could block for a while and stall the acquisition.
 * It's expected that this plugin is used mainly for debugging
 * purposes or when analysing data and as such doesn't impact the production
 * acquisition. When closing the file, file is switched to non-blocking
 * mode before. That is because kernel buffers are sometimes only synchronized
 * when the file is closed. It was observed to take a long time, but there's
 * not much we could do in that case. So the idea is not to wait for close
 * to complete.
 *
 * Available DumpPlugin parameters (in addition to ones from BasePlugin):
 * asyn param    | asyn param type | init val | mode | Description                   |
 * ------------- | --------------- | -------- | ---- | ------------------------------
 * FilePath      | asynOctet       | ""       | RW   | Absolute file path to write to
 */
class DumpPlugin : public BasePlugin {
    private: // variables
        int m_fd;           //!< File handle for an opened file, or -1

    public: // functions
        /**
         * Constructor
         *
         * Only initialize parameters in constructor, don't open the file.
         *
	     * @param[in] portName asyn port name.
	     * @param[in] dispatcherPortName Name of the dispatcher asyn port to connect to.
         * @param[in] blocking Flag whether the processing should be done in the context of caller thread or in background thread.
         */
        DumpPlugin(const char *portName, const char *dispatcherPortName, int blocking);

        /**
         * Destructor
         *
         * Closes the dump file to make sure unsynced data get flushed to filesystem.
         */
        ~DumpPlugin();

        /**
         * Overloaded asynInt32 write handler
         *
         * Overload behavior of Enable parameter. When plugin gets enabled, open the
         * file if path is set. When disabled, close the file descriptor in non-blocking
         * way - don't wait for data synchronization to complete.
         */
        asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

        /**
         * Overloaded asynOctet write handler
         *
         * When new file path is set and the plugin is enabled, the old file is closed
         * and new path is opened. If plugin is not enabled, remember the file path but
         * only open the file when plugin gets enabled.
         */
        asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual);

        /**
         * Overloaded function to receive all OCC data.
         */
        void processData(const DasPacketList * const packetList);

    private: // functions
        /**
         * Switch to new dump file.
         *
         * If for some reason new file can not be opened, previous state
         * remains in effect - old file, if any, remains the one to be written to.
         *
         * @param[in] path Absolute file path of a new file.
         * @return true on success, false otherwise.
         */
        bool openFile(const std::string &path);

        /**
         * Close file handle and disable further writes.
         *
         * Switch file synchronization mode to non-blocking, then close the file and
         * let the OS complete data synchronization.
         */
        void closeFile();

    private: // asyn parameters
        #define FIRST_DUMPPLUGIN_PARAM FilePath
        int FilePath;       //!< Absolute dump file path
        #define LAST_DUMPPLUGIN_PARAM FilePath
};

#endif // DUMP_PLUGIN_H
