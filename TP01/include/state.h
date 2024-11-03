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

typedef struct {
    State state;
    int isREJ;
    int ignore;
} StateMachine;

StateMachine createStateMachine();
StateMachine nextState(StateMachine sm, unsigned char byte, unsigned char control, unsigned char command);
int isStateFinal(StateMachine sm);

#endif // _STATE_H_
