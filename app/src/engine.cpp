#include "engine.hpp"
#include "gpu.hpp"

#include <QTimer>
#include <QUuid>
#include "props-cache.hpp"

#include <obs-module.h>
#include <util/base.h>
#include <util/platform.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <mutex>

/* canal de saída do áudio "sempre ligado" (0 = programa, 1-4 = overlays) */
static const int AUDIO_BED_CHANNEL = 5;

/* ---------- log em arquivo: config/ReplayDeckStudio/logs/AAAA-MM-DD hh-mm-ss.txt ---------- */

static FILE *logFile = nullptr;
static std::mutex logMutex;

static void logHandler(int level, const char *format, va_list args, void *)
{
	char msg[8192];
	vsnprintf(msg, sizeof(msg), format, args);
	const char *tag = level <= LOG_ERROR ? "erro" : level <= LOG_WARNING ? "aviso" : level <= LOG_INFO ? "info" : "debug";
	std::lock_guard<std::mutex> l(logMutex);
	const QByteArray ts = QTime::currentTime().toString("hh:mm:ss.zzz").toUtf8();
	if (logFile) {
		fprintf(logFile, "%s [%s] %s\n", ts.constData(), tag, msg);
		fflush(logFile);
	}
#ifndef NDEBUG
	fprintf(stderr, "%s [%s] %s\n", ts.constData(), tag, msg);
#endif
}

Engine &Engine::get()
{
	static Engine e;
	return e;
}

QString Engine::typeDisplayName(const char *id)
{
	const char *n = obs_source_get_display_name(id);
	return n ? QString::fromUtf8(n) : QString::fromUtf8(id);
}

QString Engine::defaultProjectPath() const
{
	return cfgDir + "/projeto.rdsproj";
}

bool Engine::start(QString &error)
{
	/* o motor procura módulos e dados a partir da pasta do programa (bin/64bit) */
	const QString exeDir = QCoreApplication::applicationDirPath();
	QDir::setCurrent(exeDir);

#ifdef __APPLE__
	/* Mac: dentro do .app não se escreve (quebra a assinatura): ~/Library/Application Support */
	cfgDir = QDir::homePath() + "/Library/Application Support/ReplayDeckStudio";
	QDir().mkpath(cfgDir + "/logs");
#else
	/* configurações ficam junto do programa (portátil): <pasta>/config/ReplayDeckStudio */
	cfgDir = QDir::cleanPath(exeDir + "/../../config/ReplayDeckStudio");
	if (!QDir().mkpath(cfgDir + "/logs")) {
		/* pasta do programa sem permissão de escrita: usa a pasta do usuário */
		cfgDir = QDir::homePath() + "/AppData/Roaming/ReplayDeckStudio";
		QDir().mkpath(cfgDir + "/logs");
	}
#endif
	const QString logPath = cfgDir + "/logs/" + QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss") + ".txt";
	logFile = fopen(QFile::encodeName(logPath).constData(), "w");
	base_set_log_handler(logHandler, nullptr);
	blog(LOG_INFO, "ReplayDeck Studio %s iniciando (motor libobs %s)", RDS_VERSION, obs_get_version_string());

	QDir().mkpath(cfgDir + "/plugin_config");
	const QByteArray pluginCfg = QFile::encodeName(cfgDir + "/plugin_config");
	if (!obs_startup("pt-BR", pluginCfg.constData(), nullptr)) {
		error = "O motor de vídeo não iniciou (obs_startup).";
		return false;
	}

	obs_audio_info ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	if (!obs_reset_audio(&ai)) {
		error = "Não foi possível iniciar o áudio.";
		return false;
	}

	{
		/* resolução e fps escolhidos em Configurações (configuracoes.json) */
		OBSDataAutoRelease st = obs_data_create_from_json_file_safe(
			QFile::encodeName(cfgDir + "/configuracoes.json").constData(), "bak");
		if (st)
			adapterWanted = QString::fromUtf8(obs_data_get_string(st, "placa_video"));
		if (st && obs_data_has_user_value(st, "largura")) {
			const int w = (int)obs_data_get_int(st, "largura"), h = (int)obs_data_get_int(st, "altura");
			const int n = (int)obs_data_get_int(st, "fps_num"), d = (int)obs_data_get_int(st, "fps_den");
			if (w >= 320 && h >= 240 && n > 0 && d > 0) {
				baseW = (uint32_t)w;
				baseH = (uint32_t)h;
				fpsNum = (uint32_t)n;
				fpsDen = (uint32_t)d;
			}
		}
	}
	obs_video_info ovi = {};
#ifdef _WIN32
	ovi.graphics_module = "libobs-d3d11";
#elif defined(__APPLE__)
	/* no Mac o módulo de vídeo fica em Contents/Frameworks com o número da versão no nome
	 * (libobs-opengl.33.dylib) ou como .framework, conforme a compilação do motor */
	static QByteArray glModule = "libobs-opengl.dylib";
	{
		const QDir fw(QCoreApplication::applicationDirPath() + "/../Frameworks");
		const QStringList found =
			fw.entryList({"libobs-opengl*.dylib", "libobs-opengl*.so", "libobs-opengl.framework"},
				     QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
		for (const QString &f : found) {
			QString path = fw.absoluteFilePath(f);
			if (f.endsWith(".framework"))
				path += "/libobs-opengl";
			glModule = QFile::encodeName(QDir::cleanPath(path));
			break;
		}
		blog(LOG_INFO, "módulo de vídeo: %s", glModule.constData());
	}
	ovi.graphics_module = glModule.constData();
#else
	ovi.graphics_module = "libobs-opengl";
#endif
	ovi.fps_num = fpsNum;
	ovi.fps_den = fpsDen;
	ovi.base_width = baseW;
	ovi.base_height = baseH;
	ovi.output_width = baseW;
	ovi.output_height = baseH;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.adapter = (uint32_t)gpu::pick(adapterWanted, &adapterUsed);
	blog(LOG_INFO, "vídeo na placa: %s (%s)", adapterUsed.toUtf8().constData(),
	     adapterWanted.isEmpty() || adapterWanted == "auto" ? "automático" : "escolhida em Configurações");
	ovi.gpu_conversion = true;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	const int rv = obs_reset_video(&ovi);
	if (rv != OBS_VIDEO_SUCCESS) {
		error = rv == OBS_VIDEO_NOT_SUPPORTED
#ifdef _WIN32
				? "A placa de vídeo não é compatível (precisa de DirectX 11)."
#else
				? "A placa de vídeo não é compatível (precisa de OpenGL 3.3)."
#endif
				: QString("Não foi possível iniciar o vídeo (código %1). Atualize o driver da placa de vídeo.").arg(rv);
		return false;
	}

	/* só os módulos do motor; os que dependem da interface do OBS ficam de fora */
	static const char *modules[] = {"win-capture", "win-dshow", "win-wasapi", "obs-ffmpeg", "obs-outputs", "obs-x264",
					"obs-nvenc", "obs-qsv11", "image-source", "obs-filters", "obs-transitions",
					"obs-text", "text-freetype2", "rtmp-services", "obs-browser", "vlc-video",
					"decklink", "aja", "nv-filters", "obs-vst", "obs-webrtc", "coreaudio-encoder",
					"linux-capture", "linux-v4l2", "linux-pulseaudio", "linux-alsa", "linux-pipewire",
					"mac-avcapture", "mac-capture", "mac-videotoolbox", "mac-syphon"};
	for (const char *m : modules)
		obs_add_safe_module(m);
#if LIBOBS_API_MAJOR_VER >= 32
	obs_load_core_modules();
#else
	obs_load_all_modules();
#endif
	if (loadExtraModules)
		loadExtraModules();
	obs_post_load_modules();
	obs_log_loaded_modules();

	cutTr = obs_source_create_private("cut_transition", "Corte", nullptr);
	fadeTr = obs_source_create_private("fade_transition", "Fade", nullptr);
	if (!cutTr || !fadeTr) {
		error = "As transições do motor não carregaram (módulo obs-transitions).";
		return false;
	}
	obs_scene_t *black = obs_scene_create_private("Preto (FTB)");
	blackScene = obs_scene_get_source(black); /* a cena é dona da fonte: guardamos a referência */
	obs_source_get_ref(blackScene);
	obs_scene_release(black);

	activeTr = fadeTr;
	obs_set_output_source(0, fadeTr);

	/* áudio "sempre ligado" (microfones): uma cena só de áudio no canal 5, sem imagem */
	obs_scene_t *bed = obs_scene_create_private("Áudio sempre ligado");
	audioBed = obs_scene_get_source(bed);
	obs_source_get_ref(audioBed);
	obs_scene_release(bed);
	obs_set_output_source(AUDIO_BED_CHANNEL, audioBed);
	connect(this, &Engine::busChanged, this, &Engine::syncAudioBed);
	connect(this, &Engine::inputsChanged, this, &Engine::syncAudioBed);
	connect(this, &Engine::inputChanged, this, &Engine::syncAudioBed);
	connect(this, &Engine::inputLayoutChanged, this, &Engine::syncAudioBed);
	{
		/* câmera que muda de resolução: a posição escolhida continua valendo */
		auto *sizes = new QTimer(this);
		connect(sizes, &QTimer::timeout, this, &Engine::checkSourceSizes);
		sizes->start(500);
	}
	started = true;

	/* prepara em segundo plano as opções dos tipos lentos (câmera, placas, áudio, capturas):
	 * ao clicar em adicionar, já está tudo pronto */
	QList<QByteArray> slow;
	const char *id = nullptr, *unver = nullptr;
	/* só tipos conferidos que aceitam montar as opções sem nada aberto (AJA e v4l2 do Linux não) */
	static const char *wanted[] = {"dshow_input", "decklink-input", "wasapi_input_capture", "wasapi_output_capture",
				       "monitor_capture", "window_capture", "game_capture", "macos-avcapture",
				       "coreaudio_input_capture", "coreaudio_output_capture"};
	for (size_t i = 0; obs_enum_input_types2(i, &id, &unver); i++) {
		for (const char *w : wanted)
			if (unver && strcmp(unver, w) == 0 && !(obs_get_source_output_flags(id) & OBS_SOURCE_DEPRECATED))
				slow << QByteArray(id);
	}
	PropsCache::get().start(slow);

	if (beforeProjectLoad)
		beforeProjectLoad();
	QString p = defaultProjectPath();
	if (QFile::exists(p))
		loadProject(p);
	projPath = p;
	return true;
}

void Engine::stop()
{
	if (!started)
		return;
	saveProject(projPath.isEmpty() ? defaultProjectPath() : projPath);
	PropsCache::get().stop();
	for (int ch = 0; ch < 64; ch++)
		obs_set_output_source(ch, nullptr);
	clearAll();
	clearTransitions();
	cutTr = nullptr;
	fadeTr = nullptr;
	blackScene = nullptr;
	audioBed = nullptr;
	activeTr = nullptr;
	started = false;
	obs_shutdown();
	blog(LOG_INFO, "ReplayDeck Studio encerrado");
	base_set_log_handler(nullptr, nullptr);
	if (logFile) {
		fclose(logFile);
		logFile = nullptr;
	}
}

/* ---------- entradas ---------- */

Input *Engine::input(int id) const
{
	for (Input *in : list)
		if (in->id == id)
			return in;
	return nullptr;
}

int Engine::indexOf(int id) const
{
	for (int i = 0; i < list.size(); i++)
		if (list[i]->id == id)
			return i;
	return -1;
}

static void fitToCanvas(obs_sceneitem_t *item, uint32_t w, uint32_t h)
{
	vec2 bounds;
	vec2_set(&bounds, (float)w, (float)h);
	vec2 pos;
	vec2_zero(&pos);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(item, &bounds);
}

/* a cena de cada entrada: a fonte principal embaixo e as camadas (outras entradas) por cima */
static obs_sceneitem_t *mainItem(Input *in)
{
	struct Find {
		obs_source_t *src;
		obs_sceneitem_t *found;
	} f{in->source.Get(), nullptr};
	obs_scene_enum_items(
		obs_scene_from_source(in->scene),
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *f = static_cast<Find *>(param);
			if (obs_sceneitem_get_source(item) != f->src)
				return true;
			f->found = item;
			return false;
		},
		&f);
	return f.found;
}

/* ---------- posição e camadas (como no vMix) ---------- */

bool ItemPos::isDefault() const
{
	return *this == ItemPos();
}

bool ItemPos::operator==(const ItemPos &o) const
{
	auto eq = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
	return eq(zoom, o.zoom) && eq(zoomX, o.zoomX) && eq(zoomY, o.zoomY) && eq(panX, o.panX) && eq(panY, o.panY) &&
	       eq(rotation, o.rotation) && eq(cropX1, o.cropX1) && eq(cropY1, o.cropY1) && eq(cropX2, o.cropX2) &&
	       eq(cropY2, o.cropY2);
}

void Engine::applyItemPos(obs_sceneitem_t *item, obs_source_t *src, const ItemPos &p, uint32_t W, uint32_t H)
{
	if (!item)
		return;
	obs_sceneitem_defer_update_begin(item);
	obs_sceneitem_crop noCrop = {};
	if (p.isDefault()) {
		fitToCanvas(item, W, H);
		obs_sceneitem_set_crop(item, &noCrop);
		obs_sceneitem_set_rot(item, 0.0f);
		obs_sceneitem_defer_update_end(item);
		return;
	}
	const double z = std::max(0.0, p.zoom);
	const double zx = std::max(0.0, p.zoomX), zy = std::max(0.0, p.zoomY);
	/* centro da imagem inteira (sem corte) na tela */
	const double cx = W / 2.0 + p.panX * W / 2.0;
	const double cy = H / 2.0 - p.panY * H / 2.0;
	const uint32_t sw = obs_source_get_width(src), sh = obs_source_get_height(src);
	vec2 pos, bounds;
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
	if (!sw || !sh) {
		/* ainda sem imagem (câmera abrindo): cabe na área; refeito quando a imagem chegar */
		vec2_set(&bounds, (float)(W * z * zx), (float)(H * z * zy));
		vec2_set(&pos, (float)cx, (float)cy);
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds(item, &bounds);
		obs_sceneitem_set_pos(item, &pos);
		obs_sceneitem_set_crop(item, &noCrop);
		obs_sceneitem_set_rot(item, (float)p.rotation);
		obs_sceneitem_defer_update_end(item);
		return;
	}
	const double fit = std::min((double)W / sw, (double)H / sh);
	const double fw = sw * fit * z * zx, fh = sh * fit * z * zy; /* imagem inteira na tela */
	double x1 = std::clamp(p.cropX1, 0.0, 1.0), x2 = std::clamp(p.cropX2, 0.0, 1.0);
	double y1 = std::clamp(p.cropY1, 0.0, 1.0), y2 = std::clamp(p.cropY2, 0.0, 1.0);
	if (x2 < x1)
		std::swap(x1, x2);
	if (y2 < y1)
		std::swap(y1, y2);
	obs_sceneitem_crop c;
	c.left = (int)std::lround(x1 * sw);
	c.right = (int)std::lround((1.0 - x2) * sw);
	c.top = (int)std::lround(y1 * sh);
	c.bottom = (int)std::lround((1.0 - y2) * sh);
	/* sempre sobra pelo menos 1 pixel (corte total = imagem some) */
	if (c.left + c.right >= (int)sw)
		c.right = std::max(0, (int)sw - c.left - 1);
	if (c.top + c.bottom >= (int)sh)
		c.bottom = std::max(0, (int)sh - c.top - 1);
	const double visW = fw * (double)(sw - c.left - c.right) / sw;
	const double visH = fh * (double)(sh - c.top - c.bottom) / sh;
	/* o pedaço que sobra fica onde estava na imagem inteira */
	const double midX = (c.left + (sw - c.right)) / 2.0 / sw, midY = (c.top + (sh - c.bottom)) / 2.0 / sh;
	vec2_set(&pos, (float)(cx + fw * (midX - 0.5)), (float)(cy + fh * (midY - 0.5)));
	vec2_set(&bounds, (float)std::max(visW, 0.0), (float)std::max(visH, 0.0));
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_STRETCH);
	obs_sceneitem_set_bounds(item, &bounds);
	obs_sceneitem_set_crop(item, &c);
	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_rot(item, (float)p.rotation);
	obs_sceneitem_defer_update_end(item);
}

static Input *wrapSource(int id, const QString &name, obs_source_t *src, uint32_t w, uint32_t h)
{
	auto *in = new Input();
	in->id = id;
	in->name = name;
	in->key = QUuid::createUuid().toString(QUuid::WithoutBraces);
	in->source = src; /* fica com a referência */
	const QByteArray sceneName = name.toUtf8(); /* a cena tem o nome da entrada (o replay lista por nome) */
	obs_scene_t *scene = obs_scene_create_private(sceneName.constData());
	obs_sceneitem_t *item = obs_scene_add(scene, src);
	fitToCanvas(item, w, h);
	in->scene = obs_scene_get_source(scene);
	obs_source_get_ref(in->scene);
	obs_scene_release(scene);
	/* áudio: com imagem segue o vídeo (Auto); só áudio (microfone) fica sempre na mistura */
	in->audioAuto = (obs_source_get_output_flags(src) & OBS_SOURCE_VIDEO) != 0;
	/* como no vMix: toda entrada fica ativa (imagem ao vivo na miniatura) */
	obs_source_inc_showing(in->scene);
	return in;
}

Input *Engine::addInput(const QString &sourceType, const QString &name, obs_data_t *settings)
{
	const int id = nextId++;
	const QByteArray type = sourceType.toUtf8();
	const QByteArray srcName = (name + " #" + QString::number(id)).toUtf8();
	obs_source_t *src = obs_source_create_private(type.constData(), srcName.constData(), settings);
	if (!src) {
		blog(LOG_ERROR, "não foi possível criar a entrada do tipo %s", type.constData());
		return nullptr;
	}
	nextId--;
	return adoptSource(src, name);
}

Input *Engine::adoptSource(obs_source_t *src, const QString &name)
{
	if (!src)
		return nullptr;
	const int id = nextId++;
	Input *in = wrapSource(id, name, src, baseW, baseH);
	list.append(in);
	blog(LOG_INFO, "entrada %d criada: %s (%s)", id, name.toUtf8().constData(), obs_source_get_id(src));
	emit inputsChanged();
	if (!pvw)
		setPreview(id);
	return in;
}

Input *Engine::addSharedInput(int fromId, const QString &name)
{
	Input *from = input(fromId);
	if (!from)
		return nullptr;
	const int id = nextId++;
	Input *in = wrapSource(id, name, obs_source_get_ref(from->source), baseW, baseH);
	in->audioOn = from->audioOn;
	in->volume = from->volume;
	in->audioAuto = from->audioAuto;
	in->monitor = from->monitor;
	list.append(in);
	blog(LOG_INFO, "entrada %d criada usando a mesma fonte da entrada %d", id, fromId);
	emit inputsChanged();
	return in;
}

int Engine::sharedFrom(int id) const
{
	Input *in = input(id);
	if (!in)
		return 0;
	for (Input *other : list) {
		if (other == in)
			return 0; /* é a primeira com esta fonte */
		if (other->source.Get() == in->source.Get())
			return other->id;
	}
	return 0;
}

std::function<QString(const char *, obs_data_t *)> Engine::deviceKeyOverride;
std::function<void()> Engine::beforeProjectLoad;
std::function<int(obs_source_t *)> Engine::beforeOverlayOff;
std::function<void()> Engine::loadExtraModules;

QString Engine::deviceKey(const char *unversionedType, obs_data_t *settings)
{
	if (!unversionedType || !settings)
		return QString();
	if (deviceKeyOverride)
		return deviceKeyOverride(unversionedType, settings);
	if (strcmp(unversionedType, "dshow_input") == 0)
		return QString::fromUtf8(obs_data_get_string(settings, "video_device_id"));
	if (strcmp(unversionedType, "decklink-input") == 0)
		return QString::fromUtf8(obs_data_get_string(settings, "device_hash"));
	return QString();
}

int Engine::inputUsingDevice(const char *unversionedType, const QString &key) const
{
	if (key.isEmpty())
		return 0;
	for (Input *in : list) {
		if (strcmp(obs_source_get_unversioned_id(in->source), unversionedType) != 0)
			continue;
		OBSDataAutoRelease st = obs_source_get_settings(in->source);
		if (deviceKey(unversionedType, st) == key)
			return in->id;
	}
	return 0;
}

void Engine::mergeSameDevice()
{
	bool changed = false;
	for (int i = 0; i < list.size(); i++) {
		Input *first = list[i];
		const char *unver = obs_source_get_unversioned_id(first->source);
		OBSDataAutoRelease st = obs_source_get_settings(first->source);
		const QString key = deviceKey(unver, st);
		if (key.isEmpty())
			continue;
		for (int j = i + 1; j < list.size(); j++) {
			Input *dup = list[j];
			if (dup->source.Get() == first->source.Get() ||
			    strcmp(obs_source_get_unversioned_id(dup->source), unver) != 0)
				continue;
			OBSDataAutoRelease st2 = obs_source_get_settings(dup->source);
			if (deviceKey(unver, st2) != key)
				continue;
			/* troca a fonte da entrada repetida pela fonte da primeira */
			obs_scene_t *scene = obs_scene_from_source(dup->scene);
			obs_sceneitem_t *old = obs_scene_find_source(scene, obs_source_get_name(dup->source));
			if (old)
				obs_sceneitem_remove(old);
			obs_sceneitem_t *item = obs_scene_add(scene, first->source);
			obs_sceneitem_set_visible(item, dup->enabled);
			obs_source_remove(dup->source);
			dup->source = obs_source_get_ref(first->source);
			applyLayout(dup); /* posição da entrada e principal embaixo das camadas */
			blog(LOG_INFO, "entrada %d abria o mesmo aparelho da entrada %d: agora usa a mesma fonte", dup->id,
			     first->id);
			changed = true;
		}
	}
	if (changed) {
		emit inputsChanged();
		emit busChanged();
	}
}

void Engine::removeInput(int id)
{
	const int idx = indexOf(id);
	if (idx < 0)
		return;
	for (int ch = 1; ch <= 4; ch++) {
		if (ov[ch] == id) {
			obs_set_output_source(ch, nullptr);
			ov[ch] = 0;
		}
	}
	if (pgm == id) {
		pgm = 0;
		obs_transition_set(activeTr, nullptr);
	}
	if (pvw == id)
		pvw = 0;
	/* sai das camadas das outras entradas */
	for (Input *other : list) {
		bool had = false;
		for (InputLayer &l : other->layers)
			if (l.inputId == id) {
				l.inputId = 0;
				had = true;
			}
		if (had && other->id != id) {
			applyLayout(other);
			emit inputLayoutChanged(other->id);
		}
	}
	Input *in = list.takeAt(idx);
	obs_source_dec_showing(in->scene);
	bool stillUsed = false;
	for (Input *other : list)
		stillUsed = stillUsed || other->source.Get() == in->source.Get();
	if (!stillUsed)
		obs_source_remove(in->source); /* fonte compartilhada continua viva na outra entrada */
	else
		syncDevice(in->source); /* talvez só sobraram entradas desativadas */
	delete in;
	emit inputsChanged();
	emit busChanged();
}

void Engine::applyLayout(Input *in)
{
	if (!in || !in->scene)
		return;
	obs_scene_t *scene = obs_scene_from_source(in->scene);
	obs_sceneitem_t *main = mainItem(in);
	if (main) {
		applyItemPos(main, in->source, in->pos, baseW, baseH);
		in->fittedW = obs_source_get_width(in->source);
		in->fittedH = obs_source_get_height(in->source);
		obs_sceneitem_set_order_position(main, 0);
	}
	QSet<int64_t> keep;
	int order = 0;
	for (InputLayer &l : in->layers) {
		Input *li = input(l.inputId);
		if (!li)
			l.inputId = 0;
		obs_sceneitem_t *item = l.itemId ? obs_scene_find_sceneitem_by_id(scene, l.itemId) : nullptr;
		if (item && (!li || obs_sceneitem_get_source(item) != li->scene.Get())) {
			obs_sceneitem_remove(item);
			item = nullptr;
		}
		l.itemId = 0;
		if (!li || layerWouldLoop(in->id, li->id))
			continue;
		if (!item)
			item = obs_scene_add(scene, li->scene);
		if (!item)
			continue; /* o motor recusou (entrada dentro dela mesma) */
		l.itemId = obs_sceneitem_get_id(item);
		keep << l.itemId;
		obs_sceneitem_set_visible(item, l.on);
		applyItemPos(item, li->scene, l.pos, baseW, baseH);
		obs_sceneitem_set_order_position(item, ++order);
	}
	/* sobras (camada trocada ou tirada) */
	QList<obs_sceneitem_t *> stale;
	struct Ctx {
		obs_sceneitem_t *main;
		QSet<int64_t> *keep;
		QList<obs_sceneitem_t *> *stale;
	} ctx{main, &keep, &stale};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			auto *c = static_cast<Ctx *>(p);
			if (item != c->main && !c->keep->contains(obs_sceneitem_get_id(item)))
				c->stale->append(item);
			return true;
		},
		&ctx);
	for (obs_sceneitem_t *item : stale)
		obs_sceneitem_remove(item);
}

bool Engine::layerWouldLoop(int id, int candidate) const
{
	if (candidate == id)
		return true;
	QList<int> todo{candidate};
	QSet<int> seen;
	while (!todo.isEmpty()) {
		const int cur = todo.takeLast();
		if (seen.contains(cur))
			continue;
		seen << cur;
		const Input *c = input(cur);
		if (!c)
			continue;
		for (const InputLayer &l : c->layers) {
			if (l.inputId == id)
				return true;
			if (l.inputId)
				todo << l.inputId;
		}
	}
	return false;
}

QList<int> Engine::layerInputsOf(int id) const
{
	QList<int> out;
	QList<int> todo{id};
	QSet<int> seen{id};
	while (!todo.isEmpty()) {
		const Input *c = input(todo.takeLast());
		if (!c)
			continue;
		for (const InputLayer &l : c->layers) {
			if (!l.inputId || !l.on || seen.contains(l.inputId) || !input(l.inputId))
				continue;
			seen << l.inputId;
			out << l.inputId;
			todo << l.inputId;
		}
	}
	return out;
}

void Engine::setInputPosition(int id, const ItemPos &pos)
{
	Input *in = input(id);
	if (!in || in->pos == pos)
		return;
	in->pos = pos;
	if (obs_sceneitem_t *main = mainItem(in))
		applyItemPos(main, in->source, in->pos, baseW, baseH);
	emit inputLayoutChanged(id);
}

void Engine::setLayer(int id, int index, int layerInputId)
{
	Input *in = input(id);
	if (!in || index < 0 || index >= INPUT_LAYERS)
		return;
	if (layerInputId && (!input(layerInputId) || layerWouldLoop(id, layerInputId)))
		layerInputId = 0;
	if (in->layers[index].inputId == layerInputId)
		return;
	in->layers[index].inputId = layerInputId;
	applyLayout(in);
	emit inputLayoutChanged(id);
}

void Engine::setLayerOn(int id, int index, bool on)
{
	Input *in = input(id);
	if (!in || index < 0 || index >= INPUT_LAYERS || in->layers[index].on == on)
		return;
	in->layers[index].on = on;
	obs_scene_t *scene = obs_scene_from_source(in->scene);
	if (in->layers[index].itemId)
		if (obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, in->layers[index].itemId))
			obs_sceneitem_set_visible(item, on);
	emit inputLayoutChanged(id);
}

void Engine::setLayerPosition(int id, int index, const ItemPos &pos)
{
	Input *in = input(id);
	if (!in || index < 0 || index >= INPUT_LAYERS || in->layers[index].pos == pos)
		return;
	InputLayer &l = in->layers[index];
	l.pos = pos;
	Input *li = input(l.inputId);
	if (li && l.itemId)
		if (obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(obs_scene_from_source(in->scene), l.itemId))
			applyItemPos(item, li->scene, l.pos, baseW, baseH);
	emit inputLayoutChanged(id);
}

void Engine::checkSourceSizes()
{
	for (Input *in : list) {
		if (in->pos.isDefault())
			continue; /* tela cheia: o motor já ajusta sozinho */
		const uint32_t w = obs_source_get_width(in->source), h = obs_source_get_height(in->source);
		if (w == in->fittedW && h == in->fittedH)
			continue;
		if (obs_sceneitem_t *main = mainItem(in))
			applyItemPos(main, in->source, in->pos, baseW, baseH);
		in->fittedW = w;
		in->fittedH = h;
	}
}

bool Engine::deviceActive(obs_source_t *source)
{
	if (!source || strcmp(obs_source_get_unversioned_id(source), "dshow_input") != 0)
		return true;
	OBSDataAutoRelease st = obs_source_get_settings(source);
	return obs_data_get_bool(st, "active"); /* padrão do win-dshow: true */
}

void Engine::syncDevice(obs_source_t *source)
{
	if (!source || strcmp(obs_source_get_unversioned_id(source), "dshow_input") != 0)
		return; /* só câmera/placa abre aparelho; o resto só some da entrada */
	bool used = false, wanted = false;
	for (Input *o : list) {
		if (o->source.Get() != source)
			continue;
		used = true;
		wanted = wanted || o->enabled;
	}
	if (!used || deviceActive(source) == wanted)
		return;
	uint8_t stack[128];
	calldata_t cd;
	calldata_init_fixed(&cd, stack, sizeof(stack));
	calldata_set_bool(&cd, "active", wanted);
	proc_handler_call(obs_source_get_proc_handler(source), "activate", &cd);
	blog(LOG_INFO, "aparelho de '%s' %s", obs_source_get_name(source),
	     wanted ? "ligado" : "desligado (todas as entradas que o usam estão desativadas)");
}

void Engine::setInputEnabled(int id, bool on)
{
	Input *in = input(id);
	if (!in || in->enabled == on)
		return;
	in->enabled = on;
	if (obs_sceneitem_t *item = mainItem(in))
		obs_sceneitem_set_visible(item, on);
	blog(LOG_INFO, "entrada %d %s", id, on ? "ativada" : "desativada");
	syncDevice(in->source);
	emit inputChanged(id);
}

void Engine::renameInput(int id, const QString &name)
{
	if (Input *in = input(id)) {
		const QString old = in->name;
		in->name = name;
		obs_source_set_name(in->scene, name.toUtf8().constData());
		emit inputRenamed(id, old, name);
		emit inputChanged(id);
		emit busChanged();
	}
}

void Engine::moveInput(int id, int newIndex)
{
	const int idx = indexOf(id);
	if (idx < 0 || newIndex < 0 || newIndex >= list.size() || idx == newIndex)
		return;
	list.move(idx, newIndex);
	emit inputsChanged();
}

void Engine::setInputAudio(int id, bool on)
{
	if (Input *in = input(id)) {
		in->audioOn = on;
		obs_source_set_muted(in->source, !on);
		emit inputChanged(id);
	}
}

const QList<InputColor> &inputColors()
{
	/* tons firmes mas sóbrios; verde e vermelho ficam para Prévia e Programa */
	static const QList<InputColor> list = {
		{"", "Padrão", "#2b3549"},      {"azul", "Azul", "#2456a3"},       {"laranja", "Laranja", "#c0621b"},
		{"roxo", "Roxo", "#6a3d9e"},    {"ciano", "Ciano", "#17808c"},     {"amarelo", "Amarelo", "#a3861a"},
		{"rosa", "Rosa", "#a3366a"},    {"marrom", "Marrom", "#7a5132"},   {"cinza", "Cinza", "#4b5466"},
	};
	return list;
}

QString inputColorHex(const QString &key)
{
	for (const InputColor &c : inputColors())
		if (key == QLatin1String(c.key))
			return QString::fromLatin1(c.hex);
	return QString::fromLatin1(inputColors().first().hex);
}

static QString normKey(QString k)
{
	k = k.trimmed();
	if (k.startsWith('{') && k.endsWith('}'))
		k = k.mid(1, k.size() - 2);
	return k.toLower();
}

Input *Engine::inputByKey(const QString &key) const
{
	const QString k = normKey(key);
	if (k.isEmpty())
		return nullptr;
	for (Input *in : list)
		if (normKey(in->key) == k)
			return in;
	return nullptr;
}

bool Engine::setInputKey(int id, const QString &key)
{
	Input *in = input(id);
	const QString k = normKey(key);
	if (!in || k.isEmpty())
		return false;
	Input *other = inputByKey(k);
	if (other && other != in)
		return false;
	in->key = k;
	emit inputChanged(id);
	return true;
}

void Engine::setInputColor(int id, const QString &key)
{
	if (Input *in = input(id)) {
		in->color = key;
		emit inputChanged(id);
	}
}

void Engine::setInputCollapsed(int id, bool on)
{
	if (Input *in = input(id)) {
		in->collapsed = on;
		emit inputChanged(id);
	}
}

bool Engine::hasAudio(const Input *in)
{
	return in && (obs_source_get_output_flags(in->source) & OBS_SOURCE_AUDIO);
}

void Engine::applyVolume(Input *in)
{
	obs_source_set_volume(in->source, in->volume * master);
}

void Engine::setInputVolume(int id, float mul)
{
	if (Input *in = input(id)) {
		in->volume = std::clamp(mul, 0.0f, 4.0f);
		for (Input *o : list) /* mesma fonte (mesma câmera) em outra entrada: o volume é um só */
			if (o->source.Get() == in->source.Get())
				o->volume = in->volume;
		applyVolume(in);
		emit inputChanged(id);
	}
}

void Engine::setInputAudioAuto(int id, bool on)
{
	if (Input *in = input(id)) {
		in->audioAuto = on;
		emit inputChanged(id);
	}
}

void Engine::setInputMonitor(int id, bool on)
{
	if (Input *in = input(id)) {
		in->monitor = on;
		obs_source_set_monitoring_type(in->source, on ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
							      : OBS_MONITORING_TYPE_NONE);
		emit inputChanged(id);
	}
}

void Engine::setMasterVolume(float mul)
{
	master = std::clamp(mul, 0.0f, 4.0f);
	for (Input *in : list)
		applyVolume(in);
	emit audioChanged();
}

/* Entradas "sempre" (sem Auto) que não estão no ar tocam pela cena de áudio do canal 5;
 * as que estão no ar tocam pelo próprio programa/overlay (sem dobrar o som). */
void Engine::syncAudioBed()
{
	if (!audioBed)
		return;
	QSet<obs_source_t *> onAir;
	if (Input *p = input(pgm); p && !ftb)
		onAir << p->source.Get();
	for (int ch = 1; ch <= 4; ch++)
		if (Input *o = input(ov[ch]))
			onAir << o->source.Get();
	/* camadas das entradas no ar também tocam por elas (a cena leva o som junto) */
	QList<int> airIds;
	if (!ftb)
		airIds << pgm;
	for (int ch = 1; ch <= 4; ch++)
		airIds << ov[ch];
	for (int aid : airIds)
		if (aid)
			for (int lid : layerInputsOf(aid))
				if (Input *l = input(lid))
					onAir << l->source.Get();
	QSet<obs_source_t *> want;
	for (Input *in : list)
		if (in->audioOn && !in->audioAuto && hasAudio(in) && !onAir.contains(in->source.Get()))
			want << in->source.Get();

	obs_scene_t *bed = obs_scene_from_source(audioBed);
	QList<obs_sceneitem_t *> items;
	obs_scene_enum_items(
		bed,
		[](obs_scene_t *, obs_sceneitem_t *item, void *p) {
			static_cast<QList<obs_sceneitem_t *> *>(p)->append(item);
			return true;
		},
		&items);
	QSet<obs_source_t *> have;
	for (obs_sceneitem_t *item : items) {
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (want.contains(src))
			have << src;
		else
			obs_sceneitem_remove(item);
	}
	for (obs_source_t *src : want) {
		if (have.contains(src))
			continue;
		obs_sceneitem_t *item = obs_scene_add(bed, src);
		vec2 zero;
		vec2_zero(&zero);
		obs_sceneitem_set_scale(item, &zero); /* só o som: a imagem não aparece */
	}
}

bool Engine::resetVideo(uint32_t w, uint32_t h, uint32_t num, uint32_t den, QString &error)
{
	if (w == baseW && h == baseH && num == fpsNum && den == fpsDen)
		return true;
	obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi)) {
		error = "O vídeo não está ligado.";
		return false;
	}
	ovi.base_width = ovi.output_width = w;
	ovi.base_height = ovi.output_height = h;
	ovi.fps_num = num;
	ovi.fps_den = den;
	const int rv = obs_reset_video(&ovi);
	if (rv != OBS_VIDEO_SUCCESS) {
		error = rv == OBS_VIDEO_CURRENTLY_ACTIVE
				? "Pare a gravação, a transmissão e a gravação do replay antes de mudar o vídeo."
				: QString("O vídeo não aceitou essa configuração (código %1).").arg(rv);
		return false;
	}
	baseW = w;
	baseH = h;
	fpsNum = num;
	fpsDen = den;
	/* as entradas continuam ocupando a tela inteira no tamanho novo */
	for (Input *in : list)
		applyLayout(in);
	blog(LOG_INFO, "vídeo: %ux%u, %u/%u fps", w, h, num, den);
	emit videoChanged();
	return true;
}

void Engine::setInputLoop(int id, bool on)
{
	Input *in = input(id);
	if (!in)
		return;
	in->loop = on;
	if (strcmp(obs_source_get_unversioned_id(in->source), "ffmpeg_source") == 0) {
		OBSDataAutoRelease s = obs_data_create();
		obs_data_set_bool(s, "looping", on);
		obs_source_update(in->source, s);
	}
	emit inputChanged(id);
}

/* ---------- prévia, programa e transições ---------- */

void Engine::setPreview(int id)
{
	if (id && !input(id))
		return;
	pvw = id;
	emit busChanged();
}

obs_source_t *Engine::transitionFor(TransitionKind kind)
{
	switch (kind) {
	case TransitionKind::Cut:
		return cutTr;
	case TransitionKind::Stinger: {
		StudioTransition *st = nthTransition(1, true);
		return st ? st->source.Get() : fadeTr.Get();
	}
	default:
		return fadeTr;
	}
}

void Engine::setProgramScene(obs_source_t *scene, TransitionKind kind)
{
	setProgramSceneWith(scene, transitionFor(kind), kind == TransitionKind::Cut ? 0 : durationMs);
}

void Engine::setProgramSceneWith(obs_source_t *scene, obs_source_t *tr, int ms)
{
	if (!tr)
		tr = fadeTr;
	if (tr != activeTr) {
		obs_transition_swap_begin(tr, activeTr);
		obs_set_output_source(0, tr);
		obs_transition_swap_end(tr, activeTr);
		activeTr = tr;
	}
	if (tr == cutTr.Get() || ms <= 0)
		obs_transition_set(tr, scene);
	else
		obs_transition_start(tr, OBS_TRANSITION_MODE_AUTO, (uint32_t)ms, scene);
}

void Engine::takeToProgram(int id, obs_source_t *tr, int ms)
{
	Input *in = input(id);
	if (!in)
		return;
	ftb = false;
	pgm = id;
	setProgramSceneWith(in->scene, tr, ms);
	emit busChanged();
}

Input *Engine::inputByScene(obs_source_t *scene) const
{
	for (Input *in : list)
		if (in->scene.Get() == scene)
			return in;
	return nullptr;
}

Input *Engine::inputByName(const QString &name) const
{
	for (Input *in : list)
		if (in->name == name)
			return in;
	return nullptr;
}

void Engine::transition(TransitionKind kind)
{
	if (kind == TransitionKind::FadeToBlack) {
		ftb = !ftb;
		Input *p = input(pgm);
		setProgramScene(ftb ? blackScene.Get() : (p ? p->scene.Get() : blackScene.Get()), TransitionKind::Fade);
		emit busChanged();
		return;
	}
	Input *next = input(pvw);
	if (!next)
		return;
	ftb = false;
	const int old = pgm;
	pgm = pvw;
	pvw = old ? old : pvw; /* como no vMix: o que saiu do ar volta para a prévia */
	setProgramScene(next->scene, kind);
	emit busChanged();
}

/* ---------- transições do operador ---------- */

bool StudioTransition::isStinger() const
{
	return source && strstr(obs_source_get_unversioned_id(source), "stinger") != nullptr;
}

bool StudioTransition::isVideoStinger() const
{
	return source && strcmp(obs_source_get_unversioned_id(source), "obs_stinger_transition") == 0;
}

bool StudioTransition::fixedDuration() const
{
	return source && obs_transition_fixed(source);
}

QString Engine::transitionTypeName(const char *type)
{
	static const struct {
		const char *id, *label;
	} names[] = {{"obs_stinger_transition", "Stinger"}, {"rds_gt_stinger", "Stinger GT"}, {"fade_transition", "Fade"},
		     {"slide_transition", "Deslizar"},        {"swipe_transition", "Varrer"},
		     {"wipe_transition", "Luma wipe"},         {"fade_to_color_transition", "Fade para cor"},
		     {"cut_transition", "Corte"}};
	for (auto &n : names)
		if (type && strcmp(type, n.id) == 0)
			return QString::fromUtf8(n.label);
	const char *d = type ? obs_source_get_display_name(type) : nullptr;
	return QString::fromUtf8(d ? d : (type ? type : "?"));
}

StudioTransition *Engine::userTransition(int id) const
{
	for (StudioTransition *t : trList)
		if (t->id == id)
			return t;
	return nullptr;
}

StudioTransition *Engine::nthTransition(int n, bool stingerOnly) const
{
	int k = 0;
	for (StudioTransition *t : trList)
		if ((!stingerOnly || t->isStinger()) && ++k == n)
			return t;
	return nullptr;
}

StudioTransition *Engine::addTransition(const QString &type, const QString &name, obs_data_t *settings, int ms)
{
	const QByteArray tp = type.toUtf8();
	const int id = nextTrId++;
	/* o nome da fonte é o nome mostrado: o replay escolhe a transição por ele */
	const QByteArray nm = name.toUtf8();
	obs_source_t *src = obs_source_create_private(tp.constData(), nm.constData(), settings);
	if (!src) {
		blog(LOG_ERROR, "não foi possível criar a transição do tipo %s", tp.constData());
		return nullptr;
	}
	auto *t = new StudioTransition;
	t->id = id;
	t->name = name;
	t->source = src;
	t->durationMs = qBound(50, ms, 20000);
	trList.append(t);
	blog(LOG_INFO, "transição %d criada: %s (%s)", id, name.toUtf8().constData(), tp.constData());
	emit transitionsChanged();
	return t;
}

void Engine::removeTransition(int id)
{
	StudioTransition *t = userTransition(id);
	if (!t)
		return;
	if (activeTr == t->source.Get()) {
		/* está no programa: o fade assume, com a mesma imagem */
		obs_transition_swap_begin(fadeTr, activeTr);
		obs_set_output_source(0, fadeTr);
		obs_transition_swap_end(fadeTr, activeTr);
		activeTr = fadeTr;
	}
	trList.removeOne(t);
	delete t;
	emit transitionsChanged();
}

void Engine::renameTransition(int id, const QString &name)
{
	StudioTransition *t = userTransition(id);
	if (!t || name.trimmed().isEmpty() || name == t->name)
		return;
	t->name = name.trimmed();
	obs_source_set_name(t->source, t->name.toUtf8().constData());
	emit transitionsChanged();
}

void Engine::setUserTransitionMs(int id, int ms)
{
	if (StudioTransition *t = userTransition(id)) {
		t->durationMs = qBound(50, ms, 20000);
		emit transitionsChanged();
	}
}

void Engine::moveTransition(int id, int delta)
{
	StudioTransition *t = userTransition(id);
	if (!t)
		return;
	const int i = trList.indexOf(t);
	const int j = qBound(0, i + delta, (int)trList.size() - 1);
	if (i == j)
		return;
	trList.move(i, j);
	emit transitionsChanged();
}

void Engine::transitionWith(int id)
{
	StudioTransition *t = userTransition(id);
	if (!t) {
		transition(TransitionKind::Fade);
		return;
	}
	Input *next = input(pvw);
	if (!next)
		return;
	ftb = false;
	const int old = pgm;
	pgm = pvw;
	pvw = old ? old : pvw;
	setProgramSceneWith(next->scene, t->source, t->durationMs);
	emit busChanged();
}

void Engine::clearTransitions()
{
	if (activeTr && activeTr != cutTr.Get() && activeTr != fadeTr.Get() && fadeTr) {
		obs_transition_swap_begin(fadeTr, activeTr);
		obs_set_output_source(0, fadeTr);
		obs_transition_swap_end(fadeTr, activeTr);
		activeTr = fadeTr;
	}
	qDeleteAll(trList);
	trList.clear();
	nextTrId = 1;
}

void Engine::quickPlay(int id)
{
	Input *in = input(id);
	if (!in)
		return;
	ftb = false;
	pgm = id;
	setProgramScene(in->scene, TransitionKind::Fade);
	emit busChanged();
}

void Engine::cutTo(int id)
{
	Input *in = input(id);
	if (!in)
		return;
	ftb = false;
	pgm = id;
	setProgramScene(in->scene, TransitionKind::Cut);
	emit busChanged();
}

void Engine::tbarMove(float t)
{
	Input *next = input(pvw);
	if (!next)
		return;
	if (!manual) {
		if (activeTr != fadeTr.Get()) {
			obs_transition_swap_begin(fadeTr, activeTr);
			obs_set_output_source(0, fadeTr);
			obs_transition_swap_end(fadeTr, activeTr);
			activeTr = fadeTr;
		}
		obs_transition_start(fadeTr, OBS_TRANSITION_MODE_MANUAL, 0, next->scene);
		manual = true;
	}
	obs_transition_set_manual_time(fadeTr, std::clamp(t, 0.0f, 1.0f));
}

void Engine::tbarRelease(float t)
{
	if (!manual)
		return;
	manual = false;
	Input *next = input(pvw);
	Input *cur = input(pgm);
	if (t >= 0.5f && next) {
		obs_transition_set(fadeTr, next->scene);
		ftb = false;
		const int old = pgm;
		pgm = pvw;
		pvw = old ? old : pvw;
	} else {
		obs_transition_set(fadeTr, cur ? cur->scene.Get() : nullptr);
	}
	emit busChanged();
}

void Engine::setTransitionMs(int ms)
{
	const int v = qBound(50, ms, 10000);
	if (v == durationMs)
		return;
	durationMs = v;
	emit transitionMsChanged();
}

int Engine::overlay(int channel) const
{
	return channel >= 1 && channel <= 4 ? ov[channel] : 0;
}

void Engine::toggleOverlay(int channel, int id)
{
	if (channel < 1 || channel > 4)
		return;
	const unsigned gen = ++ovGen[channel];
	if (ov[channel] == id || !input(id)) {
		Input *was = input(ov[channel]);
		const int wait = was && beforeOverlayOff ? beforeOverlayOff(was->source) : 0;
		ov[channel] = 0;
		if (wait > 0) {
			/* continua na tela até a animação de saída acabar */
			QTimer::singleShot(wait, this, [this, channel, gen] {
				if (ovGen[channel] == gen)
					obs_set_output_source(channel, nullptr);
			});
		} else {
			obs_set_output_source(channel, nullptr);
		}
	} else {
		obs_set_output_source(channel, input(id)->scene);
		ov[channel] = id;
	}
	emit busChanged();
}

/* ---------- projeto ---------- */

void Engine::clearAll()
{
	for (int ch = 1; ch <= 4; ch++) {
		obs_set_output_source(ch, nullptr);
		ov[ch] = 0;
	}
	if (activeTr)
		obs_transition_set(activeTr, nullptr);
	pvw = pgm = 0;
	for (Input *in : list) {
		obs_source_dec_showing(in->scene);
		obs_source_remove(in->source);
		delete in;
	}
	list.clear();
}

static obs_data_t *posToData(const ItemPos &p)
{
	obs_data_t *d = obs_data_create();
	obs_data_set_double(d, "zoom", p.zoom);
	obs_data_set_double(d, "zoom_x", p.zoomX);
	obs_data_set_double(d, "zoom_y", p.zoomY);
	obs_data_set_double(d, "pan_x", p.panX);
	obs_data_set_double(d, "pan_y", p.panY);
	obs_data_set_double(d, "giro", p.rotation);
	obs_data_set_double(d, "corte_x1", p.cropX1);
	obs_data_set_double(d, "corte_y1", p.cropY1);
	obs_data_set_double(d, "corte_x2", p.cropX2);
	obs_data_set_double(d, "corte_y2", p.cropY2);
	return d;
}

static ItemPos posFromData(obs_data_t *d)
{
	ItemPos p;
	if (!d)
		return p;
	auto get = [d](const char *k, double def) { return obs_data_has_user_value(d, k) ? obs_data_get_double(d, k) : def; };
	p.zoom = get("zoom", 1.0);
	p.zoomX = get("zoom_x", 1.0);
	p.zoomY = get("zoom_y", 1.0);
	p.panX = get("pan_x", 0.0);
	p.panY = get("pan_y", 0.0);
	p.rotation = get("giro", 0.0);
	p.cropX1 = get("corte_x1", 0.0);
	p.cropY1 = get("corte_y1", 0.0);
	p.cropX2 = get("corte_x2", 1.0);
	p.cropY2 = get("corte_y2", 1.0);
	return p;
}

bool Engine::saveProject(const QString &path)
{
	if (!started)
		return false;
	OBSDataAutoRelease root = obs_data_create();
	obs_data_set_int(root, "versao", 1);
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (Input *in : list) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_int(o, "id", in->id);
		obs_data_set_string(o, "nome", in->name.toUtf8().constData());
		obs_data_set_bool(o, "loop", in->loop);
		obs_data_set_bool(o, "audio", in->audioOn);
		obs_data_set_bool(o, "desativada", !in->enabled);
		obs_data_set_double(o, "volume", in->volume);
		obs_data_set_bool(o, "audio_auto", in->audioAuto);
		obs_data_set_bool(o, "fone", in->monitor);
		obs_data_set_string(o, "cor", in->color.toUtf8().constData());
		obs_data_set_bool(o, "minimizada", in->collapsed);
		obs_data_set_string(o, "chave", in->key.toUtf8().constData());
		if (!in->pos.isDefault()) {
			OBSDataAutoRelease pd = posToData(in->pos);
			obs_data_set_obj(o, "posicao", pd);
		}
		OBSDataArrayAutoRelease layers = obs_data_array_create();
		for (int li = 0; li < INPUT_LAYERS; li++) {
			const InputLayer &l = in->layers[li];
			if (!l.inputId && l.on && l.pos.isDefault())
				continue;
			OBSDataAutoRelease ld = obs_data_create();
			obs_data_set_int(ld, "numero", li + 1);
			obs_data_set_int(ld, "entrada", l.inputId);
			obs_data_set_bool(ld, "ligada", l.on);
			OBSDataAutoRelease pd = posToData(l.pos);
			obs_data_set_obj(ld, "posicao", pd);
			obs_data_array_push_back(layers, ld);
		}
		if (obs_data_array_count(layers))
			obs_data_set_array(o, "camadas", layers);
		if (const int from = sharedFrom(in->id)) {
			obs_data_set_int(o, "mesma_fonte_de", from);
		} else {
			OBSDataAutoRelease src = obs_save_source(in->source);
			obs_data_set_obj(o, "fonte", src);
		}
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(root, "entradas", arr);
	obs_data_set_int(root, "previa", pvw);
	obs_data_set_int(root, "programa", pgm);
	obs_data_set_int(root, "transicao_ms", durationMs);
	OBSDataArrayAutoRelease trs = obs_data_array_create();
	for (StudioTransition *t : trList) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_string(o, "tipo", obs_source_get_id(t->source));
		obs_data_set_string(o, "nome", t->name.toUtf8().constData());
		obs_data_set_int(o, "ms", t->durationMs);
		OBSDataAutoRelease st = obs_source_get_settings(t->source);
		obs_data_set_obj(o, "opcoes", st);
		obs_data_array_push_back(trs, o);
	}
	obs_data_set_array(root, "transicoes", trs);
	obs_data_set_double(root, "volume_geral", master);
	const QByteArray p = QFile::encodeName(path);
	const bool ok = obs_data_save_json_safe(root, p.constData(), "tmp", "bak");
	if (ok)
		projPath = path;
	blog(ok ? LOG_INFO : LOG_ERROR, "projeto %s: %s", ok ? "salvo" : "NÃO salvo", p.constData());
	return ok;
}

bool Engine::loadProject(const QString &path)
{
	const QByteArray p = QFile::encodeName(path);
	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(p.constData(), "bak");
	if (!root) {
		blog(LOG_ERROR, "projeto não abriu: %s", p.constData());
		return false;
	}
	clearAll();
	nextId = 1;
	OBSDataArrayAutoRelease arr = obs_data_get_array(root, "entradas");
	const size_t n = obs_data_array_count(arr);
	for (size_t i = 0; i < n; i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		obs_source_t *src = nullptr;
		if (const int from = (int)obs_data_get_int(o, "mesma_fonte_de")) {
			if (Input *f = input(from))
				src = obs_source_get_ref(f->source);
		} else {
			OBSDataAutoRelease srcData = obs_data_get_obj(o, "fonte");
			obs_data_erase(srcData, "uuid"); /* a fonte antiga pode ainda estar saindo da memória */
			src = obs_load_private_source(srcData);
		}
		if (!src) {
			blog(LOG_WARNING, "entrada %zu do projeto não pôde ser recriada (tipo indisponível?)", i);
			continue;
		}
		const int id = (int)obs_data_get_int(o, "id");
		Input *in = wrapSource(id, QString::fromUtf8(obs_data_get_string(o, "nome")), src, baseW, baseH);
		in->loop = obs_data_get_bool(o, "loop");
		in->audioOn = !obs_data_has_user_value(o, "audio") || obs_data_get_bool(o, "audio");
		if (obs_data_has_user_value(o, "volume"))
			in->volume = (float)obs_data_get_double(o, "volume");
		if (obs_data_has_user_value(o, "audio_auto"))
			in->audioAuto = obs_data_get_bool(o, "audio_auto");
		in->monitor = obs_data_get_bool(o, "fone");
		in->color = QString::fromUtf8(obs_data_get_string(o, "cor"));
		in->collapsed = obs_data_get_bool(o, "minimizada");
		if (const char *k = obs_data_get_string(o, "chave"); k && *k)
			in->key = QString::fromUtf8(k);
		if (in->monitor)
			obs_source_set_monitoring_type(src, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
		applyVolume(in);
		if (obs_data_has_user_value(o, "desativada"))
			in->enabled = !obs_data_get_bool(o, "desativada");
		else
			in->enabled = deviceActive(src); /* projeto antigo: câmera desativada pelo botão do driver */
		if (obs_sceneitem_t *item = mainItem(in))
			obs_sceneitem_set_visible(item, in->enabled);
		{
			OBSDataAutoRelease pd = obs_data_get_obj(o, "posicao");
			in->pos = posFromData(pd);
			OBSDataArrayAutoRelease layers = obs_data_get_array(o, "camadas");
			for (size_t li = 0; li < obs_data_array_count(layers); li++) {
				OBSDataAutoRelease ld = obs_data_array_item(layers, li);
				const int num = (int)obs_data_get_int(ld, "numero");
				if (num < 1 || num > INPUT_LAYERS)
					continue;
				InputLayer &l = in->layers[num - 1];
				l.inputId = (int)obs_data_get_int(ld, "entrada");
				l.on = !obs_data_has_user_value(ld, "ligada") || obs_data_get_bool(ld, "ligada");
				OBSDataAutoRelease lp = obs_data_get_obj(ld, "posicao");
				l.pos = posFromData(lp);
			}
		}
		list.append(in);
		nextId = std::max(nextId, id + 1);
	}
	/* camadas só depois de todas as entradas existirem (uma pode apontar para outra mais abaixo) */
	for (Input *in : list)
		applyLayout(in);
	if (obs_data_has_user_value(root, "volume_geral"))
		setMasterVolume((float)obs_data_get_double(root, "volume_geral"));
	if (obs_data_has_user_value(root, "transicao_ms"))
		setTransitionMs((int)obs_data_get_int(root, "transicao_ms"));
	clearTransitions();
	{
		OBSDataArrayAutoRelease trs = obs_data_get_array(root, "transicoes");
		const size_t nt = obs_data_array_count(trs);
		for (size_t i = 0; i < nt; i++) {
			OBSDataAutoRelease o = obs_data_array_item(trs, i);
			OBSDataAutoRelease st = obs_data_get_obj(o, "opcoes");
			addTransition(QString::fromUtf8(obs_data_get_string(o, "tipo")),
				      QString::fromUtf8(obs_data_get_string(o, "nome")), st,
				      (int)obs_data_get_int(o, "ms"));
		}
		emit transitionsChanged();
	}
	projPath = path;
	mergeSameDevice();
	for (Input *in : list)
		syncDevice(in->source);
	emit inputsChanged();

	const int savedPgm = (int)obs_data_get_int(root, "programa");
	if (input(savedPgm))
		cutTo(savedPgm);
	setPreview(input((int)obs_data_get_int(root, "previa")) ? (int)obs_data_get_int(root, "previa")
								 : (list.isEmpty() ? 0 : list.first()->id));
	blog(LOG_INFO, "projeto aberto: %s (%d entradas)", p.constData(), (int)list.size());
	return true;
}
