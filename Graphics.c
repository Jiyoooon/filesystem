#include "2440addr.h"
#include "option.h"
#include "macro.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "device_driver.h"

// GBOX LCD SPI Control

#define SPICLK	(0x1<<13) // GPE13
#define SPIDAT	(0x1<<12) // GPE12
#define SPIENA	(0x1<<2)  // GPG2

#define CLK		13
#define DAT		12
#define CSB		2

#define SPI_ALL_HI()	(rGPEDAT |= SPICLK|SPIDAT, rGPGDAT |= SPIENA)
#define SPI_EN()		(rGPGDAT &= ~SPIENA)
#define SPI_DIS()		(rGPGDAT |= SPIENA)
#define SPI_DAT_LO()	(rGPEDAT &= ~SPIDAT)
#define SPI_DAT_HI()	(rGPEDAT |= SPIDAT)
#define SPI_CLK_LO()	(rGPEDAT &= ~SPICLK)
#define SPI_CLK_HI()	(rGPEDAT |= SPICLK)
#define SPI_delay()		{int i; for(i=0; i<30; i++);}

#define LCD_XSIZE 		(960)	
#define LCD_YSIZE 		(240)
 
#define HOZVAL			(LCD_XSIZE-1)
#define LINEVAL			(LCD_YSIZE-1)

#define VBPD_030561		((18-1)&0xff)
#define VFPD_030561		((15-1)&0xff)
#define VSPW_030561		((1-1)&0x3f)
#define HBPD_030561		((352-1)&0x7f)
#define HFPD_030561		((20-1)&0xff)
#define HSPW_030561 	((108-1)&0xff)
#define CLKVAL			2

#define rLPCSEL (*(volatile unsigned int *)0x4d000060)

#define COPY(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)=*(A+loop);
#define OR(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)|=*(A+loop);
#define M5D(n) 		((n) & 0x1fffff)

#define BPP_16	0
#define BPP_24	1

unsigned int Bit_per_pixel = BPP_16;
unsigned int Trans_mode = 0;
unsigned int Shape_mode = 0;
unsigned int Shape_mode_color = 0;

void Lcd_Init(void);
void Lcd_Set_Address(unsigned int fp);
void Lcd_Port_Init(void);
void Lcd_Power_Enable(int invpwren,int pwren);
void Lcd_Envid_On_Off(int onoff);
void Spi_Port_Init(void);
void Spi_write(int data);

void Spi_Port_Init(void)
{
	// CSB(GPG2), SCK(GPE13), SDI(GPE12) => OUTPUT
	Macro_Write_Block(rGPECON, 0xf, 0x5, 24);
	Macro_Write_Block(rGPGCON, 0x3, 0x1, 4);	
	SPI_ALL_HI();
}

void Spi_write(int data)
{
	int i;
	char shift=8;

	SPI_EN();
	SPI_delay();

	for(i=0;i<24;i++)
	{
		if ((data << shift) & 0x80000000)
		{
			SPI_DAT_HI();
			SPI_CLK_LO();
			SPI_delay();
			SPI_CLK_HI();
			SPI_delay();				
		}
		else
		{
			SPI_DAT_LO();
			SPI_CLK_LO();
			SPI_delay();
			SPI_CLK_HI();
			SPI_delay();				
		}
		shift++; 
  	}

	SPI_ALL_HI();
	SPI_DIS();
}

void Lcd_Set_Address(unsigned int fp)
{
	rLCDSADDR1=((fp>>22)<<21)+M5D(fp>>1);
	rLCDSADDR2=M5D(fp+(LCD_XSIZE*LCD_YSIZE*2));
	rLCDSADDR3=LCD_XSIZE;
}

void Lcd_Envid_On_Off(int onoff)
{
	(onoff) ? (rLCDCON1 |= 1) : (rLCDCON1 &= ~0x1);
}    

void Lcd_Power_Enable(int invpwren,int pwren)
{
    rLCDCON5=(rLCDCON5&(~(1<<3)))|(pwren<<3);
    rLCDCON5=(rLCDCON5&(~(1<<5)))|(invpwren<<5);
}    

