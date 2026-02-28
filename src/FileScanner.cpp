#include "..\include\FileScanner.h"
#include "..\include\Logger.h"
#include "..\include\HashGenerator.h"
#include "..\include\MemoryTracker.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <ctime>
#include <cstring>
#include <sstream>

// Suppress STL WCHAR conversion warning from xutility
#pragma warning(disable: 4244)

FileScanner::FileScanner() : progressCallback(nullptr) {}

FileScanner::~FileScanner() {}

std::vector<FileMetadata> FileScanner::ScanDirectory(const std::string& folderPath,
                                                     const std::vector<std::string>& extensions,
                                                     bool includeSubdirectories) {
    LOG_DEBUG("ScanDirectory START - Path: " + folderPath);

    // Call internal method which does the actual work
    std::vector<FileMetadata> results = ScanDirectoryInternal(folderPath, extensions, includeSubdirectories);

    // Track total memory allocation only at top level (not in recursive calls)
    size_t vectorCapacity = results.capacity() * sizeof(FileMetadata);
    size_t estimatedStringData = results.size() * 600;  // ~600 bytes per FileMetadata strings
    size_t totalVectorMemory = vectorCapacity + estimatedStringData;
    MEMORY_TRACK_ALLOC("FileScanner::results", totalVectorMemory);
    LOG_DEBUG("ScanDirectory: Tracked FileScanner::results - " + std::to_string(totalVectorMemory / (1024*1024)) + " MB");

    LOG_DEBUG("ScanDirectory COMPLETE - Total Results: " + std::to_string(results.size()));
    return results;
}

