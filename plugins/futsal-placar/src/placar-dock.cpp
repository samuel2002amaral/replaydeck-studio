#include "placar-dock.hpp"
#include "state.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

static QString colorCss(uint32_t c)
{
	return QString("#%1").arg(c & 0xFFFFFF, 6, 16, QChar('0'));
}

static QPushButton *btn(const QString &text, const QString &tip = QString())
{
	auto *b = new QPushButton(text);
	b->setMinimumHeight(30);
	if (!tip.isEmpty())
		b->setToolTip(tip);
	return b;
}

QWidget *PlacarDock::buildTeam(int t)
{
	auto *box = new QGroupBox(t == 0 ? QStringLiteral("Time da esquerda (casa)") : QStringLiteral("Time da direita (visitante)"));
	auto *g = new QGridLayout(box);

	name[t] = new QLineEdit;
	name[t]->setPlaceholderText(QStringLiteral("Nome do time"));
	sigla[t] = new QLineEdit;
	sigla[t]->setPlaceholderText(QStringLiteral("Sigla"));
	sigla[t]->setMaxLength(4);
	sigla[t]->setMaximumWidth(60);
	bgBtn[t] = btn(QStringLiteral("Cor"), QStringLiteral("Cor do fundo do time"));
	fgBtn[t] = btn(QStringLiteral("Letra"), QStringLiteral("Cor da letra do time"));
	connect(name[t], &QLineEdit::editingFinished, this, &PlacarDock::applyNames);
	connect(sigla[t], &QLineEdit::editingFinished, this, &PlacarDock::applyNames);
	connect(bgBtn[t], &QPushButton::clicked, this, [this, t] { pickColor(t, true); });
	connect(fgBtn[t], &QPushButton::clicked, this, [this, t] { pickColor(t, false); });

	auto *row = new QHBoxLayout;
	row->addWidget(name[t], 1);
	row->addWidget(sigla[t]);
	row->addWidget(bgBtn[t]);
	row->addWidget(fgBtn[t]);
	g->addLayout(row, 0, 0, 1, 4);

	score[t] = new QLabel("0");
	score[t]->setAlignment(Qt::AlignCenter);
	score[t]->setStyleSheet("font-size: 34px; font-weight: bold;");
	auto *goal = btn(QStringLiteral("GOL +1"));
	goal->setStyleSheet("font-weight: bold;");
	goal->setMinimumHeight(44);
	auto *minus = btn(QStringLiteral("−1"), QStringLiteral("Tirar um gol"));
	connect(goal, &QPushButton::clicked, this, [t] { Placar::get().addScore(t, 1); });
	connect(minus, &QPushButton::clicked, this, [t] { Placar::get().addScore(t, -1); });
	g->addWidget(score[t], 1, 0, 2, 1);
	g->addWidget(goal, 1, 1, 1, 2);
	g->addWidget(minus, 1, 3);

	fouls[t] = new QLabel;
	auto *foul = btn(QStringLiteral("Falta +1"));
	auto *foulMinus = btn(QStringLiteral("−1"), QStringLiteral("Tirar uma falta"));
	connect(foul, &QPushButton::clicked, this, [t] { Placar::get().addFoul(t, 1); });
	connect(foulMinus, &QPushButton::clicked, this, [t] { Placar::get().addFoul(t, -1); });
	g->addWidget(fouls[t], 2, 1);
	g->addWidget(foul, 2, 2);
	g->addWidget(foulMinus, 2, 3);

	timeouts[t] = new QLabel;
	auto *to = btn(QStringLiteral("Pedido de tempo"), QStringLiteral("Para o relógio e mostra 1 minuto de pedido de tempo"));
	connect(to, &QPushButton::clicked, this, [t] { Placar::get().startTimeout(t); });
	g->addWidget(timeouts[t], 3, 0, 1, 2);
	g->addWidget(to, 3, 2, 1, 2);

	penalties[t] = new QLabel;
	auto *pen = btn(QStringLiteral("Pênalti +1"));
	auto *penMinus = btn(QStringLiteral("−1"));
	connect(pen, &QPushButton::clicked, this, [t] { Placar::get().addPenalty(t, 1); });
	connect(penMinus, &QPushButton::clicked, this, [t] { Placar::get().addPenalty(t, -1); });
	g->addWidget(penalties[t], 4, 0, 1, 2);
	g->addWidget(pen, 4, 2);
	g->addWidget(penMinus, 4, 3);
	return box;
}

