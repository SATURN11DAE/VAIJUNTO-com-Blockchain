#include "protocolo.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>

using namespace std;

// TERMINAL DO SERVIDOR
// Console interativo que roda dentro do nó: mostra estado, cadeia, saldos,
// blocos pendentes e o histórico das requisições recebidas.

struct RequisicaoRecebida {
    string horario;
    string metodo;
    string caminho;
    string origem;
    string corpo;
    int codigoStatus = 0;
};

static deque<RequisicaoRecebida> historicoRequisicoes;
static mutex mutexHistorico;
static atomic<bool> mostrarTodasRequisicoes{false};
constexpr size_t maximoRequisicoesGuardadas = 200;

static string horarioAtual() {
    time_t agora = time(nullptr);
    tm horarioLocal;
    localtime_r(&agora, &horarioLocal);
    char texto[16];
    strftime(texto, sizeof(texto), "%H:%M:%S", &horarioLocal);
    return texto;
}

// Deixa o texto em uma linha só e limita o tamanho
static string resumir(const string &texto, size_t limite = 160) {
    string linha = texto;
    for (char &caractere : linha) {
        if (caractere == '\n' || caractere == '\r' || caractere == '\t') caractere = ' ';
    }
    if (linha.size() > limite) linha = linha.substr(0, limite) + "...";
    return linha;
}

// Chamada pelo logger do servidor HTTP a cada requisição atendida.
void registrarRequisicaoRecebida(const string &metodo, const string &caminho, const string &origem,
                                 int codigoStatus, const string &corpo) {
    RequisicaoRecebida requisicao{horarioAtual(), metodo, caminho, origem, corpo, codigoStatus};

    {
        lock_guard<mutex> trava(mutexHistorico);
        historicoRequisicoes.push_back(requisicao);
        if (historicoRequisicoes.size() > maximoRequisicoesGuardadas) {
            historicoRequisicoes.pop_front();
        }
    }

    // GET e OPTIONS só aparecem no log ao vivo com "verbose on" (senão o painel enche o log)
    bool somenteLeitura = (metodo == "GET" || metodo == "OPTIONS");
    if (somenteLeitura && !mostrarTodasRequisicoes) return;

    string linha = "<- " + metodo + " " + caminho + " de " + origem + " -> " + to_string(codigoStatus);
    if (mostrarTodasRequisicoes && !corpo.empty()) linha += " | corpo: " + resumir(corpo, 100);
    logar(linha);
}

// Uma linha por transação de um array JSON "txs"
static string resumirTransacoes(const json &txs) {
    string texto;
    for (const auto &tx : txs) {
        if (!texto.empty()) texto += " ; ";
        texto += "[" + tx.value("tipo", "?") + "] " + tx.value("origem", "?") + " -> " +
                 tx.value("destino", "") + " (" + to_string(tx.value("valor", 0LL)) + ")";
        string detalhe = tx.value("detalhe", "");
        if (!detalhe.empty()) texto += " '" + detalhe + "'";
    }
    return texto;
}

static void mostrarAjuda() {
    cout << "Comandos do terminal do servidor:\n"
         << "  estado               resumo deste no (altura, topo, validade, pendentes)\n"
         << "  cadeia [n]           ultimos n blocos (padrao 10)\n"
         << "  bloco <indice>       um bloco completo em JSON\n"
         << "  saldos               saldo de cada conta segundo este no\n"
         << "  pendentes            blocos preparados e ainda nao confirmados\n"
         << "  requisicoes [n]      ultimas n requisicoes recebidas (padrao 20)\n"
         << "  verbose on|off       mostra tambem GET/OPTIONS (e corpos) ao vivo\n"
         << "  peers                pergunta aos outros nos se estao vivos\n"
         << "  sincronizar          busca a cadeia mais longa e valida dos vizinhos\n"
         << "  adulterar [indice]   corrompe um bloco de proposito (demo do vigia)\n"
         << "  sair                 encerra este no\n";
}

static void comandoEstado() {
    string erro;
    bool valida;
    size_t altura;
    string hashTopo;
    {
        lock_guard<mutex> trava(mutexCadeia);
        Saldos saldos;
        valida = cadeiaValida(cadeia, saldos, erro);
        altura = cadeia.size();
        hashTopo = cadeia.back().hash;
    }

    int pendentes = 0;
    for (const auto &item : listarReservas()) {
        if (item.value("estado", "") == "PREPARADA") pendentes++;
    }

    cout << "No:        " << meuNome << " (id " << meuId << ", porta " << nosDaRede[meuId].porta << ")\n"
         << "Altura:    " << altura << " blocos\n"
         << "Topo:      " << hashTopo.substr(0, 16) << "...\n"
         << "Cadeia:    " << (valida ? "VALIDA" : "INVALIDA (" + erro + ")") << "\n"
         << "Pendentes: " << pendentes << "\n"
         << "Quorum:    " << quorumMinimo << "/" << numeroServidores << "\n"
         << "Verbose:   " << (mostrarTodasRequisicoes ? "on" : "off") << "\n";
}

static void comandoCadeia(int quantidade) {
    vector<Bloco> copia;
    {
        lock_guard<mutex> trava(mutexCadeia);
        copia = cadeia;
    }

    size_t inicio = copia.size() > static_cast<size_t>(quantidade) ? copia.size() - quantidade : 0;
    for (size_t i = inicio; i < copia.size(); i++) {
        const Bloco &bloco = copia[i];
        cout << "#" << bloco.indice << " " << bloco.hash.substr(0, 8)
             << " prev " << bloco.hashAnterior.substr(0, 8)
             << " por " << bloco.proponente << " | "
             << resumirTransacoes(blocoParaJson(bloco)["txs"]) << "\n";
    }
}

static void comandoBloco(int indice) {
    lock_guard<mutex> trava(mutexCadeia);
    if (indice < 0 || indice >= static_cast<int>(cadeia.size())) {
        cout << "Indice fora da cadeia (0 a " << cadeia.size() - 1 << ")\n";
        return;
    }
    cout << blocoParaJson(cadeia[indice]).dump(2) << "\n";
}

static void comandoSaldos() {
    Saldos saldos;
    string erro;
    {
        lock_guard<mutex> trava(mutexCadeia);
        if (!cadeiaValida(cadeia, saldos, erro)) {
            cout << "Cadeia invalida, sem saldos confiaveis: " << erro << "\n";
            return;
        }
    }
    for (const auto &[conta, saldo] : saldos) {
        cout << "  " << left << setw(20) << conta << saldo << "\n";
    }
}

static void comandoPendentes() {
    bool algum = false;
    for (const auto &item : listarReservas()) {
        if (item.value("estado", "") != "PREPARADA") continue;
        algum = true;
        cout << item.value("id", "").substr(0, 8) << " por " << item.value("proponente", "?")
             << " | " << resumirTransacoes(item["txs"]) << "\n";
    }
    if (!algum) cout << "Nenhum bloco pendente.\n";
}

static void comandoRequisicoes(int quantidade) {
    lock_guard<mutex> trava(mutexHistorico);
    if (historicoRequisicoes.empty()) {
        cout << "Nenhuma requisicao recebida ainda.\n";
        return;
    }

    size_t inicio = historicoRequisicoes.size() > static_cast<size_t>(quantidade)
                        ? historicoRequisicoes.size() - quantidade : 0;
    for (size_t i = inicio; i < historicoRequisicoes.size(); i++) {
        const auto &requisicao = historicoRequisicoes[i];
        cout << requisicao.horario << " " << requisicao.metodo << " " << requisicao.caminho
             << " de " << requisicao.origem << " -> " << requisicao.codigoStatus;
        if (!requisicao.corpo.empty()) cout << " | " << resumir(requisicao.corpo, 100);
        cout << "\n";
    }
}

static void comandoPeers() {
    for (int i = 0; i < numeroServidores; i++) {
        if (i == meuId) continue;

        RespostaHttp resposta = httpGet(nosDaRede[i], "/" + prefixoConsorcio + "/vivo");
        cout << "  " << left << setw(16) << nosDaRede[i].nome
             << nosDaRede[i].host << ":" << nosDaRede[i].porta << "  ";

        if (!resposta.alcancouNo) {
            cout << "INACESSIVEL\n";
            continue;
        }

        try {
            json corpoJson = json::parse(resposta.corpo);
            cout << "vivo | altura " << corpoJson.value("altura", 0)
                 << " | cadeia " << (corpoJson.value("valida", false) ? "valida" : "INVALIDA") << "\n";
        } catch (...) {
            cout << "respondeu HTTP " << resposta.codigoStatus << " (corpo ilegivel)\n";
        }
    }
}

static void comandoAdulterar(int indice) {
    lock_guard<mutex> trava(mutexCadeia);
    if (indice < 0 || indice >= static_cast<int>(cadeia.size())) {
        cout << "Indice fora da cadeia\n";
        return;
    }
    cadeia[indice].txs[0].valor += 1000;
    salvarLedger();
    logar("!!! Historico adulterado de proposito no bloco " + to_string(indice));
}

// Argumento inteiro opcional, com valor padrão
static int lerInteiro(istringstream &argumentos, int padrao) {
    string texto;
    if (!(argumentos >> texto)) return padrao;
    try {
        return stoi(texto);
    } catch (...) {
        return padrao;
    }
}

void iniciarTerminalServidor() {
    if (!isatty(STDIN_FILENO)) {
        logar("Entrada padrao nao e um terminal: console do servidor desativado");
        return;
    }

    this_thread::sleep_for(chrono::milliseconds(500));
    logar("Console do servidor pronto. Digite 'ajuda'.");

    string linha;
    while (cout << "> " << flush, getline(cin, linha)) {
        istringstream argumentos(linha);
        string comando;
        if (!(argumentos >> comando)) continue;

        if (comando == "ajuda") mostrarAjuda();
        else if (comando == "estado") comandoEstado();
        else if (comando == "cadeia") comandoCadeia(lerInteiro(argumentos, 10));
        else if (comando == "bloco") comandoBloco(lerInteiro(argumentos, -1));
        else if (comando == "saldos") comandoSaldos();
        else if (comando == "pendentes") comandoPendentes();
        else if (comando == "requisicoes") comandoRequisicoes(lerInteiro(argumentos, 20));
        else if (comando == "peers") comandoPeers();
        else if (comando == "sincronizar") {
            sincronizarComVizinhos();
            lock_guard<mutex> trava(mutexCadeia);
            cout << "Altura agora: " << cadeia.size() << "\n";
        }
        else if (comando == "adulterar") comandoAdulterar(lerInteiro(argumentos, 1));
        else if (comando == "verbose") {
            string opcao;
            argumentos >> opcao;
            mostrarTodasRequisicoes = (opcao == "on");
            cout << "Verbose " << (mostrarTodasRequisicoes ? "ligado" : "desligado") << "\n";
        }
        else if (comando == "sair") {
            logar("Encerrando pelo console");
            _Exit(0);
        }
        else cout << "Comando desconhecido. Digite 'ajuda'.\n";
    }
}
