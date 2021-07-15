/* proxy.cpp
 *
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec
 */

/**
 * OCC proxy application is a utility for simple communication with OCC device.
 * It reads data from OCC and writes it to stdout. Similarly data from stdin is
 * sent to OCC. An option for listening on TCP socket was added.
 */

/*
 * Copyright (c) 2018 Oak Ridge National Laboratory.
 * All rights reserved.
 * See file LICENSE that is included with this distribution.
 *
 * @author Klemen Vodopivec <vodopiveck@ornl.gov>
 */

#include <occlib.h>

#include <arpa/inet.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/types.h>
#include <signal.h>
#include <stdexcept>
#include <string.h> // strerror
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static const int TIMEOUT = 10;  // Timeout for OCC and stdin
static bool running = true;     // SigHandler will flip it to false

static void usage(const char *progname) {
    using namespace std;
    cout << "Usage: " << progname << " [OPTION] <DEVICE FILE>" << endl;
    cout << endl;
    cout << "Utility for simple communication with OCC device from command line." << endl;
    cout << "Data from OCC is written to stdout and data on stdin is sent to OCC." << endl;
    cout << "There's also an option to open a TCP server socket and push data" << endl;
    cout << "through that channel, since we need the transfers to be packet" << endl;
    cout << "oriented, eg. when new client connects start with packet address" << endl;
    cout << "instead of random buffer location." << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  -o, --old-packets    Force SNS DAS 1.0 packets" << endl;
    cout << "  -p, --port <PORT>    Establish TCP server and push data through socket instead" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  * save OCC output to file: " << progname << " /dev/occ1 > /tmp/occ.raw" << endl;
    cout << "  * create TCP proxy on port 2000: " << progname << " -p 2000 /dev/occ1" << endl;
    cout << endl;
}

bool parseArgs(int argc, char **argv, std::string &devFile, bool &oldPackets, uint16_t &port) {
    oldPackets = false;
    devFile.clear();
    port = 0;

    for (int i = 1; i < argc; i++) {
        std::string key(argv[i]);

        if (key == "-h" || key == "--help")
            return false;
        else if (key == "-o" || key == "--old-packets") {
            oldPackets = true;
        }
        else if (key == "-p" || key == "--port") {
            if (++i >= argc) {
                return false;
            }
            unsigned long port_ = std::stoul(argv[i]);
            if (port_ > 65535) {
                return false;
            }
            port = port_;
        }
        else if (key[0] != '-') {
            devFile = argv[i];
        }
    }

    if (devFile.empty())
        return false;

    return true;
}

static void sigHandler(int signal) {
    switch (signal) {
    case SIGTERM:
    case SIGINT:
        running = false;
        break;
    default:
        break;
    }
}

class FileIO {
    public:
        int m_writeFile;
        int m_readFile;
        bool m_oldPackets;
        bool m_eof;

        FileIO()
        : m_writeFile(fileno(stdout))
        , m_readFile(fileno(stdin))
        , m_oldPackets(false)
        , m_eof(false)
        {}


        virtual void handleError() {
            throw std::runtime_error("Can't recover from stdout/stdin error");
        }

        void enableOldPackets() {
            m_oldPackets = true;
        }

        virtual bool eof() {
            return m_eof;
        }

