#pragma once

#include "FileMetadata.h"
#include <vector>
#include <string>
#include <set>
#include <functional>

class FileScanner {
public:
    FileScanner();
    ~FileScanner();

    // Callback for progress updates - receives current directory path
    using ProgressCallback = std::function<void(const std::string&)>;

    // Callback for batch results - called every N entries to stream results without accumulating all in memory
    using ResultsBatchCallback = std::function<void(std::vector<FileMetadata>&)>;

    void SetProgressCallback(ProgressCallback callback) {
        progressCallback = callback;
    }

    void SetResultsBatchCallback(ResultsBatchCallback callback) {
        resultsBatchCallback = callback;
    }

    // Stop Scan Feature - Set external stop flag for early termination
    void SetStopFlag(volatile bool* stopFlag) {
        this->stopFlag = stopFlag;
    }

    std::vector<FileMetadata> ScanDirectory(const std::string& folderPath,
                                            const std::vector<std::string>& extensions,
                                            bool includeSubdirectories = true);

    // Stream-based scan: returns results in batches via callback to avoid large memory accumulation
    // Returns total number of files found
    int ScanDirectoryStreaming(const std::string& folderPath,
                               const std::vector<std::string>& extensions,
                               bool includeSubdirectories = true);

    void AddFileType(const std::string& extension);
    void RemoveFileType(const std::string& extension);
    void ClearFileTypes();

    static FileMetadata GetFileMetadata(const std::string& filePath);
    static std::string GetFileType(const std::string& extension);
    static bool GetImageDimensions(const std::string& imagePath, int& width, int& height);

private:
    std::set<std::string> supportedExtensions;
    ProgressCallback progressCallback;
    ResultsBatchCallback resultsBatchCallback;  // Callback to stream batches without accumulating all in memory
    volatile bool* stopFlag = nullptr;  // Pointer to external stop flag for early termination

    // Internal recursive method that doesn't track memory (to avoid duplicate tracking)
    std::vector<FileMetadata> ScanDirectoryInternal(const std::string& folderPath,
                                                   const std::vector<std::string>& extensions,
                                                   bool includeSubdirectories);

    // Internal streaming method - calls callback for every file found instead of accumulating
    void ScanDirectoryInternalStreaming(const std::string& folderPath,
                                       const std::vector<std::string>& extensions,
                                       bool includeSubdirectories,
                                       std::vector<FileMetadata>& currentBatch,
                                       size_t& totalCount);

    bool MatchesFilter(const std::string& filename, const std::vector<std::string>& extensions);
    std::string GetFileTypeFromExtension(const std::string& extension);
};
