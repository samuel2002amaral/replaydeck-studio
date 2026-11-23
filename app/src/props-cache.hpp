/* Opções das entradas preparadas ANTES do clique.
 *
 * Na câmera, montar a lista de opções faz o Windows varrer todos os dispositivos e resoluções
 * (leva segundos). O cache faz isso em segundo plano quando o programa abre; ao clicar em
 * "Adicionar nova entrada" as opções já estão prontas. Cada vez que uma é usada, outra é preparada
 * (assim uma câmera ligada depois aparece na vez seguinte). Não procura sozinho de tempos em tempos. */
#pragma once

#include <obs.h>

#include <QByteArray>
#include <QList>

#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>

class PropsCache {
public:
	static PropsCache &get();

	void start(const QList<QByteArray> &typeIds);
	void stop();

	bool isReady(const QByteArray &typeId);
	/* entrega as opções prontas (e as configurações usadas nelas); quem recebe passa a ser dono.
	 * Já pede para preparar a próxima. false se ainda não estão prontas. */
	bool take(const QByteArray &typeId, obs_properties_t **props, obs_data_t **settings);
	void refreshAll();

private:
	PropsCache() = default;
	void run();

	struct Entry {
		obs_properties_t *props = nullptr;
		obs_data_t *settings = nullptr;
		bool wanted = true;
	};

	std::mutex m;
	std::condition_variable cv;
	std::map<std::string, Entry> entries;
	std::thread worker;
	bool running = false;
	bool refreshRequested = false;
};
