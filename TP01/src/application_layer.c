#include "application_layer.h"
#include "link_layer.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void handleTransmitter(FILE *inputFile, const char *filename, int fd);
void handleReceiver(FILE *outputFile, int fd);
int buildControl(unsigned char *control, int start, int file_size, const char *filename);
int buildData(unsigned char *data, unsigned char *buffer, int size, int n);
int getSize(FILE* file);
int nBytes(int n);


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {
    LinkLayer linkLayer;
    LinkLayerRole r = strcmp(role, "tx") == 0 ? LlTx : LlRx;

    strcpy(linkLayer.serialPort, serialPort);
    linkLayer.role = r;
    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;

    int fd = llopen(linkLayer);

    if (fd < 0) {
        printf("ERROR: failed to open connection\n");
        exit(-1);
    }

    FILE *inputFile = NULL;
    FILE *outputFile = NULL;

    if (r == LlTx) {
        inputFile = fopen(filename, "rb");
        if (inputFile == NULL) {
            printf("ERROR: can't open input file\n");
            llclose(fd);
            exit(-1);
        }
        handleTransmitter(inputFile, filename, fd);
    } else if (r == LlRx) {
        outputFile = fopen("penguin-received.gif", "wb");
        if (outputFile == NULL) {
            printf("ERROR: can't create output file\n");
            llclose(fd);
            exit(-1);
        }
        handleReceiver(outputFile, fd);
    } else {
        printf("ERROR: invalid role at applicationLayer\n");
        exit(-1);
    }

    if (llclose(fd) == -1) {
        printf("ERROR: failed to close connection\n");
        exit(-1);
    }

    printf("CONNECTION CLOSED\n");
    if (inputFile) fclose(inputFile);
    if (outputFile) fclose(outputFile);
    exit(0);
}


void handleTransmitter(FILE *inputFile, const char *filename, int fd) {
    unsigned char buffer[BUFFER_SIZE + 1], control[BUFFER_SIZE + 1];
    int size = getSize(inputFile);

    buildControl(control, 2, size, filename);

    if (llwrite(control, 5 + nBytes(size) + strlen(filename)) == -1) {
        printf("ERROR: failed to send control packet\n");
        llclose(fd);
        fclose(inputFile);
        exit(-1);
    }

    int n = 0, sz, bytes;
    while ((sz = fread(buffer, 1, BUFFER_SIZE - 4, inputFile)) > 0) {
        printf("Packet #%d\n", n);
        unsigned char data[BUFFER_SIZE];

        buildData(data, buffer, sz, n);

        while (1) {
            if ((bytes = llwrite(data, sz + 4)) == -1) {
                printf("ERROR: failed to send packet\n");
                llclose(fd);
                fclose(inputFile);
                exit(-1);
            }

            if (bytes > 0)
                break;
        }

        n++;
    }

    buildControl(control, 3, size, filename);

    if (llwrite(control, 5 + nBytes(sz) + strlen(filename)) == -1) {
        printf("ERROR: failed to send control packet\n");
        llclose(fd);
        fclose(inputFile);
        exit(-1);
    }
}


void handleReceiver(FILE *outputFile, int fd) {
    unsigned char control[BUFFER_SIZE], data[BUFFER_SIZE];

    if (llread(control) == -1) {
        printf("ERROR: failed to receive control packet\n");
        llclose(fd);
        fclose(outputFile);
        exit(-1);
    }

    int bytes;
    while (1) {
        if ((bytes = llread(data)) == -1) {
            printf("ERROR: failed to receive data packet\n");
            llclose(fd);
            fclose(outputFile);
            exit(-1);
        }
        if (data[0] == 3)
            break;

        if (bytes > 0)
            fwrite(data + 4, 1, bytes - 4, outputFile);
    }
}


int getSize(FILE* file) {
    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    fseek(file, 0, SEEK_SET);
    return size;
}


int buildControl(unsigned char *control, int start, int file_size, const char *filename) {
    control[0] = start;
    control[1] = 0;
    int l1 = nBytes(file_size); 
    control[2] = l1;

    for (int i = 0; i < l1; i++) {
        control[3 + i] = (file_size >> (8 * (l1 - 1 - i))) & 0xFF;
    }

    control[3 + l1] = 1;
    control[4 + l1] = strlen(filename);
    strncpy((char *)(control + 5 + l1), filename, strlen(filename));

    return 5 + l1 + strlen(filename);
}


int nBytes(int n) {
    int bytes = 0;
    while (n > 0) {
        n /= 256;
        bytes++;
    }
    return bytes;
}


int buildData(unsigned char *data, unsigned char *buffer, int size, int n) {
    data[0] = 1;
    data[1] = n;
    data[2] = size / 256;
    data[3] = size % 256;
    memcpy(data + 4, buffer, size);

    return 1;
}
