#include <epicsUnitTest.h>
#include <epicsAlgorithm.h>
#include <testMain.h>
#include <CircularBuffer.h>

#include <math.h>
#include <string.h>

static const uint32_t UNIT_SIZE = 4;

#define TEST_OK     1
#define TEST_FAIL   0
#define min(a,b) epicsMin(a,b)
#define max(a,b) epicsMax(a,b)

class CircularBufferNonprotected : public CircularBuffer {
    public:
        static uint32_t _align(uint32_t value, uint8_t base) { return CircularBuffer::_align(value,  base); }
        static uint32_t _alignDown(uint32_t value, uint8_t base) { return CircularBuffer::_alignDown(value, base); };
};

int CreateDestroy()
{
    CircularBuffer *buf;

    buf = new CircularBuffer(1024);
    delete buf;

    buf = new CircularBuffer(1997);
    delete buf;

    buf = new CircularBuffer(1 * 1024 * 1024);
    delete buf;

    return TEST_OK;
}

int PushConsume(uint32_t bufSize, uint32_t pushSize1, uint32_t consumeSize1=0, uint32_t pushSize2=0, uint32_t consumeSize2=0, uint32_t pushSize3=0)
{
    CircularBuffer buf(bufSize);
    char data[max(max(pushSize1, pushSize2), pushSize3)];
    uint32_t expected;
    uint32_t free = bufSize - UNIT_SIZE;

    expected = CircularBufferNonprotected::_alignDown(min(free, pushSize1), UNIT_SIZE);
    if (buf.push(data, pushSize1) != expected) return TEST_FAIL;
    free -= expected;

    if (consumeSize1 > 0) {
        buf.consume(consumeSize1);
        free += consumeSize1;
    }

    if (pushSize2 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize2), UNIT_SIZE);
        if (buf.push(data, pushSize2) != expected) return TEST_FAIL;
        free -= expected;
    }

    if (consumeSize2 > 0) {
        buf.consume(consumeSize2);
        free += consumeSize2;
    }

    if (pushSize3 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize3), UNIT_SIZE);
        if (buf.push(data, pushSize3) != expected) return TEST_FAIL;
    }

    return TEST_OK;
}

int Wait(uint32_t bufSize, uint32_t pushSize1, uint32_t consumeSize1=0, uint32_t pushSize2=0, uint32_t consumeSize2=0, uint32_t pushSize3=0)
{
    CircularBuffer buf(bufSize);
    char data[max(max(pushSize1, pushSize2), pushSize3)];
    uint32_t expected;
    void *ptr;
    uint32_t len;
    uint32_t free = bufSize - UNIT_SIZE;

    len = 0;
    expected = CircularBufferNonprotected::_alignDown(min(free, pushSize1), UNIT_SIZE);
    /*ignore*/buf.push(data, pushSize1); // Not testing push here
    free -= expected;
    buf.wait(&ptr, &len);
    if (len != expected) return TEST_FAIL;
    if (memcmp(data, ptr, len) != 0) return TEST_FAIL;

    if (consumeSize1 > 0) {
        buf.consume(consumeSize1);
        free += consumeSize1;
        if (free > 0) {
            expected -= consumeSize1;
            buf.wait(&ptr, &len);
            if (len != expected) return TEST_FAIL;
        }
    }

    if (pushSize2 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize2), UNIT_SIZE);
        /*ignore*/buf.push(data, pushSize2);
        free -= expected;
        buf.wait(&ptr, &len);
        if (len != (bufSize - UNIT_SIZE - free)) return TEST_FAIL;
    }

    return TEST_OK;
}

int PushOverBoundary() {
    CircularBuffer buf(256);
    char data[256];
    char *ptr;
    uint32_t len;

    if (buf.push(data, 256) != (256-UNIT_SIZE))
        return TEST_FAIL;
    buf.consume(256-UNIT_SIZE); // should be empty now

    strcpy(data, "ThisDataRollsOver"); // UNIT_SIZE=8 => "ThisData" goes at the end of buffer, "RollOver" rolls over
    buf.push(data, 48);

    // Now we're at the 248-byte offset, where the "This..." should be
    buf.wait((void **)&ptr, &len);
    if (len != 48)
        return TEST_FAIL;
    if (strncmp(ptr, "ThisDataRollsOver", strlen("ThisDataRollsOver")) != 0) {
        ptr[strlen("ThisDataRollsOver")] = '\0';
        testDiag("Expecting 'ThisDataRollsOver', got '%s'", ptr);
        return TEST_FAIL;
    }

    return TEST_OK;
}

