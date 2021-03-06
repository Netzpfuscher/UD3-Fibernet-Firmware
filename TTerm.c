#include <xc.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h""
#include "task.h"
#include "UART.h"
#include "TTerm.h"
#include "TTerm_cmd.h"

TermCommandDescriptor ** TERM_cmdList;
uint8_t TERM_cmdCount = 0;

TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, const char * usr){
    TERMINAL_HANDLE * ret = pvPortMalloc(sizeof(TERMINAL_HANDLE));
    memset(ret, 0, sizeof(TERMINAL_HANDLE));
    ret->inputBuffer = pvPortMalloc(TERM_INPUTBUFFER_SIZE);
    ret->print = printFunction;
    ret->currUserName = pvPortMalloc(strlen(usr) + 1 + strlen(UART_getVT100Code(_VT100_FOREGROUND_COLOR, _VT100_YELLOW)) + strlen(UART_getVT100Code(_VT100_RESET_ATTRIB, 0)));
    sprintf(ret->currUserName, "%s%s%s", UART_getVT100Code(_VT100_FOREGROUND_COLOR, _VT100_BLUE), usr, UART_getVT100Code(_VT100_RESET_ATTRIB, 0));
    ret->currEscSeqPos = 0xff;
    
    //if this is the first console we initialize we need to add the static commands
    if(TERM_cmdCount == 0){
        TERM_addCommand(CMD_help, "help", "Displays this help message", 0);
        TERM_addCommand(CMD_cls, "cls", "Clears the screen", 0);
        TERM_addCommand(CMD_top, "top", "shows performance stats", 0);
        TERM_addCommand(CMD_getMacState, "getMacState", "reads MAC information", 0);
        
        // dump the now sorted list for debugging
        // TODO remove this
        uint8_t currPos = 0;
        for(;currPos < TERM_cmdCount; currPos++){
            (*printFunction)("pos %d = %s\r\n", currPos, TERM_cmdList[currPos]->command);
        }
    }
    
#ifdef TERM_ENABLE_STARTUP_TEXT
    //TODO VT100 reset at boot
    //TODO add min start frame to signal that debugging started and print this again
    //TODO colors in the boot message
    TERM_sendVT100Code(ret, _VT100_RESET, 0); TERM_sendVT100Code(ret, _VT100_CURSOR_POS1, 0);
    (*ret->print)("\r\n\n\n%s\r\n", TERM_startupText1);
    (*ret->print)("%s\r\n", TERM_startupText2);
    (*ret->print)("%s\r\n", TERM_startupText3);
    (*ret->print)("\r\n%s%sDISCLAIMER%s: This is only a POC. All commands will call the test handler at the moment\r\n", UART_getVT100Code(_VT100_BACKGROUND_COLOR, _VT100_RED), UART_getVT100Code(_VT100_BLINK, 0), UART_getVT100Code(_VT100_RESET_ATTRIB, 0));
    (*ret->print)("\r\n\r\n%s@%s>", ret->currUserName, TERM_DEVICE_NAME);
#endif
    return ret;
}

void TERM_destroyHandle(TERMINAL_HANDLE * handle){
    
}

