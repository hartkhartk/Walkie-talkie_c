/**
 * @file dial_manager.h
 * @brief ניהול גלגלת 15 המצבים עם threads מוגבלים
 * 
 * כל מיקום בגלגלת יכול להחזיק חיבור פעיל.
 * מקסימום 15 threads במקביל (אחד לכל מיקום).
 */

#ifndef CORE_DIAL_MANAGER_H
#define CORE_DIAL_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Constants
// =============================================================================

#define DIAL_POSITIONS          15      // מספר מיקומים בגלגלת
#define MAX_DIAL_THREADS        15      // מקסימום threads במקביל
#define DIAL_TASK_STACK_SIZE    4096    // גודל stack לכל task
#define DIAL_TASK_PRIORITY      5       // עדיפות task

// =============================================================================
// Dial Slot State
// =============================================================================

typedef enum {
    DIAL_SLOT_EMPTY = 0,        // מיקום ריק
    DIAL_SLOT_SAVED,            // יש קוד שמור אבל לא מחובר
    DIAL_SLOT_CONNECTING,       // בתהליך התחברות
    DIAL_SLOT_CONNECTED,        // מחובר ופעיל
    DIAL_SLOT_ERROR             // שגיאה בחיבור
} dial_slot_state_t;

typedef enum {
    DIAL_CONN_DEVICE = 0,       // חיבור למכשיר (שיחה)
    DIAL_CONN_FREQUENCY         // חיבור לתדר (קבוצה)
} dial_connection_type_t;

// =============================================================================
// Dial Slot Structure
// =============================================================================

typedef struct {
    // Slot configuration
    bool is_configured;                     // האם יש קוד שמור
    dial_connection_type_t conn_type;       // סוג החיבור
    char code[DEVICE_ID_LENGTH + 1];        // קוד שמור
    char name[16];                          // שם תצוגה
    char password[PASSWORD_MAX_LENGTH + 1]; // סיסמה (אם צריך)
    
    // Connection state
    dial_slot_state_t state;                // מצב נוכחי
    bool is_muted;                          // האם מושתק
    bool is_active_audio;                   // האם אודיו פעיל (המיקום הנבחר)
    
    // Frequency-specific
    bool is_admin;                          // האם מנהל התדר
    uint8_t member_count;                   // מספר משתתפים
    
    // Thread handle
#ifdef ESP32
    TaskHandle_t task_handle;               // Handle ל-task של החיבור
#else
    void* task_handle;                      // Placeholder לסימולטור
#endif
    
    // Statistics
    uint32_t connect_time;                  // זמן התחברות
    uint32_t bytes_sent;                    // בתים שנשלחו
    uint32_t bytes_received;                // בתים שהתקבלו
    int8_t signal_strength;                 // עוצמת אות
    
} dial_slot_t;

// =============================================================================
// Dial Manager Structure
// =============================================================================

typedef struct {
    dial_slot_t slots[DIAL_POSITIONS];      // 15 מיקומים
    uint8_t current_position;               // מיקום נוכחי (0-14)
    uint8_t active_threads;                 // מספר threads פעילים
    
    // Mutex for thread-safe access
#ifdef ESP32
    SemaphoreHandle_t mutex;
#else
    void* mutex;
#endif
    
} dial_manager_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מנהל הגלגלת
 */
void dial_manager_init(dial_manager_t* dm);

/**
 * @brief שינוי מיקום נוכחי
 * @param dm מנהל הגלגלת
 * @param position מיקום חדש (0-14)
 * @return true אם הצליח
 */
bool dial_manager_set_position(dial_manager_t* dm, uint8_t position);

/**
 * @brief סיבוב הגלגלת
 * @param dm מנהל הגלגלת
 * @param direction כיוון (+1 או -1)
 * @return המיקום החדש
 */
uint8_t dial_manager_rotate(dial_manager_t* dm, int8_t direction);

/**
 * @brief שמירת קוד למיקום
 * @param dm מנהל הגלגלת
 * @param position מיקום (0-14)
 * @param conn_type סוג החיבור
 * @param code קוד לשמירה
 * @param name שם תצוגה
 * @return true אם הצליח
 */
bool dial_manager_save_slot(dial_manager_t* dm, uint8_t position,
                           dial_connection_type_t conn_type,
                           const char* code, const char* name);

/**
 * @brief מחיקת מיקום
 * @param dm מנהל הגלגלת
 * @param position מיקום למחיקה
 * @return true אם הצליח
 */
bool dial_manager_clear_slot(dial_manager_t* dm, uint8_t position);

/**
 * @brief התחברות למיקום
 * יוצר thread חדש אם אין חיבור פעיל
 * @param dm מנהל הגלגלת
 * @param position מיקום להתחברות
 * @return true אם הצליח
 */
bool dial_manager_connect(dial_manager_t* dm, uint8_t position);

/**
 * @brief ניתוק ממיקום
 * מסיים את ה-thread של החיבור
 * @param dm מנהל הגלגלת
 * @param position מיקום לניתוק
 * @return true אם הצליח
 */
bool dial_manager_disconnect(dial_manager_t* dm, uint8_t position);

/**
 * @brief ניתוק כל החיבורים
 * @param dm מנהל הגלגלת
 */
void dial_manager_disconnect_all(dial_manager_t* dm);

/**
 * @brief העברת אודיו למיקום
 * @param dm מנהל הגלגלת
 * @param position מיקום לאודיו פעיל
 * @return true אם הצליח
 */
bool dial_manager_set_active_audio(dial_manager_t* dm, uint8_t position);

/**
 * @brief השתקת מיקום
 * @param dm מנהל הגלגלת
 * @param position מיקום
 * @param muted האם להשתיק
 */
void dial_manager_set_muted(dial_manager_t* dm, uint8_t position, bool muted);

/**
 * @brief קבלת מספר threads פעילים
 * @param dm מנהל הגלגלת
 * @return מספר threads
 */
uint8_t dial_manager_get_active_count(dial_manager_t* dm);

/**
 * @brief קבלת מידע על מיקום
 * @param dm מנהל הגלגלת
 * @param position מיקום
 * @return מצביע ל-slot (או NULL)
 */
const dial_slot_t* dial_manager_get_slot(dial_manager_t* dm, uint8_t position);

/**
 * @brief קבלת המיקום הנוכחי
 * @param dm מנהל הגלגלת
 * @return מיקום נוכחי (0-14)
 */
uint8_t dial_manager_get_position(dial_manager_t* dm);

/**
 * @brief שמירת כל המיקומים ל-NVS
 * @param dm מנהל הגלגלת
 * @return true אם הצליח
 */
bool dial_manager_save_to_nvs(dial_manager_t* dm);

/**
 * @brief טעינת כל המיקומים מ-NVS
 * @param dm מנהל הגלגלת
 * @return true אם הצליח
 */
bool dial_manager_load_from_nvs(dial_manager_t* dm);

#endif // CORE_DIAL_MANAGER_H

