/**
 * @file storage.h
 * @brief מודול אחסון - SD Card ו-SPIFFS
 * 
 * תומך ב:
 * - כרטיס SD (FAT32) לאחסון הקלטות
 * - SPIFFS לנתוני קונפיגורציה
 * - גיבוי וייצוא הקלטות
 */

#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// Storage Constants
// =============================================================================

#define STORAGE_MAX_PATH_LENGTH     128
#define STORAGE_MAX_FILENAME_LENGTH 64
#define STORAGE_BUFFER_SIZE         512
#define STORAGE_MAX_RECORDINGS      1000
#define STORAGE_RECORDING_DIR       "/recordings"
#define STORAGE_CONFIG_DIR          "/config"

// Recording file format: REC_YYYYMMDD_HHMMSS.wav
#define RECORDING_PREFIX            "REC_"
#define RECORDING_EXTENSION         ".wav"

// =============================================================================
// Storage Types
// =============================================================================

typedef enum {
    STORAGE_TYPE_NONE = 0,
    STORAGE_TYPE_SPIFFS,        // Internal flash
    STORAGE_TYPE_SD,            // SD Card (FAT32)
    STORAGE_TYPE_FATFS          // Internal FAT partition
} storage_type_t;

typedef enum {
    STORAGE_OK = 0,
    STORAGE_ERROR_NOT_MOUNTED,
    STORAGE_ERROR_NOT_FOUND,
    STORAGE_ERROR_FULL,
    STORAGE_ERROR_READ,
    STORAGE_ERROR_WRITE,
    STORAGE_ERROR_CREATE,
    STORAGE_ERROR_DELETE,
    STORAGE_ERROR_FORMAT,
    STORAGE_ERROR_INVALID_PATH,
    STORAGE_ERROR_ALREADY_EXISTS,
    STORAGE_ERROR_NO_SPACE
} storage_error_t;

typedef enum {
    FILE_MODE_READ = 0,
    FILE_MODE_WRITE,
    FILE_MODE_APPEND,
    FILE_MODE_READ_WRITE
} file_mode_t;

// =============================================================================
// Storage Info Structures
// =============================================================================

/**
 * @brief מידע על מערכת אחסון
 */
typedef struct {
    storage_type_t type;
    bool is_mounted;
    uint32_t total_bytes;       // סה"כ נפח
    uint32_t used_bytes;        // נפח בשימוש
    uint32_t free_bytes;        // נפח פנוי
    uint32_t file_count;        // מספר קבצים
    char label[16];             // תווית
} storage_info_t;

/**
 * @brief מידע על קובץ
 */
typedef struct {
    char name[STORAGE_MAX_FILENAME_LENGTH];
    char path[STORAGE_MAX_PATH_LENGTH];
    uint32_t size;              // גודל בבתים
    uint32_t created_time;      // זמן יצירה (Unix timestamp)
    uint32_t modified_time;     // זמן שינוי אחרון
    bool is_directory;
    bool is_readonly;
} file_info_t;

/**
 * @brief מידע על הקלטה
 */
typedef struct {
    char filename[STORAGE_MAX_FILENAME_LENGTH];
    uint32_t duration_ms;       // אורך במילישניות
    uint32_t size_bytes;        // גודל בבתים
    uint32_t timestamp;         // זמן הקלטה
    uint16_t sample_rate;       // קצב דגימה
    uint8_t  channels;          // מספר ערוצים
    uint8_t  bits_per_sample;   // ביטים לדגימה
} recording_info_t;

/**
 * @brief handle לקובץ פתוח
 */
typedef struct {
    void* handle;               // Platform-specific handle
    storage_type_t type;
    file_mode_t mode;
    uint32_t position;
    uint32_t size;
    bool is_open;
} storage_file_t;

// =============================================================================
// Initialization
// =============================================================================

/**
 * @brief אתחול מערכת האחסון
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_init(void);

/**
 * @brief שחרור משאבי האחסון
 */
void storage_deinit(void);

/**
 * @brief בדיקה אם האחסון מאותחל
 */
bool storage_is_initialized(void);