std::vector<FileMetadata> FileScanner::ScanDirectoryInternal(const std::string& folderPath,
                                                            const std::vector<std::string>& extensions,
                                                            bool includeSubdirectories) {
    std::vector<FileMetadata> results;
    wchar_t* widePath = nullptr;
    wchar_t* searchPath = nullptr;
    HANDLE findHandle = INVALID_HANDLE_VALUE;

    LOG_DEBUG("ScanDirectoryInternal START - Path: " + folderPath);

    // Report current directory being scanned
    if (progressCallback) {
        progressCallback(folderPath);
    }

    try {
        if (folderPath.empty()) {
            LOG_WARNING("ScanDirectory - Empty folder path");
            return results;
        }

        // Convert std::string to wide string for Windows API
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            LOG_ERROR("MultiByteToWideChar failed at size calculation");
            return results;
        }

        widePath = new wchar_t[wideSize];
        if (!widePath) {
            LOG_ERROR("Failed to allocate memory for widePath");
            return results;
        }

        if (MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, widePath, wideSize) <= 0) {
            LOG_ERROR("MultiByteToWideChar failed at conversion");
            return results;
        }

        // Add UNC long path prefix if path is absolute and not already prefixed
        // This allows paths longer than MAX_PATH (260 chars)
        // Note: We need to account for the wildcard \* being added (2 chars), so check for >= 259
        size_t pathStrLen = wcslen(widePath);
        std::wstring longPathPrefix = L"";
        bool isUNCPath = (wcsncmp(widePath, L"\\\\", 2) == 0);
        bool hasPrefix = (wcsncmp(widePath, L"\\\\?\\", 4) == 0);

        if ((pathStrLen + 2) >= 260 && !isUNCPath && !hasPrefix) {
            // Path + wildcard will be >= 260, not a UNC path, and doesn't already have the prefix
            longPathPrefix = L"\\\\?\\";
        }

        if (pathStrLen > 200) {  // Only log very long paths
            LOG_DEBUG("Path length: " + std::to_string(pathStrLen) + ", With wildcard: " + std::to_string(pathStrLen + 2) + ", IsUNC: " + (isUNCPath ? std::string("Yes") : std::string("No")) + ", HasPrefix: " + (hasPrefix ? std::string("Yes") : std::string("No")) + ", Adding prefix: " + (longPathPrefix.empty() ? std::string("No") : std::string("Yes")) + " - Path: " + folderPath);
        }

        // Add wildcard for searching - use dynamic allocation for deep paths
        // Fix for: Buffer overflow when path exceeds MAX_PATH (260 chars)
        size_t pathLen = longPathPrefix.length() + pathStrLen + 3;  // +2 for \*, +1 for null terminator
        searchPath = new wchar_t[pathLen];
        if (!searchPath) {
            LOG_ERROR("Failed to allocate memory for searchPath");
            delete[] widePath;
            return results;
        }

        if (wcscpy_s(searchPath, pathLen, longPathPrefix.c_str()) != 0) {
            LOG_ERROR("wcscpy_s failed for searchPath with prefix");
            delete[] widePath;
            delete[] searchPath;
            return results;
        }
        if (wcscat_s(searchPath, pathLen, widePath) != 0) {
            LOG_ERROR("wcscat_s failed adding widePath");
            delete[] widePath;
            delete[] searchPath;
            return results;
        }
        if (wcscat_s(searchPath, pathLen, L"\\*") != 0) {
            LOG_ERROR("wcscat_s failed adding wildcard");
            delete[] widePath;
            delete[] searchPath;
            return results;
        }

        // Convert search path to string for logging (skip \\?\ prefix if present for readability)
        wchar_t* logPath = searchPath;
        if (wcsncmp(searchPath, L"\\\\?\\", 4) == 0) {
            logPath = searchPath + 4;  // Skip the \\?\ prefix for logging
        }

        int searchPathSize = WideCharToMultiByte(CP_UTF8, 0, logPath, -1, nullptr, 0, nullptr, nullptr);
        std::string searchPathStr;
        if (searchPathSize > 0) {
            searchPathStr.resize(searchPathSize - 1);
            WideCharToMultiByte(CP_UTF8, 0, logPath, -1, &searchPathStr[0], searchPathSize, nullptr, nullptr);
        }

        LOG_DEBUG("SearchPath: " + searchPathStr);

        WIN32_FIND_DATAW findData = {};
        HANDLE findHandle = FindFirstFileW(searchPath, &findData);  // Use original searchPath with prefix

        if (findHandle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            LOG_ERROR("FindFirstFileW failed with error: " + std::to_string(err) + " - SearchPath: " + searchPathStr + " - OriginalPath: " + folderPath);
            delete[] widePath;
            delete[] searchPath;
            return results;
        }

        int fileCount = 0;
        int errorCount = 0;

        do {
            // Stop Scan Feature - Check if scan should be terminated
            if (stopFlag && !*stopFlag) {
                LOG_INFO("Stopped by user - breaking directory loop at " + std::to_string(fileCount) + " files");
                break;  // Exit directory scan loop immediately
            }

            // Skip "." and ".."
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            try {
                // Build full path safely - use dynamic allocation for deep paths
                size_t fullPathLen = wcslen(widePath) + wcslen(findData.cFileName) + 2 + 1;  // +2 for \, +1 for null
                wchar_t* fullPath = new wchar_t[fullPathLen];
                if (!fullPath) {
                    LOG_WARNING("Failed to allocate memory for fullPath");
                    errorCount++;
                    continue;
                }

                if (wcscpy_s(fullPath, fullPathLen, widePath) != 0) {
                    LOG_WARNING("wcscpy_s failed for fullPath");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }
                if (wcscat_s(fullPath, fullPathLen, L"\\") != 0) {
                    LOG_WARNING("wcscat_s failed adding backslash");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }
                if (wcscat_s(fullPath, fullPathLen, findData.cFileName) != 0) {
                    LOG_WARNING("wcscat_s failed adding filename");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }

                // Convert wide string to regular string safely
                // Skip \\?\ prefix if present (don't include it in the path string)
                wchar_t* pathToConvert = fullPath;
                if (wcsncmp(fullPath, L"\\\\?\\", 4) == 0) {
                    pathToConvert = fullPath + 4;  // Skip the \\?\ prefix
                }

                int size = WideCharToMultiByte(CP_UTF8, 0, pathToConvert, -1, nullptr, 0, nullptr, nullptr);
                if (size <= 0) {
                    LOG_WARNING("WideCharToMultiByte size calculation failed");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }

                std::string fullPathStr(size - 1, 0);
                if (WideCharToMultiByte(CP_UTF8, 0, pathToConvert, -1, &fullPathStr[0], size, nullptr, nullptr) <= 0) {
                    LOG_WARNING("WideCharToMultiByte conversion failed");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }

                // Convert filename to regular string safely
                int filenameSize = WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, nullptr, 0, nullptr, nullptr);
                if (filenameSize <= 0) {
                    LOG_WARNING("Filename WideCharToMultiByte size calculation failed");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }

                std::string filename(filenameSize - 1, 0);
                if (WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, &filename[0], filenameSize, nullptr, nullptr) <= 0) {
                    LOG_WARNING("Filename WideCharToMultiByte conversion failed");
                    delete[] fullPath;
                    errorCount++;
                    continue;
                }

                LOG_DEBUG("Processing item: " + filename);

                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    LOG_DEBUG("Found directory: " + filename);
                    // It's a directory
                    if (includeSubdirectories) {
                        // Recursively scan subdirectory using internal method (no memory tracking)
                        try {
                            LOG_DEBUG("Recursing into: " + fullPathStr);
                            std::vector<FileMetadata> subResults = ScanDirectoryInternal(fullPathStr, extensions, true);
                            results.insert(results.end(), subResults.begin(), subResults.end());
                            LOG_DEBUG("Returned from: " + fullPathStr);
                        } catch (const std::exception& ex) {
                            LOG_ERROR("Exception in subdirectory scan: " + std::string(ex.what()) + " Path: " + fullPathStr);
                            errorCount++;
                        } catch (...) {
                            LOG_ERROR("Unknown exception in subdirectory scan for: " + fullPathStr);
                            errorCount++;
                        }
                    }
                } else {
                    LOG_DEBUG("Found file: " + filename);
                    // It's a file - check if it matches filter
                    if (MatchesFilter(filename, extensions)) {
                        try {
                            LOG_DEBUG("Adding file to results: " + fullPathStr);
                            FileMetadata metadata = GetFileMetadata(fullPathStr);
                            results.push_back(metadata);
                            fileCount++;
                        } catch (const std::exception& ex) {
                            LOG_ERROR("Exception getting file metadata: " + std::string(ex.what()) + " File: " + fullPathStr);
                            errorCount++;
                        } catch (...) {
                            LOG_ERROR("Unknown exception getting file metadata for: " + fullPathStr);
                            errorCount++;
                        }
                    }
                }

                // Clean up fullPath after processing
                delete[] fullPath;
            } catch (const std::exception& ex) {
                LOG_ERROR("Exception in main loop: " + std::string(ex.what()));
                errorCount++;
            } catch (...) {
                LOG_ERROR("Unknown exception in main loop");
                errorCount++;
            }

        } while (FindNextFileW(findHandle, &findData));

        LOG_INFO("ScanDirectoryInternal COMPLETE - Files: " + std::to_string(fileCount) + ", Errors: " + std::to_string(errorCount) + ", Total Results: " + std::to_string(results.size()));

    } catch (const std::exception& ex) {
        LOG_ERROR("ScanDirectoryInternal outer exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("ScanDirectoryInternal unknown outer exception");
    }

    // Cleanup - safe to call on nullptr
    if (findHandle != INVALID_HANDLE_VALUE) {
        FindClose(findHandle);
    }
    if (widePath) {
        delete[] widePath;
    }
    if (searchPath) {
        delete[] searchPath;
    }

    return results;
}

