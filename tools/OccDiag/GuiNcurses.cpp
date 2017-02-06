#include "Common.h"
#include "GuiNcurses.h"
#include "LabPacket.h"

#include <unistd.h>

#include <stdarg.h>
#include <stdexcept>
#include <ncurses.h>
#include <cstring>
#include <cinttypes>

#define log_ratelimit(period, ...) logRateLimit(__LINE__, period, __VA_ARGS__)

GuiNcurses::GuiNcurses(const char *occDevice, const std::map<uint32_t, uint32_t> &initRegisters, uint32_t statsInt)
    : m_occAdapter(occDevice, initRegisters)
    , m_runtime(0.0)
    , m_shutdown(false)
    , m_paused(false)
    , m_rxEnabled(false)
    , m_stopOnBad(false)
    , m_occOverflowed(false)
    , m_occStalled(false)
    , m_statsLogInt(statsInt > 0 ? statsInt : -60)

    , m_winConsole(9)
    , m_winData(9)
    , m_winRegisters(9)
    , m_winStats(0, 9)
{
    initscr();

    if (has_colors() == TRUE) {
        start_color();
        init_pair(TEXT_COLOR_WHITE,   COLOR_WHITE,   COLOR_BLACK);
        init_pair(TEXT_COLOR_RED,     COLOR_RED,     COLOR_BLACK);
        init_pair(TEXT_COLOR_CYAN,    COLOR_CYAN,    COLOR_BLACK);
        init_pair(TEXT_COLOR_YELLOW,  COLOR_YELLOW,  COLOR_BLACK);

        wbkgd(stdscr, COLOR_PAIR(TEXT_COLOR_WHITE));
    }

    noecho();
    cbreak(); // Line buffering disabled
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    wgetch(stdscr); // Must initialize, otherwise the screen flickers

    log("PacketAnalyzer started");
    m_winStats.show();
    m_winConsole.show();

    m_winHelp.setStatsLogInt(m_statsLogInt);
}

GuiNcurses::~GuiNcurses()
{
    endwin();

    printf("%s\n", getBriefStatus().c_str());
    std::vector<std::string> lines = m_winStats.generateReport();
    for (size_t i=0; i<lines.size(); i++) {
        printf("%s\n", lines[i].c_str());
    }
}

void GuiNcurses::run()
{
    // Talk directly to OCC to overcome generic log messages
    m_occAdapter.toggleRx(true);
    m_rxEnabled = true;

    while (!m_shutdown) {
        struct timespec t1, t2;
        double loopTime = 0.2; // GUI refresh rate

        m_cachedStats.clear();

        if (!m_rxEnabled || m_paused) {
            usleep(loopTime * 1e6);
        } else {

            clock_gettime(CLOCK_MONOTONIC, &t1);
            while (!Common::timeExpired(t1, loopTime)) {
                try {
                    m_occAdapter.process(m_cachedStats, m_stopOnBad, loopTime);
                } catch (std::bad_exception) {
                    if (m_stopOnBad) {
                        log("Encountered bad packet, pausing for inspection");
                        showDataWin();
                        pause(true);
                    }
                    break;
                } catch (std::runtime_error &e) {
                    log("ERROR: %s", e.what());
                    showDataWin();
                    toggleRx(false);
                    break;
                }
            }

            clock_gettime(CLOCK_MONOTONIC, &t2);
            m_runtime += Common::timeDiff(t2, t1);
        }

        m_winStats.setFooter( getBriefStatus() );
        m_winStats.update(m_cachedStats);
        m_winStats.redraw(true);
        input();
    }
}

