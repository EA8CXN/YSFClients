I want to update YSFGateway from good work of Jonathan because it is somewhat outdated. 
I hope i can solve several minor problems with code.

Some changes are:

- Added choice of changing mode and Reflector from handhel from reflector 1 to 6, so 1=Parrot, 2=YSF, 3=FCS, 4=DMR, 5=NXDN, 6=P25. It remember last reflector on any mode. But it is no permanent and it don't stand a power recycle.
- When mode(YSF,FCS,DMR,..) is selected you can select reflector from handhel from only selected Domain reflector when number > 6 and number <> 0.
- YSFGateway manages all system reflector selection, so when using YSF only YSF reflector shows when inquire ALL, DMR only reflectors show when inquire ALL, and so on...
- YSFGatway controls change of reflector or TG, only using syntesized passthrough for NXDN or P25.
- Full YSF2DMR included inside YSFGateway :-)
- Full WiresX commands to storage of photo,message or voice message (not tested, voice message not yet operational)
- BEACON included with choice to record DV_MODE2 speech.
- Added GPS aprs.fi information on all modes when not GPS information available in packet. 
- Added a buffer and DATA regenerating on YSF and FCS that enable on the fly change of packet destination and GPS information.
- Adeed option to lock reflector.
- Automatic Startup and revert on any mode.
- YSFGateway can pass data to network only if message or photo data type was send to ALL. WiresX processing decides if it go to network or no, so we make a buffer to delay decision. It allows to send photo and messages through YSF networks. But as Modem and MMDVMHost can't handle photo packets we need to regenerate lost packets, so photo transfer is not yet operational.
- Many many options more...

I dont have much time to do coding, so I hope this project soon improve.

Manuel
EA7EE

This software is licenced under the GPL v2 and is intended for amateur and educational use only. Use of this software for commercial purposes is strictly forbidden.
