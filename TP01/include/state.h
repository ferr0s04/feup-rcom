#ifndef _STATE_H_
#define _STATE_H_

// Define os estados possíveis da máquina de estados
typedef enum {
    START,        // Estado inicial
    FLAG_RCV,     // FLAG foi recebida
    A_RCV,        // Endereço foi recebido
    C_RCV,        // Controle foi recebido
    BCC_OK,       // Verificação de BCC foi bem-sucedida
    STOP      // FLAG final foi recebida
} State;

// Função que avança a máquina de estados com base no byte recebido
// Retorna o novo estado após o processamento
State processState(State currentState, unsigned char byte, unsigned char *address, unsigned char *control, unsigned char *bcc);

// Função que verifica se a máquina de estados atingiu o estado final (END_FLAG)
int isStateFinal(State state);

#endif // _STATE_H_
