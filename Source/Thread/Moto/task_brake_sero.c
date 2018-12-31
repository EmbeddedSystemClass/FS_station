#include "task_brake_servo.h"
#include "task_moto.h"
#include "uartns550.h"
/*SYSBIOS includes*/
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <ti/sysbios/BIOS.h>

extern uint16_t getRPM(void);
extern uint8_t getBrake(void);
extern uint8_t getRailState(void);

/********************************************************************************/
/*          静态全局变量                                                              */
/********************************************************************************/
static uint8_t uBrake,uLastBrake;	//刹车信号
static int8_t deltaBrake;
static uint16_t uRPM,uLastRPM;		//定义转速
static int16_t sDeltaRPM,sBrake;
static float fDecTarget,fDecNow;
static int8_t servo_step=0;			//标记伺服位置
static int32_t pulseCount;			//刹车位置与伺服当前位置差
static int16_t pulseE4;
static int16_t pulseE0;

static uint8_t uRail;
static uint8_t railState;
static uint8_t keyState;


static Semaphore_Handle sem_txData;
static Semaphore_Handle sem_rxData;
static Semaphore_Handle sem_modbus;
static Semaphore_Handle sem_dataReady;
UART550_BUFFER * recvBuff = NULL;
static uint8_t ackStatus = MODBUS_ACK_OK;


static void UartServorIntrHandler(void *CallBackRef, u32 Event, unsigned int EventData)
{
	uint8_t Errors;
	uint16_t UartDeviceNum = *((u16 *)CallBackRef);
    uint8_t *NextBuffer;
    
	/*
	 * All of the data has been sent.
	 */
	if (Event == XUN_EVENT_SENT_DATA) {
        Semaphore_post(sem_txData);
	}



	if (Event == XUN_EVENT_RECV_TIMEOUT || Event == XUN_EVENT_RECV_DATA) {
        NextBuffer = UartNs550PushBuffer(UartDeviceNum,EventData);
        UartNs550Recv(UartDeviceNum, NextBuffer, BUFFER_MAX_SIZE);
		Semaphore_post(sem_dataReady);
	}


	/*
	 * Data was received with an error, keep the data but determine
	 * what kind of errors occurred.
	 */
	if (Event == XUN_EVENT_RECV_ERROR) {

		Errors = UartNs550GetLastErrors(UartDeviceNum);
	}
}


static void initSem(void)
{

	Semaphore_Params semParams;
	Semaphore_Params_init(&semParams);
	semParams.mode = Semaphore_Mode_COUNTING;
	sem_dataReady = Semaphore_create(0, &semParams, NULL);

    semParams.mode = Semaphore_Mode_BINARY;
    sem_rxData = Semaphore_create(0, &semParams, NULL);
    sem_txData = Semaphore_create(0, &semParams, NULL);
    sem_modbus = Semaphore_create(1, &semParams, NULL);
}
 




//u8 *data;//数据起始地址，用于计算 CRC 值
//u8 length; //数据长度
//返回 unsigned integer 类型的 CRC 值。
static uint16_t crc_chk(uint8_t *data, uint8_t length)
{
	uint8_t j;
	uint16_t crc_reg = 0xFFFF;
	while (length--)
	{
		crc_reg ^= *data++;
		for (j = 0; j < 8; j++)
		{
			if (crc_reg & 0x01)
			{
				crc_reg = (crc_reg >> 1) ^ 0xA001;
			}
			else
			{
				crc_reg = crc_reg >> 1;
			}
		}
	}
	return crc_reg;
}


