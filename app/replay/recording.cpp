#include "recording.hpp"
#include <util/platform.h>
#include <util/threading.h>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static constexpr size_t kMaxPending = (size_t)1 << 30; /* 1 GB esperando o disco, no máximo */

Recording::~Recording()
{
	closeWriter();
	if (rf)
		fclose(rf);
}

bool Recording::open(const std::string &videoPath, const std::string &indexPath)
{
	path = videoPath;
	wf = os_fopen(videoPath.c_str(), "wb");
	if (!wf)
		return false;
	xf = os_fopen(indexPath.c_str(), "wb");
	rf = os_fopen(videoPath.c_str(), "rb");
	if (!rf) {
		closeWriter();
		return false;
	}
	if (xf) {
		const char magic[8] = {'R', 'P', 'L', 'I', 'D', 'X', '0', '1'};
		fwrite(magic, 1, sizeof(magic), xf);
	}
	wqStop = false;
	writer = std::thread(&Recording::writerLoop, this);
	return true;
}

void Recording::writerLoop()
{
	os_set_thread_name("replay-disco");
#ifdef _WIN32
	/* gravação do replay (VAR) na frente de tudo que não é vídeo */
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
	std::deque<Pending> batch;
	for (;;) {
		{
			std::unique_lock<std::mutex> lk(wqMtx);
			wqCv.wait(lk, [&] { return wqStop || !wq.empty(); });
			if (wq.empty())
				break; /* parando e tudo escrito */
			batch.swap(wq);
		}
		/* teste: disco lento/engasgado (RDS_DISCO_LENTO_MS por lote) */
		static const int slowMs = getenv("RDS_DISCO_LENTO_MS") ? atoi(getenv("RDS_DISCO_LENTO_MS")) : 0;
		if (slowMs > 0)
			os_sleep_ms((uint32_t)slowMs);
		size_t done = 0;
		for (Pending &p : batch) {
			writeOne(p);
			done += p.data.size();
		}
		/* flush uma vez por lote: o replay enxerga o quadro na hora, sem custo por quadro */
		if (wf)
			fflush(wf);
		batch.clear();
		wqBytes -= done;
		wqRoom.notify_all();
	}
}

void Recording::append(int64_t frameNo, const uint8_t *data, size_t size)
{
	if (!wf || frameNo < 0 || !size)
		return;
	std::unique_lock<std::mutex> lk(wqMtx);
	if (wqStop || frameNo <= lastQueued)
		return; /* quadro duplicado/atrasado */
	if (lastQueued >= 0 && frameNo > lastQueued + 1)
		gaps += (uint64_t)(frameNo - lastQueued - 1);
	lastQueued = frameNo;
	/* disco parado há muito tempo: aí sim espera (nunca descarta quadro) */
	if (wqBytes.load() + size > kMaxPending) {
		rlog(LOG_WARNING, "%s: o disco não está dando conta (%zu MB na fila)", path.c_str(), wqBytes.load() >> 20);
		wqRoom.wait(lk, [&] { return wqStop || wqBytes.load() + size <= kMaxPending; });
		if (wqStop)
			return;
	}
	wq.push_back(Pending{frameNo, std::vector<uint8_t>(data, data + size)});
	wqBytes += size;
	lk.unlock();
	wqCv.notify_one();
}

void Recording::writeOne(Pending &p)
{
	const size_t size = p.data.size();
	if (fwrite(p.data.data(), 1, size, wf) != size) {
		rlog(LOG_ERROR, "falha de escrita em %s (disco cheio?)", path.c_str());
		return;
	}
	Entry e{writePos.load(), (uint32_t)size, 0};
	writePos += size;

	if (xf) {
		DiskEntry d{p.frameNo, e.offset, e.size, 0};
		fwrite(&d, sizeof(d), 1, xf);
		if (++idxFlushCounter >= 30) {
			idxFlushCounter = 0;
			fflush(xf);
		}
	}

	std::lock_guard<std::mutex> lk(idxMtx);
	if ((size_t)p.frameNo >= index.size())
		index.resize((size_t)p.frameNo + 1, Entry{0, 0, 0});
	index[(size_t)p.frameNo] = e;
	if (first < 0)
		first = p.frameNo;
	last = p.frameNo;
}

bool Recording::openExisting(const std::string &videoPath, const std::string &indexPath)
{
	path = videoPath;
	rf = os_fopen(videoPath.c_str(), "rb");
	if (!rf)
		return false;
	FILE *x = os_fopen(indexPath.c_str(), "rb");
	if (!x)
		return false;
	char magic[8];
	if (fread(magic, 1, sizeof(magic), x) != sizeof(magic) || memcmp(magic, "RPLIDX01", 8) != 0) {
		fclose(x);
		return false;
	}
	std::lock_guard<std::mutex> lk(idxMtx);
	DiskEntry d;
	while (fread(&d, sizeof(d), 1, x) == 1) {
		if (d.frameNo < 0 || d.frameNo > 100000000 || !d.size)
			continue;
		if ((size_t)d.frameNo >= index.size())
			index.resize((size_t)d.frameNo + 1, Entry{0, 0, 0});
		index[(size_t)d.frameNo] = Entry{d.offset, d.size, 0};
		if (first < 0)
			first = d.frameNo;
		if (d.frameNo > last)
			last = d.frameNo;
		writePos = d.offset + d.size;
	}
	fclose(x);
	return first >= 0;
}

void Recording::closeWriter()
{
	/* escreve o que ainda está na fila antes de fechar */
	{
		std::lock_guard<std::mutex> lk(wqMtx);
		wqStop = true;
	}
	wqCv.notify_all();
	wqRoom.notify_all();
	if (writer.joinable())
		writer.join();
	if (wf) {
		fclose(wf);
		wf = nullptr;
	}
	if (xf) {
		fclose(xf);
		xf = nullptr;
	}
}

bool Recording::read(int64_t frameNo, std::vector<uint8_t> &out, int64_t &actualFrame)
{
	Entry e{0, 0, 0};
	{
		std::lock_guard<std::mutex> lk(idxMtx);
		if (first < 0)
			return false;
		if (frameNo > last)
			frameNo = last;
		if (frameNo < first)
			frameNo = first;
		int64_t f = frameNo;
		while (f >= first && index[(size_t)f].size == 0)
			f--;
		if (f < first)
			return false;
		e = index[(size_t)f];
		actualFrame = f;
	}

	std::lock_guard<std::mutex> lk(readMtx);
	if (!rf)
		return false;
	out.resize(e.size);
	if (os_fseeki64(rf, (int64_t)e.offset, SEEK_SET) != 0)
		return false;
	return fread(out.data(), 1, e.size, rf) == e.size;
}

int64_t Recording::firstFrame() const
{
	std::lock_guard<std::mutex> lk(idxMtx);
	return first;
}

int64_t Recording::lastFrame() const
{
	std::lock_guard<std::mutex> lk(idxMtx);
	return last;
}
