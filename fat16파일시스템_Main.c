/***********************************************************/
// 다음은 사용자 함수를 호출하는 루틴임 지우지 말것!
/***********************************************************/

#include "device_driver.h"
#define printf		Uart_Printf
#define main		User_Main

void User_Main(void);

void Main(void)
{
	MMU_Init();
	Uart_Init(115200);	
	printf("\n=================================\n");
	main();
	printf("=================================\n");
}


/***********************************************************/
// [3] : FAT16 구현
/***********************************************************/

#if 01

#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#pragma pack(push, 1)

/* MBR을 모델링하기 위한 구조체 타입 선언 */
typedef struct{//16byte
	unsigned char reserved[8];
	unsigned int start;//LBAStart
	unsigned int size;//Size in Sector
}TABLE;

typedef struct{
	unsigned char bootCode[446];
	TABLE table[4];
	unsigned short signature;
}MBR;

/* BR을 모델링하기 위한 구조체 타입 선언 */
typedef struct{
	unsigned char reserved[11];//0~10
	unsigned short bytePerSector;//11~12
	unsigned char sectorPerCluster;//13
	unsigned short rsvd;//14~15
	unsigned char noOfFats;//16
	unsigned short rootEntryCnt;//17~18
	unsigned char reserved2[3];//19~21
	unsigned short fatSize;//22~23
	unsigned char reserved3[8];//24~31
	unsigned int totalSector;//32~35
}BR;

/* File Entry 분석을 위한 구조체, 공용체 타입 선언 */
typedef struct{
	unsigned char r:1;
	unsigned char h:1;
	unsigned char s:1;
	unsigned char v:1;
	unsigned char d:1;
	unsigned char a:1;
	unsigned char rsvd:2;
}FILETYPE;

typedef struct{
	unsigned char ln:4;
	unsigned char : 0;
}LONG_FILE;

typedef union{
	FILETYPE file;
	LONG_FILE l;
	unsigned char c;
}FILE_ATTR;
/* 시간 포맷을 위한 비트필드 구조체 선언 */
typedef struct{
	unsigned short sec:5;
	unsigned short min:6;
	unsigned short hour:5;
}TIME;

/* 날짜 포맷을 위한 비트필드 구조체 선언 */
typedef struct{
	unsigned short day:5;
	unsigned short month:4;
	unsigned short year:7;
}DATE;

/* 하나의 32B Entry 분석을 위한 구조체 */

typedef struct
{
	/* Entry의 각 멤버를 설계한다 */
	unsigned char fileName[11];//첫바이트가 중요
	FILE_ATTR attribute;//filetype
	unsigned char reserved;//always 0
	unsigned char creationTimeTenth;
	TIME creationTime;//TIME
	DATE creationDate;//DATE
	DATE lastAccessDate;
	unsigned short firstClusterHigh;
	TIME lastWriteTime;
	DATE lastWriteDate;
	unsigned short firstClusterLow;
	unsigned int fileSize;
}ENTRY;

#pragma pack(pop)

/* MBR, BR 분석을 통하여 획득하여 저장해야 하는 정보들 */
static struct _parameter
{
	unsigned int lba_start;
	unsigned short byte_per_sector;
	unsigned char sector_per_cluster;
	unsigned int root_sector_count;
	unsigned int fat0_start;
	unsigned int root_start;
	unsigned int file_start;
}parameter;

/* 하부 설계되는 함수 목록 */

static void listing_file(void);
static void listing_file_in_subDir(ENTRY * curDir);
//static ENTRY * search_file(int id);
static int compare_filename(char * name, char * etc,char * filename);
static ENTRY * search_file_by_filename(char * filename);
static ENTRY * search_file_by_filename_in_subDir(char * filename, ENTRY * curDir);
static ENTRY * search_dir(char * dir);
static ENTRY * search_dir_in_subDir(char * dir, ENTRY * curDir);
static void change_filename(U32 file_wp, ENTRY * file_pointer, char * new_name, char * new_etc);
static int rename_file(char * old_filename, char * new_name, char * new_etc);
static int rename_file_in_subDir(char * old_filename, char * new_name, char * new_etc, ENTRY * curDir);
static int check_cluster_space(int clusterCnt);
static void edit_entry_and_copy_file(char * name, char * etc, int clusterCnt, ENTRY * origin_file);
static int copy_file(char * old, char * new_name, char * new_etc);
static int copy_file_in_subDir(char * old, char * new_name, char * new_etc, ENTRY * curDir);
static int check_file_type(ENTRY * file);
static void read_file(ENTRY * file, void * data);
static void sector_printf(void * buf);

