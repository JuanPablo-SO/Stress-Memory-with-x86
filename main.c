/* Garante codigo gerado compativel com Ivy Bridge (sem BMI2/AVX2) mesmo se
 * o arch padrao do cross-gcc for mais novo. */
#pragma GCC target("arch=ivybridge")

#include "stdint.h"
#include "stddef.h"
#include "stdbool.h"

/* ====================================================================
 * Teste de banda de ESCRITA - DDR3-1333, 1 canal, i3-3220 (Ivy Bridge)
 *
 * Usa SO o core 0. O loop de escrita nunca para nem espera nada: a tela
 * e atualizada no meio do proprio loop.
 *
 *  - AO VIVO: a cada JANELAS_POR_LEITURA janelas (~13 ms, ~75 vezes por
 *    segundo) mede a banda daquele trecho e reescreve so os caracteres
 *    que mudaram. Os digitos ficam "correndo" na tela o tempo todo.
 *  - MEDIA 1 s: a cada 1 s (medido em ciclos de TSC, nao em contagem de
 *    janelas) calcula a banda media do ultimo segundo.
 *  - MEDIA GERAL: media dessas medias desde o inicio.
 * ==================================================================== */

 /* Ponteiro para a memoria de video modo texto (VGA) no endereco fisico padrao 0xB8000. */
volatile uint16_t *vga_buffer = (uint16_t *)0xB8000;

/* Regiao escrita em loop: BASE_ZONA .. BASE_ZONA + ZONA_BYTES.
 * Assume RAM livre ali (placa com 4 GB: 256 MiB .. 768 MiB). */
#define BASE_ZONA     0x10000000u
#define ZONA_BYTES    (512u * 1024u * 1024u)
#define JANELA_BYTES  (4u * 1024u * 1024u)
#define JANELA_MIB    (JANELA_BYTES >> 20)

/* Leitura ao vivo: 32 janelas x 4 MiB = 128 MiB (~13 ms a ~10 GB/s) */
#define JANELAS_POR_LEITURA 32u

/* Teto DDR3-1333 x 64 bits = 10.666,67 MB/s = 10.172,4 MiB/s (x10) */
#define TETO_MIBS_X10 101724u

/* Exigidos pelo trampoline.S (que continua no link, mas nao e usado) */
#define NUM_CORES  4
#define STACK_SIZE 4096
uint8_t ap_stacks[NUM_CORES][STACK_SIZE];

/* Funcao para os nucleos secundarios rodarem apos o boot. Mantem os nucleos paralisados em loop (cli/hlt). */
void ap_main(void)
{
    for (;;) asm volatile ("cli\n\thlt");
}

/* Padrao de 32 bytes (256 bits) alinhado em memoria que sera carregado nos registradores AVX. */
static uint64_t padrao[4] __attribute__((aligned(32))) = {
    0xAAAAAAAAAAAAAAAAull, 0x5555555555555555ull,
    0xAAAAAAAAAAAAAAAAull, 0x5555555555555555ull
};

static uint32_t tsc_hz;
static uint32_t tsc_medido_hz;

/* ---------------- utilitarios ---------------- */

/* Le o Time Stamp Counter (TSC).
 * O que faz: Retorna o numero de ciclos do processador decorridos desde o reset.
 * Como faz: Usa a instrucao assembly 'rdtsc' que armazena os 32 bits menos significativos em eax e os mais significativos em edx, fundindo-os em um uint64_t. */