void TERM_printDebug(TERMINAL_HANDLE * handle, char * format, ...){
    //TODO make this nicer... we don't need a double buffer allocation, we should instead send the va_list to the print function. But it is way to late at night for me to code this now...
    //TODO implement a debug level control in the terminal handle (permission level?)
    va_list arg;
    va_start(arg, format);
    
    uint8_t * buff = (uint8_t*) pvPortMalloc(256);
    uint32_t length = vsprintf(buff, format, arg);
    
    (*handle->print)("\r\n%s", buff);
    
    if(handle->currBufferLength == 0){
        (*handle->print)("%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
    }else{
        (*handle->print)("%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
        if(handle->inputBuffer[handle->currBufferPosition] != 0) TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
    }
    
    vPortFree(buff);
    
    va_end(arg);
}

uint8_t TERM_processBuffer(uint8_t * data, uint16_t length, TERMINAL_HANDLE * handle){
    uint16_t currPos = 0;
    for(;currPos < length; currPos++){
        //(*handle->print)("checking 0x%02x\r\n", data[currPos]);
        if(handle->currEscSeqPos != 0xff){
            if(handle->currEscSeqPos == 0){
                if(data[currPos] == '['){
                    handle->escSeqBuff[handle->currEscSeqPos++] = data[currPos];
                }else{
                    switch(data[currPos]){
                        case 'c':
                            handle->currEscSeqPos = 0xff;
                            TERM_handleInput(_VT100_RESET, handle);
                            break;
                        case 0x1b:
                            handle->currEscSeqPos = 0;
                            break;
                        default:
                            handle->currEscSeqPos = 0xff;
                            TERM_handleInput(0x1b, handle);
                            TERM_handleInput(data[currPos], handle);
                            break;
                    }
                }
            }else{
                if(isACIILetter(data[currPos])){
                    if(data[currPos] == 'n'){
                        if(handle->currEscSeqPos == 2){
                            if(handle->escSeqBuff[0] == '5'){        //Query device status
                            }else if(handle->escSeqBuff[0] == '6'){  //Query cursor position
                            }
                        }
                    }else if(data[currPos] == 'c'){
                        if(handle->currEscSeqPos == 1){              //Query device code
                        }
                    }else if(data[currPos] == 'F'){
                        if(handle->currEscSeqPos == 1){              //end
                            TERM_handleInput(_VT100_KEY_END, handle);
                        }
                    }else if(data[currPos] == 'H'){
                        if(handle->currEscSeqPos == 1){              //pos1
                            TERM_handleInput(_VT100_KEY_POS1, handle);
                        }
                    }else if(data[currPos] == 'C'){                      //cursor forward
                        if(handle->currEscSeqPos > 1){              
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_FORWARD, handle);
                        }
                    }else if(data[currPos] == 'D'){                      //cursor backward
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_BACK, handle);
                        }
                    }else if(data[currPos] == 'A'){                      //cursor up
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_UP, handle);
                        }
                    }else if(data[currPos] == 'B'){                      //cursor down
                        if(handle->currEscSeqPos > 1){                 
                            handle->escSeqBuff[handle->currEscSeqPos] = 0;
                        }else{
                            TERM_handleInput(_VT100_CURSOR_DOWN, handle);
                        }
                    }else if(data[currPos] == 'Z'){                      //shift tab or ident request(at least from exp.; didn't find any official spec containing this)
                        TERM_handleInput(_VT100_BACKWARDS_TAB, handle);
                    }else{                      //others
                        handle->escSeqBuff[handle->currEscSeqPos+1] = 0;
                    }
                    handle->currEscSeqPos = 0xff;
                }else{
                    handle->escSeqBuff[handle->currEscSeqPos++] = data[currPos];
                }
            }
        }else{
            if(data[currPos] == 0x1B){     //ESC for V100 control sequences
                handle->currEscSeqPos = 0;
            }else{
                TERM_handleInput(data[currPos], handle);
            }
        }
    }
}

unsigned isACIILetter(char c){
    return (c > 64 && c < 91) || (c > 96 && c < 122);
}

