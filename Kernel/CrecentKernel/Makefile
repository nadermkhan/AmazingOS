# Toolchain configuration
CXX = g++
LD = ld
OBJCOPY = objcopy

# Freestanding C++ compilation flags
CXXFLAGS = -O2 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Wall -Wextra -Isrc

# Linker flags to build a raw freestanding PE32+ image with custom entry point
LDFLAGS = -T linker.ld -nostdlib --entry=boot_entry --subsystem=native --image-base 0x0

# Final executables
TARGET_PE = kernel.exe
TARGET_ELF = kernel.bin

# Object files mapping
OBJS = boot.o main.o vga.o serial.o vfs.o gdt.o idt.o apic.o interrupts.o pmm.o vmm.o heap.o switch.o scheduler.o

.PHONY: all clean qemu

all: $(TARGET_ELF)

# Link the intermediate PE executable
$(TARGET_PE): $(OBJS)
	$(LD) $(LDFLAGS) -o $(TARGET_PE) $(OBJS)

# Convert the PE executable to a standard ELF32 binary (required for Multiboot loaders)
$(TARGET_ELF): $(TARGET_PE)
	$(OBJCOPY) -O elf32-i386 $(TARGET_PE) $(TARGET_ELF)

# Object files rules
boot.o: src/boot/boot.S
	$(CXX) -c src/boot/boot.S -o boot.o

main.o: src/kernel/main.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/main.cpp -o main.o

vga.o: src/drivers/vga.cpp
	$(CXX) $(CXXFLAGS) -c src/drivers/vga.cpp -o vga.o

serial.o: src/drivers/serial.cpp
	$(CXX) $(CXXFLAGS) -c src/drivers/serial.cpp -o serial.o

vfs.o: src/fs/vfs.cpp
	$(CXX) $(CXXFLAGS) -c src/fs/vfs.cpp -o vfs.o

gdt.o: src/kernel/gdt.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/gdt.cpp -o gdt.o

idt.o: src/kernel/idt.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/idt.cpp -o idt.o

apic.o: src/drivers/apic.cpp
	$(CXX) $(CXXFLAGS) -c src/drivers/apic.cpp -o apic.o

interrupts.o: src/kernel/interrupts.S
	$(CXX) -c src/kernel/interrupts.S -o interrupts.o

pmm.o: src/kernel/pmm.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/pmm.cpp -o pmm.o

vmm.o: src/kernel/vmm.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/vmm.cpp -o vmm.o

heap.o: src/kernel/heap.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/heap.cpp -o heap.o

switch.o: src/kernel/switch.S
	$(CXX) -c src/kernel/switch.S -o switch.o

scheduler.o: src/kernel/scheduler.cpp
	$(CXX) $(CXXFLAGS) -c src/kernel/scheduler.cpp -o scheduler.o

# Clean up build artifacts
clean:
	rm -f *.o $(TARGET_PE) $(TARGET_ELF)

# Launch emulator target
qemu: $(TARGET_ELF)
	qemu-system-x86_64 -kernel $(TARGET_ELF) -serial stdio
