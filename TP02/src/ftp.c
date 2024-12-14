#include "ftp.h"


void ftp_command(int sockfd, const char *cmd, char *response, int code1, int code2) {
    if (send(sockfd, cmd, strlen(cmd), 0) < 0) {
        perror("Failed to send command");
        exit(EXIT_FAILURE);
    }

    ssize_t total = 0, bytes;
    int code = 0;
    int matched_code = 0;

    do {
        total = 0;
        while ((bytes = recv(sockfd, response + total, BUFFER_SIZE - total - 1, 0)) > 0) {
            total += bytes;
            if (response[total - 1] == '\n') break;
        }
        if (bytes < 0) {
            perror("Failed to read response");
            exit(EXIT_FAILURE);
        }
        response[total] = '\0';

        printf("%s\n", response);

        sscanf(response, "%d", &code);

        if (code == code1 || code == code2) {
            matched_code = 1;
        }
    } while (!matched_code);

    if (!matched_code) {
        fprintf(stderr, "Error: Unexpected response code %d. Expected %d or %d.\n", code, code1, code2);
        exit(EXIT_FAILURE);
    }
}


void parse_ftp_url(const char *url, char *user, char *pass, char *host, char *path, int *port) {
    char temp[BUFFER_SIZE];
    strncpy(temp, url + 6, BUFFER_SIZE - 1); // Skip "ftp://"
    temp[BUFFER_SIZE - 1] = '\0';

    *port = 21;

    char *at = strchr(temp, '@');
    char *host_part = temp;

    if (at) {
        *at = '\0';
        sscanf(temp, "%[^:]:%s", user, pass);
        host_part = at + 1;
    } else {
        strcpy(user, "anonymous");
        strcpy(pass, "anonymous");
    }

    char *slash = strchr(host_part, '/');
    char *colon = strchr(host_part, ':');

    if (slash) {
        *slash = '\0';
        strcpy(path, slash + 1);
    } else {
        strcpy(path, "");
    }

    if (colon && colon < slash) {
        *colon = '\0';
        strcpy(host, host_part);
        *port = atoi(colon + 1);
    } else {
        strcpy(host, host_part);
    }
}


int parse_pasv_response(const char *response, char *ip, int *port) {
    int ip1, ip2, ip3, ip4, p1, p2;
    if (sscanf(response, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
               &ip1, &ip2, &ip3, &ip4, &p1, &p2) != 6) {
        fprintf(stderr, "Error: Invalid PASV response: %s\n", response);
        return -1;
    }
    snprintf(ip, BUFFER_SIZE, "%d.%d.%d.%d", ip1, ip2, ip3, ip4);
    *port = (p1 << 8) | p2;
    return 0;
}


void ftp_read_response(int sockfd, char *response, int expected_code) {
    ssize_t total = 0, bytes;
    int code = 0;

    while (1) {
        total = 0;
        while ((bytes = recv(sockfd, response + total, BUFFER_SIZE - total - 1, 0)) > 0) {
            total += bytes;
            if (response[total - 1] == '\n') break;
        }
        if (bytes < 0) {
            perror("Failed to read response");
            exit(EXIT_FAILURE);
        }
        response[total] = '\0';
        printf("%s\n", response);

        sscanf(response, "%d", &code);

        if (code == expected_code) {
            break;
        } else if (code >= 400) {
            fprintf(stderr, "Error: Server returned error code %d: %s\n", code, response);
            exit(EXIT_FAILURE);
        }
    }
}


void show_progress(long long bytes_downloaded, long long total_bytes) {
    int progress = (int)((bytes_downloaded * 100) / total_bytes);
    int bar_width = 50;

    printf("\r\033[0;32m[");
    int pos = (progress * bar_width) / 100;
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) {
            printf("#");
        } else {
            printf(" ");
        }
    }
    printf("] %d%% \033[0m(%lld/%lld bytes)", progress, bytes_downloaded, total_bytes);
    fflush(stdout);
}


