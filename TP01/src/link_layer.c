#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "link_layer.h"
#include "serial_port.h"
#include "macros.h"
#include "state.h"
#include "alarm.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

static int ns = 0;
int frameNumber = 0;
int framesReceived = 0;
unsigned char lastAddress = 0;
unsigned char lastControl = 0;

LinkLayer linkLayer;
int llopen_receiver();
int llopen_transmitter();
int llclose_receiver();
int llclose_transmitter();
int sendDataFrame(const unsigned char *data, int dataSize);
int sendSET();
int sendACK();
int sendRR();
int sendREJ();
int writeBytes(const unsigned char *bytes, int num_bytes);
int isDuplicate(unsigned char address, unsigned char control);
int isValidData(unsigned char bcc, const unsigned char *data, int length);

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    int fd = openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate);

    if (fd < 0) {
        perror("Error opening serial port");
        return -1;
    }

    linkLayer = connectionParameters;
    
    if (connectionParameters.role == LlTx) {
        if (llopen_transmitter() < 0) return -1;
    }
    else if (connectionParameters.role == LlRx) {
        if (llopen_receiver() < 0) return -1;
    }
    else if (connectionParameters.role != LlRx && connectionParameters.role != LlTx) return -1;

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    extern int alarmEnabled;
    extern int alarmCount;
    alarmEnabled = FALSE;
    alarmCount = 0;
    unsigned int attempts = 0;

    (void)signal(SIGALRM, alarmHandler);

    while (attempts < linkLayer.nRetransmissions) {
        attempts++;

        if (sendDataFrame(buf, bufSize) < 0) {
            alarm(0);
            continue;
        }

        alarm(linkLayer.timeout);
        alarmEnabled = TRUE;
        State state = START;

        while (alarmEnabled) {
            unsigned char byte = 0;
            if (readByteSerialPort(&byte) == 0) {
                continue;
            }
            else if (readByteSerialPort(&byte) < 0) {
                printf("Read error\n");
                return -1;
            }
            state = processState(state, byte, NULL, NULL, NULL);

            if (state == RR) {
                printf("Received RR for frame with NS = %d\n", ns);
                ns = (ns + 1) % 2;
                alarm(0);
                return bufSize;
            } else if (state == REJ) {
                printf("Received REJ for frame with NS = %d\n", ns);
                attempts--;
                alarm(0);
                break;
            }
        }
    }

    if (alarmCount >= linkLayer.nRetransmissions) {
        printf("Maximum retries reached. Write failed.\n");
    }
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char byte, address, control, bcc;
    State state = START;
    int bytesRead = 0;
    int received = 0;

    while (!isStateFinal(state)) {
        if (readByteSerialPort(&byte) == 0) {
            continue;
        }
        else if (readByteSerialPort(&byte) < 0) {
            printf("Read error\n");
            return -1;
        }

        processState(state, byte, &address, &control, &bcc);

        if (state == STOP) {
            if (control == SET) {
                if (sendRR(address) < 0) {
                    return -1;
                }
            } else if (isDuplicate(address, control)) {
                if (sendRR(address) < 0) {
                    return -1;
                }
            } else if (!isValidData(bcc, packet, bytesRead)) {
                if (sendREJ(address) < 0) {
                    return -1;
                }
            } else {
                packet[bytesRead++] = byte;
                received++;
            }
            state = START;
        }
    }

    return bytesRead;
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {
    if (linkLayer.role == LlTx) {
        if (llclose_transmitter() < 0) return -1;
    }
    else if (linkLayer.role == LlRx) {
        if (llclose_receiver() < 0) return -1;
    }
    else return -1;

    int clstat = closeSerialPort();

    if (showStatistics) {
        //statistics(statistics);
    }

    return clstat;
}

int llopen_receiver() {
    State state = START;
    unsigned char byte, address, control, bcc;
    
    while (!isStateFinal(state)) {
        if (readByteSerialPort(&byte) < 0) {
            printf("Read error\n");
            return -1;
        } else if (readByteSerialPort(&byte) == 0) {
            continue;
        }

        state = processState(state, byte, &address, &control, &bcc);
    }

    return sendACK();
}


int llopen_transmitter() {
    extern int alarmEnabled;
    extern int alarmCount;
    alarmCount = 0;
    alarmEnabled = FALSE;
    unsigned int attempts = 0;
    (void)signal(SIGALRM, alarmHandler);

    while (attempts < linkLayer.nRetransmissions) {
        attempts++;
        if (sendSET() < 0) {
            printf("Error sending SET frame");
            alarm(0);
            continue;
        }
        alarmEnabled = TRUE;
        State state = START;
        alarm(linkLayer.timeout);

        while (alarmEnabled) {
            unsigned char address, control, bcc;
            unsigned char byte = 0;
            if (readByteSerialPort(&byte) == 0) {
                continue;
            }

            if (readByteSerialPort(&byte) < 0) {
                printf("Read error\n");
                return -1;
            }
            state = processState(state, byte, &address, &control, &bcc);
            if (state == STOP) {
                alarm(0);
                alarmEnabled = FALSE;
                return 1;
            }
        }
    }
    printf("Connection failed: UA frame not received after %d retries.\n", linkLayer.nRetransmissions);
    return -1;
}


