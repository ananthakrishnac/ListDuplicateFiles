#pragma once

#include <string>
#include <vector>
#include <ctime>

struct FileMetadata {
    std::string filename;
    std::string fullPath;
    std::string extension;
    long long fileSize;
    std::time_t timestamp;
    std::string fileType;
    int imageWidth = 0;
    int imageHeight = 0;
    std::string attributes;
    std::string createdDate;
    std::string modifiedDate;
    std::string hashVal;  // SHA256 hash based on filename, extension, size, dimensions

    FileMetadata() : fileSize(0), timestamp(0) {}
};
