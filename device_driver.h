typedef unsigned int 		U32;
typedef signed int			S32;
typedef unsigned short		U16;
typedef short int			S16;
typedef unsigned char		U8;
typedef signed char 		S8;
typedef unsigned long long 	ULL;

// Heap.c
extern void * Get_Heap_Limit(void);
extern void * Get_Heap_Base(void);
extern unsigned int Get_Heap_Size(void);
extern void Heap_Library_Initial(void);

// Led.c
extern void Led_Init(void);
extern void Led_Display(int disp);

// Uart.c
extern void Uart_Init(int baud);
extern void Uart_Fifo_Init(void);
extern int Uart_Printf(char *fmt,...);
extern char Uart_Get_Char(void);
extern char Uart_Get_Pressed(void);
extern int Uart_GetIntNum(void);
extern void Uart_GetString(char *string);

// Graphics.c
extern void Lcd_Graphic_Init(void);
extern void Lcd_Draw_BMP(int x, int y, const unsigned short int *fp);
extern void Lcd_Draw_Buffer(void);
extern void Lcd_Put_Pixel(int x,int y,int color);
extern void Lcd_Draw_BMP_File_24bpp(int x, int y, void *fp);
extern void Lcd_Clr_Screen(int color);

extern void Lcd_Draw_Line(int x1,int y1,int x2,int y2,int color);
extern void Lcd_Draw_Hline(int y, int x1, int x2, int color);	
extern void Lcd_Draw_Vline(int x, int y1, int y2, int color);	
extern void Lcd_Draw_Rect(int x1, int y1, int x2, int y2, int color);
extern void Lcd_Draw_Bar(int x1, int y1, int x2, int y2, int color);
extern void Lcd_Puts(int x, int y, int color, int bkcolor, char *str, int zx, int zy);
extern void Lcd_Printf(int x, int y, int color, int bkcolor, int zx, int zy, char *fmt,...);

// MMU.c
extern void MMU_Init(void);

// Key.c
extern void Key_Poll_Init(void);
extern void Key_IRQ_Init(void);
extern void Key_ISR_Enable(int en);
extern int Key_Get_Pressed(void);
extern void Key_Wait_Key_Released(void);
extern int Key_Wait_Key_Pressed(void);

// Sdi.c
extern int SD_Check_Card(void);
extern int SD_Init(void);
extern int SD_Read_Sector(U32 SecAddr, U32 blocks , U8 * buf);
extern int SD_Write_Sector( U32 SecAddr, U32 blocks , U8 * buf);

#define SD_SUCCESS			0
#define SD_NO_CARD			1
#define SD_INVALID_CARD		2
#define SD_NO_DATA_END		3
#define SD_TIME_OUT			4
#define SD_CMD_ERR			5
#define SD_FAIL				6
#define SD_ERR_OF_CSTAT		8
#define SD_ERR_OF_DSTAT		9

