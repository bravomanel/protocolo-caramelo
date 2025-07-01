# protocolo-caramelo
 
## Trabalho de Tópicos Especiais em Redes de Computadores 2025/1

Desenvolver um protocolo de comunicação entre computadores que envie mensagens diretamente, usando o P2P, usando um servidor central apenas para armazenar os usuários ativos.

## Integrantes

- Emanuel Bravo
- Gabriel Moura
- Gabriel Silva
- Marcus Bispo
- Myllene Couto
- Victor Coutinho


O objetivo é desenvolver um sistema de comunicação que implementa o Protocolo Caramelo. A arquitetura é híbrida:

Servidor Central: Atua como um "servidor de entrada", gerenciando uma lista de usuários ativos. Ele não participa da troca de mensagens diretas entre os clientes. Suas principais funções são registrar novos usuários, remover usuários que se desconectam e fornecer a lista de usuários ativos para os clientes.

Clientes (Pares): Os clientes se conectam ao servidor para se registrar e obter a lista de outros usuários online. A partir daí, a comunicação de mensagens diretas (M) e em broadcast (B) ocorre diretamente entre os clientes (peer-to-peer), sem passar pelo servidor, aliviando a carga do mesmo.


## Para fazer: 

1. Programa Servidor: 
    2. Armazenar usuários ativos
    3. Enviar lista para usuários
    4. Receber cadastro de usuários
    5. Receber desconxão de usuários
 2. Programa Cliente:
    1. Enviar mensagens para outros usuários
    2. Receber mensagens de outros usuários
    3. Enviar cadastro de usuário
    4. Enviar desconexão de usuário
    5. Receber lista de usuários ativos
    6. Enviar mensagem broadcast para todos os usuários
 3. Coisas para se preocupar:
    1. Conexões simultâneas
    2. Tratamento de erros
    3. Receber mensagens de outros usuários simultaneamente
    4. Receber mensagens enquanto está digitando
    5. Quantas Threads ? 



Checklist
Aqui está a lista de tarefas pra gente ir marcando o que já foi feito.

Servidor (servidor.c)

[X] 1. Estrutura Básica:
   [X] Criar o socket, fazer o bind com a porta e botar pra escutar (listen).
   [X] Deixar ele rodando em um loop infinito esperando gente conectar (accept).

[X] 2. Gerenciamento de Usuários:
   [X] Criar uma struct pra guardar os dados do usuário (nome, ip, porta, socket).
   [X] Ter uma lista/array global pra guardar todo mundo que tá online.

[X] 3. Lidar com Múltiplos Clientes (Threads!):
   [X] A cada accept(), criar uma nova thread pra cuidar daquele cliente.
   [X] A thread principal fica livre, só pra aceitar novas conexões.

[X] 4. Sincronização (Mutex!):
   [X] Proteger a lista de usuários com pthread_mutex pra não dar problema quando vários clientes conectarem/desconectarem ao mesmo tempo.

[X] 5. Lógica do Protocolo:
   [X] Receber mensagem de Registro ('R') e adicionar o novo usuário na lista.
   [X] Receber mensagem de Desconexão ('D') e tirar o usuário da lista.
   [X] Enviar a Lista de Usuários ('L') pra quem acabou de entrar.
   [X] Quando alguém entra ou sai, mandar a lista atualizada pra todo mundo automaticamente.

Cliente (cliente.c)

[X] 1. Estrutura Básica:
   [X] Conectar no servidor.
   [X] Perguntar o nome do usuário e a porta P2P que ele vai usar.
   [X] Enviar a mensagem de registro ('R') para o servidor.

[X] 2. UI sem travar (Threads):
   [X] Thread Principal (Input): Fica lendo o que o usuário digita (/msg, /broadcast, /quit).
   [X] Thread Secundária (Recebimento): Fica só ouvindo na porta P2P do cliente, esperando mensagens de outros usuários.
   [X] Thread Terciária (servidor): Fica ouvindo o broadcast da lista de usuários que o servidor envia.

[X] 3. Lógica de Envio (na Thread Principal):
   [X] Enviar Mensagem Direta ('M'): Olhar na lista local o IP/Porta do destino, conectar direto nele, mandar a mensagem e fechar a conexão.
   [X] Enviar Broadcast ('B'): Fazer um loop na lista de usuários e mandar a mensagem pra cada um.
   [X] Enviar Desconexão ('D'): Avisar o servidor que tá saindo e fechar o programa.

[X] 4. Lógica de Recebimento (na Thread Secundária e Terciária):
   [X] Receber Lista ('L') do servidor e guardar/atualizar a lista local de quem tá online. (Thread Terciária)
   [X] Receber Mensagem Direta/Broadcast ('M'/'B') de outros clientes e mostrar na tela.

[ ] 5. Aguardar para mostrar mensagem caso o usuário esteja digitando:
   [ ] Se o usuário estiver digitando, não mostrar a mensagem recebida até que ele termine de digitar ou envie a mensagem.

[ ] 6. Tratamento de Erros:
   [ ] Tratar erros de conexão, desconexão inesperada, e outros problemas que possam acontecer.


## Coisas para Lembrar
- No Servidor, uma thread por cliente além da principal. No Cliente, uma thread pra input e outra pra recebimento. É o jeito de fazer tudo funcionar ao mesmo tempo.

- Mutexes: No servidor, sempre que for mexer na lista de usuários (adicionar/remover), tem que usar mutex

- Precisamos tratar erros, Se recv() retornar 0, significa que o cliente caiu. O servidor tem que perceber isso e remover o usuário da lista, também precisamos se preocupar com os comandos para fechar o programa, o uso de kill e etc

- P2P: Pra mandar uma mensagem, o cliente A abre uma conexão com o cliente B, envia a mensagem e fecha. Não precisa manter uma conexão P2P aberta o tempo todo
