#include "state.h"
#include "macros.h"

int updateStateTransmitter(State *currentState, unsigned char byte) {
    switch (*currentState) {
        case START:
            if (byte == FLAG)
                *currentState = FLAG_RCV;
            break;
        case FLAG_RCV:
            if (byte == A_RX)
                *currentState = A_RCV;
            else if (byte != FLAG)
                *currentState = START;
            break;
        case A_RCV:
            if (byte == UA)
                *currentState = C_RCV;
            else if (byte == FLAG)
                *currentState = FLAG_RCV;
            else
                *currentState = START;
            break;
        case C_RCV:
            if (byte == (A_RX ^ UA))
                *currentState = BCC_OK;
            else if (byte == FLAG)
                *currentState = FLAG_RCV;
            else
                *currentState = START;
            break;
        case BCC_OK:
            if (byte == FLAG)
                *currentState = STOP;
            else
                *currentState = START;
            break;
        default:
            break;
    }
    return *currentState;
}

// Function to check if the state is final
int isStateFinal(State state) {
    return (state == STOP);
}

