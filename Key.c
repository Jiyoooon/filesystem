#include "2440addr.h"
#include "macro.h"

#define KEY_PRESS_RELEASE_CHECK_COUNT 1000

// OUT1~0 = GPC9~8 => GBOX
#define KEYOUT_ALL_LO() 	{Macro_Clear_Area(rGPCDAT, 0x3, 8);}
#define KEYOUT_ALL_HI() 	{Macro_Set_Area(rGPCDAT, 0x3, 8);}
#define KEYOUT0_LO() 		{KEYOUT_ALL_HI(); Macro_Clear_Bit(rGPCDAT, 8);}
#define KEYOUT1_LO() 		{KEYOUT_ALL_HI(); Macro_Clear_Bit(rGPCDAT, 9);}

void Key_Poll_Init(void)
{
	// KEYIN => GPF4,5,6,7 (Input)
	Macro_Clear_Area(rGPFCON, 0xff, 8);
	Macro_Clear_Area(rGPFUP, 0xf, 4);	

	// KEYOUT => GPC9,8 (Output)
	Macro_Write_Block(rGPCCON, 0xf, 0x5, 16);	
	KEYOUT_ALL_HI();
}

extern void Key_IRQ_Init(void)
{
	// KEYIN => GPF4,5,6,7 (EINT4-7)
	Macro_Write_Block(rGPFCON, 0xff, 0xaa, 8);
	Macro_Clear_Area(rGPFUP, 0xf, 4);	

	// KEYOUT => GPC9,8 (Output)
	Macro_Write_Block(rGPCCON, 0xf, 0x5, 16);	
	KEYOUT_ALL_LO();

	// Set trigger level of EINT3
	// 0: Low, 1: High, 2: Falling, 4: Rising, 6: Both edge
	Macro_Write_Block(rEXTINT0, 0xffff, 0x2222, 16);
}

void Key_ISR_Enable(int en)
{
	if(en)
	{
		rEINTPEND = (0xf<<4);
		rSRCPND = EINT4_7;
		rINTPND = EINT4_7;
		Macro_Clear_Area(rEINTMASK, 0xf, 4);
		Macro_Clear_Bit(rINTMSK, EINT4_7);
	}
	else
	{
		Macro_Set_Bit(rINTMSK, EINT4_7);	
	}
}

int Key_Get_Pressed(void)	
{
	static int key_code[8] = {1,2,3,4,5,6,7,8};
	int i, j, key, old_key = 0;
	int count = 0;

	for(j = 0 ; j < 3000 ; j++)
	{
		key = 0;
		for(i = 1 ; i >= 0 ; i--)
		{
			switch(i)
			{
				case 1: KEYOUT1_LO(); break;								
				case 0: KEYOUT0_LO(); break;								
			}

			key = 0;
	
			switch(~rGPFDAT & 0xf0)
			{
				case 0x10 : key = key_code[i*4 + 0]; goto EXIT;
				case 0x20 : key = key_code[i*4 + 1]; goto EXIT;
				case 0x40 : key = key_code[i*4 + 2]; goto EXIT;
				case 0x80 : key = key_code[i*4 + 3]; goto EXIT;
			}
		}
		
EXIT:
		if(old_key == key)
		{
			count++;
		}
		else
		{
			old_key = key;
			count = 0;
		}
		
		if(count >= KEY_PRESS_RELEASE_CHECK_COUNT) return key;
	}
	return 0;
}

void Key_Wait_Key_Released(void)
{
	while(Key_Get_Pressed());
}

int Key_Wait_Key_Pressed(void)
{
	int k;
	
	do
	{
		k = Key_Get_Pressed();
	}
	while(!k);

	return k;
}
