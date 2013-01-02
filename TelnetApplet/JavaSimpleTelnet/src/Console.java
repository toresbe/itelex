import java.awt.event.KeyEvent;
import java.awt.event.KeyListener;
import java.awt.event.MouseListener;
import java.awt.event.MouseMotionListener;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.LinkedList;
import java.util.Queue;

import javax.swing.JTextArea;


@SuppressWarnings("serial")
public class Console extends JTextArea {
	private boolean inputable = true;

	private ConsoleOutputStream out;
	private ConsoleInputStream in;

	private int inputStart;
	private int inputOffset;
	
	private JTextArea text;

	public Console() {
		super();
		text = this;
		setInputable(false);
		initListeners();

		inputStart = 0;
		inputOffset = 0;

		out = new ConsoleOutputStream();
		in = new ConsoleInputStream();
	}

	public OutputStream getOutStream() {
		return out;
	}

	public void close(){
		out.close();
		try {
			in.close();
		} catch (IOException e) {
			e.printStackTrace();
		}
	}
	public InputStream getInStream() {
		return in;
	}

	private void initListeners() {
		for (KeyListener k : getKeyListeners())
			removeKeyListener(k);
		for (MouseListener m : getMouseListeners())
			removeMouseListener(m);
		for (MouseMotionListener m : getMouseMotionListeners())
			removeMouseMotionListener(m);

		addKeyListener(new KeyListener() {
			synchronized public void keyPressed(KeyEvent e) {
				int cpos = getCaretPosition();
				e.consume();
				if (inputable) {
					try {
						switch (e.getKeyCode()) {
						case KeyEvent.VK_ENTER:
							setInputable(false);
							String input = text.getText(inputStart,inputOffset);
							text.insert( "\n", inputStart + inputOffset);
							inputStart += (inputOffset + 1);
							inputOffset = 0;
							in.fill((input + '\n').getBytes());
							setCaretPosition(inputStart);
							break;
						case KeyEvent.VK_LEFT:
							if (cpos > inputStart)
								setCaretPosition(cpos - 1);
							break;
						case KeyEvent.VK_RIGHT:
							if (cpos < inputStart + inputOffset)
								setCaretPosition(cpos + 1);
							break;
						case KeyEvent.VK_HOME:
							setCaretPosition(inputStart);
							break;
						case KeyEvent.VK_END:
							setCaretPosition(inputStart + inputOffset);
							break;
						case KeyEvent.VK_DELETE:
							if (cpos < inputStart + inputOffset) {
								text.replaceRange("", cpos, cpos+1);
								inputOffset--;
							}
							break;
						case KeyEvent.VK_BACK_SPACE:
							if (cpos > inputStart) {
								text.replaceRange("", cpos-1, cpos);
								inputOffset--;
							}
							break;
						default:
							char ec = e.getKeyChar();
							if (ec > 31 && ec < 127) {
								text.insert(String.valueOf(ec), getCaretPosition());
								inputOffset++;
							}
							break;
						}
						in.process();
					} catch (Exception ex) {
						ex.printStackTrace();
					}
				}
			}

			public void keyTyped(KeyEvent e) {
				e.consume();
			}

			public void keyReleased(KeyEvent e) {
				e.consume();
			}
		});
	}
	
	
	private synchronized void print(byte[] b, ConsoleOutputStream os) {
		this.insert(new String(b, 0, b.length), inputStart);
		inputStart += b.length;
		setCaretPosition(getCaretPosition() + b.length);
	}

	private void setInputable(boolean b) {
		inputable = b;
	}

	private class ConsoleOutputStream extends OutputStream {
		private Queue<Byte> buffer = new LinkedList<Byte>();
		private boolean closed;

		public ConsoleOutputStream() {
			closed = false;
		}

		@Override
		public void write(int b) throws IOException {
			if (closed) {
				throw new IOException("Stream closed");
			}
			synchronized (buffer) {
				buffer.offer((byte) b);
			}
		}

		@Override
		public void flush() {
			synchronized (buffer) {
				byte[] b = new byte[buffer.size()];
				int cnt = 0;
				while (!buffer.isEmpty())
					b[cnt++] = buffer.poll();
				print(b, this);
			}
		}

		@Override
		public void close() {
			closed = true;
		}
	}

	private class ConsoleInputStream extends InputStream {
		private Queue<Byte> buffer = new LinkedList<Byte>();
		int cnt = 0;

		synchronized public void process() {
			notify();
		}

		@Override
		synchronized public int read() throws IOException {
			if (cnt == 0) {
				setInputable(true);
				while (buffer.isEmpty()) {
					try {
						wait();
					} catch (Exception e) {}
				}
				setInputable(false);
				cnt = buffer.size() + 1;
			}
			cnt--;
			return buffer.isEmpty() ? -1 : buffer.poll();
		}

		public void fill(byte[] b) {
			for (byte i : b)
				buffer.offer(i);
		}
	}
}
