#include "protocolo.hpp"
#include "httplib.h"
#include "painel.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <vector>

using namespace std;

// CONSENSO (prepare / commit / rollback com quórum 3/5)

struct BlocoPendente {
    Bloco bloco;
    time_t preparadoEm = 0;
};

static map<string, BlocoPendente> blocosPendentes; // chave: hash do bloco

// Descarta pendentes expirados (chamar com mutexCadeia travado)
static void descartarPendentesExpirados() {
    time_t agora = time(nullptr);
    for (auto entrada = blocosPendentes.begin(); entrada != blocosPendentes.end();) {
        if (agora - entrada->second.preparadoEm > segundosParaDescartarPendente) {
            logar("Bloco preparado " + entrada->first.substr(0, 8) + " expirou: descartado");
            entrada = blocosPendentes.erase(entrada);
        } else {
            ++entrada;
        }
    }
}

// Um índice só pode ser reservado por UM bloco pendente. Sem isso, dois proponentes simultâneos
// conseguiriam quórum para blocos diferentes no mesmo índice e um deles seria desfeito depois
// de o cliente já ter recebido 200. Chamar com mutexCadeia travado.
static bool indiceReservadoPorOutroBloco(const Bloco &bloco) {
    for (const auto &[idPendente, pendente] : blocosPendentes) {
        if (pendente.bloco.indice == bloco.indice && idPendente != bloco.hash) return true;
    }
    return false;
}

static ResultadoProposta montarResultado(int codigoStatus, json corpo) {
    return {codigoStatus, std::move(corpo)};
}

// AGRUPAMENTO DE TRANSAÇÕES (fila -> bloco)

// Um pedido de cliente esperando para entrar em um bloco.
struct PedidoDeTransacao {
    Transferencia transacao;
    promise<ResultadoProposta> promessa;   // a thread do cliente espera por isto
    chrono::steady_clock::time_point enfileiradoEm;
};

static deque<shared_ptr<PedidoDeTransacao>> filaDeTransacoes;
static mutex mutexFila;                    // Protege só a fila. Nunca travar junto com mutexCadeia.
static condition_variable condicaoFila;
static once_flag agrupadorIniciado;
constexpr int maximoTentativasPorLote = 3;
constexpr int segundosEsperandoBloco = 60;

// Cada pedido é respondido exatamente uma vez (um segundo set_value é ignorado).
static void responderPedido(const shared_ptr<PedidoDeTransacao> &pedido, ResultadoProposta resultado) {
    try {
        pedido->promessa.set_value(std::move(resultado));
    } catch (const future_error &) {}
}

// Resultado de UMA rodada de consenso para um bloco.
struct ResultadoConsenso {
    ResultadoProposta resultado;
    bool tentarNovamente;   // true se a falha foi conflito de cadeia (vale remontar o bloco)
};

// CONSENSO de um bloco já montado: PREPARE nos vizinhos, contagem de votos e COMMIT ou ROLLBACK.
static ResultadoConsenso executarConsenso(const Bloco &novoBloco, const Bloco &topoDaCadeia) {
    // O próprio proponente também só vota em um bloco por índice: reserva o índice aqui.
    {
        lock_guard<mutex> trava(mutexCadeia);
        descartarPendentesExpirados();
        if (indiceReservadoPorOutroBloco(novoBloco)) {
            return {montarResultado(409, {{"erro", "indice reservado por outro bloco pendente"}}), true};
        }
        blocosPendentes[novoBloco.hash] = BlocoPendente{novoBloco, time(nullptr)};
    }
    auto liberarReservaLocal = [&]() {
        lock_guard<mutex> trava(mutexCadeia);
        blocosPendentes.erase(novoBloco.hash);
    };

    // PREPARE: pede voto aos vizinhos (o próprio nó já conta como 1 voto)
    const string caminhoBase = "/" + prefixoConsorcio + "/reservas/";
    int votos = 1;
    int nosInacessiveis = 0;
    vector<int> idsQueAprovaram;
    string corpoRequisicao = blocoParaJson(novoBloco).dump();

    for (int idVizinho = 0; idVizinho < numeroServidores; idVizinho++) {
        if (idVizinho == meuId) continue;

        RespostaHttp resposta = httpPost(nosDaRede[idVizinho], caminhoBase + "prepare", corpoRequisicao);
        if (!resposta.alcancouNo) {
            nosInacessiveis++;
            logar(nosDaRede[idVizinho].nome + " inacessivel (timeout)");
            continue;
        }

        try {
            if (resposta.codigoStatus == 200 && json::parse(resposta.corpo).value("voto", false)) {
                votos++;
                idsQueAprovaram.push_back(idVizinho);
            }
        } catch (...) {}
    }

    auto desfazerNosVizinhos = [&]() {
        for (int idVizinho : idsQueAprovaram) {
            httpDelete(nosDaRede[idVizinho], caminhoBase + novoBloco.hash);
        }
    };

    if (votos < quorumMinimo) {
        desfazerNosVizinhos();
        liberarReservaLocal();
        json corpoErro = {{"erro", "sem quorum"}, {"votos", votos}, {"inacessiveis", nosInacessiveis}};
        // Se ninguém estava fora do ar, os "nãos" podem ser de vizinhos que já têm outro bloco nesse índice.
        return {montarResultado(nosInacessiveis > 0 ? 503 : 409, corpoErro), nosInacessiveis == 0};
    }

    // COMMIT: grava localmente se o topo não mudou e avisa quem aprovou
    bool gravouLocalmente = false;
    {
        lock_guard<mutex> trava(mutexCadeia);
        if (cadeia.back().hash == topoDaCadeia.hash) {
            cadeia.push_back(novoBloco);
            salvarLedger();
            gravouLocalmente = true;
        }
        blocosPendentes.erase(novoBloco.hash);   // a reserva local acaba aqui, gravou ou não
    }

    if (!gravouLocalmente) {
        desfazerNosVizinhos();
        return {montarResultado(409, {{"erro", "conflito: a cadeia mudou, tente de novo"}}), true};
    }

    logar("Bloco " + to_string(novoBloco.indice) + " CONFIRMADO: " + to_string(novoBloco.txs.size()) +
          " transacao(oes) [votos " + to_string(votos) + "/" + to_string(numeroServidores) + "]");
    if (novoBloco.txs.size() <= 5) {
        for (const auto &tx : novoBloco.txs) {
            logar("    [" + tx.tipo + "] " + tx.origem + " -> " + tx.destino + " (" + to_string(tx.valor) + ")");
        }
    }

    for (int idVizinho : idsQueAprovaram) {
        httpPut(nosDaRede[idVizinho], caminhoBase + novoBloco.hash + "/commit");
    }

    return {montarResultado(200, {{"ok", true}, {"id", novoBloco.hash}, {"bloco", novoBloco.indice}, {"votos", votos}}), false};
}

