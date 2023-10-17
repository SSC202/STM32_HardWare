#include "stm32f1xx_hal.h"
#include "mpu9250.h"
#include "inv_mpu.h"
#include "inv_mpu_dmp_motion_driver.h"
#include <stdio.h>
/*******************�궨�岿��*************************/
/* ���ʹ�����IIC */
#ifdef MPU9250_SoftWare_IIC
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
/* ���ʹ��Ӳ��IIC */
#ifdef MPU9250_HardWare_IIC
#include "i2c.h"

#endif
/*****************IIC������������**********************/
/* ���ʹ�����IIC */
#ifdef MPU9250_SoftWare_IIC
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
void _IIC_GPIO_Init(void)
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
uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
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
uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
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
static uint8_t MPU_Write_Byte(uint8_t addr, uint8_t reg, uint8_t data)
{
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 0); // ����������ַ+д����
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
static uint8_t MPU_Read_Byte(uint8_t addr, uint8_t reg)
{
    uint8_t res;
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 0); // ����������ַ+д����
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    _IIC_Send_Byte(reg);                 // д�Ĵ�����ַ
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    _IIC_Start();
    _IIC_Send_Byte((addr << 1) | 1); // ����������ַ+������
    _IIC_Wait_Ack();                     // �ȴ�Ӧ��
    res = _IIC_Read_Byte(0);             // ��ȡ����,����nACK
    _IIC_Stop();                         // ����һ��ֹͣ����
    return res;
}

#endif
/* ���ʹ��Ӳ��IIC */
#ifdef MPU9250_HardWare_IIC
/**
 * @brief   IIC����д
 * @param   addr    ������ַ
 * @param   reg     �Ĵ�����ַ
 * @param   len     д�볤��
 * @param   buf     ������
 */
uint8_t MPU_Write_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t ret = HAL_I2C_Mem_Write(&hi2c1, (addr << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 0xFFFF);
    if (ret == HAL_OK) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief   IIC������
 * @param   addr    ������ַ
 * @param   reg     �Ĵ�����ַ
 * @param   len     д�볤��
 * @param   buf     ������
 */
uint8_t MPU_Read_Len(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *buf)
{
    uint8_t ret = HAL_I2C_Mem_Read(&hi2c1, (addr << 1), reg, I2C_MEMADD_SIZE_8BIT, buf, len, 0xFFFF);
    if (ret == HAL_OK) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief   IICд�ֽ�
 * @param   addr    ������ַ
 * @param   reg     �Ĵ�����ַ
 * @param   data    ����
 */
static uint8_t MPU_Write_Byte(uint8_t addr, uint8_t reg, uint8_t data)
{
    uint8_t ret = HAL_I2C_Mem_Read(&hi2c1, (addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 0xFFFF);
    if (ret == HAL_OK) {
        return 0;
    } else {
        return 1;
    }
}

/**
 * @brief   IIC���ֽ�
 * @param   reg     �Ĵ�����ַ
 */
static uint8_t MPU_Read_Byte(uint8_t addr, uint8_t reg)
{
    uint8_t res;
    HAL_I2C_Mem_Read(&hi2c1, (addr << 1), reg, I2C_MEMADD_SIZE_8BIT, &res, 1, 0xFFFF);
    return res;
}
#endif

/***********************��������**************************/
/**
 * @brief   ����MPU6050�����Ǵ����������̷�Χ
 * @param   fsr     0,��250dps;1,��500dps;2,��1000dps;3,��2000dps
 */
static uint8_t MPU_Set_Gyro_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(MPU9250_ADDR, MPU_GYRO_CFG_REG, fsr << 3); // ���������������̷�Χ
}

/**
 * @brief   ����MPU6050���ٶȴ����������̷�Χ
 * @param   fsr     0,��2g;1,��4g;2,��8g;3,��16g
 */
uint8_t MPU_Set_Accel_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(MPU9250_ADDR, MPU_ACCEL_CFG_REG, fsr << 3); // ���ü��ٶȴ����������̷�Χ
}

/**
 * @brief   ����MPU6050���ٶȴ����������̷�Χ
 * @param   lpf     ���ֵ�ͨ�˲�Ƶ��(Hz)
 */
static uint8_t MPU_Set_LPF(uint16_t lpf)
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
    return MPU_Write_Byte(MPU9250_ADDR, MPU_CFG_REG, data); // �������ֵ�ͨ�˲���
}

/**
 * @brief   ����MPU6050�Ĳ�����(�ٶ�Fs=1KHz)
 * @param   rate     4~1000(Hz)
 */
static uint8_t MPU_Set_Rate(uint16_t rate)
{
    uint8_t data;
    if (rate > 1000)
        rate = 1000;
    if (rate < 4)
        rate = 4;
    data = 1000 / rate - 1;
    data = MPU_Write_Byte(MPU9250_ADDR, MPU_SAMPLE_RATE_REG, data); // �������ֵ�ͨ�˲���
    return MPU_Set_LPF(rate / 2);                                   // �Զ�����LPFΪ�����ʵ�һ��
}

/**
 * @brief   �õ��¶�
 * @return  �¶�ֵ(������100��)
 */
static short MPU_Get_Temperature(void)
{
    uint8_t buf[2];
    short raw;
    float temp;
    MPU_Read_Len(MPU9250_ADDR, MPU_TEMP_OUTH_REG, 2, buf);
    raw  = ((uint16_t)buf[0] << 8) | buf[1];
    temp = 36.53 + ((double)raw) / 340;
    return temp;
}

/**
 * @brief   �õ�������ֵ(ԭʼֵ)
 * @param   gx  ������x��ԭʼ����(������)
 * @param   gy  ������y��ԭʼ����(������)
 * @param   gz  ������z��ԭʼ����(������)
 */
