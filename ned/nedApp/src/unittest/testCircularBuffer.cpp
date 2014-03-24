#include <epicsUnitTest.h>
#include <epicsAlgorithm.h>
#include <testMain.h>
#include <CircularBuffer.h>

#include <math.h>
#include <string.h>

static const uint32_t UNIT_SIZE = 8;

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

    return 1;
}

int PushConsume(uint32_t bufSize, uint32_t pushSize1, uint32_t consumeSize1=0, uint32_t pushSize2=0, uint32_t consumeSize2=0, uint32_t pushSize3=0)
{
    CircularBuffer buf(bufSize);
    char data[max(max(pushSize1, pushSize2), pushSize3)];
    uint32_t expected;
    uint32_t free = bufSize - UNIT_SIZE;

    expected = CircularBufferNonprotected::_alignDown(min(free, pushSize1), UNIT_SIZE);
    if (buf.push(data, pushSize1) != expected) return 0;
    free -= expected;

    if (consumeSize1 > 0) {
        buf.consume(consumeSize1);
        free += consumeSize1;
    }

    if (pushSize2 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize2), UNIT_SIZE);
        if (buf.push(data, pushSize2) != expected) return 0;
        free -= expected;
    }

    if (consumeSize2 > 0) {
        buf.consume(consumeSize2);
        free += consumeSize2;
    }

    if (pushSize3 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize3), UNIT_SIZE);
        if (buf.push(data, pushSize3) != expected) return 0;
    }

    return 1;
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
    if (len != expected) return 0;
    if (memcmp(data, ptr, len) != 0) return 0;

    if (consumeSize1 > 0) {
        buf.consume(consumeSize1);
        free += consumeSize1;
        if (free > 0) {
            expected -= consumeSize1;
            buf.wait(&ptr, &len);
            if (len != expected) return 0;
        }
    }

    if (pushSize2 > 0) {
        expected = CircularBufferNonprotected::_alignDown(min(free, pushSize2), UNIT_SIZE);
        /*ignore*/buf.push(data, pushSize2);
        free -= expected;
        buf.wait(&ptr, &len);
        if (len != (bufSize - UNIT_SIZE - free)) return 0;
    }

    return 1;
}

MAIN(mathTest)
{
    uint32_t bufSize, pushSize1, pushSize2, pushSize3, consumeSize1, consumeSize2;

    testPlan(42);

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

    return testDone();
}
