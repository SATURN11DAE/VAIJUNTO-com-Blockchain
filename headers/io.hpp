#pragma once

#include <string>
#include <vector>

#include "json.hpp"
#include "util.hpp"

using json = nlohmann::json;

// Conversão entre os tipos do ledger e JSON
json blocoParaJson(const Bloco &bloco);
Bloco jsonParaBloco(const json &j);
json cadeiaParaJson(const std::vector<Bloco> &cadeia);
std::vector<Bloco> jsonParaCadeia(const std::string &texto);

// Persistência em disco
void salvarLedger();
void carregarLedger();

// Cliente HTTP para falar com os outros nós.
// Esconde a biblioteca httplib: quem usa só vê "alcancouNo / codigoStatus / corpo".
// Toda requisição leva o cabeçalho X-Server-Id (quem está falando).
struct RespostaHttp {
    bool alcancouNo = false;  // false = nó inacessível (recusou conexão ou estourou timeout)
    int codigoStatus = 0;
    std::string corpo;
};

RespostaHttp httpGet(const NoDaRede &no, const std::string &caminho);
RespostaHttp httpPost(const NoDaRede &no, const std::string &caminho, const std::string &corpo);
RespostaHttp httpPut(const NoDaRede &no, const std::string &caminho, const std::string &corpo = "");
RespostaHttp httpDelete(const NoDaRede &no, const std::string &caminho);
