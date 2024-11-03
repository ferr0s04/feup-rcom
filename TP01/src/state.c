#include "state.h"
#include "macros.h"

#include <stdlib.h>
#include <stdio.h>


StateMachine createStateMachine() {
    StateMachine sm;
    sm.state = START;
    sm.isREJ = 0;
    sm.ignore = 0;
    return sm;
}


StateMachine nextState(StateMachine sm, unsigned char byte, unsigned char control, unsigned char command)
{
    switch (sm.state)
    {
    case START:
        if (byte == FLAG)
        {
            sm.state = FLAG_RCV;
        }
        else {
            sm.state = START;
        }
        break;
    case FLAG_RCV:
        if (byte == control)
        {
            sm.state = A_RCV;
        }
        else if (byte == FLAG)
        {
            sm.state = FLAG_RCV;
        }
        else {
            sm.ignore = TRUE;
            sm.state = START;
        }
        break;
    case A_RCV:
        if (byte == command)
        {
            sm.state = C_RCV;
        }
        else if (byte == FLAG)
        {
            sm.state = FLAG_RCV;
        }
        else if (byte == (REJ | (RR & 0x80))) {
            sm.isREJ = TRUE;
            sm.state = START;
        }
        else {
            sm.ignore = TRUE;
            sm.state = START;
        }
        break;
    case C_RCV:
        if (byte == (control ^ command))
        {
            sm.state = BCC_OK;
        }
        else if (byte == FLAG)
        {
            sm.state = FLAG_RCV;
        }
        else {
            sm.ignore = TRUE;
            sm.state = START;
        }
        break;
    case BCC_OK:
        if (byte == FLAG)
        {
            sm.state = STOP;
        }
        else {
            sm.ignore = TRUE;
            sm.state = START;
        }
        break;
    case STOP:
        sm.state = STOP;
        break;
    default:
        sm.state = START;
        break;
    }
    return sm;
}


int isStateFinal(StateMachine sm) {
    return (sm.state == STOP);
}
