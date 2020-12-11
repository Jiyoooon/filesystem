/***********************************************************/
// ������ ����� �Լ��� ȣ���ϴ� ��ƾ�� ������ ����!
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
// [3] : FAT16 ����
/***********************************************************/

#if 01

#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#pragma pack(push, 1)

/* MBR�� �𵨸��ϱ� ���� ����ü Ÿ�� ���� */
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

/* BR�� �𵨸��ϱ� ���� ����ü Ÿ�� ���� */
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

/* File Entry �м��� ���� ����ü, ����ü Ÿ�� ���� */
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
/* �ð� ������ ���� ��Ʈ�ʵ� ����ü ���� */
typedef struct{
	unsigned short sec:5;
	unsigned short min:6;
	unsigned short hour:5;
}TIME;

/* ��¥ ������ ���� ��Ʈ�ʵ� ����ü ���� */
typedef struct{
	unsigned short day:5;
	unsigned short month:4;
	unsigned short year:7;
}DATE;

/* �ϳ��� 32B Entry �м��� ���� ����ü */

typedef struct
{
	/* Entry�� �� ����� �����Ѵ� */
	unsigned char fileName[11];//ù����Ʈ�� �߿�
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

/* MBR, BR �м��� ���Ͽ� ȹ���Ͽ� �����ؾ� �ϴ� ������ */
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

/* �Ϻ� ����Ǵ� �Լ� ��� */

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

/* 512B ������ ���͸� �о �����ϱ� ���� �ӽ� ���� */

#define WP_INIT -1
static unsigned int buf[512/4];
static unsigned short sector[512/2];
static ENTRY * target_file_pointer;
static ENTRY * empty_file_pointer;
static U32 target_file_wp = WP_INIT;//writePointer
static U32 empty_file_wp = WP_INIT;
static int clusterList[100000];

/* 512B ������ ���͸� �μ����ִ� ������ �Լ� */

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

		SD_Read_Sector(0, 1, (U8 *)buf);//512B¥�� buffer -> buf

		/* MBR���� parameter.lba_start ���� �о �����Ѵ� */
		parameter.lba_start = ((MBR *)buf)->table[0].start;
		//printf("LBA_Start = %d\n", parameter.lba_start);

		SD_Read_Sector(parameter.lba_start, 1, (U8 *)buf);
		/* BR���� �������� �о parameter ����ü ����鿡 �����Ѵ� */

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
			char order[20];//��ɾ�
			char *p, *old, *new, *new_name, *new_etc;
			
			printf(">> ");
			Uart_GetString(order);

			p = strtok(order," ");//��ɾ�

			if(!strcmp(p,"EXIT")) break;

			if(!strcmp(p,"DIR")){
				if(curDir == (ENTRY *)0) listing_file();
				else listing_file_in_subDir(curDir);
			}
			if(!strcmp(p,"OPEN")){
				p = strtok(NULL," ");
				if(strchr(p,'.') == (char *)0){
					printf("Ȯ���ڸ� �Բ� �����ּ���.\n");
					continue;
				}

				if(curDir == (ENTRY *)0) file = search_file_by_filename(p);
				else file = search_file_by_filename_in_subDir(p,curDir);

				if((file == (ENTRY *)0))
				{
					printf("���� ���� �Դϴ�.\n");
					continue;
				}

				/* C, TXT, BMP �������� Ȯ�� */
				r = check_file_type(file);

				if(r != 0){//���õ� ���� ���
					char * data;
					int size = file->fileSize;

					/* ���� ũ�⸦ �ʰ��ϴ� ���� ������ �޸𸮷� �Ҵ��� �޾ƾ� �Ѵ� */
					num = size+512;
					data = malloc(num);

					/* ���� ������ �б� */
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
				}else printf("���� �� ���� ���� �Դϴ�.\n");
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
					printf("���丮�� ã�� ���߽��ϴ�.\n");
					continue;
				}
				curDir = directory;
				listing_file_in_subDir(curDir);
			}

			if(!strcmp(p,"CD.")) continue;

			if(!strcmp(p,"CD..")){
				if(curDir == (ENTRY *)0) continue;//root directory
				else directory = search_dir_in_subDir("..",curDir);

				if(directory->firstClusterLow == 0x0){//root�� ��
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
					printf("�ٲ� ���ϸ��� �Է����ּ���.\n");
					continue;
				}
				if(strchr(old,'.') == (char *)0 || strchr(new,'.') == (char *)0){
					printf("Ȯ���ڸ� �Բ� �Է����ּ���.\n");
					continue;
				}

				new_name = strtok(new,".");
				new_etc = strtok(NULL,"\n");

				if(curDir == (ENTRY *)0) r = rename_file(old,new_name,new_etc);
				else r = rename_file_in_subDir(old,new_name,new_etc,curDir);

				if(r == -1){
					printf("������ ã�� ���߽��ϴ�.\n");
					continue;
				}
				if(r == -2){
					printf("���ϸ��� �ߺ��˴ϴ�. ���ο� �̸����� �ٽ� �õ����ּ���.\n");
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
					printf("���� �� ���ϸ��� �Է����ּ���.\n");
					continue;
				}
				if(strchr(old,'.') == (char *)0 || strchr(new,'.') == (char *)0){
					printf("Ȯ���ڸ� �Բ� �Է����ּ���.\n");
					continue;
				}

				new_name = strtok(new,".");
				new_etc = strtok(NULL,"\n");

				if(curDir == (ENTRY *)0) r = copy_file(old,new_name,new_etc);
				else r = copy_file_in_subDir(old,new_name,new_etc,curDir);

				if(r == -1){
					printf("������ ã�� ���߽��ϴ�.\n");
					continue;
				}
				if(r == -2){
					printf("���ϸ��� �ߺ��˴ϴ�. ���ο� �̸����� �ٽ� �õ����ּ���.\n");
					continue;
				}
				if(r == -3){
					printf("������ ������ ������ �����մϴ�.\n");
					continue;
				}

				SD_Read_Sector(target_file_wp,1,(U8 *)sector);
				*file = *target_file_pointer;

				num = file->fileSize / (parameter.sector_per_cluster * parameter.byte_per_sector);
				if(file->fileSize % (parameter.sector_per_cluster * parameter.byte_per_sector) > 0) num++;

				r = check_cluster_space(num);//num : �����ϴµ� �ʿ��� cluster����

				if(r == -1){
					printf("������ ������ cluster ������ �����մϴ�.\n");
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

	for(i = 0; i < parameter.root_sector_count; i++)//��Sector �� �о��
	{
		SD_Read_Sector(parameter.root_start + i, 1, (U8 *)buf);

		for(j = 0; j < (parameter.byte_per_sector / 32); j++)//32BYTE�� entry�� �ٽ� �ɰ�
		{
			/* Name[0]�� 0x0�̸� �μ� ����, 0x05, 0xE5�̸� �������� Skip */
			if((((ENTRY *)buf)+j)->fileName[0] == 0x0) goto HERE;
			if((((ENTRY *)buf)+j)->fileName[0] == 0x05 || (((ENTRY *)buf)+j)->fileName[0] == 0xE5) continue;
			/* ���� �Ӽ��� 0x3F �Ǵ� 0x0F long file name �̹Ƿ� Skip */
			if((((ENTRY *)buf)+j)->attribute.c == 0x3F || (((ENTRY *)buf)+j)->attribute.l.ln == 0x0F) continue;

			/* Entry ���� �μ� */
			printFileInfo(((ENTRY *)buf)+j, num++);
			/* �μ�Ǵ� ���� �Ǵ� ���� ���� �� �տ� 1������ 1�� �����ϸ� ��ȣ�� �μ��Ѵ� */
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
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����

	//����clustorNum
	unsigned short clusterNum = entry->firstClusterLow;

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	printf("[NUM] [NAME .EXT] [AT] [  DATE  ] [TIME] [CLUST] [ SIZE ]\n");
	printf("=============================================================\n");

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32���� entry�� �ٽ� �ɰ�
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//�� �ڷ� ����X
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				printFileInfo(((ENTRY *)sector)+j, num++);
			}
			if((i+1)%2 == 0){
				printf("any key continue\n");
				Uart_Get_Char();
			}
		}
		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
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
		if(name[i] == '\0' && filename[i] == ' ') break;//����
		if(name[i] != filename[i]) return 0;
	}
	for(i=0;i<3;i++){
		if(etc[i] == filename[i+8]) continue;
		if(etc[i] == '\0' && filename[i+8] == ' ') break;//����
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
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����

	//����clustorNum
	unsigned short clusterNum = curDir->firstClusterLow;

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32���� entry�� �ٽ� �ɰ�
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//�� �ڷ� ����X
				if((((ENTRY *)sector)+j)->fileName[0] == 0x05 || (((ENTRY *)sector)+j)->fileName[0] == 0xE5) continue;
				if((((ENTRY *)sector)+j)->attribute.c == 0x3F || (((ENTRY *)sector)+j)->attribute.l.ln == 0x0F) continue;

				r = compare_filename(name,etc,(char *)(((ENTRY *)sector)+j)->fileName);
				if(r == 1) return ((ENTRY *)sector)+j;
			}
		}
		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
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
			if((((ENTRY *)buf)+j)->attribute.file.d == 0) continue;//directory�� �ƴ� ���

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
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����
	ENTRY * returnDir;
	unsigned short clusterNum = curDir->firstClusterLow;//����clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);

		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)buf);
			for(j = 0; j < (sectorSize / 32); j++)//32���� entry�� �ٽ� �ɰ�
			{
				if(j == 0) returnDir = ((ENTRY *)buf)+j;//���� directory����

				if((((ENTRY *)buf)+j)->fileName[0] == 0x0) goto HERE;//�� �ڷ� ����X
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
	printf("���丮�� ã�� ���߽��ϴ�.\n");
	return returnDir;
}