uint8_t TERM_handleInput(uint16_t c, TERMINAL_HANDLE * handle){
    //(*handle->print)("received 0x%04x\r\n", c);
    if(handle->currProgram != NULL){
        //call the handler of the current override
        uint8_t currRetCode = (*handle->currProgram->inputHandler)(handle, c);
        
        switch(currRetCode){
            case TERM_CMD_EXIT_SUCCESS:
                (*handle->print)("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
                break;

            case TERM_CMD_EXIT_ERROR:
                (*handle->print)("\r\nTask returned with error code %d\r\n%s@%s>", currRetCode, handle->currUserName, TERM_DEVICE_NAME);
                break;

            case TERM_CMD_EXIT_NOT_FOUND:
                (*handle->print)("\"%s\" is not a valid command. Type \"help\" to see a list of available ones\r\n%s@%s>", handle->inputBuffer, handle->currUserName, TERM_DEVICE_NAME);
                break;
        }
        
        if(c == 0x03){
            (*handle->print)("^C");
        }
        return 1;
    }
    
    switch(c){
        case '\r':      //enter
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            if(handle->currBufferLength != 0){
                (*handle->print)("\r\n", handle->inputBuffer);

                if(handle->historyBuffer[handle->currHistoryWritePosition] != 0){
                    vPortFree(handle->historyBuffer[handle->currHistoryWritePosition]);
                    handle->historyBuffer[handle->currHistoryWritePosition] = 0;
                }

                handle->historyBuffer[handle->currHistoryWritePosition] = pvPortMalloc(handle->currBufferLength + 1);
                memcpy(handle->historyBuffer[handle->currHistoryWritePosition], handle->inputBuffer, handle->currBufferLength + 1);
                
                if(++handle->currHistoryWritePosition >= TERM_HISTORYSIZE) handle->currHistoryWritePosition = 0;
                
                handle->currHistoryReadPosition = handle->currHistoryWritePosition;

                uint8_t retCode = TERM_interpretCMD(handle->inputBuffer, handle->currBufferLength, handle);
                switch(retCode){
                    case TERM_CMD_EXIT_SUCCESS:
                        (*handle->print)("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
                        break;

                    case TERM_CMD_EXIT_ERROR:
                        (*handle->print)("\r\nTask returned with error code %d\r\n%s@%s>", retCode, handle->currUserName, TERM_DEVICE_NAME);
                        break;

                    case TERM_CMD_EXIT_NOT_FOUND:
                        (*handle->print)("\"%s\" is not a valid command. Type \"help\" to see a list of available ones\r\n%s@%s>", handle->inputBuffer, handle->currUserName, TERM_DEVICE_NAME);
                        break;

                    case TERM_CMD_EXIT_PROC_STARTED:
                        break;

                    default:
                        (*handle->print)("\r\nTask returned with an unknown return code (%d)\r\n%s@%s>", retCode, handle->currUserName, TERM_DEVICE_NAME);
                        break;
                }
                handle->currBufferPosition = 0;
                handle->currBufferLength = 0;
                handle->inputBuffer[handle->currBufferPosition] = 0;
            }else{
                (*handle->print)("\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
            }       
            break;
            
        case 0x03:      //CTRL+c
            //TODO reset current buffer
            (*handle->print)("^C");
            break;
            
        case 0x08:      //backspace (used by xTerm)
        case 0x7f:      //DEL       (used by hTerm)
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            if(handle->currBufferPosition == 0) break;
            
            if(handle->inputBuffer[handle->currBufferPosition] != 0){      //check if we are at the end of our command
                //we are somewhere in the middle -> move back existing characters
                strsft(handle->inputBuffer, handle->currBufferPosition - 1, -1);    
                (*handle->print)("\x08");   
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE_END, 0);
                (*handle->print)("%s", &handle->inputBuffer[handle->currBufferPosition - 1]);
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
                handle->currBufferPosition --;
                handle->currBufferLength --;
            }else{
                //we are somewhere at the end -> just delete the current one
                handle->inputBuffer[--handle->currBufferPosition] = 0;
                (*handle->print)("\x08 \x08");           
                handle->currBufferLength --;
            }
            break;
            
        case _VT100_KEY_END:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            //TODO move cursor to EOL
            break;
            
        case _VT100_KEY_POS1:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            //TODO move cursor to BOL
            break;
            
        case _VT100_CURSOR_FORWARD:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            if(handle->currBufferPosition < handle->currBufferLength){
                handle->currBufferPosition ++;
                TERM_sendVT100Code(handle, _VT100_CURSOR_FORWARD, 0);
            }
            break;
            
        case _VT100_CURSOR_BACK:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            if(handle->currBufferPosition > 0){
                handle->currBufferPosition --;
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK, 0);
            }
            break;
            
        case _VT100_CURSOR_UP:
            TERM_checkForCopy(handle, TERM_CHECK_COMP);
            
            do{
                if(--handle->currHistoryReadPosition >= TERM_HISTORYSIZE) handle->currHistoryReadPosition = TERM_HISTORYSIZE - 1;
                
                if(handle->historyBuffer[handle->currHistoryReadPosition] != 0){
                    break;
                }
            }while(handle->currHistoryReadPosition != handle->currHistoryWritePosition);
            
            //print out the command at the current history read position
            if(handle->currHistoryReadPosition == handle->currHistoryWritePosition){
                (*handle->print)("\x07");   //rings a bell doesn't it?                                                      
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
            }else{
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->historyBuffer[handle->currHistoryReadPosition]);
            }
            break;
            
        case _VT100_CURSOR_DOWN:
            TERM_checkForCopy(handle, TERM_CHECK_COMP);
            
            while(handle->currHistoryReadPosition != handle->currHistoryWritePosition){
                if(++handle->currHistoryReadPosition >= TERM_HISTORYSIZE) handle->currHistoryReadPosition = 0;
                
                if(handle->historyBuffer[handle->currHistoryReadPosition] != 0){
                    break;
                }
            }
            
            //print out the command at the current history read position
            if(handle->currHistoryReadPosition == handle->currHistoryWritePosition){
                (*handle->print)("\x07");   //rings a bell doesn't it?                                                      
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
            }else{
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->historyBuffer[handle->currHistoryReadPosition]);
            }
            break;
            
        case '\t':      //tab
            TERM_checkForCopy(handle, TERM_CHECK_HIST);
            
            if(handle->autocompleteBuffer == NULL){ 
                handle->autocompleteBuffer = pvPortMalloc(TERM_cmdCount * sizeof(TermCommandDescriptor *));
                handle->autocompleteBufferLength = TERM_findMatchingCMDs(handle->inputBuffer, handle->currBufferLength, handle->autocompleteBuffer);
                handle->currAutocompleteCount = 0;
            }
            
            if(++handle->currAutocompleteCount > handle->autocompleteBufferLength) handle->currAutocompleteCount = 0;
            
            if(handle->currAutocompleteCount == 0){
                (*handle->print)("\x07");
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
            }else{
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]->command);
            }
            break;
            
        case _VT100_BACKWARDS_TAB:
            TERM_checkForCopy(handle, TERM_CHECK_HIST);
            
            if(handle->autocompleteBuffer == NULL){ 
                pvPortMalloc(TERM_cmdCount * sizeof(TermCommandDescriptor *));
                handle->autocompleteBufferLength = TERM_findMatchingCMDs(handle->inputBuffer, handle->currBufferLength, handle->autocompleteBuffer);
                handle->currAutocompleteCount = 0;
            }
            
            if(--handle->currAutocompleteCount > handle->autocompleteBufferLength - 1) handle->currAutocompleteCount = handle->autocompleteBufferLength - 1;
            
            if(handle->currAutocompleteCount == 0){
                (*handle->print)("\x07");
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->inputBuffer);
            }else{
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE, 0);
                (*handle->print)("\r%s@%s>%s", handle->currUserName, TERM_DEVICE_NAME, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]->command);
            }
            break;
            
        case _VT100_RESET:
            TERM_sendVT100Code(handle, _VT100_RESET, 0); TERM_sendVT100Code(handle, _VT100_CURSOR_POS1, 0);
            (*handle->print)("\r\n\n\n%s\r\n", TERM_startupText1);
            (*handle->print)("%s\r\n", TERM_startupText2);
            (*handle->print)("%s\r\n", TERM_startupText3);
            (*handle->print)("\r\n%s%sDISCLAIMER%s: This is only a POC. All commands will call the test handler at the moment\r\n", UART_getVT100Code(_VT100_BACKGROUND_COLOR, _VT100_RED), UART_getVT100Code(_VT100_BLINK, 0), UART_getVT100Code(_VT100_RESET_ATTRIB, 0));
            (*handle->print)("\r\n\r\n%s@%s>", handle->currUserName, TERM_DEVICE_NAME);
            break;
           
        case 32 ... 126:
            TERM_checkForCopy(handle, TERM_CHECK_COMP_AND_HIST);
            
            //TODO check for string length overflow
            
            if(handle->inputBuffer[handle->currBufferPosition] != 0){      //check if we are at the end of our command
                strsft(handle->inputBuffer, handle->currBufferPosition, 1);   
                handle->inputBuffer[handle->currBufferPosition] = c; 
                TERM_sendVT100Code(handle, _VT100_ERASE_LINE_END, 0);
                (*handle->print)("%s", &handle->inputBuffer[handle->currBufferPosition]);
                TERM_sendVT100Code(handle, _VT100_CURSOR_BACK_BY, handle->currBufferLength - handle->currBufferPosition);
                handle->currBufferLength ++;
                handle->currBufferPosition ++;
            }else{
                
                //we are at the end -> just delete the current one
                handle->inputBuffer[handle->currBufferPosition++] = c;
                handle->inputBuffer[handle->currBufferPosition] = 0;
                handle->currBufferLength ++;
                (*handle->print)("%c", c);
            }
            break;
            
        default:
            TERM_printDebug(handle, "unknown code received: 0x%02x\r\n", c);
            break;
    }
}

