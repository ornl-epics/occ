#ifndef DUMP_PLUGIN_H
#define DUMP_PLUGIN_H

#include "BasePlugin.h"

#include <string>

/**
 * Dummy dump plugin writes all received OCC data into a file.
 *
 * The plugin provides a FilePath parameter which can be changed
 * through parameters. Whenever changed, DumpPlugin will switch
 * over to new file and close the old one.
 *
 * DumpPlugin is not registered to OCC data by default and must
 * be explicitly enabled through Enable parameter.
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
         * Overloaded asynOctet write handler
         *
         * When FilePath is written to, this function will call openFile() and return
         * asynSuccess on success.
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
         */
        void closeFile();

    private: // asyn parameters
        #define FIRST_DUMPPLUGIN_PARAM FilePath
        int FilePath;       //!< Absolute dump file path
        #define LAST_DUMPPLUGIN_PARAM FilePath
};

#endif // DUMP_PLUGIN_H
