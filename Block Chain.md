# Ledger distribuído do consórcio

O **VaiJunto** é um sistema de caronas em que **cinco empresas** formam um consórcio. Nenhuma delas manda sozinha: cada empresa roda um **nó** com uma cópia completa do livro-razão (_ledger_), e um bloco novo só entra na cadeia se **3 dos 5 nós** concordarem.

**Um único programa** (`vaijunto_no`) executado cinco vezes. O id do nó (0 a 4) define quem ele é:

| Id  | Empresa (nó)    | Porta de exemplo |
| --- | --------------- | ---------------- |
| 0   | Skyrim          | 8081             |
| 1   | Solsthein       | 8082             |
| 2   | Apocrypha       | 8083             |
| 3   | Shivering Isles | 8084             |
| 4   | Deadlands       | 8085             |

![[01_rede.png]]

## 1 Parâmetros do sistema

|Parâmetro|Valor|Onde|Para que serve|
|---|---|---|---|
|`numeroServidores`|5|`util.hpp`|Tamanho do consórcio|
|`quorumMinimo`|3|`util.hpp`|Votos necessários (contando o próprio proponente)|
|`janelaAgrupamentoMs`|1000 ms|`protocolo.hpp`|Tempo que a transação mais antiga espera antes de fechar o bloco|
|`maximoTransacoesPorBloco`|50|`protocolo.hpp`|Fila cheia fecha o bloco na hora|
|`maximoTentativasPorLote`|3|`protocolo.cpp`|Tentativas de reenviar o lote após conflito|
|`segundosParaDescartarPendente`|10 s|`util.hpp`|Bloco preparado e não confirmado é descartado|
|`segundosEsperandoBloco`|60 s|`protocolo.cpp`|Quanto a requisição do cliente espera antes de dar 504|
|Vigia|3 s|`vigiarCadeia()`|Intervalo entre conferências da própria cadeia|
|Timeouts entre nós|conexão 1 s, leitura 2 s, escrita 2 s|`io.cpp`|Detector de falhas: quem não responde é tratado como caído|
|Pool de threads HTTP|128|`iniciarServidor()`|Sobrar thread para prepare/commit durante testes em massa|

## 2 Nó

![[02_camadas.png]]

|Arquivo|Papel|
|---|---|
|`sha256.hpp`|Implementação do SHA-256 do zero|
|`util.hpp` / `util.cpp`|`Bloco`, `Transferencia`, estado global, **regras do ledger** (`aplicarTransacoes`, `cadeiaValida`)|
|`io.hpp` / `io.cpp`|Bloco ↔ JSON, salvar/carregar `ledgers/ledger_<id>.json`, cliente HTTP para falar com os outros nós|
|`protocolo.hpp`|Contrato público: constantes do consenso e assinaturas das funções|
|`protocolo.cpp` (*)|Fila de transações, consenso, sincronização, vigia, servidor HTTP e rotas `/api/consorcio`|
|`protocoloCliente.cpp`|Rotas `/api/cliente` e autenticação HTTP Basic|
|`painel.hpp`|Painel web embutido (HTML/CSS/JS dentro de uma string C++)|
|`terminalServidor.cpp`|Console interativo do nó|
|`terminalCliente.cpp`|Programa separado (`vaijunto_cli`) para testar pela API|
|`main.cpp`|Lê os argumentos, carrega o ledger e sobe as threads e o servidor|

Bibliotecas externas (somente cabeçalhos): `httplib.h` (servidor e cliente HTTP) e `json.hpp` (nlohmann/json).

**O que o `main.cpp` faz, em ordem:**
1. Valida os argumentos: `./vaijunto_no <id 0-4> <end0> ... <end4>`, com os mesmos 5 endereços e na mesma ordem em todos os nós.
2. Preenche `nosDaRede` e define `meuId`, `meuNome` e `caminhoLedger`.
3. `carregarLedger()`: lê o arquivo, **revalida tudo** e, se estiver ausente ou inválido, recomeça do gênesis.
4. Dispara duas threads de fundo: `vigiarCadeia` e `iniciarTerminalServidor`.
5. `iniciarServidor(porta)`: atende HTTP até o processo morrer.

---

# 1. Blockchain: conceitos

**Ledger** é um registro de todas as transações e eventos ocorridos em um sistema. Geralmente, cada servidor guarda o seu próprio banco de dados, o que gera divergências entre diferentes servidores.

**DLT (Tecnologia de Ledger Distribuído)** é um livro-razão replicado e sincronizado entre vários nós de uma rede, sem banco de dados central.

O **blockchain** é um tipo específico de DLT onde os registros são agrupados em blocos encadeados cronologicamente por meio de criptografia, formando um histórico que não pode ser alterado sem ser percebido e que é auditável por todos os servidores.

|**Tipo**|**Quem pode ler e participar?**|**Quem valida os blocos?**|**Características e uso**|
|---|---|---|---|
|**Pública**|Qualquer pessoa com internet.|Qualquer nó anônimo via mineração.|Totalmente aberta, mais lenta, exige alto gasto computacional para evitar ataques.|
|**Privada**|Apenas membros de uma única empresa.|Só servidores controlados por essa empresa.|Rápida e sem taxas, mas centralizada.|
|**Consórcio**|Empresas autorizadas de um grupo fechado.|Os nós das empresas do consórcio, por votação.|Rápida, sem mineração pesada e descentralizada.|

> [!info] Onde o VaiJunto se encaixa É uma **blockchain de consórcio**: os 5 nós são conhecidos de antemão (`nomesServidores`), não há mineração e quem decide é a **votação com quórum 3/5**.

---

# 2. Hash

Uma função hash como o SHA-256 pega qualquer dado, um texto, uma reserva de carona ou um bloco inteiro, e transforma em uma sequência fixa de caracteres hexadecimais:

- É determinística: o mesmo dado gera exatamente o mesmo hash.
- É impossível descobrir o dado original olhando apenas para o hash.
- Mudar uma vírgula nos dados muda completamente o hash.

