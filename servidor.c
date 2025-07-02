// No Code::Blocks, inclua as bibliotecas: -lwsock32 -lpthread
// No Linux/gcc, compile com: gcc -o servidor servidor.c -lpthread


#ifdef _WIN32
    #define WIN
    #include <winsock2.h>
    #include <windows.h>
    #define close_socket(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define close_socket(s) close(s)
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAM_PAYLOAD 1024
#define TAM_MSG_COMPLETA (TAM_PAYLOAD + 4) // 3 para tamanho, 1 para tipo
#define PORTA_SERVIDOR_TCP 9999
#define MAXPENDING 10
#define MAX_CLIENTS 100

// --- ESTRUTURA E LISTA GLOBAL DE USUÁRIOS ---
typedef struct Usuario {
    int socket_fd;
    char ip[16];
    char nome[50];
    int porta_p2p;
    struct Usuario *prox;
} Usuario;

Usuario *lista_usuarios = NULL;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

// --- FUNCOES AUXILIARES DO PROTOCOLO ---

// Envia uma mensagem formatada de acordo com o protocolo: <LEN><TIPO><PAYLOAD>
int enviar_mensagem_protocolo(int sock, char tipo, const char *payload) {
    char buffer[TAM_MSG_COMPLETA];
    int tamanho_payload = strlen(payload);
    int tamanho_total = 1 + tamanho_payload; // 1 para o tipo

    snprintf(buffer, TAM_MSG_COMPLETA, "%03d%c%s", tamanho_total, tipo, payload);
    
    return send(sock, buffer, 3 + tamanho_total, 0);
}

// Recebe uma mensagem formatada
int receber_mensagem_protocolo(int sock, char *tipo_out, char *payload_out) {
    char len_str[4] = {0};
    int bytes_lidos = recv(sock, len_str, 3, 0);
    if (bytes_lidos <= 0) return bytes_lidos;

    int tamanho_a_ler = atoi(len_str);
    if (tamanho_a_ler > TAM_PAYLOAD) { // Proteção contra overflow
        printf("[ERRO] Payload muito grande recebido: %d\n", tamanho_a_ler);
        return -1;
    }
    
    char buffer_temp[TAM_MSG_COMPLETA] = {0};
    bytes_lidos = recv(sock, buffer_temp, tamanho_a_ler, 0);
    if (bytes_lidos <= 0) return bytes_lidos;
    
    *tipo_out = buffer_temp[0];
    strcpy(payload_out, buffer_temp + 1);

    return bytes_lidos;
}

// --- FUNÇÕES DE GERENCIAMENTO DA LISTA ---

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
        printf("[BROADCAST] Enviando lista para %d cliente(s): %s\n", num_clientes, buffer_lista);
        for (int i = 0; i < num_clientes; i++) {
            enviar_mensagem_protocolo(sockets_para_enviar[i], 'L', buffer_lista);
        }
    }
}

// --- THREAD DE TRATAMENTO DO CLIENTE ---

void *handle_client(void *arg) {
    int socket_cliente = *(int*)arg;
    free(arg);

    char tipo_msg;
    char payload[TAM_PAYLOAD];
    int registrado = 0;

    while (1) {
        int bytes_recebidos = receber_mensagem_protocolo(socket_cliente, &tipo_msg, payload);
        
        if (bytes_recebidos <= 0) {
            printf("[INFO] Cliente no socket %d encerrou a conexao.\n", socket_cliente);
            break;
        }

        if (!registrado && tipo_msg == 'R') {
            char ip[16], nome[50];
            int porta_p2p;
            // Payload: <IP>|<PORTA>|<NOME>|
            sscanf(payload, "%15[^|]|%d|%49[^|]|", ip, &porta_p2p, nome);
            
            adicionar_usuario(socket_cliente, ip, nome, porta_p2p);
            enviar_mensagem_protocolo(socket_cliente, 'A', ""); // Envia Ack de confirmação
            registrado = 1;
            broadcast_lista_usuarios();

        } else if (registrado && tipo_msg == 'D') {
            printf("[INFO] Cliente '%s' solicitou desconexao.\n", payload);
            break; // Sai do loop para remover e fechar
        
        } else {
            printf("[AVISO] Mensagem inesperada do socket %d: Tipo %c\n", socket_cliente, tipo_msg);
        }
    }
    
    remover_usuario(socket_cliente);
    broadcast_lista_usuarios(); // Informa aos outros que o usuário saiu
    
    close_socket(socket_cliente);
    pthread_exit(NULL);
}


// --- MAIN ---
int main() {
    #ifdef WIN
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("Erro ao iniciar o Winsock.\n"); return 1;
        }
    #endif

    int sock_servidor;
    struct sockaddr_in endereco_servidor;

    if ((sock_servidor = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erro no socket()\n"); return 1;
    }

    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR_TCP);

    if (bind(sock_servidor, (struct sockaddr *) &endereco_servidor, sizeof(endereco_servidor)) < 0) {
        printf("Erro no bind()\n"); return 1;
    }

    if (listen(sock_servidor, MAXPENDING) < 0) {
        printf("Erro no listen()\n"); return 1;
    }

    printf("Servidor Caramelo iniciado na porta %d. Aguardando conexoes...\n", PORTA_SERVIDOR_TCP);

    while (1) {
        int socket_cliente = accept(sock_servidor, NULL, NULL);

        if (socket_cliente < 0) {
            printf("Erro no accept()\n");
            continue;
        }
        
        int *arg = malloc(sizeof(int));
        *arg = socket_cliente;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, arg) != 0) {
            printf("Erro ao criar a thread para o cliente.\n");
            free(arg);
            close_socket(socket_cliente);
        }
        pthread_detach(thread_id);
    }

    close_socket(sock_servidor);
    #ifdef WIN
        WSACleanup();
    #endif
    return 0;
}