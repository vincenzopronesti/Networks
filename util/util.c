#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "util.h"

/**
    invia la richiesta get a SP/P

    filename: nome del file che si vuole scaricare
    ipPeer: IP del SP/P da cui si intende scaricare il file
    portPeer: porta del SP/P da cui si intende scaricare il file
    return: esito dell'operazione
        0 non si riesce a contattare il SP/P, segnala che 
            non e' necessario effettuare l'update
        -1 il file cercato non e' condiviso dal SP/P a 
            cui viene richiesto, segnala che 
            non e' necessario effettuare l'update
        >0 trasferimento andato a buon fine, quindi si puo' 
            effettuare l'update
    viene utilizzata sia da P che SP
    durante la scrittura i dati vengono scritti su un file il cui nome 
    termina in ".tmp", dopo la scrittura il file viene rinominato
 */
int sendGetRequest(char *filename, char *ipPeer, char *portPeer)
{
    // creazione socket e connessione al P
    int sockfd;
    struct sockaddr_in peeraddr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }

    memset((void *)&peeraddr, 0, sizeof(peeraddr));
    peeraddr.sin_family = AF_INET;
    int portPi = atoi(portPeer);
    peeraddr.sin_port = htons(portPi);
    if (inet_aton(ipPeer, &peeraddr.sin_addr) != 1) {
        fprintf(stderr, "error in inet_aton, cannot convert IP address\n");
        exit(EXIT_FAILURE);
    }

    if (connect(sockfd, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
        perror("error in connect");
        fprintf(stderr, "cannot connect to the peer %s:%s\n", ipPeer, portPeer);
        close(sockfd);
        return 0; // segnala che non e' necessario far aggiornare la lista dei file condivisi
    }

    // costruisce il messaggio get filename
    char *message = malloc(MAXLINE);
    memset(message, '\0', MAXLINE);
    sprintf(message, "get %s\n", filename);

    if (writen(sockfd, message, strlen(message)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }
    // lettura di un intero che rappresenta quanti byte saranno inviati
    int received_int = 0;
    int sz = 0;

    int return_status = read(sockfd, &received_int, sizeof(received_int));
    if (return_status > 0) {
        sz = ntohl(received_int);
    } else {
        fprintf(stderr, "error while reading file dimension from socket\n");
        exit(EXIT_FAILURE);
    }

    if (sz == -1) {
        printf(">>>The file you searched isn't on this peer\n");
    } else {
        // costruzione di due percorsi "files/filename" e "files/filename.tmp" 
        // quello che finisce con tmp viene utilizzato durante la scrittura 
        // poi viene rinominato eliminando il .tmp finale
        FILE *fp;
        char *path = malloc(MAXLINE);
        memset(path, '\0', MAXLINE);
        char *pathTmp = malloc(MAXLINE);
        memset(pathTmp, '\0', MAXLINE);
        snprintf(path, MAXLINE, "files/%s", filename);
        snprintf(pathTmp, MAXLINE, "files/%s.tmp", filename);
        fp = fopen(pathTmp, "wb"); 
        if (fp == NULL) {
            printf("Error opening file");
            exit(EXIT_FAILURE);
        }

        int bytesReceived = 0;
        int bytesRead = 0;
        char recvBuff[256];
        memset(recvBuff, 0, 256);
        
        while (bytesReceived < sz) {
            int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived));
            bytesRead = readn(sockfd, recvBuff, toRead);
            fwrite(recvBuff, 1, bytesRead, fp);
            bytesReceived += bytesRead;
        }
        if(bytesReceived < 0) {
            fprintf(stderr, "\n Read Error \n");
        }
        if (fclose(fp) < 0) {
            perror("error in fclose");
        }
        if(rename(pathTmp, path) < 0) {
            perror("error in rename");
        }
        if (path) {
            free(path);
        }
        if (pathTmp) {
            free(pathTmp);
        }
    }
    if (message) {
        free(message);
    }
    close(sockfd);
    return sz != -1; // serve per valutare se e' necessario fare l'update dei file condivisi
}

/**
    fileToSearch: nome del file che si vuole cercare
    fileWithList: file che all'interno contiene un elenco nel quale
        si controlla se fileToSearch e' presente o meno
    return: esito della ricerca
        0 fileToSearch non e' presente in fileWithList
        1 fileToSearch e' presente in fileWithList
 */