std::string GuiNcurses::getBriefStatus()
{
    std::string str;

    char buffer[80];
    uint64_t runtime = m_runtime;
    unsigned seconds = runtime % 60;
    unsigned minutes = ((runtime - seconds) / 60 )% 60;
    unsigned hours   = (runtime / 3600);

    snprintf(buffer, sizeof(buffer), "[Run time: %02u:%02u:%02u]", hours, minutes, seconds);
    if (m_paused) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (now.tv_sec & 0x1) {
            size_t i = strlen(buffer);
            while (--i) {
                if (buffer[i] >= '0' && buffer[i] <= '9')
                    buffer[i] = ' ';
            }
        }
    }
    str = buffer;

    const void *dmaAddr;
    size_t dmaSize = 0;
    size_t dmaUsed = 0;
    bool overflow;
    bool stalled;
    m_occAdapter.getDmaInfo(&dmaAddr, dmaSize);
    try {
        m_occAdapter.getOccStatus(dmaUsed, stalled, overflow);

        if (dmaSize != 0) dmaSize /= 1048576;
        if (dmaUsed != 0) dmaUsed /= 1048576;
        snprintf(buffer, sizeof(buffer), "-[DMA usage: %u/%u MB]", (unsigned)dmaUsed, (unsigned)dmaSize);
        str += buffer;

        snprintf(buffer, sizeof(buffer), "-[Status: %s]", (stalled ? "stalled" : (overflow ? "overflow" : "OK")));
        str += buffer;

        if (overflow && !m_occOverflowed)
            log("Detected OCC FIFO overflow");
        if (stalled && !m_occStalled)
            log("Detected OCC stall, DMA usage %u/%u MB", (unsigned)dmaUsed, (unsigned)dmaSize);
        m_occStalled = stalled;
        m_occOverflowed = overflow;

        if (!stalled && !overflow && m_statsLogInt > 0) {
            char logmsg[128];
            WinStats::AnalyzeStats stats = m_winStats.getCombinedStats();
            std::string rate = WinStats::formatRate(stats.throughput, "B/s");
            snprintf(logmsg, sizeof(logmsg),
                     "Stats: D=%uMB r=%s g=%" PRIu64 " b=%" PRIu64,
                     (unsigned)dmaUsed, rate.c_str(), stats.good, stats.bad);
            log_ratelimit(m_statsLogInt, logmsg);
        }

    } catch (std::runtime_error &e) {
        str += " [No OCC info obtained]";
        log_ratelimit(5, "Failed to read OCC status");
    }
    return str.substr(0, 78);
}

void GuiNcurses::shutdown()
{
    m_shutdown = true;
    log("PacketAnalyzer stopping...");
}

void GuiNcurses::toggleRx(bool enable)
{
    if (enable != m_rxEnabled) {
        try {
            if (enable) {
                log("Resetting OCC to clear potential partial packet from previous disable");
                m_winStats.clear();
                m_occAdapter.reset();
                LabPacket::resetRamp();
                m_runtime = 0.0;
            }

            m_occAdapter.toggleRx(enable);
            m_rxEnabled = enable;

            if (enable)
                log("Continue - enabled OCC RX");
            else
                log("Stopped - disabled OCC RX");
        } catch (std::runtime_error &e) {
            log("ERROR: failed to %s RX - %s", (enable ? "disable" : "enable"), e.what());
        }
    }
}

void GuiNcurses::pause(bool pause_)
{
    // Prevent double logging
    if (pause_ != m_paused) {
        m_paused = pause_;
        if (pause_)
            log("Paused processing - DMA continues in background");
        else
            log("Unpaused processing");
    }
}

void GuiNcurses::showDataWin()
{
    // Calculate DMA address, size and bad packet offsets
    const void *dmaAddr;
    size_t dmaSize;
    m_occAdapter.getDmaInfo(&dmaAddr, dmaSize);
    if (m_cachedStats.lastPacketAddr < dmaAddr || m_cachedStats.lastPacketAddr >= ((const char *)dmaAddr + dmaSize)) {
        // Probably using boundary roll-over buffer
        dmaAddr = m_cachedStats.lastAddr;
        dmaSize = m_cachedStats.lastLen;
    }
    m_winData.setAddr(dmaAddr, dmaSize, m_cachedStats.lastPacketAddr, m_cachedStats.lastErrorAddr);

    m_winRegisters.hide();
    m_winConsole.hide();
    m_winData.show();
    m_winHelp.redraw();
}

