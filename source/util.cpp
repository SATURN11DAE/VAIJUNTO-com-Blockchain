#include <cctype>
#include <iostream>
#include <sstream>

#include "util.hpp"
#include "sha256.hpp"

using namespace std;

// Estado global
vector<NoDaRede> nosDaRede;
int meuId = 0;
string meuNome;
string caminhoLedger;

vector<Bloco> cadeia;
mutex mutexCadeia;

static mutex mutexLog; // Evita log misturado entre threads

void logar(const string &mensagem) {
    lock_guard<mutex> trava(mutexLog);
    cout << "[" << meuNome << "] " << mensagem << endl;
}

// Regras do LEDGER

// Evita caracteres que quebrariam o formato do texto que vai para o hash
bool nomeContaValido(const string &nome) {
    if (nome.empty() || nome.size() > 32) {
        return false;
    }

    for (char caractere : nome) {
        if (!isalnum(static_cast<unsigned char>(caractere)) && caractere != '_' && caractere != '-') {
            return false;
        }
    }
    return true;
}

// Texto do bloco que entra no SHA-256 (inclui tipo e detalhe para evitar adulteração)
static string montarConteudoParaHash(const Bloco &bloco) {
    ostringstream saida;
    saida << bloco.indice << '|' << bloco.timestamp << '|'
          << bloco.hashAnterior << '|' << bloco.proponente << '|';

    for (const auto &tx : bloco.txs) {
        saida << tx.tipo << ':' << tx.origem << '>' << tx.destino
              << ':' << tx.valor << ':' << tx.detalhe << ';';
    }

    return saida.str();
}

string calcularHashDoBloco(const Bloco &bloco) {
    return sha256Hex(montarConteudoParaHash(bloco));
}

// Bloco 0: igual em todos os nós.
Bloco criarBlocoGenesis() {
    Bloco genesis;
    genesis.indice = 0;
    genesis.timestamp = 0;
    genesis.hashAnterior = "0";
    genesis.proponente = "SCP";

    // Emissão inicial para os testes terem saldo para transferir
    genesis.txs = {
        {"MINT", "SISTEMA", "passageiro1", 1000, "genesis"},
        {"MINT", "SISTEMA", "passageiro2", 1000, "genesis"}
    };

    genesis.hash = calcularHashDoBloco(genesis);
    return genesis;
}

bool aplicarTransacoes(const vector<Transferencia> &txs, int indice, Saldos &saldos, string &erro) {
    if (txs.empty()) {
        erro = "Sem transações";
        return false;
    }

    // Cópia temporária: se a 2ª tx falhar, a 1ª não suja os saldos
    Saldos saldosTemporarios = saldos;

    for (const auto &tx : txs) {
        if (!nomeContaValido(tx.origem)) {
            erro = "Origem inválida: " + tx.origem;
            return false;
        }

        if (tx.valor < 0) {
            erro = "Valor inválido";
            return false;
        }

        // Emissão inicial permitida apenas no Bloco 0
        if (tx.origem == "SISTEMA") {
            if (indice != 0 || tx.valor <= 0 || !nomeContaValido(tx.destino)) {
                erro = "Emissão inválida";
                return false;
            }

            saldosTemporarios[tx.destino] += tx.valor;
            continue;
        }

        // Registro sem movimentação financeira (ex: AVALIACAO, RESERVA)
        if (tx.valor == 0) {
            if (!tx.destino.empty() && !nomeContaValido(tx.destino)) {
                erro = "Destino inválido: " + tx.destino;
                return false;
            }
            continue;
        }

        // Transferência normal de tokens (valor > 0)
        if (!nomeContaValido(tx.destino) || tx.origem == tx.destino) {
            erro = "Contas inválidas: " + tx.origem + " -> " + tx.destino;
            return false;
        }

        auto entradaOrigem = saldosTemporarios.find(tx.origem);
        long long saldoOrigem = (entradaOrigem == saldosTemporarios.end()) ? 0 : entradaOrigem->second;

        if (saldoOrigem < tx.valor) {
            erro = "Saldo insuficiente: " + tx.origem;
            return false;
        }

        saldosTemporarios[tx.origem] -= tx.valor;
        saldosTemporarios[tx.destino] += tx.valor;
    }

    saldos = move(saldosTemporarios);
    return true;
}

bool blocoValidoApos(const Bloco &anterior, const Bloco &bloco, Saldos &saldos, string &erro) {
    if (bloco.indice != anterior.indice + 1) {
        erro = "Índice inválido";
        return false;
    }

    if (bloco.hashAnterior != anterior.hash) {
        erro = "Hash anterior inválido";
        return false;
    }

    if (calcularHashDoBloco(bloco) != bloco.hash) {
        erro = "Hash inválido";
        return false;
    }

    return aplicarTransacoes(bloco.txs, bloco.indice, saldos, erro);
}

bool cadeiaValida(const vector<Bloco> &cadeiaVerificada, Saldos &saldos, string &erro) {
    saldos.clear();

    if (cadeiaVerificada.empty()) {
        erro = "Cadeia vazia";
        return false;
    }

    Bloco genesis = criarBlocoGenesis();
    if (cadeiaVerificada[0].hash != genesis.hash ||
        cadeiaVerificada[0].hash != calcularHashDoBloco(cadeiaVerificada[0])) {
        erro = "Gênesis diferente";
        return false;
    }

    if (!aplicarTransacoes(cadeiaVerificada[0].txs, 0, saldos, erro)) {
        return false;
    }

    for (size_t i = 1; i < cadeiaVerificada.size(); i++) {
        if (!blocoValidoApos(cadeiaVerificada[i - 1], cadeiaVerificada[i], saldos, erro)) {
            erro = "Bloco " + to_string(i) + ": " + erro;
            return false;
        }
    }
    return true;
}
