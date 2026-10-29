"""Save one CAN session. Dependency: pyserial==3.5."""
import argparse
import pathlib
import threading
import time
import serial

p = argparse.ArgumentParser()
p.add_argument('--port', required=True)
p.add_argument('--out', required=True, help='New output directory; never overwrites')
p.add_argument('--bitrate', type=int, choices=[125000,250000,500000,1000000], default=250000)
a = p.parse_args()
out = pathlib.Path(a.out)
out.mkdir(parents=True, exist_ok=False)
done = threading.Event()
failed = threading.Event()
with serial.Serial(a.port, 921600, timeout=0.2, write_timeout=2) as s:
    # Opening a USB UART can reset ESP32. Discard boot chatter before commands.
    time.sleep(3)
    s.reset_input_buffer()
    def read():
        try:
            with (out/'capture.csv').open('w',encoding='utf-8',newline='') as frames, (out/'session.log').open('w',encoding='utf-8',newline='') as meta, (out/'events.csv').open('w',encoding='utf-8',newline='') as events:
                events.write('timestamp_us,label\n')
                while not done.is_set():
                    raw=s.readline()
                    if not raw:
                        continue
                    line=raw.decode('utf-8',errors='strict').rstrip('\r\n')
                    if line.startswith('#'):
                        meta.write(line+'\n'); meta.flush()
                        print(line)
                        if line.startswith('#EVENT,'):
                            events.write(line[len('#EVENT,'):]+'\n'); events.flush()
                        if line=='#STOP':
                            done.set()
                    elif line=='timestamp_us,id_hex,extended,rtr,dlc,data_hex' or (line and line[0].isdigit()):
                        frames.write(line+'\n'); frames.flush()
                    else:
                        raise RuntimeError('Unexpected serial line: '+repr(line))
        except Exception as e:
            print('CAPTURE FAILED:',e)
            failed.set(); done.set()
    worker=threading.Thread(target=read,daemon=True)
    worker.start()
    s.write(f'BITRATE {a.bitrate}\nSTART\n'.encode())
    print('Commands: MARK pressionei tecla X | MARK soltei tecla X | STATUS | STOP')
    try:
        while not done.is_set():
            c=input('> ').strip()
            if c=='STATUS' or c=='STOP' or c.startswith('MARK '):
                if len(c.encode('utf-8'))>180:
                    print('Command exceeds 180 bytes'); continue
                s.write((c+'\n').encode('utf-8'))
                if c=='STOP':
                    if not done.wait(30):
                        raise RuntimeError('No #STOP received; session incomplete')
            else:
                print('Use MARK text, STATUS or STOP')
    except (KeyboardInterrupt,EOFError):
        s.write(b'STOP\n')
        if not done.wait(30):
            failed.set(); print('Session incomplete: no #STOP received')
    finally:
        done.set(); worker.join(2)
if failed.is_set():
    (out/'INCOMPLETE.txt').write_text('Capture failed or did not close cleanly. Inspect session.log.',encoding='utf-8')
    raise SystemExit(1)
