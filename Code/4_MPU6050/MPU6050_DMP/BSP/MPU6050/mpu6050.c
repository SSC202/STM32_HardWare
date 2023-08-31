#include "stm32f1xx_hal.h"
#include "mpu6050.h"
/*******************����ѡ�񲿷�***********************/
#ifdef MPU_DMP
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#endif
/*******************�궨�岿��*************************/
#ifdef MPU6050_SoftWare_IIC
#include <inttypes.h>
#include "gpio.h"
#define IIC_WR 0 /* д����bit */
#define IIC_RD 1 /* ������bit */
#define IIC_SCL(x)                                                                                                                            \
    do {                                                                                                                                      \
        (x == 1) ? HAL_GPIO_WritePin(MPU_SCL_Port, MPU_SCL_Pin, GPIO_PIN_SET) : HAL_GPIO_WritePin(MPU_SCL_Port, MPU_SCL_Pin, GPIO_PIN_RESET); \
    } while (0)

#define IIC_SDA(x)                                                                                                                            \
    do {                                                                                                                                      \
        (x == 1) ? HAL_GPIO_WritePin(MPU_SDA_Port, MPU_SDA_Pin, GPIO_PIN_SET) : HAL_GPIO_WritePin(MPU_SDA_Port, MPU_SDA_Pin, GPIO_PIN_RESET); \
    } while (0)

#define IIC_SDA_READ() HAL_GPIO_ReadPin(MPU_SDA_Port, MPU_SDA_Pin) /* ��SDA����״̬ */
#endif
#ifdef MPU6050_HardWare_IIC
#include "iic.h"
#endif
/*****************IIC������������**********************/
/* ���ʹ�����IIC */
#ifdef MPU6050_SoftWare_IIC
/**
 * @brief               IIC �����ӳ٣����Ϊ400kHz
 * @attention           ѭ������Ϊ10ʱ��SCLƵ�� = 205KHz
                        ѭ������Ϊ7ʱ��SCLƵ�� = 347KHz�� SCL�ߵ�ƽʱ��1.5us��SCL�͵�ƽʱ��2.87us
                        ѭ������Ϊ5ʱ��SCLƵ�� = 421KHz�� SCL�ߵ�ƽʱ��1.25us��SCL�͵�ƽʱ��2.375us
*/
static void _IIC_Delay(void)
{
    uint8_t i;

    for (i = 0; i < 10; i++)
        ;
}

/**
 * @brief       CPU����IIC���������ź�
 * @attention   ��SCL�ߵ�ƽʱ��SDA����һ�������ر�ʾIIC���������ź�
 */
static void _IIC_Start(void)
{
    IIC_SDA(1);
    IIC_SCL(1);
    _IIC_Delay();
    IIC_SDA(0);
    _IIC_Delay();
    IIC_SCL(0);
    _IIC_Delay();
}

/**
 * @brief       CPU����IIC����ֹͣ�ź�
 * @attention   ��SCL�ߵ�ƽʱ��SDA����һ�������ر�ʾIIC����ֹͣ�ź�
 */
static void _IIC_Stop(void)
{
    IIC_SDA(0);
    IIC_SCL(1);
    _IIC_Delay();
    IIC_SDA(1);
}

/**
 * @brief       CPU��IIC�����豸����8bit����
 * @param       byte    ���͵�8�ֽ�����
 */
static void _IIC_Send_Byte(uint8_t byte)
{
    uint8_t i;

    /* �ȷ����ֽڵĸ�λbit7 */
    for (i = 0; i < 8; i++) {
        if (byte & 0x80) {
            IIC_SDA(1);
        } else {
            IIC_SDA(0);
        }
        _IIC_Delay();
        IIC_SCL(1);
        _IIC_Delay();
        IIC_SCL(0);
        if (i == 7) {
            IIC_SDA(1); // �ͷ�����
        }
        byte <<= 1; // ����һλ
        _IIC_Delay();
    }
}

/**
 * @brief   CPU����һ��ACK�ź�
 */
static void _IIC_Ack(void)
{
    IIC_SDA(0); /* CPU����SDA = 0 */
    _IIC_Delay();
    IIC_SCL(1); /* CPU����1��ʱ�� */
    _IIC_Delay();
    IIC_SCL(0);
    _IIC_Delay();
    IIC_SDA(1); /* CPU�ͷ�SDA���� */
}

