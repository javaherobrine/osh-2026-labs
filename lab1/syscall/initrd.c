#include<stdio.h>
int sayHello(char* ptr,int len){//sorry, but Java_Herobrine loves Assembly!
	long result;
	asm("mov $548, %%rax\n\t"
		"syscall\n\t"
		"mov %%rax, %0":"=b"(result):"D"(ptr), "S"(len):"rax");
	return result;
}
int main(){
	printf("Hello! PB25020499\n");
	char* c;
	int res=sayHello(c,0);
	printf("result=%d\n",res);
	char ch[100];
	res=sayHello(ch,100);
	printf("result=%d\n%s",res,ch);
	while(1);
}
