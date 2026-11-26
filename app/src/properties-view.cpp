#include "properties-view.hpp"
#include "props-cache.hpp"

#include <util/base.h>
#include <util/platform.h>
#include <cstring>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QApplication>
#include <QPointer>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QListWidget>
#include <QMimeData>
#include <QUrl>
#include <functional>

#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#endif

PropertiesView::PropertiesView(obs_source_t *source, const char *sourceType, QWidget *parent)
	: QWidget(parent),
	  type(sourceType)
{
	if (source) {
		/* cópia: nada muda na fonte até confirmar */
		weak = obs_source_get_weak_source(source);
		OBSDataAutoRelease cur = obs_source_get_settings(source);
		data = obs_data_get_defaults(cur);
		obs_data_apply(data, cur);
	} else {
		data = obs_get_source_defaults(sourceType);
		if (!data)
			data = obs_data_create();
#ifdef __APPLE__
		/* Mac: a captura de tela/janela (ScreenCaptureKit) lê a própria fonte ao montar as opções;
		 * sem uma fonte o programa fecha. Usa uma fonte temporária, só para as opções. */
		const QByteArray t(sourceType);
		if (t.startsWith("screen_capture") || t.startsWith("display_capture") || t.startsWith("window_capture") ||
		    t.startsWith("sck_audio_capture"))
			tempSrc = obs_source_create_private(sourceType, "opções (temporária)", data);
#endif
	}
	setupUi();
	reload();
}

/* opções já preparadas antes (cache): aparece tudo na hora */
PropertiesView::PropertiesView(const char *sourceType, obs_properties_t *ready, obs_data_t *settings, QWidget *parent)
	: QWidget(parent),
	  type(sourceType)
{
	data = settings;
	props = ready;
	setupUi();
	refresh();
}

PropertiesView *PropertiesView::create(const char *sourceType, QWidget *parent)
{
	obs_properties_t *p = nullptr;
	obs_data_t *d = nullptr;
	if (PropsCache::get().take(sourceType, &p, &d)) {
		blog(LOG_INFO, "[opções] %s: prontas do cache (na hora)", sourceType);
		return new PropertiesView(sourceType, p, d, parent);
	}
	blog(LOG_INFO, "[opções] %s: cache ainda não pronto, buscando", sourceType);
	return new PropertiesView(nullptr, sourceType, parent);
}

void PropertiesView::setupUi()
{
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	lay->addWidget(scroll);
	content = new QWidget;
	scroll->setWidget(content);
}

PropertiesView::~PropertiesView()
{
	obs_properties_destroy(props);
	obs_data_release(data);
}

static int reloads = 0;
int PropertiesView::reloadCount()
{
	return reloads;
}

/* Busca as opções em segundo plano: na câmera (DirectShow) isso varre todos os dispositivos e
 * resoluções e leva segundos; a janela abre na hora e os campos aparecem quando a busca termina. */
void PropertiesView::reload()
{
	if (loading)
		return;
	loading = true;
	obs_properties_destroy(props);
	props = nullptr;
	refresh(); /* mostra "Procurando…" */

	OBSSource src = OBSSourceAutoRelease(obs_weak_source_get_source(weak)).Get();
	if (!src && tempSrc)
		src = tempSrc.Get();
	OBSData settings = data;
	const QByteArray typeId = type;
	QPointer<PropertiesView> self(this);
	auto job = [self, src, settings, typeId]() {
#ifdef _WIN32
		const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED); /* a câmera usa COM */
#endif
		const uint64_t t0 = os_gettime_ns();
		obs_properties_t *p = src ? obs_source_properties(src) : obs_get_source_properties(typeId.constData());
		if (p && !src)
			obs_properties_apply_settings(p, settings); /* listas que dependem de outras opções já vêm certas */
		blog(LOG_INFO, "[opções] %s (%s): %llu ms", typeId.constData(), src ? "entrada existente" : "nova",
		     (unsigned long long)((os_gettime_ns() - t0) / 1000000));
#ifdef _WIN32
		if (SUCCEEDED(com))
			CoUninitialize();
#endif
		QMetaObject::invokeMethod(
			qApp,
			[self, p]() {
				if (!self) {
					obs_properties_destroy(p);
					return;
				}
				self->props = p;
				self->loading = false;
				reloads++;
				self->refresh();
				emit self->loaded();
			},
			Qt::QueuedConnection);
	};
