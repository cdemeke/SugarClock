#ifndef NET_TASK_H
#define NET_TASK_H

// Background network task.
//
// Runs all blocking HTTPS work (glucose polling, weather polling, forced
// fetches from the web UI) on a dedicated FreeRTOS task pinned to core 0,
// so the render loop on core 1 never stalls behind a network call.
//
// The task subscribes to the task watchdog and feeds it between HTTP
// steps, so a hung TLS connection still reboots the device while normal
// slow fetches do not.

// Create and start the network task. Call once from setup(), after
// http_init(), weather_init(), and esp_task_wdt_init().
void net_task_start();

#endif // NET_TASK_H