void TERM_checkForCopy(TERMINAL_HANDLE * handle, COPYCHECK_MODE mode){
    if((mode & TERM_CHECK_COMP) && handle->autocompleteBuffer != NULL){ 
        if(handle->currAutocompleteCount != 0){
            strcpy(handle->inputBuffer, handle->autocompleteBuffer[handle->currAutocompleteCount - 1]->command);
            handle->currBufferLength = strlen(handle->inputBuffer);
            handle->currBufferPosition = handle->currBufferLength;
            handle->inputBuffer[handle->currBufferPosition] = 0;
        }
        TERM_freeCommandList(handle->autocompleteBuffer, handle->autocompleteBufferLength);
        handle->autocompleteBuffer = NULL;
    }
    
    if((mode & TERM_CHECK_HIST) && handle->currHistoryWritePosition != handle->currHistoryReadPosition){
        strcpy(handle->inputBuffer, handle->historyBuffer[handle->currHistoryReadPosition]);
        handle->currBufferLength = strlen(handle->inputBuffer);
        handle->currBufferPosition = handle->currBufferLength;
        handle->currHistoryReadPosition = handle->currHistoryWritePosition;
    }
}

char * strnchr(char * str, char c, uint32_t length){
    uint32_t currPos = 0;
    for(;currPos < length && str[currPos] != 0; currPos++){
        if(str[currPos] == c) return &str[currPos];
    }
    return NULL;
}

