// No Code::Blocks, inclua a biblioteca pthread para threads:
// Project -> Build Options... -> Linker settings -> Other link options: -lwsock32 -lpthread
// Se não usar threads, apenas -lwsock32 é necessário por enquanto.

// #define WIN // Descomente para compilar no Windows
#ifdef WIN
#include <winsock2.h>
#include <windows.h> // Necessário para threads no Windows
#include <process.h> // Para _beginthreadex
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h> // Biblioteca de threads para Linux/macOS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAM_MENSAGEM 256
#define PORTA_SERVIDOR_TCP 9999
#define DEFAULT_P2P_PORT 5000
#define MAXPENDING 5

// Variáveis globais para serem acessíveis pelas threads
int sock_servidor;
char meu_nome[50];
int minha_porta_p2p;

/*
    Aqui vamos colocar as funções que tratam as ações do menu.
    Elas serão chamadas pelo loop no main.
*/

void tratar_envio_direto()
{
    char nome_destino[50];
    char mensagem[TAM_MENSAGEM];
    char buffer_final[TAM_MENSAGEM];

    printf("\nDigite o nome do destinatário: ");
    scanf("%s", nome_destino);
    printf("Digite a sua mensagem: ");
    // Limpa o buffer de entrada antes de ler a mensagem completa
    while (getchar() != '\n')
        ;
    fgets(mensagem, TAM_MENSAGEM, stdin);
    // Remove o \n que o fgets adiciona
    mensagem[strcspn(mensagem, "\n")] = 0;

    printf("\n[DEBUG] Preparando para enviar '%s' para '%s'\n", mensagem, nome_destino);

    // TODO:
    // 1. Procurar na sua lista local de usuários o IP e a Porta do 'nome_destino'.
    // 2. Criar um NOVO socket temporário.
    // 3. Conectar nesse socket com o IP/Porta do destinatário.
    // 4. Montar a mensagem no formato do protocolo: <TAMANHO>M<SEU_NOME>|<MENSAGEM>|
    //    Exemplo: sprintf(buffer_final, "%03dM%s|%s|", (int)strlen(payload), meu_nome, mensagem);
    // 5. Enviar a mensagem com send().
    // 6. Fechar o socket temporário com close().
}

void tratar_envio_broadcast()
{
    char mensagem[TAM_MENSAGEM];

    printf("\nDigite a sua mensagem para todos: ");
    while (getchar() != '\n')
        ;
    fgets(mensagem, TAM_MENSAGEM, stdin);
    mensagem[strcspn(mensagem, "\n")] = 0;

    printf("\n[DEBUG] Preparando para enviar '%s' para TODOS\n", mensagem);

    // TODO:
    // 1. Para cada usuário na sua lista local (exceto você mesmo):
    //    a. Fazer exatamente o mesmo processo da função tratar_envio_direto().
    //    b. Ou seja, um loop que cria socket, conecta, envia e fecha para cada usuário.
}

void tratar_desconexao()
{
    char buffer_final[TAM_MENSAGEM];
    printf("\nDesconectando do servidor...\n");

    // TODO:
    // 1. Montar a mensagem de desconexão: <TAMANHO>D<SEU_NOME>|
    //    Exemplo: sprintf(buffer_final, "%03dD%s|", (int)strlen(meu_nome)+1, meu_nome);
    // 2. Enviar a mensagem para o SERVIDOR usando o 'sock_servidor'.
    // 3. Fechar a conexão principal: close(sock_servidor).

    // Por enquanto, apenas fechamos
    close(sock_servidor);
}

void exibir_menu()
{
    // system("clear"); // ou "cls" no Windows
    printf("\n--- CHAT CARAMELO ---\n");
    printf("Logado como: %s\n", meu_nome);
    printf("---------------------\n");
    printf("1 - Enviar mensagem direta (DM)\n");
    printf("2 - Enviar mensagem para todos (Broadcast)\n");
    printf("3 - Listar usuários online\n");
    printf("4 - Sair\n");
    printf("Escolha uma opção: ");
}

