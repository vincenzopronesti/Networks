#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>

#include "peer.h"
#include "../util/util.h"

int superPSockFd = -1; // socket del SP a cui il P e' associato
// e' globale perche' deve essere modificato nel caso in cui 
// il SP decida di lasciare la rete

int PORT_LISTENING = -1; // porta impostata dal processo che si mette 
// in attesa di connessioni, deve essere nota anche al processo che invia 
// le richieste

int isJoined = 0; // flag per abilitare/disabilitare le richieste che il peer puo' inviare
// ad esempio per evitare che vengano fatti 2 join oppure il leave prima del join  

/**
    invia il messaggio query al BS per farsi inviare 
    l'elenco dei SP presenti
 */
void querySuperPeer()
{
    int sockfd;
    struct sockaddr_in servaddr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(BOOTSTRAP_SERVER_PORT);
    // bootstrap server should always have the same IP address 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  
    if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
        perror("error in connect, cannot connect to bootstrap server");
        exit(EXIT_FAILURE);
    }

    // invia la richiesta
    char *msg = "query\n";
    if (writen(sockfd, msg, strlen(msg)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }

    FILE *fp;
    fp = fopen(AVAILABLESP, "wb"); 
    if (NULL == fp) {
        printf("Error opening file");
        exit(EXIT_FAILURE);
    }
    // lettura del numero di byte che verranno inviati
    int received_int = 0;
    int sz = 0;

    int return_status = read(sockfd, &received_int, sizeof(received_int));
    if (return_status > 0) {
        sz = ntohl(received_int);
    } else {
        fprintf(stderr, "error while reading file dimension from socket\n");
        exit(EXIT_FAILURE);
    }
    // salvataggio dei dati
    int bytesReceived = 0;
    int bytesRead = 0;
    char recvBuff[256];
    memset(recvBuff, 0, 256);
    while (bytesReceived < sz) {
        int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
        bytesRead = readline(sockfd, recvBuff, toRead);
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
    close(sockfd);
}

/**
    superPSockFd: socket di connessione al proprio SP
    return: esito dell'operazione
        -1 errore
        0 ok    
    
    invia il messaggio di join e la lista dei file condivisi al SP 
    fornendogli il numero di porta che il P utilizzera' per 
    rispondere ai messaggi di get

    imposta il flag isJoined per impedire che si facciano piu' join
 */
int handleJoin(int superPSockFd)
{
    // costruzione del messaggio "join numeroDiPorta" da inviare al SP
    char *msg = malloc(MAXLINE);
    memset(msg, 0, MAXLINE);
    sprintf(msg, "join %d\n", PORT_LISTENING);

    if (writen(superPSockFd, msg, strlen(msg)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }
    // crea lista dei file
    makeSharedFilesList(MYSHAREDFILELIST);
    // invio lista
    int res = sendList(superPSockFd, MYSHAREDFILELIST);
    if (res < 0) {
        // interroga BS per sapere quali sono i SP disponibili e li salva su file
        // mostra l'elenco all'utente e prova a connettersi ad un nuovo SP
        fprintf(stderr, "It seems your super peer is down. Thus you've been disconnected from its subnet.\n");
        fprintf(stderr, "We're trying to reconnect to a super peer.\n");
        isJoined = 0;
        querySuperPeer();
        int c = showAvailableSP();
        superPSockFd = connectToASuperPeer(c);
        return -1; // error 
    }
    isJoined = 1;
    return 0;
}

/**
    superPSockFd: socket di connessione al proprio SP

    invia il messaggio di leave al SP, bisogna specificare 
    anche il numero di porta su cui ci si e' messi in ascolto 
    per poter essere identificati dal SP
 */
void handleLeave(int superPSockFd)
{
    // costruzione del messaggio leave numero porta
    char *msg = malloc(MAXLINE);
    memset(msg, 0, MAXLINE);
    sprintf(msg, "leave %d\n", PORT_LISTENING);

    if (writen(superPSockFd, msg, strlen(msg)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }
    isJoined = 0;
}

/**
    superPSockFd: socket di connessione al proprio SP
    fileToSearch: nome del file da cercare

    invia il messaggio whohas e il nome del file cercato al SP
    aspetta di ricevere un elenco di P che condividono il file cercato 
    per mostrarlo all'utente
 */
void handleWhoHas(int superPSockFd, char *fileToSearch)
{
    if (isJoined) {
        // costruisce il messaggio whohas filename
        char *msg = malloc(MAXLINE);
        memset(msg, 0, MAXLINE);
        sprintf(msg, "whohas %s\n", fileToSearch);

        if (writen(superPSockFd, msg, strlen(msg)) < 0) {
            perror("error in write");
            exit(EXIT_FAILURE);
        }
        // lettura del numero di byte che verranno inviati
        int received_int = 0;
        int sz = 0;

        int return_status = read(superPSockFd, &received_int, sizeof(received_int));
        if (return_status > 0) {
            sz = ntohl(received_int);
        } else {
            // interroga BS per sapere quali sono i SP disponibili e li salva su file
            // mostra l'elenco all'utente e prova a connettersi ad un nuovo SP
            fprintf(stderr, "error while reading file dimension from socket\n");
            fprintf(stderr, "It seems your super peer is down. Thus you've been disconnected from its subnet.\n");
            fprintf(stderr, "We're trying to reconnect to a super peer.\n");
            isJoined = 0;
            querySuperPeer();
            int c = showAvailableSP();
            superPSockFd = connectToASuperPeer(c);
            return;
        }

        if (sz == 0) {
            printf(">>>The file you searched isn't available in this network\n");
        } else {
            printf(">>>You can download the file \"%s\" from the following peers:\n", fileToSearch);
            int bytesReceived = 0;
            int bytesRead = 0;
            char recvBuff[256];
            memset(recvBuff, 0, 256);
            
            while (bytesReceived < sz) {
                int toRead = ((sz - bytesReceived > 256) ? (256) : (sz - bytesReceived + 1));
                bytesRead = readline(superPSockFd, recvBuff, toRead);
                fwrite(recvBuff, 1, bytesRead, stdout);
                bytesReceived += bytesRead;
            }
            if(bytesReceived < 0) {
                fprintf(stderr, "\n Read Error \n");
            }
        }
    } else {
        printf(">>>You need to send a join message to a super peer\n");
    }
}

/**
    superPSockFd: socket di connessione al proprio SP

    invia il messaggio update e la nuova lista di 
    file condivisi al proprio SP
 */
int handleUpdate(int superPSockFd)
{
    // costruisce il messaggio update numeroPorta
    char *msg = malloc(MAXLINE);
    memset(msg, 0, MAXLINE);
    sprintf(msg, "update %d\n", PORT_LISTENING);

    if (writen(superPSockFd, msg, strlen(msg)) < 0) {
        perror("error in write");
        exit(EXIT_FAILURE);
    }
    // crea lista dei file
    makeSharedFilesList(MYSHAREDFILELIST);
    // inviare lista
    int res = sendList(superPSockFd, MYSHAREDFILELIST);
    if (res < 0) {
        // interroga BS per sapere quali sono i SP disponibili e li salva su file
        // mostra l'elenco all'utente e prova a connettersi ad un nuovo SP
        fprintf(stderr, "It seems your super peer is down. Thus you've been disconnected from its subnet.\n");
        fprintf(stderr, "We're trying to reconnect to a super peer.\n");
        isJoined = 0;
        querySuperPeer();
        int c = showAvailableSP();
        superPSockFd = connectToASuperPeer(c);
        return -1; // error 
    }
    return 0; // ok
}

/**
    groupId: pid del processo padre

    quando viene ricevuto il messaggio che indica il leave del SP a cui si e' associati, 
    il processo figlio invia il segnale SIGUSR1 al processo padre (e' stato 
    installato un gestore tramite sigaction) che si occupa di 
    cercare un nuovo SP a cui associarsi (sara' poi 
    necessario effettuare nuovamente il join manualmente), 
    se non lo trova termina tutti i processi
 */
void handleLeaveSP(int groupId)
{
    kill(groupId, SIGUSR1);    
}

/**
    eseguito dal nuovo processo creato dopo l'accept
    legge il comando inviato ed esegue la relativa funzione
    la lettura avviene tramite readline quindi  
    si legge dalla socket finche' non si incontra il carattere '\n'
    (questo perche' la dimensione dei messaggi inviati dai SP e' variabile e 
    in questo modo non e' necessario conoscere a priori la dimensione del messaggio da 
    leggere)

    connfd: socket di connessione a SP/P
    peerAddr: struct relativa a chi effettua la richiesta
    groupId: pid del processo padre
 */
void processPeerRequests(int connfd, struct sockaddr *peerAddr, int groupId)
{
    int n;
    char line[MAXLINE];
    memset(line, '\0', MAXLINE);

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

    char *read_word = strtok(line, " ");
    if (strcmp(read_word, "get") == 0) {
        // il P ha ricevuto una richiesta di get da un altro P
        // lettura nome del file
        printf(">>>get request received\n");
        if ((read_word = strtok(NULL, " ")) != NULL) {
            replyGetRequest(connfd, (struct sockaddr *) peerAddr, read_word, MYSHAREDFILELIST);
            printf(">>>get completed\n");
        } else {
            // non e' stato inserito il nome del file da cercare
            fprintf(stderr, "insert a filename\n");
        }
    } else if (strcmp(read_word, "leaveSP") == 0) {
        // il SP a cui si e' associati sta lasciando la rete
        printf(">>>leaveSP received\n");
        handleLeaveSP(groupId);
    } else {
        printf("I can't understand your request: %s\n", line);
    }
}

/**
    readingPipeFd: pipe utilizzata per mandare al processo padre il numero 
        di porta su cui ci si e' messi in ascolto

    server ricorsivo che si mette in ascolto di connessioni e genera un 
    nuovo processo per servirle
    il numero di porta scelto dal server deve essere noto al processo padre 
    perche' questo possa inviarlo al proprio SP
 */
void childJobPeer(int writingPipeFdport)
{
    int groupId = getppid();

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

    // recupero del numero di porta del socket che e' in ascolto di connessioni di altri P,
    // il numero di porta deve essere inviato al SP tramite pipe
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
            processPeerRequests(connfd, (struct sockaddr *) &peerAddr, groupId);
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
    c: numero di SP disponibili
    return: file descriptor della socket

    prova a scegliere un SP a caso dalla lista, se 
    la connessione non dovesse riuscire allora si scansiona l'intero file e 
    ci si collega al il primo SP disponibile    
 */
int connectToASuperPeer(int c)
{
    FILE *fp = fopen(AVAILABLESP, "r");
    if (fp == NULL) {
        fprintf(stderr, "error cannot find super peer\n");
        exit(EXIT_FAILURE);
    }

    int foundSP = 0;
    char *line = NULL;
    size_t dim = 0;
    ssize_t read;
    // scelta casuale di un SP
    srand(time(NULL));
    int i = rand() % c; // numero scelto a caso tra 0 e c
    int j = 0;
    while (j <= i) {
        read = getline(&line, &dim, fp);
        j++;
        *(line + strlen(line) - 1) = '\0';
    }

    struct sockaddr_in peeraddr;
    if ((superPSockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }
    // ip e porta del SP scelto a caso a cui ci si vuole connettere
    char *ipSP = strtok(line, ":");
    char *portSPchar = strtok(NULL, ":");
    int portSP = atoi(portSPchar);
    memset((void *)&peeraddr, 0, sizeof(peeraddr));
    peeraddr.sin_family = AF_INET;
    peeraddr.sin_port = htons(portSP);
    if (inet_pton(AF_INET, ipSP, &peeraddr.sin_addr) <= 0) {
        fprintf(stderr, "error in inet_pton for %s", ipSP);
        exit(EXIT_FAILURE);
    }    

    if (connect(superPSockFd, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
        perror("error in connect");
        fprintf(stderr, "cannot connect to super peer %s:%s\n", ipSP, portSPchar);
    } else {
        printf(">>>Connected to super peer %s:%s\n", ipSP, portSPchar);
        printf(">>>Send a join message\n");
        foundSP = 1;
    }
    // se la connessione non e' riuscita, ci si collega al primo SP disponibile
    if (!foundSP) {
        line = NULL;
        dim = 0;
        read = 0;
        fseek(fp, 0, SEEK_SET);

        while (!foundSP && (read = getline(&line, &dim, fp)) != -1) {
            *(line + strlen(line) - 1) = '\0';
            struct sockaddr_in peeraddr;
            if ((superPSockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                perror("error in socket");
                exit(EXIT_FAILURE);
            }
            // ip e porta del SP a cui ci si vuole connettere
            char *ipSP = strtok(line, ":");
            char *portSPchar = strtok(NULL, ":");
            int portSP = atoi(portSPchar);
            memset((void *)&peeraddr, 0, sizeof(peeraddr));
            peeraddr.sin_family = AF_INET;
            peeraddr.sin_port = htons(portSP);
            if (inet_pton(AF_INET, ipSP, &peeraddr.sin_addr) <= 0) {
                fprintf(stderr, "error in inet_pton for %s", ipSP);
                exit(EXIT_FAILURE);
            }    

            if (connect(superPSockFd, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
                perror("error in connect");
                fprintf(stderr, ">>>Cannot connect to super peer %s:%s\n", ipSP, portSPchar);
            } else {
                printf(">>>Connected to super peer %s:%s\n", ipSP, portSPchar);
                printf(">>>Send a join message\n");
                foundSP = 1;
            }
        }
    }
    if (line) {
        free(line);
    }
    if (!foundSP) {
        fprintf(stderr, "cannot connect to any super peer\n");
        exit(EXIT_FAILURE);
    }
    if (fclose(fp) < 0) {
        perror("error in fclose");
    }
    return superPSockFd;
}

/**
    return: numero di SP disponibili

    stampa a schermo i SP disponibili 
    se BS non ha fornito nessun SP allora l'applicazione viene 
    terminata
 */
int showAvailableSP()
{
    FILE *fp = fopen(AVAILABLESP, "r");
    if (fp == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }

    printf(">>>List of all the available super peer:\n");
    char *line = NULL;
    size_t dim = 0;
    ssize_t read;
    int c = 0;
    while ((read = getline(&line, &dim, fp)) != -1) {
        printf("\t%s", line);
        c++;
    }
    if (line) {
        free(line);
    }
    if (c == 0) {
        fprintf(stderr, "\n>>>No super peer available\n");
        exit(EXIT_FAILURE);
    }
    if (fclose(fp) < 0) {
        perror("error in fclose");
    }
    return c; // numero di super peer
}

/**
    return: socket connessa al nuovo SP

    viene chiamata quando il SP a cui si e' associati lascia la rete 
    e' diversa da connectToASuperPeer perche' deve controllare che non ci si colleghi 
    allo stesso SP
 */
int reconnectToASuperPeer()
{
    FILE *fp = fopen(AVAILABLESP, "r");
    if (fp == NULL) {
        fprintf(stderr, "error cannot find super peer\n");
        exit(EXIT_FAILURE);
    }

    int foundSP = 0;
    char *line = NULL;
    size_t dim = 0;
    ssize_t read;
    
    // bisogna conoscere l'ip ed il numero di porta a cui la socket e' connessa per evitare di 
    // collegarsi allo stesso SP
    struct sockaddr_in currAddr;
    memset((void *)&currAddr, 0, sizeof(currAddr));
    socklen_t lenSock = sizeof(currAddr);
    if (getpeername(superPSockFd, (struct sockaddr *) &currAddr, &lenSock) < 0) {
        perror("error in getpeername");
        exit(EXIT_FAILURE);
    }
    
    char currentSPIP[INET_ADDRSTRLEN];
    memset(currentSPIP, 0, INET_ADDRSTRLEN);
    struct in_addr spIpAddr = (&currAddr)->sin_addr;
    if (inet_ntop(AF_INET, &spIpAddr, currentSPIP, INET_ADDRSTRLEN) == NULL) {
        perror("error in inet_ntop");
        exit(EXIT_FAILURE);
    }

    int currentSPportInt = ntohs(currAddr.sin_port);
    char *currentSPport = malloc(MAXLINE);
    memset(currentSPport, '\0', MAXLINE);
    sprintf(currentSPport, "%d", currentSPportInt);

    // crea una stringa che contiene la coppia IP:PORTA del SP attuale per poterla confrontare 
    // con quelle dei SP disponibili
    char *path = malloc(MAXLINE);
    memset(path, '\0', MAXLINE);
    strcat(path, currentSPIP);
    strcat(path, ":");
    strcat(path, currentSPport);

    if (close(superPSockFd) < 0) {
        perror("error in close");
    }

    if ((superPSockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }
    // scorre l'elenco dei SP
    while (!foundSP && (read = getline(&line, &dim, fp)) != -1) {
        *(line + strlen(line) - 1) = '\0';
        if (strcmp(line, path) == 0) {
            // la coppia letta e' quella del SP che vuole uscire
            continue;
        }
        struct sockaddr_in peeraddr;
        // lettura di ip e porta a cui collegarsi
        char *ipSP = strtok(line, ":");
        char *portSPchar = strtok(NULL, ":");
        int portSP = atoi(portSPchar);

        memset((void *)&peeraddr, 0, sizeof(peeraddr));
        peeraddr.sin_family = AF_INET;
        peeraddr.sin_port = htons(portSP);
        if (inet_pton(AF_INET, ipSP, &peeraddr.sin_addr) <= 0) {
            fprintf(stderr, "error in inet_pton for %s", ipSP);
            exit(EXIT_FAILURE);
        }    

        if (connect(superPSockFd, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
            perror("error in connect");
            fprintf(stderr, ">>>cannot connect to super peer %s:%s\n", ipSP, portSPchar);
        } else {
            printf(">>>Connected to super peer %s:%s\n", ipSP, portSPchar);
            printf(">>>Send a join message\n");
            foundSP = 1;
        }
    }
    if (line) {
        free(line);
    }
    if (currentSPport) {
        free(currentSPport);
    }
    if (path) {
        free(path);
    }

    if (!foundSP) {
        fprintf(stderr, ">>>Cannot connect to any super peer\n");
        clearFile(AVAILABLESP);
        clearFile(MYSHAREDFILELIST);
        kill(-1 * getpid(), SIGKILL); // termina tutti i processi
        exit(EXIT_FAILURE);
    }

    if (fclose(fp) < 0) {
        perror("error in fclose");
    }
    return superPSockFd;
}

/**
    gestore attivato alla ricezione del messaggio leaveSP
    
    sigN: id del segnale, di fatto e' sempre SIGUSR1
    
    effettua nuovamente la query al BS
    prova a connettersi ad un nuovo SP
 */
void leaveSPHandler(int sigN)
{
    isJoined = 0;
    querySuperPeer();
    superPSockFd = reconnectToASuperPeer();
}

int main(void)
{
    // installa il gestore per SIGUSR1
    struct sigaction sig1;
    sig1.sa_handler = leaveSPHandler;
    sig1.sa_flags = SA_RESTART;
    if (sigaction(SIGUSR1, &sig1, NULL) < 0) {
        perror("error in sigaction");
        exit(EXIT_FAILURE);
    }
    // disattiva il gestore di dafault per SIGPIPE, 
    // in quanto comporta la terminazione del processo che lo riceve 
    // invece qui e' necessario poter leggere il valore 
    // di errno dopo le scritture su socket per accorgesri dell'errore
    struct sigaction sig2;
    sig2.sa_handler = SIG_IGN; // ignora il segnale
    if (sigaction(SIGPIPE, &sig2, NULL) < 0) {
        perror("error in sigaction");
        exit(EXIT_FAILURE);
    }

    // interroga BS per conoscere i SP disponibili e li salva su file
    querySuperPeer();
    // stampa a schermo i SP disponibili
    int c = showAvailableSP();
    superPSockFd = connectToASuperPeer(c);
    
    if (superPSockFd == -1) {
        fprintf(stderr, "error during connection to a super peer\n");
        exit(EXIT_FAILURE);
    }

    // usa le pipe per comunicare il numero di porta
    int fdP[2];
    if (pipe(fdP) < 0) {
        perror("error in pipe");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("error in fork");
        exit(EXIT_FAILURE);
    }
    if (pid == 0) {
        // chiude la pipe in lettura
        if (close(fdP[0]) < 0) {
            perror("error in close");
            exit(EXIT_FAILURE);
        }
        childJobPeer(fdP[1]); // risponde alle richieste in arrivo
        return EXIT_SUCCESS;
    }

    // chiude la pipe in scrittura
    if (close(fdP[1]) < 0) {
        perror("error in close");
    }
    // blocca il processo fino a che non si ottiene il numero di porta
    if (readn(fdP[0], &PORT_LISTENING, sizeof(int)) < 0) {
        perror("error in read");
        exit(EXIT_FAILURE);
    }
    if (close(fdP[0]) < 0) {
        perror("error in close");
    }

    printf(">>>This peer is listening on port: %d\n", PORT_LISTENING);

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
        
        if (strcmp(buf, "query") == 0) {
            // query al BS per ottenere la lista dei SP
            querySuperPeer();
            printf(">>>query completed\n");
        } else if (strcmp(buf, "get") == 0) {
            // richiesta di un file ad un P
            if (isJoined) {
                // lettura nome del file
                if ((read_word = strtok(NULL, " ")) != NULL) {
                    char filename[256];
                    memset(filename, '\0', 256);
                    strcpy(filename, read_word);
                    // lettura ip del P a cui connettersi
                    char ipPeer[INET_ADDRSTRLEN];
                    memset(ipPeer, 0, INET_ADDRSTRLEN);
                    read_word = strtok(NULL, ":");
                    if (read_word != NULL) {
                        strcpy(ipPeer, read_word);
                        // lettura porta del P a cui connettersi
                        char *portPeer = malloc(PORT_DIM);
                        memset(portPeer, 0, PORT_DIM);
                        read_word = strtok(NULL, " ");
                        if (read_word != NULL) {
                            strcpy(portPeer, read_word);
                            printf(">>>wait while downloading data...\n");       
                            int res = sendGetRequest(filename, ipPeer, portPeer);
                            // dopo la ricezione di un file si comunica al SP la nuova 
                            // lista di file condivisi, se questa e' cambiata
                            if (res) {
                                handleUpdate(superPSockFd);
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
                printf(">>>You need to send a join message to a super peer\n");
            }
        } else if (strcmp(buf, "join") == 0) {
            // join nella sottorete di un SP
            if (!isJoined) {
                int res = handleJoin(superPSockFd);
                if (res != -1) 
                    printf(">>>join completed\n");
            } else {
                printf(">>>You've already joined a super peer\n");
            }
            
        } else if (strcmp(buf, "leave") == 0) {
            // leave dalla sottorete del SP
            if (isJoined) {
                handleLeave(superPSockFd);
            }
            // non e' stato effettuato il join al 
            // super peer quindi il leave equivale a chiudere 
            // l'applicazione 
            break;
        } else if (strcmp(buf, "whohas") == 0) {
            // ricerca di un file 
            // lettura nome del file
            if ((read_word = strtok(NULL, " ")) != NULL) {
                handleWhoHas(superPSockFd, read_word);
            } else {
                // non e' stato inserito il nome del file da cercare
                fprintf(stderr, "insert a filename\n");
            }
        } else if (strcmp(buf, "update") == 0) {
            // aggiornamento della lista dei file condivisi
            if (isJoined) {
                int res = handleUpdate(superPSockFd);
                if (res == 0)
                    printf(">>>update completed\n");
            } else {
                printf(">>>You need to send a join message to a super peer\n");
            }
        } else {
            printf("I can't understand your request\n");
        }
    }
    if (close(superPSockFd) < 0) {
        perror("error in close");
    }
    free(buffer);
    clearFile(AVAILABLESP);
    clearFile(MYSHAREDFILELIST);
    kill(-1 * getpid(), SIGKILL);
    return EXIT_SUCCESS;
}
