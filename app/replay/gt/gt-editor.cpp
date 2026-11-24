/* Editor de título no estilo do Title Editor do vMix: abas Texto / Imagens /
 * Cor com a lista de campos, edição ao vivo e presets. */
#include "gt-source.hpp"
#include "../studio-host.hpp"
#include "i18n.hpp"
#include "gt-render.hpp"
#include "../common.hpp"
#include "../ui-theme.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <functional>
#include <QColorDialog>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <util/platform.h>

static QString presetsFile(const QString &title)
{
	char *p = rdsReplayConfigPath("gt-presets"); /* Studio: <config>/replay/gt-presets */
	QString dir = p ? QString::fromUtf8(p) : QDir::homePath() + "/.instant-replay-gt-presets";
	bfree(p);
	QDir().mkpath(dir);
	QString safe = title;
	safe.replace(QRegularExpression("[^A-Za-z0-9_ -]"), "_");
	return dir + "/" + safe + ".json";
}


/* ---------- presets de planilha (CSV / TXT) ---------- */

static QString decodeText(const QByteArray &raw)
{
	QByteArray b = raw;
	if (b.startsWith("\xEF\xBB\xBF"))
		b = b.mid(3);
	const QString u = QString::fromUtf8(b);
	/* Excel costuma salvar em Windows-1252: se não for UTF-8 válido, lê como Latin-1 */
	if (u.contains(QChar::ReplacementCharacter))
		return QString::fromLatin1(b);
	return u;
}

static QList<QStringList> parseDelimited(const QString &text)
{
	const QString first = text.section('\n', 0, 0);
	QChar sep = ';';
	int best = (int)first.count(';');
	for (QChar c : {QChar(','), QChar('\t')})
		if ((int)first.count(c) > best) {
			best = (int)first.count(c);
			sep = c;
		}
	if (best == 0)
		sep = QChar(0); /* uma coluna só (TXT) */
	QList<QStringList> rows;
	QStringList row;
	QString cell;
	bool quoted = false;
	for (int i = 0; i < text.size(); i++) {
		const QChar c = text[i];
		if (quoted) {
			if (c == '"') {
				if (i + 1 < text.size() && text[i + 1] == '"') {
					cell += '"';
					i++;
				} else
					quoted = false;
			} else
				cell += c;
		} else if (c == '"' && cell.isEmpty()) {
			quoted = true;
		} else if (sep != QChar(0) && c == sep) {
			row << cell;
			cell.clear();
		} else if (c == '\n' || c == '\r') {
			if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
				i++;
			row << cell;
			cell.clear();
			bool empty = true;
			for (const QString &x : row)
				empty &= x.trimmed().isEmpty();
			if (!empty)
				rows << row;
			row.clear();
		} else
			cell += c;
	}
	if (!cell.isEmpty() || !row.isEmpty()) {
		row << cell;
		rows << row;
	}
	/* "\n" escrito dentro da célula vira quebra de linha no título */
	for (auto &r : rows)
		for (auto &x : r)
			x.replace("\\n", "\n");
	return rows;
}

/* nome da coluna → campo do título ("golscasa", "golscasa.Text", "ESCUDO.Source"...) */
static QString matchField(const QString &header, const std::vector<GtField> &fields)
{
	const QString h = header.trimmed();
	for (const GtField &f : fields)
		if (f.id.compare(h, Qt::CaseInsensitive) == 0)
			return f.id;
	for (const GtField &f : fields)
		if (f.id.section('.', 0, 0).compare(h, Qt::CaseInsensitive) == 0 &&
		    (f.kind == GtField::Text || f.kind == GtField::Image))
			return f.id;
	return QString();
}

/* ---------- prévia do título com seleção, arrasto e redimensionamento ---------- */

class GtCanvas : public QWidget {
public:
	struct Hit {
		QString id; /* "E:nome" ou "L:nome" */
		QRectF abs; /* em pixels do título */
	};
	std::function<void(const QString &)> onSelect;
	std::function<void(const QString &, const QRectF &)> onChange; /* novo retângulo absoluto */

