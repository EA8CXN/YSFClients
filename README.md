I want to update YSFGateway from good work of because it is somewhat not outdated. 
I hope i can solve several minor problems with code.

The changes are:

- Added choice of mode changing Reflector from handhel from 1 to , so 1=Parrot, 2=YSF, 3=FCS, 4=DMR, 5=NXDN, 6=P25
- When mode is selected you can select reflector from Handhel with number based in the selected Domain only if number > 6 and number <> 0.
- YSFGateway manages all system reflector, so when using YSF only YSF reflector shows when inquire ALL, DMR only reflectors show when inquire ALL, and so on...
- YSFGatway controls change of reflector or TG, only using syntesized passthrough for NXDN or P25.
- Full YSF2DMR included inside YSFGateway :-)
- BEACON included with choice to record DV_MODE2 speech.
- Added GPS aprs.if packets GPS information rewriting in all modes. 
- Added buffer and DATA regeenerating on YSF and FCS that enable on the fly change of destiny and GPS information.
- Adedd option to lock reflector.
- Automatic Startup on any mode.
- Automatic revert on any mode.
- Pass data to network only if message or photo kind. WiresX processing decides if it go to network or non, so we make a buffer to delay decision. It allows sending photo and messagin through YSF network. But as Modem can't handle photo packets we need to regenerate lost packets, so photo transfer is not jet operational.
- Many many options...

I dont have much time to do coding, so I hope this project soon improve.

Manuel
EA7EE

This software is licenced under the GPL v2 and is intended for amateur and educational use only. Use of this software for commercial purposes is strictly forbidden.