/* 512B 단위의 섹터를 읽어서 저장하기 위한 임시 버퍼 */

#define WP_INIT -1
static unsigned int buf[512/4];
static unsigned short sector[512/2];
static ENTRY * target_file_pointer;
static ENTRY * empty_file_pointer;
static U32 target_file_wp = WP_INIT;//writePointer
static U32 empty_file_wp = WP_INIT;
static int clusterList[100000];

/* 512B 단위의 섹터를 인쇄해주는 디버깅용 함수 */

static void sector_printf(void * buf)
{
	int i, j, k;

	for(i=0; i<(128/8); i++)
	{
		printf("[%3d]", i*32);

		for(j=0; j<8; j++)
		{
			for(k=0; k<4; k++)
			{
				printf("%c", ((char *)buf)[i*32+j*4+k]);
			}
			printf(" ");
		}
		printf("\n");
	}
}

void main(void)
{
	int i;
	ENTRY * curDir = (ENTRY *)0;

	Lcd_Graphic_Init();
	Lcd_Clr_Screen(0xf800);

	if(SD_Check_Card() != SD_SUCCESS) printf("Insert SD Card Please!\n");

	for(;;)
	{
		if(SD_Check_Card() == SD_SUCCESS) break;
	}

	i=SD_Init();
	if(i != SD_SUCCESS) printf("Init Err : [%d]\n", i);
	{

		SD_Read_Sector(0, 1, (U8 *)buf);//512B짜리 buffer -> buf

		/* MBR에서 parameter.lba_start 값을 읽어서 저장한다 */
		parameter.lba_start = ((MBR *)buf)->table[0].start;
		//printf("LBA_Start = %d\n", parameter.lba_start);

		SD_Read_Sector(parameter.lba_start, 1, (U8 *)buf);
		/* BR에서 정보들을 읽어서 parameter 구조체 멤버들에 저장한다 */

		parameter.byte_per_sector = ((BR *)buf)->bytePerSector;
		parameter.sector_per_cluster = ((BR *)buf)->sectorPerCluster;
		parameter.fat0_start = parameter.lba_start + ((BR *)buf)->rsvd;
		parameter.root_start = parameter.fat0_start + (((BR *)buf)->noOfFats * ((BR *)buf)->fatSize);
		parameter.root_sector_count = (((BR *)buf)->rootEntryCnt * 32) / parameter.byte_per_sector;
		parameter.file_start = parameter.root_start + parameter.root_sector_count;


		listing_file();

		for(;;)
		{
			ENTRY * file = (ENTRY *)0, * directory;

			int num, r;
			char order[20];//명령어
			char *p, *old, *new, *new_name, *new_etc;
			
			printf(">> ");
			Uart_GetString(order);

			p = strtok(order," ");//명령어

			if(!strcmp(p,"EXIT")) break;

			if(!strcmp(p,"DIR")){
				if(curDir == (ENTRY *)0) listing_file();
				else listing_file_in_subDir(curDir);
			}
			if(!strcmp(p,"OPEN")){
				p = strtok(NULL," ");
				if(strchr(p,'.') == (char *)0){
					printf("확장자를 함께 적어주세요.\n");
					continue;
				}

				if(curDir == (ENTRY *)0) file = search_file_by_filename(p);
				else file = search_file_by_filename_in_subDir(p,curDir);

				if((file == (ENTRY *)0))
				{
					printf("없는 파일 입니다.\n");
					continue;
				}

				/* C, TXT, BMP 파일인지 확인 */
				r = check_file_type(file);

				if(r != 0){//선택된 파일 출력
					char * data;
					int size = file->fileSize;

					/* 파일 크기를 초과하는 섹터 단위의 메모리로 할당을 받아야 한다 */
					num = size+512;
					data = malloc(num);

					/* 파일 데이터 읽기 */
					read_file(file, data);

					switch(r)
					{
						case 1 :
						case 2 :
							for(i = 0; i < size; i++) printf("%c", ((char *)data)[i]);
							printf("\n");
							break;
						case 3 :
							Lcd_Clr_Screen(0xf800);
							Lcd_Draw_BMP_File_24bpp(0,0,(void *)data);
							break;
						default :
							break;
					}

					free(data);
				}else printf("읽을 수 없는 파일 입니다.\n");
			}

			if(!strcmp(p,"CD")){
				p = strtok(NULL," ");

				if(!strcmp(p,".") || !strcmp(p,"..")) continue;
				if(!strcmp(p,"\\")){
					curDir = (ENTRY *)0;
					listing_file();
					continue;
				}
				if(curDir == (ENTRY *)0) directory = search_dir(p);
				else directory = search_dir_in_subDir(p,curDir);

				if(directory == (ENTRY *)0){
					printf("디렉토리를 찾지 못했습니다.\n");
					continue;
				}
				curDir = directory;
				listing_file_in_subDir(curDir);
			}

			if(!strcmp(p,"CD.")) continue;

			if(!strcmp(p,"CD..")){
				if(curDir == (ENTRY *)0) continue;//root directory
				else directory = search_dir_in_subDir("..",curDir);

				if(directory->firstClusterLow == 0x0){//root로 들어감
					curDir = (ENTRY *)0;
					listing_file();
					continue;
				}
				curDir = directory;
				listing_file_in_subDir(curDir);
			}

			if(!strcmp(p,"RENAME")){
				old = strtok(NULL," ");
				new = strtok(NULL," ");

				if(new == NULL){
					printf("바꿀 파일명을 입력해주세요.\n");
					continue;
				}
				if(strchr(old,'.') == (char *)0 || strchr(new,'.') == (char *)0){
					printf("확장자를 함께 입력해주세요.\n");
					continue;
				}

				new_name = strtok(new,".");
				new_etc = strtok(NULL,"\n");

				if(curDir == (ENTRY *)0) r = rename_file(old,new_name,new_etc);
				else r = rename_file_in_subDir(old,new_name,new_etc,curDir);

				if(r == -1){
					printf("파일을 찾지 못했습니다.\n");
					continue;
				}
				if(r == -2){
					printf("파일명이 중복됩니다. 새로운 이름으로 다시 시도해주세요.\n");
					continue;
				}
				SD_Read_Sector(target_file_wp,1,(U8 *)sector);
				change_filename(target_file_wp, target_file_pointer, new_name,new_etc);
				SD_Write_Sector(target_file_wp,1,(U8 *)sector);
				target_file_wp = WP_INIT;
			}
			if(!strcmp(p,"COPY")){
				old = strtok(NULL," ");
				new = strtok(NULL," ");

				if(new == NULL){
					printf("복사 후 파일명을 입력해주세요.\n");
					continue;
				}
				if(strchr(old,'.') == (char *)0 || strchr(new,'.') == (char *)0){
					printf("확장자를 함께 입력해주세요.\n");
					continue;
				}

				new_name = strtok(new,".");
				new_etc = strtok(NULL,"\n");

				if(curDir == (ENTRY *)0) r = copy_file(old,new_name,new_etc);
				else r = copy_file_in_subDir(old,new_name,new_etc,curDir);

				if(r == -1){
					printf("파일을 찾지 못했습니다.\n");
					continue;
				}
				if(r == -2){
					printf("파일명이 중복됩니다. 새로운 이름으로 다시 시도해주세요.\n");
					continue;
				}
				if(r == -3){
					printf("파일을 복사할 공간이 부족합니다.\n");
					continue;
				}

				SD_Read_Sector(target_file_wp,1,(U8 *)sector);
				*file = *target_file_pointer;

				num = file->fileSize / (parameter.sector_per_cluster * parameter.byte_per_sector);
				if(file->fileSize % (parameter.sector_per_cluster * parameter.byte_per_sector) > 0) num++;

				r = check_cluster_space(num);//num : 복사하는데 필요한 cluster개수

				if(r == -1){
					printf("파일을 복사할 cluster 공간이 부족합니다.\n");
					target_file_wp = empty_file_wp = WP_INIT;
					continue;
				}

				edit_entry_and_copy_file(new_name,new_etc,num,file);

				target_file_wp = empty_file_wp = WP_INIT;
			}
		}
		printf("BYE!\n");
	}
}

