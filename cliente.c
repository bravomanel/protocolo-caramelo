// No Code::Blocks, inclua a biblioteca pthread para threads:
// Project -> Build Options... -> Linker settings -> Other link options: -lwsock32 -lpthread

#ifdef _WIN32
    #define WIN
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

#define TAM_PAYLOAD 1024
#define TAM_MSG_COMPLETA (TAM_PAYLOAD + 4)
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
volatile int cliente_rodando = 1;

Usuario *lista_usuarios = NULL;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

// --- FUNCOES AUXILIARES DO PROTOCOLO ---

int enviar_mensagem_protocolo(int sock, char tipo, const char *payload) {
    char buffer[TAM_MSG_COMPLETA];
    int tamanho_payload = strlen(payload);
    int tamanho_total = 1 + tamanho_payload;

    snprintf(buffer, TAM_MSG_COMPLETA, "%03d%c%s", tamanho_total, tipo, payload);
    
    return send(sock, buffer, 3 + tamanho_total, 0);
}

int receber_mensagem_protocolo(int sock, char *tipo_out, char *payload_out) {
    char len_str[4] = {0};
    int bytes_lidos = recv(sock, len_str, 3, 0);
    if (bytes_lidos <= 0) return bytes_lidos;

    int tamanho_a_ler = atoi(len_str);
    if (tamanho_a_ler > TAM_PAYLOAD) {
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

// --- GERENCIAMENTO DA LISTA LOCAL ---

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

void processar_e_atualizar_lista(char *lista_payload) {
    destruir_lista_local();
    
    char* copia_payload = strdup(lista_payload);
    if (!copia_payload) return;

    char *token_usuario = strtok(copia_payload, "|");
    while (token_usuario != NULL) {
        Usuario *novo = (Usuario*)malloc(sizeof(Usuario));
        if (novo) {
            if (sscanf(token_usuario, "%49[^:]:%15[^:]:%d", novo->nome, novo->ip, &novo->porta) == 3) {
                pthread_mutex_lock(&mutex_lista);
                novo->prox = lista_usuarios;
                lista_usuarios = novo;
                pthread_mutex_unlock(&mutex_lista);
            } else {
                free(novo);
            }
        }
        token_usuario = strtok(NULL, "|");
    }
    free(copia_payload);
    printf("\n[INFO] Lista de usuários atualizada.\n> ");
    fflush(stdout);
}

// --- LÓGICA DO MENU E AÇÕES DO USUÁRIO ---

void imprimir_lista_local() {
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    printf("\n--- USUARIOS ONLINE ---\n");
    if (atual == NULL) {
        printf("Nenhum outro usuário online.\n");
    }
    while (atual != NULL) {
        printf("- %s (%s:%d)%s\n", atual->nome, atual->ip, atual->porta, strcmp(atual->nome, meu_nome) == 0 ? " (Você)" : "");
        atual = atual->prox;
    }
    printf("-----------------------\n");
    pthread_mutex_unlock(&mutex_lista);
}

void tratar_envio_direto() {
    char nome_destino[50];
    char mensagem[TAM_PAYLOAD - 60]; // Deixa espaço para o nome

    printf("\nDigite o nome do destinatário: ");
    scanf("%49s", nome_destino);
    while (getchar() != '\n'); 

    if (strcmp(nome_destino, meu_nome) == 0){
        printf("\n[ERRO] Você não pode enviar uma mensagem para si mesmo.\n");
        return;
    }
    
    // Copia os dados do usuário de forma segura
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    Usuario alvo = {0};
    int encontrado = 0;
    while (atual != NULL) {
        if (strcmp(atual->nome, nome_destino) == 0) {
            alvo = *atual;
            encontrado = 1;
            break;
        }
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);

    if (!encontrado) {
        printf("\n[ERRO] Usuário '%s' não encontrado ou offline.\n", nome_destino);
        return;
    }

    printf("Digite a sua mensagem: ");
    fgets(mensagem, sizeof(mensagem), stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;
    
    char payload_final[TAM_PAYLOAD];
    snprintf(payload_final, TAM_PAYLOAD, "%s|%s|", meu_nome, mensagem);

    int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_p2p < 0) return;

    struct sockaddr_in endereco_alvo;
    memset(&endereco_alvo, 0, sizeof(endereco_alvo));
    endereco_alvo.sin_family = AF_INET;
    endereco_alvo.sin_addr.s_addr = inet_addr(alvo.ip);
    endereco_alvo.sin_port = htons(alvo.porta);

    if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) >= 0) {
        enviar_mensagem_protocolo(sock_p2p, 'M', payload_final);
        printf("\n[INFO] Mensagem enviada para %s.\n", nome_destino);
    } else {
        printf("\n[ERRO] Não foi possível conectar com %s.\n", nome_destino);
    }
    close(sock_p2p);
}

