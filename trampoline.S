.global trampoline_start
.global trampoline_end
.global ap_stacks
.global ap_main

.section .data
.align 4

trampoline_start:
/* Codigo Inicial em 16-bits (Modo Real). 
 * Os nucleos secundarios (Application Processors - APs) acordam atraves de interrupcoes IPI em modo Real[cite: 3].
 * Esta etapa inicializa os registradores de segmento (DS, ES, SS) com 0x00 para garantir acessos puros à memoria sem offset indesejado[cite: 3]. */
.code16
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* Carrega a Tabela de Descritores Global (GDT).
     * Como esse codigo e movido fisicamente para um lugar mais baixo da memoria durante o SMP boot, soma-se 0x8000 para corrigir o endereco[cite: 3]. */
    lgdtl (gdtr - trampoline_start + 0x8000)

    /* Habilita o Modo Protegido ativando o Bit 0 (Protection Enable) no registrador de controle CR0[cite: 3]. */
    mov %cr0, %eax
    or $1, %al
    mov %eax, %cr0

    /* Far Jump. Limpa o pipeline de instrucoes de 16 bits, carrega o seletor do segmento de Codigo (0x08) e salta para o rotulo de 32 bits[cite: 3]. */
    ljmpl $0x08, $(pmode32 - trampoline_start + 0x8000)

.align 4
gdtr:
    .word gdt_end - gdt_start - 1
    .long gdt_start - trampoline_start + 0x8000

/* Estrutura GDT basica de transicao para o Modo Protegido (Segmento Nulo, Segmento de Codigo Executavel e Segmento de Dados)[cite: 3]. */
.align 8
gdt_start:
    .quad 0x0000000000000000
    .quad 0x00cf9a000000ffff
    .quad 0x00cf92000000ffff
gdt_end:

/* Chegada no Modo Protegido de 32 bits[cite: 3]. */
.code32
pmode32:
    /* Inicializa todos os segmentos de dados com o seletor de dados da GDT (0x10)[cite: 3]. */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    /* 1. Liga o Coprocessador (CR0)[cite: 3].
     * Prepara a FPU assim como o Boot Strap Processor (BSP) fez[cite: 3]. */
    movl %cr0, %eax
    andl $~0x4, %eax
    orl $0x2, %eax
    movl %eax, %cr0

    /* 2. Liga as excecoes XMM/SSE (CR4)[cite: 3]. */
    movl %cr4, %eax
    orl $0x600, %eax
    movl %eax, %cr4

    /* 2b. Liga AVX NESTE core[cite: 3].
     * XCR0 e estado por-logical-processor: o XSETBV feito pelo BSP
     * em boot.S nao vale para os APs. Cada core que acorda via
     * INIT/SIPI comeca "zerado" e precisa fazer isto de novo, senao
     * o primeiro vmovntdq de 256 bits que ele executar gera #UD e
     * trava (ou reseta) o core silenciosamente[cite: 3]. */
    movl %cr4, %eax
    orl $0x40000, %eax
    movl %eax, %cr4

    xor %ecx, %ecx
    movl $0x7, %eax
    xor %edx, %edx
    xsetbv

    /* Configura a pilha baseada no ID do nucleo[cite: 3].
     * O que faz: Evita que diferentes nucleos dividam e destruam a mesma pilha[cite: 3].
     * Como faz: Le o registrador do Local APIC (0xFEE00020), extrai o ID do nucleo, multiplica por 4096 (o tamanho de stack escolhido) e o utiliza como offset no array ap_stacks para montar uma pilha segura de forma independente no ESP[cite: 3]. */
    movl 0xFEE00020, %eax
    shrl $24, %eax
    imull $4096, %eax
    movl $ap_stacks, %esp
    addl %eax, %esp
    addl $4096, %esp

    /* Salta para a funcao main no codigo C, repousando este nucleo na sub-rotina ap_main[cite: 3]. */
    movl $ap_main, %eax
    call *%eax

    /* Barreira de seguranca (loop morto) caso ap_main decida retornar por erro[cite: 3]. */
1:  cli
    hlt
    jmp 1b

trampoline_end: