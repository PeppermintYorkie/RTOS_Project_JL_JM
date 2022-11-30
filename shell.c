/*
 * Shell Library (shell.c)
 */

/*
 * To Do's:
 * - write combined alpha-numeric case for parseFields to allow for alpha-numeric RTOS arguments
 */

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include "shell.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void getsUart0(USER_DATA* data)
{
    uint8_t count = 0;
    uint8_t cont_loop = 1;
    char entry;

    while(cont_loop && (count < MAX_CHARS))
    {
        entry = getcUart0();

        if((entry == 8 || entry == 127) && count > 0) // Backspace
        {
            count--;
        }
        else if(entry == 13) // Enter
        {
            cont_loop = 0;
        }
        else if(entry >= 32) // Printable Character
        {
            data->buffer[count] = entry;
            count++;
        }
    }

    data->buffer[count] = 0;

    return;
}

void parseFields(USER_DATA* info)
{
    // Alpha set: {A-Z = 65-90} and {a-z = 97-122} and {_ = 95}
    // Numeric set: {0-9 = 48-57; - = 45; . = 46}
    // Delimiter set: {everything from 32-126 inclusive, not in previous sets}

    char chr = 0, nchr = 0;
    uint8_t i = 0, arr_pos = 0;
    info->fieldCount = 0;

    // make sure arrays are clean
    for(i = 0; i < MAX_FIELDS; i++)
    {
        info->fieldPosition[i] = 255;
        info->fieldType[i] = 0;
    }

    // parse fields
    while((info->fieldCount < MAX_FIELDS) && (info->buffer[arr_pos] != 0))
    {
        chr = info->buffer[arr_pos];
        nchr = info->buffer[arr_pos + 1];

        if(chr == 48 && nchr == 120) // Hex
        {
            info->fieldPosition[info->fieldCount] = arr_pos;
            info->fieldType[info->fieldCount] = 'h';
            (info->fieldCount)++;

            arr_pos += 2;
            chr = info->buffer[arr_pos];
            while(((chr >= 65 && chr <= 70) || (chr >= 97 && chr <= 102) || (chr >= 48 && chr <= 57)) && (chr != 0))
            {
                arr_pos++;
                chr = info->buffer[arr_pos];
            }
        }
        else if((chr >= 65 && chr <= 90) || (chr >= 97 && chr <= 122) || (chr == 95)) // Alpha
        {
            // fill in fieldPosition and fieldType arrays using fieldCount
            // increment fieldCount
            // increment arr_pos until finding a chr out of bounds of if-statement

            info->fieldPosition[info->fieldCount] = arr_pos;
            info->fieldType[info->fieldCount] = 'a'; // could use 97
            (info->fieldCount)++;

            while(((chr >= 65 && chr <= 90) || (chr >= 97 && chr <= 122)) && (chr != 0))
            {
                arr_pos++;
                chr = info->buffer[arr_pos];
            }
        }
        else if(((chr == 45) || (chr == 46) || (chr >= 48 && chr <= 57)) && !(chr == 48 && nchr == 120)) // Numeric, not a hex string
        {
            info->fieldPosition[info->fieldCount] = arr_pos;
            info->fieldType[info->fieldCount] = 'n'; // could use 110
            (info->fieldCount)++;

            while(((chr == 45) || (chr == 46) || (chr >= 48 && chr <= 57)) && (chr != 0))
            {
                arr_pos++;
                chr = info->buffer[arr_pos];
            }
        }
        else // Delimiter
        {
            // convert to null and increment arr_pos
            info->buffer[arr_pos] = 0;
            arr_pos++;
        }
    }

    info->buffer[arr_pos] = 0;
}

char* getFieldString(USER_DATA* data, uint8_t fieldNumber)
{
    char fieldStr[MAX_CHARS + 1] = {};
    uint8_t i = 0;
    uint8_t posn = 0;
    uint8_t nxt_fld = fieldNumber + 1;
    int32_t nxt_posn = -1;

    if(fieldNumber < data->fieldCount)
    {
        posn = data->fieldPosition[fieldNumber];

        if(nxt_fld < data->fieldCount)
            nxt_posn = data->fieldPosition[nxt_fld];

        while((data->buffer[posn] != 0) && (posn != nxt_posn))
        {
            fieldStr[i] = data->buffer[posn];
            i++;
            posn++;
        }

        fieldStr[i] = 0;
    }
    else
    {
        fieldStr[i] = 0;
    }

    return fieldStr;
}

uint32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber)
{
    char fieldStr[MAX_CHARS + 1] = {};
    uint8_t i = 0;
    uint8_t posn = 0;
    uint8_t nxt_fld = fieldNumber + 1;
    uint32_t fieldInt = 0;
    int32_t nxt_posn = -1;

    if((fieldNumber < data->fieldCount) && (data->fieldType[fieldNumber] == 'n'))
    {
        posn = data->fieldPosition[fieldNumber];

        if(nxt_fld < data->fieldCount)
            nxt_posn = data->fieldPosition[nxt_fld];

        while((data->buffer[posn] != 0) && (posn != nxt_posn))
        {
            fieldStr[i] = data->buffer[posn];
            i++;
            posn++;
        }

        fieldStr[i] = 0;

        fieldInt = stringToUint32_t(fieldStr);
    }

    return fieldInt;
}

uint32_t stringToUint32_t(char* numStr)
{
    uint8_t i = 0;
    int8_t count = -1;
    uint32_t res = 0;

    for(i = 0; numStr[i] != 0; i++)
        count++;

    for(i = 0; i < count; i++)
        res = (res + ((int)numStr[i] - 48)) * 10;

    res = (res + ((int)numStr[i] - 48));
    return res;
}

void uint16_tToString(uint16_t num, char* numStr)
{
    uint16_t i = 0;
    uint8_t j = 0;
    char preStr[6];
    char a = 48, b = 48, c = 48, d = 48, e = 48, f = 0;

    while(i < num && e < 57)
    {
        i++;
        e++;

        if(i < num && d < 57 && e == 57)
        {
            i++;
            d++;
            e = 48;
        }

        if(i < num && c < 57 && d == 57 && e == 57)
        {
            i++;
            c++;
            d = 48;
            e = 48;
        }

        if(i < num && b < 57 && c == 57 && d == 57 && e == 57)
        {
            i++;
            b++;
            c = 48;
            d = 48;
            e = 48;
        }

        if(i < num && a < 57 && b == 57 && c == 57 && d == 57 && e == 57)
        {
            i++;
            a++;
            b = 48;
            c = 48;
            d = 48;
            e = 48;
        }
    }

    preStr[0] = a;
    preStr[1] = b;
    preStr[2] = c;
    preStr[3] = d;
    preStr[4] = e;
    preStr[5] = f;
    i = 0;

    while(preStr[i] == 48)
        i++;

    if(preStr[i] == 0)
    {
        numStr[j] = 48;
        j++;
    }
    else
    {
        while(preStr[i] != 0)
        {
            numStr[j] = preStr[i];
            i++;
            j++;
        }
    }

    numStr[j] = 0;

    return;
}

void uint32_tToString(uint32_t num, char* numStr)
{
    uint32_t x = 0;
    uint8_t y = 0;
    char preStr[11];
    char a = 48, b = 48, c = 48, d = 48, e = 48, f = 48, g = 48, h = 48, i = 48, j = 48, k = 0;

    while(x < num && j < 57)
    {
        x++;
        j++;

        if(x < num && i < 57 && j == 57)
        {
            x++;
            i++;
            j = 48;
        }

        if(x < num && h < 57 && i == 57 && j == 57)
        {
            x++;
            h++;
            i = 48;
            j = 48;
        }

        if(x < num && g < 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            g++;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && f < 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            f++;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && e < 57 && f == 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            e++;
            f = 48;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && d < 57 && e == 57 && f == 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            d++;
            e = 48;
            f = 48;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && c < 57 && d == 57 && e == 57 && f == 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            c++;
            d = 48;
            e = 48;
            f = 48;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && b < 57 && c == 57 && d == 57 && e == 57 && f == 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            b++;
            c = 48;
            d = 48;
            e = 48;
            f = 48;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }

        if(x < num && a < 57 && b == 57 && c == 57 && d == 57 && e == 57 && f == 57 && g == 57 && h == 57 && i == 57 && j == 57)
        {
            x++;
            a++;
            b = 48;
            c = 48;
            d = 48;
            e = 48;
            f = 48;
            g = 48;
            h = 48;
            i = 48;
            j = 48;
        }
    }

    preStr[0] = a;
    preStr[1] = b;
    preStr[2] = c;
    preStr[3] = d;
    preStr[4] = e;
    preStr[5] = f;
    preStr[6] = g;
    preStr[7] = h;
    preStr[8] = i;
    preStr[9] = j;
    preStr[10] = k;
    x = 0;

    while(preStr[x] == 48)
        x++;

    if(preStr[x] == 0)
    {
        numStr[y] = 48;
        y++;
    }
    else
    {
        while(preStr[x] != 0)
        {
            numStr[y] = preStr[x];
            x++;
            y++;
        }
    }

    numStr[y] = 0;

    return;
}

uint32_t hexStringToUint32_t(char *str)
{
    uint8_t i, digit;
    uint32_t result = 0;

    for (i = 2; i < 10; i++)
    {
        result <<= 4;
        digit = *str++;
        if (digit >= '0' && digit <= '9')
            result += digit - '0';
        else if (digit >= 'a' && digit <= 'f')
            result += digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F')
            result += digit - 'A' + 10;
    }
    return result;
}

