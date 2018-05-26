#include "defines.h"
#include "intr.h"
#include "interrupt.h"
#include "serial.h"
#include "lib.h"


static void intr(softvec_type_t type,unsigned long sp){
	int c;
	static char buf[32];
	static int len;

	c = getc();

	if (len < 32)
	{
		if (c != '\n')
		{
			buf[len++] = c;
		}
		else
		{
			buf[len++] = '\0';
			if (!strncmp(buf, "echo", 4))
			{
				puts(buf + 4);
				puts("\n");
			}
			else
			{
				/* Do nothing */
			}
			puts("> ");
			len = 0;
		}
	}
	else{
		puts("Over buf. reset buf");
		len=0;
		memset(buf,0,sizeof(char)*32);
	}
}


static void wait(){
	volatile long i;
	for(i=0;i<300000;i++)
		;

}


int main(void){

	INTR_DISABLE;

	puts("hello world from os.\n");
	
	softvec_setintr(SOFTVEC_TYPE_SERINTR,intr);
	serial_intr_recv_enable(SERIAL_DEFAULT_DEVICE);

	puts("> ");

	INTR_ENABLE;

	while(1){
		asm volatile ("sleep");
	}

	
	
	return 0;

}