## 2.1 SHA-256

É uma função de hash criptográfica da família SHA-2. Recebe uma entrada de qualquer tamanho e devolve sempre uma saída de tamanho fixo de 256 bits, exibida geralmente como 64 caracteres hexadecimais.

- Independentemente de ser um "Oi" ou todo o histórico de viagens de um ano, o resultado sempre terá 64 caracteres.
- A mesma entrada sempre gera o mesmo hash.
- É computacionalmente inviável descobrir qual mensagem gerou o hash. Não tem como "descriptografar": a única forma de encontrar a entrada é por força bruta, que pode levar até $2^{256}$ tentativas.
- Alterar um bit na entrada muda cerca de 50% dos bits na saída, de forma imprevisível (_efeito avalanche_).
- É praticamente impossível encontrar duas entradas diferentes que produzam o mesmo hash.

**Como o SHA-256 funciona:**
![[08_sha256_fluxo.png]]

A função de entrada é uma só. Ela encadeia os quatro passos abaixo:

```cpp
inline std::string sha256Hex(const std::string &mensagem) {
    using namespace sha256;

    std::array<uint32_t, 8> estado = estadoInicial;
    std::vector<uint8_t> dados = preencherMensagem(mensagem);

    for (size_t inicio = 0; inicio < dados.size(); inicio += 64) {
        std::array<uint32_t, 64> palavras = expandirBloco(dados, inicio);
        embaralharRodadas(estado, palavras);
    }

    return formatarHex(estado);
}
```

### 1. Preenchimento (`preencherMensagem`)

O algoritmo exige que a mensagem tenha um tamanho múltiplo exato de **512 bits (64 bytes)**. Para ajustar qualquer texto a esse molde:

1. **Bit separador:** adiciona-se o byte `0x80` (`10000000` em binário) logo após o fim da mensagem para marcar onde o texto acabou.
2. **Zeros de preenchimento:** preenche-se com bytes `0x00` até que o tamanho total fique a exatos **8 bytes (64 bits)** de completar um múltiplo de 64 bytes (`tamanho % 64 == 56`).
3. **Tamanho original:** nos últimos 8 bytes, grava-se o tamanho original da mensagem **em bits** (formato _Big-Endian_).

![[09_sha256_preenchimento.png]]

```cpp
inline std::vector<uint8_t> preencherMensagem(const std::string &mensagem) {
    std::vector<uint8_t> dados(mensagem.begin(), mensagem.end());
    uint64_t tamanhoEmBits = static_cast<uint64_t>(mensagem.size()) * 8;

    // 1. Adiciona o bit 1 seguido de sete bits 0 (byte 0x80 = 10000000 em binário)
    dados.push_back(0x80);

    // 2. Preenche com bytes 0x00 até faltar exatamente 8 bytes (64 bits)
    // para completar um bloco de 64 bytes com o tamanho.
    while (dados.size() % 64 != 56) {
        dados.push_back(0x00);
    }

    // 3. Nos últimos 8 bytes, grava o tamanho original da mensagem em Big-Endian.
    for (int posicao = 7; posicao >= 0; --posicao) {
        uint8_t byteDoTamanho = static_cast<uint8_t>((tamanhoEmBits >> (posicao * 8)) & 0xFF);
        dados.push_back(byteDoTamanho);
    }
    return dados;
}
```

### 2. Expansão da mensagem (`expandirBloco`)

Cada bloco de 64 bytes é fatiado e esticado:

- **Palavras `0` a `15`:** os 64 bytes são agrupados de 4 em 4, formando **16 palavras de 32 bits** (`uint32_t`).
- **Palavras `16` a `63`:** as outras **48 palavras** são geradas misturando palavras anteriores por meio de rotações de bits ($\sigma_0$ e $\sigma_1$) e somas:

$$W_i = W_{i-16} + \sigma_0(W_{i-15}) + W_{i-7} + \sigma_1(W_{i-2}) \pmod{2^{32}}$$

> [!tip] Por que somar sem tratar overflow? Em C++, somar dois `uint32_t` descarta automaticamente o que ultrapassa 32 bits. Na criptografia, isso equivale exatamente à **soma modular $\pmod{2^{32}}$**.

### 3. Embaralhamento (`embaralharRodadas`)

O algoritmo mantém **8 variáveis de estado** (`a, b, c, d, e, f, g, h`), inicializadas com `estadoInicial`: constantes derivadas das raízes quadradas dos 8 primeiros números primos ($2, 3, 5, 7, 11, 13, 17, 19$).

Durante **64 rodadas** ($i = 0 \dots 63$), essas variáveis passam por um embaralhamento usando:

- **Constantes de rodada (`constantesRodada`):** 64 números derivados das raízes cúbicas dos 64 primeiros primos.
- **`escolherBit(e, f, g)` (_Choice_):** para cada posição de bit, se `e == 1`, escolhe o bit de `f`; se `e == 0`, escolhe o bit de `g`.
- **`maioriaDosBits(a, b, c)` (_Majority_):** para cada bit, vence o valor (`0` ou `1`) que aparecer em pelo menos 2 das 3 variáveis.
- **`rotacionarDireita`:** gira os bits para a direita; os bits que saem pela direita reaparecem na esquerda.

Em cada rodada:

$$T_1 = h + \Sigma_1(e) + \text{Ch}(e,f,g) + K_i + W_i \qquad T_2 = \Sigma_0(a) + \text{Maj}(a,b,c)$$

Depois, as variáveis "andam uma casa" (`h←g, g←f, f←e, e←d+T₁, d←c, c←b, b←a, a←T₁+T₂`). Ao final das 64 rodadas, os valores de `a..h` são **somados** ao estado acumulado do hash.

### 4. Formatação hexadecimal (`formatarHex`)

Quando todos os blocos de 64 bytes da mensagem terminam de ser processados, os 8 números finais de 32 bits (`estado[0]` até `estado[7]`) são convertidos para base hexadecimal (8 dígitos cada, preservando zeros à esquerda):

