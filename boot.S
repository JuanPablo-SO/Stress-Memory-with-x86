/* Constantes do Cabecalho Multiboot */
.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set FLAGS,    ALIGN | MEMINFO
.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    /* O GRUB entrega, na entrada:
     *   %eax = 0x2BADB002 (magic)
     *   %ebx = ponteiro fisico para struct multiboot_info (inclui o
     *          mapa de memoria E820 repassado pela BIOS)
     * Nenhuma instrucao ate o "push %ebx" abaixo toca em %ebx, entao
     * ele sobrevive intacto ate virar argumento de main(). Isso
     * elimina a necessidade de "chutar" BASE_TESTE/ZONA_POR_CORE: o
     * C passa a descobrir em runtime quanta RAM existe de verdade. */

    mov $stack_top, %esp

    /* PASSO B: ativar SSE (igual ao original) */
    mov %cr0, %eax
    and $0xFFFB, %ax
    or $0x2, %ax
    mov %eax, %cr0

    mov %cr4, %eax
    or $0x600, %ax
    mov %eax, %cr4

    /* PASSO B2: ativar AVX.
     * VMOVNTDQ de 256 bits (YMM) e uma instrucao codificada em VEX.
     * O processador só a executa (sem #UD) se o SO tiver marcado o
     * estado AVX como "gerenciado":
     *   1) CR4.OSXSAVE (bit 18) libera XSAVE/XSETBV/XGETBV
     *   2) XSETBV com XCR0 = 0b111 (x87 | SSE | AVX) habilita o
     *      componente AVX no contexto salvo por XSAVE.
     * Sem isso, qualquer "vmovntdq %ymmN, ..." mais adiante crasha.
     * Pressuposto: CPU e Ivy Bridge (i3-3220), que suporta AVX. Se
     * este boot.S for reaproveitado em outra maquina, adicione antes
     * um CPUID (leaf 1, ECX bit 28) para confirmar o suporte. */
    mov %cr4, %eax
    or $0x40000, %eax
    mov %eax, %cr4

    xor %ecx, %ecx
    mov $0x7, %eax
    xor %edx, %edx
    xsetbv

    /* PASSO C: chama main(multiboot_info_ptr) */
    push %ebx
    call main

    cli
1:  hlt
    jmp 1b

.size _start, . - _start
