/*
 * VaiJunto, nó de ledger distribuído (versão básica)
 *
 * UM programa, instanciado CINCO vezes (uma por "empresa"):
 *   0 Skyrim | 1 Solsthein | 2 Apocrypha | 3 Shivering Isles | 4 Deadlands
 *
 * Uso (os mesmos 5 endereços, na mesma ordem, em TODOS os nós):
 *   ./vaijunto_no <id 0-4> <end0> <end1> <end2> <end3> <end4>
 *   ex.: ./vaijunto_no 0 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 127.0.0.1:8084 127.0.0.1:8085
 */
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "protocolo.hpp"

using namespace std;

int main(int argc, char **argv) {
    if (argc != 2 + numeroServidores) {
        cerr << "Uso: " << argv[0] << " <id 0-" << (numeroServidores - 1) << "> <end0> <end1> <end2> <end3> <end4>\n"
             << "Ex.: " << argv[0] << " 0 127.0.0.1:8081 127.0.0.1:8082 127.0.0.1:8083 127.0.0.1:8084 127.0.0.1:8085\n";
        return 1;
    }

    meuId = atoi(argv[1]);
    if (meuId < 0 || meuId >= numeroServidores) {
        cerr << "Erro: id deve ser entre 0 e " << (numeroServidores - 1) << "\n";
        return 1;
    }

    // Lê os endereços dos 5 nós ("host:porta")
    for (int i = 0; i < numeroServidores; i++) {
        string endereco = argv[2 + i];
        size_t posicaoSeparador = endereco.rfind(':');
        if (posicaoSeparador == string::npos) {
            cerr << "Endereco invalido: " << endereco << "\n";
            return 1;
        }

        string host = endereco.substr(0, posicaoSeparador);
        int porta = atoi(endereco.substr(posicaoSeparador + 1).c_str());
        if (host.empty() || porta <= 0 || porta > 65535) {
            cerr << "Host ou porta invalida em: " << endereco << "\n";
            return 1;
        }

        nosDaRede.push_back({nomesServidores[i], host, porta});
    }

    meuNome = nomesServidores[meuId];
    filesystem::create_directories("ledgers");
    caminhoLedger = "ledgers/ledger_" + to_string(meuId) + ".json";

    carregarLedger();                // Lê (e revalida) o ledger salvo em disco ou cria o gênesis
    thread(vigiarCadeia).detach();   // Tarefa de fundo: sincroniza e vigia adulteração
    thread(iniciarTerminalServidor).detach();  // Console interativo (só se houver terminal)

    const NoDaRede &esteNo = nosDaRede[meuId];
    logar("Ouvindo em " + esteNo.host + ":" + to_string(esteNo.porta) +
          " | Painel: http://" + esteNo.host + ":" + to_string(esteNo.porta) + "/");

    if (!iniciarServidor(esteNo.porta)) {
        cerr << "Nao consegui abrir a porta " << esteNo.porta << " (ja esta em uso?)\n";
        return 1;
    }

    return 0;
}