void Lcd_Port_Init(void)
{
	Macro_Write_Block(rGPCCON, 0x3, 0x1, 0);
	Macro_Clear_Bit(rGPCDAT, 0);
	Macro_Write_Block(rGPGCON, 0x3, 0x1, 8);
	Macro_Clear_Bit(rGPGDAT, 4);
	rGPCUP=0xffffffff; 
	Macro_Write_Block(rGPCCON, 0xff, 0xaa, 2);
	rGPDUP=0xffffffff; 
	Macro_Write_Block(rGPDCON, 0xfff, 0xaaa, 20);	
}

void Lcd_Init(void)
{
	rLCDCON1=(2<<8)|(3<<5)|(13<<1)|0;
	rLCDCON2=(VBPD_030561<<24)|((LINEVAL)<<14)|(VFPD_030561<<6)|(VSPW_030561);
	rLCDCON3=(HBPD_030561<<19)|(HOZVAL<<8)|(HFPD_030561);
	rLCDCON4=(HSPW_030561);
	rLCDCON5=(0<<12)|(1<<10)|(1<<9)|(1<<8);
	rLCDINTMSK|=(3); 	
	rLPCSEL &=(~7); 	
	rTPAL=0; 		
}

void Lcd_Graphic_Init(void)
{
	Lcd_Port_Init();
	Lcd_Init();	
	Macro_Set_Bit(rGPCDAT, 0);
	Macro_Set_Bit(rGPGDAT, 4);
	Lcd_Power_Enable(0,1);
	Lcd_Envid_On_Off(0); 

   	Lcd_Set_Address(LCD_FB1);
   	
	Spi_Port_Init();
	Spi_write(0x700001);//R01H
	Spi_write(0x727300);//XX00
	Spi_write(0x700002);//R02H
	Spi_write(0x720200);//0200
	Spi_write(0x700003);//R03H...VGH & VGL
	Spi_write(0x726364);//6364
	Spi_write(0x700004);//R04H
	Spi_write(0x7204cf);//04XX
	Spi_write(0x700005);//R05H
	Spi_write(0x72bcd4);//XXXX
	Spi_write(0x70000a);//R0AH
	Spi_write(0x724008);//4008
	Spi_write(0x70000b);//R0BH
	Spi_write(0x72d400);//D400
	Spi_write(0x70000d);//R0DH
	Spi_write(0x723229);//3229
	Spi_write(0x70000e);//R0EH...VcomAC
	Spi_write(0x723200);//3200
	Spi_write(0x70000f);//R0FH
	Spi_write(0x720000);//0000
	Spi_write(0x700016);//R16H
	Spi_write(0x729f80);//9F80
	Spi_write(0x700017);//R17H
	Spi_write(0x723fff);//XXXX
	Spi_write(0x70001e);//R1EH
	Spi_write(0x720052);//0052..CMO 3.5  005f..CMO 4

	Lcd_Envid_On_Off(1);	  
}

#define Fb_ptr0 ((unsigned short (*)[320])LCD_FB0)
#define Fb_ptr1 ((unsigned int (*)[960])LCD_FB1)

void Lcd_Draw_Buffer(void)
{
	int x,y,xx;
	
	for(y=0; y<240; y++)
	{
		for(x=0; x<320; x++)
		{
			xx = (3 * x);

			Fb_ptr1[y][xx] = (Fb_ptr0[y][x^1]<<8) & 0xf80000;
			Fb_ptr1[y][xx+1] = (Fb_ptr0[y][x^1]<<13) & 0xf80000;
			Fb_ptr1[y][xx+2] = (Fb_ptr0[y][x^1]<<18) & 0xf80000;
		}
	}	
}

/////////// BMP Draw - 24bpp //////////

