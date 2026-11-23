#include "recording.hpp"
#include <util/platform.h>
#include <cstring>

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
	return true;
}

void Recording::append(int64_t frameNo, const uint8_t *data, size_t size)
{
	if (!wf || frameNo < 0 || !size)
		return;
	{
		std::lock_guard<std::mutex> lk(idxMtx);
		if (frameNo <= last)
			return; /* quadro duplicado/atrasado */
	}

	if (fwrite(data, 1, size, wf) != size) {
		rlog(LOG_ERROR, "falha de escrita em %s (disco cheio?)", path.c_str());
		return;
	}
	/* flush para que os canais de playback enxerguem o quadro na hora */
	fflush(wf);

	Entry e{writePos.load(), (uint32_t)size, 0};
	writePos += size;

	if (xf) {
		DiskEntry d{frameNo, e.offset, e.size, 0};
		fwrite(&d, sizeof(d), 1, xf);
		if (++idxFlushCounter >= 30) {
			idxFlushCounter = 0;
			fflush(xf);
		}
	}

	std::lock_guard<std::mutex> lk(idxMtx);
	if ((size_t)frameNo >= index.size())
		index.resize((size_t)frameNo + 1, Entry{0, 0, 0});
	index[(size_t)frameNo] = e;
	if (first < 0)
		first = frameNo;
	last = frameNo;
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
