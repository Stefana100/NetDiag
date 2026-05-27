#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define PORT 2024
#define DIM_MAX 8192
#define FISIER_STARE_RUTA "/tmp/netdiag_status.txt"
#define MAX_SALTURI 30
#define TIMEOUT_SECUNDE 1
#define PACHETE_PE_HOP 3
#define FISIER_CONFIG_IP "netdiag_target.cfg"
#define FISIER_PID_TRACE "netdiag_pid.cfg"

volatile pid_t pid_trace = -1;
char tinta_trace[256] = "";

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0
#define ICMP_TIME_EXCEEDED 11
#define ICMP_DEST_UNREACH 3

unsigned short in_cksum(unsigned short *adresa, int lungime) {
    int suma = 0;
    unsigned short raspuns = 0;
    unsigned short *w = adresa;
    int elemente_ramase = lungime;

    while (elemente_ramase > 1) {
        suma += *w++;
        elemente_ramase -= 2;
    }

    if (elemente_ramase == 1) {
        *(unsigned char *) &raspuns = *(unsigned char *) w;
        suma += raspuns;
    }

    while (suma >> 16) {
        suma = (suma & 0xFFFF) + (suma >> 16);
    }

    raspuns = (unsigned short)(0xFFFF - suma);
    return raspuns;
}

double calculeaza_diferenta_timp_ms(struct timeval *timp_final, struct timeval *timp_initial) {
    long secunde = timp_final->tv_sec - timp_initial->tv_sec;
    long microsecunde = timp_final->tv_usec - timp_initial->tv_usec;
    if (microsecunde < 0) {
        secunde--;
        microsecunde += 1000000;
    }
    return (double)secunde * 1000.0 + (double)microsecunde / 1000.0;
}

void raspunde_clientului(int socket_client, const char *mesaj) {
    int lungime = strlen(mesaj);
    write(socket_client, &lungime, sizeof lungime);
    write(socket_client, mesaj, lungime);
}

int verifica_utilizator(const char *nume) {
    FILE *fis = fopen("users.conf", "r");
    if (!fis) return 0;
    char linie[256];
    while (fgets(linie, sizeof linie, fis)) {
        int len = strlen(linie);
        if(linie[len-1]=='\n')
            linie[len-1]='\0';
        if(strcmp(linie,nume)==0)
        { fclose(fis);
          return 1;
        }
    }
    fclose(fis);
    return 0;
}

int comanda_valida(const char *comanda_intreaga) {
    FILE *fis = fopen("comenzi.conf", "r");
    if (!fis) return 0;
    char linie[256];
    char comanda_principala[256];
    int i=0;
    while(comanda_intreaga[i]!=' '&& comanda_intreaga[i]!=':'&& comanda_intreaga[i]!='\0'){
        comanda_principala[i]=comanda_intreaga[i];
        i++;
    }
    comanda_principala[i]='\0';
    int ok = 0;
    while (fgets(linie, sizeof linie, fis)) {
        int len=strlen(linie);
        if(linie[len-1]=='\n')
            linie[len-1]='\0';
        if (strcmp(linie, comanda_principala) == 0) {
            ok = 1;
            break;
        }
    }

    fclose(fis);
    return ok;
}


void executa_comanda_login(int canal, const char *nume) {
    const char *mesaj_succes = "Login realizat cu succes!🎉😁\n";
    const char *mesaj_esec = "Frumoasa incercare,utilizator incorect🤭\n";

    if (verifica_utilizator(nume)) {
        write(canal, mesaj_succes, strlen(mesaj_succes));
    } else {
        write(canal, mesaj_esec, strlen(mesaj_esec));
    }
}

void executa_comanda_set_route(int canal, const char *ip_tinta) {
    struct in_addr adresa_ip;
    if (inet_pton(AF_INET, ip_tinta, &adresa_ip) == 1) {
        FILE *f = fopen(FISIER_CONFIG_IP, "w");
        if (f) {
            fprintf(f, "%s", ip_tinta);
            fclose(f);
            strncpy(tinta_trace, ip_tinta, sizeof(tinta_trace) - 1);
            dprintf(canal, "ruta setata cu succes catre %s.\n", ip_tinta);
        } else {
            dprintf(canal, "eroare interna: nu pot salva configuratia.\n");
        }
    } else {
        dprintf(canal, "adresa ip '%s' nu pare corecta.\n", ip_tinta);
    }
}

