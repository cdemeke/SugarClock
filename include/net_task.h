#ifndef NET_TASK_H
#define NET_TASK_H

// Start the background network task (core 0) that drives HTTP glucose
// and weather fetches without blocking the UI loop on core 1.
void net_task_start();

// Acquire before changing configuration. False means a fetch is still active;
// do not mutate settings. Release on the same task with net_task_resume().
bool net_task_quiesce(unsigned long timeout_ms = 1000);
void net_task_resume();

#endif // NET_TASK_H