int Bug_BigDataDontRollover() {
    // m_buffer         = 0x7FFFF4A5F010
    // m_rolloverSize   = 14400
    // m_size           = 1000000
    // m_producer       = 10864
    // m_consumer       = 10872
    // wait() was setting len = 967288 (correct len = m_size - m_consumer = 989128)
    CircularBuffer buf(1000000);
    char data[1000000];
    void *ptr;
    uint32_t len;
    if (buf.push(data, 1000000-UNIT_SIZE) != (1000000-UNIT_SIZE))
        return TEST_FAIL;
    buf.consume(10872);
    if (buf.push(data, 10864) != 10864)
        return TEST_FAIL;
    buf.wait(&ptr, &len);
    if (len != 989128) {
        testDiag("bufsize=1000000 prod=10864 cons=10872 => avail=989128 but got %d", len);
        return TEST_FAIL;
    }
    return TEST_OK;
}

int Bug_WaitConsidersProducerOnRollover() {
    // On rollover and using the temporary rollover buffer,
    // the amount of data copied into rollover buffer should not go
    // beyond producer.
    CircularBuffer buf(128);
    char data[128];
    char *ptr;
    uint32_t len;

    memset(data, '0', sizeof(data));
    buf.push(data, sizeof(data));
    buf.consume(128-UNIT_SIZE);

    memset(data, '1', sizeof(data));
    buf.push(data, 16); // 8 bytes at the end, 8 bytes roll over
    buf.wait((void **)&ptr, &len);
    // the rollover buffer could accomodate more than 16 bytes - that's how much we pushed
    // but it really shouldn't
    if (len != 16) {
        testDiag("expected 16 bytes, got %d bytes", len);
        return TEST_FAIL;
    }
    if (strncmp(ptr, "1111111111111111", 16) !=0) {
        testDiag("rollover data CRC error");
        return TEST_FAIL;
    }

    return TEST_OK;
}

int Bug_ConsumingEntireBuffer() {
    // The bug in consume() function was that the number of used bytes
    // was not calculated correctly due to unsigned nature of producer and
    // consumer. Substracting those two could return negative result, in which
    // case the modulus was not aligning it properly.
    // m_producer = 16
    // m_consumer = 24
    // m_size     = 48
    // used = (m_producer - m_consumer) % m_size; // <= this was returning 8, but the expected value is 40
    CircularBuffer buf(48);
    char data[48];

    buf.push(data, 40);
    buf.consume(24);
    buf.push(data, 24);
    // Now there's 40 bytes of data in the buffer, let's consume it all at once
    buf.consume(40);
    // ... and all the data should be gone. The bug was that m_consumer would
    // be set to 32 before, so still some data in buffer.
    return (buf.empty() ? TEST_OK : TEST_FAIL);
}

