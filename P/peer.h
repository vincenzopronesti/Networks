#define AVAILABLESP "availableSP.txt"
#define MYSHAREDFILELIST "sharedfilelist.txt"

void querySuperPeer();
int handleJoin(int superPSockFd);
void handleLeave(int superPSockFd);
void handleWhoHas(int superPSockFd, char *fileToSearch);
int handleUpdate(int superPSockFd);
void handleLeaveSP(int groupId);
void processPeerRequests(int connfd, struct sockaddr *peerAddr, int groupId);
void childJobPeer(int readingPipeFd);
int connectToASuperPeer(int c);
int showAvailableSP();
int reconnectToASuperPeer();
void leaveSPHandler(int sigN);
