/**
 * @file device_state.h
 * @brief מצבי המכשיר ומכונת מצבים
 */

#ifndef CORE_DEVICE_STATE_H
#define CORE_DEVICE_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Device States
// =============================================================================

typedef enum {
    // Main states
    STATE_IDLE,                 // מצב המתנה ראשי - מחוץ לשיחה/תדר
    STATE_IN_CALL,              // בתוך שיחה (1-על-1)
    STATE_IN_FREQUENCY,         // בתוך תדר (קבוצה)
    
    // Sub-states for menus
    STATE_INPUT_CODE,           // הזנת קוד (מכשיר או תדר)
    STATE_SCANNING,             // סריקה לגילוי מכשירים/תדרים
    STATE_SCAN_RESULTS,         // הצגת תוצאות סריקה
    STATE_SAVED_LIST,           // רשימת קודים שמורים
    STATE_INVITE_MENU,          // תפריט הזמנת מכשירים
    
    // Frequency creation states
    STATE_FREQ_CREATE_TYPE,     // בחירת סוג תדר (גלוי/נסתר)
    STATE_FREQ_CREATE_PROTECT,  // בחירת הגנה
    STATE_FREQ_CREATE_PASSWORD, // הזנת סיסמה
    
    // Connection states
    STATE_WAITING_RESPONSE,     // ממתין לתשובה (שיחה/הצטרפות)
    STATE_INCOMING_REQUEST,     // בקשה נכנסת (שיחה/הצטרפות)
    STATE_PASSWORD_ENTRY,       // הזנת סיסמה להצטרפות
    
    // Other states
    STATE_MESSAGE,              // הצגת הודעה
    STATE_ERROR                 // מצב שגיאה
} device_state_t;

// =============================================================================
// Frequency/Call Types
// =============================================================================

typedef enum {
    FREQ_TYPE_VISIBLE = 1,      // תדר גלוי
    FREQ_TYPE_HIDDEN = 2        // תדר נסתר
} frequency_type_t;

typedef enum {
    FREQ_PROTECT_NONE = 1,          // ללא הגנה
    FREQ_PROTECT_PASSWORD = 2,      // סיסמה
    FREQ_PROTECT_APPROVAL = 3,      // אישור מנהל
    FREQ_PROTECT_BOTH = 4           // סיסמה + אישור
} frequency_protection_t;

// =============================================================================
// Connection Info Structures
// =============================================================================

typedef struct {
    char id[DEVICE_ID_LENGTH + 1];      // מזהה מכשיר
    char name[16];                       // שם תצוגה (אופציונלי)
    int8_t signal_strength;             // עוצמת אות (-127 to 0 dBm)
    bool is_saved;                      // האם שמור ברשימה
} device_info_t;

typedef struct {
    char id[FREQUENCY_ID_LENGTH + 1];   // מזהה תדר
    frequency_type_t type;              // סוג (גלוי/נסתר)
    frequency_protection_t protection;  // סוג הגנה
    uint8_t member_count;               // מספר חברים
    bool is_admin;                      // האם אני המנהל
    bool is_saved;                      // האם שמור ברשימה
} frequency_info_t;

typedef struct {
    bool is_frequency;                  // true=תדר, false=מכשיר
    union {
        device_info_t device;
        frequency_info_t frequency;
    } info;
} scan_result_t;

// =============================================================================
// Saved Code Entry
// =============================================================================

typedef struct {
    bool is_frequency;                  // true=תדר, false=מכשיר
    char code[FREQUENCY_ID_LENGTH + 1]; // קוד (8 תווים)
    char name[16];                      // שם מותאם אישית
} saved_code_t;

// =============================================================================
// Device Context (Main State Container)
// =============================================================================

typedef struct {
    // Identity
    char device_id[DEVICE_ID_LENGTH + 1];       // מזהה המכשיר שלנו
    
    // Current state
    device_state_t current_state;
    device_state_t previous_state;              // למעבר חזרה
    
    // Input buffer
    char input_buffer[FREQUENCY_ID_LENGTH + 1];
    uint8_t input_cursor;
    
    // Connection status
    bool is_connected;
    bool is_muted;                              // השתקה דו-צדדית
    
    // Current connection (if connected)
    bool connected_to_frequency;                // true=תדר, false=שיחה
    union {
        device_info_t device;
        frequency_info_t frequency;
    } current_connection;
    
    // Frequency management (if we're admin)
    char pending_requests[MAX_FREQ_MEMBERS][DEVICE_ID_LENGTH + 1];
    uint8_t pending_request_count;
    
    // Scan results
    scan_result_t scan_results[MAX_SCAN_RESULTS];
    uint8_t scan_result_count;
    uint8_t scan_selected_index;
    
    // Saved codes
    saved_code_t saved_codes[MAX_SAVED_CODES];
    uint8_t saved_code_count;
    uint8_t saved_selected_index;
    
    // Hardware status
    uint8_t battery_level;                      // 0-100
    uint8_t signal_strength;                    // 0-100
    bool is_recording;
    bool is_visible;                            // מתג נראות
    
    // Timing
    uint32_t state_enter_time;                  // זמן כניסה למצב נוכחי
    uint32_t last_activity_time;                // זמן פעילות אחרון
    
    // Temporary state data
    frequency_type_t new_freq_type;
    frequency_protection_t new_freq_protection;
    char temp_password[PASSWORD_MAX_LENGTH + 1];
    
    // Message display
    char message_title[32];
    char message_text[64];
    uint32_t message_timeout;
    
} device_context_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול הקונטקסט
 */
void device_init(device_context_t* ctx);

/**
 * @brief מעבר למצב חדש
 */
void device_set_state(device_context_t* ctx, device_state_t new_state);

/**
 * @brief חזרה למצב קודם
 */
void device_go_back(device_context_t* ctx);

/**
 * @brief עדכון הלוגיקה (קרא בלופ הראשי)
 */
void device_update(device_context_t* ctx);

/**
 * @brief טיפול בלחיצת כפתור
 */
void device_handle_button(device_context_t* ctx, button_id_t button, button_event_t event);

/**
 * @brief הוספת ספרה לשדה הקלט
 */
void device_input_digit(device_context_t* ctx, uint8_t digit);

/**
 * @brief ניקוי שדה הקלט
 */
void device_clear_input(device_context_t* ctx);

/**
 * @brief שמירת קוד לרשימה
 */
bool device_save_code(device_context_t* ctx, bool is_frequency, const char* code, const char* name);

/**
 * @brief מחיקת קוד מהרשימה
 */
bool device_delete_saved_code(device_context_t* ctx, uint8_t index);

/**
 * @brief קבלת מצב נוכחי כטקסט
 */
const char* device_state_name(device_state_t state);

#endif // CORE_DEVICE_STATE_H

