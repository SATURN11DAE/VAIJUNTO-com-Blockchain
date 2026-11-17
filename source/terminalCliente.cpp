/*
 * Programa separado do nó: conversa com os nós pela API HTTP.
 *
 * Uso:
 *   ./vaijunto_cli                       (usa 127.0.0.1:8081 ... 8085)
 *   ./vaijunto_cli <end0> <end1> ...     (até 5 endereços host:porta, na ordem dos nós)
 *
 * A autenticação do nó só confere o NOME da conta (a senha é ignorada),
 * então "criar usuário" aqui é só escolher um nome.
 */
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "httplib.h"
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

struct Endereco {
    string host;
    int porta;
};

static vector<Endereco> nosDisponiveis;
static int indiceNoAtual = 0;
static string contaAtual;

struct Resposta {
    bool alcancouNo = false;
    int codigoStatus = 0;
    string corpo;
};

static Resposta chamar(int indiceNo, const string &metodo, const string &caminho,
                       const string &corpo = "", const string &conta = "") {
    const Endereco &no = nosDisponiveis[indiceNo];
    httplib::Client cliente(no.host, no.porta);
    cliente.set_connection_timeout(2, 0);
    cliente.set_read_timeout(15, 0);   // uma proposta espera os timeouts dos vizinhos
    if (!conta.empty()) cliente.set_basic_auth(conta, "teste");

    httplib::Result resultado;
    if (metodo == "GET") resultado = cliente.Get(caminho);
    else if (metodo == "POST") resultado = cliente.Post(caminho, corpo, "application/json");
    else if (metodo == "PUT") resultado = cliente.Put(caminho, corpo, "application/json");
    else resultado = cliente.Delete(caminho);

    if (!resultado) return {};
    return {true, resultado->status, resultado->body};
}

static Resposta chamarNoAtual(const string &metodo, const string &caminho,
                              const string &corpo = "", const string &conta = "") {
    return chamar(indiceNoAtual, metodo, caminho, corpo, conta);
}

