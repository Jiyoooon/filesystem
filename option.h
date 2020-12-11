//=============================================================================
// File Name : option.h
//=============================================================================

#define	FCLK		399650000		// 399.650MHz (400MHz)

#define	HCLK		(FCLK/4)
#define	PCLK		(FCLK/8)

/*************************************/
/* Memory Configuration (SDRAM 32MB) */
/*************************************/

// LCD Frame Buffer
#define END_OF_SDRAM	0x32000000				   	// 32MB
#define LCD_FB0			(END_OF_SDRAM - 0x800000)  	// 0x31800000 (must be 4BM align), size는 2MB만 할당
#define LCD_FB1			(END_OF_SDRAM - 0x400000)  	// 0x31C00000 (must be 4BM align), size는 2MB만 할당

// Non Cache Area
#define NON_CACHE_AREA	LCD_FB0						// FB영역 6MB (0x31800000~0x31e00000) Non-Cache 처리

// Heap End Address
#define	HEAP_END_ADDRESS	(NON_CACHE_AREA - 4)	// Heap : ZI_LIMIT ~ FB 직전 주소까지

// MMU Translation Table 주소 : 메모리 끝에서 16KB
#define _MMUTT_STARTADDRESS	(END_OF_SDRAM - 0x4000)	// MMUTT size => 0x1000(16KB)

// Stack Limit Address : 0x31E00000 ~ MMU-TT
#define END_OF_STACK	(LCD_FB1 + 0x200000)		// 6개의 스택 최대사이즈 => 2MB - 16KB
