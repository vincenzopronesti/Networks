#define DATA "data/"
#define AVAILABLESP "availableSP.txt"

void handleJOIN(int bootSockFd);
void handleLEAVE(int bootSockFd);
void handleJoin(int connfd, struct sockaddr *peerAddr, char *port);
void handleLeave(int connfd, struct sockaddr *peerAddr, char *port);
void handleWhoHas(int connfd, struct sockaddr *peerAddr, char *fileToSearch, char *currentIP);
void handleWhoHasAsPeer(char *fileToSearch, char *currentIP);
void handleUpdate(int connfd, struct sockaddr *peerAddr, char *port);
void handleUpdateAsPeer(char *currentIP);
void sendWHOHASrequest(char *fileToSearch, char *foundPA, int *pos, char *currentIP);
void replyWHOHASrequest(int connfd, struct sockaddr *peerAddr, char *fileToSearch);
void handleUpdateSP(int connfd);
void processRequests(int connfd, struct sockaddr *peerAddr, char *currentIP);
void childJob(int writingPipeFd, int readingPipeFd);
