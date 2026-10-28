#include"request.hpp"
#include"response.hpp"
#include"thread_pool.hpp"
#include<cstdio>
#include<cerrno>
#include<cstring>
#include<cctype>
#include<string>
#include<filesystem>
#include<unistd.h>
const std::string WD=std::filesystem::current_path().string();
http_response_with_cache<10,1<<20> cache;//10 files, 1MB
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
std::string httpserver_request_readline(std::FILE* tcpin, char buffer[4096]){//at least a carriage return will be returned if no error
	std::string str;
	char* ptr;
	while(true){
		ptr=fgets(buffer,4096,tcpin);
		if(ptr==nullptr){
			if(errno==EINTR){
				continue;
			}
			return "";//Network Error
		}
		str+=buffer;
		if(str.size()&&str.back()=='\n'){
			return str;
		}
	}
}
void http_500(int sockfd){
	bool option=true;
	setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
	http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",sockfd);
	http_write(sockfd,INTERNAL_ERROR_HTML.c_str(),INTERNAL_ERROR_SIZE);
	option=false;
	setsockopt(sockfd,IPPROTO_TCP,TCP_CORK,&option,sizeof(option));
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
void httpserver_request_handler(int sockfd){
	char http_chunk[4096];//Exactly one page
	std::memset(http_chunk,0,4096);
	std::FILE* in=fdopen(sockfd,"rb");
	std::string result;
	int code;
	std::string message;
	http_request_header header;
	result=httpserver_request_readline(in,http_chunk);
	if(result.empty()){
		fclose(in);
		return;
	}
	//request line
	std::size_t space0=result.find_first_of(' '),space1=result.find_last_of(' ');
	if(space0==space1||space0!=3||result[0]!='G'){//Not a valid line or method is not GET
		http_500(sockfd);
		fclose(in);
		return;
	}
	header.version=http_version::http_1_0;//Ignore other version in this case
	header.method=http_request_method::GET;
	std::size_t question=result.find_first_of('?', space0);
	if(question!=std::string::npos){
		space1=std::min(space1,question);
	}
	header.resource=result.substr(space0,space1-space0);//query attached by the resources are discarded in this demo
	header.resource[0]='.';
	header.resource=http_fxxking_percentage_encoding(header.resource);
	try{
		header.resource=std::filesystem::weakly_canonical(std::filesystem::path(header.resource)).string();
	}catch(std::filesystem::filesystem_error e){//path "underflow"
		http_500(sockfd);
		fclose(in);
		return;
	}
	if(!header.resource.starts_with(WD)){//Illegal access
		http_500(sockfd);
		fclose(in);
		return;
	}
	//request header
	while(true){
		result=httpserver_request_readline(in,http_chunk);
		if(result=="\r\n"){
			break;
		}
		size_t colon=result.find_first_of(':');
		if(colon==std::string::npos){//No :
			http_500(sockfd);
			fclose(in);
			return;
		}
		std::string key=result.substr(0,colon);
		for(auto&& i:key){
			i=std::tolower(i);
		}
		colon=result.find_first_not_of(' ',colon+1);
		if(colon==std::string::npos){//A: 
			http_500(sockfd);
			fclose(in);
			return;
		}
		header.headers.insert({key,result.substr(colon)});		
	}
	//send response
	cache.response_file(header.resource,sockfd,200,"OK");
	fclose(in);
}
