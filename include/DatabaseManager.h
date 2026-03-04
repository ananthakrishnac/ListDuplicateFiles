#pragma once

#include "FileMetadata.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <sqlite3.h>

class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool InitializeDatabase(const std::string& dbPath);
    bool CloseDatabase();

    bool InsertFileMetadata(const FileMetadata& metadata);
    bool InsertFileMetadataJSON(const FileMetadata& metadata);

    // Batch insert with deduplication - returns count of inserted records
    int InsertFileMetadataBatch(std::vector<FileMetadata>& batch);

    // Check if entry exists with same hash and path
    bool EntryExists(const std::string& hashVal, const std::string& fullPath);

    std::vector<FileMetadata> QueryAllFiles();
    std::vector<FileMetadata> QueryFilesByType(const std::string& fileType);
    std::vector<FileMetadata> QueryFilesByExtension(const std::string& extension);
    std::vector<FileMetadata> QueryLargeFiles(long long minSize);

    // Find duplicate files by hash value
    std::vector<std::vector<FileMetadata>> FindDuplicates();

    // Find duplicates with streaming callback - processes results in batches
    // Callback receives a batch of duplicate groups and is invoked multiple times
    void FindDuplicatesStreaming(std::function<void(std::vector<std::vector<FileMetadata>>&)> callback);

    bool CreateTables();
    bool ClearAllData();
    int GetTotalFileCount();

    // Public execute for PRAGMA commands and manual SQL
    bool ExecuteSQL(const std::string& sql);

private:
    sqlite3* db;
    bool isConnected;

    std::string EscapeSQL(const std::string& input);
};
