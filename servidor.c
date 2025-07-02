// No Code::Blocks, inclua as bibliotecas: -lwsock32 -lpthread
// No Linux/gcc, compile com: gcc -o servidor servidor.c -lpthread

#ifdef _WIN32
    #define WIN
    #include <winsock2.h>
    #include <windows.h>
    #define close_socket(s) closesocket(s)
    #define sleep(s) Sleep(s)
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <signal.h>
    #define close_socket(s) close(s)
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define TAM_PAYLOAD 1024
#define TAM_MSG_COMPLETA (TAM_PAYLOAD + 4)
#define PORTA_SERVIDOR_TCP 9999
#define MAXPENDING 10
#define MAX_CLIENTS 100
#define TIMEOUT_INATIVIDADE 300

time_t ultima_interacao_global = 0;
pthread_mutex_t mutex_interacao = PTHREAD_MUTEX_INITIALIZER;

volatile int desligamento_agendado = 0;
volatile int servidor_rodando = 1;
int sock_servidor_global;

typedef struct Usuario {
    int socket_fd;
    char ip[16];
    char nome[50];
    int porta_p2p;
    struct Usuario *prox;
} Usuario;

Usuario *lista_usuarios = NULL;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

int enviar_mensagem_protocolo(int sock, char tipo, const char *payload) {
    char buffer[TAM_MSG_COMPLETA];
    int tamanho_payload = strlen(payload);
    int tamanho_total_msg = 1 + tamanho_payload;
    snprintf(buffer, TAM_MSG_COMPLETA, "%03d%c%s", tamanho_total_msg, tipo, payload);
    return send(sock, buffer, 4 + tamanho_total_msg, 0);
}

int receber_mensagem_protocolo(int sock, char *tipo_out, char *payload_out) {
    char len_str[4] = {0};
    int bytes_lidos = recv(sock, len_str, 3, 0);
    if (bytes_lidos <= 0) return bytes_lidos;
    int tamanho_a_ler = atoi(len_str);
    if (tamanho_a_ler > TAM_PAYLOAD + 1) {
        return -1;
    }
    char buffer_temp[TAM_MSG_COMPLETA] = {0};
    bytes_lidos = recv(sock, buffer_temp, tamanho_a_ler, 0);
    if (bytes_lidos <= 0) return bytes_lidos;
    *tipo_out = buffer_temp[0];
    strcpy(payload_out, buffer_temp + 1);
    return bytes_lidos;
}

#ifndef WIN
void desligar_servidor() {
    printf("\n[SHUTDOWN] Sinal de desligamento recebido. Encerrando o servidor graciosamente...\n");

    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    printf("[SHUTDOWN] Desconectando todos os clientes...\n");
    while (atual != NULL) {
        close_socket(atual->socket_fd);
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);
    
    servidor_rodando = 0;
    close_socket(sock_servidor_global);
    
    printf("[SHUTDOWN] Servidor encerrado.\n");
    exit(0);
}
#endif

void adicionar_usuario(int socket_fd, const char* ip, const char* nome, int porta_p2p) {
    Usuario* novo = (Usuario*)malloc(sizeof(Usuario));
    novo->socket_fd = socket_fd;
    strcpy(novo->ip, ip);
    strcpy(novo->nome, nome);
    novo->porta_p2p = porta_p2p;
    
    pthread_mutex_lock(&mutex_lista);
    novo->prox = lista_usuarios;
    lista_usuarios = novo;
    pthread_mutex_unlock(&mutex_lista);

    printf("[INFO] Usuario '%s' (%s:%d) conectado via socket %d.\n", nome, ip, porta_p2p, socket_fd);
}

void remover_usuario(int socket_fd) {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios, *anterior = NULL;
    char nome_removido[50] = "desconhecido";
    while (atual != NULL && atual->socket_fd != socket_fd) {
        anterior = atual;
        atual = atual->prox;
    }
    if (atual != NULL) {
        strcpy(nome_removido, atual->nome);
        if (anterior == NULL) lista_usuarios = atual->prox;
        else anterior->prox = atual->prox;
        free(atual);
        printf("[INFO] Usuario '%s' (socket %d) desconectado.\n", nome_removido, socket_fd);
    }
    pthread_mutex_unlock(&mutex_lista);
}

void broadcast_lista_usuarios() {
    char buffer_lista[TAM_PAYLOAD] = {0};
    char buffer_usuario[100];
    int sockets_para_enviar[MAX_CLIENTS];
    int num_clientes = 0;

    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    while (atual != NULL) {
        snprintf(buffer_usuario, 100, "%s:%s:%d|", atual->nome, atual->ip, atual->porta_p2p);
        strcat(buffer_lista, buffer_usuario);
        sockets_para_enviar[num_clientes++] = atual->socket_fd;
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);

    if(num_clientes > 0) {
        printf("[BROADCAST] Enviando lista para %d cliente(s).\n", num_clientes);
        for (int i = 0; i < num_clientes; i++) {
            enviar_mensagem_protocolo(sockets_para_enviar[i], 'L', buffer_lista);
        }
    }
    
    pthread_mutex_lock(&mutex_interacao);
    ultima_interacao_global = time(NULL);
    pthread_mutex_unlock(&mutex_interacao);

    #ifndef WIN
    if (desligamento_agendado) {
        desligar_servidor();
    }
    #endif
}

