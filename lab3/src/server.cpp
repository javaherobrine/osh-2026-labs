#include<signal.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<unistd.h>
#include"thread_pool.hpp"
#include<cstring>
//determine the processors when compiling
#ifndef PROCESSOR_COUNT
#define PROCESSOR_COUNT 1
#endif
//bind at 0.0.0.0:8000 as required
constexpr int SERVER_PORT=8000;
constexpr int IP=0;
constexpr int BACKLOG=20;
httpserver_fixed_threadpool<(PROCESSOR_COUNT<<1)+2> pool;//2N+2 Principle
int serverfd;
void on_terminate(){
	if(serverfd>0){
		close(serverfd);
	}
	serverfd=-1;
	pool.shutdown_now();
	pool.join();
}
void http_sigint(int){
	close(serverfd);
	serverfd=-1;
	pool.shutdown_now();
}
int main(){
	struct sigaction sig;
	sig.sa_handler=http_sigint;
	sig.sa_flags=0;
	sigemptyset(&sig.sa_mask);
	sigaction(SIGINT,&sig,nullptr);
	sig.sa_handler=SIG_IGN;
	sigaction(SIGPIPE,&sig,nullptr);
	serverfd=socket(AF_INET,SOCK_STREAM,0);
	if(serverfd<0){
		on_terminate();
		return 1;
	}
	struct sockaddr_in local_addr;
	std::size_t s=sizeof(local_addr);
	std::memset(&local_addr,0,s);
	local_addr.sin_family=AF_INET;
	local_addr.sin_addr.s_addr=IP;
	local_addr.sin_port=htons(SERVER_PORT);
	if(bind(serverfd,reinterpret_cast<struct sockaddr*>(&local_addr),s)){
		on_terminate();
		return 2;
	}
	if(listen(serverfd,BACKLOG)){
		on_terminate();
		return 3;
	}
	while(pool){
		int sockfd=accept(serverfd,nullptr,nullptr);
		pool.push(sockfd);
	}
	pool.join();
	return 0;
}
