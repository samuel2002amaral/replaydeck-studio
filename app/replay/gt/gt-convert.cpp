/* Converte um .gtzip numa cena comum do OBS: um grupo por camada, formas e
 * imagens como fontes de imagem (já recortadas e com borda suave) e textos
 * como fontes de texto editáveis. As animações não são convertidas. */
#include "gt-render.hpp"
#include "i18n.hpp"
#include "../common.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <cmath>
#include <vector>

static QString uniqueName(const QString &base)
{
	QString n = base;
	for (int i = 2; true; i++) {
		obs_source_t *ex = obs_get_source_by_name(n.toUtf8().constData());
		if (!ex)
			return n;
		obs_source_release(ex);
		n = QString("%1 %2").arg(base).arg(i);
	}
}

static uint32_t abgr(const QColor &c)
{
	return ((uint32_t)c.alpha() << 24) | ((uint32_t)c.blue() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.red();
}

static obs_source_t *makeText(const GtElement &e, const QString &name)
{
#ifdef _WIN32
	const char *id = "text_gdiplus_v3";
#else
	const char *id = "text_ft2_source_v2";
#endif
	QString text = e.text;
	text.replace("\r\n", "\n");
	text.replace('\r', '\n');
	while (text.endsWith('\n'))
		text.chop(1);

	obs_data_t *st = obs_data_create();
	obs_data_set_string(st, "text", text.toUtf8().constData());
	obs_data_t *font = obs_data_create();
	obs_data_set_string(font, "face", e.fontFamily.toUtf8().constData());
	obs_data_set_int(font, "size", (int)std::lround(e.fontSize));
	const QString w = e.fontWeight.toLower();
	const bool bold = w.contains("bold") || w.contains("black") || w.contains("heavy");
	obs_data_set_int(font, "flags", bold ? 1 : 0);
	obs_data_set_string(font, "style", bold ? "Bold" : "Regular");
	obs_data_set_obj(st, "font", font);
	obs_data_release(font);
	const QColor c = e.fill.kind == GtBrush::Solid ? e.fill.color : QColor(Qt::white);
#ifdef _WIN32
	obs_data_set_int(st, "color", abgr(c));
	obs_data_set_int(st, "opacity", (int)std::lround(c.alphaF() * 100));
	obs_data_set_string(st, "align", e.align == "Center" ? "center" : e.align == "Right" ? "right" : "left");
	obs_data_set_string(st, "valign", e.valign == "Center" ? "center" : e.valign == "Bottom" ? "bottom" : "top");
	obs_data_set_bool(st, "extents", true);
	obs_data_set_int(st, "extents_cx", (int)std::lround(e.rect.width()));
	obs_data_set_int(st, "extents_cy", (int)std::lround(e.rect.height()));
	obs_data_set_bool(st, "extents_wrap", false);
	if (e.strokeThickness > 0 && e.stroke.kind == GtBrush::Solid) {
		obs_data_set_bool(st, "outline", true);
		obs_data_set_int(st, "outline_size", (int)std::lround(e.strokeThickness));
		obs_data_set_int(st, "outline_color", abgr(e.stroke.color));
	}
#else
	obs_data_set_int(st, "color1", abgr(c));
	obs_data_set_int(st, "color2", abgr(c));
#endif
	obs_source_t *src = obs_source_create(id, uniqueName(name).toUtf8().constData(), st, nullptr);
	obs_data_release(st);
	return src;
}

bool gtConvertToScene(const QString &file, QString &message)
{
	GtTitle t;
	QString err;
	if (!t.load(file, err)) {
		message = err;
		return false;
	}
	const QFileInfo fi(file);
	const QString dir = fi.absolutePath() + "/" + t.name + "_obs";
	QDir().mkpath(dir);

	const QString sceneName = uniqueName(QString::fromUtf8("GT - ") + t.name);
	obs_scene_t *scene = obs_scene_create(sceneName.toUtf8().constData());
	int images = 0, texts = 0;

	for (const GtLayer &L : t.layers) {
		std::vector<obs_sceneitem_t *> layerItems;
		for (const GtElement &e : L.elements) {
			/* posição no título e o pedaço que a camada deixa aparecer */
			const QRectF abs(L.rect.x() + e.rect.x(), L.rect.y() + e.rect.y(), e.rect.width(), e.rect.height());
			const QRectF vis = abs.intersected(L.rect);
			if (vis.isEmpty())
				continue;

			obs_source_t *src = nullptr;
			if (e.kind == GtKind::Text) {
				src = makeText(e, e.name.isEmpty() ? tl("Texto") : e.name);
				texts++;
			} else if (e.kind != GtKind::Other) {
				const QImage img = gtRenderElement(t, e);
				QString base = e.name.isEmpty() ? tl("elemento") : e.name;
				base.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
				const QString png = dir + "/" + base + ".png";
				img.save(png);
				obs_data_t *st = obs_data_create();
				obs_data_set_string(st, "file", png.toUtf8().constData());
				src = obs_source_create("image_source", uniqueName(e.name.isEmpty() ? tl("Imagem") : e.name).toUtf8().constData(),
							st, nullptr);
				obs_data_release(st);
				images++;
			}
			if (!src)
				continue;
			obs_sceneitem_t *item = obs_scene_add(scene, src);
			struct vec2 pos;
			vec2_set(&pos, (float)abs.x(), (float)abs.y());
			obs_sceneitem_set_pos(item, &pos);
			/* recorte da camada (em pixels da fonte, que tem o tamanho do elemento) */
			struct obs_sceneitem_crop crop = {};
			crop.left = (int)std::lround(vis.left() - abs.left());
			crop.top = (int)std::lround(vis.top() - abs.top());
			crop.right = (int)std::lround(abs.right() - vis.right());
			crop.bottom = (int)std::lround(abs.bottom() - vis.bottom());
			if (crop.left || crop.top || crop.right || crop.bottom) {
				obs_sceneitem_set_crop(item, &crop);
				vec2_set(&pos, (float)vis.x(), (float)vis.y());
				obs_sceneitem_set_pos(item, &pos);
			}
			obs_sceneitem_set_visible(item, e.visible);
			layerItems.push_back(item);
			obs_source_release(src);
		}
		/* agrupa depois de posicionar: o grupo mantém o lugar de cada item */
		if (!layerItems.empty()) {
			obs_sceneitem_t *group = obs_scene_insert_group(scene, uniqueName(t.name + " / " + L.name).toUtf8().constData(),
								       layerItems.data(), layerItems.size());
			if (group)
				obs_sceneitem_set_visible(group, L.visible);
		}
	}
	obs_scene_release(scene);
	message = tl("Cena \"%1\" criada: %2 imagens e %3 textos.\nAs imagens foram salvas em:\n%4\n\n"
				    "As animações do GT não são convertidas — para elas, use a fonte \"ReplayDeck GT\".")
			  .arg(sceneName)
			  .arg(images)
			  .arg(texts)
			  .arg(dir);
	return true;
}
