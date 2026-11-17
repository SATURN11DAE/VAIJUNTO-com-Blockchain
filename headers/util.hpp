#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace std;

// As empresas do consórcio. Os ids, de 0 a 4, são a posição nessa lista.
inline const vector<string> nomesServidores = {"Skyrim", "Solsthein", "Apocrypha", "Shivering Isles", "Deadlands"};
constexpr int numeroServidores = 5;
constexpr int quorumMinimo = 3;

// Prefixos da API
inline const string prefixoConsorcio = "api/consorcio";
inline const string prefixoCliente = "api/cliente";

// Bloco preparado e não confirmado dentro desse prazo (em segundos) é descartado.
constexpr int segundosParaDescartarPendente = 10;

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

// Endereço de um nó da rede
struct NoDaRede {
    string nome;
    string host;
    int porta = 0;
};

using Saldos = map<string, long long>; // Tokens por conta

// Estado global de um nó
extern vector<NoDaRede> nosDaRede;  // Todos os nós, incluindo este
extern int meuId;                   // Valor de 0 a 4
extern string meuNome;              // nomesServidores[meuId]
extern string caminhoLedger;        // ledgers/ledger_<id>.json

extern vector<Bloco> cadeia;        // O ledger deste nó
extern mutex mutexCadeia;           // Protege a cadeia. Nunca segurar durante chamada de rede.

void logar(const string &mensagem);

// Regras do LEDGER
bool nomeContaValido(const string &nome);
string calcularHashDoBloco(const Bloco &bloco);
Bloco criarBlocoGenesis(); // Bloco 0, igual em todos os nós

// Aplica transações sobre os saldos.
// Falha se alguma for inválida (ex.: saldo insuficiente). Sem gasto duplo.
bool aplicarTransacoes(const vector<Transferencia> &txs, int indice, Saldos &saldos, string &erro);

// O bloco encaixa depois do anterior? Se encaixar, atualiza os saldos.
bool blocoValidoApos(const Bloco &anterior, const Bloco &bloco, Saldos &saldos, string &erro);

// Verifica a cadeia inteira (hashes, elos e saldos) e devolve os saldos finais.
bool cadeiaValida(const vector<Bloco> &cadeia, Saldos &saldos, string &erro);
