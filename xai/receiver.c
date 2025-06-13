#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <time.h>
#include "packet.h"

FILE *log_fp;

// Print progress bar for file transfer
void print_progress_bar(int received_bytes, int total_bytes) {
    const int bar_width = 50;
    float percentage = (float)received_bytes / total_bytes;
    int pos = (int)(bar_width * percentage);

    printf("\r[");
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) printf("#");
        else printf("-");
    }
    printf("] %3d%%", (int)(percentage * 100));
    fflush(stdout);
}

// Log events to file
void log_event(const char *event, Packet *pkt) {
    time_t now = time(NULL);
    fprintf(log_fp, "[%ld] %s - type: %d, seqNum: %d, ackNum: %d, len: %d\n",
            now, event, pkt->type, pkt->seqNum, pkt->ackNum, pkt->length);
    fflush(log_fp);
}

int drop(float prob) {
    return ((float)rand() / RAND_MAX) < prob;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <receiver_port> <drop_prob>\n", argv[0]);
        exit(1);
    }

    int receiver_port = atoi(argv[1]);
    float drop_prob = atof(argv[2]);
    int total_received = 0;
    int f_size = 0; // Initialize to avoid undefined behavior
    srand(time(NULL));

    // Open log file
    log_fp = fopen("receiver_udp_logs", "a");
    if (!log_fp) {
        perror("Failed to open log file");
        exit(1);
    }

    // Create UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        fclose(log_fp);
        exit(1);
    }

    // Set up receiver address
    struct sockaddr_in receiver_addr;
    memset(&receiver_addr, 0, sizeof(receiver_addr));
    receiver_addr.sin_family = AF_INET;
    receiver_addr.sin_port = htons(receiver_port);
    receiver_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&receiver_addr, sizeof(receiver_addr)) < 0) {
        perror("Bind failed");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }

    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);

    // Receive and validate greeting packet
    Packet greet_pkt;
    memset(&greet_pkt, 0, sizeof(greet_pkt));
    if (recvfrom(sockfd, &greet_pkt, sizeof(greet_pkt), 0, (struct sockaddr *)&sender_addr, &addr_len) < 0) {
        perror("Failed to receive greeting");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }

    if (greet_pkt.type != TYPE_DATA || strcmp(greet_pkt.data, "Greeting") != 0 || greet_pkt.length != strlen("Greeting")) {
        fprintf(stderr, "Invalid greeting packet\n");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("RECV GREETING", &greet_pkt);

    // Send OK acknowledgment
    Packet ok;
    memset(&ok, 0, sizeof(ok));
    ok.type = TYPE_ACK;
    ok.length = strlen("OK");
    strcpy(ok.data, "OK");
    if (sendto(sockfd, &ok, sizeof(ok), 0, (struct sockaddr *)&sender_addr, addr_len) < 0) {
        perror("Failed to send OK");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("SEND OK", &ok);

    // Receive filename
    Packet fname_pkt;
    memset(&fname_pkt, 0, sizeof(fname_pkt));
    if (recvfrom(sockfd, &fname_pkt, sizeof(fname_pkt), 0, (struct sockaddr *)&sender_addr, &addr_len) < 0) {
        perror("Failed to receive filename");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    log_event("RECV FILENAME", &fname_pkt);

    // Receive file size
    Packet size_pkt;
    memset(&size_pkt, 0, sizeof(size_pkt));
    if (recvfrom(sockfd, &size_pkt, sizeof(size_pkt), 0, (struct sockaddr *)&sender_addr, &addr_len) < 0) {
        perror("Failed to receive file size");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    if (size_pkt.type != TYPE_DATA || size_pkt.length != sizeof(int)) {
        fprintf(stderr, "Invalid file size packet\n");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }
    memcpy(&f_size, size_pkt.data, sizeof(int));
    log_event("RECV FILE SIZE", &size_pkt);

    // Open output file
    char filename[128];
    snprintf(filename, sizeof(filename), "recv_%s", fname_pkt.data);
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("Failed to open output file");
        fclose(log_fp);
        close(sockfd);
        exit(1);
    }

    // Main data transfer loop
    int expected_seq = 3;
    while (1) {
        Packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        ssize_t len = recvfrom(sockfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&sender_addr, &addr_len);

        if (len < 0) {
            perror("Receive error");
            continue;
        }

        if (pkt.type == TYPE_DATA) {
            log_event("RECV DATA", &pkt);
            if (pkt.seqNum == expected_seq) {
                fwrite(pkt.data, 1, pkt.length, fp);
                total_received += pkt.length;
                expected_seq++;
                print_progress_bar(total_received, f_size);
            }

            if (drop(drop_prob)) {
                fprintf(log_fp, "Packet %d dropped intentionally\n", pkt.seqNum);
                fflush(log_fp);
                continue;
            }

            Packet ack;
            memset(&ack, 0, sizeof(ack));
            ack.type = TYPE_ACK;
            ack.ackNum = pkt.seqNum;
            if (sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&sender_addr, addr_len) < 0) {
                perror("Failed to send ACK");
            } else {
                log_event("SEND ACK", &ack);
            }
        } else if (pkt.type == TYPE_EOT) {
            log_event("RECV EOT", &pkt);
            break;
        }
    }

    // Cleanup
    fclose(fp);
    fclose(log_fp);
    close(sockfd);
    printf("\nFile transfer complete.\n");

    return 0;
}
