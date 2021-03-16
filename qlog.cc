#include "qlog.h"
#include "format.h"

namespace qlog {

const char* LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::FATAL:
        return "FATAL";
    }
    return "UNKNOWN";
}

uint32_t RingBuffer::read(char* to, uint32_t n) {
    uint32_t readable = std::min(canRead(), n);

    uint32_t offset_to_end = std::min(readable, kBufferSize - offsetOfPos(read_pos_));
    memcpy(to, buf_ + offsetOfPos(read_pos_), offset_to_end);
    memcpy(to + offset_to_end, buf_, readable - offset_to_end);

    read_pos_ += readable;
    std::atomic_thread_fence(std::memory_order_release);

    return readable;
}

void RingBuffer::wrtie(const char* from, uint32_t n) {
    // while in call usleep in a coroutine, it will not 
    // sleep and switch thread context.
    while (getUnusedBytes() < n) {
        usleep(50);
    }

    uint32_t offset_to_end = std::min(n, kBufferSize - offsetOfPos(write_pos_));
    memcpy(buf_ + offsetOfPos(write_pos_), from, offset_to_end);
    memcpy(buf_, from + offset_to_end, n - offset_to_end);

    write_pos_ += n;
    std::atomic_thread_fence(std::memory_order_release);
}

QLog::QLog() 
    : level_(LogLevel::DEBUG),
      appender_(ConsoleAppender::GetInstance()),
      prepare_exit_(false),
      thread_exit_(false),
      output_buf_(nullptr),
      double_buf_(nullptr),
      buffer_size_(1 << 24), // 16 M
      total_read_bytes_(0),
      per_read_bytes_(0),
      thread_buffers_(),
      sink_thread_(),
      buffer_mtx_(),
      cond_mtx_(),
      empty_(),
      process_() {
        
    output_buf_ = static_cast<char *>(malloc(buffer_size_));
    double_buf_ = static_cast<char *>(malloc(buffer_size_));
    sink_thread_ = std::thread(&QLog::sinkThreadFunc, this);      
}

QLog::~QLog() {
    {
        // notify background log thread. Wait all
        // buffers' data sink to disk.
        std::unique_lock<std::mutex> lock(cond_mtx_);
        prepare_exit_ = true;
        process_.notify_all();
        empty_.wait(lock);
    }

    {
        // now log thread already read all logs, exit.
        std::lock_guard<std::mutex> lock(cond_mtx_);
        thread_exit_ = true;
        // log thread may find no data to sink and 
        // continue to slepp on process_ at tha last time.
        process_.notify_all();
    }

    if (sink_thread_.joinable()) {
        sink_thread_.join();
    }

    free(output_buf_);
    free(double_buf_);

    for (auto& buf : thread_buffers_) {
        free(buf);
    }

}

void QLog::sinkThreadFunc() {
    while (!thread_exit_) {

        // lock thread_buffers to avoid reading vector
        // while a new thread push back a new buffer.
        {
            std::lock_guard<std::mutex> lock(buffer_mtx_);
            uint32_t buffer_idx = 0;
            while (!thread_exit_ && !output_full_ && 
                   (buffer_idx < thread_buffers_.size())) {
                RingBuffer *buf = thread_buffers_[buffer_idx];
                uint32_t readable = buf->canRead();

                if (buffer_size_ - per_read_bytes_ < readable) {
                    output_full_ = true;
                    break;
                }

                if (readable > 0) {
                    uint32_t n = buf->read(output_buf_ + per_read_bytes_, readable);
                    per_read_bytes_ += n;
                }
                ++buffer_idx;
            }
        }

        // no data to sink, go to sleep.
        if (per_read_bytes_ == 0) {
            std::unique_lock<std::mutex> lock(cond_mtx_);

            // no data, be notifyed that process is comming
            // exit. Get last chance to see if still have data
            // come.
            if (prepare_exit_) {
                prepare_exit_ = false;
                continue;
            }

            // static object QLog will be destructed when main thread
            // exit. However, it should ensure all logs have benn sink
            // to disk. So only if background log thread find buffers
            // already empty and notify main thread can main thread
            // destruct Log object.
            empty_.notify_one();
            process_.wait_for(lock, std::chrono::microseconds(50));
        } else {
            appender_->write(output_buf_, per_read_bytes_);

            total_read_bytes_ += per_read_bytes_;
            per_read_bytes_ = 0;
            output_full_ = false;
        }
    }
}

RingBuffer* QLog::buffer() {
    static thread_local RingBuffer* buf = nullptr;
    if (!buf) {
        std::lock_guard<std::mutex> lock(buffer_mtx_);
        buf = static_cast<RingBuffer*>(malloc(sizeof(RingBuffer)));
        thread_buffers_.push_back(buf);
    }
    return buf;
}

} // namespace qlog