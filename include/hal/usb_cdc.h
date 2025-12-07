/**
 * @file usb_cdc.h
 * @brief מודול USB CDC ו-Mass Storage
 * 
 * תומך ב:
 * - USB CDC לתקשורת סריאלית
 * - USB Mass Storage לגישה לכרטיס SD
 * - גיבוי וייצוא הקלטות דרך USB
 */

#ifndef HAL_USB_CDC_H
#define HAL_USB_CDC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// =============================================================================
// USB Constants
// =============================================================================

#define USB_CDC_BUFFER_SIZE     512
#define USB_VID                 0x303A      // Espressif VID
#define USB_PID                 0x4001      // Custom PID for Walkie-Talkie
#define USB_MANUFACTURER        "WT-PRO"
#define USB_PRODUCT             "Walkie-Talkie"
#define USB_SERIAL_PREFIX       "WT"

// =============================================================================
// USB Mode
// =============================================================================

typedef enum {
    USB_MODE_NONE = 0,
    USB_MODE_CDC,               // Serial communication
    USB_MODE_MSC,               // Mass Storage (SD card access)
    USB_MODE_CDC_MSC            // Combined CDC + MSC
} usb_mode_t;

// =============================================================================
// USB State
// =============================================================================

typedef enum {
    USB_STATE_DISCONNECTED = 0,
    USB_STATE_CONNECTED,
    USB_STATE_SUSPENDED,
    USB_STATE_CONFIGURED
} usb_state_t;

// =============================================================================
// Callback Types
// =============================================================================

/**
 * @brief Callback כשמתקבלים נתונים ב-CDC
 * @param data הנתונים שהתקבלו
 * @param length אורך הנתונים
 */
typedef void (*usb_cdc_rx_callback_t)(const uint8_t* data, size_t length);

/**
 * @brief Callback כששינוי מצב USB
 * @param state המצב החדש
 */
typedef void (*usb_state_callback_t)(usb_state_t state);

// =============================================================================
// USB Info
// =============================================================================

typedef struct {
    usb_mode_t mode;
    usb_state_t state;
    bool cdc_connected;
    bool msc_connected;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    char serial_number[32];
} usb_info_t;

// =============================================================================
// Initialization
// =============================================================================

/**
 * @brief אתחול מודול USB
 * @param mode מצב USB רצוי
 * @return true בהצלחה
 */
bool usb_init(usb_mode_t mode);

/**
 * @brief שחרור משאבי USB
 */
void usb_deinit(void);

/**
 * @brief בדיקה אם USB מאותחל
 */
bool usb_is_initialized(void);

/**
 * @brief שינוי מצב USB
 * @param mode מצב חדש
 * @return true בהצלחה
 */
bool usb_set_mode(usb_mode_t mode);

/**
 * @brief קבלת מצב USB נוכחי
 */
usb_mode_t usb_get_mode(void);

/**
 * @brief קבלת מידע על USB
 */
void usb_get_info(usb_info_t* info);

// =============================================================================
// CDC (Serial) Functions
// =============================================================================

/**
 * @brief בדיקה אם CDC מחובר
 */
bool usb_cdc_is_connected(void);

/**
 * @brief שליחת נתונים דרך CDC
 * @param data נתונים לשליחה
 * @param length אורך
 * @return מספר בתים שנשלחו
 */
int32_t usb_cdc_write(const uint8_t* data, size_t length);

/**
 * @brief שליחת מחרוזת דרך CDC
 * @param str מחרוזת לשליחה
 * @return מספר בתים שנשלחו
 */
int32_t usb_cdc_print(const char* str);

/**
 * @brief שליחת מחרוזת מפורמטת דרך CDC
 * @param format פורמט printf
 * @return מספר בתים שנשלחו
 */
int32_t usb_cdc_printf(const char* format, ...);

/**
 * @brief קריאת נתונים מ-CDC
 * @param buffer באפר פלט
 * @param max_length גודל מקסימלי
 * @return מספר בתים שנקראו
 */
int32_t usb_cdc_read(uint8_t* buffer, size_t max_length);