static bool nomeContaValido(const string &nome) {
    if (nome.empty() || nome.size() > 32) return false;
    for (char c : nome) {
        if (!isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

// Mostra status e corpo em JSON legível. Devolve o JSON (ou null se não for JSON).
static json mostrar(const Resposta &resposta, bool imprimirCorpo = true) {
    if (!resposta.alcancouNo) {
        cout << "No inacessivel (" << nosDisponiveis[indiceNoAtual].host << ":"
             << nosDisponiveis[indiceNoAtual].porta << ")\n";
        return nullptr;
    }

    json corpoJson = json::parse(resposta.corpo, nullptr, false);
    cout << "HTTP " << resposta.codigoStatus << "\n";
    if (imprimirCorpo) {
        if (corpoJson.is_discarded()) cout << resposta.corpo << "\n";
        else cout << corpoJson.dump(2) << "\n";
    }
    return corpoJson.is_discarded() ? json(nullptr) : corpoJson;
}

static string resumirTransacao(const json &tx) {
    string texto = "[" + tx.value("tipo", "?") + "] " + tx.value("origem", "?") + " -> " +
                   tx.value("destino", "") + " (" + to_string(tx.value("valor", 0LL)) + ")";
    string detalhe = tx.value("detalhe", "");
    if (!detalhe.empty()) texto += " '" + detalhe + "'";
    return texto;
}

static bool exigirConta() {
    if (contaAtual.empty()) {
        cout << "Escolha uma conta antes: usuario <nome>\n";
        return false;
    }
    return true;
}

static bool lerValor(istringstream &argumentos, long long &valor) {
    string texto;
    if (!(argumentos >> texto)) return false;
    try {
        valor = stoll(texto);
        return valor >= 0;
    } catch (...) {
        return false;
    }
}

// Texto que sobrou da linha (usado como "detalhe")
static string restoDaLinha(istringstream &argumentos) {
    string resto;
    getline(argumentos, resto);
    size_t inicio = resto.find_first_not_of(' ');
    return inicio == string::npos ? "" : resto.substr(inicio);
}

static void mostrarAjuda() {
    cout << "Comandos do terminal de teste:\n"
         << "  usuario <nome>                  escolhe a conta (nasce ao receber tokens)\n"
         << "  eu                              saldo e servidor da conta atual\n"
         << "  dar <conta> <valor>             da tokens a uma conta (sai de passageiro1/passageiro2)\n"
         << "  pagar <destino> <valor> [obs]   transferencia da conta atual\n"
         << "  evento <tipo> <destino|-> [obs] registro sem tokens (AVALIACAO, RESERVA...)\n"
         << "  historico                       transacoes da conta atual\n"
         << "  saldos                          saldo de todas as contas (segundo o no atual)\n"
         << "  cadeia                          blocos do no atual\n"
         << "  status <hash>                   estado de um bloco (PREPARADA/CONFIRMADA)\n"
         << "  nos                             quais nos estao vivos e com que altura\n"
         << "  usar <0-4>                      troca o no que recebe os comandos\n"
         << "  sincronizar                     manda o no atual sincronizar\n"
         << "  adulterar [indice]              corrompe um bloco do no atual (demo)\n"
         << "  ajuda | sair\n";
}

static void comandoUsuario(istringstream &argumentos) {
    string nome;
    argumentos >> nome;
    if (!nomeContaValido(nome)) {
        cout << "Nome invalido (use letras, numeros, _ ou -; ate 32 caracteres)\n";
        return;
    }
    contaAtual = nome;
    Resposta resposta = chamarNoAtual("GET", "/api/cliente/eu", "", contaAtual);
    json corpoJson = mostrar(resposta, false);
    if (!corpoJson.is_null()) {
        cout << "Conta '" << contaAtual << "' | saldo " << corpoJson.value("saldo", 0LL) << "\n";
    }
}

static void comandoEu() {
    if (!exigirConta()) return;
    mostrar(chamarNoAtual("GET", "/api/cliente/eu", "", contaAtual));
}

static void enviarTransacao(const string &conta, const string &tipo, const string &destino,
                            long long valor, const string &detalhe, bool imprimir = true) {
    json corpoJson = {{"tipo", tipo}, {"destino", destino}, {"valor", valor}, {"detalhe", detalhe}};
    mostrar(chamarNoAtual("POST", "/api/cliente/reservas", corpoJson.dump(), conta), imprimir);
}

static void comandoDar(istringstream &argumentos) {
    string destino;
    long long valor = 0;
    argumentos >> destino;
    if (!nomeContaValido(destino) || !lerValor(argumentos, valor) || valor == 0) {
        cout << "Uso: dar <conta> <valor>\n";
        return;
    }

    // Só o gênesis cria tokens (passageiro1 e passageiro2 começam com 1000).
    // Então "dar" é uma transferência normal a partir de uma dessas duas contas.
    for (const string &doador : {string("passageiro1"), string("passageiro2")}) {
        if (doador == destino) continue;

        json corpoJson = {{"tipo", "TRANSFERENCIA"}, {"destino", destino}, {"valor", valor}, {"detalhe", "faucet"}};
        Resposta resposta = chamarNoAtual("POST", "/api/cliente/reservas", corpoJson.dump(), doador);

        if (!resposta.alcancouNo) {
            mostrar(resposta);
            return;
        }
        if (resposta.codigoStatus == 200) {
            cout << doador << " -> " << destino << ": " << valor << " tokens (bloco confirmado)\n";
            return;
        }
        cout << doador << " nao conseguiu (HTTP " << resposta.codigoStatus << "): "
             << json::parse(resposta.corpo, nullptr, false).value("erro", "?") << "\n";
    }
    cout << "Nenhum doador conseguiu. Veja 'nos' e 'saldos'.\n";
}

static void comandoPagar(istringstream &argumentos) {
    if (!exigirConta()) return;
    string destino;
    long long valor = 0;
    argumentos >> destino;
    if (!nomeContaValido(destino) || !lerValor(argumentos, valor) || valor == 0) {
        cout << "Uso: pagar <destino> <valor> [obs]\n";
        return;
    }
    enviarTransacao(contaAtual, "TRANSFERENCIA", destino, valor, restoDaLinha(argumentos));
}

static void comandoEvento(istringstream &argumentos) {
    if (!exigirConta()) return;
    string tipo, destino;
    argumentos >> tipo >> destino;
    if (tipo.empty() || destino.empty()) {
        cout << "Uso: evento <tipo> <destino|-> [obs]\n";
        return;
    }
    enviarTransacao(contaAtual, tipo, destino == "-" ? "" : destino, 0, restoDaLinha(argumentos));
}

static void comandoHistorico() {
    if (!exigirConta()) return;
    json lista = mostrar(chamarNoAtual("GET", "/api/cliente/reservas", "", contaAtual), false);
    if (!lista.is_array()) return;
    if (lista.empty()) cout << "Sem transacoes.\n";
    for (const auto &item : lista) {
        cout << "bloco " << item.value("bloco", 0) << " | " << resumirTransacao(item)
             << " | id " << item.value("id", "").substr(0, 8) << "\n";
    }
}

static void comandoSaldos() {
    json saldos = mostrar(chamarNoAtual("GET", "/api/consorcio/saldos"), false);
    if (!saldos.is_object()) return;
    for (auto it = saldos.begin(); it != saldos.end(); ++it) {
        cout << "  " << left << setw(20) << it.key() << it.value() << "\n";
    }
}

static void comandoCadeia() {
    json blocos = mostrar(chamarNoAtual("GET", "/api/consorcio/chain"), false);
    if (!blocos.is_array()) return;
    for (const auto &bloco : blocos) {
        string txs;
        for (const auto &tx : bloco["txs"]) txs += (txs.empty() ? "" : " ; ") + resumirTransacao(tx);
        cout << "#" << bloco.value("indice", 0) << " " << bloco.value("hash", "").substr(0, 8)
             << " por " << bloco.value("proponente", "?") << " | " << txs << "\n";
    }
}

static void comandoStatus(istringstream &argumentos) {
    string idBloco;
    argumentos >> idBloco;
    if (idBloco.empty()) {
        cout << "Uso: status <hash completo do bloco>\n";
        return;
    }
    mostrar(chamarNoAtual("GET", "/api/consorcio/reservas/" + idBloco + "/status"));
}

static void comandoNos() {
    for (size_t i = 0; i < nosDisponiveis.size(); i++) {
        Resposta resposta = chamar(static_cast<int>(i), "GET", "/api/consorcio/vivo");
        cout << (static_cast<int>(i) == indiceNoAtual ? "* " : "  ") << i << " "
             << left << setw(22) << (nosDisponiveis[i].host + ":" + to_string(nosDisponiveis[i].porta));

        if (!resposta.alcancouNo) {
            cout << "INACESSIVEL\n";
            continue;
        }
        json corpoJson = json::parse(resposta.corpo, nullptr, false);
        if (corpoJson.is_discarded()) {
            cout << "HTTP " << resposta.codigoStatus << " (corpo ilegivel)\n";
            continue;
        }
        cout << left << setw(16) << corpoJson.value("no", "?")
             << "altura " << corpoJson.value("altura", 0)
             << " | " << (corpoJson.value("valida", false) ? "valida" : "INVALIDA") << "\n";
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        for (int i = 0; i < 5; i++) nosDisponiveis.push_back({"127.0.0.1", 8081 + i});
    } else {
        for (int i = 1; i < argc; i++) {
            string endereco = argv[i];
            size_t separador = endereco.rfind(':');
            int porta = separador == string::npos ? 0 : atoi(endereco.substr(separador + 1).c_str());
            if (separador == string::npos || porta <= 0 || porta > 65535) {
                cerr << "Endereco invalido: " << endereco << " (use host:porta)\n";
                return 1;
            }
            nosDisponiveis.push_back({endereco.substr(0, separador), porta});
        }
    }

    cout << "VaiJunto - terminal de teste. No atual: " << nosDisponiveis[0].host << ":"
         << nosDisponiveis[0].porta << ". Digite 'ajuda'.\n";

    string linha;
    while (true) {
        cout << (contaAtual.empty() ? "-" : contaAtual) << "@" << nosDisponiveis[indiceNoAtual].host
             << ":" << nosDisponiveis[indiceNoAtual].porta << "> " << flush;
        if (!getline(cin, linha)) break;

        istringstream argumentos(linha);
        string comando;
        if (!(argumentos >> comando)) continue;

        if (comando == "ajuda") mostrarAjuda();
        else if (comando == "usuario") comandoUsuario(argumentos);
        else if (comando == "eu") comandoEu();
        else if (comando == "dar") comandoDar(argumentos);
        else if (comando == "pagar") comandoPagar(argumentos);
        else if (comando == "evento") comandoEvento(argumentos);
        else if (comando == "historico") comandoHistorico();
        else if (comando == "saldos") comandoSaldos();
        else if (comando == "cadeia") comandoCadeia();
        else if (comando == "status") comandoStatus(argumentos);
        else if (comando == "nos") comandoNos();
        else if (comando == "usar") {
            int indice = -1;
            argumentos >> indice;
            if (indice < 0 || indice >= static_cast<int>(nosDisponiveis.size())) {
                cout << "Indice invalido (0 a " << nosDisponiveis.size() - 1 << ")\n";
            } else {
                indiceNoAtual = indice;
            }
        }
        else if (comando == "sincronizar") mostrar(chamarNoAtual("POST", "/api/consorcio/sincronizar"));
        else if (comando == "adulterar") {
            int indice = 1;
            argumentos >> indice;
            mostrar(chamarNoAtual("POST", "/api/consorcio/adulterar?indice=" + to_string(indice)));
        }
        else if (comando == "sair") break;
        else cout << "Comando desconhecido. Digite 'ajuda'.\n";
    }
    return 0;
}
