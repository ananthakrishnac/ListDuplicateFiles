#include "..\include\DatabaseManager.h"
#include "..\include\Logger.h"
#include "..\include\MemoryTracker.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <set>

DatabaseManager::DatabaseManager() : db(nullptr), isConnected(false) {}

DatabaseManager::~DatabaseManager() {
    CloseDatabase();
}

bool DatabaseManager::InitializeDatabase(const std::string& dbPath) {
    LOG_INFO("DatabaseManager::InitializeDatabase - Path: " + dbPath);

    int rc = sqlite3_open(dbPath.c_str(), &db);

    if (rc != SQLITE_OK) {
        LOG_ERROR("sqlite3_open failed with error code: " + std::to_string(rc));
        return false;
    }

    LOG_INFO("Database opened successfully");
    isConnected = true;

    // Set pragmas for reliability and performance
    LOG_DEBUG("Setting database pragmas for data integrity");
    sqlite3_exec(db, "PRAGMA synchronous = FULL;", nullptr, nullptr, nullptr);  // Ensure data is synced to disk
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);    // Write-Ahead Logging
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);     // Enable foreign keys
    LOG_DEBUG("Database pragmas set");

    bool tablesCreated = CreateTables();
    if (tablesCreated) {
        LOG_INFO("Database tables created successfully");
    } else {
        LOG_ERROR("Failed to create database tables");
    }

    return tablesCreated;
}

bool DatabaseManager::CloseDatabase() {
    LOG_DEBUG("CloseDatabase START");

    if (db != nullptr) {
        LOG_DEBUG("Closing database connection");
        int rc = sqlite3_close(db);
        if (rc != SQLITE_OK) {
            LOG_ERROR("Error closing database: " + std::to_string(rc));
        } else {
            LOG_DEBUG("Database closed successfully");
        }
        db = nullptr;
        isConnected = false;
    }

    LOG_DEBUG("CloseDatabase COMPLETE");
    return true;
}

bool DatabaseManager::CreateTables() {
    LOG_DEBUG("CreateTables START");

    if (!isConnected) {
        LOG_ERROR("CreateTables - Database not connected");
        return false;
    }

    // Create relational table for file metadata
    const char* sqlRelational = R"(
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filename TEXT NOT NULL,
            full_path TEXT UNIQUE NOT NULL,
            extension TEXT,
            file_size INTEGER,
            timestamp DATETIME,
            file_type TEXT,
            image_width INTEGER,
            image_height INTEGER,
            attributes TEXT,
            created_date DATETIME,
            modified_date DATETIME,
            hash_val TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    // Create JSON table for flexible NoSQL storage
    const char* sqlJSON = R"(
        CREATE TABLE IF NOT EXISTS files_json (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT UNIQUE NOT NULL,
            metadata JSON,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    // Create index for faster queries
    const char* sqlIndex = R"(
        CREATE INDEX IF NOT EXISTS idx_file_type ON files(file_type);
        CREATE INDEX IF NOT EXISTS idx_extension ON files(extension);
        CREATE INDEX IF NOT EXISTS idx_file_size ON files(file_size);
        CREATE INDEX IF NOT EXISTS idx_hash_val ON files(hash_val);
    )";

    char* errMsg = nullptr;

    LOG_DEBUG("Creating files table");
    if (sqlite3_exec(db, sqlRelational, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("Failed to create files table: " + std::string(errMsg ? errMsg : "unknown"));
        sqlite3_free(errMsg);
        return false;
    }
    LOG_DEBUG("Files table created");

    LOG_DEBUG("Creating files_json table");
    if (sqlite3_exec(db, sqlJSON, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("Failed to create files_json table: " + std::string(errMsg ? errMsg : "unknown"));
        sqlite3_free(errMsg);
        return false;
    }
    LOG_DEBUG("Files_json table created");

    LOG_DEBUG("Creating indexes");
    if (sqlite3_exec(db, sqlIndex, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOG_ERROR("Failed to create indexes: " + std::string(errMsg ? errMsg : "unknown"));
        sqlite3_free(errMsg);
        return false;
    }
    LOG_DEBUG("Indexes created");

    LOG_INFO("CreateTables COMPLETE");
    return true;
}

bool DatabaseManager::EntryExists(const std::string& hashVal, const std::string& fullPath) {
    if (!isConnected) {
        LOG_ERROR("EntryExists - Database not connected");
        return false;
    }

    if (hashVal.empty()) {
        LOG_DEBUG("EntryExists - Empty hash value, skipping check");
        return false;
    }

    const char* sql = "SELECT COUNT(*) FROM files WHERE hash_val = ? AND full_path = ?;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare EntryExists query");
        return false;
    }

    sqlite3_bind_text(stmt, 1, hashVal.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, fullPath.c_str(), -1, SQLITE_STATIC);

    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        exists = (count > 0);
        if (exists) {
            LOG_DEBUG("EntryExists - Entry already in database: " + fullPath);
        }
    }

    sqlite3_finalize(stmt);
    return exists;
}

int DatabaseManager::InsertFileMetadataBatch(std::vector<FileMetadata>& batch) {
    LOG_DEBUG("InsertFileMetadataBatch START - Batch size: " + std::to_string(batch.size()));

    if (!isConnected) {
        LOG_ERROR("InsertFileMetadataBatch - Database not connected");
        return 0;
    }

    if (batch.empty()) {
        LOG_DEBUG("InsertFileMetadataBatch - Empty batch");
        return 0;
    }

    // Track batch capacity (vector container + estimated string data)
    size_t batchVectorSize = batch.capacity() * sizeof(FileMetadata);
    size_t batchStringData = batch.size() * 600;  // ~600 bytes per entry for string content
    size_t totalBatchCapacity = batchVectorSize + batchStringData;
    MEMORY_TRACK_ALLOC("DatabaseManager::batch_processing", totalBatchCapacity);

    // Shrink batch to fit to save memory
    batch.shrink_to_fit();

    // Step 1: Query existing (hash, path) pairs from database
    std::set<std::pair<std::string, std::string>> existingEntries;
    // Note: set tracking is approximate - will add when entries inserted below

    // Build list with proper escaping
    std::ostringstream hashList;
    hashList << "(";
    for (size_t i = 0; i < batch.size(); ++i) {
        if (i > 0) hashList << ",";
        hashList << "'" << EscapeSQL(batch[i].hashVal) << "'";
    }
    hashList << ")";

    std::string querySql = "SELECT hash_val, full_path FROM files WHERE hash_val IN " + hashList.str() + ";";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, querySql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* hashVal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (hashVal && fullPath) {
                existingEntries.insert({hashVal, fullPath});
            }
        }
        sqlite3_finalize(stmt);
        stmt = nullptr;
    } else {
        LOG_ERROR("Failed to prepare batch query");
        MEMORY_TRACK_FREE("DatabaseManager::batch_processing", totalBatchCapacity);
        return 0;
    }

    LOG_DEBUG("InsertFileMetadataBatch - Found " + std::to_string(existingEntries.size()) + " existing entries");

    // Estimate existing entries memory (set with string pairs: ~100 bytes per entry average)
    size_t estimatedExistingEntriesMemory = existingEntries.size() * 100;
    MEMORY_TRACK_ALLOC("DatabaseManager::existingEntries_set", estimatedExistingEntriesMemory);

    // Step 3: Begin transaction
    ExecuteSQL("BEGIN TRANSACTION;");

    int insertedCount = 0;
    const char* sql = "INSERT OR REPLACE INTO files "
        "(filename, full_path, extension, file_size, timestamp, file_type, "
        "image_width, image_height, attributes, created_date, modified_date, hash_val) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    stmt = nullptr;
    // Step 2: Process each entry - skip duplicates during insert
    for (const auto& metadata : batch) {
        // Skip if already exists (duplicate detection)
        if (existingEntries.find({metadata.hashVal, metadata.fullPath}) != existingEntries.end()) {
            continue;
        }
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR("Failed to prepare batch insert statement");
            ExecuteSQL("ROLLBACK;");
            MEMORY_TRACK_FREE("DatabaseManager::batch_processing", totalBatchCapacity);
            MEMORY_TRACK_FREE("DatabaseManager::existingEntries_set", estimatedExistingEntriesMemory);
            return insertedCount;
        }

        if (!stmt) {
            LOG_ERROR("Statement pointer is null after prepare");
            ExecuteSQL("ROLLBACK;");
            MEMORY_TRACK_FREE("DatabaseManager::batch_processing", totalBatchCapacity);
            MEMORY_TRACK_FREE("DatabaseManager::existingEntries_set", estimatedExistingEntriesMemory);
            return insertedCount;
        }

        // Format timestamp
        char timeBuf[32];  // Increased buffer size for safety
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&metadata.timestamp));

        // Bind parameters
        sqlite3_bind_text(stmt, 1, metadata.filename.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, metadata.fullPath.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, metadata.extension.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, metadata.fileSize);
        sqlite3_bind_text(stmt, 5, timeBuf, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6, metadata.fileType.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 7, metadata.imageWidth);
        sqlite3_bind_int(stmt, 8, metadata.imageHeight);
        sqlite3_bind_text(stmt, 9, metadata.attributes.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 10, metadata.createdDate.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 11, metadata.modifiedDate.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 12, metadata.hashVal.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            insertedCount++;
        } else {
            LOG_ERROR("Failed to insert file: " + metadata.filename);
        }

        sqlite3_finalize(stmt);
    }

    // Also insert to JSON table (only for non-duplicates)
    const char* jsonSql = "INSERT OR REPLACE INTO files_json (file_path, metadata) VALUES (?, ?);";
    for (const auto& metadata : batch) {
        // Skip if already exists (duplicate detection)
        if (existingEntries.find({metadata.hashVal, metadata.fullPath}) != existingEntries.end()) {
            continue;
        }

        std::ostringstream jsonData;
        jsonData << "{"
                 << "\"filename\": \"" << metadata.filename << "\", "
                 << "\"extension\": \"" << metadata.extension << "\", "
                 << "\"fileSize\": " << metadata.fileSize << ", "
                 << "\"fileType\": \"" << metadata.fileType << "\", "
                 << "\"imageWidth\": " << metadata.imageWidth << ", "
                 << "\"imageHeight\": " << metadata.imageHeight << ", "
                 << "\"attributes\": \"" << metadata.attributes << "\", "
                 << "\"hashVal\": \"" << metadata.hashVal << "\""
                 << "}";

        stmt = nullptr;
        if (sqlite3_prepare_v2(db, jsonSql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (stmt) {
                std::string jsonStr = jsonData.str();
                sqlite3_bind_text(stmt, 1, metadata.fullPath.c_str(), -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 2, jsonStr.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
                stmt = nullptr;
            }
        } else {
            LOG_ERROR("Failed to prepare JSON insert statement for: " + metadata.filename);
        }
    }

    // Step 4: Commit transaction
    ExecuteSQL("COMMIT;");

    // Clean up memory tracking
    batch.clear();
    batch.shrink_to_fit();
    MEMORY_TRACK_FREE("DatabaseManager::batch_processing", totalBatchCapacity);
    MEMORY_TRACK_FREE("DatabaseManager::existingEntries_set", estimatedExistingEntriesMemory);

    LOG_INFO("InsertFileMetadataBatch COMPLETE - Inserted " + std::to_string(insertedCount) + " records");
    return insertedCount;
}

bool DatabaseManager::InsertFileMetadata(const FileMetadata& metadata) {
    LOG_DEBUG("InsertFileMetadata - File: " + metadata.filename);

    if (!isConnected) {
        LOG_ERROR("InsertFileMetadata - Database not connected");
        return false;
    }

    // Check if entry with same hash and path already exists
    if (EntryExists(metadata.hashVal, metadata.fullPath)) {
        LOG_DEBUG("InsertFileMetadata - Skipping, already in database: " + metadata.filename);
        return true;  // Return true as this is not an error, just a duplicate scan
    }

    const char* sql = "INSERT OR REPLACE INTO files "
        "(filename, full_path, extension, file_size, timestamp, file_type, "
        "image_width, image_height, attributes, created_date, modified_date, hash_val) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare INSERT statement");
        return false;
    }

    // Format timestamp
    char timeBuf[20];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&metadata.timestamp));

    // Bind parameters
    sqlite3_bind_text(stmt, 1, metadata.filename.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, metadata.fullPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, metadata.extension.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, metadata.fileSize);
    sqlite3_bind_text(stmt, 5, timeBuf, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, metadata.fileType.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 7, metadata.imageWidth);
    sqlite3_bind_int(stmt, 8, metadata.imageHeight);
    sqlite3_bind_text(stmt, 9, metadata.attributes.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, metadata.createdDate.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 11, metadata.modifiedDate.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 12, metadata.hashVal.c_str(), -1, SQLITE_STATIC);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        LOG_DEBUG("File inserted to relational table: " + metadata.filename);
        success = true;
    } else {
        LOG_ERROR("Failed to insert file to relational table: " + metadata.filename);
    }

    sqlite3_finalize(stmt);
    return success;
}

bool DatabaseManager::InsertFileMetadataJSON(const FileMetadata& metadata) {
    LOG_DEBUG("InsertFileMetadataJSON - File: " + metadata.filename);

    if (!isConnected) {
        LOG_ERROR("InsertFileMetadataJSON - Database not connected");
        return false;
    }

    // Check if entry with same hash and path already exists
    if (EntryExists(metadata.hashVal, metadata.fullPath)) {
        LOG_DEBUG("InsertFileMetadataJSON - Skipping, already in database: " + metadata.filename);
        return true;  // Return true as this is not an error, just a duplicate scan
    }

    std::ostringstream jsonData;
    jsonData << "{"
             << "\"filename\": \"" << metadata.filename << "\", "
             << "\"extension\": \"" << metadata.extension << "\", "
             << "\"fileSize\": " << metadata.fileSize << ", "
             << "\"fileType\": \"" << metadata.fileType << "\", "
             << "\"imageWidth\": " << metadata.imageWidth << ", "
             << "\"imageHeight\": " << metadata.imageHeight << ", "
             << "\"attributes\": \"" << metadata.attributes << "\", "
             << "\"hashVal\": \"" << metadata.hashVal << "\""
             << "}";

    const char* sql = "INSERT OR REPLACE INTO files_json (file_path, metadata) VALUES (?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare INSERT JSON statement");
        return false;
    }

    std::string jsonStr = jsonData.str();
    sqlite3_bind_text(stmt, 1, metadata.fullPath.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, jsonStr.c_str(), -1, SQLITE_TRANSIENT);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        LOG_DEBUG("File inserted to JSON table: " + metadata.filename);
        success = true;
    } else {
        LOG_ERROR("Failed to insert file to JSON table: " + metadata.filename);
    }

    sqlite3_finalize(stmt);
    return success;
}

std::vector<FileMetadata> DatabaseManager::QueryAllFiles() {
    LOG_DEBUG("QueryAllFiles START");
    std::vector<FileMetadata> results;
    if (!isConnected) return results;

    sqlite3_stmt* stmt;
    const char* sql = "SELECT filename, full_path, extension, file_size, file_type, "
                      "image_width, image_height, attributes, hash_val FROM files ORDER BY filename;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("QueryAllFiles - Failed to prepare statement");
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileMetadata metadata;
        metadata.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        metadata.fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        metadata.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        metadata.fileSize = sqlite3_column_int64(stmt, 3);
        metadata.fileType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        metadata.imageWidth = sqlite3_column_int(stmt, 5);
        metadata.imageHeight = sqlite3_column_int(stmt, 6);
        metadata.attributes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        metadata.hashVal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        results.push_back(metadata);
    }

    sqlite3_finalize(stmt);

    // Track memory allocation for query results
    size_t resultsVectorSize = results.capacity() * sizeof(FileMetadata);
    size_t estimatedStringData = results.size() * 600;  // ~600 bytes per entry for strings
    size_t totalQueryMemory = resultsVectorSize + estimatedStringData;
    MEMORY_TRACK_ALLOC("DatabaseManager::QueryAllFiles", totalQueryMemory);

    LOG_DEBUG("QueryAllFiles COMPLETE - Loaded " + std::to_string(results.size()) + " files, ~" +
             std::to_string(totalQueryMemory / (1024*1024)) + " MB");

    return results;
}

std::vector<FileMetadata> DatabaseManager::QueryFilesByType(const std::string& fileType) {
    std::vector<FileMetadata> results;
    if (!isConnected) return results;

    std::string sql = "SELECT filename, full_path, extension, file_size, file_type, "
                      "image_width, image_height, attributes FROM files WHERE file_type = '" +
                      EscapeSQL(fileType) + "' ORDER BY filename;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileMetadata metadata;
        metadata.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        metadata.fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        metadata.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        metadata.fileSize = sqlite3_column_int64(stmt, 3);
        metadata.fileType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        metadata.imageWidth = sqlite3_column_int(stmt, 5);
        metadata.imageHeight = sqlite3_column_int(stmt, 6);
        metadata.attributes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        results.push_back(metadata);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<FileMetadata> DatabaseManager::QueryFilesByExtension(const std::string& extension) {
    std::vector<FileMetadata> results;
    if (!isConnected) return results;

    std::string sql = "SELECT filename, full_path, extension, file_size, file_type, "
                      "image_width, image_height, attributes FROM files WHERE extension = '" +
                      EscapeSQL(extension) + "' ORDER BY filename;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileMetadata metadata;
        metadata.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        metadata.fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        metadata.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        metadata.fileSize = sqlite3_column_int64(stmt, 3);
        metadata.fileType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        metadata.imageWidth = sqlite3_column_int(stmt, 5);
        metadata.imageHeight = sqlite3_column_int(stmt, 6);
        metadata.attributes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        results.push_back(metadata);
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<FileMetadata> DatabaseManager::QueryLargeFiles(long long minSize) {
    std::vector<FileMetadata> results;
    if (!isConnected) return results;

    std::ostringstream sql;
    sql << "SELECT filename, full_path, extension, file_size, file_type, "
        << "image_width, image_height, attributes FROM files WHERE file_size > " << minSize
        << " ORDER BY file_size DESC;";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FileMetadata metadata;
        metadata.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        metadata.fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        metadata.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        metadata.fileSize = sqlite3_column_int64(stmt, 3);
        metadata.fileType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        metadata.imageWidth = sqlite3_column_int(stmt, 5);
        metadata.imageHeight = sqlite3_column_int(stmt, 6);
        metadata.attributes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        results.push_back(metadata);
    }

    sqlite3_finalize(stmt);
    return results;
}

bool DatabaseManager::ExecuteSQL(const std::string& sql) {
    if (!isConnected) {
        LOG_ERROR("ExecuteSQL - Database not connected");
        return false;
    }

    LOG_DEBUG("ExecuteSQL: " + sql.substr(0, 100) + (sql.length() > 100 ? "..." : ""));

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
        LOG_ERROR("SQL execution failed - Error code: " + std::to_string(rc) +
                  " Message: " + std::string(errMsg ? errMsg : "unknown"));
        sqlite3_free(errMsg);
        return false;
    }

    LOG_DEBUG("SQL executed successfully");
    return true;
}

std::string DatabaseManager::EscapeSQL(const std::string& input) {
    std::string result;
    for (char c : input) {
        if (c == '\'') {
            result += "''";
        } else {
            result += c;
        }
    }
    return result;
}

std::vector<std::vector<FileMetadata>> DatabaseManager::FindDuplicates() {
    std::vector<std::vector<FileMetadata>> duplicateGroups;

    if (!isConnected) {
        LOG_WARNING("FindDuplicates - Database not connected");
        return duplicateGroups;
    }

    LOG_DEBUG("FindDuplicates START");

    // Query: Find files with duplicate hash values (grouped)
    const char* sql = R"(
        SELECT hash_val, COUNT(*) as cnt FROM files
        WHERE hash_val IS NOT NULL AND hash_val != ''
        GROUP BY hash_val HAVING cnt > 1
        ORDER BY cnt DESC;
    )";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare duplicate query");
        return duplicateGroups;
    }

    // Collect all duplicate hashes
    std::vector<std::string> duplicateHashes;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* hashVal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int count = sqlite3_column_int(stmt, 1);
        if (hashVal) {
            duplicateHashes.push_back(hashVal);
            LOG_DEBUG("Found duplicate hash: " + std::string(hashVal) + " (count: " + std::to_string(count) + ")");
        }
    }
    sqlite3_finalize(stmt);

    // For each duplicate hash, fetch all files with that hash
    for (const auto& hash : duplicateHashes) {
        std::string querySql = "SELECT filename, full_path, extension, file_size, file_type, "
                               "image_width, image_height, attributes, hash_val FROM files "
                               "WHERE hash_val = ? ORDER BY filename;";

        if (sqlite3_prepare_v2(db, querySql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_ERROR("Failed to prepare file query for hash");
            continue;
        }

        sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);

        std::vector<FileMetadata> group;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FileMetadata metadata;
            metadata.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            metadata.fullPath = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            metadata.extension = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            metadata.fileSize = sqlite3_column_int64(stmt, 3);
            metadata.fileType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            metadata.imageWidth = sqlite3_column_int(stmt, 5);
            metadata.imageHeight = sqlite3_column_int(stmt, 6);
            metadata.attributes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            metadata.hashVal = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
            group.push_back(metadata);
        }

        if (!group.empty()) {
            duplicateGroups.push_back(group);
        }

        sqlite3_finalize(stmt);
    }

    LOG_INFO("FindDuplicates COMPLETE - Found " + std::to_string(duplicateGroups.size()) + " duplicate groups");
    return duplicateGroups;
}

bool DatabaseManager::ClearAllData() {
    if (!isConnected) return false;

    return ExecuteSQL("DELETE FROM files;") && ExecuteSQL("DELETE FROM files_json;");
}

int DatabaseManager::GetTotalFileCount() {
    if (!isConnected) {
        LOG_WARNING("GetTotalFileCount - Database not connected");
        return 0;
    }

    sqlite3_stmt* stmt;
    const char* sql = "SELECT COUNT(*) FROM files;";

    LOG_DEBUG("Querying total file count from database");

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare SELECT COUNT(*) statement");
        return 0;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        LOG_INFO("Total files in database: " + std::to_string(count));
    }

    sqlite3_finalize(stmt);
    return count;
}