void strsft(char * src, int32_t startByte, int32_t offset){
    if(offset == 0) return;
    
    if(offset > 0){     //shift forward
        uint32_t currPos = strlen(src) + offset;
        src[currPos--] = 0;
        for(; currPos >= startByte; currPos--){
            if(currPos == 0){
                src[currPos] = ' ';
                break;
            }
            src[currPos] = src[currPos - offset];
        }
        return;
    }else{              //shift backward
        uint32_t currPos = startByte;
        for(; src[currPos - offset] != 0; currPos++){
            src[currPos] = src[currPos - offset];
        }
        src[currPos] = src[currPos - offset];
        return;
    }
}

uint8_t TERM_interpretCMD(char * data, uint16_t dataLength, TERMINAL_HANDLE * handle){
    uint8_t currPos = 0;
    uint16_t cmdLength = dataLength;
    
    char * firstSpace = strchr(data, ' ');
    if(firstSpace != 0){
        cmdLength = (uint16_t) ((uint32_t) firstSpace - (uint32_t) data);
    }
    
    for(;currPos < TERM_cmdCount; currPos++){
        if(TERM_cmdList[currPos]->commandLength == cmdLength && strncmp(data, TERM_cmdList[currPos]->command, cmdLength) == 0){
            uint16_t argCount = TERM_countArgs(data, dataLength);
            if(argCount == TERM_ARGS_ERROR_STRING_LITERAL){
                (*handle->print)("\r\nError: unclosed string literal in command\r\n");
                return TERM_CMD_EXIT_ERROR;
            }
            
            char ** args = 0;
            if(argCount != 0){
                args = pvPortMalloc(sizeof(char*) * argCount);
                TERM_seperateArgs(data, dataLength, args);
            }
            
            uint8_t retCode = TERM_CMD_EXIT_ERROR;
            if(TERM_cmdList[currPos]->function != 0){
                retCode = (*TERM_cmdList[currPos]->function)(handle, argCount, args);
            }
            
            if(argCount != 0) vPortFree(args);
            return retCode;
        }
    }
    return TERM_CMD_EXIT_NOT_FOUND;
}