static uint8_t modbusWriteReg(uint8_t id,uint16_t addr,uint16_t data)
{
	uint16_t udelay;
    modbusCmd_t sendData;

    Semaphore_pend(sem_modbus,BIOS_WAIT_FOREVER);   
    
    for(udelay = 3000;udelay>0;udelay--);           //增加发送间隔，保证控制器能识别

    sendData.id = id;
    sendData.cmd = 0x06;
    sendData.addrH = addr >> 8;
    sendData.addrL = addr & 0x00ff;
    sendData.dataH = data >> 8;
    sendData.dataL = data & 0x00ff;
    sendData.crc = crc_chk((uint8_t *)&sendData,6);
    
    /*使能RS485发送*/
    UartNs550RS485TxEnable(SERVOR_MOTOR_UART);

    /*清除接收信用量*/
    Semaphore_pend(sem_rxData,BIOS_NO_WAIT);
    /*清除接收数据计数*/
    //recvCnt = 0;

    /*发送数据*/
    UartNs550Send(SERVOR_MOTOR_UART,(uint8_t *)&sendData,8);

    
    /*等待发送完成*/
    Semaphore_pend(sem_txData,BIOS_WAIT_FOREVER);

    /*关闭RS485发送*/
    for(udelay = 3000;udelay>0;udelay--);           //延时等待最后一个串口数据被驱动到总线上
    UartNs550RS485TxDisable(SERVOR_MOTOR_UART);

    /*等待电机响应*/
    Semaphore_pend(sem_rxData,BIOS_WAIT_FOREVER);   //丢弃MAX3160发送时，回传的数据

    
    if(FALSE  == Semaphore_pend(sem_rxData,100))    //电机ACK超时时间：100ms
    {
    	ackStatus = MODBUS_ACK_TIMEOUT;
    }


    Semaphore_post(sem_modbus);
    
    return ackStatus;
}


static uint8_t modbusReadReg(uint8_t id,uint16_t addr,uint16_t *data)
{
	uint16_t udelay;
    modbusCmd_t sendData;

    Semaphore_pend(sem_modbus,BIOS_WAIT_FOREVER);   
    
    for(udelay = 3000;udelay>0;udelay--);           //增加发送间隔，保证控制器能识别

    sendData.id = id;
    sendData.cmd = 0x03;
    sendData.addrH = addr >> 8;
    sendData.addrL = addr & 0x00ff;
    sendData.dataH = 0;
    sendData.dataL = 1;
    sendData.crc = crc_chk((uint8_t *)&sendData,6);
    
    /*使能RS485发送*/
    UartNs550RS485TxEnable(SERVOR_MOTOR_UART);

    /*清除接收信用量*/
    Semaphore_pend(sem_rxData,BIOS_NO_WAIT);
    /*清除接收数据计数*/
    //recvCnt = 0;

    /*发送数据*/
    UartNs550Send(SERVOR_MOTOR_UART,(uint8_t *)&sendData,8);

    
    /*等待发送完成*/
    Semaphore_pend(sem_txData,BIOS_WAIT_FOREVER);

    /*关闭RS485发送*/
    for(udelay = 3000;udelay>0;udelay--);           //延时等待最后一个串口数据被驱动到总线上
    UartNs550RS485TxDisable(SERVOR_MOTOR_UART);

    /*等待电机响应*/
    Semaphore_pend(sem_rxData,BIOS_WAIT_FOREVER);   //丢弃MAX3160发送时，回传的数据

    
    if(FALSE  == Semaphore_pend(sem_rxData,100))    //电机ACK超时时间：100ms
    	ackStatus = MODBUS_ACK_TIMEOUT;
    else
        *data = (uint16_t)recvBuff->Buffer[4] << 8 + recvBuff->Buffer[5];

    Semaphore_post(sem_modbus);
    
    return ackStatus;
}


static void recvDataTask(void)
{
    
    uint16_t calcCrc;
    uint16_t recvCrc;
    while(1)
    {
        Semaphore_pend(sem_dataReady, BIOS_WAIT_FOREVER);
        recvBuff = UartNs550PopBuffer(SERVOR_MOTOR_UART);
        
        if(recvBuff->Length == 8 || recvBuff->Length == 6)
        {
            calcCrc = crc_chk((uint8_t *)recvBuff->Buffer,recvBuff->Length - 2);
            //recvCrc = (uint16_t *)&(recvBuff->Buffer[recvBuff->Length-2]);
            recvCrc = ((uint16_t)recvBuff->Buffer[recvBuff->Length-2]) + (((uint16_t)recvBuff->Buffer[recvBuff->Length-1])<<8);
            if(recvCrc == calcCrc)
            {
                if(recvBuff->Length == 8)
                    ackStatus = MODBUS_ACK_OK;
                else
                    ackStatus = MODBUS_ACK_NOTOK;
            }
            else
                ackStatus = MODBUS_ACK_CRC_ERR;
        }
        else
        {
            ackStatus = MODBUS_ACK_FRAME_ERR;
        }

        Semaphore_post(sem_rxData);
    }
}