bool FileScanner::MatchesFilter(const std::string& filename,
                               const std::vector<std::string>& extensions) {
    if (extensions.empty()) {
        return true;
    }

    // Extract extension from filename
    size_t dotPos = filename.rfind('.');
    if (dotPos == std::string::npos) {
        return false; // No extension
    }

    std::string fileExt = filename.substr(dotPos);

    // Convert to lowercase for comparison
    std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& ext : extensions) {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // Add dot if not present
        if (lowerExt[0] != '.') {
            lowerExt = "." + lowerExt;
        }

        if (fileExt == lowerExt) {
            return true;
        }
    }

    return false;
}

FileMetadata FileScanner::GetFileMetadata(const std::string& filePath) {
    LOG_DEBUG("GetFileMetadata START - File: " + filePath);

    FileMetadata metadata;
    metadata.fullPath = filePath;

    try {
        // Extract filename from path
        size_t lastSlash = filePath.rfind('\\');
        if (lastSlash != std::string::npos) {
            metadata.filename = filePath.substr(lastSlash + 1);
        } else {
            metadata.filename = filePath;
        }

        // Extract extension
        size_t dotPos = metadata.filename.rfind('.');
        if (dotPos != std::string::npos) {
            metadata.extension = metadata.filename.substr(dotPos);
        }

        metadata.fileType = GetFileType(metadata.extension);

        // Convert UTF-8 path to wide string for CreateFileW
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            LOG_WARNING("MultiByteToWideChar failed for file path conversion");
            return metadata;
        }

        // Add UNC long path prefix if path exceeds MAX_PATH (260 chars)
        std::wstring longPathPrefix = L"";
        if (wideSize > 260 && filePath[0] != '\\') {
            // Path is too long and not a UNC path, add \\?\ prefix
            longPathPrefix = L"\\\\?\\";
        }

        wchar_t* widePath = new wchar_t[longPathPrefix.length() + wideSize];
        if (!widePath) {
            LOG_ERROR("Failed to allocate memory for widePath in GetFileMetadata");
            return metadata;
        }

        // Copy prefix first if present
        if (!longPathPrefix.empty()) {
            wcscpy_s(widePath, longPathPrefix.length() + 1, longPathPrefix.c_str());
        } else {
            widePath[0] = L'\0';
        }

        if (MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, widePath + longPathPrefix.length(), wideSize) <= 0) {
            LOG_WARNING("MultiByteToWideChar conversion failed for file path");
            delete[] widePath;
            return metadata;
        }

        // Get file attributes first (doesn't require opening the file)
        DWORD fileAttr = GetFileAttributesW(widePath);
        if (fileAttr != INVALID_FILE_ATTRIBUTES) {
            metadata.attributes = "ReadOnly: ";
            metadata.attributes += (fileAttr & FILE_ATTRIBUTE_READONLY) ? "Yes" : "No";
            LOG_DEBUG("File attributes: " + metadata.attributes);
        } else {
            LOG_WARNING("GetFileAttributesW failed");
            delete[] widePath;
            return metadata;
        }

        // Get file info using Windows API (use CreateFileW for proper Unicode support)
        HANDLE fileHandle = CreateFileW(widePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (fileHandle != INVALID_HANDLE_VALUE) {
            LOG_DEBUG("File handle opened successfully");

            // Get file size
            LARGE_INTEGER fileSize;
            if (GetFileSizeEx(fileHandle, &fileSize)) {
                metadata.fileSize = fileSize.QuadPart;
                LOG_DEBUG("File size: " + std::to_string(fileSize.QuadPart));
            } else {
                LOG_WARNING("GetFileSizeEx failed");
            }

            // Get file time
            FILETIME lastWriteTime;
            if (GetFileTime(fileHandle, nullptr, nullptr, &lastWriteTime)) {
                SYSTEMTIME systemTime;
                FileTimeToSystemTime(&lastWriteTime, &systemTime);

                struct tm timeinfo = {};
                timeinfo.tm_year = systemTime.wYear - 1900;
                timeinfo.tm_mon = systemTime.wMonth - 1;
                timeinfo.tm_mday = systemTime.wDay;
                timeinfo.tm_hour = systemTime.wHour;
                timeinfo.tm_min = systemTime.wMinute;
                timeinfo.tm_sec = systemTime.wSecond;

                metadata.timestamp = mktime(&timeinfo);
                LOG_DEBUG("Timestamp extracted");
            } else {
                LOG_WARNING("GetFileTime failed");
            }

            CloseHandle(fileHandle);
        } else {
            DWORD err = GetLastError();
            LOG_WARNING("CreateFileW failed with error: " + std::to_string(err) + " - Path: " + filePath);
        }

        delete[] widePath;

        // Check if image and get dimensions
        if (metadata.fileType == "Image") {
            GetImageDimensions(filePath, metadata.imageWidth, metadata.imageHeight);
        }

        // Compute hash based on filename, extension, size, and image dimensions
        metadata.hashVal = HashGenerator::GenerateFileHash(metadata.filename, metadata.extension,
                                                           metadata.fileSize, metadata.imageWidth,
                                                           metadata.imageHeight);
        LOG_DEBUG("File hash computed: " + metadata.hashVal);

        LOG_DEBUG("GetFileMetadata COMPLETE - File: " + filePath);

    } catch (const std::exception& ex) {
        LOG_ERROR("GetFileMetadata exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("GetFileMetadata unknown exception");
    }

    return metadata;
}