$$8 \text{ palavras} \times 8 \text{ dígitos hex} = 64 \text{ caracteres} \quad (256 \text{ bits})$$

### De onde vêm as constantes

O processo para transformar um número primo em uma constante hexadecimal de 32 bits segue três passos: calcula-se a raiz, descarta-se a parte inteira (ficando só com o que vem depois da vírgula) e multiplica-se a parte fracionária por $2^{32}$.

#### A) `estadoInicial` (raiz quadrada dos 8 primeiros primos)

- **Primeiro primo ($2$):**
    1. $\sqrt{2} \approx 1{,}414213562373095...$
    2. Parte fracionária: $0{,}414213562373095...$
    3. Multiplica-se por $2^{32}$ ($4.294.967.296$): $\approx 1.779.033.703$
    4. Em hexadecimal: **`0x6a09e667`** _(exatamente o `estadoInicial[0]` do código)_.
- **Segundo primo ($3$):**
    1. $\sqrt{3} \approx 1{,}732050807568877... \rightarrow 0{,}732050807568877 \times 2^{32} \approx 3.144.134.277$
    2. Em hexadecimal: **`0xbb67ae85`** _(exatamente o `estadoInicial[1]`)_.

```cpp
// Estado inicial (8 palavras de 32 bits = 256 bits no total).
// Derivado da parte fracionária das raízes quadradas dos 8 primeiros primos.
constexpr std::array<uint32_t, 8> estadoInicial = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};
```

#### B) `constantesRodada` (raiz cúbica dos 64 primeiros primos)

Usa os 64 primeiros primos ($2, 3, 5, 7, \dots, 311$), um para cada rodada do loop `for (int i = 0; i < 64; ++i)`:

- **Primeiro primo ($2$):**
    1. $\sqrt[3]{2} \approx 1{,}259921049894873...$
    2. Parte fracionária: $0{,}259921049894873...$
    3. Multiplica-se por $2^{32}$: $\approx 1.116.352.408$
    4. Em hexadecimal: **`0x428a2f98`** _(exatamente o `constantesRodada[0]` do código)_.
- **Segundo primo ($3$):**
    1. $\sqrt[3]{3} \approx 1{,}442249570307408... \rightarrow 0{,}442249570307408 \times 2^{32} \approx 1.899.447.441$
    2. Em hexadecimal: **`0x71374491`** _(exatamente o `constantesRodada[1]`)_.

```cpp
// 64 constantes derivadas da parte fracionária das raízes cúbicas dos 64 primeiros primos.
// Servem para adicionar aleatoriedade determinística em cada rodada.
constexpr std::array<uint32_t, 64> constantesRodada = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
```

## 2.2 Como conferir que a implementação está certa

Os vetores de teste oficiais do SHA-256 são públicos. Se `sha256Hex` devolver estes valores, o algoritmo está correto:

|Entrada|SHA-256 esperado|
|---|---|
|`""` (vazia)|`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`|
|`"abc"`|`ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad`|

---

# 3. Estrutura do bloco e encadeamento

## 3.1 As estruturas (`util.hpp`)

```cpp
// Transação no ledger (com ou sem movimentação de tokens)
struct Transferencia {
    string tipo = "TRANSFERENCIA"; // Ex: "TRANSFERENCIA", "AVALIACAO", "RESERVA"
    string origem;
    string destino;
    long long valor = 0;           // 0 = apenas registro no histórico (sem pagamento)
    string detalhe;                // Pode ser a nota da avaliação, id da viagem
};

// Um bloco do ledger
struct Bloco {
    int indice = 0;
    long long timestamp = 0;
    string hash;               // SHA-256 do conteúdo desse bloco
    string hashAnterior;
    string proponente;         // Nó que montou o bloco
    vector<Transferencia> txs;
};
```

## 3.2 O que entra no hash

A função `montarConteudoParaHash` transforma o bloco em **um texto único** e o `sha256Hex` calcula o hash dele. O formato é:

```
indice|timestamp|hashAnterior|proponente|tipo:origem>destino:valor:detalhe;tipo:origem>destino:valor:detalhe;...
```

Repare que `tipo` e `detalhe` também entram. Se não entrassem, alguém poderia trocar uma avaliação "nota 5" por "nota 1" sem quebrar o hash.

> [!example] Exemplo real: o hash do bloco gênesis Texto que vai para o SHA-256:
> 
> ```
> 0|0|0|SCP|MINT:SISTEMA>passageiro1:1000:genesis;MINT:SISTEMA>passageiro2:1000:genesis;
> ```
> 
> Resultado:
> 
> ```
> d726ccb8f7faa61d6b83ac1f9303c3254beab1b64953fc5f938e739d3740b2fe
> ```
> 
> Todos os nós calculam esse mesmo valor, é assim que sabem que partem da mesma base.

Essa é a opção da **concatenação simples**: todas as transações viram texto, são juntadas em sequência e entram no hash junto com o cabeçalho. Alterar qualquer centavo de qualquer transação muda o texto inteiro e o hash do bloco quebra.

> [!note] Usar Árvore de Merkle? A alternativa seria hashear as transações em pares até sobrar um único hash no topo (_Merkle Root_), que iria no cabeçalho. Ela permite provar que uma transação está no bloco sem baixar as outras. Para este protótipo a concatenação basta, então a Merkle eu vejo depois.

## 3.3 O bloco gênesis

É o bloco 0, **idêntico em todos os nós** (`criarBlocoGenesis`). Nenhum nó pode mudá-lo: `cadeiaValida` compara o gênesis recebido com o que ela mesma calcula.

|Campo|Valor|
|---|---|
|`indice`|0|
|`timestamp`|0 (fixo, para o hash ser igual em todos)|
|`hashAnterior`|`"0"`|
|`proponente`|`"SCP"`|
|`txs`|`MINT SISTEMA → passageiro1 (1000)` e `MINT SISTEMA → passageiro2 (1000)`|

