#include"response.hpp"
#include<cstring>
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
void http_response_header(int code, std::size_t length, std::string message, http_state_machine& machine){
	std::stringstream header_stream;
	header_stream<<"HTTP/1.0 "<<code<<" "<<message<<"\r\nContent-Length: "<<length<<"\r\n\r\n";
	std::string header=header_stream.str();
	machine.data.send.length=header.size();
	char* ptr=new char[machine.data.send.length];
	machine.data.send.content.ptr=ptr;
	std::memcpy(ptr,header.c_str(),machine.data.send.length);
}