	explicit GtCanvas(QWidget *parent) : QWidget(parent)
	{
		setMinimumSize(480, 270);
		setMouseTracking(true);
		setCursor(Qt::ArrowCursor);
	}
	void setImage(const QImage &img, int w, int h)
	{
		image = img;
		W = w;
		H = h;
		update();
	}
	void setHits(const std::vector<Hit> &h) { hits = h; }
	void setSelection(const QString &id, const QRectF &abs)
	{
		selId = id;
		selRect = abs;
		update();
	}

protected:
	QRectF view() const
	{
		if (W <= 0 || H <= 0)
			return QRectF();
		const double s = std::min(width() / (double)W, height() / (double)H);
		return QRectF((width() - W * s) / 2, (height() - H * s) / 2, W * s, H * s);
	}
	double scale() const
	{
		const QRectF v = view();
		return W > 0 ? v.width() / W : 1;
	}
	QPointF toTitle(const QPointF &p) const
	{
		const QRectF v = view();
		return QPointF((p.x() - v.x()) / scale(), (p.y() - v.y()) / scale());
	}
	QRectF toWidget(const QRectF &r) const
	{
		const QRectF v = view();
		const double s = scale();
		return QRectF(v.x() + r.x() * s, v.y() + r.y() * s, r.width() * s, r.height() * s);
	}
	QRectF handle() const
	{
		const QRectF r = toWidget(selRect);
		return QRectF(r.right() - 6, r.bottom() - 6, 12, 12);
	}

	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.fillRect(rect(), QColor(40, 40, 40));
		const QRectF v = view();
		/* xadrez = transparente */
		const int c = 16;
		p.save();
		p.setClipRect(v);
		for (int y = (int)v.top(); y < v.bottom(); y += c)
			for (int x = (int)v.left(); x < v.right(); x += c)
				p.fillRect(x, y, c, c, (((x - (int)v.left()) / c + (y - (int)v.top()) / c) % 2) ? QColor(70, 70, 70) : QColor(55, 55, 55));
		p.restore();
		if (!image.isNull())
			p.drawImage(v, image);
		p.setPen(QPen(QColor(120, 120, 120), 1));
		p.drawRect(v);
		if (!selId.isEmpty()) {
			const QRectF r = toWidget(selRect);
			p.setPen(QPen(QColor(255, 200, 0), 2, Qt::DashLine));
			p.setBrush(Qt::NoBrush);
			p.drawRect(r);
			p.fillRect(handle(), QColor(255, 200, 0));
		}
	}

	void mousePressEvent(QMouseEvent *e) override
	{
		if (e->button() != Qt::LeftButton)
			return;
		const QPointF pt = e->position();
		if (!selId.isEmpty() && handle().contains(pt)) {
			mode = Resize;
		} else {
			const QPointF tp = toTitle(pt);
			QString hitId;
			QRectF hitRect;
			for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
				if (it->abs.contains(tp)) {
					hitId = it->id;
					hitRect = it->abs;
					break;
				}
			}
			if (hitId.isEmpty())
				return;
			if (hitId != selId) {
				selId = hitId;
				selRect = hitRect;
				if (onSelect)
					onSelect(hitId);
			}
			mode = Move;
		}
		pressPt = toTitle(pt);
		pressRect = selRect;
		update();
	}

	void mouseMoveEvent(QMouseEvent *e) override
	{
		const QPointF pt = e->position();
		if (mode == None) {
			setCursor(!selId.isEmpty() && handle().contains(pt) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
			return;
		}
		const QPointF d = toTitle(pt) - pressPt;
		if (mode == Move)
			selRect = pressRect.translated(std::round(d.x()), std::round(d.y()));
		else
			selRect = QRectF(pressRect.topLeft(),
					 QSizeF(std::max(2.0, std::round(pressRect.width() + d.x())),
						std::max(2.0, std::round(pressRect.height() + d.y()))));
		update();
		if (onChange)
			onChange(selId, selRect);
	}

	void mouseReleaseEvent(QMouseEvent *) override { mode = None; }

private:
	enum { None, Move, Resize } mode = None;
	QImage image;
	int W = 0, H = 0;
	std::vector<Hit> hits;
	QString selId;
	QRectF selRect;
	QPointF pressPt;
	QRectF pressRect;
};

class GtEditor : public QDialog {
public:
	GtEditor(obs_source_t *src, QWidget *parent) : QDialog(parent)
	{
		source = obs_source_get_weak_source(src);
		title = gtTitleOf(src);
		values = gtValues(src);
		setAttribute(Qt::WA_DeleteOnClose);
		ui::applyDialog(this);
		setWindowTitle(tl("Editor de título: %1").arg(title ? title->name : QString("?")));
		resize(820, 520);
		build();
		loadPresets();
		showKind(GtField::Text);
	}
	~GtEditor() override { obs_weak_source_release(source); }

private:
	obs_weak_source_t *source = nullptr;
	std::shared_ptr<GtTitle> title;
	GtValues values;
	std::vector<GtField> fields;
	GtField::Kind kind = GtField::Text;

	QListWidget *list;
	QStackedWidget *stack;
	QPlainTextEdit *textEdit;
	QLabel *imgPreview;
	QLabel *imgPath;
	QPushButton *colorBtn;
	QLineEdit *colorHex;
	QPushButton *liveBtn;
	QPushButton *applyBtn;
	QListWidget *presetList;
	QJsonObject presets;
	bool loading = false;
	bool pending = false;

	/* aba Layout */
	bool layoutMode = false;
	GtCanvas *canvas = nullptr;
	QWidget *propHost = nullptr;
	QWidget *propForm = nullptr;
	QTimer *previewTimer = nullptr;
	QString selId;

	QString currentId() const
	{
		auto *it = list->currentItem();
		return it ? it->data(Qt::UserRole).toString() : QString();
	}
	const GtField *currentField() const
	{
		const QString id = currentId();
		for (auto &f : fields)
			if (f.id == id)
				return &f;
		return nullptr;
	}
	QString valueOf(const GtField &f) const { return values.contains(f.id) ? values.value(f.id) : f.value; }