#ifdef __APPLE__
	/* Mac: as capturas de tela/janela montam as opções com o AppKit (lista de telas e apps),
	 * que só pode ser usado na thread principal; em outra thread o programa fecha sozinho.
	 * A janela abre primeiro ("Procurando…") e as opções vêm logo em seguida. */
	QTimer::singleShot(0, qApp, job);
#else
	std::thread(job).detach();
#endif
}

void PropertiesView::refresh()
{
	rebuilding = true;

	delete content->layout();
	for (QObject *c : content->children())
		if (c->isWidgetType())
			delete c;
	auto *form = new QFormLayout(content);
	form->setLabelAlignment(Qt::AlignLeft);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::DontWrapRows);
	form->setContentsMargins(8, 8, 8, 8);
	form->setVerticalSpacing(6);
	if (props) {
		for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p))
			addProperty(form, p);
	}
	if (loading) {
		auto *l = new QLabel("Procurando dispositivos e opções…");
		l->setObjectName("muted");
		form->addRow(l);
	} else if (form->rowCount() == 0) {
		form->addRow(new QLabel("Esta entrada não tem opções."));
	}
	rebuilding = false;
	if (pendingApply) {
		obs_property_t *pa = pendingApply;
		pendingApply = nullptr;
		apply(pa);
	}
}

/* guarda a alteração; se a fonte pedir (ex.: escolheu outro dispositivo), refaz o formulário */
void PropertiesView::apply(obs_property_t *p)
{
	if (rebuilding)
		return;
	dirty = true;
	const bool redraw = obs_property_modified(p, data);
	emit changed();
	if (redraw)
		QTimer::singleShot(0, this, &PropertiesView::refresh);
}

void PropertiesView::resetToDefaults()
{
	obs_data_clear(data); /* ficam só os valores padrão */
	dirty = true;
	if (props)
		obs_properties_apply_settings(props, data);
	refresh();
}

void PropertiesView::applyToSource(obs_source_t *source)
{
	if (source)
		obs_source_update(source, data);
	dirty = false;
}

static QString text(const char *s)
{
	return QString::fromUtf8(s ? s : "");
}


/* Lista editável (Sequência de imagens, playlist do VLC...): arrastar para reordenar e soltar
 * arquivos do Explorer direto na lista. */
namespace {
class EditList : public QListWidget {
public:
	std::function<void(const QStringList &)> onFiles; /* arquivos soltos de fora */
	std::function<void()> onReorder;
	bool acceptFiles = false;
	EditList()
	{
		setDragDropMode(QAbstractItemView::DragDrop);
		setDefaultDropAction(Qt::MoveAction);
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setAcceptDrops(true);
		setMinimumHeight(150);
		setObjectName("editList");
	}

protected:
	void dragEnterEvent(QDragEnterEvent *e) override
	{
		if (e->source() != this && acceptFiles && e->mimeData()->hasUrls())
			e->acceptProposedAction();
		else
			QListWidget::dragEnterEvent(e);
	}
	void dragMoveEvent(QDragMoveEvent *e) override
	{
		if (e->source() != this && acceptFiles && e->mimeData()->hasUrls())
			e->acceptProposedAction();
		else
			QListWidget::dragMoveEvent(e);
	}
	void dropEvent(QDropEvent *e) override
	{
		if (e->source() != this) {
			QStringList files;
			for (const QUrl &u : e->mimeData()->urls())
				files << (u.isLocalFile() ? u.toLocalFile() : u.toString());
			if (onFiles && !files.isEmpty())
				onFiles(files);
			e->acceptProposedAction();
			return;
		}
		e->setDropAction(Qt::MoveAction);
		QListWidget::dropEvent(e);
		if (onReorder)
			QTimer::singleShot(0, onReorder);
	}
};
} // namespace