// Transforma um lote de pedidos em UM bloco e o leva ao consenso.
// Cada pedido recebe a sua resposta (200 se o bloco foi confirmado, ou o erro).
static void processarLote(vector<shared_ptr<PedidoDeTransacao>> pedidos) {
    for (int tentativa = 0; tentativa < maximoTentativasPorLote && !pedidos.empty(); tentativa++) {
        if (tentativa > 0) {
            // Conflito na rodada anterior: dá um tempo (diferente em cada nó) e se atualiza
            this_thread::sleep_for(chrono::milliseconds(100 * tentativa + 40 * meuId));
            sincronizarComVizinhos();
        }

        // 1) Foto do topo e dos saldos (sem segurar o mutex na chamada de rede)
        Bloco topoDaCadeia;
        Saldos saldos;
        string erro;
        {
            lock_guard<mutex> trava(mutexCadeia);
            topoDaCadeia = cadeia.back();
            if (!cadeiaValida(cadeia, saldos, erro)) {
                for (const auto &pedido : pedidos) {
                    responderPedido(pedido, montarResultado(503, {{"erro", "minha cadeia esta invalida, aguarde"}}));
                }
                return;
            }
        }

        // 2) Cada transação é testada sobre os saldos já alterados pelas anteriores do lote.
        //    As inválidas saem com 409 próprio e não derrubam as outras.
        int indiceNovoBloco = topoDaCadeia.indice + 1;
        Saldos saldosDoLote = saldos;
        vector<shared_ptr<PedidoDeTransacao>> aceitos;
        for (const auto &pedido : pedidos) {
            string erroTransacao;
            if (aplicarTransacoes({pedido->transacao}, indiceNovoBloco, saldosDoLote, erroTransacao)) {
                aceitos.push_back(pedido);
            } else {
                responderPedido(pedido, montarResultado(409, {{"erro", erroTransacao}}));
            }
        }
        if (aceitos.empty()) return;

        // 3) Monta o bloco com as transações aceitas
        Bloco novoBloco;
        novoBloco.indice = indiceNovoBloco;
        novoBloco.timestamp = time(nullptr);
        novoBloco.hashAnterior = topoDaCadeia.hash;
        novoBloco.proponente = meuNome;
        for (const auto &pedido : aceitos) novoBloco.txs.push_back(pedido->transacao);
        novoBloco.hash = calcularHashDoBloco(novoBloco);

        Saldos saldosParaConferir = saldos;
        if (!blocoValidoApos(topoDaCadeia, novoBloco, saldosParaConferir, erro)) {
            for (const auto &pedido : aceitos) {
                responderPedido(pedido, montarResultado(409, {{"erro", erro}}));
            }
            return;
        }

        // 4) Consenso. Em conflito de cadeia, remonta o lote (as transações são revalidadas).
        ResultadoConsenso consenso = executarConsenso(novoBloco, topoDaCadeia);
        bool ultimaTentativa = (tentativa + 1 >= maximoTentativasPorLote);
        if (consenso.tentarNovamente && !ultimaTentativa) {
            pedidos = std::move(aceitos);
            continue;
        }

        int posicao = 0;
        for (const auto &pedido : aceitos) {
            ResultadoProposta resposta = consenso.resultado;
            if (resposta.codigoStatus == 200) {
                resposta.corpo["posicaoNoBloco"] = posicao;
                resposta.corpo["transacoesNoBloco"] = aceitos.size();
            }
            responderPedido(pedido, std::move(resposta));
            posicao++;
        }
        return;
    }
}

// Thread de fundo: espera a fila encher ou a janela vencer e então fecha o bloco.
static void agruparEProporBlocos() {
    while (true) {
        vector<shared_ptr<PedidoDeTransacao>> lote;
        {
            unique_lock<mutex> trava(mutexFila);
            condicaoFila.wait(trava, [] { return !filaDeTransacoes.empty(); });

            // A janela conta a partir da transação mais antiga. Fila cheia fecha na hora.
            auto prazo = filaDeTransacoes.front()->enfileiradoEm + chrono::milliseconds(janelaAgrupamentoMs);
            condicaoFila.wait_until(trava, prazo, [] { return filaDeTransacoes.size() >= maximoTransacoesPorBloco; });

            while (!filaDeTransacoes.empty() && lote.size() < maximoTransacoesPorBloco) {
                lote.push_back(filaDeTransacoes.front());
                filaDeTransacoes.pop_front();
            }
        }

        try {
            processarLote(lote);
        } catch (const exception &excecao) {
            logar(string("Erro inesperado ao processar lote: ") + excecao.what());
            for (const auto &pedido : lote) {
                responderPedido(pedido, montarResultado(500, {{"erro", "erro interno ao montar o bloco"}}));
            }
        }
    }
}

