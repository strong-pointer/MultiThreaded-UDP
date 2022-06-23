#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>
#include <math.h>
#include "Practical.h"

struct Arguments {
  int pingPacketCount, sizeInBytes, sock;
  double pingInterval;
  char *server, *portNumber;
  struct sockaddr_storage clntAddr;
};
struct timespec start, end;
pthread_t thread[2];

// Waiting function Variables:
pthread_mutex_t fakeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t fakeCondition = PTHREAD_COND_INITIALIZER;

// Client Variables:
char *echoString; // Second arg: word to echo
size_t echoStringLen;
struct addrinfo *servAddr; // List of server addresses
ssize_t numBytes;
int counter, rcvCount, sock;
double rttMin = 9999999999, rttAvg = 0, rttMax = 0;
struct timespec startC, endC;
// default: print all info
bool noPrint = false, 
  // If the signal CTRL + C was called, off at first (used for further alterations)
  signalHandler = false;
char *packetStr;

// Server variables:
struct sockaddr_storage clntAddr; // Client address
char buffer[MAXSTRINGLENGTH]; // I/O buffer
ssize_t numBytesRcvd;

void sigHandler(int dummy) {
  signalHandler = true;

  if (noPrint) printf("**********\n");
  // Close down the threads
  pthread_join(thread[0], NULL);
  pthread_join(thread[1], NULL);
  
  counter++;
  // Calculate time the entire program took
  clock_gettime(CLOCK_REALTIME, &end);
  double timeTakenOverall;
  timeTakenOverall = ((end.tv_sec - start.tv_sec) * 1.0e3);
  if (end.tv_nsec > start.tv_nsec) timeTakenOverall += ((end.tv_nsec - start.tv_nsec) * 1.0e-6);
  // Final printout
  printf("\n%d packets transmitted, %d received. %.0lf%% packet loss, time %.0lf ms\n", counter, rcvCount, ((1 - (double)(rcvCount/counter))*10), timeTakenOverall);
  printf("rtt min/avg/max = %.3lf/%.3lf/%.3lf msec\n", rttMin, rttAvg/(double)(counter), rttMax);

  // Free and close packet/socket
  free(packetStr);
  close(sock);
  // Exit the program
  exit(0);
}

// Function to wait a specified # of seconds using pthread_cond_timedwait()
void myWait(double timeInS) {
  struct timespec ts;
  int rt = 0;

  clock_gettime(CLOCK_REALTIME, &ts);
  // Lets you wait in ms, as opposed to seconds by def
  int timeInMs = timeInS * 1000;
  ts.tv_nsec += timeInMs * 1000000;
  ts.tv_sec += ts.tv_nsec / 1000000000L;
  ts.tv_nsec = ts.tv_nsec % 1000000000L;
  
  pthread_mutex_lock(&fakeMutex);
  do {
    rt = pthread_cond_timedwait(&fakeCondition, &fakeMutex, &ts);
  } while (rt == 0);

  pthread_mutex_unlock(&fakeMutex);
}

// Client-side's sender thread function
void *Csender(void *args) {
  struct Arguments *my_args = (struct Arguments*)args;

  // Wait for the specified interval before sending
  myWait((*my_args).pingInterval);
  clock_gettime(CLOCK_REALTIME, &startC);
  
  // Send the string to the server
  numBytes = sendto(sock, echoString, echoStringLen, 0,
      servAddr->ai_addr, servAddr->ai_addrlen);
  if (numBytes < 0)
    DieWithSystemMessage("sendto() failed");
  else if (numBytes != echoStringLen)
    DieWithUserMessage("sendto() error", "sent unexpected number of bytes");

  return NULL;
}