/**
 * @brief   CPU����1��NACK�ź�
 */
static void _IIC_NAck(void)
{
    IIC_SDA(1); /* CPU����SDA = 1 */
    _IIC_Delay();
    IIC_SCL(1); /* CPU����1��ʱ�� */
    _IIC_Delay();
    IIC_SCL(0);
    _IIC_Delay();
}

/**
 * @brief   CPU��IIC�����豸��ȡ8bit����
 * @return  ��ȡ������
 */
static uint8_t _IIC_Read_Byte(uint8_t ack)
{
    uint8_t i;
    uint8_t value;

    /* ������1��bitΪ���ݵ�bit7 */
    value = 0;
    for (i = 0; i < 8; i++) {
        value <<= 1;
        IIC_SCL(1);
        _IIC_Delay();
        if (IIC_SDA_READ()) {
            value++;
        }
        IIC_SCL(0);
        _IIC_Delay();
    }
    if (ack == 0)
        _IIC_NAck();
    else
        _IIC_Ack();
    return value;
}

/**
 * @brief   CPU����һ��ʱ�ӣ�����ȡ������ACKӦ���ź�
 * @return  ����0��ʾ��ȷӦ��1��ʾ��������Ӧ
 */
static uint8_t _IIC_Wait_Ack(void)
{
    uint8_t re;

    IIC_SDA(1); /* CPU�ͷ�SDA���� */
    _IIC_Delay();
    IIC_SCL(1); /* CPU����SCL = 1, ��ʱ�����᷵��ACKӦ�� */
    _IIC_Delay();
    if (IIC_SDA_READ()) /* CPU��ȡSDA����״̬ */
    {
        re = 1;
    } else {
        re = 0;
    }
    IIC_SCL(0);
    _IIC_Delay();
    return re;
}

/**
 * @brief   ����IIC���ߵ�GPIO������ģ��IO�ķ�ʽʵ��
 * @attention   ��CubeMX��ʵ�֣�ѡ����ٿ�©���
 */
static void _IIC_GPIO_Init(void)
{
    MX_GPIO_Init();
    _IIC_Stop();
}

/**
 * @brief   ���IIC�����豸��CPU�����豸��ַ��Ȼ���ȡ�豸Ӧ�����жϸ��豸�Ƿ����
 * @param   _Address �豸��IIC���ߵ�ַ
 * @return  0��ʾ��ȷ,1��ʾδ̽�⵽
 */
static uint8_t _IIC_CheckDevice(uint8_t _Address)
{
    uint8_t ucAck;

    _IIC_GPIO_Init();                  /* ����GPIO */
    _IIC_Start();                      /* ���������ź� */
    _IIC_Send_Byte(_Address | IIC_WR); /* �����豸��ַ+��д����bit��0 = w�� 1 = r) bit7 �ȴ� */
    ucAck = _IIC_Wait_Ack();           /* ����豸��ACKӦ�� */
    _IIC_Stop();                       /* ����ֹͣ�ź� */

    return ucAck;
}

/**
 * @brief   IIC����д
 * @param   addr    ������ַ
 * @param   reg     �Ĵ�����ַ
 * @param   len     д�볤��
 * @param   buf     ������
 */
uint8_t _MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t i;
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 0); // ����������ַ+д����
    if (_IIC_Wait_Ack())             // �ȴ�Ӧ��
    {
        _IIC_Stop();
        return 1;
    }
    _IIC_Send_Byte(reg); // д�Ĵ�����ַ
    _IIC_Wait_Ack();     // �ȴ�Ӧ��
    for (i = 0; i < len; i++) {
        _IIC_Send_Byte(buf[i]); // ��������
        if (_IIC_Wait_Ack())    // �ȴ�ACK
        {
            _IIC_Stop();
            return 1;
        }
    }
    _IIC_Stop();
    return 0;
}

/**
 * @brief   IIC������
 * @param   addr    ������ַ
 * @param   reg     �Ĵ�����ַ
 * @param   len     д�볤��
 * @param   buf     ������
 */