void Lcd_Put_Pixel(int x,int y,int color)
{
	unsigned int sr, sg, sb, mask, r, g, b;

	if(Bit_per_pixel == BPP_16)
	{
		sr = 8, sg = 13, sb = 18;
		mask = 0x00f80000;
	}
	else
	{
		sr = 0, sg = 8, sb = 16;
		mask = 0x00ff0000;
	}

	r = (color << sr) & mask;
	g = (color << sg) & mask;
	b = (color << sb) & mask;

	if(!Trans_mode)
	{
		Fb_ptr1[y][3*x]   = r;
		Fb_ptr1[y][3*x+1] = g;
		Fb_ptr1[y][3*x+2] = b;
	}
	else
	{
		Fb_ptr1[y][3*x]   |= r;
		Fb_ptr1[y][3*x+1] |= g;
		Fb_ptr1[y][3*x+2] |= b;
	}
}

void Lcd_Clr_Screen(int color)
{
	int i,j;

	for(j=0;j<240;j++)
	{
		for(i=0;i<320;i++)
		{
     		Lcd_Put_Pixel(i, j, color);
		}
	}
}

void Lcd_Draw_BMP_File_24bpp(int x, int y, void *fp)
{
	// 24bpp. 비압축 모드만 지원
	int xx, yy, p;
	unsigned char * t;

	unsigned char * raw;
	unsigned int w;
	unsigned int h;
	unsigned int pad;

	Bit_per_pixel = BPP_24;

	t = (unsigned char *)((unsigned int)fp + 0xA);
	raw = (unsigned char *)(t[0]+(t[1]<<8)+(t[2]<<16)+(t[3]<<24)+(unsigned int)fp);

	t = (unsigned char *)((unsigned int)fp + 0x12);
	w = (unsigned int)(t[0]+(t[1]<<8)+(t[2]<<16)+(t[3]<<24));

	t = (unsigned char *)((unsigned int)fp + 0x16);
	h = (unsigned int)(t[0]+(t[1]<<8)+(t[2]<<16)+(t[3]<<24));

	pad = (4-(w*3)%4)%4;

	Uart_Printf("fp=%#x, raw=%#x, w=%d, h=%d\n", fp, raw, w, h);

	for(yy=(h-1);yy>=0;yy--)
	{
		for(xx=0;xx<w;xx++)
		{
			p=(int)((raw[0]<<0)+(raw[1]<<8)+(raw[2]<<16));
			Lcd_Put_Pixel(xx+x,yy+y,p);
			raw += 3;
		}
		raw = (unsigned char *)((unsigned int)raw + pad);
	}

	Bit_per_pixel = BPP_16;
}

//////////////////// GBOX Graphics Library ////////////////////////

void Lcd_Get_Info_BMP(int * x, int  * y, const unsigned short int *fp)
{
	*x =(int)fp[0];    
	*y =(int)fp[1];    
}

void Lcd_Draw_Line(int x1,int y1,int x2,int y2,int color)
{
	int dx,dy,e;
	dx=x2-x1; 
	dy=y2-y1;
    
	if(dx>=0)
	{
		if(dy >= 0) 	// dy>=0
		{
			if(dx>=dy) 	// 1/8 octant
			{
				e=dy-dx/2;
				while(x1<=x2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){y1+=1;e-=dx;}	
					x1+=1;
					e+=dy;
				}
			}
			else		// 2/8 octant
			{
				e=dx-dy/2;
				while(y1<=y2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){x1+=1;e-=dy;}	
					y1+=1;
					e+=dx;
				}
			}
		}
		else		   	// dy<0
		{
			dy=-dy;   	// dy=abs(dy)

			if(dx>=dy) 	// 8/8 octant
			{
				e=dy-dx/2;
				while(x1<=x2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){y1-=1;e-=dx;}	
					x1+=1;
					e+=dy;
				}
			}
			else		// 7/8 octant
			{
				e=dx-dy/2;
				while(y1>=y2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){x1+=1;e-=dy;}	
					y1-=1;
					e+=dx;
				}
			}
		}	
	}
	else //dx<0
	{
		dx=-dx;			//dx=abs(dx)
		if(dy >= 0) 	// dy>=0
		{
			if(dx>=dy) 	// 4/8 octant
			{
				e=dy-dx/2;
				while(x1>=x2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){y1+=1;e-=dx;}	
					x1-=1;
					e+=dy;
				}
			}
			else		// 3/8 octant
			{
				e=dx-dy/2;
				while(y1<=y2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){x1-=1;e-=dy;}	
					y1+=1;
					e+=dx;
				}
			}
		}
		else		   	// dy<0
		{
			dy=-dy;   	// dy=abs(dy)

			if(dx>=dy) 	// 5/8 octant
			{
				e=dy-dx/2;
				while(x1>=x2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){y1-=1;e-=dx;}	
					x1-=1;
					e+=dy;
				}
			}
			else		// 6/8 octant
			{
				e=dx-dy/2;
				while(y1>=y2)
				{
					Lcd_Put_Pixel(x1,y1,color);
					if(e>0){x1-=1;e-=dy;}	
					y1-=1;
					e+=dx;
				}
			}
		}	
	}
}

