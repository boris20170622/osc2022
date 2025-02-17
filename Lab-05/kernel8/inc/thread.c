#include "thread.h"
#include "uart.h"
#include "allocator.h"
#include "cpio.h"

#define TASKSIZE 4096
#define TASKEXIT 1
#define TASKFORK 2

typedef struct _Task{
	unsigned long context[12+1+2+1+31];//kreg+ksp & spsr+elr & usp+ureg
	int id;
	int status;
	unsigned long a_addr,a_size,child;
	struct _Task* next;
	/*
	task stack:this ~ this+TASKSIZE
	*/
}Task;

typedef struct{
	Task *beg,*end;
}RQ;

static RQ rq;
static int task_cnter;

void threadSwitch(){
	asm volatile("\
_threadSwitch:\n\
		stp x19, x20, [x0, 16 * 0]\n\
		stp x21, x22, [x0, 16 * 1]\n\
		stp x23, x24, [x0, 16 * 2]\n\
		stp x25, x26, [x0, 16 * 3]\n\
		stp x27, x28, [x0, 16 * 4]\n\
		stp x29, x30, [x0, 16 * 5]\n\
		mov x9, sp\n\
		str x9, [x0, 16 * 6]\n\
		\n\
		ldp x19, x20, [x1, 16 * 0]\n\
		ldp x21, x22, [x1, 16 * 1]\n\
		ldp x23, x24, [x1, 16 * 2]\n\
		ldp x25, x26, [x1, 16 * 3]\n\
		ldp x27, x28, [x1, 16 * 4]\n\
		ldp x29, x30, [x1, 16 * 5]\n\
		ldr x9, [x1, 16 * 6]\n\
		mov sp, x9\n\
		\n\
		msr tpidr_el1, x1\n\
		\n\
		ret\n\
	"::);
}

void threadSchedule(){
	if(!rq.beg){
		uart_printf("rq is empty!!\n");
		while(1){}
	}
	
	if(rq.beg==rq.end){
		//no other task, all done
	}else{
		do{
			rq.end->next=rq.beg;
			rq.end=rq.beg;
			rq.beg=rq.beg->next;
			rq.end->next=0;
		}while(rq.beg->status);//ignore abnormal task

		//uart_printf("%d %x %x %x\n",rq.beg->id,rq.beg->context[10],rq.beg->context[11],rq.beg->context[12]);
		//uart_printf("%d -> %d\n",rq.end->id,rq.beg->id);
		asm volatile("\
			mov x1, %0\n\
			mrs x0, tpidr_el1\n\
			bl _threadSwitch\n\
		"::"r"(rq.beg));//only use bl to avoid stack usage
	}
}

Task* thread_create(void* func){
	Task* new_task=(Task*)falloc(TASKSIZE);
	if((unsigned long)new_task%TASKSIZE){//aligned
		uart_printf("new_task isn't aligned!!\n");
		while(1){}
	}
	
	new_task->context[10]=(unsigned long)new_task+TASKSIZE;//fp
	new_task->context[11]=(unsigned long)func;//lr
	new_task->context[12]=(unsigned long)new_task+TASKSIZE;//sp
	new_task->id=task_cnter++;
	new_task->status=0;
	new_task->a_addr=new_task->a_size=new_task->child=0;
	new_task->next=0;

	if(rq.beg){
		rq.end->next=new_task;
		rq.end=rq.end->next;
	}else{
		rq.beg=rq.end=new_task;
	}

	return new_task;
}

/*--------------------------------------------*/

void zombiesKill(){//called by idle()
	Task* tar=rq.beg;
	while(1){
		while(tar->next&&(tar->next->status&TASKEXIT)){
			Task* tmp=tar->next->next;
			ffree((unsigned long)(tar->next));
			tar->next=tmp;
		}

		if(!tar->next){
			rq.end=tar;
			break;
		}else{
			tar=tar->next;
		}
	}
}