uint8_t _MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 0); // ����������ַ+д����
    if (_IIC_Wait_Ack())             // �ȴ�Ӧ��
    {
        _IIC_Stop();
        return 1;
    }
    _IIC_Send_Byte(reg); // д�Ĵ�����ַ
    _IIC_Wait_Ack();     // �ȴ�Ӧ��
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 1); // ����������ַ+������
    _IIC_Wait_Ack();                 // �ȴ�Ӧ��
    while (len) {
        if (len == 1)
            *buf = _IIC_Read_Byte(0); // ������,����nACK
        else
            *buf = _IIC_Read_Byte(1); // ������,����ACK
        len--;
        buf++;
    }
    _IIC_Stop(); // ����һ��ֹͣ����
    return 0;
}

/**
 * @brief   IICд�ֽ�
 * @param   reg     �Ĵ�����ַ
 * @param   data    ����
 */
static uint8_t _MPU_Write_Byte(uint8_t reg, uint8_t data)
{
    _IIC_Start();
    _IIC_Send_Byte((MPU_ADDR << 1) | 0); // ����������ַ+д����
    if (_IIC_Wait_Ack())                 // �ȴ�Ӧ��
    {
        _IIC_Stop();
        return 1;
    }
    _IIC_Send_Byte(reg);  // д�Ĵ�����ַ
    _IIC_Wait_Ack();      // �ȴ�Ӧ��
    _IIC_Send_Byte(data); // ��������
    if (_IIC_Wait_Ack())  // �ȴ�ACK
    {
        _IIC_Stop();
        return 1;
    }
    _IIC_Stop();
    return 0;
}

/**
 * @brief   IIC���ֽ�
 * @param   reg     �Ĵ�����ַ
 */
static uint8_t _MPU_Read_Byte(uint8_t reg)
{
    uint8_t res;
    _IIC_Start();
    _IIC_Send_Byte((MPU_ADDR << 1) | 0); // ����������ַ+д����
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    _IIC_Send_Byte(reg);                 // д�Ĵ�����ַ
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    _IIC_Start();
    _IIC_Send_Byte((MPU_ADDR << 1) | 1); // ����������ַ+������
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    res = _IIC_Read_Byte(0);             // ��ȡ����,����nACK
    _IIC_Stop();                         // ����һ��ֹͣ����
    return res;
}

#endif
/* ���ʹ��Ӳ��IIC */
#ifdef MPU6050_HardWare_IIC
/**
 * @brief   IICд�ֽ�
 * @param   reg     �Ĵ�����ַ
 * @param   data    ����
 */
static uint8_t _MPU_Write_Byte(uint8_t reg, uint8_t data)
{
    uint8_t send_buf[2] = {reg,data};

    return 0;
}
#endif

/***********************�û�����**************************/
/**
 * @brief ��ʼ��MPU6050
 */
uint8_t _MPU_Init(void)
{
    uint8_t res;

    _IIC_GPIO_Init();                         // ��ʼ��IIC����
    _MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0X80); // ��λMPU6050
    HAL_Delay(100);
    _MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0X00); // ����MPU6050
    _MPU_Set_Gyro_Fsr(3);                     // �����Ǵ�����,��2000dps
    _MPU_Set_Accel_Fsr(0);                    // ���ٶȴ�����,��2g
    _MPU_Set_Rate(50);                        // ���ò�����50Hz
    _MPU_Write_Byte(MPU_INT_EN_REG, 0X00);    // �ر������ж�
    _MPU_Write_Byte(MPU_USER_CTRL_REG, 0X00); // I2C��ģʽ�ر�
    _MPU_Write_Byte(MPU_FIFO_EN_REG, 0X00);   // �ر�FIFO
    _MPU_Write_Byte(MPU_INTBP_CFG_REG, 0X80); // INT���ŵ͵�ƽ��Ч
    res = _MPU_Read_Byte(MPU_DEVICE_ID_REG);
    if (res == MPU_ADDR) // ����ID��ȷ
    {
        _MPU_Write_Byte(MPU_PWR_MGMT1_REG, 0X01); // ����CLKSEL,PLL X��Ϊ�ο�
        _MPU_Write_Byte(MPU_PWR_MGMT2_REG, 0X00); // ���ٶ��������Ƕ�����
        _MPU_Set_Rate(50);                        // ���ò�����Ϊ50Hz
    } else
        return 1;
    return 0;
}

