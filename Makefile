CXX = x86_64-elf-g++
LD  = x86_64-elf-ld
ASM = nasm

CXXFLAGS = -ffreestanding -O2 -Wall -Wextra -std=gnu++20 -mno-red-zone \
           -mcmodel=kernel -fno-pic -fno-exceptions -fno-rtti \
           -mno-sse -mno-sse2 -mno-mmx

all: kernel.bin iso

boot.o:
	$(ASM) -f elf64 System64/bootload.asm -o boot.o

kernel.o:
	$(CXX) $(CXXFLAGS) -c System64/vmkrnl.cpp -o kernel.o

idt.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/idt.cpp -o idt.o

pic.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/pic.cpp -o pic.o

keyboard.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/keyboard.cpp -o keyboard.o

heap.o:
	$(CXX) $(CXXFLAGS) -c System64/memory/heap.cpp -o heap.o

write.o:
	$(CXX) $(CXXFLAGS) -c NyxApps/write.cpp -o write.o

pmm.o:
	$(CXX) $(CXXFLAGS) -c System64/memory/pmm.cpp -o pmm.o

newdelete.o:
	$(CXX) $(CXXFLAGS) -c System64/newdelete.cpp -o newdelete.o

kernel.bin: boot.o kernel.o idt.o pic.o heap.o keyboard.o write.o pmm.o newdelete.o
	$(LD) -T linker.ld -nostdlib boot.o kernel.o idt.o pic.o heap.o keyboard.o write.o pmm.o newdelete.o -o kernel.bin

iso: kernel.bin
	mkdir -p isodir/boot/grub
	cp kernel.bin isodir/boot/kernel.bin
	cp boot/grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o DOSnyx.iso isodir

clean:	
	rm -rf *.o *.bin *.iso isodir

run:
	qemu-system-x86_64 \
	-cdrom DOSnyx.iso \
	-m 512M