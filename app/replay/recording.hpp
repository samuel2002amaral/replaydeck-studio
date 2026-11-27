#pragma once
#include "common.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

/* Armazenamento em disco de UM ângulo de câmera.
 *
 * O vídeo é gravado como "elementary stream" intra-frame (cada quadro é um
 * pacote independente: H.264/HEVC all-IDR em Annex-B ou MJPEG). Um índice em
 * memória (e espelhado num .idx) mapeia nº do quadro -> offset/tamanho, o que
 * permite acesso aleatório instantâneo, reverso e quadro a quadro enquanto o
 * arquivo ainda está crescendo. */
class Recording {
public:
	~Recording();

	bool open(const std::string &videoPath, const std::string &indexPath);
	void append(int64_t frameNo, const uint8_t *data, size_t size);
	void closeWriter();
	/* Reabre uma gravação antiga (só leitura) a partir do .idx */
	bool openExisting(const std::string &videoPath, const std::string &indexPath);
	uint64_t bytesWritten() const { return writePos.load(); }
	/* quadros que nunca chegaram (buraco na numeração): perdidos de verdade */
	uint64_t gapFrames() const { return gaps.load(); }
	/* quanto está na fila esperando o disco (MB) — o disco engasgando aparece aqui */
	size_t pendingMB() const { return wqBytes.load() >> 20; }
	/* câmera gravada com 1 de cada N quadros: os N-1 do meio repetem o anterior (não são perda) */
	int repeatFill = 0;

	/* Lê o quadro pedido ou o quadro válido imediatamente anterior
	 * (se a câmera perdeu aquele quadro). */
	bool read(int64_t frameNo, std::vector<uint8_t> &out, int64_t &actualFrame);

	int64_t firstFrame() const;
	int64_t lastFrame() const;

	int codecId = 0; /* AVCodecID */
	int width = 0;
	int height = 0;
	std::string path;

private:
	struct Entry {
		uint64_t offset;
		uint32_t size;
		uint32_t reserved;
	};
	struct DiskEntry {
		int64_t frameNo;
		uint64_t offset;
		uint32_t size;
		uint32_t reserved;
	};

	mutable std::mutex idxMtx;
	std::vector<Entry> index;
	int64_t first = -1;
	int64_t last = -1;

	std::mutex readMtx;
	FILE *wf = nullptr;
	FILE *xf = nullptr;
	FILE *rf = nullptr;
	std::atomic<uint64_t> writePos{0};
	int idxFlushCounter = 0;

	/* Disco num fio próprio: a câmera (codificador) só entrega o quadro na fila e volta.
	 * Um engasgo do disco (antivírus, SSD ocupado) é absorvido pela fila (até 1 GB,
	 * ~15 s com 4 câmeras 1080p60) em vez de segurar a gravação. */
	struct Pending {
		int64_t frameNo;
		std::vector<uint8_t> data;
		int64_t fillFrom = -1; /* >= 0: quadros fillFrom..frameNo-1 repetem o anterior */
	};
	Entry lastEntry{0, 0, 0};
	bool haveLastEntry = false;
	void writerLoop();
	void writeOne(Pending &p);
	std::thread writer;
	std::mutex wqMtx;
	std::condition_variable wqCv, wqRoom;
	std::deque<Pending> wq;
	std::atomic<size_t> wqBytes{0};
	bool wqStop = false;
	int64_t lastQueued = -1;
	std::atomic<uint64_t> gaps{0};
};
