#include "state.h"
#include "macros.h"

#include <stdlib.h>
#include <stdio.h>

State nextState(State state, unsigned char byte, unsigned char control, unsigned char command)
{
    switch (state)
    {
    case START:
        if (byte == FLAG)
        {
            return FLAG_RCV;
        }
        else {
            return START;
        }
        break;
    case FLAG_RCV:
        if (byte == control)
        {
            return A_RCV;
        }
        else if (byte == FLAG)
        {
            return FLAG_RCV;
        }
        else {
            return START;
        }
        break;
    case A_RCV:
        if (byte == command)
        {
            return C_RCV;
        }
        else if (byte == FLAG)
        {
            return FLAG_RCV;
        }
        else {
            return START;
        }
        break;
    case C_RCV:
        if (byte == (control ^ command))
        {
            return BCC_OK;
        }
        else if (byte == FLAG)
        {
            return FLAG_RCV;
        }
        else {
            return START;
        }
        break;
    case BCC_OK:
        if (byte == FLAG)
        {
            return STOP;
        }
        else {
            return START;
        }
        break;
    case STOP:
        return STOP;
        break;
    default:
        return START;
        break;
    }
    return START;
}


int isStateFinal(State state) {
    return (state == STOP);
}