É a única emissão de tokens do sistema: os 2000 tokens iniciais para os testes terem saldo.

## 3.4 O encadeamento

Cada bloco guarda o hash do anterior (`hashAnterior`) e esse valor entra no cálculo do próprio hash. Isso cria uma corrente: mexer em um bloco antigo muda o hash dele, e o `hashAnterior` do bloco seguinte deixa de bater.

![[03_cadeia.png]]

## 3.5 Quando um bloco é válido?

`blocoValidoApos(anterior, bloco, saldos)` confere, nesta ordem:

1. `bloco.indice == anterior.indice + 1`
2. `bloco.hashAnterior == anterior.hash`
3. `calcularHashDoBloco(bloco) == bloco.hash` (o hash foi recalculado, não confiado)
4. `aplicarTransacoes(...)` passa sobre os saldos (veja a seção 4)

`cadeiaValida(cadeia)` aplica isso à cadeia inteira: confere o gênesis, aplica as transações dele e valida cada bloco em sequência. Se tudo passar, devolve também os **saldos finais**.

## 3.6 Várias transações por bloco

**Um bloco pode ter mais de uma transação.** Esse é o motivo de existirem blocos em vez de encadear transação por transação: agrupar dezenas de operações em um único pacote evita rodar o consenso (várias idas e vindas de rede) a cada clique de usuário.

Quando o bloco é aceito pelo consórcio, **todas** as transações dentro do vetor são efetivadas juntas no estado (saldos) de todos os nós. Se o bloco for rejeitado, nenhuma delas entra na cadeia.

> [!tip] Detalhe do código Antes de montar o bloco, o nó proponente testa cada transação sozinha e **descarta as inválidas** (cada uma recebe o seu próprio 409). Assim, uma transação ruim não derruba as outras do lote. O "tudo ou nada" vale depois que o bloco já foi montado: ou o consenso aprova o bloco inteiro, ou nenhuma das transações dele entra.

---

# 4. Saldos, tokens e gasto duplo

O saldo de uma conta **não é um número guardado em tabela**. Ele é o resultado de **reaplicar todas as transações da cadeia desde o gênesis**. É isso que torna impossível "editar" um saldo: para mudá-lo, seria preciso mudar uma transação, o que quebra o hash.

> [!info] Contas nascem ao receber tokens Não existe cadastro: a conta passa a existir quando recebe tokens pela primeira vez. Enquanto isso, o saldo dela é 0.

## 4.1 Regras de `aplicarTransacoes`

|#|Regra|Erro devolvido|
|---|---|---|
|1|O bloco precisa ter pelo menos uma transação|`Sem transações`|
|2|`origem` deve ser um nome válido|`Origem inválida: ...`|
|3|`valor` não pode ser negativo|`Valor inválido`|
|4|Se `origem == "SISTEMA"`: só vale no **bloco 0**, com `valor > 0` e destino válido (cria tokens)|`Emissão inválida`|
|5|Se `valor == 0`: é um **evento** (sem tokens). Só confere o destino, se houver|`Destino inválido: ...`|
|6|Se `valor > 0`: transferência. Destino válido e diferente da origem|`Contas inválidas: a -> b`|
|7|A origem precisa ter saldo maior ou igual ao valor|`Saldo insuficiente: <origem>`|

**Nome de conta válido** (`nomeContaValido`): de 1 a 32 caracteres, só letras, números, `_` e `-`. Isso evita que um nome contenha `|`, `>`, `:` ou `;`, que são os separadores do texto que vai para o hash.

## 4.2 Tipos de transação

|Tipo|Valor|Efeito|
|---|---|---|
|`MINT`|> 0|Cria tokens. Só no gênesis, origem `SISTEMA`|
|`TRANSFERENCIA` (padrão)|> 0|Move tokens de `origem` para `destino`|
|`AVALIACAO`, `RESERVA`, outros|0|Apenas registra um evento no histórico (`detalhe` guarda nota, id da viagem...)|

O campo `tipo` é texto livre, e o que decide se há movimentação é o `valor`: qualquer transação com `valor > 0` move tokens, seja qual for o `tipo`.

## 4.3 Prevenção de gasto duplo

A ordem das transações dentro do bloco importa. Cada transação do lote é testada sobre os saldos **já alterados pelas anteriores**:

> [!example] Exemplo `passageiro1` tem 1000 tokens e manda duas transações no mesmo instante:
> 
> - `Tx 0`: pagar 800 para `motorista1`. Passa. Sobram 200.
> - `Tx 1`: pagar 800 para `motorista2`. **Falha** com `Saldo insuficiente: passageiro1`.
> 
> A `Tx 1` recebe 409 e só a `Tx 0` entra no bloco.

**Atomicidade:** `aplicarTransacoes` trabalha numa **cópia** dos saldos (`saldosTemporarios`). Só se todas as transações passam é que a cópia substitui os saldos reais. Se a 2ª falhar, a 1ª não "suja" nada.

---

# 5. Do pedido ao bloco: agrupamento de transações

O cliente não escreve direto na cadeia. Ele faz um `POST /api/cliente/reservas`, e a requisição **fica esperando** até o bloco que contém a transação dele ser decidido pelo consenso.

![[05b_consenso_simples.png]]

![[04_agrupamento.png]]

## 5.1 O ciclo de vida, em quatro etapas

### 1. Fila

`proporTransacao(tx)` recusa a conta reservada `SISTEMA` (400), cria um `PedidoDeTransacao` com um `promise` e o coloca em `filaDeTransacoes`. A thread da requisição fica esperando o `future` (até **60 s**, depois devolve 504).

A **thread de fundo** `agruparEProporBlocos` é criada na primeira transação (`call_once`).

### 2. Janela de agrupamento

A thread espera até que **uma** destas duas condições aconteça:

- passaram `janelaAgrupamentoMs` (**1000 ms**) desde que a transação **mais antiga** entrou na fila; ou
- a fila chegou a `maximoTransacoesPorBloco` (**50**), e então fecha na hora, sem esperar a janela.

