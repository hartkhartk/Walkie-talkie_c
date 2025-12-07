/**
 * @file tasks.h
 * @brief הגדרות Tasks של FreeRTOS
 * 
 * ארכיטקטורת Tasks:
 * - task_audio_in:  קלט אודיו מהמיקרופון (עדיפות גבוהה)
 * - task_audio_out: פלט אודיו לרמקול (עדיפות גבוהה)
 * - task_comm:      תקשורת RF (עדיפות בינונית)
 * - task_ui:        ממשק משתמש (עדיפות נמוכה)
 */

#ifndef CORE_TASKS_H
#define CORE_TASKS_H

#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// FreeRTOS Configuration
// =============================================================================

#ifdef ESP32
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
    #include "freertos/queue.h"
    #include "freertos/semphr.h"
    #include "freertos/event_groups.h"
#else
    // Mock types for non-ESP32 builds
    typedef void* TaskHandle_t;
    typedef void* QueueHandle_t;
    typedef void* SemaphoreHandle_t;
    typedef void* EventGroupHandle_t;
    typedef uint32_t EventBits_t;
    #define pdMS_TO_TICKS(ms) (ms)
    #define portTICK_PERIOD_MS 1
#endif

// =============================================================================
// Task Priorities (higher = more important)
// =============================================================================

#define TASK_PRIORITY_AUDIO_IN      (configMAX_PRIORITIES - 1)  // Highest
#define TASK_PRIORITY_AUDIO_OUT     (configMAX_PRIORITIES - 1)  // Highest
#define TASK_PRIORITY_COMM          (configMAX_PRIORITIES - 2)  // High
#define TASK_PRIORITY_PROTOCOL      (configMAX_PRIORITIES - 3)  // Medium-High
#define TASK_PRIORITY_UI            (configMAX_PRIORITIES - 4)  // Medium
#define TASK_PRIORITY_IDLE          1                            // Lowest

// =============================================================================
// Stack Sizes
// =============================================================================

#define TASK_STACK_AUDIO_IN         2048
#define TASK_STACK_AUDIO_OUT        2048
#define TASK_STACK_COMM             4096
#define TASK_STACK_PROTOCOL         3072
#define TASK_STACK_UI               2048

// =============================================================================
// Task Handles
// =============================================================================

typedef struct {
    TaskHandle_t audio_in;
    TaskHandle_t audio_out;
    TaskHandle_t comm;
    TaskHandle_t protocol;
    TaskHandle_t ui;
} task_handles_t;

// =============================================================================
// Event Bits
// =============================================================================

// Audio events
#define EVENT_AUDIO_DATA_READY      (1 << 0)
#define EVENT_AUDIO_BUFFER_LOW      (1 << 1)
#define EVENT_AUDIO_BUFFER_FULL     (1 << 2)
#define EVENT_AUDIO_START_TX        (1 << 3)
#define EVENT_AUDIO_STOP_TX         (1 << 4)

// Communication events
#define EVENT_COMM_PACKET_RECEIVED  (1 << 5)
#define EVENT_COMM_PACKET_SENT      (1 << 6)
#define EVENT_COMM_TX_READY         (1 << 7)
#define EVENT_COMM_ERROR            (1 << 8)

// Connection events
#define EVENT_CONNECTION_REQUEST    (1 << 9)
#define EVENT_CONNECTION_ACCEPTED   (1 << 10)
#define EVENT_CONNECTION_REJECTED   (1 << 11)
#define EVENT_CONNECTION_LOST       (1 << 12)

// UI events
#define EVENT_UI_BUTTON_PRESS       (1 << 13)
#define EVENT_UI_DIAL_CHANGE        (1 << 14)
#define EVENT_UI_REFRESH_NEEDED     (1 << 15)

// =============================================================================
// Message Queues
// =============================================================================

// Queue lengths
#define QUEUE_AUDIO_TX_LEN          16      // Audio frames to transmit
#define QUEUE_AUDIO_RX_LEN          16      // Audio frames received
#define QUEUE_COMM_TX_LEN           8       // Packets to transmit
#define QUEUE_COMM_RX_LEN           8       // Packets received
#define QUEUE_UI_EVENTS_LEN         10      // UI events

// Queue item sizes
typedef struct {
    QueueHandle_t audio_tx;         // Audio frames to transmit
    QueueHandle_t audio_rx;         // Received audio frames
    QueueHandle_t comm_tx;          // Packets to transmit
    QueueHandle_t comm_rx;          // Received packets
    QueueHandle_t ui_events;        // UI event queue
} task_queues_t;

// =============================================================================
// Synchronization
// =============================================================================

typedef struct {
    SemaphoreHandle_t audio_buffer_mutex;   // Protect audio buffer access
    SemaphoreHandle_t comm_tx_mutex;        // Protect TX operations
    SemaphoreHandle_t state_mutex;          // Protect device state
    EventGroupHandle_t system_events;       // System-wide events
} task_sync_t;

// =============================================================================
// Task Context
// =============================================================================

