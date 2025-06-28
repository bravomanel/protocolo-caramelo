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

#define TAM_MENSAGEM 256
#define PORTA_SERVIDOR_TCP 9999
#define DEFAULT_P2P_PORT 5000

// --- ESTRUTURA PARA ARMAZENAR USUÁRIOS LOCALMENTE ---
typedef struct Usuario {
    char nome[50];
    char ip[16];
    int porta;
    struct Usuario *prox;
} Usuario;

// --- VARIÁVEIS GLOBAIS ---
int sock_servidor;
char meu_nome[50];
char meu_ip[16]; // IP que será informado ao servidor
int minha_porta_p2p;
int cliente_rodando = 1; // Flag para controlar a execução das threads

Usuario *lista_usuarios = NULL; // Ponteiro para a lista local de usuários
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER; // Mutex para proteger a lista

// --- FUNÇÕES DE GERENCIAMENTO DA LISTA LOCAL ---

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
    // Formato esperado: LNome1:IP1:Porta1|Nome2:IP2:Porta2|...
    destruir_lista_local(); // Limpa a lista antiga antes de popular com a nova

    char *payload = lista_str + 1; // Pula o caractere 'L'
    char *token_usuario = strtok(payload, "|");

    while (token_usuario != NULL) {
        Usuario *novo = (Usuario*)malloc(sizeof(Usuario));
        if (novo) {
            char *nome = strtok(token_usuario, ":");
            char *ip = strtok(NULL, ":");
            char *porta_str = strtok(NULL, ":");

            if (nome && ip && porta_str) {
                strcpy(novo->nome, nome);
                strcpy(novo->ip, ip);
                novo->porta = atoi(porta_str);
                novo->prox = NULL;

                // Adiciona na lista
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
    printf("\n[INFO] Lista de usuários atualizada.\n");
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

// --- FUNÇÕES DE AÇÕES DO MENU ---

void tratar_envio_direto() {
    char nome_destino[50];
    char mensagem[TAM_MENSAGEM];

    printf("\nDigite o nome do destinatário: ");
    scanf("%s", nome_destino);

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
    while (getchar() != '\n'); // Limpa buffer
    fgets(mensagem, TAM_MENSAGEM, stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;

    int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_p2p < 0) {
        printf("[ERRO] Falha ao criar socket P2P.\n");
        return;
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
    // Formato: M<REMETENTE>|<MENSAGEM>|
    snprintf(buffer_final, TAM_MENSAGEM, "M%s|%s|", meu_nome, mensagem);

    send(sock_p2p, buffer_final, strlen(buffer_final), 0);
    printf("\n[INFO] Mensagem enviada para %s.\n", nome_destino);
    close(sock_p2p);
}

void tratar_envio_broadcast() {
    char mensagem[TAM_MENSAGEM];
    printf("\nDigite a sua mensagem para todos: ");
    while (getchar() != '\n');
    fgets(mensagem, TAM_MENSAGEM, stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;

    pthread_mutex_lock(&mutex_lista);
    Usuario *atual = lista_usuarios;
    while(atual != NULL) {
        // Não envia para si mesmo
        if (strcmp(atual->nome, meu_nome) != 0) {
            // Reutiliza a lógica de envio direto
            int sock_p2p = socket(PF_INET, SOCK_STREAM, 0);
            if (sock_p2p >= 0) {
                struct sockaddr_in endereco_alvo;
                memset(&endereco_alvo, 0, sizeof(endereco_alvo));
                endereco_alvo.sin_family = AF_INET;
                endereco_alvo.sin_addr.s_addr = inet_addr(atual->ip);
                endereco_alvo.sin_port = htons(atual->porta);

                if (connect(sock_p2p, (struct sockaddr *)&endereco_alvo, sizeof(endereco_alvo)) >= 0) {
                    char buffer_final[TAM_MENSAGEM];
                    // Formato: B<REMETENTE>|<MENSAGEM>|
                    snprintf(buffer_final, TAM_MENSAGEM, "B%s|%s|", meu_nome, mensagem);
                    send(sock_p2p, buffer_final, strlen(buffer_final), 0);
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
    char buffer_final[TAM_MENSAGEM];
    printf("\nDesconectando do servidor...\n");

    // Formato: D<NOME>|
    snprintf(buffer_final, TAM_MENSAGEM, "D%s|", meu_nome);
    send(sock_servidor, buffer_final, strlen(buffer_final), 0);
    
    cliente_rodando = 0; // Sinaliza para as threads terminarem
    close(sock_servidor);
    destruir_lista_local();
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

// --- THREAD DE RECEBIMENTO P2P ---
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
    printf("[INFO] Thread de recebimento iniciada. Ouvindo na porta %d\n", minha_porta_p2p);

    while (cliente_rodando) {
        int sock_peer = accept(sock_listen_p2p, NULL, NULL);
        if (sock_peer < 0) {
            if (cliente_rodando) printf("[ERRO] accept() na thread P2P falhou.\n");
            continue;
        }

        char buffer[TAM_MENSAGEM];
        memset(buffer, 0, TAM_MENSAGEM);
        recv(sock_peer, buffer, TAM_MENSAGEM, 0);

        if (strlen(buffer) > 0) {
            char tipo = buffer[0];
            char *payload = buffer + 1;
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
        // Exibe o menu novamente para o usuário não perder o prompt
        printf("Escolha uma opção: ");
        fflush(stdout);
    }
    
    close(sock_listen_p2p);
    printf("[INFO] Thread de recebimento encerrada.\n");
    return NULL;
}

// --- FUNÇÃO MAIN ---
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
    strcpy(meu_ip, "127.0.0.1"); // Simplificação. Para redes reais, seria necessário um método mais robusto.

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

    // 1. Enviar mensagem de REGISTRO ('R') para o servidor
    char msg_registro[TAM_MENSAGEM];
    snprintf(msg_registro, TAM_MENSAGEM, "R%s|%d|%s|", meu_ip, minha_porta_p2p, meu_nome);
    send(sock_servidor, msg_registro, strlen(msg_registro), 0);

    // 2. Receber a lista inicial de usuários ('L') do servidor (ou uma confirmação 'A')
    // Nota: Seu servidor atual não envia a lista no registro, ele só processa.
    // Esta parte precisaria de um ajuste no servidor para ser funcional.
    // Por enquanto, vamos assumir que recebemos e processamos a lista.
    char buffer_resposta[TAM_MENSAGEM];
    memset(buffer_resposta, 0, TAM_MENSAGEM);
    // recv(sock_servidor, buffer_resposta, TAM_MENSAGEM, 0); // Descomente se o servidor enviar a lista
    // if(buffer_resposta[0] == 'L') {
    //    processar_e_atualizar_lista(buffer_resposta);
    // }
    
    // 3. Criar a thread para recebimento de mensagens P2P
    pthread_t receiver_thread_id;
    if (pthread_create(&receiver_thread_id, NULL, thread_recebimento, NULL) != 0) {
        printf("[ERRO FATAL] Falha ao criar a thread de recebimento.\n");
        return 1;
    }
    
    // 4. Loop do menu principal
    int escolha = 0;
    while (cliente_rodando && escolha != 4) {
        exibir_menu();
        if (scanf("%d", &escolha) != 1) { // Proteção contra input não numérico
            escolha = 0; // Reseta a escolha
            while(getchar() != '\n'); // Limpa o buffer de entrada
        }

        switch (escolha) {
            case 1: tratar_envio_direto(); break;
            case 2: tratar_envio_broadcast(); break;
            case 3: 
                printf("\n--- USUARIOS ONLINE ---\n");
                imprimir_lista_local();
                printf("-----------------------\n");
                break;
            case 4: tratar_desconexao(); break;
            default:
                printf("\nOpção inválida! Tente novamente.\n");
                break;
        }
    }

    // Espera a thread de recebimento terminar
    pthread_join(receiver_thread_id, NULL);
    printf("Programa encerrado.\n");

    #ifdef WIN
        WSACleanup();
    #endif

    return 0;
}