// Board Selection

#define GBOX	0
#define GBOX_II	1
#define BOARD	GBOX_II

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "option.h"
#include "2440addr.h"
#include "macro.h"
#include "device_Driver.h"

#define INICLK			300000
#define SDCLK			25000000
#define SECTOR_SIZE 	512

volatile unsigned int rd_cnt;
volatile unsigned int wr_cnt;
volatile int RCA;

int Wide=0; // 0:1bit, 1:4bit
int MMC=0;  // 0:SD  , 1:MMC

/* 하부 Primitive 함수 */

void CMD0(void);
int CMD55(void);
void Set_4bit_bus(void);
void SetBus(void);
void Card_sel_desel(char sel_desel);
int Chk_CMDend(int cmd, int be_resp);
int Chk_DATend(void);
int Chk_SD_OCR(void);

/* Device Driver 함수 */

int SD_Check_Card(void)
{
	// Port Initial
	Macro_Clear_Area(rGPEUP, 0x1f, 6);
	Macro_Write_Block(rGPECON, 0xfff, 0xaaa, 10);	

	// SD Card Detect Pin Config. GPG[0]
	Macro_Clear_Area(rGPGCON, 0x3, 0);

#if BOARD == GBOX_II
	if(Macro_Check_Bit_Set(rGPGDAT, 0)) return SD_SUCCESS;
#else
	if(Macro_Check_Bit_Clear(rGPGDAT, 0)) return SD_SUCCESS;
#endif
	else return SD_NO_CARD;
}

int SD_Init(void)
{
    int i;

    rSDIPRE = PCLK/(INICLK);	// 400KHz

	rSDICON=(0<<4)|1;		// Type A, clk enable
	rSDIFSTA |= (1<<16);	//YH 040223 FIFO reset
    rSDIBSIZE=0x200;		// 512byte(128word)
    //rSDIDTIMER=0x10000;		// Set timeout count // frog (set to default)
    rSDIDTIMER=0x7fffff;	// Set timeout count
    for(i=0;i<0x1000;i++);  // Wait 74SDCLK for MMC card
    
    CMD0();

    if(Chk_SD_OCR() != SD_SUCCESS) return SD_INVALID_CARD;

RECMD2:
    //-- Check attaced cards, it makes card identification state
    rSDICARG=0x0;   // CMD2(stuff bit)
    rSDICCON=(0x1<<10)|(0x1<<9)|(0x1<<8)|0x42; //lng_resp, wait_resp, start, CMD2

    //-- Check end of CMD2
    if(Chk_CMDend(2, 1) != SD_SUCCESS)
	goto RECMD2;
    rSDICSTA=0xa00;	// Clear cmd_end(with rsp)

RECMD3:
    //--Send RCA
    rSDICARG=MMC<<16;	    // CMD3(MMC:Set RCA, SD:Ask RCA-->SBZ)
    rSDICCON=(0x1<<9)|(0x1<<8)|0x43;	// sht_resp, wait_resp, start, CMD3

    //-- Check end of CMD3
    if(Chk_CMDend(3, 1) != SD_SUCCESS)
	goto RECMD3;
    rSDICSTA=0xa00;	// Clear cmd_end(with rsp)

    //--Publish RCA
	RCA=( rSDIRSP0 & 0xffff0000 )>>16;

	rSDIPRE=PCLK/(SDCLK);	// Normal clock=25MHz

    //--State(stand-by) check
    if( (rSDIRSP0 & 0x1e00) != 0x600 ) goto RECMD3; // CURRENT_STATE check

    Card_sel_desel(1);	// Select
	Set_4bit_bus();
    return SD_SUCCESS;
}

//////////////////////////////////////////////
// 섹터 데이터 읽기 함수 		 	
//	- SecAddr => 읽기시작할 섹터번호
//	- blocks  => 읽을 섹터의 수 (섹터당 512B)
//	- * buf   => 데이터를 담을 버퍼	
//////////////////////////////////////////////

