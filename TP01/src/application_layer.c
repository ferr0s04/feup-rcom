#include "application_layer.h"
#include "link_layer.h"
#include "macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *inputFile;
FILE *outputFile;
char buffer[BUFFER_SIZE];
int handleTransmitter();
int handleReceiver();


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;

    strcpy(linkLayer.serialPort, serialPort);

    if (strcmp(role, "tx") == 0) {
        linkLayer.role = LlTx;
        inputFile = fopen(filename, "rb");
        if (inputFile == NULL) {
            perror("ERROR: can't open input file");
            exit(-1);
        }
    }
    else if (strcmp(role, "rx") == 0) {
        linkLayer.role = LlRx;
        outputFile = fopen(filename, "wb");
        if (outputFile == NULL) {
            perror("ERROR: can't create output file");
            exit(-1);
        }
    }
    else {
        perror("ERROR: invalid role at ApplicationLayer\n");
        exit(-1);
    }

    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;    

    int fd = llopen(linkLayer);

    if (fd < 0) {
        perror("ERROR: failed to open connection\n");
        exit(-1);
    }

    printf("CONNECTION OPEN\n");

    if (linkLayer.role == LlTx) {
        if (handleTransmitter() < 0) return;
    }
    else if (linkLayer.role == LlRx) {
        if (handleReceiver() < 0) return;
    }

    if (llclose(fd) == -1) {
        perror("ERROR: failed to close connection\n");
        exit(-1);
    }

    printf("CONNECTION CLOSED\n");
}


int handleTransmitter() {
    if (inputFile == NULL) {
        perror("ERROR: file not found\n");
        exit(-1);
    }

    size_t bytesRead;
    int totalBytesSent = 0;

    printf("Starting transmission...\n");

    while (1) {
        bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile);
        if (bytesRead == 0) {
            if (feof(inputFile)) break;
            if (ferror(inputFile)) {
                perror("ERROR: can't read input file");
                break;
            }
        }

        int totalSent = 0;
        while (totalSent < bytesRead) {
            int res = llwrite((unsigned char *)buffer + totalSent, bytesRead - totalSent);
            if (res < 0) {
                perror("ERROR: failed transmitting packet\n");
                fclose(inputFile);
                return -1;
            }
            totalSent += res;
        }

        totalBytesSent += totalSent;
        printf("Sent %d bytes, Total sent: %d bytes\n", totalSent, totalBytesSent);
    }

    fclose(inputFile);
    printf("Transmission complete. Total bytes sent: %d\n", totalBytesSent);
    return 1;
}


int handleReceiver() {
    if (outputFile == NULL) {
        perror("ERROR: can't create output file");
        exit(-1);
    }

    int bytesReceived;
    int totalBytesReceived = 0;

    printf("Receiving data...\n");

    while ((bytesReceived = llread((unsigned char *)buffer)) > 0) {
        int bytesToWrite = bytesReceived;
        int bytesWritten = fwrite(buffer, 1, bytesToWrite, outputFile);
        if (bytesWritten != bytesToWrite) {
            perror("ERROR: can't write to file");
            break;
        }

        totalBytesReceived += bytesWritten;
        printf("Received %d bytes, Total received: %d bytes\n", bytesWritten, totalBytesReceived);
    }

    if (bytesReceived < 0) {
        perror("ERROR: failed receiving packet");
        exit(-1);
    } else {
        printf("File received successfully. Total bytes received: %d\n", totalBytesReceived);
    }

    fclose(outputFile);
    return 1;
}
