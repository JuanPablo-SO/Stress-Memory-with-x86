OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
/* Define qual é a primeira instrução a ser executada (aquela que criamos no boot.S) */
ENTRY(_start)

SECTIONS
{
    /* 
     * O ponto (.) representa o endereço de memória atual. 
     * Aqui, mandamos o Linker começar a alocar tudo na marca de 1 Megabyte (1M) da RAM.
     * Esta será a nossa "Zona Segura".
     */
    . = 1M;

    /* 
     * 1. SEÇÃO DE CÓDIGO (.text)
     * Alinhamos a seção em blocos de 4 Kilobytes (4K) por padrão de arquitetura.
     */
    .text BLOCK(4K) : ALIGN(4K)
    {
        /* O cabeçalho Multiboot TEM que ser a primeira coisa do arquivo! */
        *(.multiboot)
        /* Logo em seguida, vem todo o código executável (Assembly e C) */
        *(.text)
    }

    /* 
     * 2. SEÇÃO DE DADOS SOMENTE LEITURA (.rodata)
     * Onde ficam strings (ex: "Testando RAM...") e constantes globais do seu código C.
     */
    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    /* 
     * 3. SEÇÃO DE DADOS INICIALIZADOS (.data)
     * Onde ficam as variáveis globais do seu código C que já começam com algum valor.
     * Exemplo no C: int meus_dados = 5;
     */
    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    /* 
     * 4. SEÇÃO DE DADOS NÃO INICIALIZADOS E PILHA (.bss)
     * Onde ficam as variáveis globais vazias e a pilha (stack) de 16KB que criamos no boot.S.
     */
    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
    
    /* Qualquer outro endereço a partir daqui (gigabytes de RAM) fica livre para ser nossa "Zona de Guerra" */
}