void Lcd_Draw_Hline(int y, int x1, int x2, int color)
{
     int i, xx1, xx2;
     
     if(x1<x2)
     {
     	xx1=x1;
     	xx2=x2;
     }
     else 
     {
     	xx1=x2;
     	xx2=x1;
     } 
     for(i=xx1;i<=xx2;i++)
     {
         Lcd_Put_Pixel(i,y,color);
     }
} 

void Lcd_Draw_Vline(int x, int y1, int y2, int color)
{
     int i, yy1, yy2;
     
     if(y1<y2)
     {
     	yy1=y1;
     	yy2=y2;
     }
     else 
     {
     	yy1=y2;
     	yy2=y1;
     }	
     for(i=yy1;i<=yy2;i++)
     {
         Lcd_Put_Pixel(x,i,color);	
     }
}	

void Lcd_Draw_Rect(int x1, int y1, int x2, int y2, int color)
{
     int xx1, yy1, xx2, yy2;
     
     if(x1<x2)
     {
     	xx1=x1;
     	xx2=x2;
     }
     else 
     {
     	xx1=x2;
     	xx2=x1;
     } 
     
     if(y1<y2)
     {
     	yy1=y1;
     	yy2=y2;
     } 
     else 
     {
     	yy1=y2;
     	yy2=y1;
     } 
	 
     Lcd_Draw_Hline(yy1,xx1,xx2,color);
     Lcd_Draw_Hline(yy2,xx1,xx2,color);
     Lcd_Draw_Vline(xx1,yy1,yy2,color);
     Lcd_Draw_Vline(xx2,yy1,yy2,color);
} 

void Lcd_Draw_Bar(int x1, int y1, int x2, int y2, int color)
{
     int i, j;
     int xx1, yy1, xx2, yy2;
     
     if(x1<x2)
     {
     	xx1=x1;
     	xx2=x2;
     }
     else 
     {
     	xx1=x2;
     	xx2=x1;
     }
     
     if(y1<y2)
     {
     	yy1=y1;
     	yy2=y2;
     } 
     else 
     {
     	yy1=y2;
     	yy2=y1;
     }
     
     for(i=yy1;i<=yy2;i++)
     {
         for(j=xx1;j<=xx2;j++)
         {
             Lcd_Put_Pixel(j,i,color);
         }
     }
}

///////////////////////// Font Display functions ////////////////////////

static unsigned char _first[]={0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19 };
static unsigned char _middle[]={0,0,0,1,2,3,4,5,0,0,6,7,8,9,10,11,0,0,12,13,14,15,16,17,0,0,18,19,20,21};
static unsigned char _last[]={0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,0,17,18,19,20,21,22,23,24,25,26,27};
static unsigned char cho[]={0,0,0,0,0,0,0,0,0,1,3,3,3,1,2,4,4,4,2,1,3,0};
static unsigned char cho2[]={0,5,5,5,5,5,5,5,5,6,7,7,7,6,6,7,7,7,6,6,7,5};
static unsigned char jong[]={0,0,2,0,2,1,2,1,2,3,0,2,1,3,3,1,2,1,3,3,1,1};

