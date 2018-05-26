#include "defines.h"
#include "serial.h"
#include "lib.h"


int g_data = 0x10;
int g_bss;

static int s_data = 0x20;
static int s_bss;

static int init(void){
	extern int erodata,data_start,edata,bss_start,ebss;

	memcpy(&data_start,&erodata,(long)&edata - (long)&data_start);
	memset(&bss_start,0,(long)&ebss - (long)&bss_start);

	serial_init(SERIAL_DEFAULT_DEVICE);

	return 0;
}

static void printval(void){
	puts("g_data = ");	putxval(g_data,0);	puts("\n");
	puts("g_bss = ");		putxval(g_bss,0);		puts("\n");
	puts("s_data = ");	putxval(s_data,0);	puts("\n");
	puts("s_bss = ");		putxval(s_bss,0);		puts("\n");

}

int main(void){

	init();

	puts("Hello World!\n");
	
	//putxval(0x10,0);		puts("\n");
	//putxval(0xffff,0);	puts("\n");

	printval();
	puts("Over Write vals\n");
	g_data = 0x20;
	g_bss = 0x30;
	s_data = 0x40;
	s_bss = 0x50;

	printval();

	while(1)
		;
	return 0;

}
