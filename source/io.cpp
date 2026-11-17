#include <fstream>
#include <memory>
#include <sstream>

#include "httplib.h"
#include "io.hpp"

using namespace std;

// JSON <-> LEDGER
json blocoParaJson(const Bloco &bloco) {
    json txsJson = json::array();
    for (const auto &tx : bloco.txs) {
        txsJson.push_back({
            {"tipo", tx.tipo},
            {"origem", tx.origem},
            {"destino", tx.destino},
            {"valor", tx.valor},
            {"detalhe", tx.detalhe}
        });
    }

    return {
        {"indice", bloco.indice},
        {"timestamp", bloco.timestamp},
        {"hashAnterior", bloco.hashAnterior},
        {"proponente", bloco.proponente},
        {"txs", txsJson},
        {"hash", bloco.hash}
    };
}

Bloco jsonParaBloco(const json &j) {
    Bloco bloco;
    bloco.indice = j.at("indice").get<int>();
    bloco.timestamp = j.at("timestamp").get<long long>();
    bloco.hashAnterior = j.at("hashAnterior").get<string>();
    bloco.proponente = j.at("proponente").get<string>();

    for (const auto &txJson : j.at("txs")) {
        Transferencia tx;
        tx.tipo = txJson.value("tipo", "TRANSFERENCIA");
        tx.origem = txJson.at("origem").get<string>();
        tx.destino = txJson.at("destino").get<string>();
        tx.valor = txJson.at("valor").get<long long>();
        tx.detalhe = txJson.value("detalhe", "");
        bloco.txs.push_back(tx);
    }

    bloco.hash = j.at("hash").get<string>();
    return bloco;
}

json cadeiaParaJson(const vector<Bloco> &cadeiaOrigem) {
    json blocosJson = json::array();
    for (const auto &bloco : cadeiaOrigem) {
        blocosJson.push_back(blocoParaJson(bloco));
    }
    return blocosJson;
}

vector<Bloco> jsonParaCadeia(const string &texto) {
    vector<Bloco> novaCadeia;
    json blocosJson = json::parse(texto);

    for (const auto &blocoJson : blocosJson) {
        novaCadeia.push_back(jsonParaBloco(blocoJson));
    }
    return novaCadeia;
}

// PERSISTÊNCIA EM ledgers/ledger_<id>.json
void salvarLedger() {
    ofstream arquivoSaida(caminhoLedger);
    arquivoSaida << cadeiaParaJson(cadeia).dump(2);
}

void carregarLedger() {
    cadeia.clear();

    try {
        ifstream arquivoEntrada(caminhoLedger);
        if (arquivoEntrada) {
            stringstream conteudo;
            conteudo << arquivoEntrada.rdbuf();
            cadeia = jsonParaCadeia(conteudo.str());
        }
    } catch (...) {
        cadeia.clear();
    }

    Saldos saldos;
    string erro;

    if (cadeia.empty() || !cadeiaValida(cadeia, saldos, erro)) {
        if (!cadeia.empty()) {
            logar("Arquivo do LEDGER inválido (" + erro + "): recomeçando do gênesis");
        } else {
            logar("Nenhum LEDGER encontrado: criando bloco gênesis");
        }
        cadeia = {criarBlocoGenesis()};
        salvarLedger();
    } else {
        logar("LEDGER carregado do disco: " + to_string(cadeia.size()) + " blocos");
    }
}

// CLIENTE HTTP
// O timeout é o detector de falhas: nó que não responde é tratado como caído.
static unique_ptr<httplib::Client> criarClienteHttp(const NoDaRede &no) {
    auto cliente = make_unique<httplib::Client>(no.host, no.porta);

    cliente->set_connection_timeout(1, 0);
    cliente->set_read_timeout(2, 0);
    cliente->set_write_timeout(2, 0);

    return cliente;
}

static string normalizarCaminho(const string &caminho) {
    if (!caminho.empty() && caminho.front() == '/') return caminho;
    return "/" + caminho;
}

static httplib::Headers montarCabecalhos() {
    return {{"X-Server-Id", meuNome}};
}

static RespostaHttp converterResposta(const httplib::Result &resultado) {
    if (!resultado) return {};
    return {true, resultado->status, resultado->body};
}

RespostaHttp httpGet(const NoDaRede &no, const string &caminho) {
    return converterResposta(criarClienteHttp(no)->Get(normalizarCaminho(caminho), montarCabecalhos()));
}

RespostaHttp httpPost(const NoDaRede &no, const string &caminho, const string &corpo) {
    return converterResposta(criarClienteHttp(no)->Post(normalizarCaminho(caminho), montarCabecalhos(), corpo, "application/json"));
}

RespostaHttp httpPut(const NoDaRede &no, const string &caminho, const string &corpo) {
    return converterResposta(criarClienteHttp(no)->Put(normalizarCaminho(caminho), montarCabecalhos(), corpo, "application/json"));
}

RespostaHttp httpDelete(const NoDaRede &no, const string &caminho) {
    return converterResposta(criarClienteHttp(no)->Delete(normalizarCaminho(caminho), montarCabecalhos()));
}