#include "ENG8X16.H"
#include "HAN16X16.H"
#include "HANTABLE.H"

#define 	ENG_FONT_X 		8
#define 	ENG_FONT_Y 		16

#define COPY(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)=*(A+loop);
#define OR(A,B) 	for(loop=0;loop<32;loop++) *(B+loop)|=*(A+loop);

void Lcd_Han_Putch(int x,int y,int color,int bkcolor, int data, int zx, int zy)
{
	unsigned int first,middle,last;	
	unsigned int offset,loop;
	unsigned char xs,ys;
	unsigned char temp[32];
	unsigned char bitmask[]={128,64,32,16,8,4,2,1};     

	first=(unsigned)((data>>8)&0x00ff);
	middle=(unsigned)(data&0x00ff);
	offset=(first-0xA1)*(0x5E)+(middle-0xA1);
	first=*(HanTable+offset*2);
	middle=*(HanTable+offset*2+1);
	data=(int)((first<<8)+middle);    

	first=_first[(data>>10)&31];
	middle=_middle[(data>>5)&31];
	last=_last[(data)&31];     

	if(last==0)
	{
		offset=(unsigned)(cho[middle]*640); 
		offset+=first*32;
		COPY(han16x16+offset,temp);

		if(first==1||first==24) offset=5120;  
		else offset=5120+704;
		offset+=middle*32;
		OR(han16x16+offset,temp);
	}
	else 
	{
		offset=(unsigned)(cho2[middle]*640); 
		offset+=first*32;
		COPY(han16x16+offset,temp);

		if(first==1||first==24) offset=5120+704*2; 
		else offset=5120+704*3;
		offset+=middle*32;
		OR(han16x16+offset,temp);

		offset=(unsigned)(5120+2816+jong[middle]*896);
		offset+=last*32;
		OR(han16x16+offset,temp);
	}

	for(ys=0;ys<16;ys++)
	{
		for(xs=0;xs<8;xs++)
		{
			if(temp[ys*2]&bitmask[xs])
			{
				if( (zx==1)&&(zy==1) ) Lcd_Put_Pixel(x+xs,y+ys,color);
				else if( (zx==2)&&(zy==1) )
				{
					Lcd_Put_Pixel(x+2*xs,y+ys,color);
					Lcd_Put_Pixel(x+2*xs+1,y+ys,color);
				}
				else if( (zx==1)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+xs,y+2*ys,color);
					Lcd_Put_Pixel(x+xs,y+2*ys+1,color);
				}
				else if( (zx==2)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+2*xs,y+2*ys+1,color);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys,color);
					Lcd_Put_Pixel(x+2*xs,y+2*ys,color);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys+1,color);
				}
			}
			else
			{
				if( (zx==1)&&(zy==1) ) Lcd_Put_Pixel(x+xs,y+ys,bkcolor);
				else if( (zx==2)&&(zy==1) )
				{
					Lcd_Put_Pixel(x+2*xs,y+ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+ys,bkcolor);
				}
				else if( (zx==1)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+xs,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+xs,y+2*ys+1,bkcolor);
				}
				else if( (zx==2)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+2*xs,y+2*ys+1,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys+1,bkcolor);
				}	   	
			}
		}

		for(xs=0;xs<8;xs++)
		{
			if(temp[ys*2+1]&bitmask[xs])
			{
				if( (zx==1)&&(zy==1) )
				Lcd_Put_Pixel(x+xs+8,y+ys,color);
				else if( (zx==2)&&(zy==1) ){
				Lcd_Put_Pixel(x+2*(xs+8),y+ys,color);
				Lcd_Put_Pixel(x+2*(xs+8)+1,y+ys,color);
				}
				else if( (zx==1)&&(zy==2) ){
				Lcd_Put_Pixel(x+(xs+8),y+2*ys,color);
				Lcd_Put_Pixel(x+(xs+8),y+2*ys+1,color);
				}
				else if( (zx==2)&&(zy==2) ){
				Lcd_Put_Pixel(x+2*(xs+8),y+2*ys+1,color);
				Lcd_Put_Pixel(x+2*(xs+8)+1,y+2*ys,color);
				Lcd_Put_Pixel(x+2*(xs+8),y+2*ys,color);
				Lcd_Put_Pixel(x+2*(xs+8)+1,y+2*ys+1,color);
				}
			}
			else
			{	   	
				if( (zx==1)&&(zy==1) ) Lcd_Put_Pixel(x+xs+8,y+ys,bkcolor);
				else if( (zx==2)&&(zy==1) )
				{
					Lcd_Put_Pixel(x+2*(xs+8),y+ys,bkcolor);
					Lcd_Put_Pixel(x+2*(xs+8)+1,y+ys,bkcolor);
				}
				else if( (zx==1)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+(xs+8),y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+(xs+8),y+2*ys+1,bkcolor);
				}
				else if( (zx==2)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+2*(xs+8),y+2*ys+1,bkcolor);
					Lcd_Put_Pixel(x+2*(xs+8)+1,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*(xs+8),y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*(xs+8)+1,y+2*ys+1,bkcolor);
				}	   	
			}
		}
	}
}