uint8_t TERM_seperateArgs(char * data, uint16_t dataLength, char ** buff){
    uint8_t count = 0;
    uint8_t currPos = 0;
    unsigned quoteMark = 0;
    char * currStringStart = 0;
    char * lastSpace = 0;
    for(;currPos<dataLength; currPos++){
        switch(data[currPos]){
            case ' ':
                if(!quoteMark){
                    data[currPos] = 0;
                    lastSpace = &data[currPos + 1];
                }
                break;
                
            case '"':
                if(quoteMark){
                    quoteMark = 0;
                    if(currStringStart){
                        data[currPos] = 0;
                        buff[count++] = currStringStart;
                    }
                }else{
                    quoteMark = 1;
                    currStringStart = &data[currPos+1];
                }
                        
                break;
            default:
                if(!quoteMark){
                    if(lastSpace != 0){
                        buff[count++] = lastSpace;
                        lastSpace = 0;
                    }
                }
                break;
        }
    }
    if(quoteMark) TERM_ARGS_ERROR_STRING_LITERAL;
    return count;
}

uint16_t TERM_countArgs(const char * data, uint16_t dataLength){
    uint8_t count = 0;
    uint8_t currPos = 0;
    unsigned quoteMark = 0;
    char * currStringStart = 0;
    char * lastSpace = 0;
    for(;currPos<dataLength; currPos++){
        switch(data[currPos]){
            case ' ':
                if(!quoteMark){
                    lastSpace = &data[currPos + 1];
                }
                break;
                
            case '"':
                if(quoteMark){
                    quoteMark = 0;
                    if(currStringStart){
                        count ++;
                    }
                }else{
                    quoteMark = 1;
                    currStringStart = &data[currPos+1];
                }
                        
                break;
            default:
                if(!quoteMark){
                    if(lastSpace){
                        count ++;
                        lastSpace = 0;
                    }
                }
                break;
        }
    }
    if(quoteMark) TERM_ARGS_ERROR_STRING_LITERAL;
    return count;
}

uint8_t TERM_findMatchingCMDs(char * currInput, uint8_t length, TermCommandDescriptor ** buff){
    
    //TODO handle auto complete of parameters, for now we return if this is attempted
    if(strnchr(currInput, ' ', length) != NULL) return 0;
    //UART_print("scanning \"%s\" for matching cmds\r\n", currInput);
    
    uint8_t currPos = 0;
    uint8_t commandsFound = 0;
    for(;currPos < TERM_cmdCount; currPos++){
        if(strncmp(currInput, TERM_cmdList[currPos]->command, length) == 0){
            if(TERM_cmdList[currPos]->commandLength >= length){
                buff[commandsFound] = TERM_cmdList[currPos];
                commandsFound ++;
                //UART_print("found %s (count is now %d)\r\n", TERM_cmdList[currPos]->command, commandsFound);
            }
        }else{
            if(commandsFound > 0) return commandsFound;
        }
    }
    return commandsFound;
}