int SD_Read_Sector( U32 SecAddr, U32 blocks , U8 * buf)
{
	int ret;
	int status;
	U32 * Rx_buffer;

	Rx_buffer = (unsigned int *)buf;
	rd_cnt=0;

	rSDIDCON=(2<<22)|(1<<19)|(1<<17)|(Wide<<16)|(1<<14)|(2<<12)|(blocks<<0);
	//Word Rx, Rx after cmd, blk, 4bit bus, Rx start, blk num, data start, data transmit mode

	rSDICARG = SecAddr * SECTOR_SIZE; // CMD17/18(addr) // 읽어오기를 원하는 섹터 주소

RERDCMD:

  	if(blocks<2)	// SINGLE_READ
	{
		rSDICCON=(0x1<<9)|(0x1<<8)|0x51;    	// sht_resp, wait_resp, dat, start, CMD17
	  	if(Chk_CMDend(17, 1) != SD_SUCCESS) goto RERDCMD;	//-- Check end of CMD17
	}
	else	// MULTI_READ
	{
		rSDICCON=(0x1<<9)|(0x1<<8)|0x52;    	// sht_resp, wait_resp, dat, start, CMD18
	  	if(Chk_CMDend(18, 1) != SD_SUCCESS) goto RERDCMD;	//-- Check end of CMD18
	}

	rSDICSTA=0xa00;	// Clear cmd_end(with rsp)

	while(rd_cnt<128*blocks)	// 512*block bytes
	{
		if((rSDIDSTA&0x20)==0x20) // Check timeout
		{
			rSDIDSTA=(0x1<<0x5);  // Clear timeout flag
			return SD_TIME_OUT;
		}

		status=rSDIFSTA;

		if((status&0x1000)==0x1000)	// Is Rx data?
		{
			*Rx_buffer++=rSDIDAT;
			rd_cnt++;
		}
	}

	//-- Check end of DATA
	if(Chk_DATend() != SD_SUCCESS) ret = SD_NO_DATA_END;
	else ret = SD_SUCCESS;

	rSDIDCON=rSDIDCON&~(7<<12);	
	rSDIFSTA=rSDIFSTA&0x200;	
  	rSDIDSTA=0x10;	

	if(blocks > 1)
	{
RERCMD12:
		rSDICARG=0x0;	    //CMD12(stuff bit)
		rSDICCON=(0x1<<9)|(0x1<<8)|0x4c;//sht_resp, wait_resp, start, CMD12

		if(Chk_CMDend(12, 1) != SD_SUCCESS) goto RERCMD12;
		rSDICSTA=0xa00;	// Clear cmd_end(with rsp)
	}

	//if(blocks>20) blocks = 20;
	//fs_PrintSec(SecAddr, (U32 *)buf, blocks);
	return ret;
}

int SD_Write_Sector( U32 SecAddr, U32 blocks , U8 * buf)
{
	int ret;
	int status;
	U32 * Rx_buffer;

	Rx_buffer = (unsigned int *)buf;
	wr_cnt=0;

	rSDIDCON=(2<<22)|(1<<20)|(1<<17)|(Wide<<16)|(1<<14)|(3<<12)|(blocks<<0);
	//Word Rx, Tx after cmd, blk, 4bit bus, Rx start, blk num, data start, data transmit mode

	rSDICARG = SecAddr * SECTOR_SIZE; // CMD17/18(addr) // 읽어오기를 원하는 섹터 주소

RERDCMDW:

  	if(blocks<2)	// SINGLE_WRITE
	{
		rSDICCON=(0x1<<9)|(0x1<<8)|0x58;    	// sht_resp, wait_resp, dat, start, CMD24
	  	if(Chk_CMDend(24, 1) != SD_SUCCESS) goto RERDCMDW;	//-- Check end of CMD24
	}
	else	// MULTI_WRITE
	{
		rSDICCON=(0x1<<9)|(0x1<<8)|0x59;    	// sht_resp, wait_resp, dat, start, CMD25
	  	if(Chk_CMDend(25, 1) != SD_SUCCESS) goto RERDCMDW;	//-- Check end of CMD25
	}

	rSDICSTA=0xa00;	// Clear cmd_end(with rsp)

	while(wr_cnt<128*blocks)	// 512*block bytes
	{
		//if((rSDIDSTA&0x20)==0x20) // Check timeout
		//{
		//	rSDIDSTA=(0x1<<0x5);  // Clear timeout flag
		//	return SD_TIME_OUT;
		//}

		status=rSDIFSTA;

		if((status&0x2000)==0x2000)	// Is Rx data?
		{
			rSDIDAT = *Rx_buffer++;
			wr_cnt++;
		}
	}

	//-- Check end of DATA
	if(Chk_DATend() != SD_SUCCESS) ret = SD_NO_DATA_END;
	else ret = SD_SUCCESS;

	rSDIDCON=rSDIDCON&~(7<<12);
	rSDIFSTA=rSDIFSTA&0x200;
  	rSDIDSTA=0x10;

	if(blocks > 1)
	{
RERCMD12W:
		rSDICARG=0x0;	    //CMD12(stuff bit)
		rSDICCON=(0x1<<9)|(0x1<<8)|0x4c;//sht_resp, wait_resp, start, CMD12

		if(Chk_CMDend(12, 1) != SD_SUCCESS) goto RERCMD12W;
		rSDICSTA=0xa00;	// Clear cmd_end(with rsp)
	}

	//{int i; for(i=0; i<1000000; i++); } // I don't know!!

	return ret;
}


////////////////////// 	기존 소스에 있는 사용 함수들 	/////////////////////// 