int llclose_receiver() {
    State state = START;
    unsigned char byte, address, control, bcc;

    while (!isStateFinal(state)) {
        if (readByteSerialPort(&byte) == 1) {
            state = processState(state, byte, &address, &control, &bcc);
        }
    }

    if (control != DISC) {
        printf("Expected DISC frame, but got something else\n");
        return -1;
    }

    unsigned char discFrame[5] = {FLAG, A_RX, DISC, 0, FLAG};
    discFrame[3] = discFrame[1] ^ discFrame[2];
    if (writeBytesSerialPort(discFrame, sizeof(discFrame)) < 0) {
        perror("Error sending DISC frame");
        return -1;
    }

    while (!isStateFinal(state)) {
        if (readByteSerialPort(&byte) == 1) {
            state = processState(state, byte, &address, &control, &bcc);
        }
    }

    if (control != UA) {
        printf("Expected UA frame, but got something else\n");
        return -1;
    }
    return 1;
}

int llclose_transmitter() {
    unsigned char discFrame[5] = {FLAG, A_TX, DISC, 0, FLAG};
    discFrame[3] = discFrame[1] ^ discFrame[2];

    if (writeBytesSerialPort(discFrame, sizeof(discFrame)) < 0) {
        perror("Error sending DISC frame");
        return -1;
    }

    State state = START;
    unsigned char byte, address, control, bcc;

    while (!isStateFinal(state)) {
        if (readByteSerialPort(&byte) == 1) {
            state = processState(state, byte, &address, &control, &bcc);
        }
    }

    if (control != DISC) {
        printf("Expected DISC frame, but got something else\n");
        return -1;
    }

    unsigned char uaFrame[5] = {FLAG, A_TX, UA, 0, FLAG};
    uaFrame[3] = uaFrame[1] ^ uaFrame[2];
    if (writeBytesSerialPort(uaFrame, sizeof(uaFrame)) < 0) {
        perror("Error sending UA frame");
        return -1;
    }
    return 1;
}

int sendDataFrame(const unsigned char *buf, int bufSize) {
    unsigned char frame[bufSize + 6];
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = ns ? 0x40 : 0x00;
    frame[3] = frame[1] ^ frame[2];

    for (int i = 0; i < bufSize; i++) {
        frame[i + 4] = buf[i];
    }

    unsigned char bcc2 = 0;
    for (int i = 0; i < bufSize; i++) {
        bcc2 ^= buf[i];
    }
    frame[bufSize + 4] = bcc2;
    frame[bufSize + 5] = FLAG;

    alarmCount = 0;
    while (alarmCount < linkLayer.nRetransmissions) {
        alarmEnabled = 1;

        if (writeBytesSerialPort(frame, bufSize + 6) < 0) {
            perror("Error sending data frame");
            return -1;
        }

        alarm(linkLayer.timeout);
        unsigned char byte;
        State state = START;

        while (alarmEnabled) {
            if (readByteSerialPort(&byte) == 1) {
                state = processState(state, byte, NULL, NULL, NULL);
                if (state == RR) {
                    printf("Received RR for frame with NS = %d\n", ns);
                    ns = (ns + 1) % 2;
                    alarm(0);
                    return bufSize;
                } else if (state == REJ) {
                    printf("Received REJ for frame with NS = %d\n", ns);
                    break;
                }
            }
        }

        if (alarmCount >= linkLayer.nRetransmissions) {
            printf("Maximum retries reached. Frame sending failed.\n");
            return -1;
        }
    }
    return -1;
}

int sendSET() {
    unsigned char buf[5] = {FLAG, A_TX, SET, 0x00, FLAG};
    buf[3] = buf[1] ^ buf[2];

    if (writeBytes(buf, sizeof(buf)) < 0) {
        printf("Failed to send SET frame.\n");
        return -1;
    }

    return 1;
}


int sendACK() {
    unsigned char buf[5] = {FLAG, 0x00, UA, 0x00, FLAG};
    if (linkLayer.role == LlRx) {
        buf[1] = A_TX;
    }
    else if (linkLayer.role == LlTx) {
        buf[1] = A_RX;
    }
    buf[3] = buf[1] ^ buf[2];

    if (writeBytes(buf, sizeof(buf)) < 0) {
        printf("Failed to send ACK frame.\n");
        return -1;
    }

    return 1;
}

int sendRR() {
    unsigned char buf[5] = {FLAG, 0x03, frameNumber == 0 ? 0xAA : 0xAB, 0, FLAG};
    buf[3] = buf[1] ^ buf[2];

    if (writeBytes(buf, sizeof(buf)) < 0) {
        printf("Failed to send RR frame (RR%d).\n", frameNumber);
        return -1;
    }

    return 1;
}

int sendREJ() {
    unsigned char buf[5] = {FLAG, 0x03, frameNumber == 0 ? 0x54 : 0x55, 0, FLAG};
    buf[3] = buf[1] ^ buf[2]; 

    if (writeBytes(buf, sizeof(buf)) < 0) {
        printf("Failed to send REJ frame (REJ%d).\n", frameNumber);
        return -1;
    }

    return 1;
}

int writeBytes(const unsigned char *bytes, int num_bytes) {
    int written = 0;
    while (written < num_bytes)
    {
        int toWrite = num_bytes - written;
        int bytesWritten = writeBytesSerialPort(bytes + written, toWrite);

        if (bytesWritten < 0)
        {
            printf("Writing error.\n");
            return -1;
        }

        written += bytesWritten;
    }

    return written;
}

int isDuplicate(unsigned char address, unsigned char control) {
    if (address == lastAddress && control == lastControl) {
        return 1;
    }
    lastAddress = address;
    lastControl = control;
    return 0;
}

int isValidData(unsigned char bcc, const unsigned char *data, int length) {
    unsigned char calculated_bcc = 0;
    for (int i = 0; i < length; i++) {
        calculated_bcc ^= data[i];
    }
    return calculated_bcc == bcc;
}
