// No Code::Blocks, inclua a biblioteca pthread para threads:
// Project -> Build Options... -> Linker settings -> Other link options: -lwsock32 -lpthread

// #define WIN // Descomente para compilar no Windows
#ifdef WIN
    #include <winsock2.h>
    #include <windows.h>
    #include <process.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAM_MENSAGEM 1024
#define PORTA_SERVIDOR_TCP 9999
#define DEFAULT_P2P_PORT 5000
#define MAX_CLIENTS 100

typedef struct Usuario {
    char nome[50];
    char ip[16];
    int porta;
    struct Usuario *prox;
} Usuario;

int sock_servidor;
char meu_nome[50];
char meu_ip[16];
int minha_porta_p2p;
int cliente_rodando = 1;

Usuario *lista_usuarios = NULL;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

void destruir_lista_local() {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    while (atual != NULL) {
        Usuario *temp = atual;
        atual = atual->prox;
        free(temp);
    }
    lista_usuarios = NULL;
    pthread_mutex_unlock(&mutex_lista);
}

void processar_e_atualizar_lista(char *lista_str) {
    destruir_lista_local();

    char* copia_lista = strdup(lista_str);
    if (copia_lista == NULL) return;

    char *payload = copia_lista + 1;
    char *token_usuario = strtok(payload, "|");

    while (token_usuario != NULL) {
        Usuario *novo = (Usuario*)malloc(sizeof(Usuario));
        if (novo) {
            int itens_lidos = sscanf(token_usuario, "%49[^:]:%15[^:]:%d", novo->nome, novo->ip, &novo->porta);

            if (itens_lidos == 3) { // Verifica se os 3 campos foram lidos com sucesso
                novo->prox = NULL;
                pthread_mutex_lock(&mutex_lista);
                novo->prox = lista_usuarios;
                lista_usuarios = novo;
                pthread_mutex_unlock(&mutex_lista);
            } else {
                free(novo); // Libera memória se o parsing falhar
            }
        }
        token_usuario = strtok(NULL, "|");
    }
    
    free(copia_lista);
    
    printf("\n[INFO] Lista de usuários atualizada.\n");
    printf("Escolha uma opção: ");
    fflush(stdout);
}


void imprimir_lista_local() {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    if (atual == NULL) {
        printf("Nenhum outro usuário online.\n");
    }
    while (atual != NULL) {
        printf("- %s (%s:%d)\n", atual->nome, atual->ip, atual->porta);
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);
}

Usuario* encontrar_usuario(const char* nome) {
    pthread_mutex_lock(&mutex_lista);
    Usuario* atual = lista_usuarios;
    while (atual != NULL) {
        if (strcmp(atual->nome, nome) == 0) {
            pthread_mutex_unlock(&mutex_lista);
            return atual;
        }
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);
    return NULL;
}


void tratar_envio_direto() {
    char nome_destino[50];
    char mensagem[TAM_MENSAGEM];

    printf("\nDigite o nome do destinatário: ");
    scanf("%49s", nome_destino);

    Usuario* alvo = encontrar_usuario(nome_destino);
    if (alvo == NULL) {
        printf("\n[ERRO] Usuário '%s' não encontrado ou offline.\n", nome_destino);
        return;
    }
    if (strcmp(alvo->nome, meu_nome) == 0){
        printf("\n[ERRO] Você não pode enviar uma mensagem para si mesmo.\n");
        return;
    }

    printf("Digite a sua mensagem: ");
    while (getchar() != '\n');
    fgets(mensagem, TAM_MENSAGEM, stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;

    int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_p2p < 0) {
        printf("[ERRO] Falha ao criar socket P2P.\n"); return;
    }

    struct sockaddr_in endereco_alvo;
    memset(&endereco_alvo, 0, sizeof(endereco_alvo));
    endereco_alvo.sin_family = AF_INET;
    endereco_alvo.sin_addr.s_addr = inet_addr(alvo->ip);
    endereco_alvo.sin_port = htons(alvo->porta);

    if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) < 0) {
        printf("\n[ERRO] Não foi possível conectar com %s. Ele pode estar offline.\n", nome_destino);
        close(sock_p2p);
        return;
    }
    
    char buffer_final[TAM_MENSAGEM];
    snprintf(buffer_final, TAM_MENSAGEM, "M%s|%s|", meu_nome, mensagem);

    send(sock_p2p, buffer_final, strlen(buffer_final), 0);
    printf("\n[INFO] Mensagem enviada para %s.\n", nome_destino);
    close(sock_p2p);
}