void uint32_tToHexString(uint32_t num, char *numStr)
{
    char hexVals[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    uint8_t shiftVal = 28;
    uint32_t marker = 0;
    uint8_t i = 0;

    numStr[i] = '0';
    i++;
    numStr[i] = 'x';

    for(i = 2; i < 10; i++)
    {
        marker = (num & (0xF << shiftVal));
        marker = marker >> shiftVal;
        numStr[i] = hexVals[marker];
        shiftVal -= 4;
    }

    numStr[i] = 0;
    return;
}

void makeLowercase(char* str)
{
    uint8_t i = 0;
    for(i = 0; str[i] != 0; i++)
        if(str[i] >= 65 && str[i] <= 90)
            str[i] = str[i] + 32;
}

bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments)
{
    bool min_args = false;
    bool cmd_match = false;
    bool is_cmd = false;
    bool cont = false;
    uint8_t j = 0;
    char* cmd_fld = getFieldString(data, 0);

    if((data->fieldCount - 1) >= minArguments)
        min_args = true;

    if(cmd_fld[j] != 0)
        cont = true;

    makeLowercase(cmd_fld);

    while((strCommand[j] != 0) && cont)
    {
        if(strCommand[j] == cmd_fld[j])
        {
            cmd_match = true;
            j++;
        }
        else
        {
            cmd_match = false;
            cont = false;
        }
    }

    if(cmd_match)
        if(cmd_fld[j] != 0)
            cmd_match = false;

    if(min_args && cmd_match)
        is_cmd = true;

    return is_cmd;
}

bool strCmp(char* str1, const char str2[])
{
    bool match = false;
    bool cont = false;
    uint8_t j = 0;

    if(str1[j] != 0)
        cont = true;

    while((str2[j] != 0) && cont)
    {
        if(str2[j] == str1[j])
        {
            match = true;
            j++;
        }
        else
        {
            match = false;
            cont = false;
        }
    }

    if(match)
        if(str1[j] != 0)
            match = false;

    return match;
}

void strCopy(char* str1, const char str2[])
{
    uint8_t i = 0;
    for(i = 0; str2[i] != 0; i++)
        str1[i] = str2[i];
    str1[i] = 0;
}

void printf(char* str, uint32_t num)
{
    uint8_t i = 0;
    char numStr[11];

    for(i = 0; str[i] != 0; i++)
    {
        if(str[i] != 37)
            putcUart0(str[i]);
        else
        {
            i++;
            if(str[i] == 100)
            {
                uint32_tToString(num, numStr);
                putsUart0(numStr);
            }
            else if(str[i] == 104)
            {
                uint32_tToHexString(num, numStr);
                putsUart0(numStr);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Commented-Out Reference Code
//-----------------------------------------------------------------------------

/*
float stringToFloat(char* numString)
{
    int32_t sign = 1;
    uint8_t i = 0;
    int32_t count = -1;
    float num = 0;

    if(numString[i] == 45)
    {
        sign = -1;
        i++;
    }

    while((numString[i] != 46) && (numString[i] != 0))
    {
        if((numString[i] >= 48) && (numString[i] <= 57))
            count++;

        i++;
    }

    i = 0;

    while(numString[i] != 0)
    {
        if((numString[i] >= 48) && (numString[i] <= 57))
        {
            num = num + (((int)numString[i] - 48) * (pow(10, count)));  //****remove pow function
            count--;
            i++;
        }
        else
            i++;
    }

    num = num*sign;

    return num;
}

int16_t floatToInt16_t(float num_float)
{
    int16_t num_int = 0;

    if(num_float > 0)
        num_int = (int)(num_float + 0.5);
    else
        num_int = (int)(num_float - 0.5);

    return num_int;
}

int32_t stringToInt32_t(char* numString)
{
    int32_t sign = 1;
    uint8_t i = 0;
    int32_t count = -1;
    float num_float = 0;
    int32_t num_int = 0;

    if(numString[i] == 45)
    {
        sign = -1;
        i++;
    }

    while((numString[i] != 46) && (numString[i] != 0))
    {
        if((numString[i] >= 48) && (numString[i] <= 57))
            count++;

        i++;
    }

    i = 0;

    while(numString[i] != 0)
    {
        if((numString[i] >= 48) && (numString[i] <= 57))
        {
            num_float = num_float + (((int)numString[i] - 48) * (pow(10, count)));  //****remove pow function
            count--;
            i++;
        }
        else
            i++;
    }

    num_float = num_float*sign;

    if(num_float > 0)
        num_int = (int)(num_float + 0.5);
    else
        num_int = (int)(num_float - 0.5);

    return num_int;
}

uint32_t strUint32t(char* numStr)
{
    uint8_t i = 0;
    uint32_t res = 0;
    int8_t count = -1;

    for(i = 0; numStr[i] != 0; i++)
        count++;

    for(i = 0; numStr[i] != 0; i++)
    {
        res = res + (((int)numStr[i] - 48) * (pow(10, count)));     //****remove pow function
        count--;
    }

    return res;
}

uint32_t strToUint32_t(char* numStr)
{
    uint8_t i = 0;
    uint8_t j = 0;
    uint32_t chr = 0;
    uint32_t res = 0;
    int8_t count = -1;

    for(i = 0; numStr[i] != 0; i++)
        count++;

    for(i = 0; numStr[i] != 0; i++)
    {
        chr = ((int)numStr[i] - 48);

        for(j = 0; j < count; j++)
            chr = chr * 10;

        res = res + chr;
        count--;
    }

    return res;
}
*/

//write float to string function(?)
//write float to int32_t function(?)
//write int16_t to string function(?)
//write int32_t to string function(?)
