/**
 * @file audio_buffer.h
 * @brief Ring buffer לנתוני אודיו
 * 
 * Lock-free ring buffer לשימוש בין ISR ל-task.
 * תומך ב-audio frames עם sequence numbers ו-timestamps.
 */

#ifndef CORE_AUDIO_BUFFER_H
#define CORE_AUDIO_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

// =============================================================================
// Audio Frame Configuration
// =============================================================================

#define AUDIO_FRAME_SAMPLES     160         // 20ms @ 8kHz
#define AUDIO_FRAME_SIZE        (AUDIO_FRAME_SAMPLES * 2)  // 16-bit samples
#define AUDIO_BUFFER_FRAMES     32          // Number of frames in ring buffer
#define AUDIO_FRAME_DURATION_MS 20          // Frame duration

// =============================================================================
// Audio Frame Structure
// =============================================================================

/**
 * @brief מבנה frame של אודיו
 * 
 * כולל מידע על sequence ו-timestamp לצורך
 * זיהוי חבילות חסרות ותיקון jitter
 */
typedef struct {
    uint32_t timestamp;                     // זמן יצירת ה-frame (ms)
    uint16_t sequence;                      // מספר רצף
    uint16_t length;                        // אורך הנתונים בפועל
    uint8_t  samples[AUDIO_FRAME_SIZE];     // נתוני אודיו (PCM16)
    bool     valid;                         // האם ה-frame תקין
} audio_frame_t;

// =============================================================================
// Ring Buffer Statistics
// =============================================================================

typedef struct {
    uint32_t frames_written;                // סה"כ frames שנכתבו
    uint32_t frames_read;                   // סה"כ frames שנקראו
    uint32_t frames_dropped;                // frames שנשמטו (buffer full)
    uint32_t frames_missed;                 // frames שחסרו (sequence gap)
    uint32_t buffer_overruns;               // פעמים שה-buffer התמלא
    uint32_t buffer_underruns;              // פעמים שה-buffer התרוקן
    uint32_t max_fill_level;                // מילוי מקסימלי שנמדד
    uint16_t last_sequence;                 // sequence אחרון שהתקבל
} audio_buffer_stats_t;

// =============================================================================
// Ring Buffer Structure
// =============================================================================

/**
 * @brief מבנה Ring Buffer לאודיו
 * 
 * תוכנן להיות lock-free:
 * - Writer (ISR) כותב ל-write_idx
 * - Reader (Task) קורא מ-read_idx
 * - אין צורך ב-mutex כל עוד יש writer אחד ו-reader אחד
 */
typedef struct {
    audio_frame_t frames[AUDIO_BUFFER_FRAMES];  // מערך ה-frames
    volatile uint8_t write_idx;                  // אינדקס כתיבה
    volatile uint8_t read_idx;                   // אינדקס קריאה
    uint16_t next_sequence;                      // sequence הבא לכתיבה
    audio_buffer_stats_t stats;                  // סטטיסטיקות
} audio_ring_buffer_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול ה-ring buffer
 * @param buffer מצביע ל-buffer
 */
void audio_buffer_init(audio_ring_buffer_t* buffer);

/**
 * @brief ניקוי ה-buffer
 * @param buffer מצביע ל-buffer
 */
void audio_buffer_clear(audio_ring_buffer_t* buffer);

/**
 * @brief בדיקה האם ה-buffer ריק
 * @param buffer מצביע ל-buffer
 * @return true אם ריק
 */
bool audio_buffer_is_empty(const audio_ring_buffer_t* buffer);

/**
 * @brief בדיקה האם ה-buffer מלא
 * @param buffer מצביע ל-buffer
 * @return true אם מלא
 */
bool audio_buffer_is_full(const audio_ring_buffer_t* buffer);

/**
 * @brief כמות frames ב-buffer
 * @param buffer מצביע ל-buffer
 * @return מספר frames
 */
uint8_t audio_buffer_count(const audio_ring_buffer_t* buffer);

/**
 * @brief אחוז מילוי ה-buffer
 * @param buffer מצביע ל-buffer
 * @return אחוז מילוי (0-100)
 */
