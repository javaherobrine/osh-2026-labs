#ifndef STATE_MACHINE_INCLUDED
#define STATE_MACHINE_INCLUDED
#include<sys/types.h>
#include"request.hpp"
enum struct http_state{//response header is sent
	PARSE_HEADER,PROCESS_BODY,SEND_STRING,SEND_FILE
};
//http_process_body must be implemented in a specific way
struct http_state_machine{
	int sockfd;
	http_state state;
	http_request_header header;
	std::string since_last="";
	union{
		struct{
			char read_chunk[4096];
		}recv;
		struct {
			union http_state_machine_internal{
				int fd;
				const char* ptr;
			} content;
			http_state_machine_internal next_content;
			http_state next_state;
			std::size_t length;
			std::size_t next_length;
		}send;
	} data;
	std::size_t offset=0;
	void progress(int epoll_fd);
	void reset_statemachine();
};
#endif
