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