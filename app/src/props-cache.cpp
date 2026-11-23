#include "props-cache.hpp"

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#endif

static constexpr auto REFRESH_EVERY = std::chrono::minutes(2);

PropsCache &PropsCache::get()
{
	static PropsCache c;
	return c;
}

void PropsCache::start(const QList<QByteArray> &typeIds)
{
	std::lock_guard<std::mutex> l(m);
	for (const QByteArray &t : typeIds)
		entries[t.toStdString()].wanted = true;
	if (running)
		return;
	running = true;
	worker = std::thread([this] { run(); });
}

void PropsCache::stop()
{
	{
		std::lock_guard<std::mutex> l(m);
		if (!running)
			return;
		running = false;
	}
	cv.notify_all();
	if (worker.joinable())
		worker.join();
	for (auto &kv : entries) {
		obs_properties_destroy(kv.second.props);
		obs_data_release(kv.second.settings);
	}
	entries.clear();
}

bool PropsCache::isReady(const QByteArray &typeId)
{
	std::lock_guard<std::mutex> l(m);
	auto it = entries.find(typeId.toStdString());
	return it != entries.end() && it->second.props;
}

bool PropsCache::take(const QByteArray &typeId, obs_properties_t **props, obs_data_t **settings)
{
	{
		std::lock_guard<std::mutex> l(m);
		Entry &e = entries[typeId.toStdString()];
		if (!e.props) {
			e.wanted = true;
			cv.notify_all();
			return false;
		}
		*props = e.props;
		*settings = e.settings;
		e.props = nullptr;
		e.settings = nullptr;
		e.wanted = true; /* prepara a próxima */
	}
	cv.notify_all();
	return true;
}

void PropsCache::refreshAll()
{
	{
		std::lock_guard<std::mutex> l(m);
		refreshRequested = true;
	}
	cv.notify_all();
}

void PropsCache::run()
{
#ifdef _WIN32
	const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); /* a câmera (DirectShow) usa COM */
#endif
	auto lastFull = std::chrono::steady_clock::now();
	std::unique_lock<std::mutex> l(m);
	while (running) {
		/* refaz tudo de tempos em tempos, ou quando pedido */
		if (refreshRequested || std::chrono::steady_clock::now() - lastFull >= REFRESH_EVERY) {
			refreshRequested = false;
			lastFull = std::chrono::steady_clock::now();
			for (auto &kv : entries)
				kv.second.wanted = true;
		}

		std::string next;
		for (auto &kv : entries) {
			if (kv.second.wanted) {
				next = kv.first;
				break;
			}
		}
		if (next.empty()) {
			cv.wait_for(l, std::chrono::seconds(10));
			continue;
		}
		entries[next].wanted = false;

		/* a parte lenta, sem segurar a trava */
		l.unlock();
		obs_data_t *settings = obs_get_source_defaults(next.c_str());
		if (!settings)
			settings = obs_data_create();
		obs_properties_t *props = obs_get_source_properties(next.c_str());
		if (props)
			obs_properties_apply_settings(props, settings);
		l.lock();

		Entry &e = entries[next];
		obs_properties_destroy(e.props);
		obs_data_release(e.settings);
		e.props = props;
		e.settings = settings;
		if (!props)
			e.wanted = false; /* tipo não existe aqui */
	}
	l.unlock();
#ifdef _WIN32
	if (SUCCEEDED(com))
		CoUninitialize();
#endif
}