static QString text(const char *s);

void PropertiesView::addEditableList(QFormLayout *form, obs_property_t *p, const QString &label)
{
	const char *name = obs_property_name(p);
	const auto kind = obs_property_editable_list_type(p);
	const bool files = kind != OBS_EDITABLE_LIST_TYPE_STRINGS;
	const bool urls = kind == OBS_EDITABLE_LIST_TYPE_FILES_AND_URLS;

	auto *box = new QWidget;
	auto *v = new QVBoxLayout(box);
	v->setContentsMargins(0, 0, 0, 0);
	auto *list = new EditList;
	list->acceptFiles = files;
	{
		OBSDataArrayAutoRelease arr = obs_data_get_array(data, name);
		const size_t n = obs_data_array_count(arr);
		for (size_t i = 0; i < n; i++) {
			OBSDataAutoRelease it = obs_data_array_item(arr, i);
			auto *item = new QListWidgetItem(text(obs_data_get_string(it, "value")));
			item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
			item->setToolTip(item->text());
			list->addItem(item);
		}
	}
	/* a lista na tela vira a lista da fonte (na mesma ordem) */
	auto save = [this, list, p, name] {
		OBSDataArrayAutoRelease arr = obs_data_array_create();
		for (int i = 0; i < list->count(); i++) {
			OBSDataAutoRelease it = obs_data_create();
			obs_data_set_string(it, "value", list->item(i)->text().toUtf8().constData());
			obs_data_set_bool(it, "selected", false);
			obs_data_set_bool(it, "hidden", false);
			obs_data_array_push_back(arr, it);
		}
		obs_data_set_array(data, name, arr);
		apply(p);
	};
	auto addValues = [list, save](const QStringList &vals) {
		for (const QString &s : vals) {
			if (s.trimmed().isEmpty())
				continue;
			auto *item = new QListWidgetItem(s);
			item->setToolTip(s);
			list->addItem(item);
		}
		save();
	};
	list->onFiles = addValues;
	list->onReorder = save;
	v->addWidget(list);

	auto *row = new QHBoxLayout;
	auto button = [&](const QString &t, const QString &tip) {
		auto *b = new QPushButton(t);
		b->setToolTip(tip);
		row->addWidget(b);
		return b;
	};
	const QString filter = text(obs_property_editable_list_filter(p));
	const QString start = text(obs_property_editable_list_default_path(p));
	if (files) {
		connect(button("+ Arquivos…", "Escolha uma ou várias imagens (Ctrl/Shift para várias)"), &QPushButton::clicked, this,
			[this, filter, start, addValues] {
				const QStringList f = QFileDialog::getOpenFileNames(this, "Escolher arquivos", start, filter);
				if (!f.isEmpty())
					addValues(f);
			});
		connect(button("+ Pasta…", "Todas as imagens da pasta, em ordem de nome"), &QPushButton::clicked, this,
			[this, start, addValues] {
				const QString d = QFileDialog::getExistingDirectory(this, "Escolher pasta", start);
				if (!d.isEmpty())
					addValues({d});
			});
	}
	if (!files || urls) {
		connect(button(files ? "+ Link…" : "+ Adicionar…", QString()), &QPushButton::clicked, this, [this, files, addValues] {
			bool ok = false;
			const QString t = QInputDialog::getText(this, files ? "Adicionar link" : "Adicionar",
								files ? "Endereço (http://, rtsp://...):" : "Texto:",
								QLineEdit::Normal, QString(), &ok);
			if (ok && !t.trimmed().isEmpty())
				addValues({t.trimmed()});
		});
	}
	auto move = [list, save](int d) {
		const int r = list->currentRow();
		if (r < 0 || r + d < 0 || r + d >= list->count())
			return;
		QListWidgetItem *it = list->takeItem(r);
		list->insertItem(r + d, it);
		list->setCurrentRow(r + d);
		save();
	};
	connect(button("▲", "Subir"), &QPushButton::clicked, this, [move] { move(-1); });
	connect(button("▼", "Descer"), &QPushButton::clicked, this, [move] { move(1); });
	connect(button("Remover", "Tira os itens selecionados da lista (o arquivo não é apagado)"), &QPushButton::clicked, this,
		[list, save] {
			const auto sel = list->selectedItems();
			if (sel.isEmpty())
				return;
			for (QListWidgetItem *it : sel)
				delete it;
			save();
		});
	row->addStretch(1);
	v->addLayout(row);
	if (files) {
		auto *hint = new QLabel("Dica: arraste imagens ou pastas do Explorer para a lista; arraste os itens para mudar a ordem.");
		hint->setObjectName("muted");
		hint->setWordWrap(true);
		v->addWidget(hint);
	}
	box->setEnabled(obs_property_enabled(p));

	auto *l = new QLabel(label);
	l->setWordWrap(true);
	l->setMinimumWidth(110);
	l->setMaximumWidth(150);
	form->addRow(l, box);
}

