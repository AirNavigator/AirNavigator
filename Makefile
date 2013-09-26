# ============================================================================
# Name : Makefile
# Since : 22/4/2011
# Author : Alberto Realis-Luc <alberto.realisluc@gmail.com>
# Web : http://www.alus.it/airnavigator/
# Copyright : (C) 2010 Alberto Realis-Luc
# License : GNU GPL v2
# Repository : https://github.com/AirNavigator/AirNavigator.git
# Last change : 26/9/2013
# Description : Makefile for AirNavigator for TomTom devices
# ============================================================================

#Usual path of the toolchain: /usr/local/cross/gcc-3.3.4_glibc-2.3.2/bin/

#Compiler and tools
CC = arm-linux-gcc
STRIP = arm-linux-strip

#Compiler and linker flags
CFLAGS = -c -O3 -fPIC -Wall
LFLAGS = -lm -lpthread

#Source and binary paths
SRC = src/
BIN = bin/

#Libs sources and headers
LIBSRC = libs/

#Headers for others external libs
INC = include/

#Source and object files list
CFILES = main.c TomTom.c AirCalc.c BlackBox.c HSI.c Navigator.c NMEAreader.c Configuration.c Geoidal.c
OBJS = $(patsubst %.c, $(BIN)%.o, $(CFILES))

#Final distribution folder
DIST = ./release/

#libraries destination path
LIB = $(DIST)AirNavigator/lib/

#List of libraries
LIBFILES = libroxml.so
LIBS= $(patsubst %.so, $(LIB)%.so, $(LIBFILES))

#Version string
VERSION = 0.2.5

#Name of the ditribution zip file
ZIPNAME = AirNavigator_$(subst .,-,$(VERSION))

#Create bin directory if necessary
$(shell   mkdir -p $(BIN)) 

#Dependencies
all: $(DIST)AirNavigator/AirNavigator

$(DIST)AirNavigator/AirNavigator: $(OBJS) $(LIBS)
	$(CC) $(LFLAGS) $(OBJS) -L$(LIB) -lroxml -o $@
	$(STRIP) $@

$(BIN)main.o: $(SRC)main.c
	$(CC) $(CFLAGS) -D'VERSION="$(VERSION)"' $< -o $@

$(BIN)NMEAreader.o: $(SRC)NMEAreader.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)tpgps.o: $(SRC)tpgps.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)SiRFreader.o: $(SRC)SiRFreader.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)Navigator.o: $(SRC)Navigator.c
	$(CC) $(CFLAGS) -I $(LIBSRC) $< -o $@

$(BIN)HSI.o: $(SRC)HSI.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)BlackBox.o: $(SRC)BlackBox.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)TomTom.o: $(SRC)TomTom.c
	$(CC) $(CFLAGS) -DLINUX_TARGET -I $(INC) $< -o $@

$(BIN)Configuration.o: $(SRC)Configuration.c
	$(CC) $(CFLAGS) -I $(LIBSRC) $< -o $@

$(BIN)Geoidal.o: $(SRC)Geoidal.c
	$(CC) $(CFLAGS) $< -o $@

$(BIN)AirCalc.o: $(SRC)AirCalc.c
	$(CC) $(CFLAGS) $< -o $@

$(LIB)libroxml.so: $(LIBSRC)libroxml/Makefile
	make -C $(LIBSRC)libroxml
	cp -f $(LIBSRC)libroxml/libroxml.so $@

#Clean: delete all binaries and libs built
clean:
	@rm -f $(BIN)*
	make -C $(LIBSRC)libroxml clean
	@rm -f $(LIB)*
	@rm -f $(DIST)AirNavigator/AirNavigator
	@rm -f $(DIST)$(ZIPNAME).zip
	
#Make the zip file with the final distribution
zip: all
	@rm -f $(DIST)*.zip
	cd $(DIST); zip -9 -T -x "*.git*" "*.svn*" "*CVS*" "*Thumbs.db*" -r .$(DIST)$(ZIPNAME).zip . ;cd ..;