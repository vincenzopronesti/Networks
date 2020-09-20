#define BACKLOG 10
#define BOOTSTRAP_SERVER_PORT 9999
#define MAXLINE 1024
#define PORT_DIM 6 // dimensione massima del numero di porta scritto a caratteri piu' terminatore
#define MYSHAREDFILESDIR "files" // directory in cui sono contenuti i file condivisi

int sendGetRequest(char *filename, char *ipPeer, char *portPeer);
int searchFile(char *fileToSearch, char *fileWithList);
void replyGetRequest(int connfd, struct sockaddr *peerAddr, char *fileToSearch, char *fileWithList);
int strend(const char *s, const char *t);
int makeSharedFilesList(char *filename);
int sendList(int connfd, char *filename);
void clearFile(char *path);
void clearDir(char *path);
ssize_t writen(int fd, const void *buf, size_t n);
int readn(int fd, void *vptr, int len);
int readline(int fd, void *vptr, int maxlen);