static inline uint64_t ler_rdtsc(void)
{
    uint32_t lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Divisao 64/32 via divl. Exige que o quociente caiba em 32 bits.
 * O que faz: Divide para calcular taxas, como a banda transferida por segundo. */
static inline uint32_t div64(uint64_t dividendo, uint32_t divisor)
{
    uint32_t hi = (uint32_t)(dividendo >> 32);
    uint32_t lo = (uint32_t)dividendo;
    uint32_t q, r;
    asm volatile ("divl %[d]" : "=a"(q), "=d"(r)
                  : "a"(lo), "d"(hi), [d] "r"(divisor));
    return q;
}

/* Funcoes de porta I/O (In/Out) para comunicacao direta com hardware, como o timer PIT. */
static inline void outb(uint16_t port, uint8_t v)
{
    asm volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    asm volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* Mede o TSC contra o PIT (1.193.182 Hz) durante ~54,9 ms
 * O que faz: Descobre a frequencia real do processador para transformar medicoes de ciclo (rdtsc) em tempo de relogio (segundos).
 * Como faz: Configura o timer em hardware (portas 0x43/0x42/0x61), aguarda um evento fisico do sistema, marcando o TSC no inicio e no fim para estipular quantos ciclos passaram naqueles ms do PIT. */
static uint32_t calibrar_tsc(void)
{
    const uint32_t cont = 0xFFFFu;
    uint8_t v = inb(0x61);
    outb(0x61, (uint8_t)(v & 0xFCu));            /* gate=0, alto-falante off */
    outb(0x43, 0xB0);                            /* canal 2, lo/hi, modo 0 */
    outb(0x42, (uint8_t)(cont & 0xFFu));
    outb(0x42, (uint8_t)(cont >> 8));
    outb(0x61, (uint8_t)((v & 0xFCu) | 0x01u));  /* gate=1: inicia contagem */
    uint64_t t0 = ler_rdtsc();
    while (!(inb(0x61) & 0x20u)) { }
    uint64_t t1 = ler_rdtsc();
    outb(0x61, v);
    return div64((t1 - t0) * 1193182ull, cont);
}

/* No Sandy/Ivy Bridge o TSC roda a (razao base x 100 MHz). Se a medida
 * ficar a menos de 1% de um multiplo de 100 MHz, usa o multiplo: tira o
 * erro de ~0,2% da calibracao pelo PIT. 
 * O que faz: Arredonda a medicao bruta da funcao acima para o clock exato provavel do cristal-base do CPU. */
static uint32_t ajustar_tsc(uint32_t medido)
{
    uint32_t alvo = ((medido + 50000000u) / 100000000u) * 100000000u;
    uint32_t dif = (alvo > medido) ? (alvo - medido) : (medido - alvo);
    return (dif < alvo / 100u) ? alvo : medido;
}

/* ---------------- texto na VGA ---------------- */
/* Estrutura para atuar como "String Builder" simples (limite 80 caracteres de tela). */
typedef struct { char b[81]; int n; } sb_t;

#define COR_TITULO 0x0E   /* amarelo */
#define COR_TEXTO  0x07   /* cinza claro */
#define COR_VALOR  0x0A   /* verde claro */
#define COR_DESTAQ 0x0F   /* branco */

static void sb_init(sb_t *s) { s->n = 0; }
static void sb_str(sb_t *s, const char *t) { while (*t && s->n < 80) s->b[s->n++] = *t++; }

/* Numero alinhado a direita em `largura` colunas; dec=2 mostra v/100 com virgula
 * O que faz: Converte inteiros para texto formatado em buffer. */
 static void sb_num(sb_t *s, uint32_t v, int dec, int largura)
{
    char t[16]; int k = 0;
    if (dec) {
        t[k++] = (char)('0' + v % 10u); v /= 10u;
        t[k++] = (char)('0' + v % 10u); v /= 10u;
        t[k++] = ',';
    }
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (largura > k && s->n < 80) { s->b[s->n++] = ' '; largura--; }
    while (k && s->n < 80) s->b[s->n++] = t[--k];
}
static void sb_2d(sb_t *s, uint32_t v)
{
    if (s->n < 79) { s->b[s->n++] = (char)('0' + (v / 10u) % 10u); s->b[s->n++] = (char)('0' + v % 10u); }
}

/* Reescreve a linha inteira na memoria VGA.
 * Como faz: Calcula a posicao inicial da linha e aplica mascara para associar o caractere ao byte de cor escolhido (uint16). */
static void imprimir(sb_t *s, int linha, uint8_t cor)
{
    int i = 0, pos = linha * 80;
    s->b[s->n] = '\0';
    for (; s->b[i] && i < 80; i++)
        vga_buffer[pos + i] = (uint16_t)(uint8_t)s->b[i] | ((uint16_t)cor << 8);
    for (; i < 80; i++) vga_buffer[pos + i] = ((uint16_t)cor << 8) | 0x20u;
}
static void imprimir_txt(const char *t, int linha, uint8_t cor)
{
    sb_t s; sb_init(&s); sb_str(&s, t); imprimir(&s, linha, cor);
}

/* Reescreve so as colunas que mudaram desde a ultima chamada com o mesmo
 * `cache`. A VGA e lenta (memoria nao-cacheavel), entao trocar 8 ou 10
 * caracteres em vez de 80 deixa a atualizacao ao vivo quase de graca.
 * Cada `cache` pertence a uma unica linha da tela.
 * O que faz: É uma otimizacao critica. Se redesenhasse os 80 blocos o tempo gasto em I/O mascararia a performance da RAM. Faz o diferencial e atualiza a VGA seletivamente. */
static void imprimir_dif(sb_t *s, int linha, uint8_t cor, char *cache)
{
    int pos = linha * 80;
    for (int i = 0; i < 80; i++) {
        char c = (i < s->n) ? s->b[i] : ' ';
        if (c != cache[i]) {
            cache[i] = c;
            vga_buffer[pos + i] = (uint16_t)(uint8_t)c | ((uint16_t)cor << 8);
        }
    }
}

/* ---------------- nucleo da carga ---------------- */

/* Move o padrao de teste (declarado acima) direto para o registrador AVX YMM0. */
static inline void carregar_padrao(void)
{
    asm volatile ("vmovdqa %0, %%ymm0"
                  : : "m"(*(const uint8_t (*)[32])padrao) : "ymm0");
}

/* Escreve `bytes` (multiplo de 256) com stores non-temporal, 4 linhas de
 * 64 B por bloco, sem sobras no write-combining buffer.
 * O que faz: Preenche a RAM o mais rapido possivel evitando poluir a cache (L1/L2/L3) usando vmovntdq.
 * Como faz: Desenrola um loop em assembly enviando o conteudo do registrador ymm0 (256 bits/32 bytes) seguidamente, somando 256 bytes por passada na zona de testes. 
 * Ao final, a barreira sfence forca a descarga dos buffers de escrita (write-combining) para a RAM principal antes de prosseguir. */
static inline void escrever_janela(volatile uint8_t *p, uint32_t bytes)
{
    volatile uint8_t *fim = p + bytes;
    asm volatile (
        "1:\n\t"
        "vmovntdq %%ymm0,   0(%0)\n\t" "vmovntdq %%ymm0,  32(%0)\n\t"
        "vmovntdq %%ymm0,  64(%0)\n\t" "vmovntdq %%ymm0,  96(%0)\n\t"
        "vmovntdq %%ymm0, 128(%0)\n\t" "vmovntdq %%ymm0, 160(%0)\n\t"
        "vmovntdq %%ymm0, 192(%0)\n\t" "vmovntdq %%ymm0, 224(%0)\n\t"
        "add $256, %0\n\t"
        "cmp %1, %0\n\t"
        "jb 1b\n\t"
        "sfence\n\t"
        : "+r"(p) : "r"(fim) : "memory", "cc");
}

/* ---------------- painel ---------------- */

#define LIN_VIVO   5
#define LIN_MEDIA1 6
#define LIN_MEDIAG 7
#define LIN_TOTAL  12
#define LIN_TEMPO  13

static uint64_t t_inicio, t_seg, t_leit, soma_mibs;
static uint32_t total_mib, mib_seg, mib_leit, amostras;
static char cache_vivo[80], cache_total[80];

/* Prepara as strings com as métricas formatadas e as porcentagens em relacao ao limite teorico. */
static void monta_taxa(sb_t *s, const char *rotulo, uint32_t mibs)
{
    sb_init(s);
    sb_str(s, rotulo);
    sb_num(s, mibs, 0, 6);                              sb_str(s, " MiB/s | ");
    sb_num(s, mibs * 10486u / 100000u, 2, 6);           sb_str(s, " GB/s | ");
    sb_num(s, mibs * 100000u / TETO_MIBS_X10, 2, 6);    sb_str(s, " % do teto");
}

static void monta_total(sb_t *s)
{
    sb_init(s);
    sb_str(s, "TOTAL ESCRITO: ");
    sb_num(s, (uint32_t)(((uint64_t)total_mib * 100u) >> 10), 2, 0);
    sb_str(s, " GiB");
}
/* Desenha as informacoes estaticas na tela durante o carregamento, como os parametros do processador e as frequencias medidas. */
static void desenhar_fixo(void)
{
    sb_t s;
    imprimir_txt("TESTE DE BANDA DE ESCRITA DA RAM - DDR3-1333, 1 canal, 64 bits", 0, COR_TITULO);

    sb_init(&s);
    sb_str(&s, "i3-3220 | 1 thread | AVX vmovntdq (non-temporal) | zona de ");
    sb_num(&s, ZONA_BYTES >> 20, 0, 0); sb_str(&s, " MiB");
    imprimir(&s, 1, COR_TEXTO);

    imprimir_txt("Teto teorico: 10.667 MB/s = 10.172 MiB/s (1333 MT/s x 8 bytes)", 2, COR_TEXTO);

    sb_init(&s);
    sb_str(&s, "TSC: "); sb_num(&s, tsc_hz / 1000000u, 0, 0);
    sb_str(&s, " MHz (calibracao pelo PIT mediu ");
    sb_num(&s, tsc_medido_hz / 1000000u, 0, 0); sb_str(&s, " MHz)");
    imprimir(&s, 3, COR_TEXTO);

}

/* Fecha 1 segundo: media do ultimo segundo e media geral.
 * O que faz: Acumula a banda medida durante as fracoes de segundo para calcular os valores consolidados. Também atualiza o relogio de Uptime da tela. */
static void fechar_segundo(uint64_t agora)
{
    uint32_t dmib = total_mib - mib_seg;
    uint32_t dt   = (uint32_t)(agora - t_seg);
    uint32_t mibs = div64((uint64_t)dmib * tsc_hz, dt);
    t_seg = agora; mib_seg = total_mib;

    /* O 1o segundo (inclui o desenho inicial) fica fora da media geral */
    static bool primeiro = true;
    if (primeiro) primeiro = false;
    else { soma_mibs += mibs; amostras++; }

    sb_t s;
    monta_taxa(&s, "Media: ", mibs);
    imprimir(&s, LIN_MEDIA1, COR_DESTAQ);


    uint32_t seg = div64(agora - t_inicio, tsc_hz);
    sb_init(&s);
    sb_str(&s, "TEMPO: ");
    sb_2d(&s, seg / 3600u); sb_str(&s, ":");
    sb_2d(&s, (seg / 60u) % 60u); sb_str(&s, ":");
    sb_2d(&s, seg % 60u);
    imprimir(&s, LIN_TEMPO, COR_TEXTO);
}

/* Chamado a cada JANELAS_POR_LEITURA janelas, no meio do loop de escrita
 * O que faz: Interrompe a escrita momentaneamente para calcular a velocidade de vazao e imprimir a atualizacao visual inteligente. */
static void atualizar_painel(void)
{
    uint64_t agora = ler_rdtsc();

    /* Leitura ao vivo: banda so deste trecho, medida em ciclos reais */
    uint32_t dmib = total_mib - mib_leit;
    uint32_t dt   = (uint32_t)(agora - t_leit);
    sb_t s;
    monta_taxa(&s, "Leitura atual: ", div64((uint64_t)dmib * tsc_hz, dt));
    imprimir_dif(&s, LIN_VIVO, COR_DESTAQ, cache_vivo);
    monta_total(&s);
    imprimir_dif(&s, LIN_TOTAL, COR_TEXTO, cache_total);

    if (agora - t_seg >= tsc_hz) fechar_segundo(agora);

    /* Recomeca o trecho DEPOIS de escrever na tela: a leitura ao vivo nao
     * inclui o tempo gasto na VGA (as medias de 1 s incluem). */
    mib_leit = total_mib;
    t_leit = ler_rdtsc();
}

void main(void)
{
    /* Limpa 2000 caracteres de buffer da VGA preenchendo com espaco em branco (0x0720). */
    for (int i = 0; i < 2000; i++) vga_buffer[i] = 0x0720;

    imprimir_txt("Calibrando TSC pelo PIT...", 0, COR_TEXTO);
    /* Executa o processo de calibracao para descobrir a frequencia do relogio do processador. */
    tsc_medido_hz = calibrar_tsc();
    tsc_hz = ajustar_tsc(tsc_medido_hz);
    desenhar_fixo();

    volatile uint8_t *zona = (volatile uint8_t *)BASE_ZONA;
    uint32_t cursor = 0, janelas = 0;
    /* Prepara os registradores avancados com o padrao de bytes. */
    carregar_padrao();
    t_inicio = t_seg = t_leit = ler_rdtsc();
    /* Loop principal de estresse.
     * O que faz: Roda para sempre escrevendo na RAM sequencialmente em blocos.
     * Como faz: Chama escrever_janela, avanca os ponteiros de memoria e zera se chegar 
     * no limite de tamanho. De forma periodica (condicao janelas >= JANELAS_POR_LEITURA) ele atualiza o UI. */
    while (true) {
        escrever_janela(zona + cursor, JANELA_BYTES);
        total_mib += JANELA_MIB;
        cursor += JANELA_BYTES;
        if (cursor >= ZONA_BYTES) cursor = 0;

        if (++janelas >= JANELAS_POR_LEITURA) {
            janelas = 0;
            atualizar_painel();
        }
    }
}
