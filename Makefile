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

tui.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/tui.cpp -o tui.o

bf.o:
	$(CXX) $(CXXFLAGS) -c NyxApps/bf.cpp -o bf.o

disk.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/disk.cpp -o disk.o

nyxfs.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/nyxfs.cpp -o nyxfs.o

fat.o:
	$(CXX) $(CXXFLAGS) -c System64/drivers/fat.cpp -o fat.o

kernel.bin: boot.o kernel.o idt.o pic.o heap.o keyboard.o write.o pmm.o newdelete.o tui.o bf.o disk.o nyxfs.o fat.o
	$(LD) -T linker.ld -nostdlib boot.o kernel.o idt.o pic.o heap.o keyboard.o write.o pmm.o newdelete.o tui.o bf.o disk.o nyxfs.o fat.o -o kernel.bin

iso: kernel.bin
	mkdir -p isodir/boot/grub
	cp kernel.bin isodir/boot/kernel.bin
	cp boot/grub/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o DOSnyx.iso isodir

clean:	
	rm -rf *.o *.bin *.iso isodir

disk:
	rm -rf *.img

# Ensure raw test storage images are provisioned before boot execution kicks off
run: DOSnyx.iso
	@# Provision all 4 raw hard drive files if they don't exist
	if [ ! -f dosnyx_disk.img ];  then qemu-img create -f raw dosnyx_disk.img 100M; fi
	if [ ! -f dosnyx_disk2.img ]; then qemu-img create -f raw dosnyx_disk2.img 50M; fi
	if [ ! -f dosnyx_disk3.img ]; then qemu-img create -f raw dosnyx_disk3.img 50M; fi
	if [ ! -f dosnyx_disk4.img ]; then qemu-img create -f raw dosnyx_disk4.img 25M; fi
	
	@# Force the ISO to load as a separate SCSI media device, freeing up all 4 IDE channels
	qemu-system-x86_64 \
		-boot d \
		-device virtio-scsi-pci \
		-drive file=DOSnyx.iso,media=cdrom,if=none,id=cdrom0 \
		-device scsi-cd,drive=cdrom0 \
		-drive file=dosnyx_disk.img,format=raw,bus=0,unit=0,media=disk \
		-drive file=dosnyx_disk2.img,format=raw,bus=0,unit=1,media=disk \
		-drive file=dosnyx_disk3.img,format=raw,bus=1,unit=0,media=disk \
		-drive file=dosnyx_disk4.img,format=raw,bus=1,unit=1,media=disk \
		-m 512M