#pragma once

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace sha256 {
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

    // Estado inicial (8 palavras de 32 bits = 256 bits no total).
    // Derivado da parte fracionária das raízes quadradas dos 8 primeiros primos.
    constexpr std::array<uint32_t, 8> estadoInicial = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Rotaciona os bits de 'valor' para a direita 'deslocamento' vezes.
    // Os bits que saem voltam na esquerda.
    inline uint32_t rotacionarDireita(uint32_t valor, int deslocamento) {
        return (valor >> deslocamento) | (valor << (32 - deslocamento));
    }

    // Para cada bit, se 'x' for 1 escolhe o bit de 'y'.
    // Se for 0, escolhe o de 'z'.
    inline uint32_t escolherBit(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (~x & z);
    }

    // Para cada bit, vence o valor (0 ou 1) que aparecer em pelo menos 2 das 3 variáveis.
    inline uint32_t maioriaDosBits(uint32_t x, uint32_t y, uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }

    // Prepara a mensagem para que seu tamanho seja um múltiplo de 64 bytes (512 bits).
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

    // PASSO 2: Pega um bloco de 64 bytes e expande para 64 palavras de 32 bits.
    inline std::array<uint32_t, 64> expandirBloco(const std::vector<uint8_t> &dados, size_t inicioDoBloco) {
        std::array<uint32_t, 64> palavras{};

        // As primeiras 16 palavras vêm diretamente dos 64 bytes do bloco.
        for (int i = 0; i < 16; ++i) {
            size_t posicao = inicioDoBloco + (i * 4);
            palavras[i] = (static_cast<uint32_t>(dados[posicao])     << 24) |
                          (static_cast<uint32_t>(dados[posicao + 1]) << 16) |
                          (static_cast<uint32_t>(dados[posicao + 2]) << 8)  |
                          (static_cast<uint32_t>(dados[posicao + 3]));
        }

        // As outras 48 palavras são geradas misturando palavras anteriores
        for (int i = 16; i < 64; ++i) {
            uint32_t sigma0 = rotacionarDireita(palavras[i - 15], 7) ^
                              rotacionarDireita(palavras[i - 15], 18) ^
                              (palavras[i - 15] >> 3);

            uint32_t sigma1 = rotacionarDireita(palavras[i - 2], 17) ^
                              rotacionarDireita(palavras[i - 2], 19) ^
                              (palavras[i - 2] >> 10);

            palavras[i] = palavras[i - 16] + sigma0 + palavras[i - 7] + sigma1;
        }

        return palavras;
    }

    // PASSO 3: Executa as 64 rodadas de embaralhamento sobre o estado atual do hash.
    inline void embaralharRodadas(std::array<uint32_t, 8> &estado, const std::array<uint32_t, 64> &palavras) {
        uint32_t a = estado[0], b = estado[1], c = estado[2], d = estado[3];
        uint32_t e = estado[4], f = estado[5], g = estado[6], h = estado[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t somatorio1 = rotacionarDireita(e, 6) ^ rotacionarDireita(e, 11) ^ rotacionarDireita(e, 25);
            uint32_t temporario1 = h + somatorio1 + escolherBit(e, f, g) + constantesRodada[i] + palavras[i];

            uint32_t somatorio0 = rotacionarDireita(a, 2) ^ rotacionarDireita(a, 13) ^ rotacionarDireita(a, 22);
            uint32_t temporario2 = somatorio0 + maioriaDosBits(a, b, c);

            h = g;
            g = f;
            f = e;
            e = d + temporario1;
            d = c;
            c = b;
            b = a;
            a = temporario1 + temporario2;
        }

        estado[0] += a; estado[1] += b; estado[2] += c; estado[3] += d;
        estado[4] += e; estado[5] += f; estado[6] += g; estado[7] += h;
    }

    // PASSO 4: Converte os 8 números finais de 32 bits em uma string hexadecimal de 64 caracteres.
    inline std::string formatarHex(const std::array<uint32_t, 8> &estado) {
        std::ostringstream fluxoHex;
        fluxoHex << std::hex << std::setfill('0');

        for (uint32_t palavra : estado) {
            fluxoHex << std::setw(8) << palavra;
        }

        return fluxoHex.str();
    }
}

// Função principal
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
