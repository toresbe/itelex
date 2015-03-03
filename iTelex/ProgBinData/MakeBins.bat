C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\..\..\TxP2\AnalogModem2\default\AnalogModem2.hex .\AnalogModem.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\..\..\TxP2\ED1000\default\ED1000.hex .\ED1000.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\..\..\TxP2\FernschrTW39\default\FernschrTW39.hex .\FernschrTW39.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\..\..\TxP2\Messgeraet\default\Messgeraet.hex .\Messgeraet.bin
C:\WinAVR-20100110\avr\bin\objcopy.exe -I ihex -O binary ..\..\..\TxP2\SeriellUndSpeicher\default\SeriellUndSpeicher.hex .\SeriellUndSpeicher.bin
@echo ===============================================
@echo und nun alle .bin auf das Programm HEXY werfen.
@echo ===============================================
D:\Sonnenrein\Programme\hexy.exe