        virtual void write(const char *data, size_t size) {
            while (size > 0) {
                // Use poll() to avoid busy waiting
                struct pollfd pollfd;
                pollfd.fd = m_writeFile;
                pollfd.events = POLLOUT;

                switch (::poll(&pollfd, 1, TIMEOUT)) {
                case -1:
                    handleError();
                    break;
                case 1:
                    if (pollfd.revents & POLLOUT) {
                        int ret = ::write(m_writeFile, data, size);
                        if (ret == -1) {
                            std::cerr << "ERROR: Failed to write to file: " << strerror(errno) << std::endl;
                            handleError();
                            break;
                        }
                        size -= ret;
                        data += ret;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        virtual bool read(char *data, size_t size) {
            bool readsome = false;

            while (size > 0) {
                // Use poll() to avoid busy waiting
                struct pollfd pollfd;
                pollfd.fd = m_readFile;
                pollfd.events = POLLIN;

                switch (::poll(&pollfd, 1, TIMEOUT)) {
                case -1:
                    if (errno == EINTR) {
                        return false;
                    }
                    std::cerr << "Failed to read from file: " << strerror(errno) << std::endl;
                    handleError();
                    return false;
                case 1:
                    if (pollfd.revents & POLLIN) {
                        int ret = ::read(m_readFile, data, size);
                        if (ret <= 0) {
                            if (ret == -1) {
                                std::cerr << "Failed to read from file: " << strerror(errno) << std::endl;
                            } else {
                                m_eof = true;
                            }
                            handleError();
                            return false;
                        }
                        data += ret;
                        size -= ret;
                        readsome = true;
                        break;
                    }
                    // fall-thru
                default:
                    // Bail out if timeout or poll error and no previous data was read
                    if (readsome == false) {
                        return false;
                    }

                    break;
                }
            }
            return true;
        }

        size_t readPacket(char *data, size_t size) {
            size_t len;
            size_t offset;

            if (m_oldPackets) {
                if (size < 24) {
                    throw std::runtime_error("Incoming packet bigger than buffer");
                }

                if (!read(data, 24)) {
                    return 0;
                }
                len = reinterpret_cast<uint32_t *>(data)[3] + 24;
                offset = 24;
            } else {
                if (size < 8) {
                    throw std::runtime_error("Incoming packet bigger than buffer");
                }

                if (!read(data, 8)) {
                    return 0;
                }
                len = reinterpret_cast<uint32_t *>(data)[1];
                offset = 8;
            }

            if (len > size) {
                std::cerr << "ERROR: Incoming packet bigger than buffer" << std::endl;
                handleError();
                return 0;
            }
            if (len < offset) {
                std::cerr << "ERROR: Invalid packet based on length" << std::endl;
                handleError();
                return 0;
            }
            if (!read(&data[offset], len-offset)) {
                std::cerr << "ERROR: Failed to read complete packet" << std::endl;
                handleError();
                return 0;
            }
            return len;
        }
};

class TcpSocket : public FileIO {
    private:
        int m_listenSock;
    public:
        TcpSocket() {
            m_readFile = m_writeFile = -1;
            m_listenSock = -1;
        }

        void handleError() {
            if (m_writeFile != -1) {
                ::close(m_writeFile);
                m_writeFile = -1;
            }
            if (m_readFile != -1) {
                ::close(m_readFile);
                m_readFile = -1;
            }
        }

        bool eof() {
            return false;
        }

        void listen(uint16_t port) {
            struct sockaddr_in address;
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port);

            int sock = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sock == -1) {
                throw std::runtime_error(std::string("Failed to create socket") + strerror(errno));
            }

            int optval = 1;
            if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval) != 0) {
                throw std::runtime_error(std::string("Failed to set socket parameters") + strerror(errno));
            }


            if (::bind(sock, (struct sockaddr *)&address, sizeof(struct sockaddr)) != 0) {
                close(sock);
                throw std::runtime_error(std::string("Failed to bind socket") + strerror(errno));
            }

            if (::listen(sock, 1) != 0) {
                close(sock);
                throw std::runtime_error(std::string("Failed to listen to socket") + strerror(errno));
            }

            if (m_listenSock != -1) {
                ::close(m_listenSock);
            }
            m_listenSock = sock;
        }

        bool connectClient(int timeout) {
            if (m_listenSock != -1) {
                struct pollfd pollfd;
                pollfd.fd = m_listenSock;
                pollfd.events = POLLIN;
                if (poll(&pollfd, 1, timeout) == 1 && pollfd.revents & POLLIN) {
                    int addrlen;
                    m_readFile = m_writeFile = ::accept(m_listenSock, 0, (socklen_t *)&addrlen);
                }
            }
            return (m_readFile != -1 && m_writeFile != -1);
        }

        void write(const char *data, size_t size) {
            if (m_readFile == -1 || m_writeFile == -1) {
                if (!connectClient(0))
                    return;
            }
            FileIO::write(data, size);
        }