MAIN(mathTest)
{
    uint32_t bufSize, pushSize1, pushSize2, pushSize3, consumeSize1, consumeSize2;

    testPlan(46);

    testDiag("CircularBuffer constructor & destructor");
    testOk(CreateDestroy(), "Create & destroy");

    testDiag("CircularBuffer::_align() tests");
    testOk(CircularBufferNonprotected::_align(0,  8) == 0,  "_align(0,  8) == 0");
    testOk(CircularBufferNonprotected::_align(1,  8) == 8,  "_align(1,  8) == 8");
    testOk(CircularBufferNonprotected::_align(7,  8) == 8,  "_align(7,  8) == 8");
    testOk(CircularBufferNonprotected::_align(8,  8) == 8,  "_align(8,  8) == 8");
    testOk(CircularBufferNonprotected::_align(9,  8) == 16, "_align(9,  8) == 16");
    testOk(CircularBufferNonprotected::_align(15, 8) == 16, "_align(15, 8) == 16");
    testOk(CircularBufferNonprotected::_align(16, 8)==  16, "_align(16, 8) == 16");

    testDiag("CircularBuffer::_alignDown() tests");
    testOk(CircularBufferNonprotected::_alignDown(0,  8) == 0,  "_alignDown(0,  8) == 0");
    testOk(CircularBufferNonprotected::_alignDown(1,  8) == 0,  "_alignDown(1,  8) == 0");
    testOk(CircularBufferNonprotected::_alignDown(7,  8) == 0,  "_alignDown(7,  8) == 0");
    testOk(CircularBufferNonprotected::_alignDown(8,  8) == 8,  "_alignDown(8,  8) == 8");
    testOk(CircularBufferNonprotected::_alignDown(9,  8) == 8,  "_alignDown(9,  8) == 8");
    testOk(CircularBufferNonprotected::_alignDown(15, 8) == 8,  "_alignDown(15, 8) == 8");
    testOk(CircularBufferNonprotected::_alignDown(16, 8)==  16, "_alignDown(16, 8) == 16");

    testDiag("Single CircularBuffer::push() boundaries tests");
    testOk1(PushConsume(bufSize=128, pushSize1=10));
    testOk1(PushConsume(bufSize=128, pushSize1=120));
    testOk1(PushConsume(bufSize=128, pushSize1=122));
    testOk1(PushConsume(bufSize=128, pushSize1=127));
    testOk1(PushConsume(bufSize=128, pushSize1=128));
    testOk1(PushConsume(bufSize=128, pushSize1=256));

    testDiag("Multiple CircularBuffer::push() boundaries tests");
    testOk1(PushConsume(bufSize=128, pushSize1=112, consumeSize1=0, pushSize2=8));
    testOk1(PushConsume(bufSize=128, pushSize1=112, consumeSize1=0, pushSize2=10));
    testOk1(PushConsume(bufSize=128, pushSize1=112, consumeSize1=0, pushSize2=16));
    testOk1(PushConsume(bufSize=128, pushSize1=112, consumeSize1=0, pushSize2=24));
    testOk1(PushConsume(bufSize=128, pushSize1=128, consumeSize1=0, pushSize2=24));

    testDiag("Rollover CircularBuffer::push() boundaries tests");
    testOk1(PushConsume(bufSize=128, pushSize1=112, consumeSize1=64, pushSize2=56));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=56));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=64));

    testDiag("Complex rollover CircularBuffer::push() boundaries tests");
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=16, consumeSize2=32));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=16, consumeSize2=40));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=16, consumeSize2=48));

    testDiag("CircularBuffer::consume() tests");
    testOk1(PushConsume(bufSize=128, pushSize1=64,  consumeSize1=64,  pushSize2=120));
    testOk1(PushConsume(bufSize=128, pushSize1=64,  consumeSize1=40,  pushSize2=0,  consumeSize2=24, pushSize3=120));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=112, pushSize2=24, consumeSize2=32, pushSize3=120));
    testOk1(PushConsume(bufSize=128, pushSize1=120, consumeSize1=120, pushSize2=24, consumeSize2=24, pushSize3=120));

    testDiag("CircularBuffer::wait() tests");
    testOk1(Wait(bufSize=128, pushSize1=64));
    testOk1(Wait(bufSize=128, pushSize1=128));
    testOk1(Wait(bufSize=128, pushSize1=64,  consumeSize1=24));
    testOk1(Wait(bufSize=128, pushSize1=64,  consumeSize1=0,  pushSize2=24));
    testOk1(Wait(bufSize=128, pushSize1=64,  consumeSize1=0,  pushSize2=64));
    testOk1(Wait(bufSize=128, pushSize1=120, consumeSize1=64, pushSize2=64));

    testDiag("Regression tests");
    testOk1(Bug_BigDataDontRollover());
    testOk1(PushOverBoundary());
    testOk1(Bug_WaitConsidersProducerOnRollover());
    testOk1(Bug_ConsumingEntireBuffer());

    return testDone();
}
