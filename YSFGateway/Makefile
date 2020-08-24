CC      = gcc
CXX     = g++
CFLAGS  = -g -O3 -Wall -std=c++0x -pthread
LIBS    = -lm -lpthread
LDFLAGS = -g

OBJECTS = APRSWriterThread.o APRSWriter.o APRSReader.o Conf.o CRC.o DMRNetwork.o DMRData.o DMRLC.o DMRFullLC.o DMREmbeddedData.o DMREMB.o \
			DMRSlotType.o SHA256.o DelayBuffer.o DMRLookup.o DTMF.o FCSNetwork.o Golay24128.o ModeConv.o GPS.o Log.o StopWatch.o Sync.o \
			BPTC19696.o TCPSocket.o Thread.o Timer.o UDPSocket.o Utils.o Mutex.o WiresX.o Storage.o YSFConvolution.o YSFFICH.o YSFGateway.o \
			RS129.o Hamming.o QR1676.o Golay2087.o YSFNetwork.o YSFPayload.o Reflectors.o Streamer.o

all:		YSFGateway

YSFGateway:	$(OBJECTS)
		$(CXX) $(OBJECTS) $(CFLAGS) $(LIBS) -o YSFGateway

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

clean:
		$(RM) YSFGateway *.o *.d *.bak *~