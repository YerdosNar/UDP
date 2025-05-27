#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define LISTEN_PORT 1234
#define BUFFER_SIZE 1024

int main() {
    int sockfd;
    struct sockaddr_in receiver_addr, sender_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(sender_addr);

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0) {
        perror("Socket creation failed, line 18");
        exit(EXIT_FAILURE);
    }

    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(LISTEN_PORT);
    receiver_addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
        perror("Bind failed, line 28");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE-1, 0, (struct sockaddr *)&sender_addr, &addr_len);
    if(recv_len < 0) {
        perror("Recvfrom failed, line 35");
        close(EXIT_FAILURE);
    }

    buffer[recv_len] = '\0';
    printf("Recerved message: %s\n", buffer);

    // now for file
    recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE-1, 0, (struct sockaddr *)&sender_addr, &addr_len);
    if(recv_len < 0) {
        perror("recvfrom failed, line 45");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    buffer[recv_len] = '\0';
    printf("Received file name: %s\n", buffer);

    // reply
    char *ok_msg = "OK";
    ssize_t sent_bytes = sendto(sockfd, ok_msg, strlen(ok_msg), 0, (struct sockaddr *)&sender_addr, addr_len);
    if(sent_bytes < 0) {
        perror("sendto reply failed, line 56");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Sent OK response to sender.\n");

    // now let's receive file
    FILE *fp = fopen(buffer, "wb");
    if(fp == NULL) {
        perror("Failed to fopen, line 66");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Receiving file content and saving as '%s'...\n", buffer);

    // until we see "finish"
    while(1) {
        ssize_t recv_len = recvfrom(sockfd, buffer, BUFFER_SIZE-1, 0, (struct sockaddr *)&sender_addr, &addr_len);
        if(recv_len < 0) {
            perror("recvfrom file chunk failed, line 77");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // did we see "finish"
        if(recv_len == 6 && strncmp(buffer, "Finish", 6) == 0) {
            printf("Received 'Finish' message.\n");
            break;
        }

        size_t written = fwrite(buffer, 1, recv_len, fp);
        if(written != recv_len) {
            perror("fwrite fialed, line 86");
            fclose(fp);
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    fclose(fp);
    printf("File received fully and saved.\n");

    char *done_msg = "WellDone";
    ssize_t sent = sendto(sockfd, done_msg, strlen(done_msg), 0, (struct sockaddr *)&sender_addr, addr_len);
    if(sent < 0) {
        perror("sendto 'WellDone' failed, line 104");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Sent 'WellDone' response to sender.\n");

    close(sockfd);
    return 0;
}