void tratar_envio_broadcast() {
    char mensagem[TAM_MENSAGEM];
    Usuario alvos[MAX_CLIENTS];
    int num_alvos = 0;

    printf("\nDigite a sua mensagem para todos: ");
    while (getchar() != '\n');
    fgets(mensagem, TAM_MENSAGEM, stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;

    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    while(atual != NULL && num_alvos < MAX_CLIENTS) {
        if (strcmp(atual->nome, meu_nome) != 0) {
            strcpy(alvos[num_alvos].nome, atual->nome);
            strcpy(alvos[num_alvos].ip, atual->ip);
            alvos[num_alvos].porta = atual->porta;
            num_alvos++;
        }
        atual = atual->prox;
    }
    
    pthread_mutex_unlock(&mutex_lista);

    for(int i = 0; i < num_alvos; i++) {
        int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
        if (sock_p2p >= 0) {
            struct sockaddr_in endereco_alvo;
            memset(&endereco_alvo, 0, sizeof(endereco_alvo));
            endereco_alvo.sin_family = AF_INET;
            endereco_alvo.sin_addr.s_addr = inet_addr(alvos[i].ip);
            endereco_alvo.sin_port = htons(alvos[i].porta);

            if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) >= 0) {
                char buffer_final[TAM_MENSAGEM];
                snprintf(buffer_final, TAM_MENSAGEM, "B%s|%s|", meu_nome, mensagem);
                send(sock_p2p, buffer_final, strlen(buffer_final), 0);
            }
            close(sock_p2p);
        }
    }
    printf("\n[INFO] Mensagem de broadcast enviada.\n");
}


void tratar_desconexao() {
    char buffer_final[TAM_MENSAGEM];
    printf("\nDesconectando do servidor...\n");

    snprintf(buffer_final, TAM_MENSAGEM, "D%s|", meu_nome);
    send(sock_servidor, buffer_final, strlen(buffer_final), 0);
    
    cliente_rodando = 0;
    close(sock_servidor);
}

void exibir_menu() {
    printf("\n--- CHAT CARAMELO ---\n");
    printf("Logado como: %s\n", meu_nome);
    printf("---------------------\n");
    printf("1 - Enviar mensagem direta (DM)\n");
    printf("2 - Enviar mensagem para todos (Broadcast)\n");
    printf("3 - Listar usuários online\n");
    printf("4 - Sair\n");
    printf("Escolha uma opção: ");
}

void *thread_recebimento(void *arg) {
    int sock_listen_p2p;
    struct sockaddr_in endereco_local_p2p;

    sock_listen_p2p = socket(PF_INET, SOCK_STREAM, 0);
    
    memset(&endereco_local_p2p, 0, sizeof(endereco_local_p2p));
    endereco_local_p2p.sin_family = AF_INET;
    endereco_local_p2p.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco_local_p2p.sin_port = htons(minha_porta_p2p);

    if (bind(sock_listen_p2p, (struct sockaddr *) &endereco_local_p2p, sizeof(endereco_local_p2p)) < 0) {
        printf("[ERRO FATAL] bind() na porta P2P %d falhou. A porta pode estar em uso.\n", minha_porta_p2p);
        cliente_rodando = 0;
        return NULL;
    }

    listen(sock_listen_p2p, 5);

    while (cliente_rodando) {
        int sock_peer = accept(sock_listen_p2p, NULL, NULL);
        if (sock_peer < 0) {
            if (cliente_rodando) { }
            continue;
        }

        char buffer[TAM_MENSAGEM];
        memset(buffer, 0, TAM_MENSAGEM);
        recv(sock_peer, buffer, TAM_MENSAGEM, 0);

        if (strlen(buffer) > 0) {
            char copia_buffer[TAM_MENSAGEM];
            strcpy(copia_buffer, buffer);

            char tipo = copia_buffer[0];
            char *payload = copia_buffer + 1;
            char *remetente = strtok(payload, "|");
            char *mensagem = strtok(NULL, "|");

            if (remetente && mensagem) {
                if (tipo == 'M') {
                    printf("\n[DM de %s]: %s\n", remetente, mensagem);
                } else if (tipo == 'B') {
                    printf("\n[Broadcast de %s]: %s\n", remetente, mensagem);
                }
            }
        }
        close(sock_peer);
        printf("Escolha uma opção: ");
        fflush(stdout);
    }
    
    close(sock_listen_p2p);
    return NULL;
}