ResultadoProposta proporTransacao(const Transferencia &transacao) {
    if (transacao.origem == "SISTEMA") {
        return montarResultado(400, {{"erro", "conta reservada"}});
    }

    call_once(agrupadorIniciado, [] { thread(agruparEProporBlocos).detach(); });

    auto pedido = make_shared<PedidoDeTransacao>();
    pedido->transacao = transacao;
    pedido->enfileiradoEm = chrono::steady_clock::now();
    future<ResultadoProposta> futuroResultado = pedido->promessa.get_future();

    {
        lock_guard<mutex> trava(mutexFila);
        filaDeTransacoes.push_back(pedido);
    }
    condicaoFila.notify_one();

    if (futuroResultado.wait_for(chrono::seconds(segundosEsperandoBloco)) != future_status::ready) {
        return montarResultado(504, {{"erro", "tempo esgotado esperando o bloco; confira em GET /api/cliente/reservas"}});
    }
    return futuroResultado.get();
}

bool prepararBloco(const Bloco &bloco, string &motivo) {
    lock_guard<mutex> trava(mutexCadeia);
    descartarPendentesExpirados();

    if (indiceReservadoPorOutroBloco(bloco)) {
        motivo = "indice " + to_string(bloco.indice) + " reservado por outro bloco pendente";
        return false;
    }

    Saldos saldos;
    bool aceito = cadeiaValida(cadeia, saldos, motivo) &&
                  blocoValidoApos(cadeia.back(), bloco, saldos, motivo);

    if (aceito) {
        blocosPendentes[bloco.hash] = BlocoPendente{bloco, time(nullptr)};
        motivo.clear();
    }
    return aceito;
}

ResultadoAnexar anexarBloco(const Bloco &bloco, string &erro) {
    lock_guard<mutex> trava(mutexCadeia);
    int tamanhoDaCadeia = static_cast<int>(cadeia.size());

    if (bloco.indice < tamanhoDaCadeia) {
        if (cadeia[bloco.indice].hash == bloco.hash) return ResultadoAnexar::JaTinha;
        erro = "conflito: ja tenho outro bloco " + to_string(bloco.indice);
        return ResultadoAnexar::Conflito;
    }

    if (bloco.indice > tamanhoDaCadeia) return ResultadoAnexar::Atrasado;

    Saldos saldos;
    if (!cadeiaValida(cadeia, saldos, erro)) return ResultadoAnexar::Conflito;
    if (!blocoValidoApos(cadeia.back(), bloco, saldos, erro)) return ResultadoAnexar::Conflito;

    cadeia.push_back(bloco);
    salvarLedger();
    logar("Bloco " + to_string(bloco.indice) + " gravado (proponente: " + bloco.proponente + ")");
    return ResultadoAnexar::Anexado;
}

ResultadoAnexar confirmarBloco(const string &idBloco, string &erro) {
    Bloco bloco;
    {
        lock_guard<mutex> trava(mutexCadeia);
        descartarPendentesExpirados();

        auto entrada = blocosPendentes.find(idBloco);
        if (entrada == blocosPendentes.end()) {
            for (const auto &blocoDaCadeia : cadeia) {
                if (blocoDaCadeia.hash == idBloco) return ResultadoAnexar::JaTinha;
            }
            erro = "transacao desconhecida (nunca preparada ou expirada)";
            return ResultadoAnexar::Desconhecida;
        }
        bloco = entrada->second.bloco;
        blocosPendentes.erase(entrada);
    }

    ResultadoAnexar resultado = anexarBloco(bloco, erro);
    if (resultado == ResultadoAnexar::Atrasado) {
        sincronizarComVizinhos();
        resultado = anexarBloco(bloco, erro);
    }
    return resultado;
}

ResultadoDesfazer desfazerBloco(const string &idBloco) {
    lock_guard<mutex> trava(mutexCadeia);
    descartarPendentesExpirados();

    if (blocosPendentes.erase(idBloco) > 0) {
        logar("Bloco preparado " + idBloco.substr(0, 8) + " desfeito (rollback)");
        return ResultadoDesfazer::Desfeita;
    }

    for (const auto &blocoDaCadeia : cadeia) {
        if (blocoDaCadeia.hash == idBloco) return ResultadoDesfazer::JaConfirmada;
    }
    return ResultadoDesfazer::Inexistente;
}