```cpp
auto prazo = filaDeTransacoes.front()->enfileiradoEm + chrono::milliseconds(janelaAgrupamentoMs);
condicaoFila.wait_until(trava, prazo, [] { return filaDeTransacoes.size() >= maximoTransacoesPorBloco; });
```

| Cenário                              | O que acontece                                                             |
| ------------------------------------ | -------------------------------------------------------------------------- |
| Teste manual, 1 transação            | Vira 1 bloco; só espera a janela de 1 s                                    |
| Teste em massa, 40 transações juntas | Viram 1 bloco (só espera a janela)                                         |
| 120 transações de uma vez            | 50 + 50 fecham na hora; as 20 que sobram esperam a janela. Total: 3 blocos |

![[04b_cenarios.png]]

### 3. Seleção e ordenação

O lote sai da fila **na ordem de chegada**. `processarLote` tira uma foto do topo da cadeia e dos saldos e testa cada transação em sequência, como visto na seção 4.3. As inválidas recebem 409 próprio; as válidas viram **um único bloco**, com `proponente = meuNome` e `timestamp = time(nullptr)`.

### 4. O SHA-256 tranca todas elas juntas

O hash do bloco cobre cabeçalho e todas as transações (seção 3.2). O bloco segue para `executarConsenso` (seção 6).

## 5.2 O que o cliente recebe

| Código  | Quando                                                                                                                |
| ------- | --------------------------------------------------------------------------------------------------------------------- |
| **200** | Bloco confirmado. Corpo: `ok`, `id` (hash do bloco), `bloco` (índice), `votos`, `posicaoNoBloco`, `transacoesNoBloco` |
| 400     | JSON inválido ou conta `SISTEMA`                                                                                      |
| 401     | Falta HTTP Basic                                                                                                      |
| 403     | `origem` do corpo diferente da conta autenticada                                                                      |
| 409     | Transação inválida (saldo, conta...), conflito persistente ou sem quórum                                              |
| 500     | Erro inesperado ao processar o lote                                                                                   |
| 503     | Cadeia local inválida, ou sem quórum com nó inacessível                                                               |
| 504     | Passou 60 s sem o bloco ser decidido                                                                                  |

> [!warning] Na teoria cada nó guarda a transação numa _mempool_, confere a assinatura digital e propaga a fila. **Falta:**
> 
> - A fila existe só no nó que recebeu o pedido. Ela **não é propagada** aos outros nós.
> - **Não há assinatura digital.** A "identidade" é só o nome da conta enviado por HTTP Basic.
> - Quem propõe o bloco é sempre o nó que recebeu o pedido, e não há rodízio de líder.

---

# 6. Consenso: prepare, commit e rollback

Como não há servidor central, os cinco nós precisam concordar sobre qual será o próximo bloco. O protocolo segue o desenho clássico de **commit em duas fases** (_prepare / commit / rollback_), mas em cima de **blocos**. O "id da transação" é o **hash do bloco**.

- **Interseção:** dois grupos de 3 nós, num total de 5, **sempre** têm pelo menos 1 nó em comum ($3 + 3 - 5 = 1$). Esse nó em comum só vota em **um bloco por índice**, então dois blocos diferentes **não conseguem** quórum no mesmo índice.
- **Tolerância a falhas:** o sistema continua aceitando blocos com até **2 nós fora do ar**. Com 3 caídos, não há quórum e os clientes recebem 503.

> [!warning] O protocolo implementado tolera nós que caem ou ficam lentos (falha por parada), mas não nós que mentem nos votos (falha bizantina), porque os votos não são assinados.

## 6.2 O caminho de um bloco

![[05_consenso.png]]

**Passo a passo (`executarConsenso`), do lado do proponente:**
1. **Reserva o índice local.** O proponente guarda o próprio bloco em `blocosPendentes`. Se outro bloco pendente já ocupa esse índice, devolve 409 e o lote será tentado de novo.
2. **PREPARE.** Manda `POST /api/consorcio/reservas/prepare` com o bloco em JSON a cada vizinho, **um de cada vez**. O próprio nó já vale **1 voto**.
3. **Conta os votos.** Cada vizinho que devolveu `200` com `voto: true` soma 1. Quem não respondeu (timeout ou recusou conexão) conta em `nosInacessiveis`.
4. **Sem quórum (menos de 3 votos):** manda `DELETE /reservas/<hash>` para **quem aprovou** (rollback), libera a reserva local e responde:
    - **503** se algum nó estava inacessível;
    - **409** se todos responderam (os "nãos" vêm de vizinhos que já têm outro bloco ali, então vale tentar de novo).
5. **Com quórum (3 ou mais):** grava o bloco no ledger local **somente se o topo da cadeia não mudou** durante a votação. Se mudou, faz rollback nos vizinhos e responde 409 (tentar de novo).
6. **COMMIT.** Manda `PUT /reservas/<hash>/commit` a quem aprovou e responde **200** ao cliente.

> [!tip] A ordem importa O proponente grava **antes** de avisar os vizinhos. Isso garante que, uma vez que o cliente recebe 200, o bloco já está no ledger de pelo menos um nó, e os outros alcançam pelo commit ou pela sincronização.

## 6.3 Um índice, um bloco pendente
Suponha que **Skyrim** e **Deadlands** recebam pedidos ao mesmo tempo e cada um monte o bloco 5. Sem proteção, cada um poderia juntar 3 votos para blocos **diferentes** no mesmo índice, e um deles seria desfeito depois de o cliente já ter recebido 200.

A regra `indiceReservadoPorOutroBloco` impede isso: **um nó só pode ter um bloco pendente por índice** (vale também para o proponente, que reserva o seu próprio). Quem chegar depois leva um "não" e tenta de novo.

## 6.4 Repetição em caso de conflito

