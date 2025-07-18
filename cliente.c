// Para compilar com GCC:
// Linux/macOS: gcc cliente_final_v6.c -o cliente -lpthread
// Windows:     gcc cliente_final_v6.c -o cliente.exe -lwsock32 -lpthread

// Headers padrão que são seguros em qualquer sistema
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h> // Incluído aqui para garantir que os tipos de thread sejam sempre conhecidos

// Headers e definições específicas do sistema operacional
#ifdef _WIN32 // _WIN32 é pré-definido por compiladores Windows (MinGW)
    #include <winsock2.h>
    #include <windows.h>
    #include <process.h>
    #include <conio.h>
    #define close_socket(s) closesocket(s)
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <sys/select.h>
    #include <termios.h>
    #define close_socket(s) close(s)
#endif


// --- Definições do Protocolo e Constantes ---
#define TAM_PAYLOAD 1024
#define TAM_MSG_COMPLETA (TAM_PAYLOAD + 4)
#define PORTA_SERVIDOR_TCP 9999
#define DEFAULT_P2P_PORT 5000
#define MAX_ULTIMAS_MENSAGENS 10

// --- Estruturas ---
typedef struct Usuario {
    char nome[50];
    char ip[16];
    int porta;
    struct Usuario *prox;
} Usuario;

// --- Variáveis Globais ---
int sock_servidor;
char meu_nome[50];
char meu_ip[16];
int minha_porta_p2p;
volatile int cliente_rodando = 1;
int conectado_ao_servidor = 0;
Usuario *lista_usuarios = NULL;
pthread_t p2p_thread_id;
pthread_t server_thread_id;
char ultimas_mensagens[MAX_ULTIMAS_MENSAGENS][TAM_PAYLOAD];
int indice_proxima_mensagem = 0;
int total_mensagens_armazenadas = 0;
int menu_pausado = 0;
int novas_mensagens_na_pausa = 0;
int tela_precisa_atualizar = 1;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_mensagens = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_tela = PTHREAD_MUTEX_INITIALIZER;

// --- Protótipos ---
void conectar_ao_servidor();
void desconectar_do_servidor();
void tratar_envio_broadcast();

// --- Funções da Interface e Terminal ---
#ifndef _WIN32
struct termios estado_original_terminal;

void configurar_terminal_nao_bloqueante() {
    struct termios newt = estado_original_terminal;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

void restaurar_terminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &estado_original_terminal);
}
#endif

// --- FUNÇÕES DO PROTOCOLO ---
int enviar_mensagem_protocolo(int sock, char tipo, const char *payload) {
    char buffer[TAM_MSG_COMPLETA];
    int tamanho_payload = strlen(payload);
    snprintf(buffer, TAM_MSG_COMPLETA, "%03d%c%s", tamanho_payload, tipo, payload);
    return send(sock, buffer, 4 + tamanho_payload, 0);
}

int receber_mensagem_protocolo(int sock, char *tipo_out, char *payload_out) {
    char len_str[4] = {0};
    int bytes_lidos = recv(sock, len_str, 3, 0);
    if (bytes_lidos <= 0) return bytes_lidos;

    int tamanho_payload = atoi(len_str);
    if (tamanho_payload > TAM_PAYLOAD) {
        return -1;
    }

    int tamanho_a_ler = 1 + tamanho_payload;
    char buffer_temp[TAM_MSG_COMPLETA] = {0};
    bytes_lidos = recv(sock, buffer_temp, tamanho_a_ler, 0);
    if (bytes_lidos <= 0) return bytes_lidos;

    *tipo_out = buffer_temp[0];
    strcpy(payload_out, buffer_temp + 1);
    return bytes_lidos;
}

void limpar_tela() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
}

void adicionar_mensagem(const char* mensagem) {
    pthread_mutex_lock(&mutex_mensagens);
    strncpy(ultimas_mensagens[indice_proxima_mensagem], mensagem, TAM_PAYLOAD - 1);
    ultimas_mensagens[indice_proxima_mensagem][TAM_PAYLOAD - 1] = '\0';
    indice_proxima_mensagem = (indice_proxima_mensagem + 1) % MAX_ULTIMAS_MENSAGENS;
    if (total_mensagens_armazenadas < MAX_ULTIMAS_MENSAGENS) {
        total_mensagens_armazenadas++;
    }
    pthread_mutex_unlock(&mutex_mensagens);
    if (menu_pausado) {
        novas_mensagens_na_pausa++;
    } else {
        tela_precisa_atualizar = 1;
    }
}

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
                novo->prox = NULL;
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
    tela_precisa_atualizar = 1;
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

