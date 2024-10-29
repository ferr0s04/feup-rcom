#ifndef ALARM_H
#define ALARM_H

#define FALSE 0
#define TRUE 1

extern int alarmEnabled;
extern int alarmCount;

void alarmHandler(int signal);

#endif /* ALARM_H */