void GuiNcurses::showConsoleWin()
{
    m_winRegisters.hide();
    m_winData.hide();
    m_winConsole.show();
    m_winHelp.redraw();
}

void GuiNcurses::showRegistersWin()
{
    try {
        m_winRegisters.setRegisters(m_occAdapter.getRegisters());
        m_winData.hide();
        m_winConsole.hide();
        m_winRegisters.show();
        m_winHelp.redraw();
    } catch (std::runtime_error &e) {
        log("ERROR: %s", e.what());
        showConsoleWin();
    }
}

void GuiNcurses::resetOcc()
{
    try {
        m_occAdapter.reset();
        if (m_rxEnabled)
            m_occAdapter.toggleRx(true);
        m_runtime = 0.0;
        LabPacket::resetRamp();
        log("OCC reset");
    } catch (std::runtime_error &e) {
        log("ERROR: %s", e.what());
    }
}

void GuiNcurses::input()
{
    switch (wgetch(stdscr)) {
    case 'b':
    case 'B':
        m_stopOnBad = !m_stopOnBad;
        m_winHelp.setStopOnBad(m_stopOnBad);
        m_winHelp.redraw(false);
        log("Stop on bad packet %s", (m_stopOnBad ? "enabled" : "disabled"));
        break;
    case 'c':
    case 'C':
        showConsoleWin();
        break;
    case 'd':
    case 'D':
        showDataWin();
        break;
    case 'h':
    case 'H':
        if (m_winHelp.isVisible()) {
            m_winHelp.hide();
            m_winConsole.redraw(true);
            m_winData.redraw(true);
            m_winRegisters.redraw(true);
        } else {
            m_winHelp.show();
        }
        break;
    case 'i':
    case 'I':
        showRegistersWin();
        break;
    case 'l':
    case 'L':
        m_statsLogInt *= -1;
        m_winHelp.setStatsLogInt(m_statsLogInt);
        m_winHelp.redraw(true);
        break;
    case 'p':
    case 'P':
        pause(!m_paused);
        break;
    case 'q':
    case 'Q':
        shutdown();
        break;
    case 'r':
    case 'R':
        resetOcc();
        m_winStats.clear();
        break;
    case 's':
    case 'S':
        toggleRx(!m_rxEnabled);
        break;
    case 't':
        log("testing");
        break;
    case KEY_UP:
        m_winData.moveDown();
        m_winData.redraw();
        m_winHelp.redraw();
        break;
    case KEY_DOWN:
        m_winData.moveUp();
        m_winData.redraw();
        m_winHelp.redraw();
        break;
    default:
        break;
    }

    // Eat rest of key presses, some keys are multi-character and can hog
    // this function
    while (wgetch(stdscr) != ERR);
}

void GuiNcurses::log(const char *format, ...)
{
    char buffer[128];
    char *ptr = buffer;
    size_t size = sizeof(buffer);

    // Start with timestamp
    time_t now = time(NULL);
    strftime(ptr, size, "[%F %T] ", localtime(&now));
    size -= strlen(ptr);
    ptr += strlen(ptr);

    // Append formated user message
    va_list arglist;
    va_start(arglist, format);
    vsnprintf(ptr, size, format, arglist);
    va_end(arglist);

    m_winConsole.append(buffer);
}

void GuiNcurses::logRateLimit(uint32_t id, uint32_t period, const char *format, ...)
{
    auto it = m_logRateLimitCache.find(id);
    time_t now = time(NULL);
    if (it == m_logRateLimitCache.end() || (it->second + period) <= now) {
        va_list arglist;
        va_start(arglist, format);
        log(format, arglist);
        va_end(arglist);

        m_logRateLimitCache[id] = now;
    }
}