// =============================================================================
// SD Card Operations
// =============================================================================

/**
 * @brief טעינת כרטיס SD
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_sd_mount(void);

/**
 * @brief שחרור כרטיס SD
 */
void storage_sd_unmount(void);

/**
 * @brief בדיקה אם SD מחובר
 */
bool storage_sd_is_mounted(void);

/**
 * @brief קבלת מידע על כרטיס SD
 * @param info מבנה למילוי
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_sd_get_info(storage_info_t* info);

// =============================================================================
// SPIFFS Operations
// =============================================================================

/**
 * @brief טעינת SPIFFS
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_spiffs_mount(void);

/**
 * @brief שחרור SPIFFS
 */
void storage_spiffs_unmount(void);

/**
 * @brief בדיקה אם SPIFFS מחובר
 */
bool storage_spiffs_is_mounted(void);

/**
 * @brief קבלת מידע על SPIFFS
 */
storage_error_t storage_spiffs_get_info(storage_info_t* info);

/**
 * @brief פרמוט SPIFFS
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_spiffs_format(void);

// =============================================================================
// Generic File Operations
// =============================================================================

/**
 * @brief פתיחת קובץ
 * @param file מבנה קובץ
 * @param path נתיב מלא
 * @param mode מצב פתיחה
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_file_open(storage_file_t* file, const char* path, file_mode_t mode);

/**
 * @brief סגירת קובץ
 * @param file מבנה קובץ
 */
void storage_file_close(storage_file_t* file);

/**
 * @brief קריאה מקובץ
 * @param file מבנה קובץ
 * @param buffer באפר פלט
 * @param size כמות לקריאה
 * @return מספר בתים שנקראו, או -1 בשגיאה
 */
int32_t storage_file_read(storage_file_t* file, void* buffer, uint32_t size);

/**
 * @brief כתיבה לקובץ
 * @param file מבנה קובץ
 * @param buffer נתונים לכתיבה
 * @param size כמות לכתיבה
 * @return מספר בתים שנכתבו, או -1 בשגיאה
 */
int32_t storage_file_write(storage_file_t* file, const void* buffer, uint32_t size);

/**
 * @brief דילוג למיקום בקובץ
 * @param file מבנה קובץ
 * @param offset היסט
 * @param whence נקודת התחלה (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_file_seek(storage_file_t* file, int32_t offset, int whence);

/**
 * @brief קבלת מיקום נוכחי בקובץ
 */
uint32_t storage_file_tell(storage_file_t* file);

/**
 * @brief סנכרון קובץ לדיסק
 */
storage_error_t storage_file_sync(storage_file_t* file);

/**
 * @brief בדיקה אם הגענו לסוף הקובץ
 */
bool storage_file_eof(storage_file_t* file);

// =============================================================================
// File Management
// =============================================================================

/**
 * @brief בדיקה אם קובץ קיים
 */
bool storage_file_exists(const char* path);

/**
 * @brief קבלת מידע על קובץ
 */
storage_error_t storage_file_info(const char* path, file_info_t* info);

/**
 * @brief מחיקת קובץ
 */
storage_error_t storage_file_delete(const char* path);

/**
 * @brief שינוי שם קובץ
 */
storage_error_t storage_file_rename(const char* old_path, const char* new_path);

/**
 * @brief העתקת קובץ
 */
storage_error_t storage_file_copy(const char* src_path, const char* dst_path);

// =============================================================================
// Directory Operations
// =============================================================================

/**
 * @brief יצירת תיקייה
 */
storage_error_t storage_mkdir(const char* path);

/**
 * @brief מחיקת תיקייה ריקה
 */
storage_error_t storage_rmdir(const char* path);

/**
 * @brief ספירת קבצים בתיקייה
 */
int32_t storage_dir_count(const char* path);

/**
 * @brief קבלת רשימת קבצים בתיקייה
 * @param path נתיב התיקייה
 * @param files מערך פלט
 * @param max_count גודל מקסימלי של המערך
 * @return מספר קבצים שנמצאו
 */
int32_t storage_dir_list(const char* path, file_info_t* files, uint32_t max_count);

// =============================================================================
// Recording Management
// =============================================================================

