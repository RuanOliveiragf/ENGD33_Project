/* Core/Inc/datalogger.h */
#ifndef INC_DATALOGGER_H_
#define INC_DATALOGGER_H_

#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"

// Níveis de severidade usados no log de eventos (system.log)
typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} NivelLog_t;

// Fila global para que a Task de Controle possa enviar dados de telemetria
extern QueueHandle_t Fila_Datalogger;

// Função única que o main.c vai chamar para ligar o Datalogger
void Datalogger_Init(void);

#endif /* INC_DATALOGGER_H_ */