Se a falha for **conflito de cadeia** (índice reservado, topo mudou, ou "nãos" sem ninguém caído), `processarLote` repete até **3 vezes**:

1. Espera `100 × tentativa + 40 × meuId` ms. A parte do `meuId` desencontra os nós, para não colidirem de novo.
2. Chama `sincronizarComVizinhos()` para se atualizar.
3. Revalida as transações do lote sobre o novo estado e remonta o bloco.

Se a última tentativa também falhar, cada cliente recebe o erro.

## 6.5 O lado dos vizinhos

Cada vizinho trata o bloco que chegou como um **pendente**, que termina em um de dois lugares: no ledger (commit) ou descartado (rollback ou 10 s).

![[06_pendente.png]]

**PREPARE (`prepararBloco`)** só vota "sim" se:

- nenhum outro bloco pendente ocupa o mesmo índice;
- a **cadeia dele própria** está válida;
- o bloco **encaixa** na cadeia dele (índice, hashAnterior, hash recalculado e transações, tudo conferido por conta própria).

**Expiração:** o prazo de 10 s é conferido por `descartarPendentesExpirados()` toda vez que alguém mexe nos pendentes (prepare, commit, rollback, status, listagem), e não por um relógio rodando sozinho.

**COMMIT (`confirmarBloco` → `anexarBloco`)** devolve um destes resultados:

|`ResultadoAnexar`|Significado|HTTP no commit|
|---|---|---|
|`Anexado`|Gravou agora no fim da cadeia|200|
|`JaTinha`|O bloco já estava na cadeia (commit repetido ou já sincronizado)|200|
|`Conflito`|Já existe **outro** bloco naquele índice, ou ele não encaixa|409|
|`Atrasado`|Falta algum bloco antes dele. O nó **sincroniza** e tenta de novo|409 se ainda falhar|
|`Desconhecida`|Nunca foi preparado (ou o pendente expirou)|404|

**ROLLBACK (`desfazerBloco`):**

|`ResultadoDesfazer`|Significado|HTTP|
|---|---|---|
|`Desfeita`|Pendente descartado|200|
|`Inexistente`|Nada a desfazer (idempotente)|200|
|`JaConfirmada`|Bloco já está na cadeia: **o ledger é imutável**|409|

## 6.6 E se algo falhar?

|Situação|O que acontece|
|---|---|
|1 a 2 vizinhos caídos|O quórum de 3 ainda é alcançado. O nó que voltar se atualiza pela sincronização|
|3 ou mais caídos|Sem quórum: o cliente recebe **503** com o número de nós inacessíveis|
|Vizinho aprova e cai antes do commit|O pendente dele expira em 10 s. Ao voltar, o vigia o sincroniza com a cadeia mais longa|
|Commit se perde no caminho|Igual ao anterior: a sincronização (a cada 3 s) corrige|
|Proponente cai depois do prepare, antes de gravar|Ninguém recebe commit, os pendentes expiram e o bloco não existe. O cliente não recebeu resposta|
|Proponente grava e cai antes dos commits|Os vizinhos descartam o pendente, mas ao sincronizar adotam a cadeia dele (mais longa e válida): o bloco aparece|
|Dois proponentes ao mesmo tempo|A reserva de índice deixa só um avançar. O outro tenta de novo (até 3 vezes)|

> [!info] Linha do tempo de um pedido normal 0 ms: chega o POST → até 1000 ms: janela de agrupamento → prepare nos 4 vizinhos (um a um) → commit → resposta 200. Com um vizinho "pendurado", cada prepare pode custar até os timeouts do `io.cpp` (1 s de conexão + 2 s de leitura), por isso o terminal cliente espera até **15 s**.

---

# 7. Sincronização e vigia contra adulteração

## 7.1 Sincronização

`sincronizarComVizinhos()` visita cada vizinho: baixa a cadeia (`GET /chain`), **valida por inteiro** e, se for mais longa que a local, **adota**.

**A regra é: adota-se a cadeia MAIS LONGA que for VÁLIDA.** Uma cadeia inválida é rejeitada e vai para o log (`Cadeia de X REJEITADA`).

Ela é chamada por: `vigiarCadeia` (a cada 3 s), pelo `confirmarBloco` quando o nó está atrasado, pelo retry de lotes em conflito e pelo comando `sincronizar`.

## 7.2 O vigia

`vigiarCadeia()` roda numa thread de fundo e, a cada 3 s:

1. Confere se a **própria** cadeia ainda é válida. Se não for, registra `ADULTERACAO DETECTADA`, **volta ao gênesis**, salva o arquivo.
2. Chama `sincronizarComVizinhos()` e, com isso, o nó se refaz a partir das outras cortes.

![[07_sincronizacao.png]]

## 7.3 Demonstração de adulteração

O comando `adulterar [indice]` (no terminal do servidor ou do cliente) faz `cadeia[indice].txs[0].valor += 1000` e salva o arquivo, **sem recalcular o hash**. Resultado:

1. `calcularHashDoBloco` já não bate com `bloco.hash` → `cadeiaValida` falha com `Bloco N: Hash inválido`.
2. O painel marca esse nó como **cadeia adulterada** (estrela vermelha).
3. Em até 3 s o vigia detecta, descarta a cópia ruim e se ressincroniza com os vizinhos.

> [!warning] O que o vigia NÃO pega Quem alterasse um bloco **e recalculasse todos os hashes seguintes** produziria uma cadeia internamente válida. Como não há assinaturas e o desempate só olha o **tamanho**, duas cadeias válidas de mesmo tamanho e conteúdo diferente não se resolvem sozinhas (o painel mostraria o nó como "diferente"). É uma das razões para as assinaturas da seção 11.

---

# 8. API REST

Dois prefixos: **`/api/consorcio`** (nó ↔ nó e observação) e **`/api/cliente`** (usuários). Todas as respostas são JSON. Cada resposta leva o cabeçalho `X-Server-Id` com o nome do nó, e há CORS liberado (`*`), para o painel conseguir consultar os outros nós direto do navegador.