void executa_comanda_config(int canal, const char *params) {
    int valoare_interval = 0;
    if (sscanf(params, "%d", &valoare_interval) == 1 && valoare_interval > 0) {
        dprintf(canal, "Client configurat la %d secunde. (Serverul scaneaza constant la %d s)\n", valoare_interval);
    } else {
        dprintf(canal, "Format gresit!\n");
    }
}

void monitor_traceroute_loop() {
    FILE *f_ip = fopen(FISIER_CONFIG_IP, "r");
    if (f_ip) {
        if (fgets(tinta_trace, sizeof(tinta_trace), f_ip)) {
            tinta_trace[strcspn(tinta_trace, "\n")] = 0;
        }
        fclose(f_ip);
    }

    struct sockaddr_in adresa_destinatie;
    int socket_icmp;

    memset(&adresa_destinatie, 0, sizeof(adresa_destinatie));
    adresa_destinatie.sin_family = AF_INET;
    if (inet_pton(AF_INET, tinta_trace, &adresa_destinatie.sin_addr) <= 0) _exit(1);

    socket_icmp = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socket_icmp < 0) _exit(1);

    struct timeval tv_timeout = { .tv_sec = TIMEOUT_SECUNDE, .tv_usec = 0 };
    setsockopt(socket_icmp, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_timeout, sizeof(tv_timeout));

    while (1) {
        char linie_status[256];
        int ttl = 1;
        int max_salturi = MAX_SALTURI;
        int finalizat = 0;

        FILE *f_start = fopen(FISIER_STARE_RUTA, "w");
        if (f_start) {
            fprintf(f_start, "Monitorizare pornita catre %s ---\n", tinta_trace);
            fclose(f_start);
        }

        for (ttl = 1; ttl <= max_salturi; ttl++) {
            struct timeval timp_start, timp_sfarsit;
            char pachet_primit[DIM_MAX];
            int lungime_primita;
            double suma_rtt = 0.0;
            int pachete_succes = 0;
            char ip_hop[INET_ADDRSTRLEN] = "*";
            int hop_raspunde = 0;

            if (setsockopt(socket_icmp, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) break;

            for (int i = 0; i < PACHETE_PE_HOP; i++) {
                gettimeofday(&timp_start, NULL);

                struct icmphdr icmp_hdr;
                memset(&icmp_hdr, 0, sizeof(icmp_hdr));
                icmp_hdr.type = ICMP_ECHO_REQUEST;
                icmp_hdr.code = 0;
                icmp_hdr.un.echo.id = htons(getpid() & 0xFFFF);
                icmp_hdr.un.echo.sequence = htons((ttl << 8) | i);
                icmp_hdr.checksum = 0;
                icmp_hdr.checksum = in_cksum((unsigned short *)&icmp_hdr, sizeof(icmp_hdr));

               if (sendto(socket_icmp, &icmp_hdr, sizeof(icmp_hdr), 0, (struct sockaddr *)&adresa_destinatie, sizeof(adresa_destinatie)) < 0) {
                    continue;
                }

                struct sockaddr_in adresa_de_la;
                socklen_t lungime_adresa = sizeof(adresa_de_la);
                lungime_primita = recvfrom(socket_icmp, pachet_primit, sizeof(pachet_primit), 0, (struct sockaddr *)&adresa_de_la, &lungime_adresa);
                gettimeofday(&timp_sfarsit, NULL);

                if (lungime_primita > 0) {
                    struct iphdr *antet_ip = (struct iphdr *)pachet_primit;
                    struct icmphdr *antet_icmp = (struct icmphdr *)(pachet_primit + (antet_ip->ihl * 4));

                    if (antet_icmp->type == ICMP_TIME_EXCEEDED || antet_icmp->type == ICMP_DEST_UNREACH ||
                        antet_icmp->type == ICMP_ECHO_REPLY) {
                        double rtt = calculeaza_diferenta_timp_ms(&timp_sfarsit, &timp_start);
                        suma_rtt += rtt;
                        pachete_succes++;
                        inet_ntop(AF_INET, &adresa_de_la.sin_addr, ip_hop, sizeof(ip_hop));
                        hop_raspunde = 1;
                        if (antet_icmp->type == ICMP_DEST_UNREACH || antet_icmp->type == ICMP_ECHO_REPLY) finalizat = 1;
                    }
                }
            }

           if (hop_raspunde) {
               double rtt_mediu;
               if (pachete_succes > 0) {
                   rtt_mediu = suma_rtt / pachete_succes;
            }else{
                   rtt_mediu = suma_rtt / 1;}
             snprintf(linie_status, sizeof(linie_status), "Hop %02d: %-15s | RTT: %.2f ms\n", ttl, ip_hop, rtt_mediu);
               }
               else {
               snprintf(linie_status, sizeof(linie_status), "Hop %02d: * * * (timeout)\n", ttl);
           }

            FILE *f_append = fopen(FISIER_STARE_RUTA, "a");
            if (f_append) {
                fprintf(f_append, "%s", linie_status);
                fflush(f_append);
                fclose(f_append);
            }

            if (finalizat) break;
        }
        sleep(1);
        }

    close(socket_icmp);
    _exit(0);
}

