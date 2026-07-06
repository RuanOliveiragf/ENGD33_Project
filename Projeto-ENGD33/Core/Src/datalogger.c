#include "datalogger.h"
#include "fatfs.h"
#include "task.h"
#include "GY-87.h"
#include <stdio.h>
#include <stdlib.h>

// Variável externa do RTC declarada no main.c
extern RTC_HandleTypeDef hrtc;

// Definição da Fila global
QueueHandle_t Fila_Datalogger = NULL;

// Protótipos das tarefas locais (estáticas para não poluírem o escopo global)
static void Task_Controle(void *argument);
static void Task_SDCard(void *argument);
static TempoRTC_t Hardware_LerRTC(void);
static Sensores_t LerSensores_Reais(void);

// Implementação da Inicialização
void Datalogger_Init(void) {
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

static Sensores_t LerSensores_Reais(void) {
    Sensores_t s;

    // CORREÇÃO: Mudado para o nome real da função do seu arquivo (Accelerometer)
    Cartesian3D dados_aceleracao = Read_Accelerometer();

    s.accel_x = dados_aceleracao.x;
    s.accel_y = dados_aceleracao.y;
    s.accel_z = dados_aceleracao.z;
    s.temperatura = 25.0f; // Ou use a função real se houver

    return s;
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


// ======================================================
// TAREFAS DO FREERTOS
// =======================================================
static void Task_Controle(void *argument) {
    PacoteLog_t log_atual;
    float sinal_pwm = 0.0f;

    for(;;) {
        // CORREÇÃO: Chama a função real em vez do Mock!
        log_atual.dados_planta = LerSensores_Reais();

        // Cálculo do seu PWM (ajuste conforme necessário)
        sinal_pwm = log_atual.dados_planta.accel_x * 1.5f;
        if(sinal_pwm > 100.0f) sinal_pwm = 100.0f;

        log_atual.comando_pwm = sinal_pwm;
        log_atual.carimbo_tempo = Hardware_LerRTC();

        if (Fila_Datalogger != NULL) {
            xQueueSend(Fila_Datalogger, &log_atual, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void Task_SDCard(void *argument) {
    FATFS fs;
    FIL file;
    FRESULT res;
    UINT bytesWritten;
    PacoteLog_t pacote_receber;

    HAL_GPIO_WritePin(ETH_SPI1_NSS_GPIO_Port, ETH_SPI1_NSS_Pin, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));

    res = f_mount(&fs, "0:/", 1);
    if (res != FR_OK) {
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    res = f_open(&file, "0:/datalog.txt", FA_OPEN_APPEND | FA_WRITE);
    if (res != FR_OK) {
        for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for(;;) {
        if (xQueueReceive(Fila_Datalogger, &pacote_receber, portMAX_DELAY) == pdPASS) {
            char buffer_texto[128];

            int len = snprintf(buffer_texto, sizeof(buffer_texto),
                               "[%02d:%02d:%02d] Accel X: %.2f | Y: %.2f | Z: %.2f | PWM: %.2f\r\n",
                               pacote_receber.carimbo_tempo.horas,
                               pacote_receber.carimbo_tempo.minutos,
                               pacote_receber.carimbo_tempo.segundos,
                               pacote_receber.dados_planta.accel_x,  // Mudou aqui
                               pacote_receber.dados_planta.accel_y,  // Mudou aqui
                               pacote_receber.dados_planta.accel_z,  // Mudou aqui
                               pacote_receber.comando_pwm);

            res = f_write(&file, buffer_texto, len, &bytesWritten);

            if (res == FR_OK) {
                f_sync(&file);
            }
        }
    }
}
