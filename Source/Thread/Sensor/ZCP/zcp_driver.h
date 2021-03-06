/*
 * zcp_driver.h
 *
 *  Created on: 2019-4-25
 *      Author: DELL
 */

#ifndef ZCP_DRIVER_H_
#define ZCP_DRIVER_H_

#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Mailbox.h>
#include "uartns550.h"
#include "common.h"

#define MAX_ZCP_DEVIVE_NUMS (2)
#define MAX_ZCP_PACKET_SIZE (64)

/*
 * ZIGBEE报文字段定义
 */
#define ZIGBEE_HEAD (0xAA)
#define ZIGBEE_TAIL (0x55)
#define ZIGBEE_TYPE_MASTER  (0xD1)
#define ZIGBEE_TYPE_ACK     (0xD0)
#define ZIGBEE_ACK_OK       (0x00)

/*
 * 超时定义
 */
#define ZCP_ACK_TIMEOUT     (50)
#define ZCP_UART_SEND_TIMEOUT   (50)

/*
 * TYPE字段
 * 0xfd:内部错误报文
 */
#define ZCP_TYPE_ACK    (0xfd)
#define ZCP_TYPE_ERROR  (0xfe)

/*
 * ZCP错误定义
 */
#define ZCP_ERR_SEND_TIMEOUT (0x01)
#define ZCP_ERR_ACK_TIMEOUT  (0x02)
#define ZCP_ERR_ACK_ERROR   (0x03)

#define ZCP_INIT_FAILED (1)
#define ZCP_INIT_OK     (0)

#define ZCP_MBOX_DEPTH (16)

typedef enum  {
    STS_HEAD,
    STS_TYPE,
    STS_ADDR,
    STS_USR_LEN,
    STS_USR_TYPE,
    STS_SRC_ADDR_L,
    STS_SRC_ADDR_H,
    STS_DATA,
    STS_USR_CRC,
    STS_TAIL
}ZCPUnpackState_t;

typedef struct
{
    uint8_t addrH; /*地址高字节*/
    uint8_t addrL; /*地址低字节*/
    uint8_t len;
    uint8_t type;
    uint16_t src_addr;/*添加发送源地址，解决模块之间数据串扰问题*/
    uint8_t data[MAX_ZCP_PACKET_SIZE];
    uint8_t extSec; /*扩展字段*/
    Semaphore_Handle ackSem;
}ZCPPacket_t;

typedef struct _zcp_errors_{
    uint32_t sendCnt;
    uint32_t recvCnt;
    uint32_t ackTimeout;
    uint32_t ackErr;
    uint32_t sendErr;
    uint32_t userPacketErr;
    uint32_t userPacketCrcErr;
    uint32_t zigbeePacketErr;
    uint32_t stateErr;
}ZCPStatistic_t;

typedef struct _zcp_instance_{
    uint8_t uartDevNum;
    uartDataObj_t uartRxData;
    Mailbox_Handle uartRecvMbox;
    Semaphore_Handle uartSendSem;
    Semaphore_Handle ackSem;
    Mailbox_Handle userRecvMbox;
    Mailbox_Handle userSendMbox;
    uint8_t ackSts;
    ZCPStatistic_t stat;
    uint16_t src_addr;
}ZCPInstance_t;

typedef struct
{
    uint16_t addr;
    uint8_t type;
    uint8_t data[MAX_ZCP_PACKET_SIZE-4];
    uint8_t len;
}ZCPUserPacket_t;

typedef struct{
    uint8_t isUsed;
    ZCPInstance_t *pInst;
}ZCPCfgTable_t;

uint8_t ZCPInit(ZCPInstance_t *pInst, uint8_t devNum,uint8_t uartDevNum,uint16_t src_addr);
Bool ZCPPend(ZCPInstance_t *pInst,ZCPPacket_t *pUserPacket,UInt timeout);
Bool ZCPPost(ZCPInstance_t *pInst,ZCPPacket_t *pUserPacket,UInt timeout);
Bool ZCPRecvPacket(ZCPInstance_t *pInst,
        ZCPUserPacket_t *userPacket,
        int32_t *timestamp,
        UInt timeout);
Bool ZCPSendPacket(ZCPInstance_t *pInst,
        ZCPUserPacket_t *userPacket,
        Semaphore_Handle ackSem,
        UInt timeout
        );
#endif /* ZCP_DRIVER_H_ */