void PropertiesView::addProperty(QFormLayout *form, obs_property_t *p)
{
	if (!obs_property_visible(p))
		return;
	const char *name = obs_property_name(p);
	const QString label = text(obs_property_description(p));
	const bool enabled = obs_property_enabled(p);
	QWidget *w = nullptr;

	switch (obs_property_get_type(p)) {
	case OBS_PROPERTY_BOOL: {
		/* texto longo não empurra o formulário para o lado: corta no fim e mostra inteiro no balão */
		auto *cb = new QCheckBox(label);
		cb->setToolTip(label);
		cb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		cb->setMinimumWidth(0);
		cb->setChecked(obs_data_get_bool(data, name));
		connect(cb, &QCheckBox::toggled, this, [this, p, name](bool v) {
			obs_data_set_bool(data, name, v);
			apply(p);
		});
		cb->setEnabled(enabled);
		form->addRow(QString(), cb);
		return;
	}
	case OBS_PROPERTY_INT: {
		auto *sb = new QSpinBox;
		sb->setRange(obs_property_int_min(p), obs_property_int_max(p));
		sb->setSingleStep(obs_property_int_step(p));
		sb->setSuffix(text(obs_property_int_suffix(p)));
		sb->setValue((int)obs_data_get_int(data, name));
		connect(sb, &QSpinBox::valueChanged, this, [this, p, name](int v) {
			obs_data_set_int(data, name, v);
			apply(p);
		});
		if (obs_property_int_type(p) == OBS_NUMBER_SLIDER) {
			auto *row = new QWidget;
			auto *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			auto *sl = new QSlider(Qt::Horizontal);
			sl->setRange(sb->minimum(), sb->maximum());
			sl->setValue(sb->value());
			connect(sl, &QSlider::valueChanged, sb, &QSpinBox::setValue);
			connect(sb, &QSpinBox::valueChanged, sl, &QSlider::setValue);
			h->addWidget(sl, 1);
			h->addWidget(sb);
			w = row;
		} else {
			w = sb;
		}
		break;
	}
	case OBS_PROPERTY_FLOAT: {
		auto *sb = new QDoubleSpinBox;
		sb->setRange(obs_property_float_min(p), obs_property_float_max(p));
		sb->setSingleStep(obs_property_float_step(p));
		sb->setDecimals(2);
		sb->setSuffix(text(obs_property_float_suffix(p)));
		sb->setValue(obs_data_get_double(data, name));
		connect(sb, &QDoubleSpinBox::valueChanged, this, [this, p, name](double v) {
			obs_data_set_double(data, name, v);
			apply(p);
		});
		w = sb;
		break;
	}
	case OBS_PROPERTY_TEXT: {
		const auto tt = obs_property_text_type(p);
		if (tt == OBS_TEXT_INFO) {
			auto *l = new QLabel(text(obs_data_get_string(data, name)).isEmpty() ? label : text(obs_data_get_string(data, name)));
			l->setWordWrap(true);
			form->addRow(l);
			return;
		}
		if (tt == OBS_TEXT_MULTILINE) {
			auto *te = new QPlainTextEdit(text(obs_data_get_string(data, name)));
			te->setMaximumHeight(90);
			connect(te, &QPlainTextEdit::textChanged, this, [this, te, p, name] {
				obs_data_set_string(data, name, te->toPlainText().toUtf8().constData());
				apply(p);
			});
			w = te;
		} else {
			auto *le = new QLineEdit(text(obs_data_get_string(data, name)));
			if (tt == OBS_TEXT_PASSWORD)
				le->setEchoMode(QLineEdit::Password);
			connect(le, &QLineEdit::editingFinished, this, [this, le, p, name] {
				obs_data_set_string(data, name, le->text().toUtf8().constData());
				apply(p);
			});
			w = le;
		}
		break;
	}
	case OBS_PROPERTY_PATH: {
		auto *row = new QWidget;
		auto *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		auto *le = new QLineEdit(text(obs_data_get_string(data, name)));
		auto *bt = new QPushButton("Procurar…");
		h->addWidget(le, 1);
		h->addWidget(bt);
		auto set = [this, le, p, name](const QString &v) {
			le->setText(v);
			obs_data_set_string(data, name, v.toUtf8().constData());
			apply(p);
		};
		connect(le, &QLineEdit::editingFinished, this, [le, set] { set(le->text()); });
		connect(bt, &QPushButton::clicked, this, [this, p, le, set] {
			const QString filter = text(obs_property_path_filter(p));
			const QString start = le->text().isEmpty() ? text(obs_property_path_default_path(p)) : le->text();
			QString v = obs_property_path_type(p) == OBS_PATH_DIRECTORY
					    ? QFileDialog::getExistingDirectory(this, "Escolher pasta", start)
					    : QFileDialog::getOpenFileName(this, "Escolher arquivo", start, filter);
			if (!v.isEmpty())
				set(v);
		});
		w = row;
		break;
	}
	case OBS_PROPERTY_LIST: {
		auto *cb = new QComboBox;
		const auto fmt = obs_property_list_format(p);
		const size_t n = obs_property_list_item_count(p);
		for (size_t i = 0; i < n; i++) {
			const QString itemName = text(obs_property_list_item_name(p, i));
			QVariant v;
			if (fmt == OBS_COMBO_FORMAT_INT)
				v = (qlonglong)obs_property_list_item_int(p, i);
			else if (fmt == OBS_COMBO_FORMAT_FLOAT)
				v = obs_property_list_item_float(p, i);
			else if (fmt == OBS_COMBO_FORMAT_BOOL)
				v = obs_property_list_item_bool(p, i);
			else
				v = text(obs_property_list_item_string(p, i));
			cb->addItem(itemName, v);
			if (obs_property_list_item_disabled(p, i))
				cb->setItemData((int)i, 0, Qt::UserRole - 1);
		}
		int cur = -1;
		for (int i = 0; i < cb->count(); i++) {
			const QVariant v = cb->itemData(i);
			if ((fmt == OBS_COMBO_FORMAT_INT && v.toLongLong() == obs_data_get_int(data, name)) ||
			    (fmt == OBS_COMBO_FORMAT_FLOAT && qFuzzyCompare(v.toDouble() + 1, obs_data_get_double(data, name) + 1)) ||
			    (fmt == OBS_COMBO_FORMAT_BOOL && v.toBool() == obs_data_get_bool(data, name)) ||
			    (fmt == OBS_COMBO_FORMAT_STRING && v.toString() == text(obs_data_get_string(data, name)))) {
				cur = i;
				break;
			}
		}
		cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
		cb->setMinimumContentsLength(8);
		if (cur < 0 && cb->count() > 0 && fmt == OBS_COMBO_FORMAT_STRING &&
		    text(obs_data_get_string(data, name)).isEmpty() && autoPicks < 3) {
			/* nada escolhido ainda (ex.: dispositivo): já seleciona o primeiro, como o OBS faz */
			cur = 0;
			obs_data_set_string(data, name, cb->itemData(0).toString().toUtf8().constData());
			pendingApply = p;
			autoPicks++;
		}
		cb->setCurrentIndex(cur);
		connect(cb, &QComboBox::currentIndexChanged, this, [this, cb, p, name, fmt](int i) {
			if (i < 0)
				return;
			const QVariant v = cb->itemData(i);
			if (fmt == OBS_COMBO_FORMAT_INT)
				obs_data_set_int(data, name, v.toLongLong());
			else if (fmt == OBS_COMBO_FORMAT_FLOAT)
				obs_data_set_double(data, name, v.toDouble());
			else if (fmt == OBS_COMBO_FORMAT_BOOL)
				obs_data_set_bool(data, name, v.toBool());
			else
				obs_data_set_string(data, name, v.toString().toUtf8().constData());
			apply(p);
		});
		w = cb;
		break;
	}
	case OBS_PROPERTY_COLOR:
	case OBS_PROPERTY_COLOR_ALPHA: {
		auto *bt = new QPushButton;
		auto paint = [bt](uint32_t c) {
			const QColor q(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
			bt->setText(q.name().toUpper());
			bt->setStyleSheet(QString("background:%1; color:%2;").arg(q.name(), q.lightness() > 128 ? "#000" : "#fff"));
		};
		paint((uint32_t)obs_data_get_int(data, name));
		connect(bt, &QPushButton::clicked, this, [this, p, name, paint] {
			const uint32_t c = (uint32_t)obs_data_get_int(data, name);
			const QColor q = QColorDialog::getColor(QColor(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF), this);
			if (!q.isValid())
				return;
			const uint32_t v = 0xFF000000u | ((uint32_t)q.blue() << 16) | ((uint32_t)q.green() << 8) | (uint32_t)q.red();
			obs_data_set_int(data, name, v);
			paint(v);
			apply(p);
		});
		w = bt;
		break;
	}
	case OBS_PROPERTY_BUTTON: {
		/* o Ativar/Desativar do driver desligaria a câmera em TODAS as entradas: o programa usa o seu,
		 * que vale só para a entrada (Desativar entrada) */
		if (type == "dshow_input" && strcmp(name, "activate") == 0)
			return;
		auto *bt = new QPushButton(label);
		connect(bt, &QPushButton::clicked, this, [this, p] {
			OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
			if (src && obs_property_button_clicked(p, src))
				QTimer::singleShot(0, this, &PropertiesView::reload);
		});
		bt->setEnabled(enabled && weak);
		form->addRow(QString(), bt);
		return;
	}
	case OBS_PROPERTY_GROUP: {
		obs_properties_t *group = obs_property_group_content(p);
		auto *box = new QGroupBox(label);
		auto *inner = new QFormLayout(box);
		if (obs_property_group_type(p) == OBS_GROUP_CHECKABLE) {
			box->setCheckable(true);
			box->setChecked(obs_data_get_bool(data, name));
			connect(box, &QGroupBox::toggled, this, [this, p, name](bool v) {
				obs_data_set_bool(data, name, v);
				apply(p);
			});
		}
		for (obs_property_t *q = obs_properties_first(group); q; obs_property_next(&q))
			addProperty(inner, q);
		form->addRow(box);
		return;
	}
	case OBS_PROPERTY_EDITABLE_LIST:
		addEditableList(form, p, label);
		return;
	default:
		return; /* tipos raros (fonte de letra, quadros por segundo) ficam para depois */
	}

	if (w) {
		w->setEnabled(enabled);
		const QString tip = text(obs_property_long_description(p));
		if (!tip.isEmpty())
			w->setToolTip(tip);
		auto *l = new QLabel(label);
		l->setWordWrap(true);
		l->setMinimumWidth(110);
		l->setMaximumWidth(150);
		form->addRow(l, w);
	}
}