// AINDA NÃO IMPLEMENTADO - AQUI FICARÁ A LÓGICA DE RECEBIMENTO P2P
void *thread_recebimento(void *arg)
{
    printf("[THREAD] Thread de recebimento iniciada. Ouvindo na porta %d\n", minha_porta_p2p);

    // TODO:
    // 1. Criar um socket de servidor (igual ao do programa servidor) para ouvir na 'minha_porta_p2p'.
    // 2. Entrar em um loop infinito:
    //    a. Chamar accept() para esperar uma conexão de outro cliente.
    //    b. Quando receber uma conexão, chamar recv() para ler a mensagem.
    //    c. Processar a mensagem (se for 'M' ou 'B', exibir na tela).
    //    d. Fechar o socket da conexão do cliente com close().
    //    e. Voltar para o accept().

    return NULL;
}

int main(int argc, char *argv[])
{
#ifdef WIN
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        printf("Erro ao iniciar o Winsock.\n");
        return 1;
    }
#endif

    char *ip_servidor;
    char meu_nome[50];
    int minha_porta_p2p;

    if (argc == 3)
    { // Usuário usa a porta padrao
        ip_servidor = argv[1];
        strcpy(meu_nome, argv[2]);
        minha_porta_p2p = DEFAULT_P2P_PORT;
        printf("Porta P2P não especificada, usando padrão: %d\n", minha_porta_p2p);
    }
    else if (argc == 4)
    { // Usuário especificou a porta
        ip_servidor = argv[1];
        strcpy(meu_nome, argv[2]);
        minha_porta_p2p = atoi(argv[3]);
        printf("Usando porta P2P especificada: %d\n", minha_porta_p2p);
    }
    else
    { // Número incorreto de argumentos
        printf("Uso: %s <IP Servidor> <Seu Nome> [Porta P2P Opcional]\n", argv[0]);
        return 1;
    }

    if ((sock_servidor = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Erro no socket()\n");
        return 1;
    }

    struct sockaddr_in endereco_servidor;
    memset(&endereco_servidor, 0, sizeof(endereco_servidor));
    endereco_servidor.sin_family = AF_INET;
    endereco_servidor.sin_addr.s_addr = inet_addr(ip_servidor);
    endereco_servidor.sin_port = htons(PORTA_SERVIDOR_TCP);

    if (connect(sock_servidor, (struct sockaddr *)&endereco_servidor, sizeof(endereco_servidor)) < 0)
    {
        printf("Erro no connect() ao servidor principal\n");
        return 1;
    }

    printf("Conectado ao servidor! Registrando...\n");

    // TODO:
    // 2. Enviar mensagem de REGISTRO ('R') para o servidor
    // char msg_registro[TAM_MENSAGEM];
    // sprintf(msg_registro, "%03dR%s|%d|%s|", ...); // Montar a msg 'R'
    // send(sock_servidor, msg_registro, strlen(msg_registro), 0);

    // TODO:
    // 3. Receber a lista inicial de usuários ('L') do servidor
    // recv(sock_servidor, buffer_lista, TAM_MENSAGEM, 0);
    // processar_lista(buffer_lista);

    // TODO: (Passo futuro)
    // 4. Criar a thread para recebimento de mensagens P2P
    // pthread_t receiver_thread_id;
    // pthread_create(&receiver_thread_id, NULL, thread_recebimento, NULL);

    // 5. Loop do menu principal
    int escolha = 0;
    while (escolha != 4)
    {
        exibir_menu();
        scanf("%d", &escolha);

        switch (escolha)
        {
        case 1:
            tratar_envio_direto();
            break;
        case 2:
            tratar_envio_broadcast();
            break;
        case 3:
            printf("\n--- USUARIOS ONLINE ---\n");
            // TODO: Imprimir a lista de usuários que você tem armazenada localmente.
            printf("Funcionalidade ainda não implementada.\n");
            printf("-----------------------\n");
            break;
        case 4:
            tratar_desconexao();
            printf("Até mais!\n");
            break;
        default:
            printf("\nOpção inválida! Tente novamente.\n");
            // Limpa o buffer de entrada para evitar loops infinitos se o usuário digitar uma letra
            while (getchar() != '\n')
                ;
            break;
        }
    }

#ifdef WIN
    WSACleanup();
#endif

    return 0;
}