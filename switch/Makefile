# Experimental native build; requires an already installed devkitPro/libnx SDK.
ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to an existing devkitPro installation)
endif

CXX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-g++
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
LIBNX := $(DEVKITPRO)/libnx
ARCH := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
SOURCES := main.cpp ../src/switch_readonly.cpp ../src/frlg_save.cpp ../src/frlg_content.cpp ../src/frlg_pokemon.cpp ../src/frlg_text.cpp ../src/frlg_write.cpp ../src/frlg_console_write.cpp ../src/frlg_money_editor.cpp ../src/frlg_pokemon_edit.cpp ../src/frlg_pokemon_editor.cpp ../src/frlg_pokemon_write.cpp ../src/frlg_pokemon_traits.cpp ../src/frlg_legality.cpp ../src/frlg_learnsets.cpp ../src/frlg_species_tables.cpp
HEADERS := ../include/pksm/switch_readonly.hpp ../include/pksm/frlg_save.hpp ../include/pksm/frlg_content.hpp ../include/pksm/frlg_pokemon.hpp ../include/pksm/frlg_text.hpp ../include/pksm/frlg_write.hpp ../include/pksm/frlg_console_write.hpp ../include/pksm/frlg_money_editor.hpp ../include/pksm/frlg_pokemon_edit.hpp ../include/pksm/frlg_pokemon_editor.hpp ../include/pksm/frlg_pokemon_write.hpp ../include/pksm/frlg_pokemon_traits.hpp ../include/pksm/frlg_legality.hpp ../include/pksm/frlg_learnsets.hpp ../include/pksm/frlg_species_tables.hpp

.PHONY: all
all: build/pksm-frlg-readonly.nro

build/pksm-frlg-readonly.elf: $(SOURCES) $(HEADERS)
	mkdir -p build
	"$(CXX)" -std=c++17 -O2 -Wall -Wextra -Wpedantic $(ARCH) -D__SWITCH__ \
		-ffunction-sections -fdata-sections -I../include -I"$(LIBNX)/include" \
		$(SOURCES) -specs="$(LIBNX)/switch.specs" -Wl,--gc-sections \
		-L"$(LIBNX)/lib" -lnx -o "$@"

build/pksm-frlg-readonly.nro: build/pksm-frlg-readonly.elf
	"$(ELF2NRO)" "$<" "$@"
