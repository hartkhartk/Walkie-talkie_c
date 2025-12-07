/**
 * @file storage.c
 * @brief מימוש מודול אחסון - SD Card ו-SPIFFS
 */

#include "hal/storage.h"
#include "config.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

// =============================================================================
// Platform-Specific Includes
// =============================================================================

#ifdef ESP32
    #include "esp_vfs.h"
    #include "esp_vfs_fat.h"
    #include "esp_spiffs.h"
    #include "esp_log.h"
    #include "driver/sdmmc_host.h"
    #include "driver/sdspi_host.h"
    #include "sdmmc_cmd.h"
    #include "esp_timer.h"
    
    static const char* TAG = "STORAGE";
    #define LOG_INFO(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#else
    #include <sys/stat.h>
    #include <dirent.h>
    #include <unistd.h>
    #define LOG_INFO(fmt, ...) printf("[STORAGE] " fmt "\n", ##__VA_ARGS__)
    #define LOG_ERROR(fmt, ...) printf("[STORAGE ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...)
#endif

// =============================================================================
// Internal Constants
// =============================================================================

#define SD_MOUNT_POINT      "/sdcard"
#define SPIFFS_MOUNT_POINT  "/spiffs"
#define SD_SPI_SPEED        20000   // 20 MHz

// WAV file header size
#define WAV_HEADER_SIZE     44

// =============================================================================
// WAV Header Structure
// =============================================================================

#pragma pack(push, 1)
typedef struct {
    // RIFF chunk
    char     riff_tag[4];       // "RIFF"
    uint32_t riff_size;         // File size - 8
    char     wave_tag[4];       // "WAVE"
    
    // Format chunk
    char     fmt_tag[4];        // "fmt "
    uint32_t fmt_size;          // 16 for PCM
    uint16_t audio_format;      // 1 for PCM
    uint16_t num_channels;      // 1 = mono, 2 = stereo
    uint32_t sample_rate;       // e.g., 8000
    uint32_t byte_rate;         // sample_rate * channels * bits/8
    uint16_t block_align;       // channels * bits/8
    uint16_t bits_per_sample;   // e.g., 16
    
    // Data chunk
    char     data_tag[4];       // "data"
    uint32_t data_size;         // Audio data size
} wav_header_t;
#pragma pack(pop)

// =============================================================================
// Internal State
// =============================================================================

static bool g_initialized = false;
static bool g_sd_mounted = false;
static bool g_spiffs_mounted = false;

#ifdef ESP32
static sdmmc_card_t* g_sd_card = NULL;
#endif

// =============================================================================
// Platform-Specific Implementation
// =============================================================================

#ifdef ESP32

// -----------------------------------------------------------------------------
// ESP32: SD Card via SPI
// -----------------------------------------------------------------------------

storage_error_t storage_sd_mount(void) {
    if (g_sd_mounted) {
        return STORAGE_OK;
    }
    
    LOG_INFO("Mounting SD card...");
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // Configure SPI bus
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_SPI_SPEED;
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = 23,  // MOSI
        .miso_io_num = 19,  // MISO
        .sclk_io_num = 18,  // SCK
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    
    // Note: SPI bus might already be initialized by radio
    spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = 4;  // SD CS pin
    slot_config.host_id = host.slot;
    
    esp_err_t ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, 
                                             &slot_config, &mount_config, 
                                             &g_sd_card);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOG_ERROR("Failed to mount filesystem");
        } else {
            LOG_ERROR("Failed to initialize SD card (%s)", esp_err_to_name(ret));
        }
        return STORAGE_ERROR_NOT_MOUNTED;
    }
    
    // Print card info
    sdmmc_card_print_info(stdout, g_sd_card);
    
    // Create recordings directory if needed
    mkdir(SD_MOUNT_POINT STORAGE_RECORDING_DIR, 0775);
    
    g_sd_mounted = true;
    LOG_INFO("SD card mounted successfully");
    
    return STORAGE_OK;
}

void storage_sd_unmount(void) {
    if (!g_sd_mounted) {
        return;
    }
    
    esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, g_sd_card);
    g_sd_card = NULL;
    g_sd_mounted = false;
    
    LOG_INFO("SD card unmounted");
}

bool storage_sd_is_mounted(void) {
    return g_sd_mounted;
}

storage_error_t storage_sd_get_info(storage_info_t* info) {
    if (!info) return STORAGE_ERROR_INVALID_PATH;
    if (!g_sd_mounted) return STORAGE_ERROR_NOT_MOUNTED;
    
    memset(info, 0, sizeof(storage_info_t));
    info->type = STORAGE_TYPE_SD;
    info->is_mounted = true;
    
    FATFS* fs;
    DWORD fre_clust;
    
    if (f_getfree(SD_MOUNT_POINT, &fre_clust, &fs) == FR_OK) {
        uint64_t total = ((uint64_t)fs->n_fatent - 2) * fs->csize * 512;
        uint64_t free = (uint64_t)fre_clust * fs->csize * 512;
        
        info->total_bytes = (uint32_t)(total / 1024);  // In KB
        info->free_bytes = (uint32_t)(free / 1024);
        info->used_bytes = info->total_bytes - info->free_bytes;
    }
    
    // Count files
    info->file_count = storage_dir_count(SD_MOUNT_POINT STORAGE_RECORDING_DIR);
    
    strncpy(info->label, "SD Card", sizeof(info->label));
    
    return STORAGE_OK;
}

// -----------------------------------------------------------------------------
// ESP32: SPIFFS
// -----------------------------------------------------------------------------

storage_error_t storage_spiffs_mount(void) {
    if (g_spiffs_mounted) {
        return STORAGE_OK;
    }
    
    LOG_INFO("Mounting SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_MOUNT_POINT,
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            LOG_ERROR("Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            LOG_ERROR("SPIFFS partition not found");
        } else {
            LOG_ERROR("SPIFFS error: %s", esp_err_to_name(ret));
        }
        return STORAGE_ERROR_NOT_MOUNTED;
    }
    
    // Create directories if needed
    mkdir(SPIFFS_MOUNT_POINT STORAGE_CONFIG_DIR, 0775);
    mkdir(SPIFFS_MOUNT_POINT STORAGE_RECORDING_DIR, 0775);
    
    g_spiffs_mounted = true;
    LOG_INFO("SPIFFS mounted successfully");
    
    return STORAGE_OK;
}

void storage_spiffs_unmount(void) {
    if (!g_spiffs_mounted) {
        return;
    }
    
    esp_vfs_spiffs_unregister(NULL);
    g_spiffs_mounted = false;
    
    LOG_INFO("SPIFFS unmounted");
}

bool storage_spiffs_is_mounted(void) {
    return g_spiffs_mounted;
}

storage_error_t storage_spiffs_get_info(storage_info_t* info) {
    if (!info) return STORAGE_ERROR_INVALID_PATH;
    if (!g_spiffs_mounted) return STORAGE_ERROR_NOT_MOUNTED;
    
    memset(info, 0, sizeof(storage_info_t));
    info->type = STORAGE_TYPE_SPIFFS;
    info->is_mounted = true;
    
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    
    info->total_bytes = total;
    info->used_bytes = used;
    info->free_bytes = total - used;
    
    strncpy(info->label, "SPIFFS", sizeof(info->label));
    
    return STORAGE_OK;
}

storage_error_t storage_spiffs_format(void) {
    if (!g_spiffs_mounted) {
        return STORAGE_ERROR_NOT_MOUNTED;
    }
    
    LOG_INFO("Formatting SPIFFS...");
    
    esp_err_t ret = esp_spiffs_format(NULL);
    if (ret != ESP_OK) {
        LOG_ERROR("Format failed: %s", esp_err_to_name(ret));
        return STORAGE_ERROR_FORMAT;
    }
    
    LOG_INFO("SPIFFS formatted successfully");
    return STORAGE_OK;
}

#else
// -----------------------------------------------------------------------------
// Simulator/PC Implementation
// -----------------------------------------------------------------------------

storage_error_t storage_sd_mount(void) {
    g_sd_mounted = true;
    mkdir("./simulated_sd", 0775);
    mkdir("./simulated_sd/recordings", 0775);
    LOG_INFO("Simulated SD mounted");
    return STORAGE_OK;
}

void storage_sd_unmount(void) {
    g_sd_mounted = false;
    LOG_INFO("Simulated SD unmounted");
}

bool storage_sd_is_mounted(void) {
    return g_sd_mounted;
}

storage_error_t storage_sd_get_info(storage_info_t* info) {
    if (!info) return STORAGE_ERROR_INVALID_PATH;
    
    memset(info, 0, sizeof(storage_info_t));
    info->type = STORAGE_TYPE_SD;
    info->is_mounted = g_sd_mounted;
    info->total_bytes = 16 * 1024 * 1024;  // 16 GB simulated
    info->free_bytes = 15 * 1024 * 1024;
    info->used_bytes = 1 * 1024 * 1024;
    strncpy(info->label, "SD Card (Sim)", sizeof(info->label));
    
    return STORAGE_OK;
}