void Card_sel_desel(char sel_desel)
{
    //-- Card select or deselect
    if(sel_desel)
    {
RECMDS7:
		rSDICARG=RCA<<16;	// CMD7(RCA,stuff bit)
		rSDICCON= (0x1<<9)|(0x1<<8)|0x47;   // sht_resp, wait_resp, start, CMD7

		//-- Check end of CMD7
		if(Chk_CMDend(7, 1) != SD_SUCCESS)
			goto RECMDS7;
		rSDICSTA=0xa00;	// Clear cmd_end(with rsp)

		//--State(transfer) check
		if((rSDIRSP0 & 0x1e00) == 0x800)
			goto RECMDS7;
    }

    else
    {
RECMDD7:
		rSDICARG=0<<16;		//CMD7(RCA,stuff bit)
		rSDICCON=(0x1<<8)|0x47;	//no_resp, start, CMD7

		//-- Check end of CMD7
		if(Chk_CMDend(7, 0) != SD_SUCCESS)
			goto RECMDD7;
		rSDICSTA=0x800;	// Clear cmd_end(no rsp)
    }
}

void CMD0(void)
{
    //-- Make card idle state
    rSDICARG=0x0;	    	// CMD0(stuff bit)
    rSDICCON=(1<<8)|0x40;   // No_resp, start, CMD0

    //-- Check end of CMD0
    Chk_CMDend(0, 0);
    rSDICSTA=0x800;	    	// Clear cmd_end(no rsp)
}

int CMD55(void)
{
    //--Make ACMD
    rSDICARG=RCA<<16;					//CMD7(RCA,stuff bit)
    rSDICCON=(0x1<<9)|(0x1<<8)|0x77;	//sht_resp, wait_resp, start, CMD55

    //-- Check end of CMD55
    if(Chk_CMDend(55, 1) != SD_SUCCESS) return SD_CMD_ERR;
    rSDICSTA=0xa00;	// Clear cmd_end(with rsp)
	return SD_SUCCESS;
}

void Set_4bit_bus(void)
{
    Wide=1;
    SetBus();
}

void SetBus(void)
{
SET_BUS:
    CMD55();				// Make ACMD
    //-- CMD6 implement
    rSDICARG=Wide<<1;	    //Wide 0: 1bit, 1: 4bit
    rSDICCON=(0x1<<9)|(0x1<<8)|0x46;	//sht_resp, wait_resp, start, CMD55

    if(Chk_CMDend(6, 1) != SD_SUCCESS)   // ACMD6
	goto SET_BUS;
    rSDICSTA=0xa00;	    	// Clear cmd_end(with rsp)
}

int Chk_SD_OCR(void)
{
    int i;
    volatile unsigned int loop;

    //-- Negotiate operating condition for SD, it makes card ready state
    for(i=0;i<50;i++)	//If this time is short, init. can be fail.
    {
    	CMD55();    // Make ACMD

    	rSDICARG=0xff8000;	//ACMD41(SD OCR:2.7V~3.6V)
    	rSDICCON=(0x1<<9)|(0x1<<8)|0x69;//sht_resp, wait_resp, start, ACMD41

		//-- Check end of ACMD41
	   	if((Chk_CMDend(41, 1) == SD_SUCCESS) && (rSDIRSP0==0x80ff8000))
		{
		    rSDICSTA=0xa00;	// Clear cmd_end(with rsp)
		    return SD_SUCCESS;	
		}
		//Wait Card power up status
	  	for(loop=0; loop<0x1000000; loop++);	
    }
    rSDICSTA=0xa00;	// Clear cmd_end(with rsp)
    return SD_FAIL;
}

int Chk_CMDend(int cmd, int be_resp)
{
    int finish0;

    if(!be_resp)    // No response
    {
    	finish0=rSDICSTA;
		while((finish0&0x800)!=0x800)	// Check cmd end
		{
		    finish0=rSDICSTA;
		}
		rSDICSTA=finish0;				// Clear cmd end state (??)
		return SD_SUCCESS;
    }
    else			// With response
    {
    	finish0=rSDICSTA;
		while( !(((finish0&0x200)==0x200) | ((finish0&0x400)==0x400) ))    // Check cmd/rsp end
		{
			finish0=rSDICSTA;
		}

		if((cmd==1) || (cmd==41))	// CRC no check, CMD9 is a long Resp. command.
		{
		    if( (finish0&0xf00) != 0xa00 )  // Check error
		    {
				rSDICSTA=finish0;   // Clear error state

				if(((finish0&0x400)==0x400)) return SD_TIME_OUT;	// Timeout error
	    	}
		    rSDICSTA=finish0;	// Clear cmd & rsp end state
		}

		else	// CRC check
		{
		    if( (finish0&0x1f00) != 0xa00 )	// Check error
		    {
				rSDICSTA=finish0;   // Clear error state
				if(((finish0&0x400)==0x400)) return SD_TIME_OUT;	// Timeout error
    	    }
			rSDICSTA=finish0;
		}
		return SD_SUCCESS;
    }
}

int Chk_DATend(void)
{
    int finish;

    finish=rSDIDSTA;
    while( !( ((finish&0x10)==0x10) | ((finish&0x20)==0x20) ))
	// Chek timeout or data end
	finish=rSDIDSTA;

    if( (finish&0xfc) != 0x10 )
    {
        rSDIDSTA=0xec;  // Clear error state
        return SD_NO_DATA_END;
    }
    return SD_SUCCESS;
}

