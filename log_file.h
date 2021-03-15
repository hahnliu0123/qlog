#pragma once

#include <unistd.h>
#include <string>
#include <cstring> // strerror
#include <iostream>
#include <algorithm>

#include "timestamp.h"

namespace qlog {

class File {
public:
    File(): fp_(nullptr){}
    ~File() {
        if (fp_) {
            fclose(fp_);
        }
    }

    void open(const std::string& file_name, bool truncate = false) {
        file_name_ = file_name;
        const char* mode = truncate ? "w+" : "a+";
        fp_ = fopen(file_name_.c_str(), mode);
        if (!fp_) {
            std::cerr << "open " << file_name_ << " error: " << strerror(errno) << std::endl;
            exit(-1);
        }
    }

    void reopen(bool truncate = false) {
        close();
        open(file_name_, truncate);
    }

    void close() {
        if (fp_) {
            fclose(fp_);
            fp_ = nullptr;
        }
    }

    void fflush() {
        ::fflush(fp_);
    }

    void write(const char* data, size_t len) {
        if (!fp_) {
            open(file_name_);
        }
        if (fwrite_unlocked(data, sizeof(char), len, fp_) != len) {
            std::cerr << "fail to write file " << file_name_ << " error: " << strerror(errno) << std::endl;
        }
    }

    File(const File&) = delete;
    File& operator=(const File&) = delete;

private:
    std::string file_name_;
    FILE* fp_;
};

class LogFile {
public:
    LogFile(const std::string& file_name, size_t max_file_size)
        : file_name_(file_name),
          max_file_size_(max_file_size) {
        // rotate();
    }

    void write(const char* data, size_t len) {
        
        if (read_size_ + len > max_file_size_) {
            rotate();
        }
        read_size_ += len;
        file_.write(data, len);
    }

    void setMaxFileSize(size_t millBytes) { max_file_size_ = millBytes * kBytesPerMb; }
    // TODO: no thread safe. each thread can invoke it by LogAppender instance.
    void setFileName(const std::string& file_name) { 
        file_name_ = file_name; 
        rotate();
    }


private:
    std::string getFileName() {
        std::string name = file_name_;
        name += "-";
        name += detail::TimeStamp::Now().dateTime();
        std::replace(name.begin(), name.end(), ' ', '-');
        name += ".log";
        return name;
    }

    void rotate() {
        std::cout << "rotate:" << file_name_ <<std::endl;
        read_size_ = 0;
        file_.close();
        file_.open(getFileName());
    }

private:
    static const size_t kBytesPerMb = 1024 * 1024;
    File file_;
    std::string file_name_;
    size_t read_size_;
    size_t max_file_size_;
};

} // qlog