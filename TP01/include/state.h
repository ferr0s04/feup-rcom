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

State nextState(State state, unsigned char byte, unsigned char control, unsigned char command);
int isStateFinal(State state);

#endif // _STATE_H_
