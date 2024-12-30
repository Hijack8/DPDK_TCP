#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVER_PORT 8889
#define SERVER_IP "192.168.10.66"
#define MAX_BUFFER_SIZE 1024

int main() {
  int sock;
  struct sockaddr_in serverAddr;
  char sendBuffer[MAX_BUFFER_SIZE];
  memset(sendBuffer, 0, sizeof sendBuffer);
  char recvBuffer[MAX_BUFFER_SIZE];
  socklen_t addr_size;

  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) {
    perror("Socket creation failed");
    return 1;
  }

  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(SERVER_PORT);
  serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

  char *message = "Hello, UDP Server!";
  strcpy(sendBuffer, message);

  sendto(sock, sendBuffer, strlen(sendBuffer), 0,
         (struct sockaddr *)&serverAddr, sizeof(serverAddr));
  printf("Sent message: %s\n", message);

  addr_size = sizeof(serverAddr);
  int n = recvfrom(sock, recvBuffer, MAX_BUFFER_SIZE, 0,
                   (struct sockaddr *)&serverAddr, &addr_size);
  if (n < 0) {
    perror("Error receiving response");
    return 1;
  }
  recvBuffer[n] = '\0';

  printf("Received from server: %s\n", recvBuffer);

  close(sock);

  return 0;
}
