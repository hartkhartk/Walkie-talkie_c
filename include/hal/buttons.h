/**
 * @file buttons.h
 * @brief Hardware Abstraction Layer - כפתורים ומתגים
 */

#ifndef HAL_BUTTONS_H
#define HAL_BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// Button Identifiers
// =============================================================================

typedef enum {
    // Numeric keypad
    BTN_0 = 0,
    BTN_1,
    BTN_2,
    BTN_3,
    BTN_4,
    BTN_5,
    BTN_6,
    BTN_7,
    BTN_8,
    BTN_9,
    
    // Function buttons
    BTN_GREEN,          // כפתור ירוק
    BTN_RED,            // כפתור אדום
    BTN_ABOVE_GREEN,    // מעל הירוק
    BTN_ABOVE_RED,      // מעל האדום
    BTN_MULTI,          // כפתור רב-פעולות
    BTN_RECORD,         // כפתור הקלטה
    BTN_PTT,            // Push-to-Talk
    
    BTN_COUNT           // Total number of buttons
} button_id_t;

// =============================================================================
// Button Events
// =============================================================================

typedef enum {
    BTN_EVENT_NONE = 0,
    BTN_EVENT_PRESS,        // לחיצה רגילה
    BTN_EVENT_RELEASE,      // שחרור
    BTN_EVENT_LONG_PRESS,   // לחיצה ארוכה (3 שניות)
    BTN_EVENT_REPEAT        // לחיצה ממושכת (חזרות)
} button_event_t;

// =============================================================================
// Integrated PTT Button with Slide Switch
// =============================================================================
//
// העיצוב הפיזי:
// ┌─────────────────┐
// │   ▲ ALWAYS      │  ← מתג הזזה למעלה = דיבור תמיד
// │   ├─────────────┤
// │   │   [PTT]     │  ← כפתור לחיצה (רלוונטי במצב PTT)
// │   ├─────────────┤
// │   ● PTT         │  ← מתג הזזה אמצע = דיבור בלחיצה
// │   ├─────────────┤
// │   ▼ MUTED       │  ← מתג הזזה למטה = מושתק תמיד
// └─────────────────┘
//
// מתג ההזזה משולב פיזית בתוך כפתור ה-PTT.
// כשהמתג במצב "אמצע" (PTT), צריך ללחוץ על הכפתור כדי לדבר.
// כשהמתג במצב "למעלה" (ALWAYS), הדיבור פתוח תמיד.
// כשהמתג במצב "למטה" (MUTED), הדיבור מושתק תמיד.

// מצבי מתג הדיבור המשולב (3 מצבים)
typedef enum {
    TALK_MODE_ALWAYS = 0,   // דיבור תמיד (מתג למעלה)
    TALK_MODE_PTT,          // דיבור בלחיצה (מתג אמצע) - צריך ללחוץ על הכפתור
    TALK_MODE_MUTED         // השתקת הדיבור תמיד (מתג למטה)
} talk_mode_t;

// מתג נראות (2 מצבים)
typedef enum {
    VISIBILITY_VISIBLE = 0, // מכשיר גלוי
    VISIBILITY_HIDDEN       // מכשיר מוסתר
} visibility_mode_t;

// =============================================================================
// Rotary Encoder States
// =============================================================================

typedef struct {
    int8_t delta;           // שינוי מאז הקריאה האחרונה (-1, 0, +1)
    uint8_t absolute;       // ערך מוחלט (0-100 לווליום, 0-14 למצבים)
} rotary_state_t;

// =============================================================================
// Button State Structure
// =============================================================================

typedef struct {
    bool is_pressed;
    uint32_t press_start_time;
    bool long_press_triggered;
} button_state_t;

// =============================================================================
// Callback Types
// =============================================================================

typedef void (*button_callback_t)(button_id_t button, button_event_t event);
typedef void (*switch_callback_t)(void);
typedef void (*rotary_callback_t)(int8_t delta);

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מערכת הכפתורים
 */
void buttons_init(void);

/**
 * @brief עדכון מצב הכפתורים (קרא בלופ הראשי)
 */
void buttons_update(void);

/**
 * @brief בדיקה אם כפתור לחוץ כרגע
 * @param button מזהה הכפתור
 * @return true אם לחוץ
 */
bool buttons_is_pressed(button_id_t button);

/**
 * @brief קבלת אירוע כפתור (polling)
 * @param button מזהה הכפתור
 * @return סוג האירוע
 */
button_event_t buttons_get_event(button_id_t button);

/**
 * @brief רישום callback לאירועי כפתורים
 * @param callback פונקציית callback
 */
void buttons_set_callback(button_callback_t callback);

/**
 * @brief קבלת מצב מתג הדיבור (המשולב בכפתור PTT)
 * @return מצב הדיבור הנוכחי
 */
talk_mode_t buttons_get_talk_mode(void);

/**
 * @brief בדיקה האם המשתמש משדר כרגע
 * לוקח בחשבון את מצב המתג ואת לחיצת הכפתור:
 * - ALWAYS: תמיד true
 * - PTT: true רק אם הכפתור לחוץ
 * - MUTED: תמיד false
 * @return true אם המשתמש משדר
 */
bool buttons_is_transmitting(void);

/**
 * @brief קבלת מצב מתג הנראות
 * @return מצב הנראות הנוכחי
 */
visibility_mode_t buttons_get_visibility_mode(void);

/**
 * @brief רישום callback לשינוי מתג דיבור
 * @param callback פונקציית callback
 */
void buttons_set_talk_mode_callback(switch_callback_t callback);

/**
 * @brief רישום callback לשינוי מתג נראות
 * @param callback פונקציית callback
 */
void buttons_set_visibility_callback(switch_callback_t callback);

/**
 * @brief קבלת מצב גלגלת הווליום
 * @return מבנה עם שינוי וערך מוחלט
 */
rotary_state_t buttons_get_volume(void);

/**
 * @brief קבלת מצב גלגלת המצבים (0-14)
 * @return מספר המצב הנוכחי
 */
uint8_t buttons_get_mode_dial(void);

/**
 * @brief רישום callback לשינוי ווליום
 * @param callback פונקציית callback
 */
void buttons_set_volume_callback(rotary_callback_t callback);

/**
 * @brief קבלת ספרה שהוזנה (0-9), או -1 אם אין
 * @return ספרה או -1
 */
int8_t buttons_get_digit_input(void);

/**
 * @brief ניקוי כל האירועים הממתינים
 */
void buttons_clear_events(void);

#endif // HAL_BUTTONS_H

