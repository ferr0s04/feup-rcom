#ifndef _FTP_H_
#define _FTP_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024
#define TIMEOUT 14

// Function to send a command and read the server response
void ftp_command(int sockfd, const char *cmd, char *response, int code1, int code2);

// Function to parse the URL
void parse_ftp_url(const char *url, char *user, char *pass, char *host, char *path, int *port);

// Function to parse PASV response to get IP address and port
int parse_pasv_response(const char *response, char *ip, int *port);

// Function to read an expected response from the server
void ftp_read_response(int sockfd, char *response, int expected_code);

// Function to show the download progress bar
void show_progress(long long bytes_downloaded, long long total_bytes);

// Function to handle connection to server
int establish_connection(const char *hostname, int port);

// Main loop
int main(int argc, char *argv[]);

#endif // _FTP_H_