void taskUpdate(Task* p,Task* c){
	p->status^=TASKFORK;
	p->child=c->id;

	Task* tmp=c->next;
	char* src=(char*)p;
	char* dst=(char*)c;
	for(int i=0;i<TASKSIZE;++i){//task copy
		*dst=*src;
		dst++;
		src++;
	}

	c->id=p->child;
//	uart_puts("Please enter app load address (Hex): ");
	c->a_addr= (long unsigned int)falloc(TASKSIZE*1024);
	//c->a_addr=uart_get_hex(1);
	c->child=0;
	c->next=tmp;

	long k_delta=(long)c-(long)p;
	long a_delta=(long)c->a_addr-(long)p->a_addr;
	c->context[10]+=k_delta;//kernel fp
	c->context[12]+=k_delta;//kernel sp
	c->context[14]+=a_delta;//elr_el1
	c->context[15]+=a_delta;//sp_el0
	c->context[45]+=a_delta;//user fp
	c->context[46]+=a_delta;//user lr

	src=(char*)(p->context[15]);
	dst=(char*)(c->context[15]);
	for(int i=0,ii=p->a_addr+p->a_size-(p->context[15]);i<ii;++i){//program copy
		*dst=*src;
		dst++;
		src++;
	}
}

void doFork(){//called by idle()
	Task* tar=rq.beg->next;
	while(tar){
		if((tar->status)&TASKFORK){
			Task* child=thread_create(0);
			taskUpdate(tar,child);
		}
		tar=tar->next;
	}
}

void idle(){
	while(1){
		//uart_printf("idle()\n");
		//uart_getc();
		zombiesKill();
		doFork();
		threadSchedule();
	}
}
void delay(int period) {
    while (period--);
}

/*--------------------------------------------*/

int tidGet(){
	Task* cur;
	asm volatile("mrs %0, tpidr_el1\n":"=r"(cur):);
	return cur->id;
}

void exec(char* path,char** argv){//will not reset sp...
	unsigned long a_addr;
//	uart_puts("Please enter app load address (Hex): ");
	a_addr = (long unsigned int)falloc(TASKSIZE*1024);
	//a_addr=uart_get_hex(1);
	loadApp_with_argv(path,a_addr,argv,&(rq.beg->a_addr),&(rq.beg->a_size));
	exit();
}

void exit(){
	Task* cur;
	asm volatile("mrs %0, tpidr_el1\n":"=r"(cur):);
	cur->status|=TASKEXIT;
	threadSchedule();

	while(1){
		uart_printf("exit() failed!!\n");
	}
}

int fork(){
	rq.beg->status|=TASKFORK;
	threadSchedule();
	return rq.beg->child;
}

void kill(int pid){
	Task* tar=rq.beg;
	while(tar->next){
		if(tar->next->id==pid){
			Task* tmp=tar->next->next;
			ffree((unsigned long)(tar->next));
			tar->next=tmp;
			return;
		}
	}
}

/*--------------------------------------------*/


void foo1(){
	for(int i=0;i<10;++i){
		uart_printf("Thread id: %d %d\n",tidGet(),i);
		//uart_getc();
		delay(1000000);
		threadSchedule();
	}

	exit();
}

void threadTest1(){
	Task* cur=thread_create(0);//use startup stack (not kernel stack)
	asm volatile("msr tpidr_el1, %0\n"::"r"((unsigned long)cur));

	for(int i=0;i<3;++i){
		thread_create(foo1);
	}

	idle();
}

void foo2(){
	char* argv[]={"argv_test","-o","arg2",0};

	exec("app1",argv);
}

void threadTest2(){
	Task* cur=thread_create(0);//use startup stack (not kernel stack)
	asm volatile("msr tpidr_el1, %0\n"::"r"((unsigned long)cur));//TODO

	thread_create(foo2);

	idle();
}

void foo3(){
	char* argv[]={};

	exec("syscall.img",argv);
}

void threadTest3(){
	Task* cur=thread_create(0);//use startup stack (not kernel stack)

	thread_create(foo3);

	asm volatile("msr tpidr_el1, %0\n"::"r"((unsigned long)cur));//TODO
	unsigned long tmp;
	asm volatile("mrs %0, cntkctl_el1" : "=r"(tmp));
	tmp |= 1;
	asm volatile("msr cntkctl_el1, %0" : : "r"(tmp));
	unsigned long cntfrq_el0;
	asm volatile("mrs %0, cntfrq_el0" : "=r"(cntfrq_el0));
	asm volatile("msr cntp_tval_el0, %0" : : "r"(1));
	idle();
}


