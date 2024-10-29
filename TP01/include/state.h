#ifndef _STATE_H_
#define _STATE_H_

typedef enum {
    START,
    FLAG_RCV,
    A_RCV,
    C_RCV,
    BCC_OK,
    STOP
} State;

int updateStateTransmitter(State *currentState, unsigned char byte);
int isStateFinal(State state);

#endif // _STATE_H_