void *thread_servidor(void *arg) {
    char buffer[TAM_MENSAGEM];
    
    while (cliente_rodando) {
        memset(buffer, 0, TAM_MENSAGEM);
        int bytes_recebidos = recv(sock_servidor, buffer, TAM_MENSAGEM, 0);

        if (bytes_recebidos <= 0) {
            if (cliente_rodando) {
                printf("\n[ERRO] Conexão com o servidor perdida. Encerrando...\n");
                cliente_rodando = 0;
                #ifdef WIN
                    shutdown(sock_servidor, SD_BOTH);
                #else
                    close(sock_servidor);
                #endif
            }
            break;
        }

        if (buffer[0] == 'L') {
            processar_e_atualizar_lista(buffer);
        }
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    #ifdef WIN
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            printf("Erro ao iniciar o Winsock.\n");
            return 1;
        }
    #endif

    if (argc < 3 || argc > 4) {
        printf("Uso: %s <IP Servidor> <Seu Nome> [Porta P2P Opcional]\n", argv[0]);
        return 1;
    }
    
    char *ip_servidor = argv[1];
    strcpy(meu_nome, argv[2]);
    strcpy(meu_ip, "127.0.0.1");

    if (argc == 4) minha_porta_p2p = atoi(argv[3]);
    else minha_porta_p2p = DEFAULT_P2P_PORT;

    if ((sock_servidor = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Erro no socket()\n"); return 1;
    }

    struct sockaddr_in endereco_servidor;
    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = inet_addr(ip_servidor);
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR_TCP);

    if (connect(sock_servidor, (struct sockaddr *)&endereco_servidor, sizeof(endereco_servidor)) < 0) {
        printf("Erro no connect() ao servidor principal\n"); return 1;
    }

    printf("Conectado ao servidor! Registrando...\n");

    char msg_registro[TAM_MENSAGEM];
    snprintf(msg_registro, TAM_MENSAGEM, "R%s|%d|%s|", meu_ip, minha_porta_p2p, meu_nome);
    send(sock_servidor, msg_registro, strlen(msg_registro), 0);
    
    pthread_t p2p_thread_id;
    if (pthread_create(&p2p_thread_id, NULL, thread_recebimento, NULL) != 0) {
        printf("[ERRO] Falha ao criar a thread P2P.\n"); return 1;
    }

    pthread_t server_thread_id;
    if (pthread_create(&server_thread_id, NULL, thread_servidor, NULL) != 0) {
        printf("[ERRO] Falha ao criar a thread do servidor.\n"); return 1;
    }

    int escolha = 0;
    while (cliente_rodando) {
        exibir_menu();
        if (scanf("%d", &escolha) != 1) {
            if (!cliente_rodando) break;
            while(getchar() != '\n');
            escolha = 0;
            continue;
        }

        if (!cliente_rodando) break;
        if (escolha == 4) {
            tratar_desconexao();
            break;
        }

        switch (escolha) {
            case 1: tratar_envio_direto(); break;
            case 2: tratar_envio_broadcast(); break;
            case 3:
                printf("\n--- USUARIOS ONLINE ---\n");
                imprimir_lista_local();
                printf("-----------------------\n");
                break;
            default: printf("\nOpção inválida!\n"); break;
        }
    }

    pthread_join(p2p_thread_id, NULL);
    pthread_join(server_thread_id, NULL);
    
    printf("Programa encerrado.\n");
    #ifdef WIN
        WSACleanup();
    #endif
    return 0;
}