/**
 * @brief   ����MPU6050�����Ǵ����������̷�Χ
 * @param   fsr     0,��250dps;1,��500dps;2,��1000dps;3,��2000dps
 */
uint8_t _MPU_Set_Gyro_Fsr(uint8_t fsr)
{
    return _MPU_Write_Byte(MPU_GYRO_CFG_REG, fsr << 3); // ���������������̷�Χ
}

/**
 * @brief   ����MPU6050���ٶȴ����������̷�Χ
 * @param   fsr     0,��2g;1,��4g;2,��8g;3,��16g
 */
uint8_t _MPU_Set_Accel_Fsr(uint8_t fsr)
{
    return _MPU_Write_Byte(MPU_ACCEL_CFG_REG, fsr << 3); // ���ü��ٶȴ����������̷�Χ
}

/**
 * @brief   ����MPU6050���ٶȴ����������̷�Χ
 * @param   lpf     ���ֵ�ͨ�˲�Ƶ��(Hz)
 */
uint8_t _MPU_Set_LPF(uint16_t lpf)
{
    uint8_t data = 0;
    if (lpf >= 188)
        data = 1;
    else if (lpf >= 98)
        data = 2;
    else if (lpf >= 42)
        data = 3;
    else if (lpf >= 20)
        data = 4;
    else if (lpf >= 10)
        data = 5;
    else
        data = 6;
    return _MPU_Write_Byte(MPU_CFG_REG, data); // �������ֵ�ͨ�˲���
}

/**
 * @brief   ����MPU6050�Ĳ�����(�ٶ�Fs=1KHz)
 * @param   rate     4~1000(Hz)
 */
uint8_t _MPU_Set_Rate(uint16_t rate)
{
    uint8_t data;
    if (rate > 1000)
        rate = 1000;
    if (rate < 4)
        rate = 4;
    data = 1000 / rate - 1;
    data = _MPU_Write_Byte(MPU_SAMPLE_RATE_REG, data); // �������ֵ�ͨ�˲���
    return _MPU_Set_LPF(rate / 2);                     // �Զ�����LPFΪ�����ʵ�һ��
}

/**
 * @brief   �õ��¶�
 * @return  �¶�ֵ(������100��)
 */
short _MPU_Get_Temperature(void)
{
    uint8_t buf[2];
    short raw;
    float temp;
    _MPU_Read_Len(MPU_ADDR, MPU_TEMP_OUTH_REG, 2, buf);
    raw  = ((uint16_t)buf[0] << 8) | buf[1];
    temp = 36.53 + ((double)raw) / 340;
    return temp * 100;
}

/**
 * @brief   �õ�������ֵ(ԭʼֵ)
 * @param   gx  ������x��ԭʼ����(������)
 * @param   gy  ������y��ԭʼ����(������)
 * @param   gz  ������z��ԭʼ����(������)
 */
uint8_t _MPU_Get_Gyroscope(short *gx, short *gy, short *gz)
{
    uint8_t buf[6], res;
    res = _MPU_Read_Len(MPU_ADDR, MPU_GYRO_XOUTH_REG, 6, buf);
    if (res == 0) {
        *gx = ((uint16_t)buf[0] << 8) | buf[1];
        *gy = ((uint16_t)buf[2] << 8) | buf[3];
        *gz = ((uint16_t)buf[4] << 8) | buf[5];
    }
    return res;
    ;
}

/**
 * @brief   �õ����ٶ�ֵ(ԭʼֵ)
 * @param   ax  ���ٶȼ�x��ԭʼ����(������)
 * @param   ay  ���ٶȼ�y��ԭʼ����(������)
 * @param   az  ���ٶȼ�z��ԭʼ����(������)
 */
uint8_t _MPU_Get_Accelerometer(short *ax, short *ay, short *az)
{
    uint8_t buf[6], res;
    res = _MPU_Read_Len(MPU_ADDR, MPU_ACCEL_XOUTH_REG, 6, buf);
    if (res == 0) {
        *ax = ((uint16_t)buf[0] << 8) | buf[1];
        *ay = ((uint16_t)buf[2] << 8) | buf[3];
        *az = ((uint16_t)buf[4] << 8) | buf[5];
    }
    return res;
    ;
}
