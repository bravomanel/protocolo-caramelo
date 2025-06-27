// No codeblocks inclua no menu em: Project -> Build Options... -> Linker settings -> Other link options -l wsock32
//#define WIN // Se não for no windows comente essa linha e compile no terminal: gcc -o ts ts.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#define TAM_MENSAGEM 255     /* mensagem de maior tamanho */

#define PORTA_SERVIDOR_TCP 9999

#define MAXPENDING 5    /* Número máximo de requisições para conexão pendentes */

typedef struct usuario{
    char endereco[16];
    char nome[30];
    char porta[5];
    struct usuario *prox;
} usuario;

usuario *users;

usuario *criar_usuario(const char* address, const char* name, const char* port) {
    usuario* novo = (usuario*)malloc(sizeof(usuario));
    if (novo == NULL) {
        perror("Erro ao alocar memória");
        exit(1);
    }
    strncpy(novo->endereco, address, sizeof(novo->endereco) - 1);
    novo->endereco[sizeof(novo->endereco) - 1] = '\0';

    strncpy(novo->nome, name, sizeof(novo->nome) - 1);
    novo->nome[sizeof(novo->nome) - 1] = '\0';

    strncpy(novo->porta, port, sizeof(novo->porta) - 1);
    novo->porta[sizeof(novo->porta) - 1] = '\0';

    novo->prox = NULL;
    return novo;
}

usuario *inserir_na_lista(usuario *lista, usuario *novo) {
    novo->prox = lista;
    return novo;
}


usuario *remover_da_lista(usuario *lista, const char *name) {
    usuario *atual = lista, *anterior = NULL;

    while (atual != NULL && strcmp(atual->nome, name) != 0) {
        anterior = atual;
        atual = atual->prox;
    }

    if (atual == NULL) {
        printf("Usuário \"%s\" não encontrado.\n", name);
        return lista;
    }

    if (anterior == NULL) {
        // Removendo o primeiro nó
        lista = atual->prox;
    } else {
        anterior->prox = atual->prox;
    }

    free(atual);
    printf("Usuário \"%s\" removido com sucesso.\n", name);
    return lista;
}

void imprimir_lista(usuario *lista) {
    usuario *atual = lista;
    while (atual != NULL) {
        //printf("Nome: %s | Endereço: %s | Porta: %s\n", atual->nome, atual->endereco, atual->porta);
        printf("%s:%s:%s|", atual->nome, atual->endereco, atual->porta);
        atual = atual->prox;
    }
}

void destruir_lista(usuario*lista) {
    usuario *atual;
    while (lista != NULL) {
        atual = lista;
        lista = lista->prox;
        free(atual);
    }
}

int criar_socket(int porta)
{
    int sock;
    struct sockaddr_in endereco; /* Endereço Local */

    /* Criação do socket TCP para recepção e envio de pacotes */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\nErro na criação do socket!\n");fflush(stdout);
        return(-1);
    }

    if (porta > 0)
    {
        /* Construção da estrutura de endereço local */
        memset(&endereco, 0, sizeof(endereco));       /* Zerar a estrutura */
        endereco.sin_family      = AF_INET;           /* Família de endereçamento da Internet */
        endereco.sin_addr.s_addr = htonl(INADDR_ANY); /* Qualquer interface de entrada */
        endereco.sin_port        = htons(porta);      /* Porta local */

        /* Instanciar o endereco local */
        if (bind(sock, (struct sockaddr *) &endereco, sizeof(endereco)) < 0)
        {
           printf("\nErro no bind()!\n");fflush(stdout);
           return(-1);
        }

        /* Indica que o socket escutara as conexões */
        if (listen(sock, MAXPENDING) < 0)
        {
           printf("\nErro no listen()!\n");fflush(stdout);
           return(-1);
        }

    }

    return(sock);
}

int aceitar_conexao(int sock)
{
    int                socket_cliente;
    struct sockaddr_in endereco; /* Endereço Local */
    int                tamanho_endereco;

    /* Define o tamanho do endereço de recepção e envio */
    tamanho_endereco = sizeof(endereco);

    /* Aguarda pela conexão de um cliente */
    if ((socket_cliente = accept(sock, (struct sockaddr *) &endereco, &tamanho_endereco)) < 0)
    {
        printf("\nErro no accept()!\n");fflush(stdout);
        return(0);
    }
    return(socket_cliente);
}

int receber_mensagem(char *mensagem,int sock)
{
    /* Limpar o buffer da mensagem */
    memset((void *) mensagem,(int) NULL,TAM_MENSAGEM);

    /* Espera pela recepção de alguma mensagem do cliente conectado*/
    if (recv(sock, mensagem, TAM_MENSAGEM, 0) < 0)
    {
        printf("\nErro na recepção da mensagem\n");fflush(stdout);
        return(-1);
    }
    
    processar_mensagem(mensagem, sock);
    
    printf("\nTCP Servidor: Recebi (%s)\n",mensagem);fflush(stdout);

    return(0);
}

int enviar_mensagem(char *mensagem,int sock)
{
    /* Devolve o conteúdo da mensagem para o cliente */
    if (send(sock, mensagem, strlen(mensagem), 0) != strlen(mensagem))
    {
        printf("\nErro no envio da mensagem\n");fflush(stdout);
        return(-1);
    }

    printf("\nTCP Servidor: Enviei (%s)\n",mensagem);fflush(stdout);

    return(0);
}

void processar_mensagem(char *msg, int sock){

    char tipo; //tipo da mensagem
    tipo = msg[0];
    
    switch(tipo){
      case 'R':
       registrar(msg);
       break;
      case 'D':
	desconectar(msg);
	break;
      case 'L':
        //listar(msg, sock);
        break;
      case 'M':
        //mensagem_direta();
        break;
      case 'B':
        //mensagem_broadcast();
        break;
    }
       
    return;
}

void registrar(char *msg){
    //R192.168.10.20|25565|Charles|

    char *tkn;
    char copia[TAM_MENSAGEM];
    char ip[16];
    char porta[6];
    char nome_user[31];

    memset(copia, 0, sizeof(copia));
    strncpy(copia, msg, sizeof(copia) - 1);

    // Pega IP (com 'R')
    tkn = strtok(copia, "|");
    
    // Remove 'R' do começo
    if (tkn[0] == 'R') {
        tkn++; // avança o ponteiro para ignorar o 'R'
    }

    strncpy(ip, tkn, sizeof(ip) - 1);
    ip[sizeof(ip) - 1] = '\0';

    // Porta
    tkn = strtok(NULL, "|");
    strncpy(porta, tkn, sizeof(porta) - 1);
    porta[sizeof(porta) - 1] = '\0';

    // Nome
    tkn = strtok(NULL, "|");
    strncpy(nome_user, tkn, sizeof(nome_user) - 1);
    nome_user[sizeof(nome_user) - 1] = '\0';

    // Tamanho (em string)
    char tam[4];
    snprintf(tam, sizeof(tam), "%03lu", strlen(nome_user));

    // Construir resposta
    char resposta[TAM_MENSAGEM + 4];
    snprintf(resposta, sizeof(resposta), "%s%s", tam, msg);

    printf("tam = %s\nip = %s\nporta = %s\nnome = %s\n", tam, ip, porta, nome_user);
    printf("resposta = %s\n", resposta);

    //users = inserir_na_lista(users, criar_usuario(ip, nome_user, porta));
}

void desconectar(char *msg) {
    //DCharles|"

    char nome_user[31];
    char tam[4];

    // Pular o 'D' e copiar o restante até o '|'
    char *inicio_nome = msg + 1;
    char *fim = strchr(inicio_nome, '|'); // busca o '|'

    if (fim == NULL) {
        printf("Erro: formato inválido de mensagem.\n");
        return;
    }

    size_t len = fim - inicio_nome; // tamanho do nome

    // Segurança: limitar ao tamanho do buffer
    if (len >= sizeof(nome_user)) {
        printf("Erro: nome muito grande.\n");
        return;
    }

    strncpy(nome_user, inicio_nome, len);
    nome_user[len] = '\0'; // garantir término da string

    // Formatar tam como string de 3 dígitos com zero à esquerda
    snprintf(tam, sizeof(tam), "%03lu", strlen(nome_user));
    
    char resposta[4 + strlen(msg) + 1]; // 3 dígitos + msg + '\0'
    snprintf(resposta, sizeof(resposta), "%s%s", tam, msg);

    // Mostrar resultados
    printf("tam = %s\nnome = %s\n", tam, nome_user);
    printf("resposta = %s\n", resposta);

    //users = remover_da_lista(users, nome_user);
}

void listar(char *msg, int sock){

}

int main()
{
    int                sock;                   /* Socket */
    int                socket_cliente;         /* Socket de conexão com o cliente */
    int                resultado;              /* Resultado das funções */
    char               mensagem[TAM_MENSAGEM]; /* Buffer para a recepção da string de echo */
#ifdef WIN
    WORD wPackedValues;
    WSADATA  SocketInfo;
    int      nLastError,
	         nVersionMinor = 1,
	         nVersionMajor = 1;
    wPackedValues = (WORD)(((WORD)nVersionMinor)<< 8)|(WORD)nVersionMajor;
    nLastError = WSAStartup(wPackedValues, &SocketInfo);
#endif

    sock = criar_socket(PORTA_SERVIDOR_TCP);
    if (sock < 0)
    {
        printf("\nErro na criação do socket!\n");
        return(1);
    }

    for (;;) /* Loop eterno */
    {
        /* Aguarda por uma conexão e a aceita criando o socket de contato com o cliente */
        socket_cliente = aceitar_conexao(sock);
        if (socket_cliente == 0)
        {
            printf("\nErro na conexao do socket!\n");
            return(1);
        }

        /* Recebe a mensagem do cliente */
        resultado = receber_mensagem(mensagem,socket_cliente);
        if (resultado < 0)
        {
            printf("\nErro no recebimento da mensagem\n");
            return(1);
        }

        /* Devolve o conteúdo da mensagem para o cliente */
        resultado = enviar_mensagem(mensagem,socket_cliente);
        if (resultado < 0)
        {
            printf("\nErro no envio da mensagem\n");
            return(1);
        }
        
        close(socket_cliente);    /* Fecha o socket do cliente */
    }
    /*não passa por aqui */
}
