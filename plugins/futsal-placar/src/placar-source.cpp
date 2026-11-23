/* Fonte "Placar de Futsal": desenha o placar com QPainter numa QImage e manda para a GPU
 * só quando algo muda (pontuação, nomes, cores ou o número mostrado no relógio). */
#include "state.hpp"

#include <obs-module.h>
#include <graphics/graphics.h>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QString>

#include <algorithm>
#include <mutex>
#include <string>

enum Layout { L_FULL = 0, L_COMPACT = 1, L_CLOCK = 2 };

struct PlacarSource {
	obs_source_t *source = nullptr;
	int layout = L_FULL;
	bool showFouls = true;
	bool showCompetition = true;
	std::string fontFamily = "Arial";
	uint32_t panel = 0xFF151515;

	std::mutex m;
	QImage img;          /* pronto para subir para a GPU */
	bool imgDirty = false;
	gs_texture_t *tex = nullptr;
	uint32_t w = 1000, h = 90;
	std::string lastKey; /* o que foi desenhado por último */
	bool settingsDirty = true;
};

static QColor argb(uint32_t c)
{
	return QColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
}

static void drawFit(QPainter &p, const QRectF &r, const QString &text, QFont f, int align, const QColor &c)
{
	double size = r.height() * 0.62;
	f.setPixelSize(std::max(6, (int)size));
	QFontMetricsF fm(f);
	while (size > 6 && fm.horizontalAdvance(text) > r.width() - 8) {
		size -= 1;
		f.setPixelSize((int)size);
		fm = QFontMetricsF(f);
	}
	p.setFont(f);
	p.setPen(c);
	p.drawText(r.adjusted(4, 0, -4, 0), align | Qt::AlignVCenter, text);
}

static void drawFouls(QPainter &p, const QRectF &r, const Team &t, bool mirrored, QFont f)
{
	const QColor txt(220, 220, 220);
	const QColor on = t.fouls >= 5 ? QColor(235, 64, 52) : QColor(255, 196, 0);
	const QColor off(80, 80, 80);
	const double d = r.height() * 0.42;
	const double gap = d * 0.55;
	f.setPixelSize(std::max(6, (int)(r.height() * 0.55)));
	f.setBold(true);
	p.setFont(f);
	QFontMetricsF fm(f);
	/* placar compacto: só as bolinhas */
	const QString label = r.width() < 200 ? QString() : QStringLiteral("FALTAS");
	const double lw = fm.horizontalAdvance(label);
	const double dotsW = 5 * d + 4 * gap;
	const QString extra = t.fouls > 5 ? QStringLiteral("+%1").arg(t.fouls - 5) : QString();
	const double ew = extra.isEmpty() ? 0 : fm.horizontalAdvance(extra) + gap;
	const double total = (label.isEmpty() ? 0 : lw + gap * 1.5) + dotsW + ew;
	const double margin = r.width() < 200 ? 6 : 10;
	double x = mirrored ? r.right() - margin - total : r.left() + margin;
	const double cy = r.center().y();

	if (!label.isEmpty()) {
		p.setPen(txt);
		p.drawText(QRectF(x, r.top(), lw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, label);
		x += lw + gap * 1.5;
	}
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 5; i++) {
		p.setBrush(i < t.fouls ? on : off);
		p.drawEllipse(QRectF(x, cy - d / 2, d, d));
		x += d + gap;
	}
	if (!extra.isEmpty()) {
		p.setPen(on);
		p.drawText(QRectF(x, r.top(), ew + 4, r.height()), Qt::AlignVCenter | Qt::AlignLeft, extra);
	}

	/* pedido de tempo usado: quadradinho */
	const double s = r.height() * 0.5;
	const double tx = mirrored ? r.left() + margin : r.right() - margin - s;
	p.setPen(QPen(QColor(200, 200, 200), 1.5));
	p.setBrush(t.timeouts > 0 ? QColor(255, 255, 255) : Qt::transparent);
	p.drawRect(QRectF(tx, cy - s / 2, s, s));
}

