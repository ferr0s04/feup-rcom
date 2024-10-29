#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 256

FILE *inputFile;
FILE *outputFile;
char buffer[BUFFER_SIZE];
int handleReceiver();
int handleTransmitter();

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer linkLayer;

    strcpy(linkLayer.serialPort, serialPort);

    if (strcmp(role, "tx") == 0) {
        linkLayer.role = LlTx;
        inputFile = fopen(filename, "rb"); // Open file for reading in binary mode
        if (inputFile == NULL) {
            perror("Failed to open input file");
            exit(-1);
        }
    }
    else if (strcmp(role, "rx") == 0) {
        linkLayer.role = LlRx;
        outputFile = fopen(filename, "wb"); // Open file for writing in binary mode
        if (outputFile == NULL) {
            perror("Failed to create output file");
            exit(-1);
        }
    }
    else {
        perror("Invalid role\n");
        exit(-1);
    }

    linkLayer.baudRate = baudRate;
    linkLayer.nRetransmissions = nTries;
    linkLayer.timeout = timeout;    

    int fd = llopen(linkLayer);

    if (fd < 0) {
        perror("Failed to open connection\n");
        exit(-1);
    }

    printf("Serial Port connection successful\n");

    if (linkLayer.role == LlTx) {
        if (handleTransmitter() < 0) return;
    }
    else if (linkLayer.role == LlRx) {
        if (handleReceiver() < 0) return;
    }

    if (llclose(fd) == -1) {
        perror("Failed to close connection\n");
        exit(-1);
    }

    printf("Connection closed\n");
}

int handleReceiver() {
    if (outputFile == NULL) {
        perror("Error creating output file");
        exit(-1);
    }

    int bytesReceived;
    int totalBytesReceived = 0;

    printf("Receiving data...\n");

    // Read data from link layer and write to file
    while ((bytesReceived = llread((unsigned char *)buffer)) > 0) {
        if (bytesReceived > 3) {
            int bytesToWrite = bytesReceived - 3; // Adjust the number of bytes to write
            int bytesWritten = fwrite(buffer + 3, 1, bytesToWrite, outputFile);
            if (bytesWritten != bytesToWrite) {
                perror("Error writing to file");
                break;
            }

            totalBytesReceived += bytesWritten;
            printf("Received %d bytes, Total received: %d bytes\n", bytesWritten, totalBytesReceived);
        } else {
            printf("Received %d bytes\n", bytesReceived);
        }

    }

    if (bytesReceived < 0) {
        perror("Error receiving packet");
        exit(-1);
    } else {
        printf("File received successfully. Total bytes received: %d\n", totalBytesReceived);
    }

    fclose(outputFile);
    return 1;
}

int handleTransmitter() {
    if (inputFile == NULL) {
        perror("File not found\n");
        exit(-1);
    }

    size_t bytesRead;
    int totalBytesSent = 0;

    printf("Starting transmission...\n");

    while (1) {
        // Read data from input file
        bytesRead = fread(buffer, 1, BUFFER_SIZE, inputFile);
        if (bytesRead == 0) {
            if (feof(inputFile)) break;  // EOF reached
            if (ferror(inputFile)) {
                perror("Error reading input file");
                break;
            }
        }

        // Transmit data via llwrite, ensuring all bytes are sent
        int totalSent = 0;
        while (totalSent < bytesRead) {
            int res = llwrite((unsigned char *)buffer + totalSent, bytesRead - totalSent);
            if (res < 0) {
                perror("Error transmitting packet\n");
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

