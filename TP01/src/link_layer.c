#include "link_layer.h"
#include "serial_port.h"
#include "macros.h"
#include "state.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

typedef struct {
    int framesSent;
    int framesReceived;
    int retransmissions;
    int timeouts;
    int receivedRR;
    int receivedREJ;
    int receivedDuplicate;
    double elapsedTime;
} Statistics;

int alarmEnabled = FALSE;
int alarmCount = 0;
extern int fd;
unsigned char byte;
int nr = 1;
int ns = 0;
unsigned char seq = 0xFF;
LinkLayer linkLayer;
Statistics statistics;
struct timespec start, end;

void alarmHandler(int signal);
int llopenTransmitter();
int llopenReceiver();
int llcloseTransmitter();
int llcloseReceiver();
void printStatistics();
int buildPacket(const unsigned char *buf, int bufSize, unsigned char *packet, int ns);


void alarmHandler(int signal) {
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
}


////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    linkLayer = connectionParameters;
    fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);

    if (fd < 0) {
        printf("ERROR: failed to open serial port\n");
        exit(-1);
    }

    clock_gettime(CLOCK_REALTIME, &start);

    switch (connectionParameters.role) {
        case LlTx:
            return llopenTransmitter(connectionParameters);
        case LlRx:
            return llopenReceiver();
        default:
            printf("ERROR: invalid role at llopen\n");
            return -1;
    }
}


////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    unsigned char _buf[2 * MAX_PAYLOAD_SIZE];

    int bytes = 0;
    alarmCount = 0;
    alarmEnabled = FALSE;
    StateMachine state = createStateMachine();

    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {
        if (alarmEnabled == FALSE || state.isREJ == TRUE) {
            if (state.isREJ == TRUE) {
                statistics.retransmissions++;
                alarmCount++;
                state.state = START;
                state.isREJ = FALSE;
                alarmEnabled = FALSE;
                continue;
            }

            (void)signal(SIGALRM, alarmHandler);
            int packetSize = buildPacket(buf, bufSize, _buf, ns);
            bytes = writeBytesSerialPort(_buf, packetSize);

            if (bytes < 0) exit(-1);
            statistics.framesSent++;

            state.state = START;
            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        if (readByteSerialPort(_buf) == 0) {
            statistics.timeouts++;
            continue;
        }

        state = nextState(state, *_buf, A_TX, (RR | (nr << 7)));
        if (isStateFinal(state)) {
            printf("RR received\n");
            statistics.receivedRR++;
            statistics.framesReceived++;
            break;
        } else if (state.isREJ == TRUE) {
            printf("REJ received\n");
            statistics.receivedREJ++;
            readByteSerialPort(_buf);
            readByteSerialPort(_buf);
        }
    }

    if (isStateFinal(state)) {
        nr = ns;
        ns = (ns + 1) % 2;
        return bytes;
    } else {
        printf("ERROR: llwrite\n");
        return -1;
    }
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char buf[6];
    StateMachine state = createStateMachine();

    while (state.state != BCC_OK) {
        if (readByteSerialPort(buf) == 0) continue;

        if (state.state == A_RCV && (*buf == (0 << 7) || (*buf == (1 << 7)))) {
            ns = (*buf >> 7);
            nr = (ns + 1) % 2;
        }

        state = nextState(state, *buf, A_TX, ns << 7);
        if (state.ignore == TRUE) return 0;
    }

    unsigned char bcc2 = 0;
    int i = 0, data = 0;

    while (!isStateFinal(state)) {
        if (state.ignore == TRUE || state.isREJ == TRUE) break;
        if (readByteSerialPort(buf) == 0) continue;
        if (i == 0) data = (*buf == 1);

        if (i == 1 && data) {
            printf("Packet #%d \n", *buf);
            if (seq == *buf) {
                state.ignore = TRUE;
                printf("Duplicated packet\n");
                statistics.receivedDuplicate++;
                break;
            }
            seq = *buf;
        }

        if (*buf == FLAG) {
            state.state = STOP;
            break;
        }

        if (*buf == ESC) {
            while (*buf == ESC)
                readByteSerialPort(buf);

            if (*buf == ESC_FLAG) *buf = FLAG;
            else if (*buf == ESC_ESC) *buf = ESC;
        }

        *(packet + i) = *buf;
        i++;
        bcc2 ^= *buf;
    }

    if (bcc2 != 0) {
        printf("Wrong BCC2\n");
        state.isREJ = TRUE;
        seq--;
    }

    *(packet + i) = '\0';
    i--;

    buf[0] = FLAG;
    buf[1] = A_TX;
    buf[2] = (state.isREJ == FALSE ? RR : REJ) | (nr << 7);
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;

    writeBytesSerialPort(buf, 6);
    printf("%s sent\n", state.isREJ == FALSE ? "RR" : "REJ");

    if (state.isREJ == TRUE) return 0;
    statistics.framesReceived++;
    return i;
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {   
    int clstat;

    if (linkLayer.role == LlTx) {
        llcloseTransmitter();
    } else if (linkLayer.role == LlRx) {
        llcloseReceiver();
    } else {
        printf("ERROR: invalid role at llclose\n");
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &end);
    statistics.elapsedTime = (end.tv_sec-start.tv_sec)+ (end.tv_nsec-start.tv_nsec)/1e9;

    clstat = closeSerialPort();
    
    if (showStatistics) {
        printStatistics();
    }
    
    return clstat;
}


int llopenTransmitter() {
    unsigned char buf[5] = {FLAG, A_TX, SET, A_TX ^ SET, FLAG};
    StateMachine state = createStateMachine();
    int bytes;

    while (!isStateFinal(state) && linkLayer.nRetransmissions > alarmCount) {
        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);
            alarm(linkLayer.timeout);

            if (writeBytesSerialPort(buf, 5) == -1) {
            printf("ERROR: failed to write SET\n");
            exit(-1);
            }

            state.state = START;
            alarmEnabled = TRUE;
        }

        if ((bytes = readByteSerialPort(buf)) <= 0) {
            if (bytes < 0) exit(-1);
            continue;
        }

        if (state.ignore == TRUE) {
            state.state = START;
        }

        state = nextState(state, *buf, A_TX, UA);

        if (isStateFinal(state)) {
            printf("UA received\n");
            break;
        }
    }

    if (!isStateFinal(state)) return -1;
    return 1;
}


