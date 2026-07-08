#include "datalogger.h"
#include "fatfs.h"
#include "task.h"
#include "GY-87.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define COMPASS_BIAS  0.0f

// Variável externa do RTC declarada no main.c
extern RTC_HandleTypeDef hrtc;

// Definição da Fila global
QueueHandle_t Fila_Datalogger = NULL;

// Protótipos das tarefas e funções locais (estáticas para não poluírem o escopo global)
static void Task_Controle(void *argument);
static void Task_SDCard(void *argument);
static TempoRTC_t Hardware_LerRTC(void);
//static Sensores_t Mock_LerSensores(void);
static Sensores_t Hardware_LerSensores(void);
static const char *Log_NomeNivel(NivelLog_t nivel);
static void Log_Escrever(FIL *arquivo, NivelLog_t nivel, const char *tarefa, const char *tag, const char *formato, ...);

// Implementação da Inicialização
void Datalogger_Init(void) {
	//srand(HAL_GetTick());
    //cnfigura os sensores físicos antes de criar as tasks
    Configure_Giroscope_and_Accelerometer();//inclui HAL_Delay(100) interno
    Configure_Compass();
    // 1. Cria a fila (suporta até 10 pacotes na fila de espera)
    Fila_Datalogger = xQueueCreate(10, sizeof(PacoteLog_t));

    if (Fila_Datalogger != NULL) {
        // 2. Cria a Task de Controle (Prioridade Alta: 3)
        xTaskCreate(Task_Controle, "Controle", 256, NULL, 3, NULL);

        // 3. Cria a Task do SD Card (Prioridade Baixa: 1)
        xTaskCreate(Task_SDCard, "SDCard_SPI", 1024, NULL, 1, NULL);
    }
}

// =======================================================
// INTERFACES DE HARDWARE E MOCK (Internas)
// =======================================================
static TempoRTC_t Hardware_LerRTC(void) {
    TempoRTC_t tempo_real;
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    // É OBRIGATÓRIO ler o Time e logo em sequência o Date para o registrador destravar corretamente
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    tempo_real.horas = sTime.Hours;
    tempo_real.minutos = sTime.Minutes;
    tempo_real.segundos = sTime.Seconds;

    // Novas linhas para a Data:
    tempo_real.dia = sDate.Date;
    tempo_real.mes = sDate.Month;
    tempo_real.ano = 2000 + sDate.Year; // O RTC do STM32 salva o ano de 0 a 99 (ex: 26 vira 2026)

    return tempo_real;
}

/*

static Sensores_t Mock_LerSensores(void) {
    Sensores_t s;

    // 1. Acelerador: Varia de 0.0% a 100.0%
    // (float)rand() / RAND_MAX gera um número entre 0.0 e 1.0
    s.acelerador = ((float)rand() / RAND_MAX) * 100.0f;

    // 2. Corrente do Motor: Varia de 5.0A a 25.0A (Amplitude de 20.0A)
    s.corrente_motor = 5.0f + (((float)rand() / RAND_MAX) * 20.0f);

    // 3. Velocidade (RPM): Varia de 1000.0 RPM a 3000.0 RPM (Amplitude de 2000.0 RPM)
    s.velocidade = 1000.0f + (((float)rand() / RAND_MAX) * 2000.0f);

    return s;
}
*/

static Sensores_t Hardware_LerSensores(void) {
    Sensores_t s;

    Cartesian3D acel = Read_Accelerometer();
    Cartesian3D giro = Read_Giroscope();
    float bussola = Read_Compass(COMPASS_BIAS);

    s.acel_x = acel.x;
    s.acel_y = acel.y;
    s.acel_z = acel.z;
    s.giro_x = giro.x;
    s.giro_y = giro.y;
    s.giro_z = giro.z;
    s.bussola = bussola;

    return s;
}

// =======================================================
// LOG DE EVENTOS (system.log)
// Formato: [YYYY-MM-DD HH:MM:SS] | NIVEL | Tarefa | Tag | Mensagem
// =======================================================
static const char *Log_NomeNivel(NivelLog_t nivel) {
    switch (nivel) {
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "INFO ";
    }
}