static int8_t MPU_Get_Gyroscope(short *gx, short *gy, short *gz)
{
    uint8_t buf[6], res;
    res = MPU_Read_Len(MPU9250_ADDR, MPU_GYRO_XOUTH_REG, 6, buf);
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
static int8_t MPU_Get_Accelerometer(short *ax, short *ay, short *az)
{
    uint8_t buf[6], res;
    res = MPU_Read_Len(MPU9250_ADDR, MPU_ACCEL_XOUTH_REG, 6, buf);
    if (res == 0) {
        *ax = ((uint16_t)buf[0] << 8) | buf[1];
        *ay = ((uint16_t)buf[2] << 8) | buf[3];
        *az = ((uint16_t)buf[4] << 8) | buf[5];
    }
    return res;
    ;
}

/**
 * @brief   �õ�������ֵ(ԭʼֵ)
 * @param   mx  ������x��ԭʼ����(������)
 * @param   my  ������y��ԭʼ����(������)
 * @param   mz  ������z��ԭʼ����(������)
 */
uint8_t MPU_Get_Magnetometer(short *mx, short *my, short *mz)
{
    uint8_t buf[6], res;
    res = MPU_Read_Len(AK8963_ADDR, MAG_XOUT_L, 6, buf);
    if (res == 0) {
        *mx = ((uint16_t)buf[1] << 8) | buf[0];
        *my = ((uint16_t)buf[3] << 8) | buf[2];
        *mz = ((uint16_t)buf[5] << 8) | buf[4];
    }
    MPU_Write_Byte(AK8963_ADDR, MAG_CNTL1, 0X11); // AK8963ÿ�ζ����Ժ���Ҫ��������Ϊ���β���ģʽ
    return res;
}
/*********************************�û�����*******************************************/

IMU_Data_t mpu_data;
/**
 * @brief ��ʼ��MPU6050
 */
uint8_t MPU_Init(void)
{
    uint8_t res = 0;
#ifdef MPU6050_SoftWare_IIC
    _IIC_GPIO_Init(); // ��ʼ��IIC����
#endif
    MPU_Write_Byte(MPU9250_ADDR, MPU_PWR_MGMT1_REG, 0X80); // ��λMPU9250
    HAL_Delay(100);                                        // ��ʱ100ms
    MPU_Write_Byte(MPU9250_ADDR, MPU_PWR_MGMT1_REG, 0X00); // ����MPU9250
    MPU_Set_Gyro_Fsr(3);                                   // �����Ǵ�����,��2000dps
    MPU_Set_Accel_Fsr(0);                                  // ���ٶȴ�����,��2g
    MPU_Set_Rate(50);                                      // ���ò�����50Hz
    MPU_Write_Byte(MPU9250_ADDR, MPU_INT_EN_REG, 0X00);    // �ر������ж�
    MPU_Write_Byte(MPU9250_ADDR, MPU_USER_CTRL_REG, 0X00); // I2C��ģʽ�ر�
    MPU_Write_Byte(MPU9250_ADDR, MPU_FIFO_EN_REG, 0X00);   // �ر�FIFO
    MPU_Write_Byte(MPU9250_ADDR, MPU_INTBP_CFG_REG, 0X82); // INT���ŵ͵�ƽ��Ч������bypassģʽ������ֱ�Ӷ�ȡ������
    res = MPU_Read_Byte(MPU9250_ADDR, MPU_DEVICE_ID_REG);  // ��ȡMPU6500��ID
    // printf("res = %x\r\n",res);
    if (res == MPU6500_ID) // ����ID��ȷ
    {
        MPU_Write_Byte(MPU9250_ADDR, MPU_PWR_MGMT1_REG, 0X01); // ����CLKSEL,PLL X��Ϊ�ο�
        MPU_Write_Byte(MPU9250_ADDR, MPU_PWR_MGMT2_REG, 0X00); // ���ٶ��������Ƕ�����
        MPU_Set_Rate(50);                                      // ���ò�����Ϊ50Hz
    } else
        return 1;
    // printf("init start\r\n");
    res = MPU_Read_Byte(AK8963_ADDR, MAG_WIA); // ��ȡAK8963 ID
    printf("res = %d\r\n",res);
    if (res == AK8963_ID) {
        MPU_Write_Byte(AK8963_ADDR, MAG_CNTL1, 0X11); // ����AK8963Ϊ���β���ģʽ
    } else
        return 1;
    printf("init start\r\n");
    uint8_t err = mpu_dmp_init();
    while (err) {
        printf("mpu_init_err:%d\r\n", err);
        mpu_dmp_init();
    }
    printf("mpu9250 Ok\r\n");
    return 0;
}

/**
 * @brief MPU6050 ���ݲ������˲�
 * @param _imu_data IMU ���ݽṹ��
 */
void MPU_Data_Get(IMU_Data_t *uimu_data)
{
    short uaccx, uaccy, uaccz;
    short ugyrox, ugyroy, ugyroz;
    MPU_Get_Accelerometer(&uaccx, &uaccy, &uaccz);
    MPU_Get_Gyroscope(&ugyrox, &ugyroy, &ugyroz);

    uimu_data->accel_x = uaccx;
    uimu_data->accel_y = uaccy;
    uimu_data->accel_z = uaccz;
    uimu_data->gyro_x  = ugyrox;
    uimu_data->gyro_y  = ugyroy;
    uimu_data->gyro_z  = ugyroz;

    float upitch, uroll, uyaw;
    while (mpu_mpl_get_data(&upitch, &uroll, &uyaw))
        ;
    uimu_data->pitch = upitch;
    uimu_data->roll  = uroll;
    uimu_data->yaw   = uyaw;

    uimu_data->tempreture = (float)MPU_Get_Temperature();
}
