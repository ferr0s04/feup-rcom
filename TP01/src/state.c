#include "state.h"
#include "macros.h"

// Function to process state machine
State processState(State currentState, unsigned char byte, unsigned char *address, unsigned char *control, unsigned char *bcc) {
    switch (currentState) {
        case START:
            if (byte == FLAG) {
                return FLAG_RCV;  // Transition to FLAG_RCV if FLAG is received
            }
            break;

        case FLAG_RCV:
            if (byte == A_TX || byte == A_RX) {  // Expected address
                *address = byte;
                return A_RCV;  // Transition to A_RCV
            } else if (byte == FLAG) {
                return FLAG_RCV;  // Stay in FLAG_RCV if another FLAG is received
            } else {
                return START;  // Restart on Other_RCV
            }
            break;

        case A_RCV:
            if (byte == FLAG) {
                return FLAG_RCV;  // Return to FLAG_RCV if FLAG is received
            }
            *control = byte;
            return C_RCV;  // Transition to C_RCV
            break;

        case C_RCV:
            if (byte == FLAG) {
                return FLAG_RCV;  // Return to FLAG_RCV if FLAG is received
            }
            *bcc = *address ^ *control;  // Calculate BCC1
            if (byte == *bcc) {
                return BCC_OK;  // If BCC is correct, transition to BCC_OK
            } else {
                return START;  // Restart if BCC is incorrect
            }
            break;

        case BCC_OK:
            if (byte == FLAG) {
                return STOP;  // Transition to STOP on receiving FLAG
            } else {
                return START;  // Restart if not FLAG
            }
            break;

        default:
            return START;  // Unknown state, restart the state machine
    }

    return currentState;  // Return current state if no transition occurred
}

// Function to check if the state is final
int isStateFinal(State state) {
    return (state == STOP);
}

