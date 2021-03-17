#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>
#include <inttypes.h>
#include <cstring>
#include <memory>
#include <stdarg.h>
#include <map>
#include <atomic>
#include <unordered_map>
#include <iostream>
#include <assert.h>


#include "timestamp.h"
#include "appender.h"
#include "util.h"

#define QLOG_FMT(level) \
    if (qlog::QLog::GetLevel() <= level)  qlog::LogMsg(level, __FILE__, __FUNCTION__, __LINE__)


#define QLOG_TRACE QLOG_FMT(qlog::LogLevel::TRACE)
#define QLOG_DEBUG QLOG_FMT(qlog::LogLevel::DEBUG)
#define QLOG_INFO QLOG_FMT(qlog::LogLevel::INFO)
#define QLOG_ERROR QLOG_FMT(qlog::LogLevel::ERROR)
#define QLOG_WARN QLOG_FMT(qlog::LogLevel::WARN)
#define QLOG_FATAL QLOG_FMT(qlog::LogLevel::FATAL)

namespace qlog {


enum class LogLevel : uint8_t {
    TRACE = 0,
    DEBUG,
    INFO,
    ERROR,
    WARN,
    FATAL,
};

const char* LogLevelToString(LogLevel level);

// designed like linux kernel's kfifo.
class RingBuffer {
public:
    RingBuffer()
        : write_pos_(0), read_pos_(0), last_log_pos_(0) {
        buf_ = (char*)malloc(kBufferSize);    
        assert(buf_);
    }

    ~RingBuffer() {
        free(buf_);
    }

    uint32_t offsetOfPos(uint32_t pos) const { return pos & (kBufferSize - 1); }

    uint32_t getUsedBytes() const {
        std::atomic_thread_fence(std::memory_order_acquire);
        return write_pos_ - read_pos_;
    }

    uint32_t getUnusedBytes() const { return kBufferSize - getUsedBytes(); }

    uint32_t canRead() const {
        std::atomic_thread_fence(std::memory_order_acquire);
        return last_log_pos_ - read_pos_;
    }

    // call by ~LogMsg() to update complete log len.
    void incementLastLogPos(uint32_t n) {
        last_log_pos_ += n;
        std::atomic_thread_fence(std::memory_order_release);
    }

    uint32_t read(char* to, uint32_t n);
    void write(const char* from, uint32_t n);

private:
    static const uint32_t kBufferSize = 1 * 1024 * 1024; // 1M
    uint32_t write_pos_;
    uint32_t read_pos_;
    uint32_t last_log_pos_;
    char* buf_{nullptr};
};

static std::unordered_map<std::string, LogAppender*>
    g_appender_map = {
        {"console", ConsoleAppender::GetInstance()},
        {"file", FileAppender::GetInstance()}
    };


class QLog {
public:
    // QLog only has a static instance.
    static QLog* GetInstance() {
        static QLog s_qlog;
        return &s_qlog;
    }
    static void SetLevel(LogLevel level) {
        QLog::GetInstance()->setLevel(level);
    }
    static LogLevel GetLevel() {
        return QLog::GetInstance()->getLevel();
    }
    static void SetAppender(const std::string& name) {
        if (g_appender_map.find(name) != g_appender_map.end()) {
            QLog::GetInstance()->setAppender(g_appender_map[name]);
        }
    }
    static LogAppender* GetAppender() {
        return QLog::GetInstance()->getAppender();
    }
    static std::string PrintAppender() {
        auto appender = QLog::GetAppender();
        if (appender == ConsoleAppender::GetInstance()) {
            return "console";
        } else if (appender == FileAppender::GetInstance()) {
            return "file";
        }
        return "";
    }

private:
    QLog();
    ~QLog();
    QLog(const QLog&) = delete;
    QLog& operator=(const QLog&) = delete;

public:
    LogLevel getLevel() const { return level_; };
    void setLevel(LogLevel level) { level_ = level; }
    LogAppender* getAppender() { return appender_; }
    void setAppender(LogAppender* appender) { appender_ = appender; }

    void write(const char* data, uint32_t n) {
        buffer()->write(data, static_cast<uint32_t>(n));
    }

    void incementLastLogPos(uint32_t n) {
        buffer()->incementLastLogPos(n);
    }

    int64_t getSinkBytes() { return total_read_bytes_; }

private:
    // get all thread buffer to log thread buffer.
    void sinkThreadFunc();

    RingBuffer* buffer();

private:
    LogLevel level_;
    LogAppender* appender_;

    bool prepare_exit_;
    bool output_full_;
    bool thread_exit_;

    char* output_buf_;
    char* double_buf_;
    uint32_t buffer_size_;
    uint32_t total_read_bytes_;
    uint32_t per_read_bytes_;

    std::vector<RingBuffer*> thread_buffers_;
    std::thread sink_thread_;
    std::mutex buffer_mtx_;
    std::mutex cond_mtx_;

    // for safety exit.
    std::condition_variable empty_;
    std::condition_variable process_;
};


} // namespace qlog