// Client-side's receiver thread function
void *Creceiver(void *args) {
  struct Arguments *my_args = (struct Arguments*)args;
  
  struct sockaddr_storage fromAddr; // Source address of server
  // Set length of from address structure (in-out parameter)
  socklen_t fromAddrLen = sizeof(fromAddr);
  char buffer[echoStringLen + 1]; // I/O buffer
  numBytes = recvfrom(sock, buffer, MAXSTRINGLENGTH, 0,
      (struct sockaddr *) &fromAddr, &fromAddrLen);
  if (numBytes < 0)
    DieWithSystemMessage("recvfrom() failed");
  else if (numBytes != echoStringLen)
    DieWithUserMessage("recvfrom() error", "received unexpected number of bytes");

  // Verify reception from expected source
  if (!SockAddrsEqual(servAddr->ai_addr, (struct sockaddr *) &fromAddr))
    DieWithUserMessage("recvfrom()", "received a packet from unknown source");

  // End timer that started in Client Receiver
  clock_gettime(CLOCK_REALTIME, &endC);
  // Calculate round trip time for this packet (in ms)
  double timeTaken;
  timeTaken = ((endC.tv_sec - startC.tv_sec) * 1.0e3);
  if (endC.tv_nsec > startC.tv_nsec) timeTaken += ((endC.tv_nsec - startC.tv_nsec) * 1.0e-6);

  // If this is the last packet, free the address info struct
  if (counter+1 == (*my_args).pingPacketCount)
    freeaddrinfo(servAddr);

  buffer[echoStringLen] = '\0';     // Null-terminate received data
  // Print the echoed bytes with stats, if noPrint is false (default)
  if (!noPrint)
    printf("%3d%6lu%10.3lf\n", counter+1, sizeof(buffer), timeTaken);
  // Calculate RTT variables:
  rttAvg += timeTaken;
  if (rttMin > timeTaken) rttMin = timeTaken;
  if (rttMax < timeTaken) rttMax = timeTaken;
  rcvCount++;

  return NULL;
}

// Server-side's sending thread function
void *Ssender(void *args) {
  struct Arguments *my_args = (struct Arguments*)args;

  // Send received datagram back to the client
  ssize_t numBytesSent = sendto((*my_args).sock, buffer, numBytesRcvd, 0,
      (struct sockaddr *) &clntAddr, sizeof(clntAddr));
  if (numBytesSent < 0)
    DieWithSystemMessage("sendto() failed)");
  else if (numBytesSent != numBytesRcvd)
    DieWithUserMessage("sendto()", "sent unexpected number of bytes");

  return NULL;
}

// Server-side's receiving thread function
void *Sreceiver(void *args) {
  struct Arguments *my_args = (struct Arguments*)args;
  // Set Length of client address structure (in-out parameter)
  socklen_t clntAddrLen = sizeof(clntAddr);

  // Size of received message
  numBytesRcvd = recvfrom((*my_args).sock, buffer, MAXSTRINGLENGTH, 0,
      (struct sockaddr *) &clntAddr, &clntAddrLen);
  if (numBytesRcvd < 0)
    DieWithSystemMessage("recvfrom() failed");
  
  return NULL;
}

