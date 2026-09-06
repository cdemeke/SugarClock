#ifndef NET_TASK_H
#define NET_TASK_H

// Start the background network task (core 0) that drives HTTP glucose
// and weather fetches without blocking the UI loop on core 1.
void net_task_start();

#endif // NET_TASK_H
