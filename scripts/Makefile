include ../Makefile.inc

DST = $(PREFIX)/lib/kcs/scripts
VAR = $(PREFIX)/var/lib/kcs

SRC = functions.sh mappings.sh sensors.sh beamformer.sh capture-start capture-stop capture-list capture-destination-beamformer-data capture-destination-beamformer-meta capture-destination capture-status sync-now k7-accumulation-length k7-adc-snap-shot k7-quant-snap-shot k7-snap-shot k7-gain k7-frequency-select halt restart xport label-input label-clear enable-sensors corr test-reload-rf-gain restart-mode-ready restart-mode-c16n400M1k restart-mode-c16n400M8k restart-mode-bc16n400M1k restart-mode-bc8n400M1k_top restart-mode-bc8n400M1k_bottom restart-mode-c16n13M4k restart-mode-c16n25M4k restart-mode-c16n2M4k restart-mode-c16n3M8k restart-mode-c16n7M4k restart-mode-bwbc4a restart-mode-twbc4a restart-mode-c8n25M4k_bottom restart-mode-c8n25M4k_top restart-mode-c8n7M4k_bottom restart-mode-c8n7M4k_top k7-beam-passband k7-beam-weights

all:

clean:

install: all
	$(INSTALL) -d $(DST)
	$(INSTALL) $(SRC) $(DST)