## 8.1 `/api/consorcio`

| Método e rota                                                                       | O que faz                                                                    |
| ----------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| `GET /health` e `GET /vivo`                                                         | `{no, altura, hashTopo, valida}` (e `erro` se inválida)                      |
| `GET /chain`                                                                        | A cadeia inteira em JSON                                                     |
| `GET /saldos`                                                                       | Saldo de cada conta (409 se a cadeia estiver inválida)                       |
| `GET /peers`                                                                        | Lista dos 5 nós `{nome, url, eu}`                                            |
| `GET /reservas`                                                                     | Blocos **pendentes** (`PREPARADA`) e **confirmados** (`CONFIRMADA`) deste nó |
| `POST /reservas/prepare`                                                            | Fase 1: recebe um bloco, responde `{voto, id, motivo}`                       |
| `PUT /reservas/{hash}/commit`                                                       | Fase 2: confirma o bloco pendente                                            |
| `DELETE /reservas/{hash}`                                                           | Rollback do pendente                                                         |
| `GET /reservas/{hash}/status`                                                       | `PREPARADA`, `CONFIRMADA` ou `DESCONHECIDA` (404)                            |
| `POST /sincronizar`                                                                 | Força a sincronização e devolve a altura                                     |
| `POST /adulterar?indice=N`                                                          | Corrompe um bloco de propósito (**só para demo**)                            |
|  `GET /trechos`, `PUT /reservas/{id}/cancel`, `PUT /reservas/{id}/cancel-motorista` | Devolvem **501** (dependem de caronas no ledger)                             |

## 8.2 `/api/cliente`

Todas exigem **HTTP Basic** (`Authorization: Basic base64(conta:senha)`).

| Método e rota                                                                                               | O que faz                                                                                                    |
| ----------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `GET /eu`                                                                                                   | `{id, nome, saldo, servidor}` da conta autenticada                                                           |
| `POST /reservas`                                                                                            | Cria uma transação. Corpo: `{"tipo","destino","valor","detalhe"}`. A **origem é sempre a conta autenticada** |
| `GET /reservas`                                                                                             | Histórico da conta (transações em que ela é origem ou destino, sem o gênesis)                                |
| `POST /usuarios`                                                                                            | 501: o cadastro por chave pública ainda não existe                                                           |
| `GET /itinerarios`, `POST /rotas`, `POST` e `GET /caronas`, `DELETE /caronas/{id}`, `DELETE /reservas/{id}` | 501                                                                                                          |

**Exemplo com `curl`:**

```bash
# saldo
curl -u passageiro1:x http://127.0.0.1:8081/api/cliente/eu

# pagamento de 30 tokens
curl -u passageiro1:x -X POST http://127.0.0.1:8081/api/cliente/reservas \
     -d '{"destino":"motorista1","valor":30,"detalhe":"corrida-42"}'

# evento sem tokens (valor 0)
curl -u passageiro1:x -X POST http://127.0.0.1:8081/api/cliente/reservas \
     -d '{"tipo":"AVALIACAO","destino":"motorista1","detalhe":"nota 5"}'
```

Resposta de sucesso:

```json
{ "ok": true, "id": "<hash do bloco>", "bloco": 1, "votos": 5,
  "posicaoNoBloco": 0, "transacoesNoBloco": 1 }
```

---

# 9. Terminais e painel web

## 9.1 Console do servidor (`terminalServidor.cpp`)

Roda dentro de cada nó, **só se a entrada for um terminal** (`isatty`). Digite `ajuda` para a lista.

|Comando|O que mostra ou faz|
|---|---|
|`estado`|Resumo: altura, topo, validade, pendentes, quórum, verbose|
|`cadeia [n]`|Últimos `n` blocos (padrão 10)|
|`bloco <indice>`|Um bloco completo em JSON|
|`saldos`|Saldo de cada conta segundo este nó|
|`pendentes`|Blocos preparados e ainda não confirmados|
|`requisicoes [n]`|Últimas `n` requisições recebidas (padrão 20)|
|`verbose on\|off`|Mostra também GET/OPTIONS (e corpos) ao vivo. Por padrão só mostra as demais|
|`peers`|Pergunta aos outros nós se estão vivos, com altura e validade|
|`sincronizar`|Busca a cadeia mais longa e válida dos vizinhos|
|`adulterar [indice]`|Corrompe um bloco de propósito|
|`sair`|Encerra o nó|

## 9.2 Terminal cliente (`vaijunto_cli`)

Programa **separado** do nó, que fala só pela API. Sem argumentos usa `127.0.0.1:8081` a `8085`; ou passe até 5 endereços `host:porta`.

|Comando|O que faz|
|---|---|
|`usuario <nome>`|Escolhe a conta atual (ela "nasce" ao receber tokens)|
|`eu`|Saldo e servidor da conta|
|`dar <conta> <valor>`|"Torneira": transfere de `passageiro1` ou `passageiro2` (as únicas com saldo inicial)|
|`pagar <destino> <valor> [obs]`|Transferência da conta atual|
|`evento <tipo> <destino\|-> [obs]`|Registro sem tokens (AVALIACAO, RESERVA...)|
|`historico`|Transações da conta atual|
|`saldos` / `cadeia`|Saldos e blocos segundo o nó atual|
|`status <hash>`|Estado de um bloco (`PREPARADA` / `CONFIRMADA`)|
|`nos`|Quais nós estão vivos e com que altura|
|`usar <0-4>`|Troca o nó que recebe os comandos|
|`sincronizar` / `adulterar [indice]`|Mandam o nó atual sincronizar ou se corromper|

## 9.3 Painel web (`painel.hpp`)

Abra `http://host:porta/` de qualquer nó. A página consulta **diretamente os 5 nós** a cada 2 s, sem recarregar.