static void printFileInfo(ENTRY * p, int n){
	printf("[%.3d] ",n);
	printf("%.8s",p->fileName);
	printf(".%.3s ",p->fileName+8);
	printf("%#x ",p->attribute);
	printf("%d:%.2d:%.2d ",p->creationDate.year+1980, p->creationDate.month, p->creationDate.day);
	printf("%.2d:%.2d:%.2d ",p->creationTime.hour, p->creationTime.min, p->creationTime.sec*2);
	printf("%5d ",p->firstClusterLow);
	printf("%10d ",p->fileSize);
	printf("\n");
}

static void listing_file(void)
{
	int i, j, num = 1;

	printf("[NUM] [NAME .EXT] [AT] [  DATE  ] [TIME] [CLUST] [ SIZE ]\n");
	printf("=============================================================\n");

	for(i = 0; i < parameter.root_sector_count; i++)//한Sector 씩 읽어옴
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)buf);

		for(j = 0; j < (parameter.byte_per_sector / 32); j++)//32BYTE의 entry로 다시 쪼갬
		{
			/* Name[0]가 0x0이면 인쇄 종료, 0x05, 0xE5이면 삭제파일 Skip */
			if((((ENTRY *)buf)+j)->fileName[0] == 0x0) goto HERE;
			if((((ENTRY *)buf)+j)->fileName[0] == 0x05 || (((ENTRY *)buf)+j)->fileName[0] == 0xE5) continue;
			/* 파일 속성이 0x3F 또는 0x0F long file name 이므로 Skip */
			if((((ENTRY *)buf)+j)->attribute.c == 0x3F || (((ENTRY *)buf)+j)->attribute.l.ln == 0x0F) continue;

			/* Entry 정보 인쇄 */
			printFileInfo(((ENTRY *)buf)+j, num++);
			/* 인쇄되는 파일 또는 폴더 마다 맨 앞에 1번부터 1씩 증가하며 번호를 인쇄한다 */
		}
	}
	HERE:
	printf("=============================================================\n");
}

static void listing_file_in_subDir(ENTRY * entry)
{
	int i, j, num = 1;
	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼

	//최초clustorNum
	unsigned short clusterNum = entry->firstClusterLow;

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	printf("[NUM] [NAME .EXT] [AT] [  DATE  ] [TIME] [CLUST] [ SIZE ]\n");
	printf("=============================================================\n");

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32개의 entry로 다시 쪼갬
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//이 뒤로 파일X
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				printFileInfo(((ENTRY *)sector)+j, num++);
			}
			if((i+1)%2 == 0){
				printf("any key continue\n");
				Uart_Get_Char();
			}
		}
		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	printf("=============================================================\n");
}

static int compare_filename(char * name, char * etc,char * filename){
	int i;

	for(i=0;i<8;i++){
		if(name[i] == filename[i]) continue;
		if(name[i] == '\0' && filename[i] == ' ') break;//같음
		if(name[i] != filename[i]) return 0;
	}
	for(i=0;i<3;i++){
		if(etc[i] == filename[i+8]) continue;
		if(etc[i] == '\0' && filename[i+8] == ' ') break;//같음
		if(etc[i] != filename[i+8]) return 0;
	}

	return 1;
}

static ENTRY * search_file_by_filename(char * p)
{
	int i, j, r;
	char *name = strtok(p,".");
	char *etc = strtok(NULL,"\n");

	for(i = 0; i < parameter.root_sector_count; i++)
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)sector);

		for(j = 0; j < (parameter.byte_per_sector / 32); j++)
		{
			if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;
			if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
			if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

			r = compare_filename(name,etc,(char *)(((ENTRY *)sector)+j)->fileName);
			if(r == 1) return ((ENTRY *)sector)+j;
		}
	}
	HERE:
	return (ENTRY *)0;
}

static ENTRY * search_file_by_filename_in_subDir(char * p, ENTRY * curDir)
{
	int i, j, r;
	char *name = strtok(p,".");
	char *etc = strtok(NULL,"\n");
	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼

	//최초clustorNum
	unsigned short clusterNum = curDir->firstClusterLow;

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32개의 entry로 다시 쪼갬
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//이 뒤로 파일X
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				r = compare_filename(name,etc,(char *)(((ENTRY *)sector)+j)->fileName);
				if(r == 1) return ((ENTRY *)sector)+j;
			}
		}
		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	return (ENTRY *)0;
}

static ENTRY * search_dir(char * dir)
{
	int i, j, k;

	for(i = 0; i < parameter.root_sector_count; i++)
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)buf);

		for(j = 0; j < (parameter.byte_per_sector / 32); j++)
		{
			if((((ENTRY *)buf)+j)->fileName[0] == 0x0) goto HERE;
			if((((ENTRY *)buf)+j)->fileName[0] == 0x05 || (((ENTRY *)buf)+j)->fileName[0] == 0xE5) continue;
			if((((ENTRY *)buf)+j)->attribute.file.d == 0) continue;//directory가 아닌 경우

			for(k=0;;k++){
				if(dir[k] == (((ENTRY *)buf)+j)->fileName[k]) continue;
				if(dir[k] == '\0' && (((ENTRY *)buf)+j)->fileName[k] == ' ') return ((ENTRY *)buf)+j;
				if(dir[k] != (((ENTRY *)buf)+j)->fileName[k]) break;
			}
		}
	}
	HERE:
	return (ENTRY *)0;
}

static ENTRY * search_dir_in_subDir(char * dir, ENTRY * curDir){
	int i,j,k;
	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼
	ENTRY * returnDir;
	unsigned short clusterNum = curDir->firstClusterLow;//최초clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)buf);
			for(j = 0; j < (sectorSize / 32); j++)//32개의 entry로 다시 쪼갬
			{
				if(j == 0) returnDir = ((ENTRY *)buf)+j;//현재 directory저장

				if((((ENTRY *)buf)+j)->fileName[0] == 0x0) goto HERE;//이 뒤로 파일X
				if((((ENTRY *)buf)+j)->fileName[0] == 0x05 || (((ENTRY *)buf)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)buf)+j)->attribute.file.d == 0) continue;//directory X

				for(k=0;;k++){
					if(dir[k] == (((ENTRY *)buf)+j)->fileName[k]) continue;
					if(dir[k] == '\0' && (((ENTRY *)buf)+j)->fileName[k] == ' ') return ((ENTRY *)buf)+j;
					if(dir[k] != (((ENTRY *)buf)+j)->fileName[k]) break;
				}
			}
		}
	}
	HERE:
	printf("디렉토리를 찾지 못했습니다.\n");
	return returnDir;
}

static void change_filename(U32 file_wp, ENTRY * file_pointer, char * new_name, char * new_etc){
	int k;

	for(k=0;k<8;k++){//파일이름
		if(new_name[k] == '\0') break;
		file_pointer->fileName[k] = new_name[k];
	}
	for(;k<8;k++) file_pointer->fileName[k] = ' ';
	for(k=0;k<3;k++){//확장자
		if(new_etc[k] == '\0') break;
		file_pointer->fileName[k+8] = new_etc[k];
	}
	for(;k<3;k++) file_pointer->fileName[k+8] = ' ';
}

static int rename_file(char * old, char * new_name, char * new_etc)
{
	int i, j, r;
	int isDouble=0;
	char *old_name = strtok(old,".");
	char *old_etc = strtok(NULL,"\n");

	for(i = 0; i < parameter.root_sector_count; i++)
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)sector);
		for(j = 0; j < (parameter.byte_per_sector / 32); j++)
		{
			if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;
			if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
			if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

			if(!isDouble && compare_filename(new_name,new_etc,(char *)(((ENTRY *)sector)+j)->fileName)) isDouble = 1;
			r = compare_filename(old_name,old_etc,(char *)(((ENTRY *)sector)+j)->fileName);
			if(r == 1){
				target_file_pointer = ((ENTRY *)sector)+j;
				target_file_wp = parameter.root_start + i;
			}
		}
	}
	HERE:
	//파일명 중복
	if(isDouble){
		if(target_file_wp == -1) return -1;//중복 & 파일 못찾음
		return -2;
	}
	return target_file_wp;
}

