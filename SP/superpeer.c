#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <sys/mman.h>

#include "superpeer.h"
#include "../util/util.h"

int PORT_LISTENING = -1;
int isJoined = 0;

/**
    invia il messaggio di join al BS e riceve la lista dei SP
    crea la lista dei propri file condivisi e la aggiunge alla 
    sua directory "data/", dove sarvera' anche i dati comunicati 
    dagli altri P

    bootSockFd: socket connessa al BS
 */
void handleJOIN(int bootSockFd)
{
    if (!isJoined) {
        // lettura del numero di porta sul quale il SP sara' in ascolto di connessioni, 
        // dovra' essere inviato al nodo di bootstrap nel messaggio di JOIN
        char *port = malloc(PORT_DIM);
        memset(port, 0, PORT_DIM);
        sprintf(port, "%d", PORT_LISTENING);
        // costruisce il messaggio di JOIN numeroPorta
        char *msg = malloc(MAXLINE);
        memset(msg, 0, MAXLINE);
        sprintf(msg, "JOIN %d\n", PORT_LISTENING);

        if (writen(bootSockFd, msg, strlen(msg)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
        
        FILE *fp = fopen(AVAILABLESP, "wb"); 
        if (NULL == fp) {
            printf("Error opening file");
            exit(EXIT_FAILURE);
        }
        // legge la dimensione del file per sapere quanti byte dovra' leggere
        int received_int = 0;
        int sz = 0;

        int return_status = read(bootSockFd, &received_int, sizeof(received_int));
        if (return_status > 0) {
            sz = ntohl(received_int);
        } else {
            fprintf(stderr, "error while reading file dimension from socket\n");
            exit(EXIT_FAILURE);
        }
        // lettura dalla socket degli indirizzi dei SP
        int bytesReceived = 0;
        int bytesRead = 0;
        char recvBuff[256];
        memset(recvBuff, 0, 256);
        // riceve i dati in porzioni da 256 byte e li salva
        while (bytesReceived < sz) {
            int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
            bytesRead = readline(bootSockFd, recvBuff, toRead);
            fwrite(recvBuff, 1, bytesRead, fp);
            bytesReceived += bytesRead;
        }

        if(bytesReceived < 0) {
            fprintf(stderr, "\n Read Error \n");
        }
        if (fclose(fp) < 0) {
            perror("error in fclose");
            exit(EXIT_FAILURE);
        }

        // costruisce il percorso ed il nome del file da creare 
        // in cui inserire la lista dei file condivisi
        // sara' del tipo data/IP:PORTA
        // lettura del proprio ip
        struct sockaddr_in currAddr;
        memset((void *)&currAddr, 0, sizeof(currAddr));
        socklen_t lenSock = sizeof(currAddr);
        if (getsockname(bootSockFd, (struct sockaddr *) &currAddr, &lenSock) < 0) {
            perror("error in getsockname");
            exit(EXIT_FAILURE);
        }
        char currentIP[INET_ADDRSTRLEN];
        memset(currentIP, 0, INET_ADDRSTRLEN);
        struct in_addr ipAddr = (&currAddr)->sin_addr;
        if (inet_ntop(AF_INET, &ipAddr, currentIP, INET_ADDRSTRLEN) == NULL) {
            perror("error in inet_ntop");
            exit(EXIT_FAILURE);
        }
        // crea nuovo file (il cui nome sara' IP:PORTA di ascolto del SP stesso) nella directory data/ 
        // all'interno del file ci sara' la lista dei file condivisi
        char *path = malloc(MAXLINE);
        memset(path, '\0', MAXLINE);
        strcpy(path, DATA);
        strcat(path, currentIP);
        strcat(path, ":");
        strcat(path, port);

        // crea la lista dei file condivisi
        makeSharedFilesList(path);
        if (port) {
            free(port);
        }
        if (path) {
            free(path);
        }
        isJoined = 1;
    } else {
        printf("You've already sent a JOIN message to the bootstrap server\n");
    }
}

/**
    invia il messaggio di LEAVE al BS, poi informa tutti i P associati che 
    dovranno trovarsi un nuovo SP inviando in messaggio leaveSP

    bootSockFd: socket connessa al BS
 */
void handleLEAVE(int bootSockFd)
{
    // lettura del numero di porta che dovra' essere comunicato al BS
    char *port = malloc(PORT_DIM);
    memset(port, 0, PORT_DIM);
    sprintf(port, "%d", PORT_LISTENING);
    // costruisce il mesaggio LEAVE numeroPorta
    char *msg = malloc(MAXLINE);
    memset(msg, 0, MAXLINE);
    sprintf(msg, "LEAVE %d\n", PORT_LISTENING);

    if (writen(bootSockFd, msg, strlen(msg)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }

    // lettura del proprio ip per evitare di inviarsi il messaggio leaveSP
    struct sockaddr_in currAddr;
    memset((void *)&currAddr, 0, sizeof(currAddr));
    socklen_t lenSock = sizeof(currAddr);
    if (getsockname(bootSockFd, (struct sockaddr *) &currAddr, &lenSock) < 0) {
        perror("error in getsockname");
        exit(EXIT_FAILURE);
    }
    char currentIP[INET_ADDRSTRLEN];
    memset(currentIP, 0, INET_ADDRSTRLEN);
    struct in_addr ipAddr = (&currAddr)->sin_addr;
    if (inet_ntop(AF_INET, &ipAddr, currentIP, INET_ADDRSTRLEN) == NULL) {
        perror("error in inet_ntop");
        exit(EXIT_FAILURE);
    }
    // legge i nomi dei file nella directory data/ e invia il messaggio leaveSP
    DIR *d;
    struct dirent *dir;
    d = opendir("data");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                char *peerIp = strtok(dir->d_name, ":");
                if (peerIp == NULL) {
                    fprintf(stderr, "cannot get ip address from: %s\n", dir->d_name);
                    continue;
                }
                char *peerPort = strtok(NULL, ":");
                if (peerPort == NULL) {
                    fprintf(stderr, "cannot get port number from: %s\n", dir->d_name);
                    continue;
                }
                if ((strcmp(currentIP, peerIp) == 0) && (strcmp(port, peerPort) == 0)) {
                    continue; // non invia il messaggio al SP stesso
                } else {
                    // socket per comunicare con il P associato
                    int sock = socket(AF_INET, SOCK_STREAM, 0);
                    if (sock < 0) {
                        perror("error in socket");
                        exit(EXIT_FAILURE);
                    }
                    // inserimento di ip e porta nella struct peeraddr
                    int peerPortInt = atoi(peerPort);
                    struct sockaddr_in peeraddr;
                    memset((void *)&peeraddr, 0, sizeof(peeraddr));
                    peeraddr.sin_family = AF_INET;
                    peeraddr.sin_port = htons(peerPortInt);
                    if (inet_pton(AF_INET, peerIp, &peeraddr.sin_addr) <= 0) {
                        fprintf(stderr, "error in inet_pton for %s", peerIp);
                        exit(EXIT_FAILURE);
                    }    

                    if (connect(sock, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
                        fprintf(stderr, "error in connect to peer %s:%s\n", peerIp, peerPort);
                        perror("connect");
                    } else {
                        char *msg = "leaveSP\n";
                        if (writen(sock, msg, strlen(msg)) < 0) {
                            perror("error in write");
                            exit(EXIT_FAILURE);
                        }
                    }

                    if (close(sock) < 0) {
                        perror("error in close");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
        closedir(d);
    } else {
        perror("error in opendir");
        exit(EXIT_FAILURE);
    }
    if (port) {
        free(port);
    }
}

/**
    gestisce l'entrata di un nuovo P nella propria sottorete

    connfd: socket di connessione con il P
    peerAddr: struct relativa al P, serve per estrarre l'IP
    port: numero di porta inviato dal P

    crea un file relativo al P all'interno della 
    propria directory "data/" e ci salva l'elenco dei file 
    condivisi dal P che vuole effetturare il join  
 */
void handleJoin(int connfd, struct sockaddr *peerAddr, char *port)
{
    // lettura dell'ip del P
    struct in_addr ipAddr = ((struct sockaddr_in *) peerAddr)->sin_addr;
    char strIP[INET_ADDRSTRLEN];
    memset(strIP, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipAddr, strIP, INET_ADDRSTRLEN);
    // crea nuovo file (il cui nome sara' IP:PORTA del P) nella directory data/ 
    // all'interno del file ci sara' la lista dei file condivisi
    char *path = malloc(MAXLINE);
    memset(path, '\0', MAXLINE);
    strcpy(path, DATA);
    strcat(path, strIP);
    strcat(path, ":");
    strcat(path, port);

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("fopen");
    }
    // legge quanti saranno i byte che dovra' leggere dalla socket
    int received_int = 0;
    int sz = 0;

    int return_status = read(connfd, &received_int, sizeof(received_int));
    if (return_status > 0) {
        sz = ntohl(received_int);
    } else {
        fprintf(stderr, "error while reading file dimension from socket\n");
        exit(EXIT_FAILURE);
    }
    int bytesReceived = 0;
    int bytesRead = 0;
    char recvBuff[256];
    memset(recvBuff, 0, 256);
    // riceve i dati in porzioni da 256 byte e li salva sul file relativo al P 
    while (bytesReceived < sz) {
        int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
        bytesRead = readline(connfd, recvBuff, toRead);
        fwrite(recvBuff, 1, bytesRead, fp);
        bytesReceived += bytesRead;
    }

    if (fclose(fp) != 0) {
        perror("error in fclose");
    }
    if (path) {
        free(path);
    }
}

/**
    gestisce l'uscita di un P dalla propria sottorete
    
    connfd: socket di connessione con il P
    peerAddr: struct relativa al P, serve per estrarre l'IP
    port: numero di porta inviato dal P

    elimina il file che contiene la lista dei 
    file condivisi dal P dalla directory "data/"
 */
void handleLeave(int connfd, struct sockaddr *peerAddr, char *port)
{
    // lettura dell'ip del P da eliminare
    struct in_addr ipAddr = ((struct sockaddr_in *) peerAddr)->sin_addr;
    char strIP[INET_ADDRSTRLEN];
    memset(strIP, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipAddr, strIP, INET_ADDRSTRLEN);
    // costruisce il nome del file (IP:PORTA del P) che dovra' essere eliminato
    char *path = malloc(MAXLINE);
    memset(path, '\0', MAXLINE);
    strcpy(path, DATA);
    strcat(path, strIP);
    strcat(path, ":");
    strcat(path, port);

    // eliminazione del file contenente la lista dei file condivisi da un determinato P
    if (remove(path) < 0) {
        perror("error in remove");
    }
    if (path) {
        free(path);
    }
}

/**
    gestisce la richiesta whohas da parte di un P
    
    connfd: socket di connessione con il P
    peerAddr: struct relativa al P, serve per estrarre l'IP
    fileToSearch: nome del file di cui si vuole effettuare la ricerca
    currentIP: ip del SP, serve per evitare di inviare WHOHAS al SP stesso

    controlla prima nella directory dei P associati e poi manda la
    richiesta WHOHAS a tutti gli altri SP (che rispondono consultando 
    le rispettive directory data/)
 */
void handleWhoHas(int connfd, struct sockaddr *peerAddr, char *fileToSearch, char *currentIP)
{
    // area di memoria in cui salvare gli IP dei P che hanno il file cercato
    int dim = 4096; 
    char *foundPA = (char *) mmap(NULL, dim, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (foundPA == MAP_FAILED) {
        perror("error in mmap");
    }
    int pos = 0;

    // scansiona i file nella directory data per cercare 
    // se qualcuno dei P associati possiede il file cercato
    DIR *d;
    struct dirent *dir;
    d = opendir("data");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                char path[256];
                memset(path, '\0', 256);
                strcpy(path, "data/");
                strcat(path, dir->d_name);
                FILE *fp = fopen(path, "r");
                if (fp == NULL) {
                    perror("error in fopen");
                }
                char *line = NULL;
                size_t dim = 0;
                ssize_t read;

                while ((read = getline(&line, &dim, fp)) != -1) {
                    *(line + strlen(line) - 1) = '\0';
                    if (strcmp(line, fileToSearch) == 0) {
                        // si inserisce la coppia IP:porta del P che possiede il file 
                        // nella lista dei P da restituire a chi ha fatto la richiesta

                        char *a = malloc(MAXLINE);
                        memset(a, 0, MAXLINE);
                        strcpy(a, dir->d_name);
                        strcat(a, "\n");
                        strcat(foundPA + pos, a);
                        pos += strlen(a);
                        if (a) {
                            free(a);
                        }
                    }
                }
                if (line) {
                    free(line);
                }
                if (fclose(fp) < 0) {
                    perror("error in fclose");
                }
            }
        }
        closedir(d);
    } else {
        perror("error in opendir");
        exit(EXIT_FAILURE);
    }

    // invia il messaggio WHOHAS agli altri SP per fornire l'elenco completo dei P 
    // che condividono il file cercato
    sendWHOHASrequest(fileToSearch, foundPA, &pos, currentIP);

    int sz = pos;
    int tmp = htonl(sz);
    // invia il numero di byte che il P che ha fatto la richiesta dovra' leggere
    if (writen(connfd, &tmp, sizeof(tmp)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }

    int sentBytes = 0;
    int index = 0;
    while (sentBytes < sz) {
        char buff[256] = {0};
        int toRead = ((sz - sentBytes > 256) ? (256) : (sz - sentBytes));
        snprintf(buff, toRead + 1, "%s", (char *) foundPA + index); //  + 1 per il teminatore
        index += toRead;

        if (writen(connfd, buff, strlen(buff)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
        sentBytes += strlen(buff);
    }
    if (munmap(foundPA, dim) < 0) {
        perror("perror in munmap");
    }
}

/**
    gestisce la richiesta whohas fatta dal SP 

    fileToSearch: nome del file di cui si vuole effettuare la ricerca
    currentIP: ip del SP, serve per evitare di inviare WHOHAS al SP stesso

    e' sostanzialmente simile alla funzione handleWhoHas, la differenza e' che 
    i dati non devono essere inviati sulla socket ma mostrati all'utente
    effettua quindi i seguenti passi:
    1) controlla se il file cercato e' nella directory "data/" che tiene traccia dei file condivisi 
        dai P associati al SP stesso
    2) invia il comando WHOHAS agli altri SP 
    3) mostra i risultati all'utente 
 */
void handleWhoHasAsPeer(char *fileToSearch, char *currentIP)
{
    if (isJoined) {
        // area di memoria in cui salvare gli IP dei P che hanno il file cercato
        int dim = 4096; 
        char *foundPA = (char *) mmap(NULL, dim, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (foundPA == MAP_FAILED) {
            perror("error in mmap");
        }
        int pos = 0;

        // scansiona i file nella directory data per cercare 
        // se qualcuno dei P associati possiede il file cercato
        DIR *d;
        struct dirent *dir;
        d = opendir("data");
        if (d) {
            while ((dir = readdir(d)) != NULL) {
                if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                    char path[256];
                    memset(path, '\0', 256);
                    strcpy(path, "data/");
                    strcat(path, dir->d_name);
                    FILE *fp = fopen(path, "r");
                    if (fp == NULL) {
                        perror("error in fopen");
                    }
                    char *line = NULL;
                    size_t dim = 0;
                    ssize_t read;

                    while ((read = getline(&line, &dim, fp)) != -1) {
                        *(line + strlen(line) - 1) = '\0';
                        if (strcmp(line, fileToSearch) == 0) {
                            // si inserisce la coppia IP:porta del P che possiede il file 
                            // nella lista dei P da mostrare all'utente
                            char *a = malloc(MAXLINE);
                            memset(a, 0, MAXLINE);
                            strcpy(a, dir->d_name);
                            strcat(a, "\n");
                            strcat(foundPA + pos, a);
                            pos += strlen(a);
                            if (a) {
                                free(a);
                            }                   
                        }
                    }
                    if (line) {
                        free(line);
                    }
                    if (fclose(fp) < 0) {
                        perror("error in fclose");
                    }
                }
            }
            closedir(d);
        } else {
            perror("error in opendir");
            exit(EXIT_FAILURE);
        }
        //invia il messaggio WHOHAS agli altri SP
        sendWHOHASrequest(fileToSearch, foundPA, &pos, currentIP);

        int sz = pos;
        if (sz == 0) { // non e' stato salvato alcun indirizzo quindi il file cercato non e' disponibile
            printf(">>>The file you searched isn't available in this network\n");
        } else {
            printf(">>>You can download the file \"%s\" from the following peers:\n", fileToSearch);
            printf("%s", (char *) foundPA);
        }
        if (munmap(foundPA, dim) < 0) {
            perror("error in munmap");
        }
    } else {
        printf(">>>You need to send a JOIN message to the bootstrap server\n");
    }
}

/**
    gestisce il messaggio di update inviato dai P associati alla sottorete

    connfd: socket di connessione con il P
    peerAddr: struct relativa al P, serve per estrarre l'IP
    port: numero di porta inviato dal P

    legge il nuovo elenco di file condivisi e aggiorna il 
    file, nella directory "data/", relativo al P che ha inviato la richiesta
 */
void handleUpdate(int connfd, struct sockaddr *peerAddr, char *port)
{
    // lettura dell'ip del P che ha effettuato la richiesta
    struct in_addr ipAddr = ((struct sockaddr_in *) peerAddr)->sin_addr;
    char strIP[INET_ADDRSTRLEN];
    memset(strIP, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipAddr, strIP, INET_ADDRSTRLEN);
    // apre il file (il cui nome e' l'IP:PORTA del P) nella directory data
    // all'interno del file ci sara' la lista dei file condivisi che varra' quindi aggiornata
    char *path = malloc(MAXLINE);
    memset(path, '\0', MAXLINE);
    strcpy(path, DATA);
    strcat(path, strIP);
    strcat(path, ":");
    strcat(path, port);

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        perror("fopen");
    }
    // lettura del numero di byte da prendere dalla socket
    int received_int = 0;
    int sz = 0;

    int return_status = read(connfd, &received_int, sizeof(received_int));
    if (return_status > 0) {
        sz = ntohl(received_int);
    } else {
        fprintf(stderr, "error while reading file dimension from socket\n");
        exit(EXIT_FAILURE);
    }
    int bytesReceived = 0;
    int bytesRead = 0;
    char recvBuff[256];
    memset(recvBuff, 0, 256);
    
    while (bytesReceived < sz) {
        int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
        bytesRead = readline(connfd, recvBuff, toRead);
        fwrite(recvBuff, 1, bytesRead, fp);
        bytesReceived += bytesRead;
    }

    if (fclose(fp) != 0) {
        perror("error in fclose");
    }
    if (path) {
        free(path);
    }
}

/**
    gestisce il messaggio di update del SP stesso

    currentIP: ip del SP, serve per capire quale dei file nella directory 
        "data/" bisogna aggiornare
 */
void handleUpdateAsPeer(char *currentIP)
{
    // lettura del proprio numero di porta per sapere quale file aggiornare
    char *port = malloc(PORT_DIM);
    memset(port, 0, PORT_DIM);
    sprintf(port, "%d", PORT_LISTENING);

    // costruisce il percorso del file da aggiornare data/IP:PORTA
    char *path = malloc(MAXLINE);
    memset(path, '\0', MAXLINE);
    strcpy(path, DATA);
    strcat(path, currentIP);
    strcat(path, ":");
    strcat(path, port);

    // crea la lista dei file condivisi
    makeSharedFilesList(path);

    if (port) {
        free(port);
    }
    if (path) {
        free(path);
    }
}

/**
    gestisce l'invio del messaggio WHOHAS

    fileToSearch: nome del file da cercare
    foundPA: inizio dell'area di memoria in cui e' possibile salvare i
        le coppie IP:porta dei P che condividono il file cercato
    pos: rappresenta la posizione da cui e' possibile iniziare ad aggiungere 
        dati all'intero dell'area di memoria foundPA
    currentIP: ip del SP, serve per evitare di inviare WHOHAS al SP stesso

    invia la richiesta WHOHAS agli altri SP per farsi rispondere con gli indirizzi dei P 
    che hanno un determinato file ma si trovano in un'altra sottorete
    WHOHAS e' generato esclusivamente dai SP, su richiesta dei P (invio del messaggio whohas) 
    o tramite linea di comando (non era richiesto ma e' stato utile per le prove)
 */
void sendWHOHASrequest(char *fileToSearch, char *foundPA, int *pos, char *currentIP)
{
    // invia la richiesta WHOHAS a tutti i SP nel file, il SP che invia la richiesta dovra' escludere il 
    // proprio indirizzo dalla lista
    FILE *fp = fopen(AVAILABLESP, "r");
    if (fp == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }

    int superPSockFd = -1;
    char *line = NULL;
    size_t dim = 0;
    ssize_t readLineData;
    // il SP che invia la richiesta WHOHAS deve conoscere 
    // il proprio numero di porta per potersi escludere dall'elenco dei SP
    char *currentPort = malloc(PORT_DIM);
    memset(currentPort, 0, PORT_DIM);
    sprintf(currentPort, "%d", PORT_LISTENING);

    while ((readLineData = getline(&line, &dim, fp)) != -1) {
        *(line + strlen(line) - 1) = '\0'; // line contiene la coppia IP:PORTA
        struct sockaddr_in peeraddr;
        if ((superPSockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("error in socket");
            exit(EXIT_FAILURE);
        }
        char *ipSP = strtok(line, ":"); // IP del SP da contattare
        char *portSPchar = strtok(NULL, ":"); // porta del SP da contattare
        int portSP = atoi(portSPchar);

        if ((strcmp(ipSP, currentIP) == 0) && (strcmp(portSPchar, currentPort) == 0)) {
            if (close(superPSockFd) < 0) {
                perror("error in close");
                exit(EXIT_FAILURE);
            }
            continue; // non invia WHOHAS al proprio indirizzo
        }
        // inserisce ip e porta nella struct sockaddr_in
        memset((void *)&peeraddr, 0, sizeof(peeraddr));
        peeraddr.sin_family = AF_INET;
        peeraddr.sin_port = htons(portSP);
        if (inet_pton(AF_INET, ipSP, &peeraddr.sin_addr) <= 0) {
            fprintf(stderr, "error in inet_pton for %s", ipSP);
            exit(EXIT_FAILURE);
        }    

        if (connect(superPSockFd, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
            perror("error in connect");
        } else { 
            // costruzione del messaggio WHOHAS filename
            char *msg = malloc(MAXLINE);
            memset(msg, 0, MAXLINE);
            sprintf(msg, "WHOHAS %s\n", fileToSearch);

            if (writen(superPSockFd, msg, strlen(msg)) < 0) {
                perror("error in write");
                exit(EXIT_FAILURE);
            }
            // lettura del numero di byte che dovranno essere letti dalla socket
            int received_int = 0;
            int sz = 0;

            int return_status = read(superPSockFd, &received_int, sizeof(received_int));
            if (return_status > 0) {
                sz = ntohl(received_int);
            } else {
                fprintf(stderr, "error while reading file dimension from socket\n");
                exit(EXIT_FAILURE);
            }

            if (sz > 0) {
                int bytesReceived = 0;
                int bytesRead = 0;                
                while (bytesReceived < sz) {
                    char recvBuff[256];
                    memset(recvBuff, 0, 256);
                    int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
                    bytesRead = readline(superPSockFd, recvBuff, toRead);
                    strcat(foundPA + *pos, recvBuff);
                    *pos += strlen(recvBuff);

                    bytesReceived += bytesRead;
                }

                if(bytesReceived < 0) {
                    fprintf(stderr, "\n Read Error \n");
                }
            } else {
                // il file cercato non e' sul SP a cui si sta inviando la richiesta
//                printf("the file you searched \"%s\" is not available on super peer: %s:%s 's subnet\n", 
//                    fileToSearch, ipSP, portSPchar);
            }
            if (close(superPSockFd) < 0) {
                perror("error in close");
                exit(EXIT_FAILURE);
            }
        }
    }
    if (line) {
        free(line);
    }
    if (currentPort) {
        free(currentPort);
    }
    if (fclose(fp) < 0) {
        perror("error in fclose");
    }
}

/**
    e' il complementare di sendWHOHASrequest

    connfd: socket di connessione con il SP
    peerAddr: struct relativa al SP che ha inviato la richiesta
    fileToSearch: nome del file di cui si vuole effettuare la ricerca

    riceve la richiesta WHOHAS inviata da un altro SP quindi 
    deve rispondere effettuando la ricerca dei P che condividono 
    il file richiesto esclusivamente all'interno della propria sottorete

 */
void replyWHOHASrequest(int connfd, struct sockaddr *peerAddr, char *fileToSearch)
{
    // area di memoria in cui salvare gli IP dei P che hanno il file cercato
    int dim = 4096; 
    char *foundPA = (char *) mmap(NULL, dim, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (foundPA == MAP_FAILED) {
        perror("error in mmap");
    }
    int pos = 0;

    // apertura della directory in cui sono elencati i P associati con i relativi file condivisi
    // si apre ciascun file e si controlla se il file cercato e' contenuto al suo interno 
    // se lo e' allora la coppia IP:porta del P viene salvata 
    // e successivamente inviata come risposta al SP che la ha richiesta
    DIR *d;
    struct dirent *dir;
    d = opendir("data");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0) {
                char path[256];
                memset(path, '\0', 256);
                strcpy(path, "data/");
                strcat(path, dir->d_name);
                FILE *fp = fopen(path, "r");
                if (fp == NULL) {
                    perror("error in fopen");
                }
                char *line = NULL;
                size_t dim = 0;
                ssize_t read;

                while ((read = getline(&line, &dim, fp)) != -1) {
                    *(line + strlen(line) - 1) = '\0';
                    if (strcmp(line, fileToSearch) == 0) {
                        // si inserisce la coppia IP:porta del P che possiede il file 
                        // nella lista dei P da restituire a chi ha fatto la richiesta
                        char *a = malloc(MAXLINE);
                        memset(a, 0, MAXLINE);
                        strcpy(a, dir->d_name);
                        strcat(a, "\n");
                        strcat(foundPA + pos, a);
                        pos += strlen(a);
                        if (a) {
                            free(a);
                        }
                    }
                }
                if (line) {
                    free(line);
                }
                if (fclose(fp) < 0) {
                    perror("error in fclose");
                }
            }
        }
        closedir(d);
    }

    // invia la dimensione dell'elenco di indirizzi
    int sz = pos;
    int index = 0;
    int tmp = htonl(sz);
    if (writen(connfd, &tmp, sizeof(tmp)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }
    // invia i dati
    int sentBytes = 0;
    while (sentBytes < sz) {
        char buff[256] = {0};
        int toRead = ((sz - sentBytes > 256) ? (256) : (sz - sentBytes));
        snprintf(buff, toRead + 1, "%s", (char *) foundPA + index); //  + 1 per il teminatore
        index += toRead;

        if (writen(connfd, buff, strlen(buff)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
        sentBytes += strlen(buff);
    }
    if (munmap(foundPA, dim) < 0) {
        perror("error in munmap");
    }
}

/**
    gestisce la ricezione di updateSP dal BS

    connfd: socket di connessione al BS

    aggiorna l'elenco dei SP con la nuova versione fornita dal BS
 */
void handleUpdateSP(int connfd)
{
    FILE *fp = fopen(AVAILABLESP, "wb"); 
    if (NULL == fp) {
        printf("Error opening file");
        exit(EXIT_FAILURE);
    }
    // lettura del numero di byte che verranno inviati
    int received_int = 0;
    int sz = 0;

    int return_status = read(connfd, &received_int, sizeof(received_int));
    if (return_status > 0) {
        sz = ntohl(received_int);
    } else {
        fprintf(stderr, "error while reading file dimension from socket\n");
        exit(EXIT_FAILURE);
    }

    int bytesReceived = 0;
    int bytesRead = 0;
    char recvBuff[256];
    memset(recvBuff, 0, 256);
    // aggiornamento del file
    while (bytesReceived < sz) {
        int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
        bytesRead = readline(connfd, recvBuff, toRead);
        fwrite(recvBuff, 1, bytesRead, fp);
        bytesReceived += bytesRead;
    }

    if(bytesReceived < 0) {
        fprintf(stderr, "\n Read Error \n");
    }
    if (fclose(fp) < 0) {
        perror("error in fclose");
        exit(EXIT_FAILURE);
    }
}

/**
    eseguito dal nuovo processo creato dopo l'accept
    legge il comando inviato ed esegue la relativa funzione
    la lettura avviene tramite readline quindi  
    si legge dalla socket finche' non si incontra il carattere '\n'
    (questo perche' la dimensione dei messaggi inviati dai SP e' variabile e 
    in questo modo non e' necessario conoscere a priori la dimensione del messaggio da 
    leggere)

    connfd: socket di connessione con chi ha inviato la richiesta BS/SP/P
    peerAddr: struct relativa al nodo all'altro capo della connessione
    currentIP: ip del SP
 */
void processRequests(int connfd, struct sockaddr *peerAddr, char *currentIP)
{
    int n;
    char line[MAXLINE];
    memset(line, '\0', MAXLINE);
    while (1) {
        if ((n = readline(connfd, line, MAXLINE)) == 0)
            return; // closed connection
        if (n < 0) {
            perror("error in readline");
            exit(EXIT_FAILURE);
        }

        int notFound = 1;
        int i = 0;
        while (notFound) {
            if (*(line + i) == '\n') {
                *(line + i) = '\0';
                notFound = 0;
            } else {
                i++;
            }
        }

        char *buffer = strtok(line, " ");
        char *read_word;

        if (strcmp(buffer, "join") == 0){ // inviato da P
            printf(">>>join request received\n");
            read_word = strtok(NULL, " ");
            if (read_word != NULL) {
                handleJoin(connfd, (struct sockaddr *) peerAddr, read_word);
                printf(">>>join completed\n");
            } else {
                fprintf(stderr, "cannot serve join request, port number for a listening socket is required\n");
            }
        } 
        else if (strcmp(buffer, "leave") == 0){ // inviato da P
            // rimozione dell'indirizzo IP dalla lista dei P
            printf(">>>leave request received\n");
            read_word = strtok(NULL, " ");
            if (read_word != NULL) {
                handleLeave(connfd, (struct sockaddr *) peerAddr, read_word);
                printf(">>>leave completed\n");
                break;
            } else {
                fprintf(stderr, "cannot serve leave request, port number for a listening socket is required\n");
            }
        } else if (strcmp(buffer, "whohas") == 0) {
            printf(">>>whohas request received\n");
            // ricerca di un file, richiesta da un P
            // si dovra' leggere anche il nome del file
            if ((read_word = strtok(NULL, " ")) != NULL) {
                // cerca nella directory dei P associati
                // se non ci sono riscontri allora inoltra la richiesta WHOHAS agli altri SP
                handleWhoHas(connfd, (struct sockaddr *) peerAddr, read_word, currentIP);
                printf(">>>whohas completed\n");
            } else {
                // non e' stato inserito il nome del file da cercare
                fprintf(stderr, "insert a filename\n");
            }
        } else if (strcmp(buffer, "update") == 0) {
            // inviato da P per modificare la lista dei file condivisi
            printf(">>>update request received\n");
            read_word = strtok(NULL, " "); // lettura numero della porta del P
            if (read_word != NULL) {
                handleUpdate(connfd, (struct sockaddr *) peerAddr, read_word);
                printf(">>>update completed\n");
            } else {
                fprintf(stderr, "cannot serve update request, port number for a listening socket is required\n");
            }
        } else if (strcmp(buffer, "WHOHAS") == 0) {
            printf(">>>WHOHAS request received\n");
            read_word = strtok(NULL, " "); // lettura nome del file
            if (read_word != NULL) {
                replyWHOHASrequest(connfd, (struct sockaddr *) peerAddr, read_word);
                printf(">>>WHOHAS completed\n");
                break;
            } else {
                fprintf(stderr, "cannot reply to WHOHAS request, insert a filename\n");
            }
        } else if (strcmp(buffer, "updateSP") == 0) {
            // il messaggio viene inviato quando un nuovo SP si e' aggiunto alla rete
            // dopo l'aggiornamento del file non ci si aspettano altri messaggi dal BS 
            handleUpdateSP(connfd);
            printf(">>>list of available super peers updated\n");
            break;
        } else if (strcmp(buffer, "get") == 0) {
            printf(">>>get request received\n");
            // il SP ha ricevuto una richiesta di get da un altro P
            if ((read_word = strtok(NULL, " ")) != NULL) { // lettura nome del file
                // bisogna costruire il percorso del file in cui e' presente l'elenco 
                // dei file condivisi dal SP, per questo e' necessario conoscere il numero di 
                // porta su cui si e' in ascolto
                char *port = malloc(PORT_DIM);
                memset(port, 0, PORT_DIM);
                sprintf(port, "%d", PORT_LISTENING);                
                // lettura del proprio indirizzo ip
                struct sockaddr_in currAddr;
                memset((void *)&currAddr, 0, sizeof(currAddr));
                socklen_t lenSock = sizeof(currAddr);
                if (getsockname(connfd, (struct sockaddr *) &currAddr, &lenSock) < 0) {
                    perror("error in getsockname");
                    exit(EXIT_FAILURE);
                }
                char currentIP[INET_ADDRSTRLEN];
                memset(currentIP, 0, INET_ADDRSTRLEN);
                struct in_addr ipAddr = (&currAddr)->sin_addr;
                if (inet_ntop(AF_INET, &ipAddr, currentIP, INET_ADDRSTRLEN) == NULL) {
                    perror("error in inet_ntop");
                    exit(EXIT_FAILURE);
                }
                // creazione del percorso data/IP:PORTA in cui cercare se il file richiesto 
                // e' effettivamente condiviso
                char *path = malloc(MAXLINE);
                memset(path, '\0', MAXLINE);
                strcpy(path, DATA);
                strcat(path, currentIP);
                strcat(path, ":");
                strcat(path, port);

                replyGetRequest(connfd, (struct sockaddr *) peerAddr, read_word, path);
                printf(">>>get completed\n");
                if (port) {
                    free(port);
                }
                if (path) {
                    free(path);
                }
                break;
            } else {
                // non e' stato inserito il nome del file da cercare
                fprintf(stderr, "insert a filename\n");
            }
        } else {
            printf("I can't understand your request: %s\n", line);
        }
    }
}

/**
    writingPipeFd: pipe utilizzata per mandare al processo padre il numero 
        di porta su cui ci si e' messi in ascolto
    readingPipeFd: pipe utilizzata per leggere il proprio indirizzo IP 
        che viene fornito dal processo padre, in quanto viene letto dalla 
        socket connessa al BS 

    server ricorsivo che si mette in ascolto di connessioni e genera un 
    nuovo processo per servirle
    il numero di porta scelto dal server deve essere noto al processo padre 
    perche' questo possa inviarlo al BS
 */
void childJob(int readingPipeFdip, int writingPipeFdport)
{
    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("error in bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("Failed to listen");
        exit(EXIT_FAILURE);
    }

    // recupero del numero di porta del socket che e' in ascolto di connessioni dei P,
    // il numero di porta deve essere inviato al BS per il JOIN
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(listenfd, (struct sockaddr *)&sin, &len) == -1) {
        perror("getsockname");
        exit(EXIT_FAILURE);
    } else {
        PORT_LISTENING = ntohs(sin.sin_port);
        if (writen(writingPipeFdport, &PORT_LISTENING, sizeof(int)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
        if (close(writingPipeFdport) < 0) {
            perror("error in close");
        }
    }

    char *currentIP = malloc(MAXLINE);
    memset(currentIP, 0, MAXLINE);
    if (readn(readingPipeFdip, currentIP, MAXLINE) < 0) {
        perror("error in read");
        exit(EXIT_FAILURE);
    }

    if (close(readingPipeFdip) < 0) {
        perror("error in close");
    }

    while (1) {
        struct sockaddr_in peerAddr;
        unsigned int len = sizeof(peerAddr);
        connfd = accept(listenfd, (struct sockaddr*) &peerAddr , &len);

        pid_t pid;
        if ((pid = fork()) < 0) {
            perror("error in fork");
            exit(EXIT_FAILURE);
        }
        if (pid == 0) {
            if (close(listenfd) < 0) {
                perror("error in close");
                exit(EXIT_FAILURE);
            }
            processRequests(connfd, (struct sockaddr *) &peerAddr, currentIP);
            if (close(connfd) < 0) {
                perror("error in close");
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
        if (close(connfd) < 0) {
            perror("error in close");
        }
    }
}

/**
    1) prova a collegarsi a BS
    2) fa partire un processo che si occupa di gestire le richieste in arrivo dalla rete
    3) si mette in ascolto delle richieste dell'utente su stdin
 */
int main(void)
{
    int bootSockFd;
    struct sockaddr_in servaddr;
    if ((bootSockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(BOOTSTRAP_SERVER_PORT);
    // bootstrap server should always have the same IP address 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (connect(bootSockFd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        perror("error in connect, cannot connnect to bootstrap server");
        exit(EXIT_FAILURE);
    }

    // salva il proprio indirizzo IP
    struct sockaddr_in currAddr;
    memset((void *)&currAddr, 0, sizeof(currAddr));
    socklen_t lenSock = sizeof(currAddr);
    if (getsockname(bootSockFd, (struct sockaddr *) &currAddr, &lenSock) < 0) {
        perror("error in getsockname");
        exit(EXIT_FAILURE);
    }
    char currentIP[INET_ADDRSTRLEN];
    memset(currentIP, 0, INET_ADDRSTRLEN);
    struct in_addr ipAddr = (&currAddr)->sin_addr;
    if (inet_ntop(AF_INET, &ipAddr, currentIP, INET_ADDRSTRLEN) == NULL) {
        perror("error in inet_ntop");
        exit(EXIT_FAILURE);
    }
    printf(">>>This super peer's IP address is: %s\n", currentIP);

    // usa le pipe per ricevere il numero di porta
    int fdPport[2];
    if (pipe(fdPport) < 0) {
        perror("error in pipe");
        exit(EXIT_FAILURE);
    }

    // usa le pipe per inviare il proprio indirizzo IP
    int fdPip[2];
    if (pipe(fdPip) < 0) {
        perror("error in pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("error in fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        // chiude pipe in lettura per il numero di porta
        if (close(fdPport[0]) < 0) {
            perror("error in close");
            exit(EXIT_FAILURE);
        }
        // chiude la pipe in scrittura per l'indirizzo IP
        if (close(fdPip[1]) < 0) {
            perror("error in close");
            exit(EXIT_FAILURE);
        }
        childJob(fdPip[0], fdPport[1]); // si occupera' di rispondere alle richieste in arrivo
        return EXIT_SUCCESS;
    }

    // chiude pipe in scrittura per il numero di porta
    if (close(fdPport[1]) < 0) {
        perror("error in close");
        exit(EXIT_FAILURE);
    }

    // chiude la pipe in lettura per l'indirizzo IP
    if (close(fdPip[0]) < 0) {
        perror("error in close");
        exit(EXIT_FAILURE);
    }

    // blocca il processo fino a che non si ottiene il numero di porta
    if (readn(fdPport[0], &PORT_LISTENING, sizeof(int)) < 0) {
        perror("error in read");
        exit(EXIT_FAILURE);
    }

    // invia l'indirizzo IP al processo figlio
    if (writen(fdPip[1], currentIP, strlen(currentIP)) < 0) {
        perror("error in writeEE");
        exit(EXIT_FAILURE);
    }

    if (close(fdPport[0]) < 0) {
        perror("error in close");
    }
    if (close(fdPip[1]) < 0) {
        perror("error in close");
    }

    printf(">>>This super peer is listening on port: %d\n", PORT_LISTENING);
    printf(">>>Send a JOIN message\n");

    // legge le richieste da stdin
    char *buffer = NULL;
    int read;
    long unsigned int len;
    while ((read = getline(&buffer, &len, stdin)) > 0) {
        if (read == -1) {
            perror("error in getline");
            exit(EXIT_FAILURE);
        }
        int notFound = 1;
        int i = 0;
        while (notFound) {
            if (*(buffer + i) == '\n') {
                *(buffer + i) = '\0';
                notFound = 0;
            } else {
                i++;
            }
        }
        char *buf = strtok(buffer, " ");
        char *read_word;

        if (strcmp(buf, "JOIN") == 0) {
            // invio del messaggio di JOIN al BS
            handleJOIN(bootSockFd);
            printf(">>>JOIN completed\n");
        } else if (strcmp(buf, "WHOHAS") == 0) {
            // ricerca di un file, la richiesta viene effettuata solo interrogando gli altri SP
            // e' servita solo per delle prove
            // lettura del nome
            if ((read_word = strtok(NULL, " ")) != NULL) {
                int dim = 4096; 
                void *foundPA = mmap(NULL, dim, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (foundPA == MAP_FAILED) {
                    perror("error in mmap");
                }
                int pos = 0;
                sendWHOHASrequest(read_word, foundPA, &pos, currentIP);
                if (pos == 0) {
                    printf("The file you searched is not available\n");
                } else {
                    printf("The file you searched is on the following peers:\n");
                    printf("%s\n", (char *) foundPA);
                }
                if (munmap(foundPA, dim) < 0) {
                    perror("error in munmap");
                }
            } else {
                // non e' stato inserito il nome del file da cercare
                fprintf(stderr, "insert a filename\n");
            }
        } else if (strcmp(buf, "LEAVE") == 0) {
            // invio del messaggio di leave al BS
            if (isJoined) {
                handleLEAVE(bootSockFd);
            }
            break; // exit
        } else if (strcmp(buf, "whohas") == 0) {
            // ricerca di un file, e' diversa dalla precedente perche' prima di 
            // interrogare gli altri SP, cerca tra i file condivisi dai P associati
            read_word = strtok(NULL, " "); // lettura nome del file
            if (read_word != NULL) {
                handleWhoHasAsPeer(read_word, currentIP);
            } else {
                printf("insert a filename\n");
            }
        } else if (strcmp(buf, "update") == 0) {
            // aggiorna l'elenco dei propri file condivisi
            if (isJoined) {
                handleUpdateAsPeer(currentIP);
                printf(">>>update completed\n");
            } else {
                printf(">>>You need to send a JOIN message to the bootstrap server\n");
            }
        } else if (strcmp(buf, "get") == 0) {
            // richiesta di un file ad un P
            // lettura nome del file
            if (isJoined) { 
                if ((read_word = strtok(NULL, " ")) != NULL) {
                    char filename[256];
                    memset(filename, '\0', 256);
                    strcpy(filename, read_word);
                    char ipPeer[INET_ADDRSTRLEN];
                    memset(ipPeer, 0, INET_ADDRSTRLEN);
                    // lettura dell'ip del P da contattare
                    if ((read_word = strtok(NULL, ":")) != NULL) {
                        strcpy(ipPeer, read_word);
                        char *portPeer = malloc(PORT_DIM);
                        memset(portPeer, 0, PORT_DIM);
                        // lettura numero di porta del P da contattare
                        if ((read_word = strtok(NULL, " ")) != NULL) {
                            strcpy(portPeer, read_word);
                            printf(">>>wait while downloading data...\n");            
                            int res = sendGetRequest(filename, ipPeer, portPeer);
                            // dopo la ricezione di un file si aggiorna la lista di file condivisi
                            // potrebbe non essere stato possibile scaricare il file quindi la lista 
                            // non viene aggiornata
                            if (res) {
                                handleUpdateAsPeer(currentIP);
                                printf(">>>download completed\n");
                            }
                        } else {
                            fprintf(stderr, "missing argument, insert filename IP address:port number\n");
                        }
                        if (portPeer) {
                            free(portPeer);
                        }
                    } else {
                        fprintf(stderr, "missing argument, insert filename IP address:port number\n");
                    }
                } else {
                    // non e' stato inserito il nome del file da cercare
                    fprintf(stderr, "missing argument, insert filename IP address:port number\n");
                }
            } else {
                printf(">>>You need to send a JOIN message to the bootstrap server\n");
            }
        } else {
            printf("I can't understand your request\n");
        }
        
    }
    if (close(bootSockFd) < 0) {
        perror("error in close");
    }
    clearFile(AVAILABLESP);
    clearDir("data");
    free(buffer);
    kill(-1 * getpid(), SIGKILL); // serve a far terminare anche i processi figli che gestiscono le connessioni con i P
    return EXIT_SUCCESS;
}