| Elemento                 | Origem dos dados                | O que mostra                                                                          |
| ------------------------ | ------------------------------- | ------------------------------------------------------------------------------------- |
| Constelação (5 estrelas) | `/peers` + `/health` de cada nó | Uma estrela por empresa. O nó em que você está tem um sigilo em volta                 |
| HUD superior             | `/health` do nó local           | Nome da corte, altura (nº de blocos) e se a cadeia está **Íntegra** ou **Adulterada** |
| Tokens em circulação     | `/saldos`                       | Soma de todos os saldos                                                               |
| Barras inferiores        | `/health` de todos              | Cortes no ar, cadeias íntegras e em sincronia (mesmo `hashTopo`)                      |
| Contas                   | `/saldos`                       | Saldo de cada conta                                                                   |
| Crônica dos blocos       | `/chain`                        | Blocos do mais novo para o mais antigo, com elo, selo e transações                    |

-----

# 10. Como rodar e demonstrar

## 10.1 Compilar

Com `httplib.h` e `json.hpp` na pasta (ou no include path):

```bash
g++ -std=c++17 -O2 -pthread main.cpp util.cpp io.cpp protocolo.cpp protocoloCliente.cpp \
    terminalServidor.cpp -o vaijunto_no

g++ -std=c++17 -O2 -pthread terminalCliente.cpp -o vaijunto_cli
```

## 10.2 Subir os cinco nós

Em **cinco terminais**, na mesma pasta de trabalho (cada nó grava em `ledgers/ledger_<id>.json`):

```bash
./vaijunto_no 0 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 127.0.0.1:8084 127.0.0.1:8085
./vaijunto_no 1 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 127.0.0.1:8084 127.0.0.1:8085
# ... ids 2, 3 e 4, com os mesmos cinco endereços
```

Em outro terminal: `./vaijunto_cli`. E abra `http://127.0.0.1:8081/` no navegador.

## 10.3 Roteiro de demonstração

|#|O que fazer|O que observar|
|---|---|---|
|1|`usuario passageiro1` → `eu`|Saldo 1000 (vem do gênesis)|
|2|`pagar motorista1 30 corrida-42`|Espera ~1 s (janela). 200 com `votos: 5`. O painel ganha o bloco 1 em todas as estrelas|
|3|`evento AVALIACAO motorista1 nota 5`|Bloco sem movimentar tokens|
|4|`pagar motorista1 5000`|**409** `Saldo insuficiente`. Nenhum bloco é criado|
|5|**Rajada:** 30 `curl` em paralelo (veja abaixo)|Poucos blocos com várias transações (`transacoesNoBloco`), e não 30 blocos|
|6|Derrube **2 nós** (Ctrl+C) e pague de novo|Continua funcionando. Estrelas cinza no painel|
|7|Derrube um **terceiro**|**503** com `inacessiveis` no corpo. Sem quórum, nada é gravado|
|8|Suba os nós de volta|Eles sincronizam sozinhos (log `Sincronizado com ...`) e as barras voltam a 5/5|
|9|`adulterar 1` em um nó|Estrela vermelha. Em até 3 s: `ADULTERACAO DETECTADA`, volta ao gênesis, ressincroniza|

**Rajada do passo 5:**

```bash
for i in $(seq 1 30); do
  curl -s -u passageiro1:x -X POST http://127.0.0.1:8081/api/cliente/reservas \
       -d '{"destino":"motorista1","valor":1}' &
done; wait
```

---

# 11. Limitações e próximos passos

### Criptografia assimétrica e assinatura digital

Para garantir que ninguém gaste créditos de outro usuário nem forje avaliações em nome de passageiros que não viajaram, usa-se criptografia de chaves assimétricas (ex.: ECDSA / Ed25519):

- **Chave privada:** fica com o usuário. Serve para **assinar** transações.
- **Chave pública:** é a identidade pública do usuário na rede (vincula saldos e reputação ao usuário, e não à empresa).

No código isso corresponde aos campos comentados `// string id;` e `// string assinatura;` em `Transferencia`, e à rota `POST /usuarios` (hoje 501: "a identidade passará a ser a chave pública do usuário").

### Escrow, split atômico e reputação

- **Escrow (custódia) e split atômico:** na reserva, os tokens saem do passageiro e ficam travados no protocolo. Ao fim da viagem, **uma única transação atômica** divide e envia os valores para todos os motoristas dos trechos (ou todos recebem, ou nada é efetivado). A base já existe: um bloco é "tudo ou nada".
- **Reputação vinculada ao usuário:** avaliações e _no-shows_ são transações gravadas na blockchain apontando para a chave pública do usuário. Mesmo que o motorista mude de empresa, o histórico continua acessível e inalterável. Hoje o tipo `AVALIACAO` (valor 0) já pode ser **registrado**, mas ainda não é assinado nem calculado como reputação.
- **Caronas, rotas e cancelamentos:** as rotas `/caronas`, `/rotas`, `/itinerarios`, `cancel` e `cancel-motorista` respondem 501. O cancelamento virará uma transação `CANCELAR` no ledger.

## 11.2 Limitações conhecidas do protótipo

|Limitação|Por quê importa|
|---|---|
|Senha ignorada e sem assinaturas|Qualquer um age em nome de qualquer conta|
|Consenso tolera quedas, **não** nós maliciosos|Votos não são assinados (não é BFT)|
|Desempate só por tamanho|Duas cadeias válidas de mesmo tamanho e conteúdo diferente não se reconciliam|
|`tipo` e `detalhe` não são validados|Um `detalhe` com `;` ou `:` pode tornar ambígua a serialização que vai para o hash|
|Validação completa da cadeia a cada operação|Custo $O(n)$ em cada requisição, prepare e leitura de saldos. Não escala para cadeias grandes|
|Ledger regravado por inteiro a cada bloco|`salvarLedger()` reescreve o arquivo todo|
|Prepare **sequencial** nos vizinhos|A latência soma, e um nó lento atrasa todos|
|`adulterar` e `sair` expostos|Úteis na demonstração, perigosos fora dela|
|CORS aberto (`*`)|Conveniente para o painel, inadequado para produção|