static int rename_file_in_subDir(char * old, char * new_name, char * new_etc, ENTRY * curDir)
{

	int i, j, r, isDouble=0;
	char *old_name = strtok(old,".");
	char *old_etc = strtok(NULL,"\n");

	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼
	unsigned short clusterNum = curDir->firstClusterLow;//최초clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32개의 entry로 다시 쪼갬
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//이 뒤로 파일X
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				r = compare_filename(old_name,old_etc,(char *)(((ENTRY *)sector)+j)->fileName);
				if(!isDouble && compare_filename(new_name,new_etc,(char *)(((ENTRY *)sector)+j)->fileName)) isDouble = 1;
				if(r == 1){
					target_file_pointer= ((ENTRY *)sector)+j;
					target_file_wp = parameter.file_start + jumpSize + i;
				}
			}
		}
		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	//파일명 중복
	if(isDouble){
		if(target_file_wp == -1) return -1;//중복 & 파일 못찾음
		return -2;
	}
	return target_file_wp;
}

static int check_cluster_space(int clusterCnt){
	int i,j,num=0;
	unsigned int fat0_buf[512/4];

	for(i=0;i<parameter.root_start - parameter.fat0_start;i++){
		SD_Read_Sector(parameter.fat0_start + i,1,(U8 *)fat0_buf);
		for(j=0;j<parameter.byte_per_sector / 2;j++){
			if(((short *)fat0_buf)[j] != 0x0) continue;
			//빈클러스터 탐색
			clusterList[num++] = (i * (parameter.byte_per_sector/2)) + j;//cluster번호 저장
			if(num == clusterCnt) return 1;
		}
	}
	return -1;
}


static void edit_entry_and_copy_file(char * name, char * etc, int clusterCnt, ENTRY * origin_file){
	int i,j,fat0_index,origin_jump, copy_jump,cnt,size;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = sectorSize/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼
	unsigned short origin_cluster, copy_cluster;
	unsigned int data_buf[512/4];

	//entry 생성
	origin_cluster = origin_file->firstClusterLow;

	SD_Read_Sector(empty_file_wp, 1, (U8 *)sector);
	*empty_file_pointer = *origin_file;
	empty_file_pointer->firstClusterLow = clusterList[0];
	change_filename(empty_file_wp, empty_file_pointer, name, etc);

	SD_Write_Sector(empty_file_wp, 1, (U8 *)sector);

	//파일 복붙
	size = origin_file->fileSize;
	fat0_index = origin_cluster/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);

	for(i=0;i<clusterCnt;i++){
		copy_cluster = clusterList[i];
		origin_jump = parameter.sector_per_cluster * (origin_cluster-2);
		copy_jump = parameter.sector_per_cluster * (copy_cluster-2);

		origin_cluster = ((short *)fat0_buf)[origin_cluster%addressCnt];//다음 clustorNum
		if(origin_cluster/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = origin_cluster/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		if(origin_cluster == 0xFFFF) {//마지막페이지->안에꺼 읽고 return
			cnt = size/sectorSize + 1;
			for(j=0;j<cnt;j++){
				SD_Read_Sector(parameter.file_start + origin_jump + j,1,(U8 *)data_buf);
				SD_Write_Sector(parameter.file_start + copy_jump + j,1,(U8 *)data_buf);
				size -= sectorSize;
			}
		}else{
			//32개 sector 모두 옮겨 씀
			for(j=0;j<parameter.sector_per_cluster;j++){//32sector 전부 읽어야 함
				SD_Read_Sector(parameter.file_start + origin_jump + j,1,(U8 *)data_buf);
				SD_Write_Sector(parameter.file_start + copy_jump + j,1,(U8 *)data_buf);
				size -= sectorSize;
			}
		}
	}

	//fat0정보 수정
	for(i=0;i<clusterCnt-1;i++){
		copy_cluster = clusterList[i];
		if(copy_cluster/addressCnt != fat0_index){
			fat0_index = copy_cluster/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
		((short *)fat0_buf)[copy_cluster%addressCnt] = clusterList[i+1];
		SD_Write_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
	}
	//마지막 CLUSTER 0XFFFF
	copy_cluster = clusterList[i];
	if(copy_cluster/addressCnt != fat0_index){
		fat0_index = copy_cluster/addressCnt;
		SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
	}
	((short *)fat0_buf)[copy_cluster%addressCnt] = 0xFFFF;
	SD_Write_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
}

static int copy_file(char * old, char * new_name, char * new_etc)
{
	int i, j, r;
	int isDouble=0;
	char *old_name = strtok(old,".");
	char *old_etc = strtok(NULL,"\n");

	for(i = 0; i < parameter.root_sector_count; i++)
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)sector);
		for(j = 0; j < (parameter.byte_per_sector / 32); j++)
		{
			if((((ENTRY *)sector)+j)->fileName[0] == 0x0) {
				empty_file_pointer = ((ENTRY *)sector)+j;
				empty_file_wp = parameter.root_start + i;
				goto HERE;
			}
			if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
			if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

			if(!isDouble && compare_filename(new_name,new_etc,(char *)(((ENTRY *)sector)+j)->fileName)) isDouble = 1;
			r = compare_filename(old_name,old_etc,(char *)(((ENTRY *)sector)+j)->fileName);
			if(r == 1){
				target_file_pointer = ((ENTRY *)sector)+j;
				target_file_wp = parameter.root_start + i;
			}
		}
	}
	HERE:

	if(target_file_wp != -1){//파일 찾음
		if(isDouble) return -2;//중복
		if(!empty_file_wp) return -3;//entry 빈공간X
		return 1;
	}
	return -1;//파일 못찾음
}

