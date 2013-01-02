
import java.awt.BorderLayout;

import javax.swing.JScrollPane;
import javax.swing.SwingUtilities;

@SuppressWarnings("serial")
public class JST extends javax.swing.JApplet {
	private Console console;
	private Telnet telnet; 
	
	public JST() {
		super();
	}
	
    public void init() {
        try {
            SwingUtilities.invokeAndWait(new Runnable() {
                public void run() {
                	initGUI();
                }
            });
        } catch (Exception e) { System.err.println(e);}
        
        int port = 23;
        String host = "192.168.0.99";
        
        try{
        	 port = Integer.parseInt(this.getParameter("PORT"));
        	 if(!this.getDocumentBase().getHost().equals("")){
        		 host = this.getDocumentBase().getHost();
        	 }
        }catch(Exception e){}
        
		telnet = new Telnet(host, port, console.getOutStream(),console.getInStream());
    }

    public void destroy(){
    	if(telnet instanceof Telnet){
    		telnet.close();
    	}
    	console.close();
    	telnet = null;
    	console = null;
    	this.getContentPane().removeAll();
    }
    
	private void initGUI() {
		try {
			console = new Console();
			this.getContentPane().add(new JScrollPane(console),BorderLayout.CENTER);
		} catch (Exception e) {
			e.printStackTrace();
		}
	}
}