int searchFile(char *fileToSearch, char *fileWithList)
{
    int found = 0;
    FILE *sharedFileList = fopen(fileWithList, "r");
    if (sharedFileList == NULL) {
        perror("error in fopen");
    }
    char *line = malloc(MAXLINE);
    memset(line, '\0', MAXLINE);
    while ((fgets(line, MAXLINE, sharedFileList) != NULL) && found == 0) {
        *(line + strlen(line) - 1) = '\0';
        if (strcmp(line, fileToSearch) == 0) {
            found = 1;
        }
    }
    if (line) {
        free(line);
    }
    if (fclose(sharedFileList) < 0) {
        perror("error in fclose");
    }
    return found;
}

/**
    risponde alla richiesta di get di un file da parte di un altro P/SP

    connfd: socket di connessione a SP/P
    peerAddr: struct relativa a cui ha effettuato la richiesta
    fileToSearch: nome del file richiesto
    fileWithList: percorso del file che contiene l'elenco dei file condivisi
        nel caso dei P sara' "sharedfilelist.txt"
        nel caso dei SP sara' "data/IP:porta"

    e' utilizzata sia da P che da SP
    controlla che il file richiesto sia effettivamente condiviso
    se il file esiste, prima di inviarlo, ne invia la dimensione
    altrimenti risponde alla richiesta con -1
 */
void replyGetRequest(int connfd, struct sockaddr *peerAddr, char *fileToSearch, char *fileWithList)
{
    // controllo esistenza del file
    if (searchFile(fileToSearch, fileWithList)) {
        FILE *fp;
        char *path = malloc(MAXLINE);
        memset(path, '\0', MAXLINE);
        snprintf(path, MAXLINE, "files/%s", fileToSearch);
        fp = fopen(path, "rb"); 
        if (fp == NULL) {
            printf("Error opening file");
            exit(EXIT_FAILURE);
        }

        // invia la dimensione del file
        fseek(fp, 0, SEEK_END);
        int sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        int tmp = htonl(sz);
        if (writen(connfd, &tmp, sizeof(tmp)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }

        int sentBytes = 0;
        while (sentBytes < sz) {
            unsigned char buff[256] = {0};
            int toRead = ((sz - sentBytes > 256) ? (256) : (sz - sentBytes));
            int nread = fread(buff, 1, toRead, fp);

            if (nread > 0) {
                if (writen(connfd, buff, nread) < 0) {
                    perror("error in write");
                    exit(EXIT_FAILURE);
                }
                sentBytes += nread;
            }

            if (nread < 256) {
                if (feof(fp))
                    fprintf(stderr, "End of file\n");
                if (ferror(fp))
                    fprintf(stderr, "Error reading\n");
                break;
            }
        }
        if (path) {
            free(path);
        }
        if (fclose(fp) < 0) {
            perror("error in fclose");
        }
    } else {
        // il file non esiste
        int sz = -1; 
        int tmp = htonl(sz);
        if (writen(connfd, &tmp, sizeof(tmp)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
    }
}

/**
    controlla se la "stringa" t e' la parte finale della 
    "stringa" s

    restituisce 1 in caso affermativo, 0 altrimenti

    e' utilizzata da makeSharedFilesList per evitare di 
    inserire i file in scrittura nella lista dei file condivisi 
 */
int strend(const char *s, const char *t)
{
    size_t ls = strlen(s); // find length of s
    size_t lt = strlen(t); // find length of t
    if (ls >= lt)  // check if t can fit in s
    {
        // point s to where t should start and compare the strings from there
        return (0 == memcmp(t, s + (ls - lt), lt));
    }
    return 0; // t was longer than s
}

/**
    filename: nome del file da creare 
    return: dimensione del file creato

    crea la lista dei file condivisi e la salva nel file filename
    
    e' utilizzata sia da P che da SP 
    nel caso dei P filename sara' "sharedfilelist.txt"
    nel caso dei SP filename sara' "data/IP:porta"
 */
int makeSharedFilesList(char *filename)
{
    FILE *sharedFileList = fopen(filename, "w");
    if (sharedFileList == NULL) {
        perror("error in fopen");
    }
    int dim = 0;
    DIR *d;
    struct dirent *dir;
    d = opendir(MYSHAREDFILESDIR);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                // se il file non e' in scrittura allora si puo' condividere  
                if (strend(dir->d_name, ".tmp") == 0) 
                    fprintf(sharedFileList, "%s\n", dir->d_name);
            }
        }
        closedir(d);
    }
    fseek(sharedFileList, 0L, SEEK_END);
    dim = ftell(sharedFileList);
    if (fclose(sharedFileList) < 0) {
        perror("error in fclose");
    }
    return dim;
}