	void build()
	{
		auto *root = new QHBoxLayout(this);
		root->setContentsMargins(0, 0, 0, 0);
		root->setSpacing(0);

		/* ---- esquerda: abas + campos + Ao vivo ---- */
		auto *left = new QWidget(this);
		left->setFixedWidth(270);
		left->setObjectName("gtLeft");
		left->setStyleSheet("QWidget#gtLeft { background: #1a1d24; border-right: 1px solid #2d323d; }"
				    "QListWidget { background: transparent; border: none; color: #e8ebf1; font-weight: 600; outline: none; }"
				    "QListWidget::item { padding: 7px 10px; border-radius: 6px; margin: 1px 6px; }"
				    "QListWidget::item:hover { background: #242832; }"
				    "QListWidget::item:selected { background: #1e3a8a; color: white; }");
		auto *lv = new QVBoxLayout(left);
		lv->setContentsMargins(0, 0, 0, 0);
		lv->setSpacing(0);
		auto *tabs = new QHBoxLayout();
		tabs->setSpacing(0);
		auto *grp = new QButtonGroup(this);
		const char *names[4] = {N_("Texto"), N_("Imagens"), N_("Cor"), N_("Layout")};
		for (int i = 0; i < 4; i++) {
			auto *b = new QPushButton(tl(names[i]), left);
			b->setCheckable(true);
			b->setMinimumHeight(34);
			b->setStyleSheet("QPushButton { color: #8b93a5; border: none; border-bottom: 2px solid transparent; border-radius: 0;"
					 " background: transparent; font-weight: 700; }"
					 "QPushButton:hover { color: #e8ebf1; }"
					 "QPushButton:checked { color: #e8ebf1; border-bottom: 2px solid #3b82f6; background: transparent; }");
			grp->addButton(b, i);
			tabs->addWidget(b);
			if (i == 0)
				b->setChecked(true);
			if (i == 3) {
				connect(b, &QPushButton::clicked, this, [this] { showLayout(); });
			} else {
				const GtField::Kind k = i == 0 ? GtField::Text : i == 1 ? GtField::Image : GtField::Color;
				connect(b, &QPushButton::clicked, this, [this, k] { showKind(k); });
			}
		}
		lv->addLayout(tabs);
		list = new QListWidget(left);
		lv->addWidget(list, 1);
		liveBtn = new QPushButton(tl("Ao vivo"), left);
		liveBtn->setCheckable(true);
		liveBtn->setChecked(true);
		liveBtn->setMinimumHeight(40);
		liveBtn->setToolTip(tl("Ligado: cada alteração vai na hora para o título.\nDesligado: use Aplicar."));
		liveBtn->setProperty("kind", "go");
		liveBtn->setText(tl("●  Ao vivo"));
		lv->addWidget(liveBtn);
		root->addWidget(left);
		connect(list, &QListWidget::currentRowChanged, this, [this] {
			if (layoutMode)
				selectItem(currentId(), false);
			else
				showField();
		});

		/* ---- direita: editor do campo + presets ---- */
		auto *right = new QWidget(this);
		auto *rv = new QVBoxLayout(right);
		stack = new QStackedWidget(right);

		textEdit = new QPlainTextEdit(stack);
		textEdit->setStyleSheet("font-size: 16px;");
		stack->addWidget(textEdit);
		connect(textEdit, &QPlainTextEdit::textChanged, this, [this] {
			if (loading)
				return;
			const GtField *f = currentField();
			if (!f)
				return;
			/* o GT usa \r\n entre as linhas */
			QString t = textEdit->toPlainText();
			t.replace("\n", "\r\n");
			setValue(f->id, t);
		});

		auto *imgPage = new QWidget(stack);
		auto *iv = new QVBoxLayout(imgPage);
		imgPreview = new QLabel(imgPage);
		imgPreview->setAlignment(Qt::AlignCenter);
		imgPreview->setMinimumHeight(220);
		imgPreview->setStyleSheet("background: black;");
		iv->addWidget(imgPreview, 1);
		imgPath = new QLabel(imgPage);
		imgPath->setWordWrap(true);
		iv->addWidget(imgPath);
		auto *ib = new QHBoxLayout();
		auto *browse = new QPushButton(tl("Procurar…"), imgPage);
		auto *restore = new QPushButton(tl("Imagem original"), imgPage);
		auto *clear = new QPushButton(tl("Sem imagem"), imgPage);
		ib->addWidget(browse);
		ib->addWidget(restore);
		ib->addWidget(clear);
		ib->addStretch(1);
		iv->addLayout(ib);
		stack->addWidget(imgPage);
		connect(browse, &QPushButton::clicked, this, [this] {
			const GtField *f = currentField();
			if (!f)
				return;
			const QString file = QFileDialog::getOpenFileName(
				this, tl("Escolher imagem"), QString(),
				tl("Imagens (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga)"));
			if (!file.isEmpty()) {
				setValue(f->id, file);
				showField();
			}
		});
		connect(restore, &QPushButton::clicked, this, [this] {
			if (const GtField *f = currentField()) {
				values.remove(f->id);
				push();
				showField();
			}
		});
		connect(clear, &QPushButton::clicked, this, [this] {
			if (const GtField *f = currentField()) {
				setValue(f->id, QString());
				showField();
			}
		});

		auto *colPage = new QWidget(stack);
		auto *cv = new QVBoxLayout(colPage);
		colorBtn = new QPushButton(colPage);
		colorBtn->setMinimumHeight(120);
		cv->addWidget(colorBtn);
		auto *ch = new QHBoxLayout();
		ch->addWidget(new QLabel(tl("Cor (#AARRGGBB):"), colPage));
		colorHex = new QLineEdit(colPage);
		ch->addWidget(colorHex, 1);
		cv->addLayout(ch);
		cv->addStretch(1);
		stack->addWidget(colPage);
		connect(colorBtn, &QPushButton::clicked, this, [this] {
			const GtField *f = currentField();
			if (!f)
				return;
			const QColor c = QColorDialog::getColor(gtColor(valueOf(*f)), this, tl("Cor"),
								QColorDialog::ShowAlphaChannel);
			if (c.isValid()) {
				setValue(f->id, gtColorString(c));
				showField();
			}
		});
		connect(colorHex, &QLineEdit::editingFinished, this, [this] {
			const GtField *f = currentField();
			if (!f || loading)
				return;
			setValue(f->id, gtColorString(gtColor(colorHex->text())));
			showField();
		});

		/* aba Layout: prévia + propriedades */
		auto *layPage = new QSplitter(Qt::Horizontal, stack);
		canvas = new GtCanvas(layPage);
		canvas->onSelect = [this](const QString &id) { selectItem(id, true); };
		canvas->onChange = [this](const QString &id, const QRectF &abs) { moveItem(id, abs); };
		layPage->addWidget(canvas);
		auto *propScroll = new QScrollArea(layPage);
		propScroll->setWidgetResizable(true);
		propScroll->setMinimumWidth(420);
		propHost = new QWidget(propScroll);
		new QVBoxLayout(propHost);
		propScroll->setWidget(propHost);
		layPage->addWidget(propScroll);
		layPage->setStretchFactor(0, 1);
		layPage->setStretchFactor(1, 0);
		stack->addWidget(layPage);
		previewTimer = new QTimer(this);
		previewTimer->setSingleShot(true);
		connect(previewTimer, &QTimer::timeout, this, [this] { refreshPreview(); });

		rv->addWidget(stack, 3);

		auto *tools = new QHBoxLayout();
		applyBtn = new QPushButton(tl("Aplicar"), right);
		applyBtn->setEnabled(false);
		connect(applyBtn, &QPushButton::clicked, this, [this] {
			push();
			pending = false;
			applyBtn->setEnabled(false);
		});
		connect(liveBtn, &QPushButton::toggled, this, [this](bool on) {
			if (on && pending) {
				push();
				pending = false;
			}
			applyBtn->setEnabled(!on && pending);
		});
		auto *inBtn = new QPushButton(tl("▶ ENTRADA"), right);
		inBtn->setProperty("kind", "go");
		auto *outBtn = new QPushButton(tl("← SAÍDA"), right);
		outBtn->setProperty("kind", "out");
		connect(inBtn, &QPushButton::clicked, this, [this] { withSource([](obs_source_t *s) { gtPlayIn(s); }); });
		connect(outBtn, &QPushButton::clicked, this, [this] { withSource([](obs_source_t *s) { gtPlayOut(s); }); });
		tools->addWidget(applyBtn);
		tools->addStretch(1);
		tools->addWidget(inBtn);
		tools->addWidget(outBtn);
		rv->addLayout(tools);

		/* presets */
		auto *ph = new QHBoxLayout();
		auto *plus = new QPushButton("+", right);
		auto *minus = new QPushButton(QString::fromUtf8("−"), right);
		auto *reset = new QPushButton(tl("Limpar"), right);
		auto *imp = new QPushButton(tl("Importação"), right);
		auto *exp = new QPushButton(tl("Exportação"), right);
		for (auto *b : {plus, minus})
			b->setFixedWidth(32);
		plus->setToolTip(tl("Salva os valores atuais como preset"));
		minus->setToolTip(tl("Apaga o preset selecionado"));
		reset->setToolTip(tl("Volta todos os campos para o original do arquivo"));
		ph->addWidget(plus);
		ph->addWidget(minus);
		ph->addWidget(reset);
		ph->addWidget(imp);
		ph->addWidget(exp);
		ph->addWidget(new QLabel(tl("<b>Presets</b>"), right));
		ph->addStretch(1);
		rv->addLayout(ph);
		presetList = new QListWidget(right);
		presetList->setToolTip(tl("Dois cliques aplicam o preset"));
		rv->addWidget(presetList, 1);
		root->addWidget(right, 1);

		connect(plus, &QPushButton::clicked, this, [this] {
			bool ok = false;
			const QString name = QInputDialog::getText(this, tl("Novo preset"), tl("Nome:"),
								   QLineEdit::Normal, QString(), &ok);
			if (!ok || name.trimmed().isEmpty())
				return;
			QJsonObject o;
			for (auto it = values.begin(); it != values.end(); ++it)
				o.insert(it.key(), it.value());
			presets.insert(name.trimmed(), o);
			savePresets();
		});
		connect(minus, &QPushButton::clicked, this, [this] {
			if (auto *it = presetList->currentItem()) {
				presets.remove(it->text());
				savePresets();
			}
		});
		connect(reset, &QPushButton::clicked, this, [this] {
			values.clear();
			push();
			if (layoutMode) {
				refreshPreview();
				buildForm();
			} else
				showField();
		});
		connect(presetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
			const QJsonObject o = presets.value(it->text()).toObject();
			values.clear();
			for (auto k = o.begin(); k != o.end(); ++k)
				values.insert(k.key(), k.value().toString());
			push();
			showField();
		});
		connect(exp, &QPushButton::clicked, this, [this] {
			const QString f = QFileDialog::getSaveFileName(
				this, tl("Exportar presets"), (title ? title->name : QString("presets")) + ".csv",
				tl("Planilha CSV (*.csv);;Texto (*.txt);;JSON (*.json)"));
			if (f.isEmpty())
				return;
			QFile out(f);
			if (!out.open(QIODevice::WriteOnly))
				return;
			if (f.endsWith(".json", Qt::CaseInsensitive)) {
				out.write(QJsonDocument(presets).toJson());
				return;
			}
			/* CSV com ";" (abre direto no Excel em português) */
			QStringList cols;
			for (auto k = presets.begin(); k != presets.end(); ++k) {
				const QJsonObject o = k.value().toObject();
				for (auto c = o.begin(); c != o.end(); ++c)
					if (!cols.contains(c.key()))
						cols << c.key();
			}
			auto q = [](QString v) {
				v.replace("\r\n", "\n").replace('\r', '\n');
				if (v.contains(';') || v.contains('"') || v.contains('\n'))
					v = '"' + v.replace("\"", "\"\"") + '"';
				return v;
			};
			QString csv = "Preset";
			for (const QString &c : cols)
				csv += ";" + q(c);
			csv += "\r\n";
			for (auto k = presets.begin(); k != presets.end(); ++k) {
				const QJsonObject o = k.value().toObject();
				csv += q(k.key());
				for (const QString &c : cols)
					csv += ";" + q(o.value(c).toString());
				csv += "\r\n";
			}
			out.write("\xEF\xBB\xBF");
			out.write(csv.toUtf8());
		});
		connect(imp, &QPushButton::clicked, this, [this] {
			const QString f = QFileDialog::getOpenFileName(
				this, tl("Importar presets"), QString(),
				tl("Presets (*.csv *.txt *.json);;Planilha CSV (*.csv);;Texto (*.txt);;JSON (*.json)"));
			if (f.isEmpty())
				return;
			QFile in(f);
			if (!in.open(QIODevice::ReadOnly))
				return;
			const QByteArray raw = in.readAll();
			if (f.endsWith(".json", Qt::CaseInsensitive)) {
				const QJsonObject o = QJsonDocument::fromJson(raw).object();
				for (auto k = o.begin(); k != o.end(); ++k)
					presets.insert(k.key(), k.value());
				savePresets();
				return;
			}
			const int n = importDelimited(decodeText(raw));
			if (n > 0)
				savePresets();
			QMessageBox::information(this, "ReplayDeck GT",
						 n > 0 ? tl("%1 preset(s) importado(s).").arg(n)
						       : tl("Nenhuma linha reconhecida no arquivo."));
		});
	}

	/* Cada linha vira um preset.
	 * Com cabeçalho (nomes dos campos, ex.: "TITULO TIME;TITULO TIME2;ESCUDO"): cada coluna vai
	 * para o campo do mesmo nome; uma coluna "Preset"/"Nome do preset" dá o nome.
	 * Sem cabeçalho: as colunas preenchem os campos de texto na ordem (TXT: uma linha = o 1º campo). */
	int importDelimited(const QString &text)
	{
		if (!title)
			return 0;
		const std::vector<GtField> fields = title->fields();
		std::vector<QString> textFields;
		for (const GtField &fl : fields)
			if (fl.kind == GtField::Text)
				textFields.push_back(fl.id);
		const QList<QStringList> rows = parseDelimited(text);
		if (rows.isEmpty())
			return 0;
		std::vector<QString> map(rows[0].size());
		int nameCol = -1, matched = 0;
		for (int c = 0; c < rows[0].size(); c++) {
			const QString h = rows[0][c].trimmed();
			if (h.compare("Preset", Qt::CaseInsensitive) == 0 || h.compare("Nome do preset", Qt::CaseInsensitive) == 0)
				nameCol = c;
			else if (!(map[c] = matchField(h, fields)).isEmpty())
				matched++;
		}
		const bool header = matched > 0 || nameCol >= 0;
		if (!header) {
			map.assign(rows[0].size(), QString());
			for (int c = 0; c < rows[0].size() && c < (int)textFields.size(); c++)
				map[c] = textFields[(size_t)c];
		}
		int n = 0;
		for (int r = header ? 1 : 0; r < rows.size(); r++) {
			QJsonObject o;
			for (int c = 0; c < rows[r].size() && c < (int)map.size(); c++)
				if (!map[c].isEmpty())
					o.insert(map[c], rows[r][c]);
			if (o.isEmpty())
				continue;
			QString name = nameCol >= 0 && nameCol < rows[r].size() ? rows[r][nameCol].trimmed() : QString();
			if (name.isEmpty())
				for (const QString &x : rows[r])
					if (!x.trimmed().isEmpty()) {
						name = x.trimmed().section('\n', 0, 0).left(40);
						break;
					}
			QString unique = name;
			for (int i = 2; presets.contains(unique); i++)
				unique = QString("%1 (%2)").arg(name).arg(i);
			presets.insert(unique, o);
			n++;
		}
		return n;
	}

	template<typename F> void withSource(F f)
	{
		obs_source_t *s = obs_weak_source_get_source(source);
		if (s)
			f(s);
		obs_source_release(s);
	}

	void setValue(const QString &id, const QString &v)
	{
		values.insert(id, v);
		if (layoutMode && previewTimer)
			previewTimer->start(60);
		if (liveBtn->isChecked()) {
			push();
		} else {
			pending = true;
			applyBtn->setEnabled(true);
		}
	}

	void push()
	{
		withSource([this](obs_source_t *s) { gtSetValues(s, values); });
	}

	void showKind(GtField::Kind k)
	{
		layoutMode = false;
		kind = k;
		fields = title ? title->fields() : std::vector<GtField>();
		loading = true;
		list->clear();
		for (auto &f : fields) {
			if (f.kind != k)
				continue;
			auto *it = new QListWidgetItem(f.id, list);
			it->setData(Qt::UserRole, f.id);
		}
		loading = false;
		stack->setCurrentIndex(k == GtField::Text ? 0 : k == GtField::Image ? 1 : 2);
		resize(std::max(width(), 820), height());
		if (list->count())
			list->setCurrentRow(0);
		else
			showField();
	}

	void showField()
	{
		const GtField *f = currentField();
		loading = true;
		if (!f) {
			textEdit->clear();
			imgPreview->clear();
			imgPath->clear();
			loading = false;
			return;
		}
		const QString v = valueOf(*f);
		if (f->kind == GtField::Text) {
			QString t = v;
			t.replace("\r\n", "\n");
			t.replace('\r', '\n');
			textEdit->setPlainText(t);
		} else if (f->kind == GtField::Image) {
			QImage img;
			if (values.contains(f->id))
				img = v.isEmpty() ? QImage() : QImage(v.startsWith("file:") ? v.mid(5) : v);
			else if (title)
				img = title->image(f->value);
			imgPreview->setPixmap(img.isNull() ? QPixmap()
							   : QPixmap::fromImage(img.scaled(imgPreview->size() * 0.95,
											   Qt::KeepAspectRatio,
											   Qt::SmoothTransformation)));
			imgPath->setText(values.contains(f->id) ? (v.isEmpty() ? tl("(sem imagem)") : v)
								: tl("Original: ") + (title ? title->resourceFileName(f->value) : QString()));
		} else {
			const QColor c = gtColor(v);
			colorBtn->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); border: 1px solid #888;")
							.arg(c.red())
							.arg(c.green())
							.arg(c.blue())
							.arg(c.alpha()));
			colorHex->setText(gtColorString(c));
		}
		loading = false;
	}


	/* ---------------- aba Layout ---------------- */

	const GtLayer *layerByName(const QString &n) const
	{
		if (!title)
			return nullptr;
		for (auto &L : title->layers)
			if (L.name == n)
				return &L;
		return nullptr;
	}
	bool elementByName(const QString &n, const GtLayer **layer, const GtElement **el) const
	{
		if (!title)
			return false;
		for (auto &L : title->layers)
			for (auto &e : L.elements)
				if (e.name == n) {
					*layer = &L;
					*el = &e;
					return true;
				}
		return false;
	}
	QRectF absRect(const QString &id) const
	{
		const QString name = id.mid(2);
		if (id.startsWith("L:")) {
			if (const GtLayer *L = layerByName(name))
				return gtLayerRect(*L, values);
			return QRectF();
		}
		const GtLayer *L = nullptr;
		const GtElement *e = nullptr;
		if (!elementByName(name, &L, &e))
			return QRectF();
		const QRectF lr = gtLayerRect(*L, values);
		const GtElement ae = gtApplyValues(*e, values);
		return ae.rect.translated(lr.topLeft());
	}

	void showLayout()
	{
		layoutMode = true;
		loading = true;
		list->clear();
		if (title) {
			for (auto &L : title->layers) {
				auto *li = new QListWidgetItem(QString::fromUtf8("▣ ") + L.name, list);
				li->setData(Qt::UserRole, "L:" + L.name);
				for (auto &e : L.elements) {
					if (e.name.isEmpty())
						continue;
					const char *icon = e.kind == GtKind::Text ? "T  " : e.kind == GtKind::Image ? "🖼 " : "■  ";
					auto *it = new QListWidgetItem("      " + QString::fromUtf8(icon) + e.name, list);
					it->setData(Qt::UserRole, "E:" + e.name);
				}
			}
		}
		loading = false;
		stack->setCurrentIndex(3);
		resize(std::max(width(), 1320), std::max(height(), 720));
		refreshPreview();
		if (list->count() > 1)
			list->setCurrentRow(1);
	}

	void refreshPreview()
	{
		if (!title || !canvas)
			return;
		canvas->setImage(gtRenderStill(*title, values), title->width, title->height);
		std::vector<GtCanvas::Hit> hits;
		for (auto &L : title->layers) {
			if (!gtLayerVisible(L, values))
				continue;
			const QRectF lr = gtLayerRect(L, values);
			for (auto &e : L.elements) {
				const GtElement ae = gtApplyValues(e, values);
				if (!ae.visible || e.name.isEmpty())
					continue;
				hits.push_back({"E:" + e.name, ae.rect.translated(lr.topLeft()).intersected(lr)});
			}
		}
		canvas->setHits(hits);
		if (!selId.isEmpty())
			canvas->setSelection(selId, absRect(selId));
	}

	void selectItem(const QString &id, bool fromCanvas)
	{
		if (id.isEmpty() || loading)
			return;
		selId = id;
		if (fromCanvas) {
			loading = true;
			for (int i = 0; i < list->count(); i++)
				if (list->item(i)->data(Qt::UserRole).toString() == id) {
					list->setCurrentRow(i);
					break;
				}
			loading = false;
		}
		canvas->setSelection(id, absRect(id));
		buildForm();
	}

	/* arrasto na prévia */
	void moveItem(const QString &id, const QRectF &abs)
	{
		const QString name = id.mid(2);
		QString p;
		QPointF origin;
		if (id.startsWith("L:")) {
			p = "Layer:" + name;
		} else {
			const GtLayer *L = nullptr;
			const GtElement *e = nullptr;
			if (!elementByName(name, &L, &e))
				return;
			p = name;
			origin = gtLayerRect(*L, values).topLeft();
		}
		values.insert(p + ".X", QString::number(abs.x() - origin.x()));
		values.insert(p + ".Y", QString::number(abs.y() - origin.y()));
		values.insert(p + ".Width", QString::number(abs.width()));
		values.insert(p + ".Height", QString::number(abs.height()));
		if (liveBtn->isChecked())
			push();
		else {
			pending = true;
			applyBtn->setEnabled(true);
		}
		previewTimer->start(60);
		syncGeometryFields();
	}

	/* campos de posição que a prévia atualiza durante o arrasto */
	QDoubleSpinBox *fx = nullptr, *fy = nullptr, *fw = nullptr, *fh = nullptr;

	void syncGeometryFields()
	{
		if (!fx)
			return;
		const QString name = selId.mid(2);
		QRectF r;
		if (selId.startsWith("L:")) {
			if (const GtLayer *L = layerByName(name))
				r = gtLayerRect(*L, values);
		} else {
			const GtLayer *L = nullptr;
			const GtElement *e = nullptr;
			if (elementByName(name, &L, &e))
				r = gtApplyValues(*e, values).rect;
		}
		loading = true;
		fx->setValue(r.x());
		fy->setValue(r.y());
		fw->setValue(r.width());
		fh->setValue(r.height());
		loading = false;
	}

	void buildForm()
	{
		delete propForm;
		propForm = new QWidget(propHost);
		propHost->layout()->addWidget(propForm);
		auto *f = new QFormLayout(propForm);
		f->setLabelAlignment(Qt::AlignRight);
		fx = fy = fw = fh = nullptr;
		if (selId.isEmpty() || !title)
			return;

		const bool isLayer = selId.startsWith("L:");
		const QString name = selId.mid(2);
		const QString p = isLayer ? "Layer:" + name : name;
		const GtLayer *L = nullptr;
		const GtElement *orig = nullptr;
		GtElement e;
		QRectF r;
		bool visible = true;
		if (isLayer) {
			L = layerByName(name);
			if (!L)
				return;
			r = gtLayerRect(*L, values);
			visible = gtLayerVisible(*L, values);
		} else {
			if (!elementByName(name, &L, &orig))
				return;
			e = gtApplyValues(*orig, values);
			r = e.rect;
			visible = e.visible;
		}

		const QString headText =
			isLayer ? tl("<b>Camada:</b> %1<br><small>o que sair da camada não aparece</small>").arg(name.toHtmlEscaped())
				: tl("<b>Elemento:</b> %1<br><small>camada %2 — posição relativa à camada</small>")
					  .arg(name.toHtmlEscaped(), L->name.toHtmlEscaped());
		auto *head = new QLabel(headText, propForm);
		f->addRow(head);

		auto dbl = [&](double v, double min, double max, int dec = 1) {
			auto *sp = new QDoubleSpinBox(propForm);
			sp->setRange(min, max);
			sp->setDecimals(dec);
			sp->setValue(v);
			return sp;
		};
		auto onNum = [this](QDoubleSpinBox *sp, const QString &key) {
			connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key](double v) {
				if (loading)
					return;
				setValue(key, QString::number(v));
				canvas->setSelection(selId, absRect(selId));
			});
		};
		fx = dbl(r.x(), -10000, 10000);
		fy = dbl(r.y(), -10000, 10000);
		fw = dbl(r.width(), 1, 10000);
		fh = dbl(r.height(), 1, 10000);
		onNum(fx, p + ".X");
		onNum(fy, p + ".Y");
		onNum(fw, p + ".Width");
		onNum(fh, p + ".Height");
		auto *pos = new QHBoxLayout();
		pos->addWidget(new QLabel("X", propForm));
		pos->addWidget(fx);
		pos->addWidget(new QLabel("Y", propForm));
		pos->addWidget(fy);
		f->addRow(tl("Posição"), pos);
		auto *size = new QHBoxLayout();
		size->addWidget(new QLabel("L", propForm));
		size->addWidget(fw);
		size->addWidget(new QLabel("A", propForm));
		size->addWidget(fh);
		f->addRow(tl("Tamanho"), size);

		auto *rot = dbl(isLayer ? gtLayerRotation(*L, values) : e.rotation, -360, 360, 1);
		rot->setSuffix(QString::fromUtf8("°"));
		rot->setSingleStep(1);
		onNum(rot, p + ".Rotation");
		f->addRow(tl("Rotação"), rot);

		auto *vis = new QCheckBox(tl("Visível"), propForm);
		vis->setChecked(visible);
		connect(vis, &QCheckBox::toggled, this, [this, p](bool on) { setValue(p + ".Visible", on ? "True" : "False"); });
		f->addRow(QString(), vis);

		if (!isLayer) {
			auto *op = new QSpinBox(propForm);
			op->setRange(0, 100);
			op->setSuffix(" %");
			op->setValue((int)std::lround(e.opacity * 100));
			connect(op, QOverload<int>::of(&QSpinBox::valueChanged), this,
				[this, p](int v) { setValue(p + ".Opacity", QString::number(v / 100.0)); });
			f->addRow(tl("Opacidade"), op);
		}

		auto colorButton = [&](const QColor &c, const QString &key) {
			auto *b = new QPushButton(gtColorString(c), propForm);
			b->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); color: %5;")
						 .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
						 .arg(c.lightness() > 128 ? "black" : "white"));
			connect(b, &QPushButton::clicked, this, [this, b, key, c] {
				const QColor n = QColorDialog::getColor(gtColor(values.value(key, gtColorString(c))), this,
									tl("Cor"), QColorDialog::ShowAlphaChannel);
				if (!n.isValid())
					return;
				setValue(key, gtColorString(n));
				b->setText(gtColorString(n));
				b->setStyleSheet(QString("background-color: rgba(%1,%2,%3,%4); color: %5;")
							 .arg(n.red()).arg(n.green()).arg(n.blue()).arg(n.alpha())
							 .arg(n.lightness() > 128 ? "black" : "white"));
			});
			return b;
		};
		auto combo = [&](const QStringList &items, const QString &cur, const QString &key) {
			auto *c = new QComboBox(propForm);
			c->addItems(items);
			c->setCurrentIndex(std::max(0, (int)items.indexOf(cur)));
			connect(c, &QComboBox::currentTextChanged, this, [this, key](const QString &t) { setValue(key, t); });
			return c;
		};

		if (!isLayer && e.kind == GtKind::Text) {
			auto *font = new QFontComboBox(propForm);
			font->setCurrentFont(QFont(e.fontFamily));
			connect(font, &QFontComboBox::currentFontChanged, this,
				[this, p](const QFont &fnt) { setValue(p + ".FontFamily", fnt.family()); });
			f->addRow(tl("Fonte"), font);
			auto *fs = dbl(e.fontSize, 1, 1000, 1);
			onNum(fs, p + ".FontSize");
			f->addRow(tl("Tamanho da fonte"), fs);
			f->addRow(tl("Peso"),
				  combo({"Thin", "ExtraLight", "Light", "Regular", "Medium", "SemiBold", "Bold", "ExtraBold", "Black"},
					e.fontWeight, p + ".FontWeight"));
			auto *it = new QCheckBox(tl("Itálico"), propForm);
			it->setChecked(e.fontStyle.compare("Italic", Qt::CaseInsensitive) == 0);
			connect(it, &QCheckBox::toggled, this, [this, p](bool on) { setValue(p + ".FontStyle", on ? "Italic" : "Normal"); });
			f->addRow(QString(), it);
			f->addRow(tl("Alinhamento"), combo({"Left", "Center", "Right"}, e.align, p + ".TextAlign"));
			f->addRow(tl("Vertical"), combo({"Top", "Center", "Bottom"}, e.valign, p + ".VerticalAlign"));
			auto *ls = dbl(e.lineSpacing, 0.1, 10, 2);
			ls->setSingleStep(0.05);
			onNum(ls, p + ".LineSpacing");
			f->addRow(tl("Espaço entre linhas"), ls);
			f->addRow(tl("Cor do texto"), colorButton(e.fill.color, p + ".Fill.Color"));
		}
		if (!isLayer && (e.kind == GtKind::Rectangle || e.kind == GtKind::Ellipse)) {
			if (e.fill.kind == GtBrush::Bitmap)
				f->addRow(tl("Preenchimento"),
					  new QLabel(tl("imagem — troque na aba Imagens"), propForm));
			else
				f->addRow(tl("Cor"), colorButton(e.fill.color, p + ".Fill.Color"));
			if (e.kind == GtKind::Rectangle) {
				auto *rad = dbl(e.radiusX, 0, 1000, 1);
				onNum(rad, p + ".RadiusX");
				f->addRow(tl("Cantos arredondados"), rad);
			}
		}
		if (!isLayer && e.kind != GtKind::Image && e.kind != GtKind::Other) {
			auto *st = dbl(e.strokeThickness, 0, 200, 1);
			onNum(st, p + ".StrokeThickness");
			f->addRow(tl("Contorno (espessura)"), st);
			f->addRow(tl("Contorno (cor)"),
				  colorButton(e.stroke.kind == GtBrush::None ? QColor(Qt::black) : e.stroke.color, p + ".Stroke.Color"));
		}
		if (!isLayer && e.kind == GtKind::Image)
			f->addRow(tl("Ajuste da imagem"),
				  combo({"Uniform", "Fill", "UniformToFill", "None"}, e.imageStretch, p + ".Stretch"));

		auto *reset = new QPushButton(tl("Restaurar posição e estilo originais"), propForm);
		connect(reset, &QPushButton::clicked, this, [this, p] {
			static const QStringList keys = {".X", ".Y", ".Width", ".Height", ".Visible", ".Opacity", ".Rotation", ".FontFamily",
							 ".FontSize", ".FontWeight", ".FontStyle", ".TextAlign", ".VerticalAlign",
							 ".LineSpacing", ".StrokeThickness", ".Stroke.Color", ".RadiusX", ".Stretch"};
			for (const QString &k : keys)
				values.remove(p + k);
			push();
			refreshPreview();
			buildForm();
		});
		f->addRow(reset);
	}

	void loadPresets()
	{
		if (!title)
			return;
		QFile in(presetsFile(title->name));
		if (in.open(QIODevice::ReadOnly))
			presets = QJsonDocument::fromJson(in.readAll()).object();
		refreshPresets();
	}

	void savePresets()
	{
		if (title) {
			QFile out(presetsFile(title->name));
			if (out.open(QIODevice::WriteOnly))
				out.write(QJsonDocument(presets).toJson());
		}
		refreshPresets();
	}

	void refreshPresets()
	{
		presetList->clear();
		for (auto k = presets.begin(); k != presets.end(); ++k)
			presetList->addItem(k.key());
	}
};

void gtOpenEditor(obs_source_t *src, QWidget *parent)
{
	if (!gtIsSource(src) || !gtTitleOf(src)) {
		QMessageBox::information(parent, "ReplayDeck GT",
					 tl("Escolha primeiro o arquivo .gtzip nas propriedades da fonte."));
		return;
	}
	auto *dlg = new GtEditor(src, parent);
	dlg->show();
	dlg->raise();
}