string estadoDaTransacao(const string &idBloco) {
    lock_guard<mutex> trava(mutexCadeia);
    descartarPendentesExpirados();

    if (blocosPendentes.count(idBloco)) return "PREPARADA";
    for (const auto &blocoDaCadeia : cadeia) {
        if (blocoDaCadeia.hash == idBloco) return "CONFIRMADA";
    }
    return "DESCONHECIDA";
}

json listarReservas() {
    lock_guard<mutex> trava(mutexCadeia);
    descartarPendentesExpirados();

    json listaReservas = json::array();
    for (const auto &[idBloco, pendente] : blocosPendentes) {
        listaReservas.push_back({
            {"id", idBloco},
            {"estado", "PREPARADA"},
            {"proponente", pendente.bloco.proponente},
            {"txs", blocoParaJson(pendente.bloco)["txs"]}
        });
    }

    for (size_t i = 1; i < cadeia.size(); i++) {
        listaReservas.push_back({
            {"id", cadeia[i].hash},
            {"estado", "CONFIRMADA"},
            {"bloco", cadeia[i].indice},
            {"proponente", cadeia[i].proponente},
            {"txs", blocoParaJson(cadeia[i])["txs"]}
        });
    }
    return listaReservas;
}

// SINCRONIZAÇÃO E VIGIA CONTRA ADULTERAÇÃO

void sincronizarComVizinhos() {
    for (int idVizinho = 0; idVizinho < numeroServidores; idVizinho++) {
        if (idVizinho == meuId) continue;

        RespostaHttp resposta = httpGet(nosDaRede[idVizinho], "/" + prefixoConsorcio + "/chain");
        if (!resposta.alcancouNo || resposta.codigoStatus != 200) continue;

        vector<Bloco> cadeiaDoVizinho;
        try {
            cadeiaDoVizinho = jsonParaCadeia(resposta.corpo);
        } catch (...) {
            continue;
        }

        Saldos saldos;
        string erro;
        if (!cadeiaValida(cadeiaDoVizinho, saldos, erro)) {
            logar("Cadeia de " + nosDaRede[idVizinho].nome + " REJEITADA: " + erro);
            continue;
        }

        lock_guard<mutex> trava(mutexCadeia);
        if (cadeiaDoVizinho.size() > cadeia.size()) {
            cadeia = std::move(cadeiaDoVizinho);
            salvarLedger();
            logar("Sincronizado com " + nosDaRede[idVizinho].nome + " (" + to_string(cadeia.size()) + " blocos)");
        }
    }
}

void vigiarCadeia() {
    this_thread::sleep_for(chrono::seconds(1));
    while (true) {
        {
            lock_guard<mutex> trava(mutexCadeia);
            Saldos saldos;
            string erro;
            if (!cadeiaValida(cadeia, saldos, erro)) {
                logar("ADULTERACAO DETECTADA (" + erro + "): descartando e ressincronizando");
                cadeia = {criarBlocoGenesis()};
                salvarLedger();
            }
        }
        sincronizarComVizinhos();
        this_thread::sleep_for(chrono::seconds(3));
    }
}

// SERVIDOR HTTP E ROTAS /api/consorcio

void enviarJson(httplib::Response &resposta, int codigoStatus, const json &corpoJson) {
    resposta.status = codigoStatus;
    resposta.set_content(corpoJson.dump(2) + "\n", "application/json");
}

void responderNaoImplementado(httplib::Response &resposta, const string &detalhe) {
    enviarJson(resposta, 501, {{"erro", "Ainda nao implementado nesta versao"}, {"detalhe", detalhe}});
}

