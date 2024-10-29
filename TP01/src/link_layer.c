#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include "link_layer.h"
#include "serial_port.h"
#include "macros.h"
#include "state.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

int frameNumber = 0;
int framesReceived = 0;
unsigned char lastAddress = 0;
unsigned char lastControl = 0;
State state = START;
int alarmEnabled = FALSE;
int alarmCount = 0;
unsigned char byte;
unsigned char buf_tx = 0;

LinkLayer linkLayer;
void alarmHandler(int signal);
int llopenReceiver();
int llopenTransmitter(LinkLayer connectionParameters);

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Alarm #%d\n", alarmCount);
    printf("Enabled = %d\n", alarmEnabled);
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) < 0) {
        perror("Failed to open serial port\n");
        exit(-1);
    }

    switch (connectionParameters.role) {
        case LlTx:
            return llopenTransmitter(connectionParameters);
        case LlRx:
            return llopenReceiver();
        default:
            perror("Invalid role\n");
            return -1;
    }
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {
    // Frame with start/end flags, address, control fields, and BCC
    int maxFrameSize = 6 + 2 * bufSize;  // Worst case (stuffing doubles size)
    unsigned char *frame = (unsigned char *)malloc(maxFrameSize);

    frame[0] = FLAG;  // Start flag
    frame[1] = A_TX;  // Address (example address)
    frame[2] = C_N(buf_tx);  // Control field (example control)
    frame[3] = frame[1] ^ frame[2];  // BCC1

    // Calculate BCC2 over the data payload
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // Copy data and apply byte stuffing
    int stuffedIndex = 4;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG || buf[i] == ESC) {
            frame[stuffedIndex++] = ESC;
            frame[stuffedIndex++] = buf[i] ^ 0x20;  // XOR with 0x20 after ESC
        } else {
            frame[stuffedIndex++] = buf[i];
        }
    }

    // Add BCC2 with stuffing
    if (BCC2 == FLAG || BCC2 == ESC) {
        frame[stuffedIndex++] = ESC;
        frame[stuffedIndex++] = BCC2 ^ 0x20;
    } else {
        frame[stuffedIndex++] = BCC2;
    }

    frame[stuffedIndex++] = FLAG;  // End flag

    // Write the frame to the serial port
    if (writeBytesSerialPort(frame, stuffedIndex) < 0) {
        perror("Failed to write frame");
        free(frame);
        return -1;
    }

    free(frame);
    return bufSize;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {
    unsigned char byte;
    unsigned char destuffedPacket[1024];
    int destuffedIndex = 0;
    int inEscapeMode = 0;
    int isPacketStarted = 0; // To track if we have started receiving a valid packet

    while (1) {
        if (readByteSerialPort(&byte) < 0) {
            perror("Failed to read byte from serial port");
            exit(-1);
        }

        // Check for the start flag
        if (byte == FLAG) {
            // If we have previously started receiving a packet, it means we have a complete packet
            if (isPacketStarted) {
                // Validate and process the complete frame
                if (destuffedIndex > 0) {
                    // Validate BCC2
                    unsigned char bcc2 = destuffedPacket[destuffedIndex - 1]; // Last byte is BCC2
                    destuffedIndex--;  // Remove BCC2 from the packet

                    // Calculate BCC2 over the destuffed data
                    unsigned char acc = destuffedPacket[0];
                    for (int j = 1; j < destuffedIndex; j++) {
                        acc ^= destuffedPacket[j];
                    }

                    // Validate BCC2
                    if (bcc2 == acc) {
                        memcpy(packet, destuffedPacket, destuffedIndex);
                        return destuffedIndex;  // Return size of the valid data
                    }
                }
                // Reset destuffed index for the next packet
                destuffedIndex = 0;
            }
            // Mark the start of a new packet
            isPacketStarted = 1;
        } else if (byte == ESC) {
            // Next byte is escaped, flag escape mode
            inEscapeMode = 1;
        } else {
            if (inEscapeMode) {
                byte ^= 0x20;  // Unstuff the byte
                inEscapeMode = 0;
            }
            // Add byte to destuffed packet if we have started a valid packet
            if (isPacketStarted) {
                if (destuffedIndex < sizeof(destuffedPacket)) {
                    destuffedPacket[destuffedIndex++] = byte;
                } else {
                    perror("Buffer overflow detected");
                    exit(-1);
                }
            }
        }
    }

    return -1;  // In case of error
}


////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    int clstat = closeSerialPort();
    return clstat;
}

int llopenReceiver()
{
    unsigned char byte;

    while (state != STOP) {
        if (readByteSerialPort(&byte) > 0) {
            printf("Received byte: 0x%02X\n", byte);
            switch (state) {
                case START:
                    if (byte == FLAG)
                        state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TX)
                        state = A_RCV;
                    else if (byte != FLAG)
                        state = START;
                    break;
                case A_RCV:
                    if (byte == SET)
                        state = C_RCV;
                    else if (byte == FLAG)
                        state = FLAG_RCV;
                    else
                        state = START;
                    break;
                case C_RCV:
                    if (byte == (A_TX ^ SET))
                        state = BCC_OK;
                    else if (byte == FLAG)
                        state = FLAG_RCV;
                    else
                        state = START;
                    break;
                case BCC_OK:
                    if (byte == FLAG)
                        state = STOP;
                    else
                        state = START;
                    break;
                default:
                    break;
            }
        }
    }

    unsigned char buf[5] = {FLAG, A_RX, UA, A_RX ^ UA, FLAG};
    if (writeBytesSerialPort(buf, 5) == -1) {
        perror("Failed to write UA\n");
        return -1;
    }
    return 1;
}


int llopenTransmitter(LinkLayer connectionParameters)
{
    (void)signal(SIGALRM, alarmHandler);
    unsigned char byte;
    unsigned char buf[5] = {FLAG, A_TX, SET, A_TX ^ SET, FLAG};

    while (state != STOP && connectionParameters.nRetransmissions > 0) {
        alarm(connectionParameters.timeout);
        alarmEnabled = FALSE;

        if (writeBytesSerialPort(buf, 5) == -1) {
            perror("Failed to write SET\n");
            exit(-1);
        }

        while (state != STOP && alarmEnabled == FALSE) {
            if (readByteSerialPort(&byte) > 0) {
                printf("Received byte: 0x%02X\n", byte);
                updateStateTransmitter(&state, byte);
            }
        }

        connectionParameters.nRetransmissions -= 1;
    }

    if (state != STOP) {
        perror("llopen error\n");
        return -1;
    }
    return 1;
}