int llopenReceiver() {
    unsigned char buf[5];
    StateMachine state = createStateMachine();

    while (1) {

        if (state.ignore == TRUE) {
            state.state = START;
        }

        int bytes;
        if ((bytes = readByteSerialPort(buf)) == 0) continue;

        state = nextState(state, *buf, A_TX, SET);
        if (isStateFinal(state)) {
            printf("SET received\n");
            break;
        }
    }

    buf[0] = FLAG;
    buf[1] = A_TX;
    buf[2] = UA;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;

    writeBytesSerialPort(buf, 5);
    printf("UA sent\n");

    return 1;
}


int llcloseTransmitter() {
    alarmEnabled = FALSE;
    alarmCount = 0;

    unsigned char buf[BUFFER_SIZE + 1] = {FLAG, A_TX, DISC, A_TX ^ DISC, FLAG}, _buf[BUFFER_SIZE + 1];

    StateMachine state = createStateMachine();
    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {
        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);

            writeBytesSerialPort(buf, 6);
            printf("DISC sent\n");

            state.state = START;

            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        int bytes = readByteSerialPort(_buf);
        if (bytes < 0) exit(-1);
        else if (bytes == 0) continue;

        state = nextState(state, *_buf, A_RX, DISC);
        if (isStateFinal(state)) {
            printf("DISC received\n");
        }
    }

    if (!isStateFinal(state)) {
        printf("ERROR: llclose\n");
        return -1;
    }

    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = UA;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;

    if (!writeBytesSerialPort(buf, 6)) {
        printf("ERROR: UA\n");
        exit(-1);
    }

    printf("UA sent\n");

    return 1;
}


int llcloseReceiver() {
    unsigned char buf[BUFFER_SIZE + 1] = {0};
    StateMachine state = createStateMachine();
    while (1) {
        if (readByteSerialPort(buf) == 0) continue;

        state = nextState(state, *buf, A_TX, DISC);
        if (isStateFinal(state)) {
            printf("DISC received\n");
            break;
        }
    }

    unsigned char _buf[BUFFER_SIZE + 1] = {FLAG, A_RX, DISC, A_RX ^ DISC, FLAG, '\0'};
    alarmEnabled = FALSE;
    alarmCount = 0;
    state.state = START;

    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {

        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);

            writeBytesSerialPort(_buf, 6);
            printf("DISC sent\n");

            state.state = START;

            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        int bytes = readByteSerialPort(_buf);
        if (bytes < 0) exit(-1);
        if (bytes == 0) continue;

        state = nextState(state, *_buf, A_RX, UA);
        if (isStateFinal(state)) {
            printf("UA received\n");
            break;
        }
    }

    if (!isStateFinal(state)) return -1;
    return 1;
}


void printStatistics() {
    printf("\n");
    printf("Communication Statistics:\n");
    printf("Frames Sent: %d\n", statistics.framesSent);
    printf("Frames Received: %d\n", statistics.framesReceived);
    printf("RR Received: %d\n", statistics.receivedRR);
    printf("REJ Received: %d\n", statistics.receivedREJ);
    printf("Duplicates Received: %d\n", statistics.receivedDuplicate);
    printf("Retransmissions: %d\n", statistics.retransmissions);
    printf("Timeouts: %d\n", statistics.timeouts);
    printf("\n");
    printf("Elapsed Time: %.2f s\n", statistics.elapsedTime);
}


int buildPacket(const unsigned char *buf, int bufSize, unsigned char *packet, int ns) {
    int index = 0;
    packet[index++] = FLAG;
    packet[index++] = A_TX;
    packet[index++] = (ns << 7);
    packet[index++] = packet[1] ^ packet[2];

    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++) {
        bcc2 ^= buf[i];

        if (buf[i] == FLAG || buf[i] == ESC) {
            packet[index++] = ESC;
            packet[index++] = ESC_BCC2 & buf[i];
        } else {
            packet[index++] = buf[i];
        }
    }

    if (bcc2 == FLAG || bcc2 == ESC) {
        packet[index++] = ESC;
        packet[index++] = ESC_BCC2 & bcc2;
    } else {
        packet[index++] = bcc2;
    }

    packet[index++] = FLAG;

    return index;
}