static int copy_file_in_subDir(char * old, char * new_name, char * new_etc, ENTRY * curDir)
{

	int i, j, r, isDouble=0;
	char *old_name = strtok(old,".");
	char *old_etc = strtok(NULL,"\n");

	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼
	unsigned short clusterNum = curDir->firstClusterLow;//최초clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32개의 entry로 다시 쪼갬
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) {
					empty_file_pointer = ((ENTRY *)sector)+j;
					empty_file_wp = parameter.file_start + jumpSize + i;
					goto HERE;//이 뒤로 파일X
				}
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				r = compare_filename(old_name,old_etc,(char *)(((ENTRY *)sector)+j)->fileName);
				if(!isDouble && compare_filename(new_name,new_etc,(char *)(((ENTRY *)sector)+j)->fileName)) isDouble = 1;
				if(r == 1){
					target_file_pointer = ((ENTRY *)sector)+j;
					target_file_wp = parameter.file_start + jumpSize + i;
				}
			}
		}
		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	//파일명 중복
	if(target_file_wp != -1){//파일 찾음
		if(isDouble) return -2;//중복
		return 1;
	}
	return -1;//파일 못찾음
}
static int check_file_type(ENTRY * file)
{
	int i;
	char type[4];
	/* 리턴 값 : C 파일 => 1, TXT 파일 => 2, BMP 파일 => 3, 그외 => 0 리턴 */
	for(i=0;i<3;i++){
		type[i] = (file->fileName + 8)[i];
	}
	type[3]='\0';

	if(strcmp(type,"C  ") == 0) return 1;
	if(strcmp(type,"TXT") == 0) return 2;
	if(strcmp(type,"BMP") == 0) return 3;

	return 0;
}

static void read_file(ENTRY * file, void * data)
{
	// 주어진 Entry의 실제 데이터를 읽어서 data 주소에 저장한다
	int i,fat0_index,jumpSize,cnt;
	int sectorSize=parameter.byte_per_sector;//512
	int size = file->fileSize;//filesize
	unsigned int addressCnt = sectorSize/2;//512B buf안에 들어있는 cluster 주소 개수
	unsigned short fat0_buf[addressCnt];//cluster 256개 주소 가지고 있는 버퍼
	U8 *data_buf = data;//임시버퍼 1바이트 단위로 access

	unsigned short clusterNum;
	//최초clustorNum
	clusterNum = file->firstClusterLow;
	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		//다음 clusterNum으로 넘어감
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//다음 clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0에서 새로운 256개 주소 지역 가져와야함
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		if(clusterNum == 0xFFFF) {//마지막페이지->안에꺼 읽고 return
			cnt = size/sectorSize + 1;
			for(i=0;i<cnt;i++){
				SD_Read_Sector(parameter.file_start + jumpSize + i,1,data_buf);
				data_buf += sectorSize;
				size -= sectorSize;
			}
			return;
		}else{
			//32개 sector 모두 *data에 옮겨 씀
			for(i=0;i<parameter.sector_per_cluster;i++){//32sector 전부 읽어야 함
				SD_Read_Sector(parameter.file_start + jumpSize + i,1,data_buf);
				data_buf += sectorSize;

				size -= sectorSize;
			}

		}
	}
}
#endif