uint8_t audio_buffer_fill_percent(const audio_ring_buffer_t* buffer);

// =============================================================================
// Write Operations (for ISR/Capture)
// =============================================================================

/**
 * @brief כתיבת frame ל-buffer
 * 
 * מיועד לקריאה מ-ISR או task קלט.
 * מוסיף sequence ו-timestamp אוטומטית.
 * 
 * @param buffer מצביע ל-buffer
 * @param samples נתוני אודיו
 * @param length אורך הנתונים
 * @param timestamp זמן (אם 0, ייוצר אוטומטית)
 * @return true אם הכתיבה הצליחה, false אם ה-buffer מלא
 */
bool audio_buffer_write(audio_ring_buffer_t* buffer, 
                        const uint8_t* samples, 
                        uint16_t length,
                        uint32_t timestamp);

/**
 * @brief כתיבת frame מוכן ל-buffer
 * 
 * @param buffer מצביע ל-buffer
 * @param frame מצביע ל-frame
 * @return true אם הכתיבה הצליחה
 */
bool audio_buffer_write_frame(audio_ring_buffer_t* buffer,
                              const audio_frame_t* frame);

// =============================================================================
// Read Operations (for Task/Playback)
// =============================================================================

/**
 * @brief קריאת frame מה-buffer
 * 
 * מיועד לקריאה מ-task פלט.
 * 
 * @param buffer מצביע ל-buffer
 * @param frame מצביע לקבלת ה-frame
 * @return true אם הקריאה הצליחה, false אם ה-buffer ריק
 */
bool audio_buffer_read(audio_ring_buffer_t* buffer, 
                       audio_frame_t* frame);

/**
 * @brief הצצה ל-frame הבא בלי להוציא
 * 
 * @param buffer מצביע ל-buffer
 * @param frame מצביע לקבלת ה-frame
 * @return true אם יש frame, false אם ריק
 */
bool audio_buffer_peek(const audio_ring_buffer_t* buffer,
                       audio_frame_t* frame);

/**
 * @brief דילוג על frame
 * 
 * @param buffer מצביע ל-buffer
 * @return true אם הדילוג הצליח
 */
bool audio_buffer_skip(audio_ring_buffer_t* buffer);

// =============================================================================
// Statistics
// =============================================================================

/**
 * @brief קבלת סטטיסטיקות
 * @param buffer מצביע ל-buffer
 * @return מצביע לסטטיסטיקות (read-only)
 */
const audio_buffer_stats_t* audio_buffer_get_stats(const audio_ring_buffer_t* buffer);

/**
 * @brief איפוס סטטיסטיקות
 * @param buffer מצביע ל-buffer
 */
void audio_buffer_reset_stats(audio_ring_buffer_t* buffer);

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * @brief חישוב זמן ב-buffer (ms)
 * 
 * @param buffer מצביע ל-buffer
 * @return זמן אודיו ב-ms
 */
uint32_t audio_buffer_duration_ms(const audio_ring_buffer_t* buffer);

/**
 * @brief בדיקת פער ב-sequence
 * 
 * @param expected ה-sequence הצפוי
 * @param received ה-sequence שהתקבל
 * @return מספר frames שחסרים (0 אם אין פער)
 */
uint16_t audio_buffer_sequence_gap(uint16_t expected, uint16_t received);

// =============================================================================
// Jitter Buffer Functions
// =============================================================================

/**
 * @brief הגדרת עומק jitter buffer
 * 
 * מגדיר כמה frames לאגור לפני התחלת ניגון
 * לצורך פיצוי על jitter ברשת.
 * 
 * @param buffer מצביע ל-buffer
 * @param frames מספר frames (ברירת מחדל: 3)
 */
void audio_buffer_set_jitter_depth(audio_ring_buffer_t* buffer, uint8_t frames);

/**
 * @brief בדיקה האם ה-jitter buffer מלא מספיק
 * 
 * @param buffer מצביע ל-buffer
 * @return true אם יש מספיק frames לניגון
 */
bool audio_buffer_jitter_ready(const audio_ring_buffer_t* buffer);

#endif // CORE_AUDIO_BUFFER_H

