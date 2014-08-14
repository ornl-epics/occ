#ifndef NED_LOG_H
#define NED_LOG_H

#include <asynDriver.h>

#ifndef __GNU_C__
#define __PRETTY_FUNCTION__ __func__
#endif
#define LOG_ERROR(text, ...) asynPrint(this->pasynUserSelf, ASYN_TRACE_ERROR, "%s:%d(%s) ERROR: " text "\n", __PRETTY_FUNCTION__, __LINE__, this->portName, ##__VA_ARGS__)
#define LOG_WARN(text, ...)  asynPrint(this->pasynUserSelf, ASYN_TRACE_WARNING, "%s:%d(%s) WARN: " text "\n", __PRETTY_FUNCTION__, __LINE__, this->portName, ##__VA_ARGS__)
#define LOG_INFO(text, ...)  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%d(%s) INFO: " text "\n", __PRETTY_FUNCTION__, __LINE__, this->portName, ##__VA_ARGS__)
#define LOG_DEBUG(text, ...) asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, "%s:%d(%s) DEBUG: " text "\n", __PRETTY_FUNCTION__, __LINE__, this->portName, ##__VA_ARGS__)

#endif // NED_LOG_H