void TERM_freeCommandList(TermCommandDescriptor ** cl, uint16_t length){
    /*uint8_t currPos = 0;
    for(;currPos < length; currPos++){
        vPortFree(cl[currPos]);
    }*/
    vPortFree(cl);
}

uint8_t TERM_buildCMDList(){
    uint16_t currPos = 1;
    uint32_t startTime = xTaskGetTickCount();
    while(currPos < TERM_cmdCount){
        TermCommandDescriptor * currCMD = TERM_cmdList[currPos];
        TermCommandDescriptor * lastCMD = TERM_cmdList[currPos - 1];
        
        if(!TERM_isSorted(currCMD, lastCMD)){
            TERM_cmdList[currPos] = lastCMD;
            TERM_cmdList[currPos - 1] = currCMD;
            currPos = 1;
        }else{
            currPos ++;
        }
    }
    //UART_print("Sorted the command list in %d ms\r\n", xTaskGetTickCount() - startTime);
}

uint8_t TERM_addCommand(TermCommandFunction function, const char * command, const char * description, uint8_t minPermissionLevel){
    if(TERM_cmdCount == 0xff) return 0;
    
    TermCommandDescriptor * newCMD = pvPortMalloc(sizeof(TermCommandDescriptor));
    TermCommandDescriptor ** newCMDList = pvPortMalloc((TERM_cmdCount + 1) * sizeof(TermCommandDescriptor *));
    
    newCMD->command = command;
    newCMD->commandDescription = description;
    //UART_print("added %s", command);
    newCMD->commandLength = strlen(command);
    newCMD->function = function;
    newCMD->minPermissionLevel = minPermissionLevel;
    
    if(TERM_cmdCount > 0){
        memcpy(newCMDList, TERM_cmdList, TERM_cmdCount * sizeof(TermCommandDescriptor *));
        vPortFree(TERM_cmdList);
    }
    
    TERM_cmdList = newCMDList;
    TERM_cmdList[TERM_cmdCount++] = newCMD;
    TERM_buildCMDList();
    return TERM_cmdCount;
}

unsigned TERM_isSorted(TermCommandDescriptor * a, TermCommandDescriptor * b){
    uint8_t currPos = 0;
    //compare the lowercase ASCII values of each character in the command (They are alphabetically sorted)
    for(;currPos < a->commandLength && currPos < b->commandLength; currPos++){
        char letterA = toLowerCase(a->command[currPos]);
        char letterB = toLowerCase(b->command[currPos]);
        //if the letters are different we return 1 if a is smaller than b (they are correctly sorted) or zero if its the other way around
        if(letterA > letterB){
            return 1;
        }else if(letterB > letterA){
            return 0;
        }
    }
    
    //the two commands have identical letters for their entire length we check which one is longer (the shortest should come first)
    if(a->commandLength > b->commandLength){
        return 1;
    }else if(b->commandLength > a->commandLength){
        return 0;
    }else{
        //it might happen that a command is added twice (or two identical ones are added), in which case we just say they are sorted correctly and print an error in the console
        //TODO implement an alarm here
        UART_print("WARNING: Found identical commands: \"%S\" and \"%S\"\r\n", a->command, b->command);        
        return 1;
    }
}

char toLowerCase(char c){
    if(c > 65 && c < 90){
        return c + 32;
    }
    
    switch(c){
        case '�':
            return '�';
        case '�':
            return '�';
        case '�':
            return '�';
        default:
            return c;
    }
}

void TERM_setCursorPos(TERMINAL_HANDLE * handle, uint16_t x, uint16_t y){
    
}