storage_error_t storage_spiffs_mount(void) {
    g_spiffs_mounted = true;
    mkdir("./simulated_spiffs", 0775);
    mkdir("./simulated_spiffs/config", 0775);
    LOG_INFO("Simulated SPIFFS mounted");
    return STORAGE_OK;
}

void storage_spiffs_unmount(void) {
    g_spiffs_mounted = false;
    LOG_INFO("Simulated SPIFFS unmounted");
}

bool storage_spiffs_is_mounted(void) {
    return g_spiffs_mounted;
}

storage_error_t storage_spiffs_get_info(storage_info_t* info) {
    if (!info) return STORAGE_ERROR_INVALID_PATH;
    
    memset(info, 0, sizeof(storage_info_t));
    info->type = STORAGE_TYPE_SPIFFS;
    info->is_mounted = g_spiffs_mounted;
    info->total_bytes = 1024 * 1024;  // 1 MB simulated
    info->free_bytes = 512 * 1024;
    info->used_bytes = 512 * 1024;
    strncpy(info->label, "SPIFFS (Sim)", sizeof(info->label));
    
    return STORAGE_OK;
}

storage_error_t storage_spiffs_format(void) {
    LOG_INFO("Simulated SPIFFS format");
    return STORAGE_OK;
}

#endif

// =============================================================================
// Common Implementation
// =============================================================================

storage_error_t storage_init(void) {
    if (g_initialized) {
        return STORAGE_OK;
    }
    
    LOG_INFO("Initializing storage system...");
    
    // Try to mount SPIFFS first (always available)
    storage_error_t ret = storage_spiffs_mount();
    if (ret != STORAGE_OK) {
        LOG_ERROR("Failed to mount SPIFFS");
    }
    
    // Try to mount SD card (optional)
    ret = storage_sd_mount();
    if (ret != STORAGE_OK) {
        LOG_INFO("SD card not available, using SPIFFS only");
    }
    
    g_initialized = true;
    LOG_INFO("Storage system initialized");
    
    return STORAGE_OK;
}

void storage_deinit(void) {
    if (!g_initialized) {
        return;
    }
    
    storage_sd_unmount();
    storage_spiffs_unmount();
    
    g_initialized = false;
    LOG_INFO("Storage system deinitialized");
}

bool storage_is_initialized(void) {
    return g_initialized;
}

// =============================================================================
// File Operations
// =============================================================================

