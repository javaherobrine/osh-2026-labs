#ifndef RESPONSE_HPP_INCLUDED
#define RESPONSE_HPP_INCLUDED
#include<unordered_map>
#include<set>
#include<string>
#include<ctime>
#include<sys/stat.h>
#include<sys/socket.h>
#include<sys/sendfile.h>
#include<fcntl.h>
#include<netinet/tcp.h>
#include<netinet/in.h>
#include<unistd.h>
#include<list>
#include<cstdio>
#include<sstream>
#include"state_machine.hpp"
struct http_file_cache{
	char* data=nullptr;
	std::string filename;
	std::time_t last_modification=0;
	~http_file_cache();
	bool on_modify(const struct stat& status);
};
extern const std::string NOT_FOUND_HTML;
extern const std::string INTERNAL_ERROR_HTML;
extern const std::size_t NOT_FOUND_SIZE;
extern const std::size_t INTERNAL_ERROR_SIZE;
void http_response_header(int code, std::size_t length, std::string message, http_state_machine& machine);
template<std::size_t max_cached, std::size_t max_file_size>
struct http_response_generator{
	void response_file(std::string filename, http_state_machine& machine, int code, std::string message){
		struct stat status;
		if(stat(filename.c_str(),&status)){
			evict(filename);//The file is deleted but maybe it lives in the cache
			http_response_header(404,NOT_FOUND_SIZE,"Not Found", machine);
			machine.data.send.next_state=http_state::SEND_STRING;
			machine.data.send.next_content.ptr=NOT_FOUND_HTML.c_str();
			machine.data.send.next_length=NOT_FOUND_SIZE;
			return;
		}
		if(S_ISDIR(status.st_mode)){
			http_response_header(500,INTERNAL_ERROR_SIZE,"Internal Server Error",machine);
			machine.data.send.next_state=http_state::SEND_STRING;
			machine.data.send.next_content.ptr=INTERNAL_ERROR_HTML.c_str();
			machine.data.send.next_length=INTERNAL_ERROR_SIZE;
			return;
		}
		http_response_header(200,status.st_size,"OK",machine);
		if(status.st_size<=max_file_size){
			http_file_cache* cached_file=cache(filename,status);
			if(cached_file==nullptr){
				http_sendfile(filename,machine,status);
			}else{
				machine.data.send.next_state=http_state::SEND_STRING;
				machine.data.send.next_content.ptr=cached_file->data;
				machine.data.send.next_length=status.st_size;
			}
		}else{
			http_sendfile(filename,machine,status);
		}
	}
private:
	http_file_cache* cache(const std::string& filename, const struct stat& status){
		std::unordered_map<std::string,std::list<http_file_cache*>::iterator>::iterator iter=mapped.find(filename);
		if(iter!=mapped.end()){
			cached.splice(cached.begin(),cached,iter->second);
			if(status.st_mtime!=(*(iter->second))->last_modification){
				if(!(*(iter->second))->on_modify(status)){//Fail to update, evict it
					evict(filename);
					return nullptr;
				}
			}
			return *(iter->second);
		}
		http_file_cache* c=new http_file_cache;
		c->filename=filename;
		if(!(c->on_modify(status))){
			delete c;
			return nullptr;
		}
		cached.push_front(c);
		mapped[filename]=cached.begin();
		while(cached.size()>max_cached){//evict old caches
			evict(cached.back()->filename);
		}
		return c;
	}
	std::list<http_file_cache*> cached;
	std::unordered_map<std::string,std::list<http_file_cache*>::iterator> mapped;
	void evict(const std::string& key){
		std::unordered_map<std::string,std::list<http_file_cache*>::iterator>::iterator to_be_evicted=mapped.find(key);
		if(to_be_evicted==mapped.end()){//Not a valid cache
			return;
		}
		delete *(to_be_evicted->second);
		cached.erase(to_be_evicted->second);
		mapped.erase(to_be_evicted);
	}
	void http_sendfile(const std::string& filename, http_state_machine& machine, const struct stat& status){
		evict(filename);//Maybe the file is growing very large and shoule be removed from cache
		int fd=open(filename.c_str(),O_RDONLY);
		machine.data.send.next_length=status.st_size;
		machine.data.send.next_content.fd=fd;
		machine.data.send.next_state=http_state::SEND_FILE;
	}
};
#endif