void TERM_sendVT100Code(TERMINAL_HANDLE * handle, uint16_t cmd, uint8_t var){
    switch(cmd){
        case _VT100_RESET:
            (*handle->print)("%cc", 0x1b);
            break;
        case _VT100_CURSOR_BACK:
            (*handle->print)("\x1b[D");
            break;
        case _VT100_CURSOR_FORWARD:
            (*handle->print)("\x1b[C");
            break;
        case _VT100_CURSOR_POS1:
            (*handle->print)("\x1b[H");
            break;
        case _VT100_CURSOR_END:
            (*handle->print)("\x1b[F");
            break;
        case _VT100_FOREGROUND_COLOR:
            (*handle->print)("\x1b[%dm", var+30);
            break;
        case _VT100_BACKGROUND_COLOR:
            (*handle->print)("\x1b[%dm", var+40);
            break;
        case _VT100_RESET_ATTRIB:
            (*handle->print)("\x1b[0m");
            break;
        case _VT100_BRIGHT:
            (*handle->print)("\x1b[1m");
            break;
        case _VT100_DIM:
            (*handle->print)("\x1b[2m");
            break;
        case _VT100_UNDERSCORE:
            (*handle->print)("\x1b[4m");
            break;
        case _VT100_BLINK:
            (*handle->print)("\x1b[5m");
            break;
        case _VT100_REVERSE:
            (*handle->print)("\x1b[7m");
            break;
        case _VT100_HIDDEN:
            (*handle->print)("\x1b[8m");
            break;
        case _VT100_ERASE_SCREEN:
            (*handle->print)("\x1b[2J");
            break;
        case _VT100_ERASE_LINE:
            (*handle->print)("\x1b[2K");
            break;
        case _VT100_FONT_G0:
            (*handle->print)("\x1b(");
            break;
        case _VT100_FONT_G1:
            (*handle->print)("\x1b)");
            break;
        case _VT100_WRAP_ON:
            (*handle->print)("\x1b[7h");
            break;
        case _VT100_WRAP_OFF:
            (*handle->print)("\x1b[7l");
            break;
        case _VT100_ERASE_LINE_END:
            (*handle->print)("\x1b[K");
            break;
        case _VT100_CURSOR_BACK_BY:
            (*handle->print)("\x1b[%dD", var);
            break;
        case _VT100_CURSOR_FORWARD_BY:
            (*handle->print)("\x1b[%dC", var);
            break;
        case _VT100_CURSOR_SAVE_POSITION:
            (*handle->print)("\x1b7");
            break;
        case _VT100_CURSOR_RESTORE_POSITION:
            (*handle->print)("\x1b8");
            break;
            
    }
}

void TERM_attachProgramm(TERMINAL_HANDLE * handle, TermProgram * prog){
    handle->currProgram = prog;
}

void TERM_removeProgramm(TERMINAL_HANDLE * handle){
    handle->currProgram = 0;
}


#ifdef TERM_ENABLE_STARTUP_TEXT
const char TERM_startupText1[] = "\r\n   _/    _/  _/_/_/    _/_/_/        _/_/_/_/  _/  _/                            _/      _/              _/      \r\n  _/    _/  _/    _/        _/      _/            _/_/_/      _/_/    _/  _/_/  _/_/    _/    _/_/    _/_/_/_/   ";
const char TERM_startupText2[] = " _/    _/  _/    _/    _/_/        _/_/_/    _/  _/    _/  _/_/_/_/  _/_/      _/  _/  _/  _/_/_/_/    _/        \r\n_/    _/  _/    _/        _/      _/        _/  _/    _/  _/        _/        _/    _/_/  _/          _/         ";
const char TERM_startupText3[] = " _/_/    _/_/_/    _/_/_/        _/        _/  _/_/_/      _/_/_/  _/        _/      _/    _/_/_/      _/_/   ";
#endif   