PlacarDock::PlacarDock(QWidget *parent) : QWidget(parent)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	outer->addWidget(scroll);
	auto *content = new QWidget;
	scroll->setWidget(content);
	auto *v = new QVBoxLayout(content);

	/* relógio */
	auto *clockBox = new QGroupBox(QStringLiteral("Relógio"));
	auto *cg = new QGridLayout(clockBox);
	period = new QComboBox;
	for (int p = 0; p < P_COUNT; p++)
		period->addItem(QString::fromStdString(Placar::periodName(p, false)));
	connect(period, &QComboBox::currentIndexChanged, this, [](int i) { Placar::get().setPeriod(i); });
	clock = new QLabel("20:00");
	clock->setAlignment(Qt::AlignCenter);
	clock->setStyleSheet("font-size: 40px; font-weight: bold; font-family: monospace;");
	status = new QLabel;
	status->setAlignment(Qt::AlignCenter);
	startStop = btn(QStringLiteral("▶ Iniciar"));
	startStop->setMinimumHeight(44);
	startStop->setStyleSheet("font-weight: bold;");
	connect(startStop, &QPushButton::clicked, this, [] { Placar::get().toggleClock(); });
	auto *minus1 = btn(QStringLiteral("−1 s"));
	auto *plus1 = btn(QStringLiteral("+1 s"));
	connect(minus1, &QPushButton::clicked, this, [] { Placar::get().adjustClock(-1000); });
	connect(plus1, &QPushButton::clicked, this, [] { Placar::get().adjustClock(1000); });
	setTime = new QLineEdit;
	setTime->setPlaceholderText(QStringLiteral("mm:ss"));
	setTime->setMaximumWidth(80);
	auto *apply = btn(QStringLiteral("Ajustar"), QStringLiteral("Coloca este tempo no relógio"));
	auto applyTime = [this] {
		static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,2})(?::(\\d{1,2}))?\\s*$"));
		const auto m = re.match(setTime->text());
		if (!m.hasMatch())
			return;
		const int a = m.captured(1).toInt();
		const int b = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
		Placar::get().setClockDisplay(((int64_t)a * 60 + b) * 1000);
		setTime->clear();
	};
	connect(apply, &QPushButton::clicked, this, applyTime);
	connect(setTime, &QLineEdit::returnPressed, this, applyTime);
	auto *reset = btn(QStringLiteral("Zerar período"));
	connect(reset, &QPushButton::clicked, this, [] { Placar::get().resetPeriodClock(); });
	auto *next = btn(QStringLiteral("Próximo período ▸"));
	connect(next, &QPushButton::clicked, this, [] { Placar::get().nextPeriod(); });
	auto *cancelTo = btn(QStringLiteral("Encerrar pedido de tempo"));
	connect(cancelTo, &QPushButton::clicked, this, [] { Placar::get().cancelTimeout(); });

	cg->addWidget(period, 0, 0, 1, 2);
	cg->addWidget(status, 0, 2, 1, 2);
	cg->addWidget(clock, 1, 0, 1, 4);
	cg->addWidget(startStop, 2, 0, 1, 4);
	cg->addWidget(minus1, 3, 0);
	cg->addWidget(plus1, 3, 1);
	cg->addWidget(setTime, 3, 2);
	cg->addWidget(apply, 3, 3);
	cg->addWidget(reset, 4, 0, 1, 2);
	cg->addWidget(next, 4, 2, 1, 2);
	cg->addWidget(cancelTo, 5, 0, 1, 4);
	v->addWidget(clockBox);

	v->addWidget(buildTeam(0));
	v->addWidget(buildTeam(1));

	/* jogo e configuração */
	auto *cfg = new QGroupBox(QStringLiteral("Jogo"));
	auto *fg = new QGridLayout(cfg);
	competition = new QLineEdit;
	competition->setPlaceholderText(QStringLiteral("Competição / rodada (aparece em cima do placar)"));
	connect(competition, &QLineEdit::editingFinished, this,
		[this] { Placar::get().setCompetition(competition->text().toStdString()); });
	periodMin = new QSpinBox;
	periodMin->setRange(1, 99);
	periodMin->setSuffix(QStringLiteral(" min"));
	extraMin = new QSpinBox;
	extraMin->setRange(1, 99);
	extraMin->setSuffix(QStringLiteral(" min"));
	countDown = new QCheckBox(QStringLiteral("Contagem regressiva"));
	auto applyCfg = [this] { Placar::get().setConfig(periodMin->value(), extraMin->value(), countDown->isChecked()); };
	connect(periodMin, &QSpinBox::valueChanged, this, applyCfg);
	connect(extraMin, &QSpinBox::valueChanged, this, applyCfg);
	connect(countDown, &QCheckBox::toggled, this, applyCfg);
	auto *swap = btn(QStringLiteral("⇄ Inverter times"));
	connect(swap, &QPushButton::clicked, this, [] { Placar::get().swapTeams(); });
	auto *newGame = btn(QStringLiteral("Novo jogo"));
	connect(newGame, &QPushButton::clicked, this, [this] {
		if (QMessageBox::question(this, QStringLiteral("Novo jogo"),
					  QStringLiteral("Zerar placar, faltas, pedidos de tempo e relógio?")) == QMessageBox::Yes)
			Placar::get().newGame();
	});

	fg->addWidget(competition, 0, 0, 1, 4);
	fg->addWidget(new QLabel(QStringLiteral("Tempo:")), 1, 0);
	fg->addWidget(periodMin, 1, 1);
	fg->addWidget(new QLabel(QStringLiteral("Prorrogação:")), 1, 2);
	fg->addWidget(extraMin, 1, 3);
	fg->addWidget(countDown, 2, 0, 1, 4);
	fg->addWidget(swap, 3, 0, 1, 2);
	fg->addWidget(newGame, 3, 2, 1, 2);
	v->addWidget(cfg);

	auto *hint = new QLabel(QStringLiteral(
		"Para pôr na tela: adicione a fonte \"Placar de Futsal\" na cena. "
		"Atalhos em Configurações → Teclas de atalho (procure por \"Placar\")."));
	hint->setWordWrap(true);
	hint->setStyleSheet("color: gray;");
	v->addWidget(hint);
	v->addStretch(1);

	timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &PlacarDock::refresh);
	timer->start(100);
	refresh();
}

