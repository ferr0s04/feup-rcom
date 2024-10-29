#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include "alarm.h"

int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
    printf("Enabled = %d\n", alarmEnabled);
}