static QImage renderPlacar(PlacarSource *s, const Snapshot &st, const QString &clock)
{
	QFont f(QString::fromStdString(s->fontFamily));
	f.setBold(true);
	f.setStyleStrategy(QFont::PreferAntialias);

	const QColor panel = argb(s->panel);
	const QColor dark = panel.darker(130);
	const QColor white(255, 255, 255);
	const bool penalties = st.period == P_PENALTIES;
	const bool timeout = st.timeoutTeam >= 0;

	if (s->layout == L_CLOCK) {
		QImage img(260, 70, QImage::Format_RGBA8888_Premultiplied);
		img.fill(Qt::transparent);
		QPainter p(&img);
		p.setRenderHint(QPainter::Antialiasing);
		p.setRenderHint(QPainter::TextAntialiasing);
		const QColor bg = timeout ? argb(st.team[st.timeoutTeam].bg) : panel;
		p.fillRect(QRectF(0, 0, 260, 70), bg);
		const QString per = timeout ? QStringLiteral("TEMPO") : QString::fromStdString(Placar::periodName(st.period, true));
		drawFit(p, QRectF(0, 0, 90, 70), per, f, Qt::AlignCenter, timeout ? argb(st.team[st.timeoutTeam].fg) : QColor(255, 196, 0));
		drawFit(p, QRectF(90, 0, 170, 70), clock, f, Qt::AlignCenter, timeout ? argb(st.team[st.timeoutTeam].fg) : white);
		return img;
	}

	const bool compact = s->layout == L_COMPACT;
	const double teamW = compact ? 120 : 340;
	const double scoreW = compact ? 62 : 76;
	const double clockW = compact ? 160 : 190;
	const double rowH = compact ? 54 : 66;
	const double subH = s->showFouls || penalties ? (compact ? 24 : 28) : 0;
	const bool compRow = s->showCompetition && !st.competition.empty() && !compact;
	const double topH = compRow ? 26 : 0;
	const int W = (int)(teamW * 2 + scoreW * 2 + clockW);
	const int H = (int)(topH + rowH + subH);

	QImage img(W, H, QImage::Format_RGBA8888_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.setRenderHint(QPainter::Antialiasing);
	p.setRenderHint(QPainter::TextAntialiasing);

	double y = 0;
	if (compRow) {
		p.fillRect(QRectF(0, 0, W, topH), dark);
		QFont cf = f;
		drawFit(p, QRectF(0, 0, W, topH), QString::fromStdString(st.competition).toUpper(), cf, Qt::AlignCenter,
			QColor(230, 230, 230));
		y = topH;
	}

	/* linha principal: [casa][gols][gols][visitante][relógio] */
	double x = 0;
	for (int i = 0; i < 2; i++) {
		const Team &t = st.team[i];
		const QRectF teamR(i == 0 ? 0 : teamW + scoreW * 2, y, teamW, rowH);
		p.fillRect(teamR, argb(t.bg));
		const QString name = QString::fromStdString(compact ? t.sigla : t.name).toUpper();
		drawFit(p, teamR.adjusted(8, rowH * 0.12, -8, -rowH * 0.12), name, f, Qt::AlignCenter, argb(t.fg));

		const QRectF scoreR(teamW + scoreW * i, y, scoreW, rowH);
		p.fillRect(scoreR, i == 0 ? panel : panel.lighter(115));
		drawFit(p, scoreR.adjusted(0, rowH * 0.06, 0, -rowH * 0.06), QString::number(t.score), f, Qt::AlignCenter, white);
	}
	x = teamW * 2 + scoreW * 2;

	const QRectF clockR(x, y, clockW, rowH);
	if (timeout) {
		const Team &t = st.team[st.timeoutTeam];
		p.fillRect(clockR, argb(t.bg));
		drawFit(p, QRectF(x, y, clockW, rowH * 0.42), QStringLiteral("TEMPO ") + QString::fromStdString(t.sigla).toUpper(), f,
			Qt::AlignCenter, argb(t.fg));
		drawFit(p, QRectF(x, y + rowH * 0.38, clockW, rowH * 0.62), clock, f, Qt::AlignCenter, argb(t.fg));
	} else {
		p.fillRect(clockR, dark);
		const QString per = QString::fromStdString(Placar::periodName(st.period, true));
		if (penalties) {
			drawFit(p, clockR.adjusted(0, rowH * 0.15, 0, -rowH * 0.15), QString::fromStdString(Placar::periodName(st.period, false)).toUpper(), f,
				Qt::AlignCenter, QColor(255, 196, 0));
		} else {
			drawFit(p, QRectF(x, y, clockW * 0.36, rowH), per, f, Qt::AlignCenter, QColor(255, 196, 0));
			drawFit(p, QRectF(x + clockW * 0.32, y + rowH * 0.1, clockW * 0.68, rowH * 0.8), clock, f, Qt::AlignCenter,
				st.running ? white : QColor(210, 210, 210));
		}
	}

	/* linha de baixo: faltas e pedidos de tempo (ou pênaltis) */
	if (subH > 0) {
		y += rowH;
		p.fillRect(QRectF(0, y, W, subH), QColor(25, 25, 25, 235));
		if (penalties) {
			for (int i = 0; i < 2; i++) {
				const QRectF r(teamW + scoreW * i, y, scoreW, subH);
				drawFit(p, r, QStringLiteral("(%1)").arg(st.team[i].penalties), f, Qt::AlignCenter, QColor(255, 196, 0));
			}
		} else if (s->showFouls) {
			drawFouls(p, QRectF(0, y, teamW, subH), st.team[0], false, f);
			drawFouls(p, QRectF(teamW + scoreW * 2, y, teamW, subH), st.team[1], true, f);
		}
	}
	return img;
}

static QString clockText(const Snapshot &st)
{
	if (st.timeoutTeam >= 0)
		return QString::fromStdString(Placar::formatClock(st.timeoutLeftMs, true));
	if (st.period == P_PENALTIES)
		return QString();
	const int64_t shown = st.countDown ? st.lengthMs - st.elapsedMs : st.elapsedMs;
	return QString::fromStdString(Placar::formatClock(shown, st.countDown));
}

/* ---------- callbacks da fonte ---------- */

static const char *placar_name(void *)
{
	return obs_module_text("Placar.Fonte");
}

static void placar_update(void *data, obs_data_t *settings)
{
	auto *s = static_cast<PlacarSource *>(data);
	std::lock_guard<std::mutex> l(s->m);
	s->layout = (int)obs_data_get_int(settings, "layout");
	s->showFouls = obs_data_get_bool(settings, "show_fouls");
	s->showCompetition = obs_data_get_bool(settings, "show_competition");
	obs_data_t *font = obs_data_get_obj(settings, "font");
	const char *face = font ? obs_data_get_string(font, "face") : "";
	s->fontFamily = face && *face ? face : "Arial";
	obs_data_release(font);
	s->panel = (uint32_t)obs_data_get_int(settings, "panel_color") | 0xFF000000;
	s->settingsDirty = true;
}

static void *placar_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new PlacarSource();
	s->source = source;
	placar_update(s, settings);
	return s;
}

