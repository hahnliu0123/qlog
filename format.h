#pragma once

#include "timestamp.h"
#include "log_msg.h"

#include <unistd.h>
#include <functional>
#include <memory>

namespace qlog {

namespace detail {


} // namespace detail


class LogFormatter {
public:
    static LogFormatter* GetInstance() {
        static LogFormatter formatter;
        return &formatter;
    }
    static void SetFormat(const std::string& fmt) {
        LogFormatter::GetInstance()->setFormat(fmt);
    }

    class FmtItem { 
    public:
        typedef std::shared_ptr<FmtItem> SPtr;
        explicit FmtItem(const std::string& str = "") {}
        virtual ~FmtItem() {}
        virtual void format(LogMsg& msg) = 0;
    };

    void setFormat(const std::string& fmt) {
        pattern_ = fmt;
        init();
    }
    
    const std::vector<FmtItem::SPtr> getFormatItem() const { return items_; }
    
private:
    LogFormatter(const std::string& pattern = "%time %file:%line %func() "): pattern_(pattern) {
        init();
    }
    ~LogFormatter() = default;

    void init();

private:
    std::string pattern_;
    std::vector<FmtItem::SPtr> items_;  
};


} // namespace qlog