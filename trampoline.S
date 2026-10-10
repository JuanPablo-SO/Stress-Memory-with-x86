.global trampoline_start
.global trampoline_end
.global ap_stacks
.global ap_main

.section .data
.align 4

trampoline_start:
/* Codigo Inicial em 16-bits (Modo Real). 
 * Os nucleos secundarios (Application Processors - APs) acordam atraves de interrupcoes IPI em modo Real.
 * Esta etapa inicializa os registradores de segmento (DS, ES, SS) com 0x00 para garantir acessos puros à memoria sem offset indesejado. */
.code16
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* Carrega a Tabela de Descritores Global (GDT).
     * Como esse codigo e movido fisicamente para um lugar mais baixo da memoria durante o SMP boot, soma-se 0x8000 para corrigir o endereco. */
    lgdtl (gdtr - trampoline_start + 0x8000)

    /* Habilita o Modo Protegido ativando o Bit 0 (Protection Enable) no registrador de controle CR0. */
    mov %cr0, %eax
    or $1, %al
    mov %eax, %cr0

    /* Far Jump. Limpa o pipeline de instrucoes de 16 bits, carrega o seletor do segmento de Codigo (0x08) e salta para o rotulo de 32 bits. */
    ljmpl $0x08, $(pmode32 - trampoline_start + 0x8000)

.align 4
gdtr:
    .word gdt_end - gdt_start - 1
    .long gdt_start - trampoline_start + 0x8000

/* Estrutura GDT basica de transicao para o Modo Protegido (Segmento Nulo, Segmento de Codigo Executavel e Segmento de Dados). */
.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:

/* Chegada no Modo Protegido de 32 bits. */
.code32
pmode32:
    /* Inicializa todos os segmentos de dados com o seletor de dados da GDT (0x10). */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* 1. Liga o Coprocessador (CR0).
     * Prepara a FPU assim como o Boot Strap Processor (BSP) fez. */
    movl %cr0, %eax
    andl $~0x4, %eax
    orl $0x2, %eax
    movl %eax, %cr0

    /* 2. Liga as excecoes XMM/SSE (CR4). */
    movl %cr4, %eax
    orl $0x600, %eax
    movl %eax, %cr4

    /* 2b. Liga AVX NESTE core.
     * XCR0 e estado por-logical-processor: o XSETBV feito pelo BSP
     * em boot.S nao vale para os APs. Cada core que acorda via
     * INIT/SIPI comeca "zerado" e precisa fazer isto de novo, senao
     * o primeiro vmovntdq de 256 bits que ele executar gera #UD e
     * trava (ou reseta) o core silenciosamente. */
    movl %cr4, %eax
    orl $0x40000, %eax
    movl %eax, %cr4

    xor %ecx, %ecx
    movl $0x7, %eax
    xor %edx, %edx
    xsetbv

    /* Configura a pilha baseada no ID do nucleo.
     * O que faz: Evita que diferentes nucleos dividam e destruam a mesma pilha.
     * Como faz: Le o registrador do Local APIC (0xFEE00020), extrai o ID do nucleo, multiplica por 4096 (o tamanho de stack escolhido) e o utiliza como offset no array ap_stacks para montar uma pilha segura de forma independente no ESP. */
    movl 0xFEE00020, %eax
    shrl $24, %eax
    imull $4096, %eax
    movl $ap_stacks, %esp
    addl %eax, %esp
    addl $4096, %esp

    /* Salta para a funcao main no codigo C, repousando este nucleo na sub-rotina ap_main. */
    movl $ap_main, %eax
    call *%eax

    /* Barreira de seguranca (loop morto) caso ap_main decida retornar por erro. */
1:  cli
    hlt
    jmp 1b

trampoline_end:
