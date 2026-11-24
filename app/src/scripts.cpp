#include "scripts.hpp"
#include "engine.hpp"
#include "functions.hpp"

#include <obs.hpp>
#include <util/base.h>

#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

/* ---------- compilar ---------- */

/* API.Function("Cut", Input:="1", Value:="2")  ->  Cut Input="1" Value="2" */
static bool vbCall(const QString &l, QString *out)
{
	static const QRegularExpression call(R"re(^API\.Function\s*\(\s*"(\w+)"\s*(?:,(.*))?\)\s*$)re",
					     QRegularExpression::CaseInsensitiveOption);
	const auto m = call.match(l);
	if (!m.hasMatch())
		return false;
	QString line = m.captured(1);
	static const QRegularExpression arg(R"re((\w+)\s*:=\s*("(?:[^"]|"")*"|[^,\s)]+))re");
	auto it = arg.globalMatch(m.captured(2));
	while (it.hasNext()) {
		const auto a = it.next();
		QString v = a.captured(2);
		if (v.startsWith('"'))
			v = v.mid(1, v.size() - 2).replace("\"\"", "\"");
		line += QString(" %1=\"%2\"").arg(a.captured(1), v);
	}
	*out = line;
	return true;
}

QList<ScriptOp> compileScript(const QString &code, QString *error)
{
	QList<ScriptOp> ops;
	QList<int> stack; /* índices de LoopStart abertos */
	const QStringList lines = code.split('\n');
	auto fail = [&](int ln, const QString &msg) {
		if (error)
			*error = QString("linha %1: %2").arg(ln).arg(msg);
		return QList<ScriptOp>();
	};
	static const QRegularExpression sleepRe(R"re(^(?:Sleep|Esperar|Aguardar)\s*\(?\s*(\d+)\s*\)?\s*$)re",
						QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression forRe(R"re(^For\s+\w+(?:\s+As\s+\w+)?\s*=\s*(\d+)\s+To\s+(\d+)\s*$)re",
					      QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression repRe(R"re(^Repetir(?:\s+(\d+)(?:\s+vezes)?)?\s*$)re",
					      QRegularExpression::CaseInsensitiveOption);
	for (int i = 0; i < lines.size(); i++) {
		const int ln = i + 1;
		QString l = lines[i].trimmed();
		if (l.isEmpty() || l.startsWith('\'') || l.startsWith('#') || l.startsWith("//") ||
		    l.startsWith("Rem ", Qt::CaseInsensitive))
			continue;
		ScriptOp op;
		op.line = ln;
		QRegularExpressionMatch m;
		if ((m = sleepRe.match(l)).hasMatch()) {
			op.kind = ScriptOp::Sleep;
			op.ms = m.captured(1).toInt();
		} else if (l.compare("Do", Qt::CaseInsensitive) == 0 || l.compare("While True", Qt::CaseInsensitive) == 0) {
			op.kind = ScriptOp::LoopStart;
		} else if ((m = repRe.match(l)).hasMatch()) {
			op.kind = ScriptOp::LoopStart;
			op.count = m.captured(1).isEmpty() ? -1 : m.captured(1).toInt();
		} else if ((m = forRe.match(l)).hasMatch()) {
			op.kind = ScriptOp::LoopStart;
			op.count = std::max(0, m.captured(2).toInt() - m.captured(1).toInt() + 1);
		} else if (l.compare("Loop", Qt::CaseInsensitive) == 0 || l.compare("Fim", Qt::CaseInsensitive) == 0 ||
			   l.compare("End While", Qt::CaseInsensitive) == 0 || l.compare("Wend", Qt::CaseInsensitive) == 0 ||
			   l.startsWith("Next", Qt::CaseInsensitive)) {
			if (stack.isEmpty())
				return fail(ln, "\"" + l + "\" sem o começo da repetição");
			op.kind = ScriptOp::LoopEnd;
			op.jump = stack.takeLast();
			ops[op.jump].jump = ops.size() + 1;
		} else {
			QString call;
			if (l.startsWith("API.", Qt::CaseInsensitive)) {
				if (!vbCall(l, &call))
					return fail(ln, "não entendi: " + l);
			} else {
				call = l;
			}
			const QString fn = call.section(' ', 0, 0).section('&', 0, 0).remove("Function=");
			if (!findFunction(fn))
				return fail(ln, "função desconhecida: " + fn);
			op.text = call;
		}
		if (op.kind == ScriptOp::LoopStart)
			stack << ops.size();
		ops << op;
	}
	if (!stack.isEmpty())
		return fail(ops[stack.last()].line, "repetição sem \"Loop\"/\"Fim\"");
	return ops;
}

/* ---------- rodar ---------- */

class ScriptRun : public QObject {
public:
	ScriptRun(const QString &n, QList<ScriptOp> o) : QObject(&Scripts::get()), name(n), ops(std::move(o))
	{
		timer.setSingleShot(true);
		timer.setTimerType(Qt::PreciseTimer);
		connect(&timer, &QTimer::timeout, this, &ScriptRun::step);
	}
	void begin() { timer.start(0); }
	void halt() { timer.stop(); }

private:
	void step()
	{
		/* executa até a próxima espera; uma repetição sem espera devolve a vez à interface */
		int budget = 2000;
		while (pc < ops.size()) {
			ScriptOp &op = ops[pc];
			switch (op.kind) {
			case ScriptOp::Call: {
				QString err;
				if (!runFunctionLine(op.text, &err)) {
					Scripts::get().finished(name, QString("linha %1: %2").arg(op.line).arg(err));
					return;
				}
				pc++;
				break;
			}
			case ScriptOp::Sleep:
				pc++;
				timer.start(std::max(1, op.ms));
				return;
			case ScriptOp::LoopStart:
				counters[pc] = op.count;
				if (op.count == 0) {
					pc = op.jump;
					break;
				}
				pc++;
				break;
			case ScriptOp::LoopEnd: {
				const int start = op.jump;
				int &left = counters[start];
				if (left > 0)
					left--;
				if (left == 0) {
					pc++;
				} else {
					pc = start + 1;
					if (--budget <= 0) {
						timer.start(1);
						return;
					}
				}
				break;
			}
			}
		}
		Scripts::get().finished(name, QString());
	}

	QString name;
	QList<ScriptOp> ops;
	QMap<int, int> counters;
	int pc = 0;
	QTimer timer;
};

Scripts &Scripts::get()
{
	static Scripts s;
	return s;
}

static QString scriptsFile()
{
	return QDir(Engine::get().configDir()).filePath("scripts.json");
}

void Scripts::load()
{
	order.clear();
	codes.clear();
	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(scriptsFile().toUtf8().constData(), "bak");
	OBSDataArrayAutoRelease arr = root ? obs_data_get_array(root, "scripts") : nullptr;
	for (size_t i = 0, n = obs_data_array_count(arr); i < n; i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		const QString name = QString::fromUtf8(obs_data_get_string(o, "nome"));
		if (name.isEmpty() || codes.contains(name))
			continue;
		order << name;
		codes.insert(name, QString::fromUtf8(obs_data_get_string(o, "codigo")));
	}
	/* as funções ScriptStart/ScriptStop/ScriptStopAll chegam aqui */
	scriptHook = [](const QString &fn, const QString &name) -> QString {
		if (fn == "ScriptStopAll") {
			Scripts::get().stopAll();
			return QString();
		}
		if (!Scripts::get().codes.contains(name))
			return QString("script \"%1\" não existe").arg(name);
		if (fn == "ScriptStop") {
			Scripts::get().stop(name);
			return QString();
		}
		return Scripts::get().start(name);
	};
}

void Scripts::save()
{
	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const QString &n : order) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_string(o, "nome", n.toUtf8().constData());
		obs_data_set_string(o, "codigo", codes.value(n).toUtf8().constData());
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(root, "scripts", arr);
	obs_data_save_json_safe(root, scriptsFile().toUtf8().constData(), "tmp", "bak");
}

void Scripts::setCode(const QString &name, const QString &code)
{
	if (!codes.contains(name) || codes.value(name) == code)
		return;
	codes[name] = code;
	save();
}

bool Scripts::add(const QString &name)
{
	if (name.trimmed().isEmpty() || codes.contains(name.trimmed()))
		return false;
	order << name.trimmed();
	codes.insert(name.trimmed(), QString());
	save();
	emit changed();
	return true;
}

void Scripts::remove(const QString &name)
{
	stop(name);
	order.removeAll(name);
	codes.remove(name);
	errors.remove(name);
	save();
	emit changed();
}

bool Scripts::rename(const QString &from, const QString &to)
{
	const QString t = to.trimmed();
	if (t.isEmpty() || !codes.contains(from) || (codes.contains(t) && t != from))
		return false;
	stop(from);
	order[order.indexOf(from)] = t;
	codes.insert(t, codes.take(from));
	errors.remove(from);
	save();
	emit changed();
	return true;
}

QString Scripts::start(const QString &name)
{
	if (!codes.contains(name))
		return QString("script \"%1\" não existe").arg(name);
	QString err;
	QList<ScriptOp> ops = compileScript(codes.value(name), &err);
	if (!err.isEmpty()) {
		errors[name] = err;
		emit changed();
		return err;
	}
	stop(name); /* rodar de novo = recomeça */
	errors.remove(name);
	auto *r = new ScriptRun(name, std::move(ops));
	runs.insert(name, r);
	blog(LOG_INFO, "[script] '%s' começou", name.toUtf8().constData());
	r->begin();
	emit changed();
	return QString();
}

void Scripts::stop(const QString &name)
{
	ScriptRun *r = runs.take(name);
	if (!r)
		return;
	r->halt();
	r->deleteLater();
	blog(LOG_INFO, "[script] '%s' parado", name.toUtf8().constData());
	emit changed();
}

void Scripts::stopAll()
{
	for (const QString &n : runs.keys())
		stop(n);
}

void Scripts::finished(const QString &name, const QString &error)
{
	ScriptRun *r = runs.take(name);
	if (r) {
		r->halt();
		r->deleteLater();
	}
	if (!error.isEmpty()) {
		errors[name] = error;
		blog(LOG_WARNING, "[script] '%s' parou com erro: %s", name.toUtf8().constData(), error.toUtf8().constData());
	} else {
		blog(LOG_INFO, "[script] '%s' terminou", name.toUtf8().constData());
	}
	emit changed();
}

/* ---------- janela ---------- */

static const char *kHelp =
	"' Exemplo: placar entra, fica 10 s e sai\n"
	"OverlayInput1In Input=Placar\n"
	"Sleep(10000)\n"
	"OverlayInput1Out\n"
	"\n"
	"' Também na forma do vMix:\n"
	"' API.Function(\"Fade\", Input:=\"CAM 2\", Duration:=\"800\")\n"
	"' Repetições: Do ... Loop (até parar) · For i = 1 To 3 ... Next · Repetir 3 ... Fim\n";

ScriptsDialog::ScriptsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Scripts");
	resize(900, 560);
	auto *root = new QHBoxLayout(this);
	auto *split = new QSplitter;
	root->addWidget(split);

	auto *left = new QWidget;
	auto *lv = new QVBoxLayout(left);
	lv->setContentsMargins(0, 0, 0, 0);
	list = new QListWidget;
	lv->addWidget(list, 1);
	auto *lb = new QHBoxLayout;
	auto *add = new QPushButton("＋ Novo");
	auto *ren = new QPushButton("Renomear");
	auto *del = new QPushButton("Remover");
	lb->addWidget(add);
	lb->addWidget(ren);
	lb->addWidget(del);
	lv->addLayout(lb);
	split->addWidget(left);

	auto *right = new QWidget;
	auto *rv = new QVBoxLayout(right);
	rv->setContentsMargins(0, 0, 0, 0);
	editor = new QPlainTextEdit;
	QFont mono("Consolas");
	mono.setStyleHint(QFont::Monospace);
	mono.setPointSize(11);
	editor->setFont(mono);
	editor->setPlaceholderText(QString::fromUtf8(kHelp));
	rv->addWidget(editor, 1);
	auto *rb = new QHBoxLayout;
	auto *run = new QPushButton("▶ Rodar");
	run->setObjectName("greenButton");
	auto *stop = new QPushButton("■ Parar");
	auto *stopAll = new QPushButton("Parar todos");
	state = new QLabel;
	state->setObjectName("muted");
	state->setWordWrap(true);
	rb->addWidget(run);
	rb->addWidget(stop);
	rb->addWidget(stopAll);
	rb->addWidget(state, 1);
	rv->addLayout(rb);
	auto *hint = new QLabel("Atalho: função ScriptStart com Value = nome do script (Configurações → Atalhos). "
				"Também pela API do vMix: /api/?Function=ScriptStart&Value=nome");
	hint->setObjectName("muted");
	hint->setWordWrap(true);
	rv->addWidget(hint);
	split->addWidget(right);
	split->setStretchFactor(1, 1);
	split->setSizes({220, 680});

	Scripts &s = Scripts::get();
	connect(add, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Novo script", "Nome:", QLineEdit::Normal, QString(), &ok);
		if (ok && !Scripts::get().add(n))
			QMessageBox::warning(this, "Scripts", "Nome vazio ou já existe.");
		else if (ok)
			select(n.trimmed());
	});
	connect(ren, &QPushButton::clicked, this, [this] {
		if (current.isEmpty())
			return;
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Renomear script", "Nome:", QLineEdit::Normal, current, &ok);
		if (ok && n != current) {
			if (Scripts::get().rename(current, n))
				select(n.trimmed());
			else
				QMessageBox::warning(this, "Scripts", "Nome vazio ou já existe.");
		}
	});
	connect(del, &QPushButton::clicked, this, [this] {
		if (!current.isEmpty() &&
		    QMessageBox::question(this, "Scripts", QString("Remover o script \"%1\"?").arg(current)) == QMessageBox::Yes) {
			Scripts::get().remove(current);
			current.clear();
			refreshList();
		}
	});
	connect(run, &QPushButton::clicked, this, [this] {
		if (current.isEmpty())
			return;
		Scripts::get().setCode(current, editor->toPlainText());
		Scripts::get().start(current);
	});
	connect(stop, &QPushButton::clicked, this, [this] { Scripts::get().stop(current); });
	connect(stopAll, &QPushButton::clicked, this, [] { Scripts::get().stopAll(); });
	connect(list, &QListWidget::currentTextChanged, this, [this](const QString &t) {
		if (!t.isEmpty() && t != current)
			select(t.section("  ", 0, 0));
	});
	connect(editor, &QPlainTextEdit::textChanged, this, [this] {
		if (!loading && !current.isEmpty())
			Scripts::get().setCode(current, editor->toPlainText());
	});
	connect(&s, &Scripts::changed, this, [this] {
		refreshList();
		updateState();
	});
	refreshList();
	if (!s.names().isEmpty())
		select(s.names().first());
	else
		updateState();
}

void ScriptsDialog::refreshList()
{
	const QSignalBlocker b(list);
	list->clear();
	for (const QString &n : Scripts::get().names()) {
		auto *it = new QListWidgetItem(n + (Scripts::get().running(n) ? QString::fromUtf8("  ● rodando") : QString()));
		list->addItem(it);
		if (n == current)
			list->setCurrentItem(it);
	}
}

void ScriptsDialog::select(const QString &name)
{
	current = name;
	loading = true;
	editor->setPlainText(Scripts::get().code(name));
	loading = false;
	refreshList();
	updateState();
}

void ScriptsDialog::updateState()
{
	editor->setEnabled(!current.isEmpty());
	if (current.isEmpty()) {
		state->setText("Crie um script com ＋ Novo.");
		return;
	}
	const QString err = Scripts::get().lastError(current);
	if (Scripts::get().running(current))
		state->setText(QString::fromUtf8("● rodando"));
	else if (!err.isEmpty())
		state->setText("Erro — " + err);
	else
		state->setText("Parado");
}
