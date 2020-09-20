#define LIST "list.txt"

void handleJOIN(int connfd, struct sockaddr *peerAddr, char *port);
void handleLEAVE(struct sockaddr *peerAddr, char *port);
void updateSP(int checkJoin, char *ipJoined);
void processRequests(int connfd, struct sockaddr *peerAddr);
