#include "properties-view.hpp"

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

PropertiesView::PropertiesView(obs_source_t *source, obs_data_t *settings, const char *sourceType, QWidget *parent)
	: QWidget(parent),
	  type(sourceType)
{
	if (source) {
		weak = obs_source_get_weak_source(source);
		data = obs_source_get_settings(source);
	} else {
		data = settings;
		obs_data_addref(data);
	}
	auto *lay = new QVBoxLayout(this);
	lay->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	lay->addWidget(scroll);
	content = new QWidget;
	scroll->setWidget(content);
	rebuild();
}

PropertiesView::~PropertiesView()
{
	obs_properties_destroy(props);
	obs_data_release(data);
}

void PropertiesView::rebuild()
{
	rebuilding = true;
	obs_properties_destroy(props);
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	props = src ? obs_source_properties(src) : obs_get_source_properties(type.constData());

	delete content->layout();
	for (QObject *c : content->children())
		if (c->isWidgetType())
			delete c;
	auto *form = new QFormLayout(content);
	form->setLabelAlignment(Qt::AlignLeft);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setContentsMargins(8, 8, 8, 8);
	form->setVerticalSpacing(6);
	if (props) {
		for (obs_property_t *p = obs_properties_first(props); p; obs_property_next(&p))
			addProperty(form, p);
	}
	if (form->rowCount() == 0)
		form->addRow(new QLabel("Esta entrada não tem opções."));
	rebuilding = false;
}

/* grava a alteração na fonte; se a fonte pedir, refaz o formulário (ex.: escolheu outro dispositivo) */
void PropertiesView::apply(obs_property_t *p)
{
	if (rebuilding)
		return;
	OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
	if (src)
		obs_source_update(src, data);
	const bool refresh = obs_property_modified(p, data);
	emit changed();
	if (refresh)
		QTimer::singleShot(0, this, &PropertiesView::rebuild);
}

static QString text(const char *s)
{
	return QString::fromUtf8(s ? s : "");
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
		auto *cb = new QCheckBox(label);
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
		auto *bt = new QPushButton(label);
		connect(bt, &QPushButton::clicked, this, [this, p] {
			OBSSourceAutoRelease src = obs_weak_source_get_source(weak);
			if (src && obs_property_button_clicked(p, src))
				QTimer::singleShot(0, this, &PropertiesView::rebuild);
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
	default:
		return; /* tipos raros (fonte, lista editável...) ficam para depois */
	}

	if (w) {
		w->setEnabled(enabled);
		const QString tip = text(obs_property_long_description(p));
		if (!tip.isEmpty())
			w->setToolTip(tip);
		form->addRow(label, w);
	}
}
