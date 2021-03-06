#define _VT100_CURSOR_POS1 3
#define _VT100_CURSOR_END 4
#define _VT100_FOREGROUND_COLOR 5
#define _VT100_BACKGROUND_COLOR 6
#define _VT100_RESET_ATTRIB 7
#define _VT100_BRIGHT 8
#define _VT100_DIM 9
#define _VT100_UNDERSCORE 10
#define _VT100_BLINK 11
#define _VT100_REVERSE 12
#define _VT100_HIDDEN 13
#define _VT100_ERASE_SCREEN 14
#define _VT100_ERASE_LINE 15
#define _VT100_FONT_G0 16
#define _VT100_FONT_G1 17
#define _VT100_WRAP_ON 18
#define _VT100_WRAP_OFF 19
#define _VT100_ERASE_LINE_END 20
#define _VT100_CURSOR_BACK_BY 21
#define _VT100_CURSOR_FORWARD_BY 22
#define _VT100_CURSOR_SAVE_POSITION 23
#define _VT100_CURSOR_RESTORE_POSITION 24

//VT100 cmds given to us by the terminal software (they need to be > 8 bits so the handler can tell them apart from normal characters)
#define _VT100_RESET                0x1000
#define _VT100_KEY_END              0x1001
#define _VT100_KEY_POS1             0x1002
#define _VT100_CURSOR_FORWARD       0x1003
#define _VT100_CURSOR_BACK          0x1004
#define _VT100_CURSOR_UP            0x1005
#define _VT100_CURSOR_DOWN          0x1006
#define _VT100_BACKWARDS_TAB        0x1007

#define _VT100_BLACK 0
#define _VT100_RED 1
#define _VT100_GREEN 2
#define _VT100_YELLOW 3
#define _VT100_BLUE 4
#define _VT100_MAGENTA 5
#define _VT100_CYAN 6
#define _VT100_WHITE 7

#define _VT100_POS_IGNORE 0xffff

#define TERM_DEVICE_NAME "UD3 Fibernet"
#define TERM_VERSION_STRING "V1.0"

#define TERM_HISTORYSIZE 16
#define TERM_INPUTBUFFER_SIZE 128

                        
#define TERM_ARGS_ERROR_STRING_LITERAL 0xffff

#define TERM_CMD_EXIT_ERROR 0
#define TERM_CMD_EXIT_NOT_FOUND 1
#define TERM_CMD_EXIT_SUCCESS 0xff
#define TERM_CMD_EXIT_PROC_STARTED 0xfe
#define TERM_CMD_CONTINUE 0x80

#define TERM_ENABLE_STARTUP_TEXT

#ifdef TERM_ENABLE_STARTUP_TEXT
const extern char TERM_startupText1[];
const extern char TERM_startupText2[];
const extern char TERM_startupText3[];
#endif

typedef uint8_t (* TermCommandFunction)(struct TERMINAL_HANDLE * handle, uint8_t argCount, char ** args);
typedef uint8_t (* TermCommandInputHandler)(struct TERMINAL_HANDLE * handle, uint16_t c);
typedef void (* TermPrintHandler)(char * format, ...);

typedef struct{
    TaskHandle_t task;
    TermCommandInputHandler inputHandler;
} TermProgram;

typedef struct{
    TermCommandFunction function;
    const char * command;
    const char * commandDescription;
    uint8_t commandLength;
    uint8_t minPermissionLevel;
} TermCommandDescriptor;

typedef struct{
    char * inputBuffer;
    uint32_t currBufferPosition;
    uint32_t currBufferLength;
    uint32_t currAutocompleteCount;
    TermProgram * currProgram;
    TermCommandDescriptor ** autocompleteBuffer;
    uint32_t autocompleteBufferLength;
    TermPrintHandler print;
    char * currUserName;
    char * historyBuffer[TERM_HISTORYSIZE];
    uint32_t currHistoryWritePosition;
    uint32_t currHistoryReadPosition;
    uint8_t currEscSeqPos;
    uint8_t escSeqBuff[16];
} TERMINAL_HANDLE;

typedef enum{
    TERM_CHECK_COMP_AND_HIST = 0b11, TERM_CHECK_COMP = 0b01, TERM_CHECK_HIST = 0b10, 
} COPYCHECK_MODE;

extern TermCommandDescriptor ** TERM_cmdList;
extern uint8_t TERM_cmdCount;

TERMINAL_HANDLE * TERM_createNewHandle(TermPrintHandler printFunction, const char * usr);
void TERM_destroyHandle(TERMINAL_HANDLE * handle);
uint8_t TERM_processBuffer(uint8_t * data, uint16_t length, TERMINAL_HANDLE * handle);
unsigned isACIILetter(char c);
uint8_t TERM_handleInput(uint16_t c, TERMINAL_HANDLE * handle);
char * strnchr(char * str, char c, uint32_t length);
void strsft(char * src, int32_t startByte, int32_t offset);
uint8_t TERM_findMatchingCMDs(char * currInput, uint8_t length, TermCommandDescriptor ** buff);
void TERM_freeCommandList(TermCommandDescriptor ** cl, uint16_t length);
uint8_t TERM_buildCMDList();
uint8_t TERM_addCommand(TermCommandFunction function, const char * command, const char * description, uint8_t minPermissionLevel);
unsigned TERM_isSorted(TermCommandDescriptor * a, TermCommandDescriptor * b);
char toLowerCase(char c);
void TERM_setCursorPos(TERMINAL_HANDLE * handle, uint16_t x, uint16_t y);
void TERM_sendVT100Code(TERMINAL_HANDLE * handle, uint16_t cmd, uint8_t var);
uint16_t TERM_countArgs(const char * data, uint16_t dataLength);
uint8_t TERM_interpretCMD(char * data, uint16_t dataLength, TERMINAL_HANDLE * handle);
uint8_t TERM_seperateArgs(char * data, uint16_t dataLength, char ** buff);
void TERM_checkForCopy(TERMINAL_HANDLE * handle, COPYCHECK_MODE mode);
void TERM_printDebug(TERMINAL_HANDLE * handle, char * format, ...);
void TERM_removeProgramm(TERMINAL_HANDLE * handle);
void TERM_attachProgramm(TERMINAL_HANDLE * handle, TermProgram * prog);