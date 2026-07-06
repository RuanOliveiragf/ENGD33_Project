/*
 * GY-87.c
 *
 *  Created on: Aug 30, 2025
 *      Author: Jes
 */

/* Includes ------------------------------------------------------------------*/
#include "GY-87.h"
#include "main.h"
#include <math.h>

void Configure_Compass(void)
{
    uint8_t Reg_A_Config;
    uint8_t Reg_B_Config;
    uint8_t Reg_MODE_Config;

    Reg_A_Config = (HMC5883L_AVERAGING_1 | HMC5883L_RATE_15 | HMC5883L_BIAS_NORMAL);
    Reg_B_Config = (HMC5883L_GAIN_660);
    Reg_MODE_Config = (HMC5883L_I2C_SPEED_4OOKHZ | HMC5883L_MODE_CONTINUOUS);

    HAL_I2C_Mem_Write(&hi2c1, HMC5883L_ADDRESS << 1, HMC5883L_REG_CONFIG_A, 1, &Reg_A_Config, 1, 10);
    HAL_I2C_Mem_Write(&hi2c1, HMC5883L_ADDRESS << 1, HMC5883L_REG_CONFIG_B, 1, &Reg_B_Config, 1, 10);
    HAL_I2C_Mem_Write(&hi2c1, HMC5883L_ADDRESS << 1, HMC5883L_REG_MODE, 1, &Reg_MODE_Config, 1, 10);
}

uint8_t Read_Compass_Status_Register(void)
{
    uint8_t Status = 0;
    HAL_I2C_Mem_Read(&hi2c1, HMC5883L_ADDRESS << 1, HMC5883L_REG_STATUS, 1, &Status, 1, 10);
    return Status;
}

float Read_Compass(float bias)
{
    uint8_t buffer[6];
    int16_t x, y; // Mudado para int16_t para suportar leituras negativas corretas
    float angle_zero;
    static float angle, angle1, angle2;

    angle_zero = bias;

    // Corrigido de '=' para '=='
    if (HAL_GPIO_ReadPin(IMU_DRDY_EXTI13_GPIO_Port, IMU_DRDY_EXTI13_Pin) == GPIO_PIN_SET)
    {
        HAL_I2C_Mem_Read(&hi2c1, HMC5883L_ADDRESS << 1, HMC5883L_REG_DATAX_H, 1, buffer, 6, 10);

        x = (int16_t)((buffer[0] << 8) | buffer[1]);
        // z = (int16_t)((buffer[2] << 8) | buffer[3]); // Se precisar do Z no futuro
        y = (int16_t)((buffer[4] << 8) | buffer[5]);

        angle2 = angle1;
        angle1 = angle;
        angle = (atan2(((float)y), ((float)x)) * 57.2957795131f) + angle_zero; // 180.0 / M_PI
    }
    else
    {
        angle = angle + (angle1 - angle2);
        angle2 = angle1;
        angle1 = angle;
    }
    return angle;
}

void Configure_Giroscope_and_Accelerometer(void)
{
    uint8_t Reg_Config;
    uint8_t Reg_Giro_Config;
    uint8_t Reg_Accel_Config;
    uint8_t Reg_Pwr_Mgmt_1;

    Reg_Pwr_Mgmt_1 = (MPU6050_DEVICE_RESET);
    Reg_Config = (EXT_SYNC_SET_DISABLED | EXT_SYNC_DLPF_CFG_1K_180B);
    Reg_Giro_Config = (GYRO_FULL_SCALE_1000);
    Reg_Accel_Config = (ACCEL_FULL_SCALE_2G);

    // Tira o MPU6050 do modo Sleep inicial e reinicia
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDRESS, MPU6050_PWR_MGMT_1, 1, &Reg_Pwr_Mgmt_1, 1, 10);
    HAL_Delay(100); // Pequeno delay essencial pós-reset para estabilizar o sensor

    // Acorda o sensor definindo o clock interno baseado no Giroscópio do eixo X (mais estável)
    Reg_Pwr_Mgmt_1 = MPU6050_CLKSEL_PLL_X_GYR;
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDRESS, MPU6050_PWR_MGMT_1, 1, &Reg_Pwr_Mgmt_1, 1, 10);

    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDRESS, MPU6050_CONFIG_REG, 1, &Reg_Config, 1, 10);
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDRESS, MPU6050_GYRO_CONFIG, 1, &Reg_Giro_Config, 1, 10);
    HAL_I2C_Mem_Write(&hi2c1, MPU6050_ADDRESS, MPU6050_ACCEL_CONFIG, 1, &Reg_Accel_Config, 1, 10);
}

Cartesian3D Read_Accelerometer(void)
{
    uint8_t buffer[6];
    int16_t raw_x, raw_y, raw_z;
    Cartesian3D acceleration;

    // Lendo os 6 registradores de uma vez só (MUITO mais rápido e evita desalinhamento de dados)
    if (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDRESS, MPU6050_ACCEL_XOUT_H, 1, buffer, 6, 10) == HAL_OK)
    {
        raw_x = (int16_t)((buffer[0] << 8) | buffer[1]);
        raw_y = (int16_t)((buffer[2] << 8) | buffer[3]);
        raw_z = (int16_t)((buffer[4] << 8) | buffer[5]);

        // No modo ACCEL_FULL_SCALE_2G, o fator de escala é 16384 LSB/g.
        // Multiplicar pelo ganho correto para ter o valor em m/s² (1g = 9.81 m/s²)
        acceleration.x = ((float)raw_x / 16384.0f) * 9.81f;
        acceleration.y = ((float)raw_y / 16384.0f) * 9.81f;
        acceleration.z = ((float)raw_z / 16384.0f) * 9.81f;
    }
    else
    {
        acceleration.x = 0.0f;
        acceleration.y = 0.0f;
        acceleration.z = 0.0f;
    }

    return acceleration;
}

Cartesian3D Read_Giroscope(void)
{
    uint8_t buffer[6];
    int16_t raw_x, raw_y, raw_z;
    Cartesian3D giro;

    // Lendo todos os eixos do giroscópio em uma única rajada I2C
    if (HAL_I2C_Mem_Read(&hi2c1, MPU6050_ADDRESS, MPU6050_GYRO_XOUT_H, 1, buffer, 6, 10) == HAL_OK)
    {
        raw_x = (int16_t)((buffer[0] << 8) | buffer[1]);
        raw_y = (int16_t)((buffer[2] << 8) | buffer[3]);
        raw_z = (int16_t)((buffer[4] << 8) | buffer[5]);

        // No modo GYRO_FULL_SCALE_1000, o fator de escala é 32.8 LSB / (graus por segundo)
        giro.x = (float)raw_x / 32.8f;
        giro.y = (float)raw_y / 32.8f;
        giro.z = (float)raw_z / 32.8f;
    }
    else
    {
        giro.x = 0.0f;
        giro.y = 0.0f;
        giro.z = 0.0f;
    }

    return giro;
}
