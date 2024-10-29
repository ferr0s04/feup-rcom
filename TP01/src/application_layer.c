
// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int handleReceiver(const char *filename);
int handleTransmitter(const char *filename);

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // Set up link layer parameters
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.baudRate = baudRate;
    connectionParameters.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Open connection
    if (llopen(connectionParameters) < 0) {
        printf("Failed to establish connection.\n");
        return;
    }

    if (connectionParameters.role == LlRx) // Receiver
    {
        if (handleReceiver(filename) < 0)
        {
            printf("Error during data transfer.\n");
        }
    }
    else if (connectionParameters.role == LlTx) // Transmitter
    {
        if (handleTransmitter(filename) < 0)
        {
            printf("Error during data transfer.\n");
        }
    }

    // Close connection
    if (llclose(1) < 0) {
        printf("Failed to close connection.\n");
    }
}

int handleReceiver(const char *filename) {
    unsigned char packet[MAX_PAYLOAD_SIZE];
    int bytesRead = llread(packet);

    if (bytesRead < 0) {
        printf("Failed to receive the file.\n");
    } else {
        FILE *file = fopen(filename, "wb");
        if (!file) {
            perror("Failed to open file for writing");
            return -1;
        }
        fwrite(packet, 1, bytesRead, file);
        fclose(file);
        printf("File received successfully: %d bytes written to %s.\n", bytesRead, filename);
    }
    return 1;
}

int handleTransmitter(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file for reading");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    unsigned char *buffer = (unsigned char *)malloc(fileSize);
    if (!buffer) {
        perror("Failed to allocate memory");
        fclose(file);
        return -1;
    }

    fread(buffer, 1, fileSize, file);
    fclose(file);

    int bytesWritten = llwrite(buffer, fileSize);
    if (bytesWritten < 0) {
        printf("Failed to send the file.\n");
    } else {
        printf("File sent successfully: %d bytes written.\n", bytesWritten);
    }

    free(buffer);
    return 1;
}