static void placar_destroy(void *data)
{
	auto *s = static_cast<PlacarSource *>(data);
	if (s->tex) {
		obs_enter_graphics();
		gs_texture_destroy(s->tex);
		obs_leave_graphics();
	}
	delete s;
}

static void placar_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "layout", L_FULL);
	obs_data_set_default_bool(settings, "show_fouls", true);
	obs_data_set_default_bool(settings, "show_competition", true);
	obs_data_set_default_int(settings, "panel_color", 0xFF151515);
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", "Arial");
	obs_data_set_int(font, "size", 48);
	obs_data_set_default_obj(settings, "font", font);
	obs_data_release(font);
}

static obs_properties_t *placar_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *l = obs_properties_add_list(props, "layout", obs_module_text("Placar.Layout"), OBS_COMBO_TYPE_LIST,
						    OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(l, obs_module_text("Placar.Layout.Completo"), L_FULL);
	obs_property_list_add_int(l, obs_module_text("Placar.Layout.Compacto"), L_COMPACT);
	obs_property_list_add_int(l, obs_module_text("Placar.Layout.Relogio"), L_CLOCK);
	obs_properties_add_bool(props, "show_fouls", obs_module_text("Placar.MostrarFaltas"));
	obs_properties_add_bool(props, "show_competition", obs_module_text("Placar.MostrarCompeticao"));
	obs_properties_add_font(props, "font", obs_module_text("Placar.Fonte.Letra"));
	obs_properties_add_color(props, "panel_color", obs_module_text("Placar.CorPainel"));
	obs_properties_add_text(props, "info", obs_module_text("Placar.Info"), OBS_TEXT_INFO);
	return props;
}