        bool read(char *data, size_t size) {
            if (m_readFile == -1 || m_writeFile == -1) {
                if (!connectClient(0))
                    return false;
            }
            return FileIO::read(data, size);
        }
};


class OccHandler {
    private:
        std::unique_ptr<struct occ_handle, decltype(&occ_close)> m_occ;
        std::vector<char> m_buffer;
        static const size_t BUFFER_SIZE = 10 * 1024;
        bool m_oldPackets;
        FileIO *m_fileIO;

    public:
        OccHandler(const std::string &devFile, bool oldPackets, FileIO *fileIO)
        : m_occ(NULL, &occ_close)
        , m_fileIO(fileIO)
        {
            struct occ_handle *_occ;
            int ret;
            if ((ret = occ_open(devFile.c_str(), OCC_INTERFACE_OPTICAL, &_occ)) != 0) {
                throw std::runtime_error(std::string("Failed to initialize OCC interface: ") + strerror(-ret));
            }
            m_occ = std::unique_ptr<struct occ_handle, decltype(&occ_close)>(_occ, &occ_close);

            // Initialize OCC parameters
            if ((ret = occ_enable_old_packets(m_occ.get(), oldPackets)) != 0) {
                m_occ.reset();
                throw std::runtime_error(std::string("Failed to enable old DAS packets: ") + strerror(-ret));
            }
            if ((ret = occ_enable_rx(m_occ.get(), true)) != 0) {
                m_occ.reset();
                throw std::runtime_error(std::string("Failed to enable RX: ") + strerror(-ret));
            }

            m_buffer.reserve(BUFFER_SIZE);
            if (oldPackets)
                m_fileIO->enableOldPackets();
        }

        void transferFromOcc() {
            void *addr;
            size_t count;

            // Poll OCC for some data and write it to stdout
            int ret = occ_data_wait(m_occ.get(), &addr, &count, TIMEOUT);
            if (ret == 0) {
                m_fileIO->write(reinterpret_cast<char *>(addr), count);
                occ_data_ack(m_occ.get(), count);
            } else {
                if (ret != -ENODATA && ret != -ETIME && ret != -EINTR) {
                    throw std::runtime_error(std::string("Can not read from OCC: ") + strerror(-ret));
                }
            }
        }

        void transferToOcc() {
            size_t size = m_fileIO->readPacket(m_buffer.data(), BUFFER_SIZE);
            if (size > 0) {
                int ret = occ_send(m_occ.get(), m_buffer.data(), size);
                if (ret < 0) {
                    throw std::runtime_error(std::string("Failed to write data to OCC: ") + strerror(-ret));
                } else if (static_cast<size_t>(ret) != size) {
                    std::cerr << "Wrote " << ret << " of " << size << " bytes" << std::endl;
                    throw std::runtime_error("Failed to write data to OCC");
                }
            }
        }
};

int main(int argc, char **argv) {
    bool oldPackets = false;
    std::string devFile;
    uint16_t port = 0;
    FileIO *fileIO;

    if (!parseArgs(argc, argv, devFile, oldPackets, port)) {
        usage(argv[0]);
        return 1;
    }

    if (port > 0) {
        try {
            TcpSocket *tcpSocket = new TcpSocket;
            tcpSocket->listen(port);
            fileIO = tcpSocket;
        } catch (std::runtime_error &e) {
            std::cerr << "ERROR: " << e.what() << std::endl;
            return 1;
        }
    } else {
        fileIO = new FileIO;
    }

    // Setup sighandler
    static struct sigaction sigact;
    sigact.sa_handler = &sigHandler;
    sigact.sa_flags = 0;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGTERM, &sigact, NULL);
    sigaction(SIGINT, &sigact, NULL);

    try {
        unsigned dice = 0;
        OccHandler occ(devFile, oldPackets, fileIO);
        while (running) {
            if ((dice++ % 2) == 0) {
                occ.transferFromOcc();
            } else {
                occ.transferToOcc();
            }
        }
    } catch (std::runtime_error &e) {
        if (!fileIO->eof()) {
            std::cerr << "ERROR: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
