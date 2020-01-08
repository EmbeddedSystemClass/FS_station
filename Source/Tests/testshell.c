/*
 * testshell.c
 *
 *  Created on: 2019-12-25
 *      Author: DELL
 */

#include "shell.h"
#include "uartStdio.h"
#include <xdc/runtime/Error.h>
#include <ti/sysbios/BIOS.h>
#include <xdc/runtime/System.h>
#include <ti/sysbios/knl/Task.h>

static SHELL_TypeDef shell;
extern uint8_t UARTSemGetc(uint8_t * val,uint32_t timeout);
extern void console_output(const char * chr, uint32_t len);
extern void console_shellInit();
//extern const unsigned int _shell_command_start = 0xc0000000;
//extern const unsigned int _shell_command_end = 0xc0000000;
static char getChar(char *c)
{
    UARTSemGetc(c,BIOS_WAIT_FOREVER);
    return 0;
}

static void shell_write(char c)
{
    console_output(&c,1);
}

void shell_open()
{
    Task_Handle taskhandle_shell = NULL;
    Error_Block eb;
    Task_Params taskParams;

    if(taskhandle_shell == NULL)
    {
        Error_init(&eb);
        Task_Params_init(&taskParams);
        taskParams.priority = 5;
        taskParams.stackSize = 2048;
        taskParams.instance->name = "shellTask";
        taskParams.arg0 = (xdc_UArg)(&shell);
        taskhandle_shell = Task_create(shellTask, &taskParams, &eb);
        if (taskhandle_shell == NULL) {
            System_printf("Task_create() failed!\n");
            BIOS_exit(0);
        }
    }
}

/*void shell_close()*/
/*{*/
    /*if(taskhandle_shell != NULL)*/
    /*{*/
        /*Task_delete(&taskhandle_shell);*/
        /*taskhandle_shell = NULL;*/
    /*}*/
/*}*/

/*void shell_readRegisterDefault()*/
/*{*/
    /*shell.read = getChar;*/
/*}*/

/*void shell_readRegister(shellRead cb)*/
/*{*/
    /*shell.read = cb;*/
/*}*/

void testShellTask(void)
{

    shell.read = getChar;
    shell.write = shell_write;

    shellInit(&shell);

    console_shellInit();

    shell_open();
}