void vBrakeServoTask(void *param)
{
	uint8_t state;

    /*Pn070 Son使能驱动器*/
    modbusWriteReg(0x01,0x0046,0x7FFE);

    /*Pn071 内部位置0,并取消触发*/
    modbusWriteReg(0x01,0x0047,0x7FFF);
	
	/*Pn120 内部位置0(万)*/
    modbusWriteReg(0x01,0x0078,0x0000);

    //Pn121	内部位置0（个）
    modbusWriteReg(0x01,0x0079,0x0000);
	
	while (1)
	{	
		Task_sleep(BRAKETIME);
		uBrake = getBrake();
		fDecTarget = uBrake*AMAX/100;				//计算目标减速度
		uRPM = getRPM();							//转速
		fDecNow=PI*DM*(uRPM-uLastRPM)/(60*KM*BRAKETIME/1000);//
		uLastRPM=uRPM;
		/*	总行程45000个脉冲
			每步450脉冲
		*/
		if(servo_step!=uBrake)
		{
			pulseCount=uBrake-servo_step;
			pulseCount=430*(uBrake-servo_step);		//原来450
			pulseE4=pulseCount/10000;				//万位
			pulseE0=pulseCount-10000*pulseE4;		//个位
			while(1)
			{
                state = modbusWriteReg(0x01,0x0046,0x7FFE);
				
				if(state != MODBUS_ACK_OK)
                    break;
                else;

                state = modbusWriteReg(0x01,0x0047,0x7FFF);
				
				if(state != MODBUS_ACK_OK)
                    break;
                else;

                state = modbusWriteReg(0x01,0x0078,pulseE4);
				
				if(state != MODBUS_ACK_OK)
                    break;
                else;

                state = modbusWriteReg(0x01,0x0079,pulseE0);
				
				if(state != MODBUS_ACK_OK)
                    break;
                else;
			
				state = modbusWriteReg(0x01,0x0047,0x7BFF);
                
                if(state == MODBUS_ACK_OK)
                {
				    servo_step=uBrake;		//标记伺服位置
				    break;
                }
				else
				{
                    break;
				}
				
			}
		}

	}
}