bool iniciarServidor(int porta) {
    httplib::Server servidor;

    // Cada requisição de cliente fica bloqueada até o bloco dela ser decidido. O pool precisa
    // ser grande para sobrar thread para o tráfego entre nós (prepare/commit) durante testes em massa.
    servidor.new_task_queue = [] { return new httplib::ThreadPool(128); };

    servidor.set_default_headers({
        {"X-Server-Id", meuNome},
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type, Authorization, X-Server-Id"}
    });

    servidor.Options(R"(.*)", [](const httplib::Request &, httplib::Response &resposta) {
        enviarJson(resposta, 200, json::object());
    });

    servidor.Get("/", [](const httplib::Request &, httplib::Response &resposta) {
        resposta.set_content(PAINEL_HTML, "text/html; charset=utf-8");
    });

    registrarRotasConsorcio(servidor);
    registrarRotasCliente(servidor);

    servidor.set_error_handler([](const httplib::Request &requisicao, httplib::Response &resposta) {
        if (!resposta.body.empty()) return;

        string descricaoErro = "Rota fora de /" + prefixoConsorcio + " e /" + prefixoCliente;
        if (requisicao.path.rfind("/" + prefixoConsorcio, 0) == 0) {
            descricaoErro = "Rota ou metodo inexistente em /" + prefixoConsorcio;
        } else if (requisicao.path.rfind("/" + prefixoCliente, 0) == 0) {
            descricaoErro = "Rota ou metodo inexistente em /" + prefixoCliente;
        }
        enviarJson(resposta, resposta.status > 0 ? resposta.status : 404, {{"erro", descricaoErro}});
    });

    servidor.set_logger([](const httplib::Request &requisicao, const httplib::Response &resposta) {
        string origem = requisicao.has_header("X-Server-Id") ? requisicao.get_header_value("X-Server-Id") : "cliente";
        registrarRequisicaoRecebida(requisicao.method, requisicao.path, origem, resposta.status, requisicao.body);
    });

    return servidor.listen("0.0.0.0", porta);
}

void registrarRotasConsorcio(httplib::Server &servidor) {
    const string prefixo = "/" + prefixoConsorcio;

    auto responderSaude = [](const httplib::Request &, httplib::Response &resposta) {
        lock_guard<mutex> trava(mutexCadeia);
        Saldos saldos;
        string erro;
        bool cadeiaEstaValida = cadeiaValida(cadeia, saldos, erro);

        json corpoJson = {
            {"status", meuNome + " ta on"},
            {"no", meuNome},
            {"altura", cadeia.size()},
            {"hashTopo", cadeia.back().hash},
            {"valida", cadeiaEstaValida}
        };
        if (!cadeiaEstaValida) corpoJson["erro"] = erro;
        enviarJson(resposta, 200, corpoJson);
    };

    servidor.Get(prefixo + "/health", responderSaude);
    servidor.Get(prefixo + "/vivo", responderSaude);

    servidor.Get(prefixo + "/chain", [](const httplib::Request &, httplib::Response &resposta) {
        lock_guard<mutex> trava(mutexCadeia);
        enviarJson(resposta, 200, cadeiaParaJson(cadeia));
    });

    servidor.Get(prefixo + "/saldos", [](const httplib::Request &, httplib::Response &resposta) {
        lock_guard<mutex> trava(mutexCadeia);
        Saldos saldos;
        string erro;
        if (!cadeiaValida(cadeia, saldos, erro)) {
            return enviarJson(resposta, 409, {{"erro", erro}});
        }
        enviarJson(resposta, 200, json(saldos));
    });

    servidor.Get(prefixo + "/peers", [](const httplib::Request &, httplib::Response &resposta) {
        json listaNos = json::array();
        for (int i = 0; i < numeroServidores; i++) {
            listaNos.push_back({
                {"nome", nosDaRede[i].nome},
                {"url", "http://" + nosDaRede[i].host + ":" + to_string(nosDaRede[i].porta)},
                {"eu", i == meuId}
            });
        }
        enviarJson(resposta, 200, listaNos);
    });

    servidor.Get(prefixo + "/trechos", [](const httplib::Request &, httplib::Response &resposta) {
        responderNaoImplementado(resposta, "ainda nao existem caronas/trechos no ledger");
    });

    // ---- 2PC sobre blocos
    servidor.Get(prefixo + "/reservas", [](const httplib::Request &, httplib::Response &resposta) {
        enviarJson(resposta, 200, listarReservas());
    });

    servidor.Post(prefixo + "/reservas/prepare", [](const httplib::Request &requisicao, httplib::Response &resposta) {
        try {
            Bloco bloco = jsonParaBloco(json::parse(requisicao.body));
            string motivo;
            bool aceito = prepararBloco(bloco, motivo);
            enviarJson(resposta, aceito ? 200 : 409, {{"voto", aceito}, {"id", bloco.hash}, {"motivo", motivo}});
        } catch (...) {
            enviarJson(resposta, 400, {{"voto", false}, {"erro", "bloco ilegivel"}});
        }
    });

    servidor.Put(prefixo + "/reservas/" + regexIdentificador + "/commit", [](const httplib::Request &requisicao, httplib::Response &resposta) {
        string idBloco = requisicao.matches[1];
        string erro;
        ResultadoAnexar resultado = confirmarBloco(idBloco, erro);

        if (resultado == ResultadoAnexar::Anexado || resultado == ResultadoAnexar::JaTinha) {
            enviarJson(resposta, 200, {{"ok", true}, {"id", idBloco}, {"estado", "CONFIRMADA"}});
        } else {
            enviarJson(resposta, resultado == ResultadoAnexar::Desconhecida ? 404 : 409, {{"ok", false}, {"erro", erro}});
        }
    });

    servidor.Put(prefixo + "/reservas/" + regexIdentificador + "/cancel", [](const httplib::Request &, httplib::Response &resposta) {
        responderNaoImplementado(resposta, "o cancelamento sera uma transacao CANCELAR no ledger");
    });

    servidor.Put(prefixo + "/reservas/" + regexIdentificador + "/cancel-motorista", [](const httplib::Request &, httplib::Response &resposta) {
        responderNaoImplementado(resposta, "o cancelamento pelo motorista sera uma transacao no ledger");
    });

    servidor.Get(prefixo + "/reservas/" + regexIdentificador + "/status", [](const httplib::Request &requisicao, httplib::Response &resposta) {
        string idBloco = requisicao.matches[1];
        string estado = estadoDaTransacao(idBloco);
        enviarJson(resposta, estado == "DESCONHECIDA" ? 404 : 200, {{"id", idBloco}, {"estado", estado}});
    });

    servidor.Delete(prefixo + "/reservas/" + regexIdentificador, [](const httplib::Request &requisicao, httplib::Response &resposta) {
        string idBloco = requisicao.matches[1];
        switch (desfazerBloco(idBloco)) {
            case ResultadoDesfazer::Desfeita:
                return enviarJson(resposta, 200, {{"ok", true}, {"id", idBloco}});
            case ResultadoDesfazer::Inexistente:
                return enviarJson(resposta, 200, {{"ok", true}, {"id", idBloco}, {"nota", "nada a desfazer"}});
            case ResultadoDesfazer::JaConfirmada:
                return enviarJson(resposta, 409, {{"ok", false}, {"erro", "ja confirmada: o ledger e imutavel"}});
        }
    });

    // ---- Manutenção e demonstração
    servidor.Post(prefixo + "/sincronizar", [](const httplib::Request &, httplib::Response &resposta) {
        sincronizarComVizinhos();
        lock_guard<mutex> trava(mutexCadeia);
        enviarJson(resposta, 200, {{"altura", cadeia.size()}});
    });

    servidor.Post(prefixo + "/adulterar", [](const httplib::Request &requisicao, httplib::Response &resposta) {
        int indiceBloco = requisicao.has_param("indice") ? atoi(requisicao.get_param_value("indice").c_str()) : 1;
        lock_guard<mutex> trava(mutexCadeia);
        if (indiceBloco < 0 || indiceBloco >= static_cast<int>(cadeia.size())) {
            return enviarJson(resposta, 400, {{"erro", "indice fora da cadeia"}});
        }
        cadeia[indiceBloco].txs[0].valor += 1000;
        salvarLedger();
        logar("!!! Historico adulterado de proposito no bloco " + to_string(indiceBloco));
        enviarJson(resposta, 200, {{"adulterado", indiceBloco}});
    });
}
