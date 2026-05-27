#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <pthread.h>

#define PORT 2024
#define DIM_MAX 4096
int socket_catre_server = 0;
volatile int secunde_refresh = 0;
pthread_mutex_t lacat_socket = PTHREAD_MUTEX_INITIALIZER;

void afiseaza_ajutor() {
    printf("\nComenzi disponibile:\n");
    printf("  login : <nume_utilizator>\n");
    printf("  set-route : <ip_destinatie>\n");
    printf("  start-trace\n");
    printf("  stop-trace\n");
    printf("  get-route-status\n");
    printf("  config : <secunde>\n");
    printf("  quit\n");
}

void *thread_monitorizare(void *arg) {
    while(1) {
        if (secunde_refresh == 0) {
            sleep(1);
            continue;
        }
        sleep(secunde_refresh);
        if (secunde_refresh == 0) continue;
        pthread_mutex_lock(&lacat_socket);
        write(socket_catre_server, "get-route-status", strlen("get-route-status"));
        int dimensiune = 0;
        if (read(socket_catre_server, &dimensiune, sizeof(int)) > 0) {
             if (dimensiune > 0) {
                 char *raspuns_server = malloc(dimensiune + 1);
                 int total_citit = 0;
                 while(total_citit < dimensiune) {
                     int citit_acum = read(socket_catre_server, raspuns_server + total_citit, dimensiune - total_citit);
                     if(citit_acum <= 0) break;
                     total_citit += citit_acum;
                 }
                 raspuns_server[dimensiune] = 0;
                 system("clear");
                 printf("Monitorizare pornita la %d s\n", secunde_refresh);
                 printf("%s", raspuns_server);
                 printf("\n(Poti scrie comenzi!)\n> ");
                 fflush(stdout);
                 free(raspuns_server);
             }
        }
        pthread_mutex_unlock(&lacat_socket);
    }
    return NULL;
}
int main(int argc, char const *argv[]) {
    struct sockaddr_in serv_addr;
    char ip_server[INET_ADDRSTRLEN];

    if (argc < 2) strcpy(ip_server, "127.0.0.1");
    else strncpy(ip_server, argv[1], INET_ADDRSTRLEN);

    if ((socket_catre_server = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Eroare socket"); return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    if(inet_pton(AF_INET, ip_server, &serv_addr.sin_addr) <= 0) return -1;

    if (connect(socket_catre_server, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Conexiune esuata la %s\n", ip_server); return -1;
    }
    printf("Conectat la NetDiag! Scrie 'help' pentru comenzi.\n");
    pthread_t id_thread;
    pthread_create(&id_thread, NULL, thread_monitorizare, NULL);
    char linie[DIM_MAX];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(linie, sizeof(linie), stdin)) break;
        if (strlen(linie) == 0) continue;
        if (strncmp(linie, "help", 4) == 0)
            afiseaza_ajutor();
        if (strncmp(linie, "config :", 8) == 0) {
            int sec = 0;
            if (sscanf(linie + 8, "%d", &sec) == 1 && sec > 0) {
                secunde_refresh = sec;
                printf("Monitorizare pornita!\n");
            }
        }
        else if (strncmp(linie, "stop-trace", 10) == 0) {
            secunde_refresh = 0;
            printf("Monitorizare oprita.\n");
        }
        pthread_mutex_lock(&lacat_socket);

        if (write(socket_catre_server, linie, strlen(linie)) < 0) break;
        if (strncmp(linie, "quit", 4) == 0) break;
        int dimensiune = 0;
        if (read(socket_catre_server, &dimensiune, sizeof(int)) <= 0) {
            printf("\nServer deconectat.\n");
            exit(0);
        }
        if (dimensiune > 0) {
            char *raspuns_server = malloc(dimensiune + 1);
            int total_citit = 0;
            while(total_citit < dimensiune) {
                int citit_acum = read(socket_catre_server, raspuns_server + total_citit, dimensiune - total_citit);
                if(citit_acum <= 0) break;
                total_citit += citit_acum;
            }
            raspuns_server[dimensiune] = 0;
            printf("%s", raspuns_server);
            free(raspuns_server);
        }
        pthread_mutex_unlock(&lacat_socket);
    }
    close(socket_catre_server);
    return 0;
}