static void placar_tick(void *data, float)
{
	auto *s = static_cast<PlacarSource *>(data);
	Placar::get().tick();
	const Snapshot st = Placar::get().snapshot();
	const QString clock = clockText(st);
	const std::string key = std::to_string(st.version) + "|" + clock.toStdString();

	std::lock_guard<std::mutex> l(s->m);
	if (!s->settingsDirty && key == s->lastKey)
		return;
	s->lastKey = key;
	s->settingsDirty = false;
	s->img = renderPlacar(s, st, clock);
	s->w = (uint32_t)s->img.width();
	s->h = (uint32_t)s->img.height();
	s->imgDirty = true;
}

static void placar_render(void *data, gs_effect_t *)
{
	auto *s = static_cast<PlacarSource *>(data);
	{
		std::lock_guard<std::mutex> l(s->m);
		if (s->imgDirty && !s->img.isNull()) {
			if (s->tex && (gs_texture_get_width(s->tex) != (uint32_t)s->img.width() ||
				       gs_texture_get_height(s->tex) != (uint32_t)s->img.height())) {
				gs_texture_destroy(s->tex);
				s->tex = nullptr;
			}
			if (!s->tex) {
				const uint8_t *bits = s->img.constBits();
				s->tex = gs_texture_create((uint32_t)s->img.width(), (uint32_t)s->img.height(), GS_RGBA, 1, &bits,
							   GS_DYNAMIC);
			} else {
				gs_texture_set_image(s->tex, s->img.constBits(), (uint32_t)s->img.bytesPerLine(), false);
			}
			s->imgDirty = false;
		}
	}
	if (!s->tex)
		return;

	/* a imagem já vem com alfa pré-multiplicado */
	gs_effect_t *eff = obs_get_base_effect(OBS_EFFECT_PREMULTIPLIED_ALPHA);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_eparam_t *img = gs_effect_get_param_by_name(eff, "image");
	gs_effect_set_texture(img, s->tex);
	while (gs_effect_loop(eff, "Draw"))
		gs_draw_sprite(s->tex, 0, 0, 0);
	gs_blend_state_pop();
}

static uint32_t placar_width(void *data)
{
	auto *s = static_cast<PlacarSource *>(data);
	std::lock_guard<std::mutex> l(s->m);
	return s->w;
}

static uint32_t placar_height(void *data)
{
	auto *s = static_cast<PlacarSource *>(data);
	std::lock_guard<std::mutex> l(s->m);
	return s->h;
}

void registerPlacarSource()
{
	struct obs_source_info info = {};
	info.id = "futsal_placar_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = placar_name;
	info.create = placar_create;
	info.destroy = placar_destroy;
	info.update = placar_update;
	info.get_defaults = placar_defaults;
	info.get_properties = placar_properties;
	info.video_tick = placar_tick;
	info.video_render = placar_render;
	info.get_width = placar_width;
	info.get_height = placar_height;
	info.icon_type = OBS_ICON_TYPE_TEXT;
	obs_register_source(&info);
}