static void change_filename(U32 file_wp, ENTRY * file_pointer, char * new_name, char * new_etc){
	int k;

	for(k=0;k<8;k++){//�����̸�
		if(new_name[k] == '\0') break;
		file_pointer->fileName[k] = new_name[k];
	}
	for(;k<8;k++) file_pointer->fileName[k] = ' ';
	for(k=0;k<3;k++){//Ȯ����
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
	//���ϸ� �ߺ�
	if(isDouble){
		if(target_file_wp == -1) return -1;//�ߺ� & ���� ��ã��
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
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����
	unsigned short clusterNum = curDir->firstClusterLow;//����clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32���� entry�� �ٽ� �ɰ�
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) goto HERE;//�� �ڷ� ����X
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
		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	//���ϸ� �ߺ�
	if(isDouble){
		if(target_file_wp == -1) return -1;//�ߺ� & ���� ��ã��
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
			//��Ŭ������ Ž��
			clusterList[num++] = (i * (parameter.byte_per_sector/2)) + j;//cluster��ȣ ����
			if(num == clusterCnt) return 1;
		}
	}
	return -1;
}


static void edit_entry_and_copy_file(char * name, char * etc, int clusterCnt, ENTRY * origin_file){
	int i,j,fat0_index,origin_jump, copy_jump,cnt,size;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = sectorSize/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����
	unsigned short origin_cluster, copy_cluster;
	unsigned int data_buf[512/4];

	//entry ����
	origin_cluster = origin_file->firstClusterLow;

	SD_Read_Sector(empty_file_wp, 1, (U8 *)sector);
	*empty_file_pointer = *origin_file;
	empty_file_pointer->firstClusterLow = clusterList[0];
	change_filename(empty_file_wp, empty_file_pointer, name, etc);

	SD_Write_Sector(empty_file_wp, 1, (U8 *)sector);

	//���� ����
	size = origin_file->fileSize;
	fat0_index = origin_cluster/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);

	for(i=0;i<clusterCnt;i++){
		copy_cluster = clusterList[i];
		origin_jump = parameter.sector_per_cluster * (origin_cluster-2);
		copy_jump = parameter.sector_per_cluster * (copy_cluster-2);

		origin_cluster = ((short *)fat0_buf)[origin_cluster%addressCnt];//���� clustorNum
		if(origin_cluster/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
			fat0_index = origin_cluster/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		if(origin_cluster == 0xFFFF) {//������������->�ȿ��� �а� return
			cnt = size/sectorSize + 1;
			for(j=0;j<cnt;j++){
				SD_Read_Sector(parameter.file_start + origin_jump + j,1,(U8 *)data_buf);
				SD_Write_Sector(parameter.file_start + copy_jump + j,1,(U8 *)data_buf);
				size -= sectorSize;
			}
		}else{
			//32�� sector ��� �Ű� ��
			for(j=0;j<parameter.sector_per_cluster;j++){//32sector ���� �о�� ��
				SD_Read_Sector(parameter.file_start + origin_jump + j,1,(U8 *)data_buf);
				SD_Write_Sector(parameter.file_start + copy_jump + j,1,(U8 *)data_buf);
				size -= sectorSize;
			}
		}
	}

	//fat0���� ����
	for(i=0;i<clusterCnt-1;i++){
		copy_cluster = clusterList[i];
		if(copy_cluster/addressCnt != fat0_index){
			fat0_index = copy_cluster/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
		((short *)fat0_buf)[copy_cluster%addressCnt] = clusterList[i+1];
		SD_Write_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
	}
	//������ CLUSTER 0XFFFF
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

	if(target_file_wp != -1){//���� ã��
		if(isDouble) return -2;//�ߺ�
		if(!empty_file_wp) return -3;//entry �����X
		return 1;
	}
	return -1;//���� ��ã��
}

static int copy_file_in_subDir(char * old, char * new_name, char * new_etc, ENTRY * curDir)
{

	int i, j, r, isDouble=0;
	char *old_name = strtok(old,".");
	char *old_etc = strtok(NULL,"\n");

	int fat0_index,jumpSize;
	int sectorSize=parameter.byte_per_sector;//512
	unsigned int addressCnt = parameter.byte_per_sector/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����
	unsigned short clusterNum = curDir->firstClusterLow;//����clustorNum

	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		for(i = 0 ; i < parameter.sector_per_cluster ; i++){
			SD_Read_Sector(parameter.file_start + jumpSize + i, 1, (U8 *)sector);
			for(j = 0; j < (sectorSize / 32); j++)//32���� entry�� �ٽ� �ɰ�
			{
				if((((ENTRY *)sector)+j)->fileName[0] == 0x0) {
					empty_file_pointer = ((ENTRY *)sector)+j;
					empty_file_wp = parameter.file_start + jumpSize + i;
					goto HERE;//�� �ڷ� ����X
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
		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}
	}
	HERE:
	//���ϸ� �ߺ�
	if(target_file_wp != -1){//���� ã��
		if(isDouble) return -2;//�ߺ�
		return 1;
	}
	return -1;//���� ��ã��
}
static int check_file_type(ENTRY * file)
{
	int i;
	char type[4];
	/* ���� �� : C ���� => 1, TXT ���� => 2, BMP ���� => 3, �׿� => 0 ���� */
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
	// �־��� Entry�� ���� �����͸� �о data �ּҿ� �����Ѵ�
	int i,fat0_index,jumpSize,cnt;
	int sectorSize=parameter.byte_per_sector;//512
	int size = file->fileSize;//filesize
	unsigned int addressCnt = sectorSize/2;//512B buf�ȿ� ����ִ� cluster �ּ� ����
	unsigned short fat0_buf[addressCnt];//cluster 256�� �ּ� ������ �ִ� ����
	U8 *data_buf = data;//�ӽù��� 1����Ʈ ������ access

	unsigned short clusterNum;
	//����clustorNum
	clusterNum = file->firstClusterLow;
	fat0_index = clusterNum/addressCnt;
	SD_Read_Sector(parameter.fat0_start + fat0_index, 1, (U8 *)fat0_buf);

	for(;;){
		jumpSize = parameter.sector_per_cluster * (clusterNum-2);
		//���� clusterNum���� �Ѿ
		clusterNum = ((short *)fat0_buf)[clusterNum%addressCnt];//���� clustorNum
		if(clusterNum/addressCnt != fat0_index){//fat0���� ���ο� 256�� �ּ� ���� �����;���
			fat0_index = clusterNum/addressCnt;
			SD_Read_Sector(parameter.fat0_start + fat0_index,1,(U8 *)fat0_buf);
		}

		if(clusterNum == 0xFFFF) {//������������->�ȿ��� �а� return
			cnt = size/sectorSize + 1;
			for(i=0;i<cnt;i++){
				SD_Read_Sector(parameter.file_start + jumpSize + i,1,data_buf);
				data_buf += sectorSize;
				size -= sectorSize;
			}
			return;
		}else{
			//32�� sector ��� *data�� �Ű� ��
			for(i=0;i<parameter.sector_per_cluster;i++){//32sector ���� �о�� ��
				SD_Read_Sector(parameter.file_start + jumpSize + i,1,data_buf);
				data_buf += sectorSize;

				size -= sectorSize;
			}

		}
	}
}
#endif