void Lcd_Eng_Putch(int x,int y,int color,int bkcolor,int data, int zx, int zy)
{
	unsigned offset,loop;
	unsigned char xs,ys;
	unsigned char temp[32];
	unsigned char bitmask[]={128,64,32,16,8,4,2,1};     

	offset=(unsigned)(data*16);
	COPY(eng8x16+offset,temp);

	for(ys=0;ys<16;ys++)
	{
		for(xs=0;xs<8;xs++)
		{
			if(temp[ys]&bitmask[xs])
			{

				if( (zx==1)&&(zy==1) ) Lcd_Put_Pixel(x+xs,y+ys,color);
				else if( (zx==2)&&(zy==1) )
				{
					Lcd_Put_Pixel(x+2*xs,y+ys,color);
					Lcd_Put_Pixel(x+2*xs+1,y+ys,color);
				}
				else if( (zx==1)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+xs,y+2*ys,color);
					Lcd_Put_Pixel(x+xs,y+2*ys+1,color);
				}
				else if( (zx==2)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+2*xs,y+2*ys+1,color);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys,color);
					Lcd_Put_Pixel(x+2*xs,y+2*ys,color);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys+1,color);
				}
			} 
			else
			{
				if( (zx==1)&&(zy==1) ) Lcd_Put_Pixel(x+xs,y+ys,bkcolor);
				else if( (zx==2)&&(zy==1) )
				{
					Lcd_Put_Pixel(x+2*xs,y+ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+ys,bkcolor);
				}
				else if( (zx==1)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+xs,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+xs,y+2*ys+1,bkcolor);
				}
				else if( (zx==2)&&(zy==2) )
				{
					Lcd_Put_Pixel(x+2*xs,y+2*ys+1,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs,y+2*ys,bkcolor);
					Lcd_Put_Pixel(x+2*xs+1,y+2*ys+1,bkcolor);
				}   	
			}
		}
	}
}

void Lcd_Puts(int x, int y, int color, int bkcolor, char *str, int zx, int zy)
{
     unsigned data;
   
     while(*str)
     {
        data=*str++;
        if(data>=128) 
        { 
             data*=256;
             data|=*str++;
             Lcd_Han_Putch(x, y, color, bkcolor, (int)data, zx, zy);
             x+=zx*16;
        }
        else 
        {
             Lcd_Eng_Putch(x, y, color, bkcolor, (int)data, zx, zy);
             x+=zx*ENG_FONT_X;
        }
     } 
} 

void Lcd_Printf(int x, int y, int color, int bkcolor, int zx, int zy, char *fmt,...)
{
	va_list ap;
	char string[256];

	va_start(ap,fmt);
	vsprintf(string,fmt,ap);
	Lcd_Puts(x, y, color, bkcolor, string, zx, zy);
	va_end(ap);
}


