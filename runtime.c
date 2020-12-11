#include <sys/stat.h>
#include <sys/types.h>
#include <locale.h>

#define STACK_BASE		0x31800000
#define STACK_LIMIT		0x31600000

#define __HEAP_BASE__ 	0x30800000
#define __HEAP_LIMIT__ 	0x31800000

/***********************************************************/
// [5-2] : Heap ¼³Ä¡
/***********************************************************/

static caddr_t heap = (caddr_t)__HEAP_BASE__;

caddr_t _sbrk (int size)
{
	caddr_t prevHeap;
	caddr_t nextHeap;

	if (heap == NULL)
	{
		heap = (caddr_t)__HEAP_BASE__;
	}

	prevHeap = heap;
	nextHeap = (caddr_t)(((unsigned int)(heap + size) + 7) & ~7);

	if((unsigned int)nextHeap >= __HEAP_LIMIT__)
	{
		return (caddr_t)NULL;
	}

	heap = nextHeap;

	return prevHeap;}

unsigned int Get_Stack_Base(void)
{
	return STACK_BASE;
}

unsigned int Get_Stack_Limit(void)
{
	return STACK_LIMIT;
}

unsigned int Get_Heap_Base(void)
{
	return __HEAP_BASE__;
}

unsigned int Get_Heap_Limit(void)
{
	return __HEAP_LIMIT__;
}

void Run_Time_Config(void)
{
	setlocale(LC_ALL, "C");
}
