/*
 * Shell Library (shell.h)
 */

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef SHELL_H_
#define SHELL_H_

#include <stdint.h>
#include <stdbool.h>
#include "tm4c123gh6pm.h"

#define MAX_CHARS 80
#define MAX_FIELDS 5

// PortA masks
#define UART_TX_MASK 2
#define UART_RX_MASK 1

typedef struct _USER_DATA
{
    char buffer[MAX_CHARS+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void getsUart0(USER_DATA* data);
void parseFields(USER_DATA* info);
char* getFieldString(USER_DATA* data, uint8_t fieldNumber);
uint32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber);
uint32_t stringToUint32_t(char* numStr);
void uint16_tToString(uint16_t num, char* numStr);
void uint32_tToString(uint32_t num, char* numStr);
void uint32_tToHexString(uint32_t num, char* numStr);
void makeLowercase(char* str);
bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments);
bool strCmp(char* str1, const char str2[]);
void strCopy(char* str1, const char str2[]);
void printf(char* str, uint32_t num);

#endif /* SHELL_H_ */