std::string FileScanner::GetFileType(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Image types
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
        ext == ".bmp" || ext == ".tiff" || ext == ".webp") {
        return "Image";
    }
    // Document types
    else if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".txt" ||
             ext == ".xls" || ext == ".xlsx" || ext == ".ppt" || ext == ".pptx") {
        return "Document";
    }
    // Video types
    else if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov" ||
             ext == ".wmv" || ext == ".flv") {
        return "Video";
    }
    // Audio types
    else if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".aac" ||
             ext == ".m4a" || ext == ".wma") {
        return "Audio";
    }
    // Executable types
    else if (ext == ".exe" || ext == ".dll" || ext == ".sys" || ext == ".msi") {
        return "Executable";
    }
    else {
        return "Other";
    }
}

bool FileScanner::GetImageDimensions(const std::string& imagePath, int& width, int& height) {
    // Simplified approach - just detect if it's an image
    // In a production app, you'd use libraries like FreeImage or DirectX
    width = 0;
    height = 0;
    return false;
}

void FileScanner::AddFileType(const std::string& extension) {
    supportedExtensions.insert(extension);
}

void FileScanner::RemoveFileType(const std::string& extension) {
    supportedExtensions.erase(extension);
}

void FileScanner::ClearFileTypes() {
    supportedExtensions.clear();
}

