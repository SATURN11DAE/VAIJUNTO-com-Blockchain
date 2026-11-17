#include "protocolo.hpp"
#include "httplib.h"

#include <functional>

using namespace std;

// AUTENTICAÇÃO HTTP BASIC ("conta:senha")

static bool decodificarBase64(const string &entrada, string &saida) {
    static const string tabelaBase64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned acumulador = 0;
    int bitsDisponiveis = -8;
    saida.clear();

    for (unsigned char caractere : entrada) {
        if (caractere == '=') break;
        size_t posicao = tabelaBase64.find(static_cast<char>(caractere));
        if (posicao == string::npos) return false;

        acumulador = ((acumulador << 6) + static_cast<unsigned>(posicao)) & 0xFFFFFF;
        bitsDisponiveis += 6;
        if (bitsDisponiveis >= 0) {
            saida.push_back(static_cast<char>((acumulador >> bitsDisponiveis) & 0xFF));
            bitsDisponiveis -= 8;
        }
    }
    return true;
}

static bool autenticarRequisicao(const httplib::Request &requisicao, httplib::Response &resposta, string &conta) {
    string cabecalho = requisicao.get_header_value("Authorization");
    string credenciais;

    if (cabecalho.rfind("Basic ", 0) == 0 && decodificarBase64(cabecalho.substr(6), credenciais)) {
        conta = credenciais.substr(0, credenciais.find(':'));
        if (nomeContaValido(conta)) return true;
    }

    enviarJson(resposta, 401, {{"erro", "Credenciais invalidas ou ausentes (HTTP Basic: id e senha)"}});
    return false;
}

using HandlerAutenticado = function<void(const httplib::Request &, httplib::Response &, const string &conta)>;

// Envolve um handler para só executar se a requisição estiver autenticada.
static function<void(const httplib::Request &, httplib::Response &)> exigirAutenticacao(HandlerAutenticado handler) {
    return [handler](const httplib::Request &requisicao, httplib::Response &resposta) {
        string conta;
        if (autenticarRequisicao(requisicao, resposta, conta)) {
            handler(requisicao, resposta, conta);
        }
    };
}

static HandlerAutenticado rotaNaoImplementada(const string &detalhe) {
    return [detalhe](const httplib::Request &, httplib::Response &resposta, const string &) {
        responderNaoImplementado(resposta, detalhe);
    };
}

// ROTAS /api/cliente

void registrarRotasCliente(httplib::Server &servidor) {
    const string prefixo = "/" + prefixoCliente;
    const string detalheSemCaronas = "ainda nao existem caronas no ledger";

    servidor.Post(prefixo + "/usuarios", [](const httplib::Request &, httplib::Response &resposta) {
        responderNaoImplementado(resposta, "cadastro: a identidade passara a ser a chave publica do usuario");
    });

    servidor.Get(prefixo + "/eu", exigirAutenticacao([](const httplib::Request &, httplib::Response &resposta, const string &conta) {
        lock_guard<mutex> trava(mutexCadeia);
        Saldos saldos;
        string erro;
        if (!cadeiaValida(cadeia, saldos, erro)) {
            return enviarJson(resposta, 503, {{"erro", "minha cadeia esta invalida, aguarde"}});
        }

        auto entradaSaldo = saldos.find(conta);
        enviarJson(resposta, 200, {
            {"id", conta},
            {"nome", conta},
            {"saldo", entradaSaldo == saldos.end() ? 0 : entradaSaldo->second},
            {"servidor", meuNome}
        });
    }));

    servidor.Get(prefixo + "/itinerarios", exigirAutenticacao(rotaNaoImplementada(detalheSemCaronas)));
    servidor.Post(prefixo + "/rotas", exigirAutenticacao(rotaNaoImplementada(detalheSemCaronas)));
    servidor.Post(prefixo + "/caronas", exigirAutenticacao(rotaNaoImplementada(detalheSemCaronas)));
    servidor.Get(prefixo + "/caronas", exigirAutenticacao(rotaNaoImplementada(detalheSemCaronas)));
    servidor.Delete(prefixo + "/caronas/" + regexIdentificador, exigirAutenticacao(rotaNaoImplementada(detalheSemCaronas)));

    // Registra uma transação no ledger (com pagamento: valor > 0, ou evento: valor == 0).
    // A requisição espera o bloco ser fechado (janela de agrupamento) e decidido pelo consenso.
    servidor.Post(prefixo + "/reservas", exigirAutenticacao([](const httplib::Request &requisicao, httplib::Response &resposta, const string &conta) {
        Transferencia transacao;
        transacao.origem = conta; // Quem origina é sempre o usuário autenticado

        try {
            json corpoJson = json::parse(requisicao.body);
            if (corpoJson.contains("origem") && corpoJson["origem"].get<string>() != conta) {
                return enviarJson(resposta, 403, {{"erro", "so e possivel operar em nome da propria conta"}});
            }

            transacao.tipo = corpoJson.value("tipo", "TRANSFERENCIA");
            transacao.destino = corpoJson.value("destino", "");
            transacao.valor = corpoJson.value("valor", 0LL);
            transacao.detalhe = corpoJson.value("detalhe", "");
        } catch (...) {
            return enviarJson(resposta, 400, {{"erro", "JSON invalido. Ex: {\"destino\":\"motorista1\",\"valor\":30}"}});
        }

        ResultadoProposta resultado = proporTransacao(transacao);
        enviarJson(resposta, resultado.codigoStatus, resultado.corpo);
    }));

    servidor.Get(prefixo + "/reservas", exigirAutenticacao([](const httplib::Request &, httplib::Response &resposta, const string &conta) {
        lock_guard<mutex> trava(mutexCadeia);
        json listaReservas = json::array();

        for (size_t i = 1; i < cadeia.size(); i++) {
            for (const auto &tx : cadeia[i].txs) {
                if (tx.origem == conta || tx.destino == conta) {
                    listaReservas.push_back({
                        {"id", cadeia[i].hash},
                        {"bloco", cadeia[i].indice},
                        {"timestamp", cadeia[i].timestamp},
                        {"tipo", tx.tipo},
                        {"origem", tx.origem},
                        {"destino", tx.destino},
                        {"valor", tx.valor},
                        {"detalhe", tx.detalhe}
                    });
                }
            }
        }
        enviarJson(resposta, 200, listaReservas);
    }));

    servidor.Delete(prefixo + "/reservas/" + regexIdentificador,
                    exigirAutenticacao(rotaNaoImplementada("o cancelamento sera uma transacao CANCELAR no ledger")));
}