void *thread_recebimento_p2p(void *arg) {
    int sock_listen_p2p = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in endereco_local_p2p;
    memset(&endereco_local_p2p, 0, sizeof(endereco_local_p2p));
    endereco_local_p2p.sin_family = AF_INET;
    endereco_local_p2p.sin_addr.s_addr = htonl(INADDR_ANY);
    endereco_local_p2p.sin_port = htons(minha_porta_p2p);
    if (bind(sock_listen_p2p, (struct sockaddr *) &endereco_local_p2p, sizeof(endereco_local_p2p)) < 0) {
        adicionar_mensagem("[ERRO FATAL] Falha na porta P2P. Tente outra.");
        desconectar_do_servidor();
        return NULL;
    }
    listen(sock_listen_p2p, 5);
    while (cliente_rodando && conectado_ao_servidor) {
        int sock_peer = accept(sock_listen_p2p, NULL, NULL);
        if (sock_peer < 0) continue;
        char tipo, payload[TAM_PAYLOAD], msg_formatada[TAM_PAYLOAD];
        if (receber_mensagem_protocolo(sock_peer, &tipo, payload) > 0) {
            char *remetente = strtok(payload, "|");
            char *mensagem = strtok(NULL, "|");
            if (remetente && mensagem) {
                if (tipo == 'M') snprintf(msg_formatada, TAM_PAYLOAD, "[DM de %s]: %s", remetente, mensagem);
                else if (tipo == 'B') snprintf(msg_formatada, TAM_PAYLOAD, "[Broadcast de %s]: %s", remetente, mensagem);
                adicionar_mensagem(msg_formatada);
            }
        }
        close_socket(sock_peer);
    }
    close_socket(sock_listen_p2p);
    return NULL;
}

void *thread_recebimento_servidor(void *arg) {
    while (cliente_rodando && conectado_ao_servidor) {
        char tipo, payload[TAM_PAYLOAD];
        int bytes_recebidos = receber_mensagem_protocolo(sock_servidor, &tipo, payload);
        if (bytes_recebidos <= 0) {
            if (cliente_rodando && conectado_ao_servidor) {
                adicionar_mensagem("[SISTEMA] Conexao com o servidor perdida.");
                conectado_ao_servidor = 0;
                destruir_lista_local();
                tela_precisa_atualizar = 1;
            }
            break;
        }
        if (tipo == 'L') processar_e_atualizar_lista(payload);
        if (tipo == 'B') adicionar_mensagem("[SISTEMA] acorda povo!!!");

    }
    return NULL;
}

void tratar_envio_direto() {
    menu_pausado = 1;
    #ifndef _WIN32
        restaurar_terminal();
    #endif
    limpar_tela();
    printf("--- Envio de Mensagem Direta (DM) ---\n\n");
    printf("Usuarios disponíveis para contato:\n");
    pthread_mutex_lock(&mutex_lista);
    Usuario *user_iter = lista_usuarios;
    int users_found = 0;
    while(user_iter != NULL) {
        if(strcmp(user_iter->nome, meu_nome) != 0) {
            printf("- %s\n", user_iter->nome);
            users_found++;
        }
        user_iter = user_iter->prox;
    }
    if (users_found == 0) {
        printf("Nenhum outro usuario online no momento.\n");
    }
    pthread_mutex_unlock(&mutex_lista);
    printf("----------------------------------\n");
    char nome_destino[50];
    printf("\nDigite o nome do destinatario (ou 'cancelar' para voltar): ");
    scanf("%49s", nome_destino);
    if (strcmp(nome_destino, "cancelar") != 0) {
        Usuario* alvo = encontrar_usuario(nome_destino);
        if (alvo == NULL || strcmp(alvo->nome, meu_nome) == 0) {
            printf("\n[ERRO] Usuario invalido ou nao encontrado.\n Pressione Enter para voltar...");
            while (getchar() != '\n'); getchar();
        } else {
            char mensagem[TAM_PAYLOAD - 60];
            printf("Digite a sua mensagem para %s: ", nome_destino);
            while (getchar() != '\n');
            fgets(mensagem, sizeof(mensagem), stdin);
            mensagem[strcspn(mensagem, "\n")] = 0;
            char payload_final[TAM_PAYLOAD];
            snprintf(payload_final, TAM_PAYLOAD, "%s|%s|", meu_nome, mensagem);
            int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
            if (sock_p2p >= 0) {
                struct sockaddr_in endereco_alvo;
                memset(&endereco_alvo, 0, sizeof(endereco_alvo));
                endereco_alvo.sin_family = AF_INET;
                endereco_alvo.sin_addr.s_addr = inet_addr(alvo->ip);
                endereco_alvo.sin_port = htons(alvo->porta);
                if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) >= 0) {
                    enviar_mensagem_protocolo(sock_p2p, 'M', payload_final);
                    adicionar_mensagem("[SISTEMA] Mensagem enviada.");
                } else {
                    adicionar_mensagem("[SISTEMA] ERRO: Não foi possível conectar com o usuário.");
                }
                close_socket(sock_p2p);
            }
        }
    }
    #ifndef _WIN32
        configurar_terminal_nao_bloqueante();
    #endif
    menu_pausado = 0;
    tela_precisa_atualizar = 1;
}

