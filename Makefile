CROSS = /home/juan/x-tools/x86_64-unknown-elf/bin/x86_64-unknown-elf

.PHONY: all flash clean

all: teste_ram.iso

boot.o: boot.S
	$(CROSS)-gcc -m32 -c boot.S -o boot.o

trampoline.o: trampoline.S
	$(CROSS)-gcc -m32 -c trampoline.S -o trampoline.o

main.o: main.c
	$(CROSS)-gcc -m32 -march=ivybridge -ffreestanding -O2 -Wall -Wextra -c main.c -o main.o

projeto_termico.bin: boot.o trampoline.o main.o
	$(CROSS)-gcc -m32 -T linker.ld -o projeto_termico.bin boot.o trampoline.o main.o -ffreestanding -O2 -nostdlib
	@# Falha o build se o binario tiver instrucoes que o Ivy Bridge (i3-3220) nao executa
	@if $(CROSS)-objdump -d projeto_termico.bin | grep -E -q '\b(mulx|shrx|shlx|sarx|rorx|andn|bzhi|pdep|pext|blsi|blsr|blsmsk|tzcnt|lzcnt|movbe|vfmadd[0-9a-z]*|vpbroadcast[a-z]*|vperm2i128|vinserti128|vextracti128|vpgather[a-z]*)\b'; then \
		echo 'ERRO: binario usa instrucoes que o Ivy Bridge nao tem (BMI/AVX2/FMA)'; rm -f projeto_termico.bin; exit 1; fi
	@if $(CROSS)-objdump -d projeto_termico.bin | grep -E -q '\b(mulx|shrx|shlx|sarx|rorx|andn|bzhi|pdep|pext|blsi|blsr|blsmsk|tzcnt|lzcnt|movbe|vfmadd[0-9a-z]*|vpbroadcast[a-z]*|vperm2i128|vinserti128|vextracti128|vpgather[a-z]*)\b'; then \
		echo 'ERRO: binario usa instrucoes que o Ivy Bridge nao tem (BMI/AVX2/FMA)'; exit 1; fi

teste_ram.iso: projeto_termico.bin
	mkdir -p isodir/boot
	cp projeto_termico.bin isodir/boot/
	grub-mkrescue -o teste_ram.iso isodir

flash: teste_ram.iso
	sudo umount /dev/sda || true
	sudo dd if=teste_ram.iso of=/dev/sda bs=4M status=progress oflag=sync
	sudo sync

clean:
	rm -f *.o *.bin *.iso
	rm -rf isodir/boot/*.bin