void executa_comanda_start_trace(int canal) {
    FILE *f = fopen(FISIER_CONFIG_IP, "r");
    if (f) {
        if (fgets(tinta_trace, sizeof(tinta_trace), f)) {
            tinta_trace[strcspn(tinta_trace, "\n")] = 0;
        }
        fclose(f);
    }

    if (tinta_trace[0] == '\0') {
        dprintf(canal, "ruta nu este setata.\n");
        return;
    }

    FILE *f_pid = fopen(FISIER_PID_TRACE, "r");
    pid_t pid_existent = -1;
    if (f_pid) {
        fscanf(f_pid, "%d", &pid_existent);
        fclose(f_pid);
    }

    if (pid_existent > 0 && kill(pid_existent, 0) == 0) {
        dprintf(canal, "monitorizarea ruleaza deja (pid: %d).\n", pid_existent);
        return;
    }

    pid_trace = fork();
    if (pid_trace < 0) {
        perror("eroare fork");
        dprintf(canal, "serverul nu a putut porni monitorizarea.\n");
        return;
    }

    if (pid_trace == 0) {
        monitor_traceroute_loop();
    } else {
        f_pid = fopen(FISIER_PID_TRACE, "w");
        if (f_pid) {
            fprintf(f_pid, "%d", pid_trace);
            fclose(f_pid);
        }
        dprintf(canal, "monitorizarea rutei catre %s a pornit in fundal (PID %d).\n", tinta_trace, pid_trace);
    }
}

void executa_comanda_stop_trace(int canal) {
    FILE *f_pid = fopen(FISIER_PID_TRACE, "r");
    pid_t pid_de_oprit = -1;
    if (f_pid) {
        fscanf(f_pid, "%d", &pid_de_oprit);
        fclose(f_pid);
    }

    if (pid_de_oprit > 0) {
        if (kill(pid_de_oprit, SIGTERM) == 0) {
            waitpid(pid_de_oprit, NULL, 0);
            dprintf(canal, "monitorizarea rutei a fost oprita cu succes.\n");
            remove(FISIER_PID_TRACE);
            remove(FISIER_STARE_RUTA);
            return;
        } else {
            dprintf(canal, "monitorizarea nu pare sa ruleze sau eroare la oprire.\n");
        }
    } else {
        dprintf(canal, "monitorizarea nu este activa (nu am gasit PID).\n");
    }
}

void executa_comanda_get_route_status(int canal) {
    FILE *f_pid = fopen(FISIER_PID_TRACE, "r");
    pid_t pid_activ = -1;
    if (f_pid) {
        fscanf(f_pid, "%d", &pid_activ);
        fclose(f_pid);
    }

    if (pid_activ > 0 && kill(pid_activ, 0) == 0) {
        FILE *fisier_status = fopen(FISIER_STARE_RUTA, "r");
        if (fisier_status) {
            char continut_status[DIM_MAX];
            size_t n = fread(continut_status, 1, sizeof(continut_status) - 1, fisier_status);
            continut_status[n] = '\0';
            fclose(fisier_status);

            if (n > 0) {
                dprintf(canal, "\n%s", continut_status);
            } else {
                dprintf(canal, "Se initializeaza monitorizarea...\n");
            }
        } else {
            dprintf(canal, "Eroare la citirea fisierului de status.\n");
        }
    } else {
        dprintf(canal, "Monitorizarea nu este activa. Foloseste 'start-trace'.\n");
    }
}