static void Log_Escrever(FIL *arquivo, NivelLog_t nivel, const char *tarefa, const char *tag, const char *formato, ...) {
    TempoRTC_t agora = Hardware_LerRTC();
    char mensagem[80];
    char linha[160];
    UINT bytesEscritos;
    int len;
    va_list args;

    va_start(args, formato);
    vsnprintf(mensagem, sizeof(mensagem), formato, args);
    va_end(args);

    len = snprintf(linha, sizeof(linha),
                   "[%04d-%02d-%02d %02d:%02d:%02d] | %s | %-10s | %-10s | %s\r\n",
                   agora.ano, agora.mes, agora.dia,
                   agora.horas, agora.minutos, agora.segundos,
                   Log_NomeNivel(nivel), tarefa, tag, mensagem);

    if (f_write(arquivo, linha, len, &bytesEscritos) == FR_OK) {
        f_sync(arquivo);
    }
}

// ======================================================
// TAREFAS DO FREERTOS
// =======================================================
static void Task_Controle(void *argument) {
    PacoteLog_t log_atual;
    float sinal_pwm = 0.0f;

    for(;;) {
        log_atual.dados_planta  = Hardware_LerSensores();   // ← era Mock_LerSensores
        log_atual.carimbo_tempo = Hardware_LerRTC();

        float mag = sqrtf(log_atual.dados_planta.acel_x * log_atual.dados_planta.acel_x +
                          log_atual.dados_planta.acel_y * log_atual.dados_planta.acel_y);
        sinal_pwm = (mag / 9.81f) * 100.0f;
        if (sinal_pwm > 100.0f) sinal_pwm = 100.0f;

        log_atual.comando_pwm = sinal_pwm;

        if (Fila_Datalogger != NULL) {
            xQueueSend(Fila_Datalogger, &log_atual, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void Task_SDCard(void *argument) {
    FATFS fs;
    FIL arquivo_log;   // system.log -> eventos e erros (formato profissional)
    FIL arquivo_csv;   // data.csv   -> telemetria tabular
    FRESULT res;
    UINT bytesWritten;
    PacoteLog_t pacote_receber;

    HAL_GPIO_WritePin(ETH_SPI1_NSS_GPIO_Port, ETH_SPI1_NSS_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));

    res = f_mount(&fs, "0:/", 1);
    if (res != FR_OK) {
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    res = f_open(&arquivo_log, "0:/system.log", FA_OPEN_APPEND | FA_WRITE);
    if (res != FR_OK) {
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    Log_Escrever(&arquivo_log, LOG_INFO, "SDCard", "FatFS", "Cartao SD montado com sucesso.");

    res = f_open(&arquivo_csv, "0:/data.csv", FA_OPEN_APPEND | FA_WRITE);
    if (res != FR_OK) {
        Log_Escrever(&arquivo_log, LOG_ERROR, "SDCard", "FatFS", "Falha ao abrir data.csv (codigo %d).", res);
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Se o arquivo acabou de ser criado (vazio), grava o cabecalho das colunas
    if (f_size(&arquivo_csv) == 0) {
        const char *cabecalho = "timestamp,acelerador,corrente_motor,velocidade,pwm\r\n";
        if (f_write(&arquivo_csv, cabecalho, strlen(cabecalho), &bytesWritten) == FR_OK) {
            f_sync(&arquivo_csv);
        }
    }

    Log_Escrever(&arquivo_log, LOG_INFO, "SDCard", "Datalogger", "Iniciando gravacao de telemetria em data.csv.");

    for(;;) {
        if (xQueueReceive(Fila_Datalogger, &pacote_receber, portMAX_DELAY) == pdPASS) {
            char linha_csv[128];

            int len = snprintf(linha_csv, sizeof(linha_csv),
                                "%04d-%02d-%02d %02d:%02d:%02d,%.2f,%.2f,%.2f,%.2f\r\n",
                                pacote_receber.carimbo_tempo.ano,
                                pacote_receber.carimbo_tempo.mes,
                                pacote_receber.carimbo_tempo.dia,
                                pacote_receber.carimbo_tempo.horas,
                                pacote_receber.carimbo_tempo.minutos,
                                pacote_receber.carimbo_tempo.segundos,
	                            pacote_receber.dados_planta.acel_x,
	                            pacote_receber.dados_planta.acel_y,
	                            pacote_receber.dados_planta.acel_z,
	                            pacote_receber.dados_planta.giro_x,
	                            pacote_receber.dados_planta.giro_y,
	                            pacote_receber.dados_planta.giro_z,
	                            pacote_receber.dados_planta.bussola,
                                pacote_receber.comando_pwm);

            res = f_write(&arquivo_csv, linha_csv, len, &bytesWritten);

            if (res == FR_OK) {
                f_sync(&arquivo_csv);
            } else {
                Log_Escrever(&arquivo_log, LOG_ERROR, "SDCard", "FatFS", "Falha ao escrever em data.csv (codigo %d).", res);
            }
        }
    }
}