storage_error_t storage_file_open(storage_file_t* file, const char* path, file_mode_t mode) {
    if (!file || !path) {
        return STORAGE_ERROR_INVALID_PATH;
    }
    
    memset(file, 0, sizeof(storage_file_t));
    
    const char* mode_str;
    switch (mode) {
        case FILE_MODE_READ:       mode_str = "rb"; break;
        case FILE_MODE_WRITE:      mode_str = "wb"; break;
        case FILE_MODE_APPEND:     mode_str = "ab"; break;
        case FILE_MODE_READ_WRITE: mode_str = "r+b"; break;
        default: return STORAGE_ERROR_INVALID_PATH;
    }
    
    FILE* fp = fopen(path, mode_str);
    if (!fp) {
        LOG_ERROR("Failed to open file: %s", path);
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    file->size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    file->handle = fp;
    file->mode = mode;
    file->position = 0;
    file->is_open = true;
    file->type = storage_get_type_from_path(path);
    
    LOG_DEBUG("Opened file: %s (size: %u)", path, file->size);
    
    return STORAGE_OK;
}

void storage_file_close(storage_file_t* file) {
    if (!file || !file->is_open || !file->handle) {
        return;
    }
    
    fclose((FILE*)file->handle);
    file->handle = NULL;
    file->is_open = false;
    
    LOG_DEBUG("Closed file");
}

int32_t storage_file_read(storage_file_t* file, void* buffer, uint32_t size) {
    if (!file || !file->is_open || !buffer) {
        return -1;
    }
    
    size_t bytes_read = fread(buffer, 1, size, (FILE*)file->handle);
    file->position += bytes_read;
    
    return (int32_t)bytes_read;
}

int32_t storage_file_write(storage_file_t* file, const void* buffer, uint32_t size) {
    if (!file || !file->is_open || !buffer) {
        return -1;
    }
    
    size_t bytes_written = fwrite(buffer, 1, size, (FILE*)file->handle);
    file->position += bytes_written;
    file->size = (file->position > file->size) ? file->position : file->size;
    
    return (int32_t)bytes_written;
}

storage_error_t storage_file_seek(storage_file_t* file, int32_t offset, int whence) {
    if (!file || !file->is_open) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    if (fseek((FILE*)file->handle, offset, whence) != 0) {
        return STORAGE_ERROR_READ;
    }
    
    file->position = ftell((FILE*)file->handle);
    return STORAGE_OK;
}

uint32_t storage_file_tell(storage_file_t* file) {
    if (!file || !file->is_open) {
        return 0;
    }
    return file->position;
}

storage_error_t storage_file_sync(storage_file_t* file) {
    if (!file || !file->is_open) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    fflush((FILE*)file->handle);
    return STORAGE_OK;
}

bool storage_file_eof(storage_file_t* file) {
    if (!file || !file->is_open) {
        return true;
    }
    return feof((FILE*)file->handle) != 0;
}

// =============================================================================
// File Management
// =============================================================================

bool storage_file_exists(const char* path) {
    if (!path) return false;
    
    FILE* fp = fopen(path, "rb");
    if (fp) {
        fclose(fp);
        return true;
    }
    return false;
}

storage_error_t storage_file_delete(const char* path) {
    if (!path) return STORAGE_ERROR_INVALID_PATH;
    
    if (remove(path) == 0) {
        LOG_INFO("Deleted file: %s", path);
        return STORAGE_OK;
    }
    
    LOG_ERROR("Failed to delete: %s", path);
    return STORAGE_ERROR_DELETE;
}

storage_error_t storage_file_rename(const char* old_path, const char* new_path) {
    if (!old_path || !new_path) return STORAGE_ERROR_INVALID_PATH;
    
    if (rename(old_path, new_path) == 0) {
        LOG_INFO("Renamed: %s -> %s", old_path, new_path);
        return STORAGE_OK;
    }
    
    return STORAGE_ERROR_WRITE;
}

storage_error_t storage_file_copy(const char* src_path, const char* dst_path) {
    if (!src_path || !dst_path) return STORAGE_ERROR_INVALID_PATH;
    
    storage_file_t src, dst;
    uint8_t buffer[STORAGE_BUFFER_SIZE];
    
    if (storage_file_open(&src, src_path, FILE_MODE_READ) != STORAGE_OK) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    if (storage_file_open(&dst, dst_path, FILE_MODE_WRITE) != STORAGE_OK) {
        storage_file_close(&src);
        return STORAGE_ERROR_CREATE;
    }
    
    int32_t bytes;
    while ((bytes = storage_file_read(&src, buffer, sizeof(buffer))) > 0) {
        if (storage_file_write(&dst, buffer, bytes) != bytes) {
            storage_file_close(&src);
            storage_file_close(&dst);
            return STORAGE_ERROR_WRITE;
        }
    }
    
    storage_file_close(&src);
    storage_file_close(&dst);
    
    LOG_INFO("Copied: %s -> %s", src_path, dst_path);
    return STORAGE_OK;
}

// =============================================================================
// Recording Management
// =============================================================================

storage_error_t storage_recording_start(storage_file_t* file) {
    if (!file) return STORAGE_ERROR_INVALID_PATH;
    
    char filename[STORAGE_MAX_PATH_LENGTH];
    char path[STORAGE_MAX_PATH_LENGTH];
    
    storage_generate_recording_name(filename, sizeof(filename));
    
    // Prefer SD card, fall back to SPIFFS
    if (g_sd_mounted) {
        snprintf(path, sizeof(path), "%s%s/%s", 
                 SD_MOUNT_POINT, STORAGE_RECORDING_DIR, filename);
    } else if (g_spiffs_mounted) {
        snprintf(path, sizeof(path), "%s%s/%s", 
                 SPIFFS_MOUNT_POINT, STORAGE_RECORDING_DIR, filename);
    } else {
        return STORAGE_ERROR_NOT_MOUNTED;
    }
    
    storage_error_t ret = storage_file_open(file, path, FILE_MODE_WRITE);
    if (ret != STORAGE_OK) {
        return ret;
    }
    
    // Write placeholder WAV header
    ret = storage_wav_write_header(file, 8000, 16, 1);
    if (ret != STORAGE_OK) {
        storage_file_close(file);
        return ret;
    }
    
    LOG_INFO("Started recording: %s", filename);
    return STORAGE_OK;
}

storage_error_t storage_recording_finish(storage_file_t* file, uint32_t sample_count) {
    if (!file || !file->is_open) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    // Calculate data size (16-bit mono)
    uint32_t data_size = sample_count * 2;
    
    // Update WAV header
    storage_error_t ret = storage_wav_update_header(file, data_size);
    if (ret != STORAGE_OK) {
        return ret;
    }
    
    storage_file_sync(file);
    storage_file_close(file);
    
    LOG_INFO("Finished recording: %u samples, %u bytes", sample_count, data_size);
    return STORAGE_OK;
}

// =============================================================================
// WAV File Helpers
// =============================================================================

storage_error_t storage_wav_write_header(storage_file_t* file, 
                                         uint16_t sample_rate,
                                         uint8_t bits_per_sample,
                                         uint8_t channels) {
    if (!file || !file->is_open) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    wav_header_t header;
    memset(&header, 0, sizeof(header));
    
    // RIFF chunk
    memcpy(header.riff_tag, "RIFF", 4);
    header.riff_size = 0;  // Will be updated later
    memcpy(header.wave_tag, "WAVE", 4);
    
    // Format chunk
    memcpy(header.fmt_tag, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;  // PCM
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    
    // Data chunk
    memcpy(header.data_tag, "data", 4);
    header.data_size = 0;  // Will be updated later
    
    if (storage_file_write(file, &header, sizeof(header)) != sizeof(header)) {
        return STORAGE_ERROR_WRITE;
    }
    
    return STORAGE_OK;
}

storage_error_t storage_wav_update_header(storage_file_t* file, uint32_t data_size) {
    if (!file || !file->is_open) {
        return STORAGE_ERROR_NOT_FOUND;
    }
    
    // Update RIFF size
    uint32_t riff_size = data_size + 36;
    storage_file_seek(file, 4, SEEK_SET);
    storage_file_write(file, &riff_size, 4);
    
    // Update data size
    storage_file_seek(file, 40, SEEK_SET);
    storage_file_write(file, &data_size, 4);
    
    return STORAGE_OK;
}

// =============================================================================
// Backup & Export
// =============================================================================

int32_t storage_backup_to_sd(void) {
    if (!g_sd_mounted || !g_spiffs_mounted) {
        LOG_ERROR("Both SD and SPIFFS must be mounted for backup");
        return -1;
    }
    
    LOG_INFO("Starting backup from SPIFFS to SD...");
    
    // This would list SPIFFS recordings and copy them to SD
    // Simplified implementation:
    int32_t copied = 0;
    
    // In real implementation, iterate through SPIFFS recordings
    // and copy each to SD card
    
    LOG_INFO("Backup complete: %d files copied", copied);
    return copied;
}

// =============================================================================
// Utilities
// =============================================================================

void storage_format_size(uint32_t bytes, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", bytes / 1024.0);
    } else {
        snprintf(buffer, buffer_size, "%u B", bytes);
    }
}

void storage_generate_recording_name(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    snprintf(buffer, buffer_size, "%s%04d%02d%02d_%02d%02d%02d%s",
             RECORDING_PREFIX,
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             RECORDING_EXTENSION);
}

storage_type_t storage_get_type_from_path(const char* path) {
    if (!path) return STORAGE_TYPE_NONE;
    
    if (strstr(path, SD_MOUNT_POINT) == path ||
        strstr(path, "sdcard") != NULL ||
        strstr(path, "simulated_sd") != NULL) {
        return STORAGE_TYPE_SD;
    }
    
    if (strstr(path, SPIFFS_MOUNT_POINT) == path ||
        strstr(path, "spiffs") != NULL ||
        strstr(path, "simulated_spiffs") != NULL) {
        return STORAGE_TYPE_SPIFFS;
    }
    
    return STORAGE_TYPE_NONE;
}

// =============================================================================
// Directory Operations (Simplified)
// =============================================================================

storage_error_t storage_mkdir(const char* path) {
    if (!path) return STORAGE_ERROR_INVALID_PATH;
    
#ifdef _WIN32
    if (_mkdir(path) == 0) return STORAGE_OK;
#else
    if (mkdir(path, 0775) == 0) return STORAGE_OK;
#endif
    
    return STORAGE_ERROR_CREATE;
}

int32_t storage_dir_count(const char* path) {
    if (!path) return 0;
    
    int32_t count = 0;
    
#ifndef ESP32
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                count++;
            }
        }
        closedir(dir);
    }
#else
    // ESP32 implementation using esp_vfs
    DIR* dir = opendir(path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                count++;
            }
        }
        closedir(dir);
    }
#endif
    
    return count;
}

