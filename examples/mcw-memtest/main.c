// Requires SGDK 1.70 or compatible


#include "genesis.h"

#define MCW_PORT 0x1FFFFE;
#define CMD_API_VERSION 0
#define CMD_MEM_READ 1
#define CMD_MEM_WRITE 2
#define CMD_PRINT 3
#define MWC_MAGIC 0x5E


static u16 posY, posX;

static char *hex = "0123456789ABCDEF";

static u8  cartApiVersion = 0;

static u16 testAddr = 0;
static u16 errorCnt = 0;
static u16 testCnt = 0;
static u8 errorsFound = 0;

static void memTest(void);
static void cartInit(void);
static u8 cartReadApiVersion(void);


int main(bool param)
{
    u8 value;


    VDP_setScreenWidth320();
    VDP_setHInterrupt(0);
    VDP_setHilightShadow(0);
    VDP_setPaletteColor(15+16, 0x0222);
    VDP_setTextPalette(0);
    VDP_setPaletteColor(0, 0x0882);

    value = JOY_getPortType(PORT_1);
    switch (value)
    {
        case PORT_TYPE_MENACER:
            JOY_setSupport(PORT_1, JOY_SUPPORT_MENACER);
            break;
        case PORT_TYPE_JUSTIFIER:
            JOY_setSupport(PORT_1, JOY_SUPPORT_JUSTIFIER_BOTH);
            break;
        case PORT_TYPE_MOUSE:
            JOY_setSupport(PORT_1, JOY_SUPPORT_MOUSE);
            break;
        case PORT_TYPE_TEAMPLAYER:
            JOY_setSupport(PORT_1, JOY_SUPPORT_TEAMPLAYER);
            break;
    }


#if 1
    JOY_setSupport(PORT_2, JOY_SUPPORT_PHASER);
#endif

	cartInit();
	testAddr = 0;
	errorCnt = 0;
	testCnt = 0;
	
	cartApiVersion = cartReadApiVersion();

    while(1)
    {
        SYS_doVBlankProcess();
        memTest();
    }
}


static void printChar(char c, u16 state)
{
    char temp[2];
    temp[0] = c;
    temp[1] = 0;
    VDP_setTextPalette(state ? 1 : 0);
    VDP_drawText(temp, posX, posY);
    posX ++;
}

static void printWord(u16 val, u16 state)
{
    char temp[8];
    temp[0] = '0';
    temp[1] = 'x';
    temp[2] = hex[val >> 12];
    temp[3] = hex[(val >> 8) & 15];
    temp[4] = hex[(val >> 4) & 15];
    temp[5] = hex[val & 15];
    temp[6] = 0;
    VDP_setTextPalette(state ? 1 : 0);
    VDP_drawText(temp, posX, posY);
    posX += 8;
}

//These pragmas are important to meet read/write timigs with the cart.
#pragma GCC push_options
#pragma GCC optimize ("O1")

static void cartSendString(char* t)
{
	u8 c;
	volatile u16* cart = (u16*) MCW_PORT;
	// send magic
	*cart = MWC_MAGIC;
	__asm("nop");
	__asm("nop");
	// set command: Print
	*cart = CMD_PRINT;
	
	// send the string
	do {
		c = *t;
		t++;
		*cart = c;
	} while (c);
}

// Required only during soft reset if there was a transaction in progress.
static void cartInit(void) {
	volatile u16* cart = (u16*) MCW_PORT;
	u16 i = 0;
	u16 j;
	
	// send a non-sensical byte sequence so that the cart resets its internal variables
	while (i <= 0xFF) {
		*cart = 0xFF;
		i++;
		__asm("nop");
		__asm("nop");
		
		j = *cart;
		i++;
		__asm("nop");
		__asm("nop");
	}
}

static void cartWriteSlowMem(u8* data, u16 addr, u16 len)
{
	volatile u16* cart = (u16*) MCW_PORT;
	// send magic
	*cart = MWC_MAGIC;
	__asm("nop");
	__asm("nop");
	// set command: memory write
	*cart = CMD_MEM_WRITE;
	__asm("nop");
	
	//len - Little endian
	*cart = len & 0xFF;
	*cart = len >> 8;
	
	//address - little endian
	*cart = addr & 0xFF;
	*cart = addr >> 8;
	
	//data
	while (len--) {
		*cart = *data;
		data++;
	}
}

static void cartReadSlowMem(u8* data, u16 addr, u16 len)
{
	volatile u16* cart = (u16*) MCW_PORT;
	// send magic
	*cart = MWC_MAGIC;
	__asm("nop");
	__asm("nop");
	
	// set command: memory write
	*cart = CMD_MEM_READ;
	__asm("nop");

	//len - Little endian
	*cart = len & 0xFF;
	*cart = len >> 8;
	
	//address - Little endian
	*cart = addr & 0xFF;
	*cart = addr >> 8;


	//data
	while (len--) {
		*data = *cart;
		data++;
	}
}

static u8 cartReadApiVersion(void) {
	volatile u16* cart = (u16*) MCW_PORT;
	// send magic
	*cart = MWC_MAGIC;
	__asm("nop");
	__asm("nop");
	
	// set command: memory write
	*cart = CMD_API_VERSION;
	__asm("nop");
	__asm("nop");
	
	return 	*cart;
}
#pragma GCC pop_options 


static void runTest(u16 len)
{
	u8 wbuffer[16];
	u8 rbuffer[16];
	u8 b1 = (testAddr >> 8) + (testCnt & 0xF) + 1;
	u8 b2 = 0xFF - b1;
	u16 i,j;
	u16 addr = testAddr;
	
	//set-up the 16byte buffer
	i = 0;
	while (i < 16) { 
		wbuffer[i++] = b1;
		wbuffer[i++] = b2;
	}
	
	
	i = addr + len;
	while (addr < i) {
	
		//write the buffer to slow ram
		cartWriteSlowMem(wbuffer, addr, 16);
		
		//erase the read buffer
		j = 0;
		while(j < 16) rbuffer[j++] = 0;
		
		//read the buffer from slow ram
		cartReadSlowMem(rbuffer, addr, 16);
		
		//compare the results
		j = 0;
		while(j < 16) {
			if (rbuffer[j] != b1) {
				errorCnt++;
				errorsFound = 1;
			}
			j++;
			if (rbuffer[j] != b2) {
				errorCnt++;
				errorsFound = 1;
			}
			j++;
		}	
		addr +=	16;
	}
}

static void memTest() {
	VDP_setTextPalette(0);
	VDP_drawText("MCWiFi - mem test 1.0", 2, 2);
	
	if (cartApiVersion == 0 || cartApiVersion == 0xFF) {
		VDP_drawText("invalid API version - cart not found", 2, 4);
		return;
	}
	VDP_drawText("   cart API:", 2, 4);

	posX = 16;
	posY = 4;
	printChar('0' + (cartApiVersion >> 4), 0); //major
	printChar('.', 0);
	printChar('0' + (cartApiVersion & 0xF), 0); //minor
	
	
	VDP_drawText(" iterations:", 2, 5);
	VDP_drawText("    address:", 2, 6);
	VDP_drawText("     errors:", 2, 7);
	
	posX = 16;
	posY = 5;
	printWord(testCnt, 0);
	
	posX -= 8;
	posY++;
	printWord(testAddr, 0);
	
	posX -= 8;
	posY++;
	printWord(errorCnt, errorsFound);
	
	runTest(1024);
	testAddr += 1024;
	if (testAddr == 0) {
		testCnt++;
	}
}



