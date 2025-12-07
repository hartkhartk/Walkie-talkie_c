/**
 * @file display.h
 * @brief Hardware Abstraction Layer - מסך
 */

#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Display Constants
// =============================================================================

#define DISPLAY_LINES           8       // מספר שורות טקסט (64px / 8px per char)
#define DISPLAY_CHARS_PER_LINE  21      // תווים בשורה (128px / 6px per char)

// =============================================================================
// Text Alignment
// =============================================================================

typedef enum {
    ALIGN_LEFT = 0,
    ALIGN_CENTER,
    ALIGN_RIGHT
} text_align_t;

// =============================================================================
// Font Size
// =============================================================================

typedef enum {
    FONT_SMALL = 0,     // 6x8 pixels
    FONT_MEDIUM,        // 8x12 pixels
    FONT_LARGE          // 12x16 pixels
} font_size_t;

// =============================================================================
// Icons
// =============================================================================

typedef enum {
    ICON_NONE = 0,
    ICON_BATTERY_FULL,
    ICON_BATTERY_MED,
    ICON_BATTERY_LOW,
    ICON_BATTERY_CHARGING,
    ICON_SIGNAL_FULL,
    ICON_SIGNAL_MED,
    ICON_SIGNAL_LOW,
    ICON_SIGNAL_NONE,
    ICON_LOCKED,
    ICON_UNLOCKED,
    ICON_VISIBLE,
    ICON_HIDDEN,
    ICON_MICROPHONE,
    ICON_MICROPHONE_MUTED,
    ICON_SPEAKER,
    ICON_SPEAKER_MUTED,
    ICON_RECORDING,
    ICON_CALL,
    ICON_FREQUENCY,
    ICON_ARROW_UP,
    ICON_ARROW_DOWN,
    ICON_ARROW_LEFT,
    ICON_ARROW_RIGHT,
    ICON_CHECK,
    ICON_CROSS,
    ICON_STAR,
    ICON_COUNT
} icon_t;

// =============================================================================
// Screen Regions
// =============================================================================

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
} display_region_t;

// Predefined regions
#define REGION_STATUS_BAR   (display_region_t){0, 0, 128, 8}
#define REGION_MAIN         (display_region_t){0, 8, 128, 48}
#define REGION_FOOTER       (display_region_t){0, 56, 128, 8}

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול המסך
 */
void display_init(void);

/**
 * @brief ניקוי המסך
 */
void display_clear(void);

/**
 * @brief ניקוי אזור ספציפי
 * @param region האזור לניקוי
 */
void display_clear_region(display_region_t region);

/**
 * @brief עדכון המסך (שליחת buffer לחומרה)
 */
void display_update(void);

/**
 * @brief הדפסת טקסט
 * @param x מיקום X
 * @param y מיקום Y
 * @param text הטקסט להדפסה
 * @param font גודל גופן
 */
void display_print(uint8_t x, uint8_t y, const char* text, font_size_t font);

/**
 * @brief הדפסת טקסט עם יישור
 * @param y מיקום Y
 * @param text הטקסט להדפסה
 * @param font גודל גופן
 * @param align יישור
 */
void display_print_aligned(uint8_t y, const char* text, font_size_t font, text_align_t align);

/**
 * @brief הדפסת שורה מודגשת (נבחרת)
 * @param line_num מספר השורה (0-7)
 * @param text הטקסט
 * @param is_selected האם נבחר (צבעים הפוכים)
 */
void display_print_line(uint8_t line_num, const char* text, bool is_selected);

/**
 * @brief הצגת אייקון
 * @param x מיקום X
 * @param y מיקום Y
 * @param icon סוג האייקון
 */
void display_icon(uint8_t x, uint8_t y, icon_t icon);

/**
 * @brief ציור קו
 * @param x1 נקודת התחלה X
 * @param y1 נקודת התחלה Y
 * @param x2 נקודת סיום X
 * @param y2 נקודת סיום Y
 */
void display_line(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

/**
 * @brief ציור מלבן
 * @param x מיקום X
 * @param y מיקום Y
 * @param width רוחב
 * @param height גובה
 * @param filled האם מלא
 */
void display_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool filled);

/**
 * @brief הצגת סרגל התקדמות
 * @param x מיקום X
 * @param y מיקום Y
 * @param width רוחב
 * @param progress אחוז (0-100)
 */
void display_progress_bar(uint8_t x, uint8_t y, uint8_t width, uint8_t progress);

/**
 * @brief הצגת שורת סטטוס עליונה
 * @param battery_level רמת סוללה (0-100)
 * @param signal_level עוצמת קליטה (0-100)
 * @param is_recording האם מקליט
 * @param visibility_mode מצב נראות
 */
void display_status_bar(uint8_t battery_level, uint8_t signal_level, 
                        bool is_recording, bool is_visible);

/**
 * @brief הצגת הודעה במרכז המסך
 * @param title כותרת
 * @param message הודעה
 */
void display_message(const char* title, const char* message);

/**
 * @brief הצגת דיאלוג אישור
 * @param title כותרת
 * @param message הודעה
 * @param green_text טקסט לכפתור ירוק
 * @param red_text טקסט לכפתור אדום
 */
void display_confirm_dialog(const char* title, const char* message,
                           const char* green_text, const char* red_text);

/**
 * @brief הצגת שדה הזנת טקסט/מספרים
 * @param label תווית
 * @param value הערך הנוכחי
 * @param cursor_pos מיקום הסמן
 * @param max_length אורך מקסימלי
 */
void display_input_field(const char* label, const char* value, 
                         uint8_t cursor_pos, uint8_t max_length);

/**
 * @brief הצגת רשימה עם גלילה
 * @param items מערך פריטים
 * @param item_count מספר פריטים
 * @param selected_index אינדקס נבחר
 * @param scroll_offset היסט גלילה
 */
void display_list(const char** items, uint8_t item_count, 
                  uint8_t selected_index, uint8_t scroll_offset);

/**
 * @brief הפעלת/כיבוי תאורת רקע
 * @param on מופעל/כבוי
 */
void display_backlight(bool on);

/**
 * @brief כוונון בהירות
 * @param level רמה (0-100)
 */
void display_set_brightness(uint8_t level);

/**
 * @brief כניסה למצב חיסכון
 */
void display_sleep(void);

/**
 * @brief יציאה ממצב חיסכון
 */
void display_wake(void);

#endif // HAL_DISPLAY_H

