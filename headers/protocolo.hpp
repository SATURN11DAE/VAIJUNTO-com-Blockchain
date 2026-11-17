#pragma once

#include <cstddef>
#include <string>

#include "io.hpp"

using namespace std;

// Como os 5 nós concordam sobre o próximo bloco (quórum 3/5).
// O protocolo entre nós segue o desenho prepare / commit / rollback,
// mas em cima de blocos. O "id da transação" é o HASH do bloco.

// AGRUPAMENTO DE TRANSAÇÕES EM BLOCOS
// As transações que chegam ao nó ficam numa fila. A fila é "fechada" em UM bloco quando:
//   a) passou 'janelaAgrupamentoMs' desde que a transação mais antiga entrou, ou
//   b) a fila chegou a 'maximoTransacoesPorBloco' (fecha na hora, sem esperar a janela).
// Teste manual: 1 transação vira 1 bloco (só espera a janela).
// Teste em massa: muitas transações chegam juntas e viram poucos blocos.
// Janela 0 = não espera, mas ainda agrupa o que chegou no mesmo instante.
constexpr int janelaAgrupamentoMs = 1000;
constexpr size_t maximoTransacoesPorBloco = 50;

// Lado do nó que recebeu o pedido do cliente
struct ResultadoProposta {
    int codigoStatus;  // código HTTP a devolver ao cliente
    json corpo;        // resposta em JSON
};

// Põe a transação na fila e ESPERA o bloco que a contém ser decidido pelo consenso.
// Quem fecha o bloco é uma thread de fundo (criada na primeira chamada). Ela:
// 1) descarta as transações inválidas (cada uma recebe seu próprio 409)
// 2) monta UM bloco com as válidas  3) POST prepare nos vizinhos
// 4) com consenso: grava e manda PUT commit; sem consenso: manda DELETE (rollback).
// Se a cadeia mudou no meio do caminho, o lote é remontado e tentado de novo (até 3 vezes).
ResultadoProposta proporTransacao(const Transferencia &transacao);

// Lado dos vizinhos
// PREPARE: valida o bloco e, se aceitar, o guarda como "pendente"
bool prepararBloco(const Bloco &bloco, string &motivo);

enum class ResultadoAnexar {
    Anexado,
    JaTinha,
    Conflito,
    Atrasado,
    Desconhecida
};

// Grava um bloco no fim da cadeia, se ele encaixar.
ResultadoAnexar anexarBloco(const Bloco &bloco, string &erro);

// COMMIT: confirma o bloco pendente com esse id (se eu estiver atrasado, sincroniza antes).
ResultadoAnexar confirmarBloco(const string &idBloco, string &erro);

enum class ResultadoDesfazer {
    Desfeita,
    JaConfirmada,
    Inexistente
};

// ROLLBACK: descarta um bloco pendente. Bloco já confirmado não volta atrás (ledger imutável).
ResultadoDesfazer desfazerBloco(const string &idBloco);

string estadoDaTransacao(const string &idBloco);  // "PREPARADA" | "CONFIRMADA" | "DESCONHECIDA"
json listarReservas();                            // pendentes + confirmadas deste nó

// SINCRONIZAÇÃO
// Busca a cadeia dos vizinhos e adota a MAIS LONGA que for VÁLIDA.
void sincronizarComVizinhos();

// Tarefa de fundo (rodar em uma thread): a cada 3 s confere a própria cadeia e sincroniza.
void vigiarCadeia();

// SERVIDOR HTTP / API REST
// Registra todas as rotas e começa a atender. Só retorna se a porta não abrir.
bool iniciarServidor(int porta);

// TERMINAL DO SERVIDOR (console interativo do nó, em terminalServidor.cpp)
// Guarda cada requisição recebida (o logger do servidor HTTP chama isto).
void registrarRequisicaoRecebida(const string &metodo, const string &caminho, const string &origem,
                                 int codigoStatus, const string &corpo);
// Lê comandos do teclado. Rodar em uma thread. Só ativa se a entrada for um terminal.
void iniciarTerminalServidor();

// PEÇAS COMPARTILHADAS ENTRE OS ARQUIVOS DE ROTAS
namespace httplib {
    class Server;
    struct Request;
    struct Response;
}

// Envia 'corpoJson' como JSON com o status dado.
void enviarJson(httplib::Response &resposta, int codigoStatus, const json &corpoJson);
// Resposta 501 para rotas que dependem de algo que o ledger ainda não tem.
void responderNaoImplementado(httplib::Response &resposta, const string &detalhe);

void registrarRotasConsorcio(httplib::Server &servidor);   // /api/consorcio  (nó <-> nó)
void registrarRotasCliente(httplib::Server &servidor);     // /api/cliente    (motoristas e passageiros)

// Trecho de regex que casa um identificador na URL (hash do bloco, id de carona...).
inline const string regexIdentificador = "([A-Za-z0-9_-]+)";
