/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
 ******************************************************************************
  */
 /* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
extern SPI_HandleTypeDef hspi1; // Puxa o SPI configurado no main.c

// Ativa o Chip Select do SD (Coloca em nível BAIXO)
static void SD_Select(void) {
    HAL_GPIO_WritePin(ETH_SPI1_NSS_GPIO_Port, ETH_SPI1_NSS_Pin, GPIO_PIN_RESET);
}

// Desativa o Chip Select do SD (Coloca em nível ALTO)
static void SD_Deselect(void) {
    HAL_GPIO_WritePin(ETH_SPI1_NSS_GPIO_Port, ETH_SPI1_NSS_Pin, GPIO_PIN_SET);
}

// Envia e recebe um byte simultaneamente pelo barramento SPI
static uint8_t SPI_RxTx(uint8_t byte) {
    uint8_t rx_byte = 0xFF;
    HAL_SPI_TransmitReceive(&hspi1, &byte, &rx_byte, 1, HAL_MAX_DELAY);
    return rx_byte;
}
/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    SD_Deselect();
    // Envia 80 ciclos de clock em nível alto para o SD acordar no modo SPI
    for (int i = 0; i < 10; i++) {
        SPI_RxTx(0xFF);
    }
    Stat &= ~STA_NOINIT; // CORREÇÃO: Remove o estado de "Não Inicializado" (Sucesso!)
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    return Stat; // CORREÇÃO: Retorna o status real (0 se já inicializado) em vez de travar em STA_NOINIT
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    SD_Select();
    if (HAL_SPI_Receive(&hspi1, buff, count * 512, HAL_MAX_DELAY) != HAL_OK) {
        SD_Deselect();
        return RES_ERROR;
    }
    SD_Deselect();
    return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    SD_Select();
    if (HAL_SPI_Transmit(&hspi1, (uint8_t*)buff, count * 512, HAL_MAX_DELAY) != HAL_OK) {
        SD_Deselect();
        return RES_ERROR;
    }
    SD_Deselect();
    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    // CORREÇÃO: Implementado suporte mínimo necessário para sincronizar arquivos do FreeRTOS com f_sync e f_close
    DRESULT res = RES_ERROR;

    if (Stat & STA_NOINIT) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            SD_Select();
            // Aguarda o barramento SPI liberar a linha MISO (indica fim da escrita interna do SD)
            while (SPI_RxTx(0xFF) != 0xFF);
            SD_Deselect();
            res = RES_OK;
            break;

        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            res = RES_OK;
            break;

        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;
            res = RES_OK;
            break;

        default:
            res = RES_PARERR;
            break;
    }
    return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */
