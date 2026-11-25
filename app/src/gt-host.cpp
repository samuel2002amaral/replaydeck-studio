/* Títulos GT no Studio: cada título é uma ENTRADA (tipo instant_replay_gt).
 * O painel GT (replay/gt/gt-dock.cpp) fala com a "biblioteca" e os "canais de overlay";
 * aqui as duas coisas viram as entradas e os overlays 1 a 4 do Studio. */
#include "engine.hpp"
#include "gt-host.hpp"

#include "../replay/overlay-channels.hpp"
#include "../replay/gt/gt-source.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cstring>

static bool isGt(const Input *in)
{
	return in && in->source && strcmp(obs_source_get_id(in->source), GT_SOURCE_ID) == 0;
}

static Input *inputOfSource(obs_source_t *src)
{
	if (!src)
		return nullptr;
	for (Input *in : Engine::get().inputs())
		if (in->source == src)
			return in;
	return nullptr;
}

/* o painel identifica pelo nome da fonte ("Nome #id": único) */
static Input *inputOfSourceName(const std::string &name)
{
	if (name.empty())
		return nullptr;
	for (Input *in : Engine::get().inputs())
		if (in->source && name == obs_source_get_name(in->source))
			return in;
	return nullptr;
}

static QString uniqueInputName(const QString &wanted)
{
	const QString base = wanted.trimmed().isEmpty() ? QStringLiteral("Título") : wanted.trimmed();
	QString n = base;
	for (int i = 2; Engine::get().inputByName(n); i++)
		n = QString("%1 %2").arg(base).arg(i);
	return n;
}

/* ---------- biblioteca = entradas GT ---------- */

void gtLibraryInit() {}
void gtLibraryShutdown() {}

std::vector<obs_source_t *> gtLibraryList()
{
	std::vector<obs_source_t *> out;
	for (Input *in : Engine::get().inputs())
		if (isGt(in))
			out.push_back(obs_source_get_ref(in->source));
	return out;
}

obs_source_t *gtLibraryFind(const char *name)
{
	if (!name)
		return nullptr;
	Input *in = Engine::get().inputByName(QString::fromUtf8(name));
	if (!isGt(in))
		in = inputOfSourceName(name);
	return isGt(in) ? obs_source_get_ref(in->source) : nullptr;
}

obs_source_t *gtLibraryAdd(const QString &file)
{
	OBSDataAutoRelease st = obs_data_create();
	obs_data_set_string(st, "file", file.toUtf8().constData());
	obs_data_set_int(st, "overlay", 1);
	Input *in = Engine::get().addInput(GT_SOURCE_ID, uniqueInputName(QFileInfo(file).completeBaseName()), st);
	return in ? obs_source_get_ref(in->source) : nullptr;
}

obs_source_t *gtLibraryNewBlank(const QString &wanted)
{
	Engine &e = Engine::get();
	const QString dir = e.configDir() + "/titulos";
	QDir().mkpath(dir);
	QString base = uniqueInputName(wanted.trimmed().isEmpty() ? QStringLiteral("Título novo") : wanted);
	base.replace(QRegularExpression(R"re([\\/:*?"<>|])re"), "-");
	QString file = dir + "/" + base + ".gtxml";
	for (int i = 2; QFile::exists(file); i++)
		file = dir + QString("/%1 (%2).gtxml").arg(base).arg(i);
	const int W = (int)e.baseWidth(), H = (int)e.baseHeight();
	const QString xml = QString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
				    "<Composition Width=\"%1\" Height=\"%2\">\n"
				    "  <Layer Name=\"Camada 1\" Location=\"0,0\" Dimensions=\"%1,%2\">\n"
				    "    <Layer.Composition><Composition Width=\"%1\" Height=\"%2\" /></Layer.Composition>\n"
				    "  </Layer>\n"
				    "</Composition>\n")
				    .arg(W)
				    .arg(H);
	QFile out(file);
	if (!out.open(QIODevice::WriteOnly))
		return nullptr;
	out.write(xml.toUtf8());
	out.close();
	return gtLibraryAdd(file);
}

obs_source_t *gtLibraryDuplicate(obs_source_t *src, const QString &wanted)
{
	if (!src)
		return nullptr;
	OBSDataAutoRelease from = obs_source_get_settings(src);
	OBSDataAutoRelease st = obs_data_create();
	obs_data_apply(st, from);
	Input *in = Engine::get().addInput(GT_SOURCE_ID, uniqueInputName(wanted), st);
	return in ? obs_source_get_ref(in->source) : nullptr;
}

bool gtRename(obs_source_t *src, const QString &wanted)
{
	Input *in = inputOfSource(src);
	const QString name = wanted.trimmed();
	if (!in || name.isEmpty())
		return false;
	Input *other = Engine::get().inputByName(name);
	if (other && other != in)
		return false;
	Engine::get().renameInput(in->id, name);
	return true;
}

void gtLibraryRemove(obs_source_t *src)
{
	if (Input *in = inputOfSource(src))
		Engine::get().removeInput(in->id);
}

QString gtDisplayName(obs_source_t *src)
{
	if (Input *in = inputOfSource(src))
		return in->name;
	return src ? QString::fromUtf8(obs_source_get_name(src)) : QString();
}

/* ---------- canais de overlay = overlays 1..4 do Studio ---------- */

OverlayChannels &overlays()
{
	static OverlayChannels o;
	return o;
}

OverlaySlot OverlayChannels::slot(int i)
{
	OverlaySlot s;
	if (Input *in = Engine::get().input(Engine::get().overlay(i + 1)))
		s.source = obs_source_get_name(in->source);
	return s;
}

void OverlayChannels::setSlot(int, const OverlaySlot &) {}

bool OverlayChannels::isOn(int i)
{
	return i >= 0 && i < OVERLAY_COUNT && Engine::get().overlay(i + 1) != 0;
}

void OverlayChannels::setOn(int i, bool on)
{
	if (i < 0 || i >= OVERLAY_COUNT)
		return;
	const int cur = Engine::get().overlay(i + 1);
	if (!on && cur)
		Engine::get().toggleOverlay(i + 1, cur); /* mesma entrada = desliga (com a saída animada) */
}

void OverlayChannels::toggle(int i)
{
	setOn(i, false);
}

void OverlayChannels::assignOn(int i, const std::string &source)
{
	if (i < 0 || i >= OVERLAY_COUNT)
		return;
	Input *in = inputOfSourceName(source);
	if (in && Engine::get().overlay(i + 1) != in->id)
		Engine::get().toggleOverlay(i + 1, in->id);
}

int OverlayChannels::channelOf(const std::string &source)
{
	Input *in = inputOfSourceName(source);
	if (!in)
		return -1;
	for (int i = 0; i < OVERLAY_COUNT; i++)
		if (Engine::get().overlay(i + 1) == in->id)
			return i;
	return -1;
}

/* ---------- ligação ---------- */

namespace gthost {

void install()
{
	registerGtSource();
	registerGtStinger();
	/* título saindo de um overlay: anima a saída e só depois tira da tela */
	Engine::beforeOverlayOff = [](obs_source_t *src) -> int {
		if (!gtIsSource(src))
			return 0;
		const int st = gtState(src);
		if (st != 1 && st != 2) /* não está no ar (já saiu) */
			return 0;
		const int ms = gtOutLengthMs(src);
		if (ms <= 0)
			return 0;
		gtPlayOut(src);
		return ms + 120;
	};
}

} // namespace gthost
