import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.PrintStream;
import java.net.InetSocketAddress;
import java.net.Socket;

public class Telnet {
    private String host;
    private int port;
    private OutputStream consoleOS;
    private InputStream consoleIS;
    
    private Socket s;
    private Pipe  toServer;
    private Pipe  fromServer;
    
    public Telnet(String host, int port, OutputStream os, InputStream is){
    	this.consoleOS = os;
    	this.consoleIS = is;  	
    	this.host = host;
    	this.port = port;
    	connect();
    }
    
    public void close(){
    	try {
			fromServer.close();
			toServer.close();
			s.shutdownOutput();
			s.close();
		} catch (Exception e) {/*ignore*/}
    }
    
    private  void print(String text){
		try {
			consoleOS.write((text+"\r\n").getBytes());
			consoleOS.flush();
		} catch (Exception e) {}
     }
    
    private void connect() {
        System.out.println("Host " + host + "; port " + port);
        try {
            s = new Socket();
            s.bind(null);
            s.setReuseAddress(true);
            s.connect(new InetSocketAddress(host, port), 2000 ); 
            
            (toServer = new Pipe(s.getInputStream(),consoleOS)).start();
            (fromServer= new Pipe(consoleIS, s.getOutputStream())).start();
        } catch (IOException e) {
        	print("#Conn to "+host+":"+port+" FAILD");
            System.out.println(e);
            return;
        }
    }
}

class Pipe extends Thread {
    BufferedReader is;
    PrintStream os;
    
    Pipe(InputStream is, OutputStream os) {
        this.is = new BufferedReader(new InputStreamReader(is));
        this.os = new PrintStream(os);
    }
    
    public void close() throws Exception{
    	this.interrupt();
    }
    
    public void run() {
        String line;
        try {
            while ((!this.isInterrupted()) && (line = is.readLine()) != null) {
	            	os.print(line + "\r\n");
	                os.flush();
            }
        } catch (Exception e) {}
    }
}