/**
    connfd: socket di connessione a SP
    filename: nome del file da inviare
    return: esisto dell'operazione
        -1 errore
        0 ok

    invia il contenuto di un file, e' utilizzata sia da 
    BS(lista dei SP) che da P(lista dei file condivisi)
 */
int sendList(int connfd, char *filename)
{
    FILE *fp = fopen(filename,"rb");
    if (fp == NULL) {
        printf("File open error");
        exit(EXIT_FAILURE);   
    }

    // invia la dimensione del file
    fseek(fp, 0L, SEEK_END);
    int sz = ftell(fp);
    rewind(fp);
    int tmp = htonl(sz);
    if (writen(connfd, &tmp, sizeof(tmp)) < 0) {
        perror("error in write");
        return -1; // peer disconnected
    }

    int sentBytes = 0;
    while (sentBytes < sz) {
        unsigned char buff[256] = {0};
        int toRead = ((sz - sentBytes > 256) ? (256) : (sz - sentBytes));
        int nread = fread(buff, 1, toRead, fp);      

        if (nread > 0) {
            if (writen(connfd, buff, nread) < 0) {
                perror("error in write");
                exit(EXIT_FAILURE);
            }
            sentBytes += nread;
        }

        if (nread < 256) {
            if (feof(fp))
                fprintf(stderr, "End of file\n");
            if (ferror(fp))
                fprintf(stderr, "Error reading\n");
            break;
        }
    }
    if (fclose(fp) != 0 ) {
        perror("error in fclose");
        exit(EXIT_FAILURE);
    }
    return 0; // ok
}

/** 
    path: percorso di un file di cui si vuole cancellare
        il contenuto
 */
void clearFile(char *path)
{
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		perror("error in fopen");
		fprintf(stderr, "cannot clear file \"%s\"\n", path);
	}
	if (fclose(f) < 0) {
		perror("error in fclose");
	}
}

/**
    path: directory di cui si vuole eliminare il contenuto
 */
void clearDir(char *path)
{
	DIR *d;
    struct dirent *dir;
    d = opendir(path);

    if (d) {
	    while ((dir = readdir(d)) != NULL) {
	        if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
	            char completePath[256];
	            memset(completePath, '\0', 256);
	            strcat(completePath, path);
	            strcat(completePath, "/");
	            strcat(completePath, dir->d_name);
			    if (remove(completePath) < 0) {
			        perror("error in remove");
			        fprintf(stderr, "cannot remove %s\n", completePath);
			    }
	        }
	    }
	} else {
		perror("error in opendir");
	}
    closedir(d);

}

ssize_t writen(int fd, const void *buf, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    const char *ptr;

    ptr = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if ((nwritten < 0) && (errno == EINTR)) 
                nwritten = 0;        
            else 
                return(-1);     /* errore */
        }
        nleft -= nwritten;
        ptr += nwritten;  
    }
    return(n-nleft);
}

/**
    prova a leggere esattamente len byte da fd e si scrive in vptr
 */
int readn(int fd, void *vptr, int len)
{
    int  n, rc;
    char c, *ptr;

    ptr = vptr;
    for (n = 1; n <= len; n++) {
        if ((rc = read(fd, &c, 1)) == 1) { 
            *ptr++ = c;
        } else if (rc == 0) {        /* read ha letto l'EOF */
            if (n == 1) 
                return(0); /* esce senza aver letto nulla */
            else 
                break;
        } else 
            return(-1);      /* errore */
    }

    //*ptr = 0; /* per indicare la fine dell'input */
    return(n - 1);    /* restituisce il numero di byte letti */
}

int readline(int fd, void *vptr, int maxlen)
{
    int  n, rc;
    char c, *ptr;

    ptr = vptr;
    for (n = 1; n < maxlen; n++) {
        if ((rc = read(fd, &c, 1)) == 1) { 
            *ptr++ = c;
            if (c == '\n') 
                break;
        } else if (rc == 0) {        /* read ha letto l'EOF */
            if (n == 1) 
                return(0); /* esce senza aver letto nulla */
            else 
                break;
        } else {
            return(-1);      /* errore */
        }
    }

    *ptr = 0; /* per indicare la fine dell'input */
    return(n);    /* restituisce il numero di byte letti */
}
