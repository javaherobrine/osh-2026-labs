#include<unistd.h>
#include<sys/epoll.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<signal.h>
#include<netinet/tcp.h>
#include<cstring>
#include<array>
#include"state_machine.hpp"
#include"epoll_helper.hpp"
#ifndef MAX_FD_COUNT
#error "You MUST configure the maximum file descripor count in compile time via -DMAX_FD_COUNT=`value` for my optimization. If confused, you can use `ulimit -n` and use its output."
#endif
//bind at 0.0.0.0:8000 as required
constexpr int SERVER_PORT=8000;
constexpr int IP=0;
constexpr int BACKLOG=SOMAXCONN;
std::array<http_state_machine,MAX_FD_COUNT> connections;
std::array<epoll_event,MAX_FD_COUNT> events;
int serverfd,epollfd;
inline void on_terminate(){
	if(serverfd>0){
		close(serverfd);
	}
	serverfd=-1;
}
static void http_sigint(int){
	close(serverfd);
	close(epollfd);
}
inline static void http_non_block(int fd){
	int flag=fcntl(fd,F_GETFL,0);
	fcntl(fd,F_SETFL,flag|O_NONBLOCK);
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
	epollfd=epoll_create(1);
	if(epollfd<0){
		close(epollfd);
		on_terminate();
		return 4;
	}
	http_non_block(serverfd);
	struct epoll_event event;
	event.events=EPOLLIN|EPOLLET;
	event.data.fd=serverfd;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,serverfd,&event);
	while(true){
		int count=epoll_wait(epollfd,events.data(),MAX_FD_COUNT,-1);
		if(count<0){
			break;
		}
		for(int i=0;i<count;++i){
			if(events[i].data.fd==serverfd){
				while(true){
					int sockfd=accept(serverfd,nullptr,nullptr);
					if(sockfd<0){
						if(is_eagain(errno)){
							break;
						}
						goto cleanup;
					}
					http_non_block(sockfd);
					bool option=true;
					setsockopt(sockfd,IPPROTO_TCP,TCP_NODELAY,&option,sizeof(option));
					setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
					connections[sockfd].reset_statemachine();
					connections[sockfd].sockfd=sockfd;
					event.events=EPOLLIN|EPOLLET;
					event.data.fd=sockfd;
					epoll_ctl(epollfd,EPOLL_CTL_ADD,sockfd,&event);
				}
			}else{
				connections[events[i].data.fd].progress(epollfd);
			}
		}
	}
	cleanup:close(serverfd);
	close(epollfd);
	return 0;
}
