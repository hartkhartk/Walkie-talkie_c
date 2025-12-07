/**
 * @file config.h
 * @brief הגדרות תצורה למכשיר הקשר
 */

#ifndef CONFIG_H
#define CONFIG_H

// =============================================================================
// Device Configuration
// =============================================================================

#define DEVICE_NAME             "WT-PRO"
#define FIRMWARE_VERSION        "1.0.0"
#define DEVICE_ID_LENGTH        8       // 8 digits (numbers only, 0-9)
#define FREQUENCY_ID_LENGTH     8       // 8 digits (numbers only, 0-9)

// Device ID: ייחודי לצמיתות - נוצר פעם אחת ונשמר
// Frequency ID: ייחודי רק בזמן שהתדר פעיל - אחרי סגירה הקוד חוזר להיות פנוי

// =============================================================================
// Hardware Pin Definitions (ESP32)
// =============================================================================

// Keypad Pins (4x3 matrix for 0-9 + extra buttons)
#define PIN_KEYPAD_ROW_0        4
#define PIN_KEYPAD_ROW_1        5
#define PIN_KEYPAD_ROW_2        18
#define PIN_KEYPAD_ROW_3        19
#define PIN_KEYPAD_COL_0        21
#define PIN_KEYPAD_COL_1        22
#define PIN_KEYPAD_COL_2        23

// Function Buttons
#define PIN_BTN_GREEN           25      // כפתור ירוק
#define PIN_BTN_RED             26      // כפתור אדום
#define PIN_BTN_ABOVE_GREEN     27      // מעל הירוק
#define PIN_BTN_ABOVE_RED       32      // מעל האדום
#define PIN_BTN_MULTI           33      // כפתור רב-פעולות
#define PIN_BTN_RECORD          34      // כפתור הקלטה

// Integrated PTT Button with Slide Switch
// כפתור PTT משולב עם מתג הזזה בתוכו (3 מצבים)
// המתג בתוך הכפתור: למעלה=תמיד, אמצע=לחיצה, למטה=מושתק
// לוחצים על הכפתור עצמו בשביל PTT (רלוונטי רק במצב אמצע)
#define PIN_PTT_BUTTON          35      // לחיצת PTT (הכפתור עצמו)
#define PIN_PTT_SLIDE_A         36      // מתג הזזה - bit A (בתוך הכפתור)
#define PIN_PTT_SLIDE_B         39      // מתג הזזה - bit B (בתוך הכפתור)

// Visibility Slide Switch
#define PIN_SW_VISIBILITY       13      // מתג גלוי/מוסתר

// Rotary Encoders
#define PIN_VOLUME_A            14      // גלגלת ווליום - A
#define PIN_VOLUME_B            12      // גלגלת ווליום - B
#define PIN_MODE_DIAL           15      // גלגלת 15 מצבים (ADC)

// Display (SPI)
#define PIN_DISPLAY_CS          2
#define PIN_DISPLAY_DC          16
#define PIN_DISPLAY_RST         17
#define PIN_DISPLAY_SCK         SCK     // Default SPI
#define PIN_DISPLAY_MOSI        MOSI    // Default SPI

// Audio
#define PIN_AUDIO_OUT           DAC1    // GPIO25 on ESP32
#define PIN_AUDIO_IN            ADC1_CH0 // GPIO36 on ESP32
#define PIN_SPEAKER_EN          0       // Speaker enable

// Radio Module (SX1276/RFM95)
#define PIN_RADIO_CS            5
#define PIN_RADIO_RST           14
#define PIN_RADIO_DIO0          2

// USB
#define PIN_USB_DETECT          37

// Battery
#define PIN_BATTERY_ADC         38      // Battery voltage ADC
#define PIN_CHARGE_STATUS       1       // Charging indicator

// =============================================================================
// Timing Constants (ms)
// =============================================================================

#define LONG_PRESS_DURATION     3000    // לחיצה ארוכה = 3 שניות
#define DEBOUNCE_TIME           50      // זמן debounce לכפתורים
#define SCAN_TIMEOUT            5000    // זמן סריקה
#define CALL_TIMEOUT            30000   // זמן המתנה לתשובה לשיחה
#define DISPLAY_REFRESH_RATE    100     // רענון מסך

// =============================================================================
// Protocol Constants
// =============================================================================

// FREQUENCY_ID_LENGTH מוגדר למעלה עם DEVICE_ID_LENGTH
#define PASSWORD_MAX_LENGTH     16      // אורך מקסימלי לסיסמה
#define MAX_SAVED_CODES         50      // מקסימום קודים שמורים
#define MAX_FREQ_MEMBERS        100     // מקסימום משתתפים בתדר
#define MAX_SCAN_RESULTS        20      // מקסימום תוצאות סריקה

// =============================================================================
// Radio Configuration
// =============================================================================

#define RADIO_FREQUENCY         433E6   // 433 MHz (or 868/915 depending on region)
#define RADIO_BANDWIDTH         125E3   // 125 kHz bandwidth
#define RADIO_TX_POWER          20      // dBm
#define RADIO_SPREADING_FACTOR  7

// =============================================================================
// Display Configuration
// =============================================================================

#define DISPLAY_WIDTH           128
#define DISPLAY_HEIGHT          64
#define DISPLAY_ROTATION        0

// =============================================================================
// Audio Configuration
// =============================================================================

#define AUDIO_SAMPLE_RATE       8000    // 8 kHz
#define AUDIO_BITS              16
#define AUDIO_BUFFER_SIZE       256

#endif // CONFIG_H

