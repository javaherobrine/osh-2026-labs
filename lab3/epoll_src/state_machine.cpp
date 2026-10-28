#include"state_machine.hpp"
#include"epoll_helper.hpp"
#include"response.hpp"
#include<cstring>
#include<cerrno>
#include<filesystem>
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/sendfile.h>
#include<fcntl.h>
#include<sys/sendfile.h>
#include<sys/epoll.h>
#include<netinet/tcp.h>
#include<netinet/in.h>
#include<unistd.h>
#include<iostream>
http_response_generator<10,1<<20> generator;
const std::string WD=std::filesystem::current_path().string();
void close_gracefully(http_state_machine& machine,int epoll_fd){
	bool option=false;
	setsockopt(machine.sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
	close(machine.sockfd);
	if(machine.state==http_state::SEND_FILE){
		close(machine.data.send.content.fd);
	}
	if(machine.data.send.next_state==http_state::SEND_FILE){
		close(machine.data.send.next_content.fd);
	}
	epoll_ctl(epoll_fd,EPOLL_CTL_DEL,machine.sockfd,nullptr);
}
void http_state_machine::reset_statemachine(){
	header={};
	memset(&data,0,sizeof(data));
	state=http_state::PARSE_HEADER;
	since_last="";
}
int http_hex_number(char c){
	if(isdigit(c)){
		return c-'0';
	}
	if(c>='A'&&c<='F'){
		return c-'A'+10;
	}
	if(c>='a'&&c<='f'){
		return c-'a'+10;
	}
	return -1;
}
std::string http_fxxking_percentage_encoding(const std::string& url){//Annoying!
	std::size_t size=url.size();
	std::string str;
	str.reserve(url.size());
	for(std::size_t i=0;i<size;){
		if(url[i]=='%'){
			if(size-i>2){
				int p=http_hex_number(url[i+1]),q=http_hex_number(url[i+2]);
				if(p!=-1&&q!=-1){
					str+=static_cast<char>((p<<4)|q);
					i+=3;
					continue;
				}
			}
		}
		str+=url[i];
		++i;
	}
	return str;
}
void http_state_machine_header(http_state_machine& machine,int epoll_fd);
void http_state_machine_body(http_state_machine& machine,int epoll_fd);
void http_state_machine_sendfile(http_state_machine& machine,int epoll_fd);
void http_state_machine_sendstring(http_state_machine& machine,int epoll_fd);
bool http_process_header(http_state_machine& machine,int epoll_fd){
	if(machine.since_last.back()!='\r'){
		return false;
	}
	if(machine.since_last=="\r"){
		machine.since_last="";
		machine.data.send.next_length=0;
		return true;
	}
	http_request_header& header=machine.header;
	if(header.resource==""){
		std::size_t space0=machine.since_last.find_first_of(' '),space1=machine.since_last.find_last_of(' ');
		if(space0==space1||space0!=3||machine.since_last[0]!='G'){//Not a valid line or method is not GET
			http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
			machine.data.send.next_state=http_state::SEND_STRING;
			machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
			machine.data.send.next_length=INTERNAL_ERROR_SIZE;
			machine.since_last="";
			return true;
		}
		header.version=http_version::http_1_0;//Ignore other version in this case
		header.method=http_request_method::GET;
		std::size_t question=machine.since_last.find_first_of('?', space0);
		if(question!=std::string::npos){
			space1=std::min(space1,question);
		}
		header.resource=machine.since_last.substr(space0,space1-space0);//query attached by the resources are discarded in this demo
		header.resource[0]='.';
		header.resource=http_fxxking_percentage_encoding(header.resource);
		try{
			header.resource=std::filesystem::weakly_canonical(std::filesystem::path(header.resource)).string();
		}catch(std::filesystem::filesystem_error e){//path "underflow"
			http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
			machine.data.send.next_state=http_state::SEND_STRING;
			machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
			machine.data.send.next_length=INTERNAL_ERROR_SIZE;
			machine.since_last="";
			return true;
		}
		if(!header.resource.starts_with(WD)){//Illegal access
			http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
			machine.data.send.next_state=http_state::SEND_STRING;
			machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
			machine.data.send.next_length=INTERNAL_ERROR_SIZE;
			machine.since_last="";
			return true;
		}
		machine.since_last="";
		return false;
	}
	std::size_t colon=machine.since_last.find_first_of(':');
	if(colon==std::string::npos){//No :
		http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
		machine.data.send.next_state=http_state::SEND_STRING;
		machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
		machine.data.send.next_length=INTERNAL_ERROR_SIZE;
		machine.since_last="";
		return true;
	}
	std::string key=machine.since_last.substr(0,colon);
	for(auto&& i:key){
		i=std::tolower(i);
	}
	colon=machine.since_last.find_first_not_of(' ',colon+1);
	if(colon==std::string::npos){//A: 
		http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
		machine.data.send.next_state=http_state::SEND_STRING;
		machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
		machine.data.send.next_length=INTERNAL_ERROR_SIZE;
		machine.since_last="";
		return true;
	}
	header.headers.insert({key,machine.since_last.substr(colon)});
	machine.since_last="";
	return false;
}
void http_state_machine_header(http_state_machine& machine,int epoll_fd){
	while(true){
		machine.offset=0;
		ssize_t recv=read(machine.sockfd,machine.data.recv.read_chunk,4096);
		if(recv<0){
			if(!is_eagain(errno)){
				close_gracefully(machine,epoll_fd);
			}
			return;
		}
		std::size_t last;
		while(true){
			last=recv;
			for(std::size_t i=machine.offset;i<recv;++i){
				if(machine.data.recv.read_chunk[i]=='\n'){
					last=i;
					break;
				}
			}
			machine.since_last.append(machine.data.recv.read_chunk+machine.offset,last-machine.offset);
			machine.offset=last+1;
			if(http_process_header(machine,epoll_fd)){
				goto cleanup;
			}
			if(last==recv){
				break;
			}
		}
	}
	cleanup:http_state_machine_body(machine,epoll_fd);
}
void http_state_machine_body(http_state_machine& machine,int epoll_fd){
	machine.offset=0;
	struct epoll_event event;
	event.data.fd=machine.sockfd;
	event.events=EPOLLOUT | EPOLLET;
	epoll_ctl(epoll_fd,EPOLL_CTL_MOD,machine.sockfd,&event);
	if(machine.data.send.next_length==0){
		generator.response_file(machine.header.resource,machine,200,"OK");
	}
	http_state_machine_sendstring(machine,epoll_fd);
}
void http_state_machine_sendfile(http_state_machine& machine,int epoll_fd){
	while(true){
		ssize_t send=sendfile(machine.sockfd,machine.data.send.content.fd,nullptr,machine.data.send.length-machine.offset);
		if(send<0){
			if(!is_eagain(errno)){
				close_gracefully(machine,epoll_fd);
			}
			break;
		}
		machine.offset+=send;
		if(machine.data.send.length==machine.offset){
			close_gracefully(machine,epoll_fd);
			return;
		}
	}
}
void http_state_machine_sendstring(http_state_machine& machine,int epoll_fd){
	do{
		ssize_t send=write(machine.sockfd,machine.data.send.content.ptr+machine.offset,machine.data.send.length-machine.offset);
		if(send<0){
			perror("write: ");
			if(!is_eagain(errno)){
				close_gracefully(machine,epoll_fd);
			}
			return;
		}
		machine.offset+=send;
	}while(machine.data.send.length!=machine.offset);
	machine.state=machine.data.send.next_state;
	machine.offset=0;
	const char* ptr=machine.data.send.content.ptr;
	machine.data.send.length=machine.data.send.next_length;
	machine.data.send.content=machine.data.send.next_content;
	machine.data.send.next_state=http_state::PARSE_HEADER;
	switch(machine.state){
	case http_state::SEND_STRING:
		delete[] ptr;
		http_state_machine_sendstring(machine,epoll_fd);
		break;
	case http_state::SEND_FILE:
		delete[] ptr;
		http_state_machine_sendfile(machine,epoll_fd);
		break;
	default:
		close_gracefully(machine,epoll_fd);
		return;
	}
}
void http_state_machine::progress(int epoll_fd){
	switch(state){
	case http_state::PARSE_HEADER:
		http_state_machine_header(*this,epoll_fd);
		return;
	case http_state::PROCESS_BODY:
		http_state_machine_body(*this,epoll_fd);
		return;
	case http_state::SEND_STRING:
		http_state_machine_sendstring(*this,epoll_fd);
		return;
	case http_state::SEND_FILE:
		http_state_machine_sendfile(*this,epoll_fd);
		return;
	default:
		return;
	}
}