int main(int argc, char *argv[]) {
  int opt;
  char *temp;
  struct Arguments args;
  // Default values for input:
  args.pingPacketCount = 0x7fffffff;
  args.pingInterval = 1.0;
  args.portNumber = "33333";
  args.sizeInBytes = 12;
    // default: client mode
  bool serverMode = false;

  // Parse Command Line Arguments for possible edits to defaults
  while ((opt = getopt(argc, argv, "c:i:p:s:nS")) != -1) 
  {
    switch (opt)
    {
      case 'c':
        args.pingPacketCount = atoi(optarg);
        break;
      case 'i':
        temp = optarg;
        sscanf(temp, "%lf", &args.pingInterval);
        break;
      case 'p':
        args.portNumber = optarg;
        break;
      case 's':
        args.sizeInBytes =  atoi(optarg);
        break;
      case 'n':
        noPrint = true;
        break;
      case 'S':
        serverMode = true;
        break;
    }
  }
  // Server address is always the last CLA
  args.server = argv[argc-1];
  int dotCount = 0;
  if (!serverMode) {
    for (int i = 0; i < sizeof(args.server); i++) {
      if (args.server[i] == '.') dotCount++;
    }
    if (dotCount < 2) {
      fprintf(stderr, "%s is not a valid IP, please re-enter at end of CLAs!\nClosing the program\n", args.server);
      exit(0);
    }
  }

  // Client Mode
  if (!serverMode) {
    // Start the signal handler for CTRL + C
    signal(SIGINT, sigHandler);
    rcvCount = 0;

    // Create a packet with the size specified by user
    packetStr = (char *)malloc(sizeof(char)*args.sizeInBytes+1);
    memset(packetStr, '#', args.sizeInBytes);
    *(packetStr+args.sizeInBytes-1) = '\0';

    // Last arg: server address/name
    char *server = args.server;

    // Set the sent string to our newly allocated "packet"
    echoString = packetStr;
    echoStringLen = strlen(packetStr);
    if (echoStringLen > MAXSTRINGLENGTH) // Check input length
      DieWithUserMessage(echoString, "string too long");

    // Argument (optional): server port/service
    char *servPort = args.portNumber;

    // Tell the system what kind(s) of address info we want
    struct addrinfo addrCriteria;                   // Criteria for address match
    memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
    addrCriteria.ai_family = AF_UNSPEC;             // Any address family
    // For the following fields, a zero value means "don't care"
    addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram sockets
    addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP protocol

    // Get address(es)
    int rtnVal = getaddrinfo(server, servPort, &addrCriteria, &servAddr);
    if (rtnVal != 0)
      DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));

    // Create a datagram/UDP socket
    sock = socket(servAddr->ai_family, servAddr->ai_socktype,
        servAddr->ai_protocol); // Socket descriptor for client
    args.sock = sock;
    if (sock < 0)
      DieWithSystemMessage("socket() failed");

    // Print headers, only print first 3 if noPrint is selected by CLA
    fprintf(stderr, "Count%15d\n", args.pingPacketCount);
    fprintf(stderr, "Size%16d\n", args.sizeInBytes);
    fprintf(stderr, "Interval%12.3lf\n", args.pingInterval);
    if (!noPrint) {
      fprintf(stderr, "Port%16s\n", args.portNumber);
      fprintf(stderr, "Server_ip      %s\n", args.server);
    }

    // Start program timer
    clock_gettime(CLOCK_REALTIME, &start);
    // Send specified # of packets by multi-threading
    for (counter = 0; counter < args.pingPacketCount && signalHandler == false; counter++) {
      // Send the string to the server
      pthread_create(&thread[0], NULL, Csender, &args);
      // Receive a response
      pthread_create(&thread[1], NULL, Creceiver, &args);
      // Close threads
      pthread_join(thread[0], NULL);
      pthread_join(thread[1], NULL);
    }
    // Calculate time the entire program took
    clock_gettime(CLOCK_REALTIME, &end);
    double timeTakenOverall;
    timeTakenOverall = ((end.tv_sec - start.tv_sec) * 1.0e3);
    if (end.tv_nsec > start.tv_nsec) timeTakenOverall += ((end.tv_nsec - start.tv_nsec) * 1.0e-6);

    if (noPrint) printf("**********\n");
    // Final printout
    printf("%d packets transmitted, %d received. %.0lf%% packet loss, time %.0lf ms\n", counter, rcvCount, ((1 - (double)(rcvCount/counter))*10), timeTakenOverall);
    printf("rtt min/avg/max = %.3lf/%.3lf/%.3lf msec\n", rttMin, rttAvg/(double)(counter), rttMax);
    free(packetStr);

    close(sock);
  }
  // Server Mode
  else {
    char *service = args.portNumber; // Argument:  local port/service

    // Construct the server address structure
    struct addrinfo addrCriteria;                   // Criteria for address
    memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
    addrCriteria.ai_family = AF_UNSPEC;             // Any address family
    addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
    addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram socket
    addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP socket

    struct addrinfo *servAddr; // List of server addresses
    int rtnVal = getaddrinfo(NULL, service, &addrCriteria, &servAddr);
    if (rtnVal != 0)
      DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));

    // Create socket for incoming connections
    int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
        servAddr->ai_protocol);
    args.sock = sock;
    if (sock < 0)
      DieWithSystemMessage("socket() failed");

    // Bind to the local address
    if (bind(sock, servAddr->ai_addr, servAddr->ai_addrlen) < 0)
      DieWithSystemMessage("bind() failed");

    // Free address list allocated by getaddrinfo()
    freeaddrinfo(servAddr);

    for (;;) { // Run forever
      // Run threads
      pthread_create(&thread[0], NULL, Sreceiver, &args);
      pthread_join(thread[0], NULL);
      // Close threads
      pthread_create(&thread[1], NULL, Ssender, &args);
      pthread_join(thread[1], NULL);
    }
  }

  return 0;
}