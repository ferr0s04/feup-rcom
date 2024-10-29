#ifndef ALARM_H
#define ALARM_H

#define FALSE 0
#define TRUE 1

extern int alarmEnabled;  // Flag to indicate if the alarm is enabled
extern int alarmCount;    // Counter to track the number of alarms triggered

// Function prototypes

/**
 * @brief Alarm handler function.
 * 
 * This function is called when the SIGALRM signal is triggered. It disables
 * the alarm and increments the alarm counter.
 * 
 * @param signal Signal number (SIGALRM)
 */
void alarmHandler(int signal);

#endif /* ALARM_H */

