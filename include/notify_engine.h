#ifndef NOTIFY_ENGINE_H
#define NOTIFY_ENGINE_H

#include <stdint.h>

// Initialize notification engine
void notify_init();

// Notification loop - expires old notifications
void notify_loop();

// Push a new notification
void notify_push(const char* text, int duration_sec = 60, bool urgent = false);

// Check if there's an active notification to display
bool notify_has_active();

// Get current notification text
const char* notify_get_text();

// Check if current notification is urgent
bool notify_is_urgent();

// Dismiss current notification
void notify_dismiss();

#endif // NOTIFY_ENGINE_H