void tratar_envio_broadcast(){
  char mensagem[TAM_PAYLOAD - 60]; // Deixa espaço para o nome
  
  printf("Digite a sua mensagem broadcast: ");
  while (getchar() != '\n');
  fgets(mensagem, sizeof(mensagem), stdin);
  mensagem[strcspn(mensagem, "\n")] = 0;
    
  char payload_final[TAM_PAYLOAD];
  snprintf(payload_final, TAM_PAYLOAD, "%s|%s|", meu_nome, mensagem);
  pthread_mutex_lock(&mutex_lista);
    
  Usuario *atual = lista_usuarios;
  while(atual != NULL) {
      if (strcmp(atual->nome, meu_nome) != 0) {
          int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
          if (sock_p2p >= 0) {
              struct sockaddr_in endereco_alvo;
              memset(&endereco_alvo, 0, sizeof(endereco_alvo));
              endereco_alvo.sin_family = AF_INET;
              endereco_alvo.sin_addr.s_addr = inet_addr(atual->ip);
              endereco_alvo.sin_port = htons(atual->porta);
              
              if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) >= 0) {
                  enviar_mensagem_protocolo(sock_p2p, 'B', payload_final);
              }
              close(sock_p2p);
          }
      }
      atual = atual->prox;
  }
  pthread_mutex_unlock(&mutex_lista);
  printf("\n[INFO] Mensagem de broadcast enviada.\n");
}


void tratar_desconexao() {
    printf("\nDesconectando do servidor...\n");
    enviar_mensagem_protocolo(sock_servidor, 'D', meu_nome);
    cliente_rodando = 0;
    close(sock_servidor);
}

void exibir_menu() {
    printf("\n--- CHAT CARAMELO ---\n");
    printf("1 - Enviar mensagem direta (DM)\n");
    printf("2 - Enviar mensagem broadcast\n");
    printf("3 - Listar usuários online\n");
    printf("4 - Sair\n");
    printf("> ");
}

// --- THREADS DE RECEBIMENTO ---

void *thread_recebimento_p2p(void *arg) {
    int sock_listen_p2p = socket(PF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in endereco_local_p2p;
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
        if (sock_peer < 0) continue;

        char tipo, payload[TAM_PAYLOAD];
        if (receber_mensagem_protocolo(sock_peer, &tipo, payload) > 0) {
            char *remetente = strtok(payload, "|");
            char *mensagem = strtok(NULL, "|");
            if (remetente && mensagem) {
                if (tipo == 'M') {
                    printf("\n[DM de %s]: %s\n> ", remetente, mensagem);
                } else {
                    printf("\n[Msg de %s]: %s\n> ", remetente, mensagem);
                }
                fflush(stdout);
            }
        }
        close(sock_peer);
    }
    close(sock_listen_p2p);
    return NULL;
}

void *thread_recebimento_servidor(void *arg) {
    while (cliente_rodando) {
        char tipo, payload[TAM_PAYLOAD];
        int bytes_recebidos = receber_mensagem_protocolo(sock_servidor, &tipo, payload);

        if (bytes_recebidos <= 0) {
            if (cliente_rodando) {
                printf("\n[ERRO] Conexão com o servidor perdida. Encerrando...\n");
                cliente_rodando = 0;
            }
            break;
        }

        if (tipo == 'L') {
            processar_e_atualizar_lista(payload);
        }
    }
    return NULL;
}

// --- MAIN ---
int main(int argc, char *argv[]) {
    #ifdef WIN
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;
    #endif

    if (argc < 4 || argc > 5) {
        printf("Uso: %s <IP Servidor> <Seu IP para P2P> <Seu Nome> [Porta P2P Opcional]\n", argv[0]);
        printf("Exemplo: %s 192.168.0.5 192.168.0.10 Joao 5001\n", argv[0]);
        return 1;
    }
    
    char *ip_servidor = argv[1];
    strcpy(meu_ip, argv[2]);
    strcpy(meu_nome, argv[3]);
    minha_porta_p2p = (argc == 5) ? atoi(argv[4]) : DEFAULT_P2P_PORT;

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

    // 1. Registrar no servidor
    char payload_registro[200];
    snprintf(payload_registro, 200, "%s|%d|%s|", meu_ip, minha_porta_p2p, meu_nome);
    enviar_mensagem_protocolo(sock_servidor, 'R', payload_registro);

    // 2. Esperar confirmação (Ack)
    char tipo_ack;
    char payload_ack[TAM_PAYLOAD];
    if (receber_mensagem_protocolo(sock_servidor, &tipo_ack, payload_ack) > 0 && tipo_ack == 'A') {
        printf("Conectado e registrado com sucesso como '%s'!\n", meu_nome);
    } else {
        printf("Falha ao registrar no servidor. Encerrando.\n");
        close(sock_servidor);
        return 1;
    }
    
    // 3. Iniciar threads de recebimento
    pthread_t p2p_thread_id, server_thread_id;
    pthread_create(&p2p_thread_id, NULL, thread_recebimento_p2p, NULL);
    pthread_create(&server_thread_id, NULL, thread_recebimento_servidor, NULL);

    int escolha = 0;
    while (cliente_rodando) {
        exibir_menu();
        if (scanf("%d", &escolha) != 1) {
            while(getchar() != '\n'); 
            if (!cliente_rodando) break;
            printf("\nOpção inválida!\n");
            continue;
        }

        if (!cliente_rodando) break;
        switch (escolha) {
            case 1: tratar_envio_direto(); break;
            case 2: tratar_envio_broadcast(); break;
            case 3: imprimir_lista_local(); break;
            case 4: tratar_desconexao(); break;
            default: printf("\nOpção inválida!\n"); break;
        }
    }

    // Aguarda threads finalizarem
    #ifdef WIN
      // No windows, o shutdown pode ajudar a desbloquear a thread do accept
      shutdown(sock_servidor, SD_BOTH);
    #endif
    pthread_join(p2p_thread_id, NULL);
    pthread_join(server_thread_id, NULL);
    destruir_lista_local();
    
    printf("Programa encerrado.\n");
    #ifdef WIN
        WSACleanup();
    #endif
    return 0;
}