int establish_connection(const char *hostname, int port) {
    int sockfd;
    struct sockaddr_in server_addr;
    struct hostent *server;
    struct timeval timeout;
    fd_set set;

    // Resolve address
    server = gethostbyname(hostname);
    if (!server) {
        fprintf(stderr, "Error: No such host: %s\n", hostname);
        exit(EXIT_FAILURE);
    }

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(port);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        exit(EXIT_FAILURE);
    }

    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect()");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        FD_ZERO(&set);
        FD_SET(sockfd, &set);

        int ret = select(sockfd + 1, NULL, &set, NULL, &timeout);
        if (ret == 0) {
            fprintf(stderr, "Error: Connection timed out\n");
            close(sockfd);
            exit(EXIT_FAILURE);
        } else if (ret < 0) {
            perror("select()");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    fcntl(sockfd, F_SETFL, 0);

    return sockfd;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s ftp://<user>:<password>@<host>/<url-path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char user[BUFFER_SIZE] = {0}, pass[BUFFER_SIZE] = {0}, host[BUFFER_SIZE] = {0}, path[BUFFER_SIZE] = {0};
    int port;
    char response[BUFFER_SIZE] = {0};
    parse_ftp_url(argv[1], user, pass, host, path, &port);

    printf("User: %s\n", user);
    printf("Pass: %s\n", pass);
    printf("Host: %s\n", host);
    printf("Port: %d\n", port);
    printf("Path: %s\n", path);

    printf("Connecting to %s...\n", host);

    // Establish connection
    int control_sock = establish_connection(host, port);
    if (control_sock < 0) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    printf("Connected to %s\n", host);

    ftp_read_response(control_sock, response, 220);

    // Login
    char cmd[BUFFER_SIZE] = {0};
    snprintf(cmd, BUFFER_SIZE, "USER %s\r\n", user);
    send(control_sock, cmd, strlen(cmd), 0);
    ftp_read_response(control_sock, response, 331);

    snprintf(cmd, BUFFER_SIZE, "PASS %s\r\n", pass);
    send(control_sock, cmd, strlen(cmd), 0);
    ftp_read_response(control_sock, response, 230);

    snprintf(cmd, BUFFER_SIZE, "PASV\r\n");
    send(control_sock, cmd, strlen(cmd), 0);
    ftp_read_response(control_sock, response, 227);

    char data_ip[BUFFER_SIZE] = {0};
    int data_port = 0;
    if (parse_pasv_response(response, data_ip, &data_port) < 0) {
        close(control_sock);
        return EXIT_FAILURE;
    }

    int data_sock = establish_connection(data_ip, data_port);
    if (data_sock < 0) {
        perror("Failed to create data socket");
        exit(EXIT_FAILURE);
    }

    snprintf(cmd, BUFFER_SIZE, "RETR %s\r\n", path);
    ftp_command(control_sock, cmd, response, 150, 125);

    char mode[10];
    long long total_bytes = 0;
    int ret;

    ret = sscanf(response, "150 Opening %s mode data connection for %*s (%lld bytes)", mode, &total_bytes);
    if (ret == 2) {
    } else {
        ret = sscanf(response, "150 Opening %s data connection.", mode);
    }

    if (total_bytes <= 0 && ret == 2) {
        perror("Failed to get file size");
        exit(EXIT_FAILURE);
    } else if (total_bytes <= 0 && ret != 2) { // If the server doesn't send the file size
        total_bytes = 1000000000;
    }

    char *filename = strrchr(path, '/');
    if (filename) {
        filename++; 
    } else {
        filename = path;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        exit(EXIT_FAILURE);
    }

    char file_buffer[BUFFER_SIZE] = {0};
    long long bytes_downloaded = 0;
    int bytes;
    while ((bytes = recv(data_sock, file_buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(file_buffer, 1, bytes, file);
        bytes_downloaded += bytes;
        if (ret == 2) {
            show_progress(bytes_downloaded, total_bytes);
        }
    }

    fclose(file);
    close(data_sock);
    close(control_sock);

    printf("\nFile downloaded successfully\n");
    return 0;
}