/**
 * @brief קריאת שורה מ-CDC
 * @param buffer באפר פלט
 * @param max_length גודל מקסימלי
 * @return מספר בתים שנקראו (כולל \n)
 */
int32_t usb_cdc_readline(char* buffer, size_t max_length);

/**
 * @brief בדיקה כמה בתים ממתינים לקריאה
 */
int32_t usb_cdc_available(void);

/**
 * @brief ניקוי באפר קבלה
 */
void usb_cdc_flush_rx(void);

/**
 * @brief המתנה עד שכל הנתונים נשלחו
 */
void usb_cdc_flush_tx(void);

/**
 * @brief רישום callback לקבלת נתונים
 */
void usb_cdc_set_rx_callback(usb_cdc_rx_callback_t callback);

// =============================================================================
// MSC (Mass Storage) Functions
// =============================================================================

/**
 * @brief בדיקה אם MSC מחובר
 */
bool usb_msc_is_connected(void);

/**
 * @brief הפעלת מצב Mass Storage
 * 
 * כשמופעל, כרטיס ה-SD יופיע כדיסק נייד במחשב
 * 
 * @return true בהצלחה
 */
bool usb_msc_enable(void);

/**
 * @brief כיבוי מצב Mass Storage
 */
void usb_msc_disable(void);

/**
 * @brief בדיקה אם המחשב כותב לדיסק
 * 
 * חשוב לבדוק לפני ניתוק!
 */
bool usb_msc_is_writing(void);

/**
 * @brief סנכרון כל הנתונים לדיסק
 */
void usb_msc_sync(void);

// =============================================================================
// State & Callbacks
// =============================================================================

/**
 * @brief קבלת מצב USB
 */
usb_state_t usb_get_state(void);

/**
 * @brief רישום callback לשינוי מצב
 */
void usb_set_state_callback(usb_state_callback_t callback);

/**
 * @brief בדיקה אם כבל USB מחובר פיזית
 */
bool usb_cable_connected(void);

// =============================================================================
// Device Info
// =============================================================================

/**
 * @brief קבלת מספר סריאלי ייחודי
 * @param buffer באפר פלט
 * @param buffer_size גודל הבאפר
 */
void usb_get_serial_number(char* buffer, size_t buffer_size);

/**
 * @brief הגדרת מספר סריאלי מותאם
 * @param serial מספר סריאלי
 */
void usb_set_serial_number(const char* serial);

// =============================================================================
// Command Interface
// =============================================================================

/**
 * @brief פקודות שניתן לשלוח דרך CDC
 */
typedef enum {
    USB_CMD_NONE = 0,
    USB_CMD_GET_INFO,           // קבלת מידע על המכשיר
    USB_CMD_GET_STATUS,         // קבלת סטטוס
    USB_CMD_GET_RECORDINGS,     // רשימת הקלטות
    USB_CMD_DELETE_RECORDING,   // מחיקת הקלטה
    USB_CMD_DOWNLOAD_RECORDING, // הורדת הקלטה
    USB_CMD_SET_CONFIG,         // הגדרת קונפיגורציה
    USB_CMD_GET_CONFIG,         // קריאת קונפיגורציה
    USB_CMD_REBOOT,             // אתחול מחדש
    USB_CMD_FACTORY_RESET,      // איפוס להגדרות יצרן
    USB_CMD_OTA_START,          // התחלת עדכון OTA
} usb_command_t;

/**
 * @brief עיבוד פקודה שהתקבלה
 * @param cmd מחרוזת הפקודה
 * @param response באפר לתשובה
 * @param response_size גודל הבאפר
 * @return true אם הפקודה הובנה
 */
bool usb_process_command(const char* cmd, char* response, size_t response_size);

/**
 * @brief הפעלת ממשק פקודות אינטראקטיבי
 * 
 * צריך להיקרא מהלופ הראשי
 */
void usb_command_loop(void);

// =============================================================================
// Update Loop
// =============================================================================

/**
 * @brief עדכון מצב USB
 * 
 * צריך להיקרא מהלופ הראשי
 */
void usb_update(void);

#endif // HAL_USB_CDC_H

