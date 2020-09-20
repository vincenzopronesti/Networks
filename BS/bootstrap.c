#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "bootstrap.h"
#include "../util/util.h"

/**
    serve a gestire il JOIN di un nuovo SP nella rete

    connfd: socket di connessione con il SP
    peerAddr: struct relativa al SP, serve per estrarre l'IP
    port: numero di porta inviato dal SP

    salva l'indirizzo IP ed il numero di porta nella lista dei SP 
    e risponde al SP che ha fatto la richiesta con l'elenco dei SP
    attualmente disponibili (la coppia IP:porta del SP che ha 
    effettuato la richiesta e' inclusa nell'elenco)
    infine, invia agli altri SP la nuova versione dell'elenco dei SP 
 */
void handleJOIN(int connfd, struct sockaddr *peerAddr, char *port)
{
    // apertura file con l'elenco dei SP
    FILE *fp;
    if ((fp = fopen(LIST, "a")) == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }

    // recupera ip del SP
    struct in_addr ipAddr = ((struct sockaddr_in *) peerAddr)->sin_addr;
    char strIP[INET_ADDRSTRLEN];
    memset(strIP, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipAddr, strIP, INET_ADDRSTRLEN);

    // aggiunge la coppia IP:porta all'elenco dei SP
    char *currIP = malloc(MAXLINE);
    memset(currIP, 0, MAXLINE);
    sprintf(currIP, "%s:%s", strIP, port);
    fprintf(fp, "%s\n", currIP);

    
    if (fclose(fp) != 0) {
        perror("error in fclose");
        exit(EXIT_FAILURE);
    }
    // invio degli indirizzi
    sendList(connfd, LIST);

    // fa aggiornare agli altri SP la lista dei SP disponibili
    updateSP(1, currIP); // i parametri servono per non inviare due volte 
    // lo stesso elenco al SP che ha fatto il JOIN
    if (currIP) {
        free(currIP);
    }
}

/**
    rimozione dell'indirizzo IP e della porta dall'elenco dei SP

    peerAddr: struct relativa al SP, serve per estrarre l'IP
    port: numero di porta inviato dal SP

    crea un file temporaneo in cui copia tutte le coppie IP:numero di porta 
    tranne quella da rimuovere, dopo rinomina il file
    infine, invia a tutti i SP la nuova versione dell'elenco dei SP 
 */
void handleLEAVE(struct sockaddr *peerAddr, char *port)
{
    FILE *fp;
    if ((fp = fopen(LIST, "r")) == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }
    // file temporaneo in cui vengono copiati 
    // tutti gli indirizzi tranne quello da 
    // cancellare
    FILE *tmp;
    if ((tmp = fopen("tmp", "w")) == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }

    // recupera ip del SP
    struct in_addr ipAddr = ((struct sockaddr_in *) peerAddr)->sin_addr;
    char strIP[INET_ADDRSTRLEN];
    memset(strIP, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &ipAddr, strIP, INET_ADDRSTRLEN);
    // costruisce la coppia ip:porta
    char *ipSP = malloc(MAXLINE);
    memset(ipSP, 0, MAXLINE);
    sprintf(ipSP, "%s:%s", strIP, port);

    // legge le righe del file una alla volta, e copia tutto tranne il SP da rimuovere
    char line [MAXLINE];
    memset(line, '\0', MAXLINE);
    while (fgets(line, sizeof(line), fp) != NULL) {
        *(line + strlen(line) - 1) = '\0';
        if (strcmp(line, ipSP) != 0) {
            fprintf(tmp, "%s\n", line);
        }
    }

    if (fclose(fp) != 0) {
        perror("error in fclose");
        exit(EXIT_FAILURE);
    }
    if (fclose(tmp) != 0 ) {
        perror("error in fclose");
        exit(EXIT_FAILURE);
    }
    // sostituzione del file temporaneo con quello aggiornato
    if(rename("tmp" , LIST) < 0) {
        perror("error in rename");
    }
    if (ipSP) {
        free(ipSP);
    }
    // fa aggiornare agli altri SP la lista dei SP disponibili
    updateSP(0, NULL); // i parametri indicano che 
    // non bisogna escludere alcun indirizzo dall'elenco
}

/**
    viene invocata dopo il JOIN o il LEAVE di un SP per notificare 
    gli altri SP del cambiamento nell'elenco dei SP disponibili

    checkJoin: 
        vale 1 se updateSP e' stata chiamata da handleJOIN
        vale 0 se updateSP e' stata chiamata da handleLEAVE
    ipJoined: indica la coppia "IP:numeroDiPorta" relativa al 
        SP che ha effettuato il JOIN, vale NULL se updateSP e' stata 
        chiamata da handleLEAVE

    apre il file dei SP e per ciascuna coppia IP:numeroDiPorta che 
    trova nel file, ne invia il contenuto
    se updateSP viene invocata con checkJoin = 1, allora l'elenco dei SP 
    non viene inviato al SP identificato da ipJoined 
 */