void broadcast_mensagem_geral() {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    while (atual != NULL) {
        enviar_mensagem_protocolo(atual->socket_fd, 'B', "Mensagem de alerta do servidor.");
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);

    pthread_mutex_lock(&mutex_interacao);
    ultima_interacao_global = time(NULL);
    pthread_mutex_unlock(&mutex_interacao);

    #ifndef WIN
    if (desligamento_agendado) {
        desligar_servidor();
    }
    #endif
}

void *timer_inatividade(void *arg) {
    (void)arg;
    while (servidor_rodando) {
        #ifdef WIN
            Sleep(TIMEOUT_INATIVIDADE * 1000);
        #else
            sleep(TIMEOUT_INATIVIDADE);
        #endif

        if (!servidor_rodando) break;

        time_t agora = time(NULL);
        time_t ultima_interacao;
        int num_clientes = 0;
        
        pthread_mutex_lock(&mutex_lista);
        Usuario *atual = lista_usuarios;
        while (atual != NULL) {
            num_clientes++;
            atual = atual->prox;
        }
        pthread_mutex_unlock(&mutex_lista);
        
        pthread_mutex_lock(&mutex_interacao);
        ultima_interacao = ultima_interacao_global;
        pthread_mutex_unlock(&mutex_interacao);
        
        if (num_clientes > 0 && difftime(agora, ultima_interacao) >= TIMEOUT_INATIVIDADE) {
            printf("[TIMER] Nenhuma interação nos últimos %d segundos. Enviando mensagem de alerta.\n", TIMEOUT_INATIVIDADE);
            broadcast_mensagem_geral();
        }
    }
    return NULL;
}

void *handle_client(void *arg) {
    int socket_cliente = *(int*)arg;
    free(arg);
    char tipo_msg, payload[TAM_PAYLOAD];
    int registrado = 0;
    while (servidor_rodando) {
        int bytes_recebidos = receber_mensagem_protocolo(socket_cliente, &tipo_msg, payload);
        if (bytes_recebidos <= 0) {
            break;
        }
        if (!registrado && tipo_msg == 'R') {
            char ip[16], nome[50];
            int porta_p2p;
            sscanf(payload, "%15[^|]|%d|%49[^|]|", ip, &porta_p2p, nome);
            adicionar_usuario(socket_cliente, ip, nome, porta_p2p);
            enviar_mensagem_protocolo(socket_cliente, 'A', "");
            registrado = 1;
            broadcast_lista_usuarios();
        } else if (registrado && tipo_msg == 'D') {
            break;
        }
    }
    remover_usuario(socket_cliente);
    broadcast_lista_usuarios();
    close_socket(socket_cliente);
    pthread_exit(NULL);
}

#ifndef WIN
void tratar_sinal_desligamento(int sinal) {
    if (sinal == SIGUSR1) {
        printf("\n[SINAL] Sinal SIGUSR1 (10) recebido. Desligamento agendado para a proxima interacao.\n");
        desligamento_agendado = 1;
    }
}
#endif

int main() {
    #ifdef WIN
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { return 1; }
    #else
        signal(SIGUSR1, tratar_sinal_desligamento);
    #endif

    struct sockaddr_in endereco_servidor;
    if ((sock_servidor_global = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erro no socket()\n"); return 1;
    }

    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR_TCP);

    if (bind(sock_servidor_global, (struct sockaddr *) &endereco_servidor, sizeof(endereco_servidor)) < 0) {
        printf("Erro no bind()\n"); return 1;
    }

    if (listen(sock_servidor_global, MAXPENDING) < 0) {
        printf("Erro no listen()\n"); return 1;
    }

    printf("Servidor Caramelo iniciado na porta %d. Aguardando conexoes...\n", PORTA_SERVIDOR_TCP);

    pthread_t thread_timer;
    if (pthread_create(&thread_timer, NULL, timer_inatividade, NULL) != 0) {
        printf("Erro ao criar thread do timer\n");
    }
    pthread_detach(thread_timer);

    while (servidor_rodando) {
        int socket_cliente = accept(sock_servidor_global, NULL, NULL);
        if (socket_cliente < 0) {
            if (servidor_rodando) printf("Erro no accept()\n");
            continue;
        }
        int *arg = malloc(sizeof(int));
        *arg = socket_cliente;
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, arg) != 0) {
            free(arg);
            close_socket(socket_cliente);
        }
        pthread_detach(thread_id);
    }

    close_socket(sock_servidor_global);
    #ifdef WIN
        WSACleanup();
    #endif
    return 0;
}