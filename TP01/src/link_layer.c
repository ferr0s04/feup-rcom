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
        perror("ERROR: failed to open serial port\n");
        exit(-1);
    }

    clock_gettime(CLOCK_REALTIME, &start);

    switch (connectionParameters.role) {
        case LlTx:
            return llopenTransmitter(connectionParameters);
        case LlRx:
            return llopenReceiver();
        default:
            perror("ERROR: invalid role at llopen\n");
            return -1;
    }
}


////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    int isREJ = FALSE;
    unsigned char _buf[2 * MAX_PAYLOAD_SIZE];

    int bytes = 0;
    alarmCount = 0;
    alarmEnabled = FALSE;
    State state = START;

    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {
        if (alarmEnabled == FALSE || isREJ == TRUE) {
            if (isREJ == TRUE) {
                statistics.retransmissions++;
                alarmCount++;
            }

            (void)signal(SIGALRM, alarmHandler);

            _buf[0] = FLAG;
            _buf[1] = A_TX;
            _buf[2] = (ns << 7);
            _buf[3] = _buf[1] ^ _buf[2];

            int i, j = 0;
            unsigned char bcc2 = 0;
            for (i = 0; i < bufSize; i++) {
                bcc2 ^= buf[i];

                if (buf[i] == FLAG || buf[i] == ESC) {
                    _buf[4 + i + j] = ESC;
                    _buf[4 + i + j + 1] = ESC_BCC2 & buf[i];
                    j++;
                }
                else {
                    _buf[4 + i + j] = buf[i];
                }
            }

            if (bcc2 == FLAG || bcc2 == ESC) {
                _buf[4 + i + j] = ESC;
                _buf[4 + i + j + 1] = ESC_BCC2 & bcc2;
                j++;
            }
            else {
                _buf[4 + i + j] = bcc2;
            }
            _buf[5 + i + j] = FLAG;

            bytes = write(fd, _buf, 6 + i + j);

            if (bytes < 0) exit(-1);
            statistics.framesSent++;

            state = START;
            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        if (read(fd, _buf, 1) == 0) {
            statistics.timeouts++;
            continue;
        }

        state = nextState(state, *_buf, A_TX, (RR | (nr << 7)));
        if (isStateFinal(state)) {
            printf("RR received\n");
            break;
        }
        else if (isREJ) {
            printf("REJ received\n");
            read(fd, _buf, 1);
            read(fd, _buf, 1);
        }
    }

    if (isStateFinal(state)) {
        nr = ns;
        ns = (ns + 1) % 2;
        return bytes;
    }
    else {
        perror("ERROR: llwrite\n");
        return -1;
    }
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    int isREJ = FALSE;
    unsigned char buf[6];
    State state = START;

    while (state != BCC_OK)
    {
        if (read(fd, buf, 1) == 0) continue;

        if (state == A_RCV && (*buf == (0 << 7) || (*buf == (1 << 7)))) {
            ns = (*buf >> 7);
            nr = (ns + 1) % 2;
        }

        state = nextState(state, *buf, A_TX, ns << 7);
        if (state == START) return 0;
    }

    unsigned char bcc2 = 0;
    int i = 0, data = 0;
    while (!isStateFinal(state))
    {
        if (state == START || isREJ)
            break;

        if (read(fd, buf, 1) == 0)
            continue;

        if (i == 0)
            data = (*buf == 1);

        if (i == 1 && data) {
            printf("#%d \n", *buf);
            if (seq == *buf) {
                state = START;
                printf("Duplicated packet\n");
                break;
            }
            seq = *buf;
        }

        if (*buf == FLAG) {
            state = STOP;
            break;
        }

        if (*buf == ESC) {
            while (*buf == ESC)
                read(fd, buf, 1);

            if (*buf == ESC_FLAG) *buf = FLAG;
            else if (*buf == ESC_ESC) *buf = ESC;
        }

        *(packet + i) = *buf;
        i++;
        bcc2 ^= *buf;
    }

    if (bcc2 != 0) {
        printf("Wrong BCC2\n");
        isREJ = TRUE;
        seq--;
    }

    *(packet + i) = '\0';
    i--;

    buf[0] = FLAG;
    buf[1] = A_TX;
    buf[2] = (isREJ == FALSE ? RR : REJ) | (nr << 7);
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;

    write(fd, buf, 6);
    printf("%s sent\n", isREJ == FALSE ? "RR" : "REJ");

    if (isREJ == TRUE) return 0;
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
        perror("ERROR: invalid role at llclose\n");
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
    State state = START;
    int bytes;

    while (!isStateFinal(state) && linkLayer.nRetransmissions > alarmCount) {
        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);
            alarm(linkLayer.timeout);

            if (writeBytesSerialPort(buf, 5) == -1) {
            perror("ERROR: failed to write SET\n");
            exit(-1);
            }

            state = START;
            alarmEnabled = TRUE;
        }

        if ((bytes = read(fd, buf, 1)) <= 0) {
            if (bytes < 0) exit(-1);
            continue;
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
    State state = START;

    while (1) {
        int bytes;
        if ((bytes = read(fd, buf, 1)) == 0) continue;

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

    write(fd, buf, 5);
    printf("UA sent\n");

    return 1;
}


int llcloseTransmitter() {
    alarmEnabled = FALSE;
    alarmCount = 0;

    unsigned char buf[BUFFER_SIZE + 1] = {FLAG, A_TX, DISC, A_TX ^ DISC, FLAG}, _buf[BUFFER_SIZE + 1];

    State state = START;
    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {
        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);

            write(fd, buf, 6);
            printf("DISC sent\n");

            state = START;

            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        int bytes = read(fd, _buf, 1);
        if (bytes < 0) exit(-1);
        else if (bytes == 0) continue;

        state = nextState(state, *_buf, A_RX, DISC);
        if (isStateFinal(state)) {
            printf("DISC received\n");
        }
    }

    if (!isStateFinal(state)) {
        perror("ERROR: llclose\n");
        return -1;
    }

    buf[0] = FLAG;
    buf[1] = A_RX;
    buf[2] = UA;
    buf[3] = buf[1] ^ buf[2];
    buf[4] = FLAG;

    if (!write(fd, buf, 6)) {
        perror("ERROR: UA\n");
        exit(-1);
    }

    printf("UA sent\n");

    return 1;
}


int llcloseReceiver() {
    unsigned char buf[BUFFER_SIZE + 1] = {0};
    State state = START;
    while (1) {
        if (read(fd, buf, 1) == 0) continue;

        state = nextState(state, *buf, A_TX, DISC);
        if (isStateFinal(state)) {
            printf("DISC received\n");
            break;
        }
    }

    unsigned char _buf[BUFFER_SIZE + 1] = {FLAG, A_RX, DISC, A_RX ^ DISC, FLAG, '\0'};
    alarmEnabled = FALSE;
    alarmCount = 0;
    state = START;

    while (alarmCount < linkLayer.nRetransmissions && !isStateFinal(state)) {

        if (alarmEnabled == FALSE) {
            (void)signal(SIGALRM, alarmHandler);

            write(fd, _buf, 6);
            printf("DISC sent\n");

            state = START;

            alarm(linkLayer.timeout);
            alarmEnabled = TRUE;
        }

        int bytes = read(fd, _buf, 1);
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
    printf("Retransmissions: %d\n", statistics.retransmissions);
    printf("Timeouts: %d\n", statistics.timeouts);
    printf("\n");
    printf("Elapsed Time: %.2f s\n", statistics.elapsedTime);
}