/**
 * @brief התחלת הקלטה חדשה
 * @param file מבנה קובץ (פלט)
 * @return STORAGE_OK בהצלחה
 */
storage_error_t storage_recording_start(storage_file_t* file);

/**
 * @brief סיום הקלטה ועדכון header
 * @param file מבנה קובץ
 * @param sample_count מספר דגימות
 */
storage_error_t storage_recording_finish(storage_file_t* file, uint32_t sample_count);

/**
 * @brief קבלת רשימת הקלטות
 * @param recordings מערך פלט
 * @param max_count גודל מקסימלי
 * @return מספר הקלטות
 */
int32_t storage_recording_list(recording_info_t* recordings, uint32_t max_count);

/**
 * @brief מחיקת הקלטה
 */
storage_error_t storage_recording_delete(const char* filename);

/**
 * @brief מחיקת כל ההקלטות
 */
storage_error_t storage_recording_delete_all(void);

/**
 * @brief קבלת סה"כ מקום הקלטות
 */
uint32_t storage_recording_total_size(void);

/**
 * @brief קבלת מספר הקלטות
 */
uint32_t storage_recording_count(void);

// =============================================================================
// Configuration Storage
// =============================================================================

/**
 * @brief שמירת נתון בקונפיגורציה
 * @param key מפתח
 * @param data נתונים
 * @param size גודל
 */
storage_error_t storage_config_set(const char* key, const void* data, size_t size);

/**
 * @brief קריאת נתון מקונפיגורציה
 * @param key מפתח
 * @param data באפר פלט
 * @param size גודל מקסימלי
 * @return גודל הנתונים שנקראו
 */
int32_t storage_config_get(const char* key, void* data, size_t size);

/**
 * @brief מחיקת נתון מקונפיגורציה
 */
storage_error_t storage_config_delete(const char* key);

/**
 * @brief בדיקה אם מפתח קיים
 */
bool storage_config_exists(const char* key);

// =============================================================================
// Backup & Export
// =============================================================================

/**
 * @brief העתקת הקלטות מ-SPIFFS ל-SD
 * @return מספר קבצים שהועתקו, או -1 בשגיאה
 */
int32_t storage_backup_to_sd(void);

/**
 * @brief יצירת קובץ גיבוי מלא
 * @param backup_path נתיב לקובץ הגיבוי
 */
storage_error_t storage_create_backup(const char* backup_path);

/**
 * @brief שחזור מגיבוי
 * @param backup_path נתיב לקובץ הגיבוי
 */
storage_error_t storage_restore_backup(const char* backup_path);

// =============================================================================
// WAV File Helpers
// =============================================================================

/**
 * @brief כתיבת WAV header
 * @param file קובץ פתוח
 * @param sample_rate קצב דגימה
 * @param bits_per_sample ביטים לדגימה
 * @param channels ערוצים
 */
storage_error_t storage_wav_write_header(storage_file_t* file, 
                                         uint16_t sample_rate,
                                         uint8_t bits_per_sample,
                                         uint8_t channels);

/**
 * @brief עדכון WAV header עם גודל סופי
 * @param file קובץ פתוח
 * @param data_size גודל נתוני האודיו
 */
storage_error_t storage_wav_update_header(storage_file_t* file, uint32_t data_size);

/**
 * @brief קריאת WAV header
 * @param file קובץ פתוח
 * @param info מבנה פלט
 */
storage_error_t storage_wav_read_header(storage_file_t* file, recording_info_t* info);

// =============================================================================
// Utilities
// =============================================================================

/**
 * @brief פרמוט גודל לטקסט (1.5 MB, 320 KB, etc.)
 * @param bytes גודל בבתים
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 */
void storage_format_size(uint32_t bytes, char* buffer, size_t buffer_size);

/**
 * @brief יצירת שם קובץ הקלטה ייחודי
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 */
void storage_generate_recording_name(char* buffer, size_t buffer_size);

/**
 * @brief קבלת סוג האחסון מנתיב
 */
storage_type_t storage_get_type_from_path(const char* path);

#endif // HAL_STORAGE_H

