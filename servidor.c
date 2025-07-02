// No Code::Blocks, inclua as bibliotecas: -lwsock32 -lpthread
// No Linux/gcc, compile com: gcc -o servidor servidor.c -lpthread

// #define WIN // Descomente para compilar no Windows
#ifdef WIN
    #include <winsock2.h>
    #include <windows.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAM_MENSAGEM 1000
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

    printf("[INFO] Usuário '%s' (%s:%d) conectado.\n", nome, ip, porta_p2p);
}

void remover_usuario(int socket_fd, char* nome_removido_out) {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios, *anterior = NULL;
    int encontrado = 0;

    while (atual != NULL && atual->socket_fd != socket_fd) {
        anterior = atual;
        atual = atual->prox;
    }

    if (atual != NULL) {
        strcpy(nome_removido_out, atual->nome);
        encontrado = 1;
        if (anterior == NULL) lista_usuarios = atual->prox;
        else anterior->prox = atual->prox;
        free(atual);
    }
    
    pthread_mutex_unlock(&mutex_lista);
    
    if (encontrado) {
        printf("[INFO] Usuário '%s' desconectado.\n", nome_removido_out);
    }
}

void broadcast_lista_usuarios() {
    char buffer_lista[TAM_MENSAGEM * 10];
    char buffer_usuario[100];
    int sockets_para_enviar[MAX_CLIENTS];
    int num_clientes = 0;

    pthread_mutex_lock(&mutex_lista);

    memset(buffer_lista, 0, sizeof(buffer_lista));
    strcpy(buffer_lista, "L");
    Usuario *atual = lista_usuarios;
    if (atual == NULL) {
        strcat(buffer_lista, "|");
    } else {
        while (atual != NULL) {
            snprintf(buffer_usuario, 100, "%s:%s:%d|", atual->nome, atual->ip, atual->porta_p2p);
            strcat(buffer_lista, buffer_usuario);
            atual = atual->prox;
        }
    }
    
    atual = lista_usuarios;
    while (atual != NULL && num_clientes < MAX_CLIENTS) {
        sockets_para_enviar[num_clientes++] = atual->socket_fd;
        atual = atual->prox;
    }

    pthread_mutex_unlock(&mutex_lista);

    printf("[BROADCAST] Enviando lista para %d cliente(s): %s\n", num_clientes, buffer_lista);
    for (int i = 0; i < num_clientes; i++) {
        if (send(sockets_para_enviar[i], buffer_lista, strlen(buffer_lista), 0) < 0) {
        }
    }
}

// --- LÓGICA DE PROCESSAMENTO DE MENSAGENS E THREAD ---

void processar_registro(char *msg, int sock, const char* ip_cliente) {
    char copia[TAM_MENSAGEM];
    strcpy(copia, msg);

    char *payload = copia + 1;
    strtok(payload, "|");
    char *porta_str = strtok(NULL, "|");
    char *nome = strtok(NULL, "|");

    if (porta_str && nome) {
        adicionar_usuario(sock, ip_cliente, nome, atoi(porta_str));
    } else {
        printf("[ERRO] Mensagem de registro mal formada: %s\n", msg);
    }
}

void *handle_client(void *arg) {
    int socket_cliente = ((struct Usuario*)arg)->socket_fd;
    char ip_cliente[16];
    strcpy(ip_cliente, ((struct Usuario*)arg)->ip);
    free(arg);

    char buffer[TAM_MENSAGEM];
    int bytes_recebidos;
    int registrado = 0;

    while ((bytes_recebidos = recv(socket_cliente, buffer, TAM_MENSAGEM, 0)) > 0) {
        buffer[bytes_recebidos] = '\0';
        char tipo = buffer[0];

        if (!registrado && tipo == 'R') {
            processar_registro(buffer, socket_cliente, ip_cliente);
            registrado = 1;
            broadcast_lista_usuarios();
        } else if (registrado && tipo == 'D') {
            break;
        } else {
            printf("[AVISO] Mensagem inesperada do socket %d: %s\n", socket_cliente, buffer);
        }
    }
    
    char nome_removido[50] = "desconhecido";
    remover_usuario(socket_cliente, nome_removido);
    if (strcmp(nome_removido, "desconhecido") != 0) {
        broadcast_lista_usuarios();
    }
    
    close(socket_cliente);
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

    printf("Servidor Caramelo iniciado na porta %d. Aguardando conexões...\n", PORTA_SERVIDOR_TCP);

    while (1) { // Loop principal para aceitar novas conexões
        struct sockaddr_in endereco_cliente;
        socklen_t tamanho_endereco = sizeof(endereco_cliente);
        int socket_cliente = accept(sock_servidor, (struct sockaddr *) &endereco_cliente, &tamanho_endereco);

        if (socket_cliente < 0) {
            printf("Erro no accept()\n");
            continue; // Continua para a próxima tentativa de conexão
        }
        
        // Prepara os dados para passar para a nova thread
        Usuario *args = (Usuario*)malloc(sizeof(Usuario));
        args->socket_fd = socket_cliente;
        strcpy(args->ip, inet_ntoa(endereco_cliente.sin_addr));

        // Cria uma thread para cuidar do novo cliente
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)args) != 0) {
            printf("Erro ao criar a thread para o cliente.\n");
            free(args);
            close(socket_cliente);
        }
        pthread_detach(thread_id); // Não precisamos esperar a thread terminar
    }

    close(sock_servidor);
    #ifdef WIN
        WSACleanup();
    #endif
    return 0;
}