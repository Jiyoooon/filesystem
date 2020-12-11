#include "2440addr.h"
#include "macro.h"

void WDT_Init(void);
void WDT_Start(int start);

void WDT_Init(void)
{
	rWTDAT = 50000;
	rWTCNT = 50000;

	// interrupt Disable, WDT disable, no reset mode
	// prescler[15:8] = 0, MUX[4:3] = 16
	// WDT 주파수 설정 PCLK/16 = 49.3916MHz/16
	
	rWTCON = 0;
}

void WDT_Start(int start)
{
	(start)? (Macro_Set_Bit(rWTCON ,5)) : (Macro_Clear_Bit(rWTCON ,5));
}
