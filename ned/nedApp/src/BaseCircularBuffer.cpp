#include "BaseCircularBuffer.hpp"

using namespace std;

BaseCircularBuffer::BaseCircularBuffer()
    , m_consumer(0)
    , m_producer(UNIT_SIZE)
{}

BaseCircularBuffer::~BaseCircularBuffer()
{}