void PlacarDock::applyNames()
{
	for (int t = 0; t < 2; t++)
		Placar::get().setTeam(t, name[t]->text().toStdString(), sigla[t]->text().toStdString());
}

void PlacarDock::pickColor(int t, bool background)
{
	const Snapshot st = Placar::get().snapshot();
	const uint32_t cur = background ? st.team[t].bg : st.team[t].fg;
	const QColor c = QColorDialog::getColor(QColor(colorCss(cur)), this, QStringLiteral("Escolher cor"));
	if (!c.isValid())
		return;
	const uint32_t v = 0xFF000000u | ((uint32_t)c.red() << 16) | ((uint32_t)c.green() << 8) | (uint32_t)c.blue();
	Placar::get().setColors(t, background ? v : st.team[t].bg, background ? st.team[t].fg : v);
}

void PlacarDock::refresh()
{
	Placar::get().tick();
	const Snapshot st = Placar::get().snapshot();

	/* relógio: sempre (muda sem mudar a versão) */
	QString shown;
	if (st.timeoutTeam >= 0)
		shown = QString::fromStdString(Placar::formatClock(st.timeoutLeftMs, true));
	else if (st.period == P_PENALTIES)
		shown = QStringLiteral("—");
	else
		shown = QString::fromStdString(
			Placar::formatClock(st.countDown ? st.lengthMs - st.elapsedMs : st.elapsedMs, st.countDown));
	clock->setText(shown);
	startStop->setText(st.running ? QStringLiteral("❚❚ Pausar") : QStringLiteral("▶ Iniciar"));
	if (st.timeoutTeam >= 0)
		status->setText(QStringLiteral("Pedido de tempo: %1").arg(QString::fromStdString(st.team[st.timeoutTeam].sigla)));
	else if (st.lengthMs > 0 && st.elapsedMs >= st.lengthMs)
		status->setText(QStringLiteral("Fim do período"));
	else
		status->setText(st.running ? QStringLiteral("Rolando") : QStringLiteral("Parado"));

	if (st.version == lastVersion)
		return;
	lastVersion = st.version;

	for (int t = 0; t < 2; t++) {
		const Team &tm = st.team[t];
		if (!name[t]->hasFocus())
			name[t]->setText(QString::fromStdString(tm.name));
		if (!sigla[t]->hasFocus())
			sigla[t]->setText(QString::fromStdString(tm.sigla));
		bgBtn[t]->setStyleSheet(QString("background:%1; color:%2;").arg(colorCss(tm.bg), colorCss(tm.fg)));
		fgBtn[t]->setStyleSheet(QString("background:%1; color:%2;").arg(colorCss(tm.bg), colorCss(tm.fg)));
		score[t]->setText(QString::number(tm.score));
		fouls[t]->setText(QStringLiteral("Faltas: %1%2")
					  .arg(tm.fouls)
					  .arg(tm.fouls >= 5 ? QStringLiteral(" ⚠ próxima: tiro livre direto") : QString()));
		fouls[t]->setStyleSheet(tm.fouls >= 5 ? "color: #e5443a; font-weight: bold;" : "");
		timeouts[t]->setText(QStringLiteral("Pedidos de tempo no período: %1").arg(tm.timeouts));
		penalties[t]->setText(QStringLiteral("Pênaltis: %1").arg(tm.penalties));
	}
	{
		QSignalBlocker b1(period), b2(periodMin), b3(extraMin), b4(countDown);
		period->setCurrentIndex(st.period);
		periodMin->setValue(st.periodMinutes);
		extraMin->setValue(st.extraMinutes);
		countDown->setChecked(st.countDown);
	}
	if (!competition->hasFocus())
		competition->setText(QString::fromStdString(st.competition));
}