void cleanup(int signum) {
    if (pid_trace > 0) {
        kill(pid_trace, SIGTERM);
        waitpid(pid_trace, NULL, 0);
    }
    remove(FISIER_STARE_RUTA);
    exit(0);
}
int main() {
    umask(0);
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    signal(SIGCHLD, SIG_IGN);

    int server_fd, socket_client;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    char comanda[DIM_MAX];

    printf("Server NetDiag TCP (PID: %d) pornit pe port %d. Astept clienti simultani...\n", getpid(), PORT);

    while (1) {
        if ((socket_client = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        pid_t pid_client = fork();

        if (pid_client < 0) {
            perror("Eroare la fork client");
            close(socket_client);
            continue;
        }

        if (pid_client == 0) {
            close(server_fd);

            int autentificat = 0;
            char user_curent[256] = "";

            while(1) {
                int n = read(socket_client, comanda, sizeof comanda - 1);
                if (n <= 0) break;

                comanda[n] = '\0';
                int len = strlen(comanda);
                while (len > 0 && (comanda[len-1] == '\n' || comanda[len-1] == ' ' || comanda[len-1] == '\r')) {
                    comanda[--len] = '\0';
                }

                if(!comanda_valida(comanda)) {
                    raspunde_clientului(socket_client, "Buna incercare, insa comanda nu e corecta🫢!\n");
                    continue;
                }

                int socket[2];
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket) < 0) {
                    perror("eroare la socketpair");
                    break;
                }

                pid_t pid_cmd = fork();
                if (pid_cmd < 0) {
                    perror("eroare la fork comanda");
                    close(socket[0]); close(socket[1]);
                    break;
                }

                if (pid_cmd == 0) {
                    int canal = socket[1];
                    close(socket[0]);

                    if (strncmp(comanda, "login :", 7) == 0) {
                        char nume[256];
                        if (sscanf(comanda + 7, "%255s", nume) != 1)
                            write(canal, "Format gresit!\n", 15);
                        else
                            executa_comanda_login(canal, nume);
                    }
                    else if (strncmp(comanda, "logout", 6) == 0) {
                        if (autentificat) write(canal, "logout realizat cu succes🤝!\n", strlen("logout realizat cu succes🤝!\n"));
                        else write(canal, "nu esti logat🙀\n", strlen("nu esti logat🙀\n"));
                    }
                    else if (strncmp(comanda, "quit", 4) == 0) {
                        write(canal, "ai iesit🫡\n", strlen("ai iesit🫡\n"));
                    }
                    else if (!autentificat) {
                        write(canal, "nu esti logat!🙀\n", strlen("nu esti logat!🙀\n"));
                    }
                    else if (strncmp(comanda, "set-route :", 11) == 0) {
                        char ip[256];
                        if(sscanf(comanda+11, "%255s", ip)!=1) write(canal, "IP lipsa.\n", 10);
                        else executa_comanda_set_route(canal, ip);
                    }
                    else if (strncmp(comanda, "start-trace", 11) == 0) executa_comanda_start_trace(canal);
                    else if (strncmp(comanda, "stop-trace", 10) == 0) executa_comanda_stop_trace(canal);
                    else if (strncmp(comanda, "config :", 8) == 0) executa_comanda_config(canal, comanda + 8);
                    else if (strncmp(comanda, "get-route-status", 16) == 0) executa_comanda_get_route_status(canal);
                    else write(canal, "Eroare interna.\n", 16);
                    close(canal);
                    _exit(0);
                }
                else {
                    int canal = socket[0];
                    close(socket[1]);
                    char raspuns_server[DIM_MAX];
                    int r = read(canal, raspuns_server, sizeof raspuns_server - 1);
                    if (r < 0) raspuns_server[0] = '\0'; else raspuns_server[r] = '\0';

                    raspunde_clientului(socket_client, raspuns_server);

                    if (strncmp(comanda, "start-trace", 11) != 0) {
                        waitpid(pid_cmd, NULL, 0);
                    }

                    if (strncmp(comanda, "login :", 7) == 0 && strstr(raspuns_server, "succes")) {
                        sscanf(comanda + 7, "%255s", user_curent);
                        autentificat = 1;
                    }
                    if (strncmp(comanda, "logout", 6) == 0) autentificat = 0;
                    if (strncmp(comanda, "quit", 4) == 0) {
                        close(socket_client);
                        _exit(0);
                    }
                    close(canal);
                }
            }
            close(socket_client);
            _exit(0);

        } else {
            close(socket_client);
        }
    }

    return 0;
}