void tratar_envio_broadcast() {
    menu_pausado = 1;
    #ifndef _WIN32
        restaurar_terminal();
    #endif
    limpar_tela();
    printf("--- Envio de Mensagem para Todos (Broadcast) ---\n");
    char mensagem[TAM_PAYLOAD - 60];
    printf("Digite a sua mensagem: ");
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
                  close_socket(sock_p2p);
              }
        }
        atual = atual->prox;
    }
    pthread_mutex_unlock(&mutex_lista);
    adicionar_mensagem("[SISTEMA] Mensagem de broadcast enviada.");
    #ifndef _WIN32
        configurar_terminal_nao_bloqueante();
    #endif
    menu_pausado = 0;
    tela_precisa_atualizar = 1;
}

void redesenhar_tela_principal() {
    limpar_tela();
    printf("--- CHAT CARAMELO ---\n");
    printf("Logado como: %s\n\n", meu_nome);
    printf("USUARIOS ONLINE:\n");
    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    if (atual == NULL) {
        printf("Nenhum usuario conectado.\n");
    } else {
        int count = 0;
        while(atual != NULL) {
            printf("%s", atual->nome);
            count++;
            if (count % 4 == 0 || atual->prox == NULL) printf("\n");
            else printf(" | ");
            atual = atual->prox;
        }
    }
    pthread_mutex_unlock(&mutex_lista);
    printf("-----------------------------------------------------------\n\n");
    printf("ULTIMAS MENSAGENS:\n");
    pthread_mutex_lock(&mutex_mensagens);
    if (total_mensagens_armazenadas == 0) {
        printf("Nenhuma mensagem ainda.\n");
    } else {
        int i = 0;
        int start_index = (indice_proxima_mensagem - total_mensagens_armazenadas + MAX_ULTIMAS_MENSAGENS) % MAX_ULTIMAS_MENSAGENS;
        for (i = 0; i < total_mensagens_armazenadas; i++) {
            printf("  %s\n", ultimas_mensagens[(start_index + i) % MAX_ULTIMAS_MENSAGENS]);
        }
    }
    pthread_mutex_unlock(&mutex_mensagens);
    printf("-----------------------------------------------------------\n");
    if (novas_mensagens_na_pausa > 0) {
        printf("[!] Existem %d novas mensagens!\n", novas_mensagens_na_pausa);
        novas_mensagens_na_pausa = 0;
    }
    printf("--- MENU ---\n");
    printf("2 - Desconectar\n");
    printf("3 - Enviar Broadcast\n");
    printf("4 - Enviar Mensagem Individual\n");
    printf("------------\n");
    printf("Escolha uma opcao: ");
    fflush(stdout);
}

void tratar_entrada_usuario(char c) {
    switch (c) {
        case '2': desconectar_do_servidor(); break;
        case '3': tratar_envio_broadcast(); break;
        case '4': tratar_envio_direto(); break;
    }
}

void exibir_menu_desconectado() {
    limpar_tela();
    printf("\n--- CHAT CARAMELO ---\n    DESCONECTADO\n---------------------\n");
    printf("1 - Conectar\n5 - Sair\n---------------------\nEscolha uma opcao: ");
}

void conectar_ao_servidor() {
    if (conectado_ao_servidor) return;
    menu_pausado = 1;
    limpar_tela();
    char ip_servidor[16];
    printf("\nDigite o IP do Servidor (padrao: 127.0.0.1): ");
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
    if (fgets(ip_servidor, sizeof(ip_servidor), stdin) != NULL) {
        ip_servidor[strcspn(ip_servidor, "\n")] = 0;
        if (ip_servidor[0] == '\0') {
            strcpy(ip_servidor, "127.0.0.1");
            printf("Nenhum IP digitado. Usando o padrao: %s\n", ip_servidor);
        }
    } else {
        strcpy(ip_servidor, "127.0.0.1");
        printf("Entrada inválida. Usando o padrao: %s\n", ip_servidor);
    }
    printf("Digite o seu nome de usuario: ");
    scanf("%49s", meu_nome);
    printf("Digite a porta P2P (0 para padrao %d): ", DEFAULT_P2P_PORT);
    scanf("%d", &minha_porta_p2p);
    if (minha_porta_p2p == 0) minha_porta_p2p = DEFAULT_P2P_PORT;
    strcpy(meu_ip, "127.0.0.1");
    if ((sock_servidor = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n[ERRO] Falha ao criar socket.\n"); return;
    }
    struct sockaddr_in endereco_servidor;
    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = inet_addr(ip_servidor);
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR_TCP);
    if (connect(sock_servidor, (struct sockaddr *)&endereco_servidor, sizeof(endereco_servidor)) < 0) {
        printf("\n[ERRO] Falha ao conectar ao servidor %s.\n", ip_servidor);
        close_socket(sock_servidor);
        menu_pausado = 0;
        return;
    }
    char payload_registro[200];
    snprintf(payload_registro, 200, "%s|%d|%s|", meu_ip, minha_porta_p2p, meu_nome);
    enviar_mensagem_protocolo(sock_servidor, 'R', payload_registro);
    char tipo_ack, payload_ack[TAM_PAYLOAD];
    if (receber_mensagem_protocolo(sock_servidor, &tipo_ack, payload_ack) <= 0 || tipo_ack != 'A') {
        printf("Falha ao registrar no servidor. Resposta: '%c'.\n", tipo_ack);
        close_socket(sock_servidor);
        menu_pausado = 0;
        printf("Pressione Enter para continuar...");
        while(getchar()!='\n'); getchar();
        return;
    }
    conectado_ao_servidor = 1;
    adicionar_mensagem("[SISTEMA] Conectado e registrado com sucesso!");
    pthread_create(&p2p_thread_id, NULL, thread_recebimento_p2p, NULL);
    pthread_create(&server_thread_id, NULL, thread_recebimento_servidor, NULL);
    #ifndef _WIN32
        configurar_terminal_nao_bloqueante();
    #endif
    menu_pausado = 0;
    tela_precisa_atualizar = 1;
}

void desconectar_do_servidor() {
    if (!conectado_ao_servidor) return;
    #ifndef _WIN32
        restaurar_terminal();
    #endif
    enviar_mensagem_protocolo(sock_servidor, 'D', meu_nome);
    conectado_ao_servidor = 0;
    #ifdef _WIN32
        shutdown(sock_servidor, SD_BOTH);
    #endif
    close_socket(sock_servidor);
    destruir_lista_local();
    limpar_tela();
    printf("Desconectado com sucesso.\n");
}

// --- FUNÇÃO MAIN ---
int main(void) {
    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #else
        tcgetattr(STDIN_FILENO, &estado_original_terminal);
        atexit(restaurar_terminal);
    #endif
    while (cliente_rodando) {
        if (conectado_ao_servidor) {
            pthread_mutex_lock(&mutex_tela);
            if (tela_precisa_atualizar && !menu_pausado) {
                redesenhar_tela_principal();
                tela_precisa_atualizar = 0;
            }
            pthread_mutex_unlock(&mutex_tela);
            #ifdef _WIN32
                if (_kbhit()) {
                    char c = _getch();
                    tratar_entrada_usuario(c);
                }
                Sleep(100);
            #else
                struct timeval tv = {0L, 100000L};
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                    char c = getchar();
                    tratar_entrada_usuario(c);
                }
            #endif
        } else {
            exibir_menu_desconectado();
            int escolha = 0;
            if (scanf("%d", &escolha) != 1) {
                while(getchar() != '\n');
                continue;
            }
            if (escolha == 1) {
                conectar_ao_servidor();
            } else if (escolha == 5) {
                cliente_rodando = 0;
            }
        }
    }
    printf("\nEncerrando e aguardando threads...\n");
    cliente_rodando = 0;
    // As threads só são criadas se a conexão for bem-sucedida
    if(conectado_ao_servidor) {
        if(p2p_thread_id) pthread_join(p2p_thread_id, NULL);
        if(server_thread_id) pthread_join(server_thread_id, NULL);
    }
    printf("Programa encerrado.\n");
    #ifdef _WIN32
        WSACleanup();
    #endif
    return 0;
}