// Stream-based scan: processes files with progressive callbacks during scan to avoid peak memory accumulation
int FileScanner::ScanDirectoryStreaming(const std::string& folderPath,
                                        const std::vector<std::string>& extensions,
                                        bool includeSubdirectories) {
    LOG_INFO("ScanDirectoryStreaming START - Path: " + folderPath);

    if (!resultsBatchCallback) {
        LOG_ERROR("ScanDirectoryStreaming - No results callback registered");
        return 0;
    }

    std::vector<FileMetadata> batch;
    const size_t BATCH_SIZE = 100;
    size_t totalCount = 0;

    // Call streaming internal scan which yields results during traversal
    try {
        ScanDirectoryInternalStreaming(folderPath, extensions, includeSubdirectories, batch, totalCount);

        // Send any remaining files in final batch
        if (!batch.empty()) {
            LOG_DEBUG("ScanDirectoryStreaming: Yielding final batch of " + std::to_string(batch.size()) + " files");
            resultsBatchCallback(batch);
            batch.clear();
        }

        LOG_INFO("ScanDirectoryStreaming COMPLETE - Total: " + std::to_string(totalCount) + " files");
    } catch (const std::exception& ex) {
        LOG_ERROR("ScanDirectoryStreaming exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("ScanDirectoryStreaming unknown exception");
    }

    return static_cast<int>(totalCount);
}

// Internal streaming scan - yields batches via callback during directory traversal (minimal memory footprint)
void FileScanner::ScanDirectoryInternalStreaming(const std::string& folderPath,
                                                const std::vector<std::string>& extensions,
                                                bool includeSubdirectories,
                                                std::vector<FileMetadata>& batch,
                                                size_t& totalCount) {
    LOG_DEBUG("ScanDirectoryInternalStreaming START - Path: " + folderPath);
    LOG_DEBUG("ScanDirectoryInternalStreaming - Input path length: " + std::to_string(folderPath.length()));

    const size_t BATCH_SIZE = 100;
    wchar_t* widePath = nullptr;
    wchar_t* searchPath = nullptr;
    HANDLE findHandle = INVALID_HANDLE_VALUE;

    try {
        if (folderPath.empty()) {
            LOG_WARNING("ScanDirectoryInternalStreaming - Empty folder path");
            return;
        }

        // Report current directory being scanned
        if (progressCallback) {
            progressCallback(folderPath);
        }

        // Convert to wide string
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, nullptr, 0);
        if (wideSize <= 0) {
            LOG_ERROR("MultiByteToWideChar failed for streaming scan");
            return;
        }

        widePath = new wchar_t[wideSize];
        if (!widePath) {
            LOG_ERROR("Failed to allocate memory for widePath in streaming scan");
            return;
        }

        if (MultiByteToWideChar(CP_UTF8, 0, folderPath.c_str(), -1, widePath, wideSize) <= 0) {
            LOG_ERROR("MultiByteToWideChar conversion failed for streaming scan");
            delete[] widePath;
            return;
        }

        // Add UNC long path prefix if path is absolute and not already prefixed
        // This allows paths longer than MAX_PATH (260 chars)
        // Note: We need to account for the wildcard \* being added (2 chars), so check for >= 259
        size_t pathStrLen = wcslen(widePath);
        std::wstring longPathPrefix = L"";
        bool isUNCPath = (wcsncmp(widePath, L"\\\\", 2) == 0);
        bool hasPrefix = (wcsncmp(widePath, L"\\\\?\\", 4) == 0);

        if ((pathStrLen + 2) >= 260 && !isUNCPath && !hasPrefix) {
            // Path + wildcard will be >= 260, not a UNC path, and doesn't already have the prefix
            longPathPrefix = L"\\\\?\\";
        }

        if (pathStrLen > 200) {  // Only log very long paths
            LOG_DEBUG("Streaming - LONG PATH - Input path length: " + std::to_string(pathStrLen) + ", With wildcard: " + std::to_string(pathStrLen + 2) + ", IsUNC: " + (isUNCPath ? std::string("Yes") : std::string("No")) + ", HasPrefix: " + (hasPrefix ? std::string("Yes") : std::string("No")) + ", Adding prefix: " + (longPathPrefix.empty() ? std::string("No") : std::string("Yes")) + " - Path: " + folderPath);
        }

        // Add wildcard for search
        size_t pathLen = longPathPrefix.length() + pathStrLen + 3;
        searchPath = new wchar_t[pathLen];
        if (!searchPath) {
            LOG_ERROR("Failed to allocate memory for searchPath in streaming scan");
            delete[] widePath;
            return;
        }

        if (wcscpy_s(searchPath, pathLen, longPathPrefix.c_str()) != 0 ||
            wcscat_s(searchPath, pathLen, widePath) != 0 ||
            wcscat_s(searchPath, pathLen, L"\\*") != 0) {
            LOG_ERROR("Path concatenation failed for streaming scan");
            delete[] widePath;
            delete[] searchPath;
            return;
        }

        // Convert search path to string for logging (skip \\?\ prefix if present for readability)
        wchar_t* logPath = searchPath;
        if (wcsncmp(searchPath, L"\\\\?\\", 4) == 0) {
            logPath = searchPath + 4;  // Skip the \\?\ prefix for logging
        }

        int searchPathSize = WideCharToMultiByte(CP_UTF8, 0, logPath, -1, nullptr, 0, nullptr, nullptr);
        std::string searchPathStr;
        if (searchPathSize > 0) {
            searchPathStr.resize(searchPathSize - 1);
            WideCharToMultiByte(CP_UTF8, 0, logPath, -1, &searchPathStr[0], searchPathSize, nullptr, nullptr);
        }

        LOG_DEBUG("SearchPath: " + searchPathStr);

        WIN32_FIND_DATAW findData = {};
        findHandle = FindFirstFileW(searchPath, &findData);  // Use original searchPath with prefix

        if (findHandle == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            // Error 5 = ACCESS_DENIED (skip with warning instead of error for protected system folders)
            if (err == 5) {
                LOG_WARNING("FindFirstFileW failed (Access Denied): " + std::to_string(err) + " - Path: " + folderPath);
            } else {
                LOG_ERROR("FindFirstFileW failed: " + std::to_string(err) + " - SearchPath: " + searchPathStr + " - OriginalPath: " + folderPath);
            }
            delete[] widePath;
            delete[] searchPath;
            return;
        }

        do {
            // Check stop flag
            if (stopFlag && !*stopFlag) {
                LOG_INFO("ScanDirectoryInternalStreaming stopped by user at " + std::to_string(totalCount) + " files");
                break;
            }

            // Skip "." and ".."
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0) {
                continue;
            }

            try {
                // Build full path
                size_t fullPathLen = wcslen(widePath) + wcslen(findData.cFileName) + 2 + 1;
                wchar_t* fullPath = new wchar_t[fullPathLen];
                if (!fullPath) {
                    LOG_WARNING("Failed to allocate fullPath in streaming scan");
                    continue;
                }

                if (wcscpy_s(fullPath, fullPathLen, widePath) != 0 ||
                    wcscat_s(fullPath, fullPathLen, L"\\") != 0 ||
                    wcscat_s(fullPath, fullPathLen, findData.cFileName) != 0) {
                    LOG_WARNING("Path construction failed in streaming scan");
                    delete[] fullPath;
                    continue;
                }

                // Convert to regular string
                // Skip \\?\ prefix if present (don't include it in the path string)
                wchar_t* pathToConvert = fullPath;
                if (wcsncmp(fullPath, L"\\\\?\\", 4) == 0) {
                    pathToConvert = fullPath + 4;  // Skip the \\?\ prefix
                }

                int size = WideCharToMultiByte(CP_UTF8, 0, pathToConvert, -1, nullptr, 0, nullptr, nullptr);
                if (size <= 0) {
                    LOG_WARNING("WideCharToMultiByte size calculation failed in streaming scan");
                    delete[] fullPath;
                    continue;
                }

                std::string fullPathStr(size - 1, 0);
                if (WideCharToMultiByte(CP_UTF8, 0, pathToConvert, -1, &fullPathStr[0], size, nullptr, nullptr) <= 0) {
                    LOG_WARNING("WideCharToMultiByte conversion failed in streaming scan");
                    delete[] fullPath;
                    continue;
                }

                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    // Recurse into subdirectory
                    if (includeSubdirectories) {
                        try {
                            ScanDirectoryInternalStreaming(fullPathStr, extensions, true, batch, totalCount);
                        } catch (const std::exception& ex) {
                            LOG_ERROR("Exception in subdirectory streaming: " + std::string(ex.what()));
                        }
                    }
                } else {
                    // It's a file - check if it matches filter
                    int filenameSize = WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, nullptr, 0, nullptr, nullptr);
                    if (filenameSize > 0) {
                        std::string filename(filenameSize - 1, 0);
                        if (WideCharToMultiByte(CP_UTF8, 0, findData.cFileName, -1, &filename[0], filenameSize, nullptr, nullptr) > 0) {
                            if (MatchesFilter(filename, extensions)) {
                                try {
                                    FileMetadata metadata = GetFileMetadata(fullPathStr);
                                    batch.push_back(metadata);
                                    totalCount++;

                                    // When batch reaches size, call callback and clear
                                    if (batch.size() >= BATCH_SIZE) {
                                        LOG_DEBUG("ScanDirectoryInternalStreaming: Yielding batch of " + std::to_string(batch.size()));
                                        resultsBatchCallback(batch);
                                        batch.clear();
                                    }
                                } catch (const std::exception& ex) {
                                    LOG_ERROR("Exception getting file metadata in streaming: " + std::string(ex.what()));
                                }
                            }
                        }
                    }
                }

                delete[] fullPath;
            } catch (const std::exception& ex) {
                LOG_ERROR("Exception in streaming loop: " + std::string(ex.what()));
            }

        } while (FindNextFileW(findHandle, &findData));

        LOG_INFO("ScanDirectoryInternalStreaming COMPLETE - Path: " + folderPath);

    } catch (const std::exception& ex) {
        LOG_ERROR("ScanDirectoryInternalStreaming outer exception: " + std::string(ex.what()));
    } catch (...) {
        LOG_ERROR("ScanDirectoryInternalStreaming unknown outer exception");
    }

    // Cleanup
    if (findHandle != INVALID_HANDLE_VALUE) {
        FindClose(findHandle);
    }
    if (widePath) {
        delete[] widePath;
    }
    if (searchPath) {
        delete[] searchPath;
    }
}
