#include "log_msg.h"
#include "format.h"

namespace qlog {

LogMsg::LogMsg(LogLevel level, const char* file, 
               const char* function, uint32_t line)
    : count_(0),level_(level), file_(file),  
      function_(function), line_(line) {

    auto items = LogFormatter::GetInstance()->getFormatItem();
    for (auto item : items) {
        item->format(*this);
    }
}

LogMsg::~LogMsg() {
    *this << "\n";
    QLog::GetInstance()->incementLastLogPos(count_);
}

void LogMsg::append(const char* data, size_t n) {
    QLog::GetInstance()->write(data, n);
    count_ += static_cast<uint32_t>(n);
}


} // namespace qlog