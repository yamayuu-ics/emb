#include "defines.h"
#include "serial.h"
#include "lib.h"



static void wait(){
	volatile long i;
	for(i=0;i<300000;i++)
		;

}


int main(void){

	static char buf[32];

	puts("hello world from os.\n");
	
	while(1){
		puts("kzos > ");
		gets(buf);
		//puts("buf : ");
		//puts(buf);

		if(!strncmp(buf,"echo",4)){
			puts(buf+4);
			puts("\n");
		}
		else if(!strcmp(buf,"exit")){
			break;
		}
		else{
			;
		}
	}
	
	return 0;

}