typedef struct {
    task_handles_t handles;
    task_queues_t queues;
    task_sync_t sync;
    bool initialized;
} tasks_context_t;

// =============================================================================
// API Functions
// =============================================================================

/**
 * @brief אתחול מערכת ה-Tasks
 * @param ctx מצביע ל-context
 * @return true בהצלחה
 */
bool tasks_init(tasks_context_t* ctx);

/**
 * @brief הפעלת כל ה-Tasks
 * @param ctx מצביע ל-context
 * @return true בהצלחה
 */
bool tasks_start(tasks_context_t* ctx);

/**
 * @brief עצירת כל ה-Tasks
 * @param ctx מצביע ל-context
 */
void tasks_stop(tasks_context_t* ctx);

/**
 * @brief בדיקת מצב ה-Tasks
 * @param ctx מצביע ל-context
 * @return true אם כל ה-tasks רצים
 */
bool tasks_are_running(const tasks_context_t* ctx);

// =============================================================================
// Event Functions
// =============================================================================

/**
 * @brief הגדרת event bits
 * @param ctx מצביע ל-context
 * @param bits הביטים להגדרה
 */
void tasks_set_event(tasks_context_t* ctx, EventBits_t bits);

/**
 * @brief ניקוי event bits
 * @param ctx מצביע ל-context
 * @param bits הביטים לניקוי
 */
void tasks_clear_event(tasks_context_t* ctx, EventBits_t bits);

/**
 * @brief המתנה ל-event
 * @param ctx מצביע ל-context
 * @param bits_to_wait הביטים להמתין להם
 * @param wait_all true להמתין לכולם
 * @param timeout_ms timeout במילישניות
 * @return הביטים שהופעלו
 */
EventBits_t tasks_wait_event(
    tasks_context_t* ctx,
    EventBits_t bits_to_wait,
    bool wait_all,
    uint32_t timeout_ms
);

// =============================================================================
// Task Statistics
// =============================================================================

typedef struct {
    uint32_t run_count;             // פעמים שה-task רץ
    uint32_t wake_count;            // פעמים שה-task התעורר
    uint32_t error_count;           // שגיאות
    uint32_t high_watermark;        // שימוש מקסימלי ב-stack
    uint32_t avg_runtime_us;        // זמן ריצה ממוצע
    uint32_t last_run_time;         // זמן ריצה אחרון
} task_stats_t;

typedef struct {
    task_stats_t audio_in;
    task_stats_t audio_out;
    task_stats_t comm;
    task_stats_t protocol;
    task_stats_t ui;
    uint32_t free_heap;
    uint32_t min_free_heap;
    uint32_t uptime_seconds;
} system_stats_t;

/**
 * @brief קבלת סטטיסטיקות מערכת
 * @param stats מצביע לקבלת הסטטיסטיקות
 */
void tasks_get_stats(system_stats_t* stats);

// =============================================================================
// Individual Task Declarations
// =============================================================================

/**
 * @brief Task קלט אודיו
 * 
 * אחראי על:
 * - דגימת אודיו מהמיקרופון
 * - כתיבה ל-ring buffer
 * - יצירת audio frames
 * 
 * עדיפות: גבוהה ביותר (ISR-like)
 * קצב: כל 20ms (50Hz)
 */
void task_audio_in(void* param);

/**
 * @brief Task פלט אודיו
 * 
 * אחראי על:
 * - קריאה מ-ring buffer
 * - ניגון אודיו לרמקול
 * - ניהול jitter buffer
 * 
 * עדיפות: גבוהה ביותר (ISR-like)
 * קצב: כל 20ms (50Hz)
 */
void task_audio_out(void* param);

/**
 * @brief Task תקשורת RF
 * 
 * אחראי על:
 * - שליחת וקבלת חבילות
 * - ניהול מודול RF
 * - שידור והאזנה
 * 
 * עדיפות: גבוהה
 */
void task_comm(void* param);

/**
 * @brief Task פרוטוקול
 * 
 * אחראי על:
 * - פענוח הודעות
 * - בניית חבילות
 * - מכונת מצבים של פרוטוקול
 * - הצפנה/פענוח
 * 
 * עדיפות: בינונית-גבוהה
 */
void task_protocol(void* param);

/**
 * @brief Task ממשק משתמש
 * 
 * אחראי על:
 * - קריאת כפתורים
 * - עדכון תצוגה
 * - תפריטים
 * 
 * עדיפות: בינונית
 * קצב: כל 50ms (20Hz)
 */
void task_ui(void* param);

// =============================================================================
// Watchdog
// =============================================================================

/**
 * @brief רישום task ל-watchdog
 * @param task_id מזהה ה-task
 */
void tasks_watchdog_register(uint8_t task_id);

/**
 * @brief דיווח פעילות ל-watchdog
 * @param task_id מזהה ה-task
 */
void tasks_watchdog_feed(uint8_t task_id);

/**
 * @brief בדיקת watchdog
 * @return true אם כל ה-tasks מדווחים
 */
bool tasks_watchdog_check(void);

#endif // CORE_TASKS_H