static void vChangeRailTask(void)
{
	uint8_t state;
    uint16_t recvReg;
	static uint8_t step=0;
	static uint8_t preach=0;
	static uint8_t change_en=1;
	static uint8_t SCount=0;
	static uint8_t modbusCount=0;
	
	while(1)
	{
		Task_sleep(50);
		
		railState = getRailState();

        #if 0
		if(keyState == LEFTRAIL)	
            railState = LEFTRAIL;         //置轮子处于轨道状态
		else if(keyState == RIGHTRAIL)	
            railState=RIGHTRAIL;
        else;
        #endif
		
		uRail=getRail();
        
		preach=0;
		SCount=0;
		while((uRail == 1) && (railState == RIGHTRAIL) && change_en == 1)//正转270度
		{
			switch(step)
			{
				case 0:
                    //选择内部位置：Pn071
                    state = modbusWriteReg(0x02,0x0047,0x7FFF);

                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
					}
                    else;
                    
					break;
				case 1:
                    //使能伺服电机：Pn070
                    state = modbusWriteReg(0x02,0x0046,0x7FFE);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
					}
                    else;
					
					break;
				case 2:
				    //触发内部位置，电机转动：Pn071
                    state = modbusWriteReg(0x02,0x0047,0x7BFF);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
                        Task_sleep(500);
					}
                    else;
					
					break;
				case 3:
                    //读取Dn018，判断bit3-Preach
                    state = modbusReadReg(0x02,0x0182,&recvReg);

                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
                    else
                    {
                        SCount++;
                        if(state == MODBUS_ACK_OK)
                        {
                            if(recvReg & 0x0800)//取Bit3 Preach,Bit 位为 0，表示功能为 ON 状态，为 1 则是 OFF 状态
							{
								//位置偏差，发出偏差报警
								preach=0;
							}
							else
							{
								//到达指定位置，进行下一步
								preach=1;
								step++;
								modbusCount=0;
							}
                        }
                        else;
                            
                    }
                
					break;
				case 4:
				    //关闭伺服电机：Pn070
                    state = modbusWriteReg(0x02,0x0046,0x7FFF);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
                        if(preach)
						{
							railState=LEFTRAIL;
							preach=0;//清除状态
						}
						else
						{
							change_en=0;//指令完成但位置偏差时，禁止变轨
						}
						step=0;//复位步骤
						modbusCount=0;
						
					}
                    else;
					
					break;
			}		
		}
		while((uRail == 2 )&&(railState == LEFTRAIL) && change_en == 1)//反转270度
		{
			switch(step)
			{
				case 0:
                    //选择内部位置：Pn071
                    state = modbusWriteReg(0x02,0x0047,0x7FFF);

                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
					}
                    else;
                    
					break;
				case 1:
                    //使能伺服电机：Pn070
                    state = modbusWriteReg(0x02,0x0046,0x7FFE);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
					}
                    else;
                    
					break;
				case 2:
                    //触发内部位置，电机转动：Pn071
                    state = modbusWriteReg(0x02,0x0047,0x7AFF);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
						//写成功，进行下一步
						step++;
						modbusCount=0;
                        Task_sleep(500);
					}
                    else;
                    
					break;
				case 3:
                    //读取Dn018，判断bit3-Preach
                    state = modbusReadReg(0x02,0x0182,&recvReg);

                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
                    else
                    {
                        SCount++;
                        if(state == MODBUS_ACK_OK)
                        {
                            if(recvReg & 0x0800)//取Bit3 Preach,Bit 位为 0，表示功能为 ON 状态，为 1 则是 OFF 状态
							{
								//位置偏差，发出偏差报警
								preach=0;
							}
							else
							{
								//到达指定位置，进行下一步
								preach=1;
								step++;
								modbusCount=0;
							}
                        }
                        else;
                            
                    }
                    
					break;
				case 4://失能伺服电机
				    //关闭伺服电机：Pn070
                    state = modbusWriteReg(0x02,0x0046,0x7FFF);
                    
                    if(state == MODBUS_ACK_TIMEOUT)
                        modbusCount++;
					else if(state == MODBUS_ACK_OK)
					{
                        if(preach)
						{
							railState=RIGHTRAIL;
							preach=0;//清除状态
						}
						else
						{
							change_en=0;//指令完成但位置偏差时，禁止变轨
						}
						step=0;//复位步骤
						modbusCount=0;
						
					}
                    else;
					break;
			}		
		}
		
		
	}
}


void testBrakeServoInit()
{
    Task_Handle task;
	Task_Params taskParams;
	
	//初始化串口
	UartNs550SetMode(SERVOR_MOTOR_UART, UART_RS485_MODE);
	UartNs550Init(SERVOR_MOTOR_UART,UartServorIntrHandler);
    
	//初始化信号量
	initSem();

	Task_Params_init(&taskParams);
	taskParams.priority = 5;
	taskParams.stackSize = 2048;
    
	task = Task_create(recvDataTask, &taskParams, NULL);
	if (task == NULL) {
		System_printf("Task_create() failed!\n");
		BIOS_exit(0);
	}

    task = Task_create(vBrakeServoTask, &taskParams, NULL);
	if (task == NULL) {
		System_printf("Task_create() failed!\n");
		BIOS_exit(0);
	}

    task = Task_create(vChangeRailTask, &taskParams, NULL);
    if (task == NULL) {
        System_printf("Task_create() failed!\n");
        BIOS_exit(0);
    }

}
