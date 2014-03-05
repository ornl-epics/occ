#ifndef OCCADAPTER_HPP
#define OCCADAPTER_HPP

#include <stdint.h>
#include <string>

// Forward declarations
struct occ_handle;

class OccAdapter {
    public:
        OccAdapter(const std::string &devfile);
        ~OccAdapter();

        bool isPcie();
        void enablePcieGenerator(uint32_t rate);
    protected:
        struct occ_handle *m_occ;

        std::string occErrorString(int error);
};

#endif // OCCADAPTER_HPP
