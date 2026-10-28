#include"response.hpp"
#include<exception>
#include<sys/types.h>
const std::string NOT_FOUND_HTML=R"???(
<!DOCTYPE html>
<html>
        <head>
                <meta charset="utf-8" />
                <title>404 Not Found</title>
        </head>
        <body>
                <h1 align="center" style="color:rgb(255,0,0);">java.io.FileNotFoundException: No such file or directory</h1>
                <hr />
                <button onclick="window.history.back()">Return to previous page</button>
        </body>
</html>
)???";
const std::string INTERNAL_ERROR_HTML=R"???(
<!DOCTYPE html>
<html>
	<head>
		<meta charset="utf-8" />
		<title>500 Internal Server Error</title>
	</head>
	<body>
		<h1 align="center" style="color:rgb(255,0,0);">java.lang.Error: Unknown Internal Server Error</h1>
		<hr />
		<button onclick="window.history.back()">Return to previous page</button>
	</body>
</html>
)???";
const std::size_t NOT_FOUND_SIZE=NOT_FOUND_HTML.size();
const std::size_t INTERNAL_ERROR_SIZE=INTERNAL_ERROR_HTML.size();
void http_write(int fd, const char* ptr, size_t length){
	size_t index=0;
	while(index!=length){
		ssize_t ret=write(fd,ptr+index,length-index);
		if(ret<0){
			return;
		}
		index+=ret;
	}
}
http_file_cache::~http_file_cache(){
	delete[] data;
}
bool http_file_cache::on_modify(const struct stat& status){
	char* ptr;
	try{
		ptr=new char[status.st_size];
	}catch(std::bad_alloc bad){
		return false;
	}
	std::FILE* f=std::fopen(filename.c_str(),"rb");
	if(f==nullptr){
		delete[] ptr;
		return false;
	}
	std::size_t s=0;
	while(s!=status.st_size){
		ssize_t ss=std::fread(ptr+s,1,status.st_size-s,f);
		if(ss<0){
			delete[] ptr;
			std::fclose(f);
			return false;
		}else{
			s+=ss;
		}
	}
	delete[] data;
	data=ptr;
	last_modification=status.st_mtime;
	return true;
}
void http_response_header(int code, std::size_t length, std::string message, int sockfd){
	std::stringstream header_stream;
	header_stream<<"HTTP/1.0 "<<code<<" "<<message<<"\r\nContent-Length: "<<length<<"\r\n\r\n";
	std::string header=header_stream.str();
	http_write(sockfd,header.c_str(),header.size());
}
