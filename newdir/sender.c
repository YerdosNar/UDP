#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_PORT 1234
#define SERVER_IP "127.0.0.1"
#define BUFFER 1024

int main() {
    int sockfd;
    struct sockaddr_in receiver_addr;
    char *message = "Greeting";

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        perror("Socket creation failed\n");
        exit(EXIT_FAILURE);
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(SERVER_PORT);
    receiver_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    ssize_t sent_bytes = sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
    if(sent_bytes < 0) {
        perror("Sendto failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Message sent: %s\n", message);

    // now file
    char *filename = "test.txt";
    ssize_t sent_filename = sendto(sockfd, filename, strlen(filename), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
    if(sent_filename < 0) {
        perror("Sendto filename failed, line 39");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("File name sent: %s\n", filename);

    char buffer[BUFFER];
    socklen_t addr_len = sizeof(receiver_addr);
    ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER-1, 0, (struct sockaddr *)&receiver_addr, &addr_len);
    if(recv_len < 0) {
        perror("RECVFROM failed, line 50");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    buffer[recv_len] = '\0';
    printf("Received from receiver: %s\n", buffer);

    // let's open file and send chunks
    FILE *fp = fopen(filename, "rb");
    if(fp == NULL) {
        perror("Failed to open file, line 61");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char file_buffer[BUFFER];
    size_t bytes_read;
    printf("Sending file contents ... \n");

    // chunks now
    while((bytes_read = fread(file_buffer, 1, BUFFER, fp)) > 0) {
        ssize_t sent = sendto(sockfd, file_buffer, bytes_read, 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
        if(sent < 0) {
            perror("sendto file chunks failed, line 74");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    fclose(fp);
    printf("File sent fully.\n");

    char *finish_msg = "Finish";
    ssize_t finish_sent = sendto(sockfd, finish_msg, strlen(finish_msg), 0, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr));
    if(finish_sent < 0) {
        perror("sendto Finish failed, line 87");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Sent Finish message.\n");

    close(sockfd);
    return 0;
}