void updateSP(int checkJoin, char *ipJoined)
{
    // apertura del file con la lista dei SP
    FILE *fp = fopen(LIST, "r");
    if (fp == NULL) {
        perror("error in fopen");
        exit(EXIT_FAILURE);
    }
    fseek(fp, 0, SEEK_END);
    int sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    

    char *line = NULL;
    size_t dim = 0;
    ssize_t read;
    // scansione dell'elenco dei SP
    while ((read = getline(&line, &dim, fp)) != -1) {
        *(line + strlen(line) - 1) = '\0';
        // evita di inviare updateSP a chi ha appena fatto il JOIN
        if (checkJoin) { // se updateSP e' stato chiamato da handleLEAVE e' inutile fare il confronto
            if (ipJoined != NULL)
                if (strcmp(line, ipJoined) == 0)
                    continue;
        }        
        char *ip = strtok(line, ":"); // IP del SP da contattare
        char *portS = strtok(NULL, ":");
        int port = atoi(portS); // porta del SP da contattare
        
        // creazione della socket che sara' utilizzata 
        // per contattare l'i-esimo SP dell'elenco
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("error in socket");
            exit(EXIT_FAILURE);
        }
        // inserimento di ip e numero di porta del SP da contattare
        struct sockaddr_in peeraddr;
        memset((void *)&peeraddr, 0, sizeof(peeraddr));
        peeraddr.sin_family = AF_INET;
        peeraddr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip, &peeraddr.sin_addr) <= 0) {
            fprintf(stderr, "error in inet_pton for %s", ip);
            exit(EXIT_FAILURE);
        }    

        if (connect(sock, (struct sockaddr *) &peeraddr, sizeof(peeraddr)) < 0) {
            fprintf(stderr, "error in connect to super peer %s:%s\n", ip, portS);
            perror("error in connect");
        } else {
            char *msg = "updateSP\n";
            if (writen(sock, msg, strlen(msg)) < 0) {
                perror("error in write");
                exit(EXIT_FAILURE);
            }

            // invio della dimensione del file
            int tmp = htonl(sz);
            if (writen(sock, &tmp, sizeof(tmp)) < 0) {
                perror("error in write");
            }
            // evita di dover gestire il puntatore della lettura con fseek
            FILE *f = fopen(LIST, "r");
            if (f == NULL) {
                perror("error in fopen");
                exit(EXIT_FAILURE);
            }

            int sentBytes = 0;
            while (sentBytes < sz) {
                // legge il file in porzioni da 256 bytes
                unsigned char buff[256] = {0};
                int toRead = ((sz - sentBytes > 256) ? (256) : (sz - sentBytes));
                int nread = fread(buff, 1, toRead, f);        

                // se la lettura ha successo si puo' inviare
                if (nread > 0) {
                    if (writen(sock, buff, nread) < 0) {
                        perror("error in write");
                        exit(EXIT_FAILURE);
                    }
                    sentBytes += nread;
                }

                if (nread < 256) {
                    if (feof(f))
                        fprintf(stderr, "End of file\n");
                    if (ferror(f))
                        fprintf(stderr, "Error reading\n");
                    break;
                }
            }
            if (fclose(f) < 0) {
                perror("error in fclose");
            }
        }

        if (close(sock) < 0) {
            perror("error in close");
            exit(EXIT_FAILURE);
        }

    }
    if (line) {
        free(line);
    }
    if (fclose(fp) < 0) {
        perror("error in close");
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

    connfd: socket di connessione con SP oppure P
    peerAddr: struct relativa al SP/P che ha effettuato la richiesta
        nel caso dei SP serve per estrarre l'indirizzo IP

    nel caso si sia connesso un SP (JOIN), il processo termina con il LEAVE del SP
    nel caso si sia connesso un P, il processo termina dopo aver risposto 
        alla richiesta di query
 */
void processRequests(int connfd, struct sockaddr *peerAddr)
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

        if (strcmp(buffer, "JOIN") == 0){ 
            // inviato da SP
            // invio della risposta con l'insieme dei SP presenti
            // aggiunta dell'indirizzo IP alla lista dei SP
            printf(">>>JOIN request received\n");
            char *read_port = strtok(NULL, " ");
            if (read_port != NULL) {
                handleJOIN(connfd, (struct sockaddr *) peerAddr, read_port);
            } else {
                fprintf(stderr, "cannot serve JOIN request, port number for a listening socket is required\n");
            }
        } else if (strcmp(buffer, "LEAVE") == 0) { 
            // inviato da SP
            // rimozione dell'indirizzo IP dalla lista dei SP
            printf(">>>LEAVE request received\n");
            char *read_port = strtok(NULL, " ");
            if (read_port != NULL) {
                handleLEAVE((struct sockaddr *) peerAddr, read_port);    
                break;
            } else {
                fprintf(stderr, "cannot serve LEAVE request, port number for a listening socket is required\n");
            }
        } else if (strcmp(buffer, "query") == 0) { 
            // inviato da P, query da SP e' implicito in join
            // invio dell'elenco dei SP senza salvare l'indirizzo IP
            printf(">>>query request received\n");
            sendList(connfd, LIST);
            break;
        } else {
            printf(">>>bad request\n");
        }
    }
}

int main(void)
{
    int listenfd = 0;
    int connfd = 0;
    struct sockaddr_in serv_addr;
 
    clearFile(LIST);

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("error in socket");
        exit(EXIT_FAILURE);
    }
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // bootstrap server should always have the same IP address 
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(BOOTSTRAP_SERVER_PORT);

    if (bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("error in bind");
        exit(EXIT_FAILURE);
    }

    if (listen(listenfd, BACKLOG) == -1) {
        perror("Failed to listen");
        exit(EXIT_FAILURE);
    }

    // crea un nuovo processo per gestire le connessioni in arrivo
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
            processRequests(connfd, (struct sockaddr *) &peerAddr);
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
    return EXIT_SUCCESS;
}
