#include "ptz.hpp"
#include "engine.hpp"

#include <obs.hpp>
#include <util/base.h>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTcpSocket>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <QVBoxLayout>

#include <algorithm>

Ptz &Ptz::get()
{
	static Ptz p;
	return p;
}

static QString ptzFile()
{
	return QDir(Engine::get().configDir()).filePath("ptz.json");
}

void Ptz::load()
{
	cams.clear();
	OBSDataAutoRelease d = obs_data_create_from_json_file_safe(ptzFile().toUtf8().constData(), "bak");
	if (!d)
		return;
	OBSDataArrayAutoRelease arr = obs_data_get_array(d, "cameras");
	for (size_t i = 0, n = obs_data_array_count(arr); i < n; i++) {
		OBSDataAutoRelease o = obs_data_array_item(arr, i);
		cams << PtzCamera{QString::fromUtf8(obs_data_get_string(o, "nome")),
				  QString::fromUtf8(obs_data_get_string(o, "ip")), (int)obs_data_get_int(o, "porta"),
				  (int)obs_data_get_int(o, "protocolo")};
	}
	if (obs_data_has_user_value(d, "velocidade"))
		speed = (int)obs_data_get_int(d, "velocidade");
}

void Ptz::save()
{
	OBSDataAutoRelease d = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (const PtzCamera &c : cams) {
		OBSDataAutoRelease o = obs_data_create();
		obs_data_set_string(o, "nome", c.name.toUtf8().constData());
		obs_data_set_string(o, "ip", c.host.toUtf8().constData());
		obs_data_set_int(o, "porta", c.port);
		obs_data_set_int(o, "protocolo", c.protocol);
		obs_data_array_push_back(arr, o);
	}
	obs_data_set_array(d, "cameras", arr);
	obs_data_set_int(d, "velocidade", speed);
	obs_data_save_json_safe(d, ptzFile().toUtf8().constData(), "tmp", "bak");
}

int Ptz::find(const QString &spec) const
{
	const QString s = spec.trimmed();
	if (s.isEmpty())
		return current;
	bool num = false;
	const int n = s.toInt(&num);
	if (num)
		return n - 1;
	for (int i = 0; i < cams.size(); i++)
		if (cams[i].name.compare(s, Qt::CaseInsensitive) == 0)
			return i;
	return -1;
}

void Ptz::send(int cam, const QByteArray &visca)
{
	if (cam < 0 || cam >= cams.size())
		return;
	const PtzCamera &c = cams[cam];
	const QHostAddress addr(c.host);
	if (c.protocol == 1) {
		/* TCP: uma conexão por câmera, aberta na primeira vez */
		while (tcp.size() <= cam)
			tcp << nullptr;
		QTcpSocket *&s = tcp[cam];
		if (!s) {
			s = new QTcpSocket(this);
			s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
			connect(s, &QTcpSocket::readyRead, s, [s] { s->readAll(); });
		}
		if (s->state() == QAbstractSocket::UnconnectedState)
			s->connectToHost(c.host, (quint16)c.port);
		s->write(visca);
		return;
	}
	if (!udp) {
		udp = new QUdpSocket(this);
		connect(udp, &QUdpSocket::readyRead, udp, [this] {
			while (udp->hasPendingDatagrams())
				udp->receiveDatagram();
		});
	}
	if (c.protocol == 0) {
		/* VISCA sobre IP da Sony: cabeçalho tipo 0x0100, tamanho, número de sequência */
		QByteArray pkt;
		pkt.append(char(0x01)).append(char(0x00));
		pkt.append(char((visca.size() >> 8) & 0xFF)).append(char(visca.size() & 0xFF));
		seq++;
		for (int sh = 24; sh >= 0; sh -= 8)
			pkt.append(char((seq >> sh) & 0xFF));
		pkt.append(visca);
		udp->writeDatagram(pkt, addr, (quint16)c.port);
	} else {
		udp->writeDatagram(visca, addr, (quint16)c.port);
	}
}

static QByteArray bytes(std::initializer_list<int> b)
{
	QByteArray a;
	for (int x : b)
		a.append(char(x));
	return a;
}

void Ptz::move(int cam, int dx, int dy)
{
	const int ps = std::clamp(speed, 1, 24), ts = std::clamp(speed * 23 / 24, 1, 23);
	const int h = dx < 0 ? 1 : dx > 0 ? 2 : 3;
	const int v = dy > 0 ? 1 : dy < 0 ? 2 : 3;
	send(cam, bytes({0x81, 0x01, 0x06, 0x01, ps, ts, h, v, 0xFF}));
}

void Ptz::zoom(int cam, int dir)
{
	const int p = std::clamp(speed * 7 / 24, 1, 7);
	const int z = dir > 0 ? (0x20 | p) : dir < 0 ? (0x30 | p) : 0x00;
	send(cam, bytes({0x81, 0x01, 0x04, 0x07, z, 0xFF}));
}

void Ptz::home(int cam)
{
	send(cam, bytes({0x81, 0x01, 0x06, 0x04, 0xFF}));
}

void Ptz::preset(int cam, int n, bool store)
{
	send(cam, bytes({0x81, 0x01, 0x04, 0x3F, store ? 0x01 : 0x02, std::clamp(n - 1, 0, 127), 0xFF}));
	blog(LOG_INFO, "PTZ %s: preset %d %s", cam >= 0 && cam < cams.size() ? cams[cam].name.toUtf8().constData() : "?",
	     n, store ? "gravado" : "chamado");
}

void Ptz::autofocus(int cam)
{
	send(cam, bytes({0x81, 0x01, 0x04, 0x38, 0x02, 0xFF}));
}

/* ---------- painel ---------- */

PtzPanel::PtzPanel(QWidget *parent) : QWidget(parent)
{
	Ptz &p = Ptz::get();
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(8, 8, 8, 8);
	auto *top = new QHBoxLayout;
	camBox = new QComboBox;
	camBox->setMinimumWidth(200);
	auto *add = new QPushButton("＋ Câmera");
	auto *edit = new QPushButton("Editar");
	auto *del = new QPushButton("Remover");
	top->addWidget(new QLabel("Câmera:"));
	top->addWidget(camBox, 1);
	top->addWidget(add);
	top->addWidget(edit);
	top->addWidget(del);
	v->addLayout(top);

	auto *body = new QHBoxLayout;
	/* direcional: segure para mover */
	auto *pad = new QGridLayout;
	pad->setSpacing(4);
	const struct {
		int r, c, dx, dy;
		const char *t;
	} dirs[] = {{0, 0, -1, 1, "↖"}, {0, 1, 0, 1, "↑"},  {0, 2, 1, 1, "↗"},  {1, 0, -1, 0, "←"},
		    {1, 2, 1, 0, "→"},  {2, 0, -1, -1, "↙"}, {2, 1, 0, -1, "↓"}, {2, 2, 1, -1, "↘"}};
	for (const auto &d : dirs) {
		auto *b = new QPushButton(QString::fromUtf8(d.t));
		b->setFixedSize(52, 44);
		const int dx = d.dx, dy = d.dy;
		connect(b, &QPushButton::pressed, this, [dx, dy] { Ptz::get().move(Ptz::get().current, dx, dy); });
		connect(b, &QPushButton::released, this, [] { Ptz::get().stop(Ptz::get().current); });
		pad->addWidget(b, d.r, d.c);
	}
	auto *homeBtn = new QPushButton("⌂");
	homeBtn->setFixedSize(52, 44);
	homeBtn->setToolTip("Posição inicial (home)");
	connect(homeBtn, &QPushButton::clicked, this, [] { Ptz::get().home(Ptz::get().current); });
	pad->addWidget(homeBtn, 1, 1);
	body->addLayout(pad);

	auto *mid = new QVBoxLayout;
	auto *zin = new QPushButton("Zoom +");
	auto *zout = new QPushButton("Zoom −");
	connect(zin, &QPushButton::pressed, this, [] { Ptz::get().zoom(Ptz::get().current, 1); });
	connect(zout, &QPushButton::pressed, this, [] { Ptz::get().zoom(Ptz::get().current, -1); });
	for (QPushButton *b : {zin, zout})
		connect(b, &QPushButton::released, this, [] { Ptz::get().zoom(Ptz::get().current, 0); });
	auto *af = new QPushButton("Foco automático");
	connect(af, &QPushButton::clicked, this, [] { Ptz::get().autofocus(Ptz::get().current); });
	speed = new QSlider(Qt::Horizontal);
	speed->setRange(1, 24);
	speed->setValue(p.speed);
	connect(speed, &QSlider::valueChanged, this, [](int s) {
		Ptz::get().speed = s;
		Ptz::get().save();
	});
	mid->addWidget(zin);
	mid->addWidget(zout);
	mid->addWidget(af);
	mid->addWidget(new QLabel("Velocidade"));
	mid->addWidget(speed);
	mid->addStretch(1);
	body->addLayout(mid);

	/* posições gravadas */
	auto *pre = new QVBoxLayout;
	auto *grid = new QGridLayout;
	grid->setSpacing(4);
	for (int i = 0; i < 12; i++) {
		auto *b = new QPushButton(QString::number(i + 1));
		b->setFixedSize(52, 40);
		const int n = i + 1;
		connect(b, &QPushButton::clicked, this, [this, n] {
			const bool store = storeBtn->isChecked();
			Ptz::get().preset(Ptz::get().current, n, store);
			if (store)
				storeBtn->setChecked(false);
		});
		grid->addWidget(b, i / 4, i % 4);
	}
	pre->addWidget(new QLabel("Posições (presets)"));
	pre->addLayout(grid);
	storeBtn = new QPushButton("Gravar posição: clique aqui e depois no número");
	storeBtn->setCheckable(true);
	pre->addWidget(storeBtn);
	pre->addStretch(1);
	body->addLayout(pre, 1);
	v->addLayout(body, 1);

	auto *hint = new QLabel("VISCA sobre IP. Atalhos e API: PTZPreset (Value = número), PTZHome e as de segurar "
				"PTZHoldUp/Down/Left/Right/ZoomIn/ZoomOut (Input = câmera; vazio = a escolhida aqui).");
	hint->setObjectName("muted");
	hint->setWordWrap(true);
	v->addWidget(hint);

	connect(camBox, &QComboBox::currentIndexChanged, this, [](int i) { Ptz::get().current = std::max(0, i); });
	connect(add, &QPushButton::clicked, this, [this] {
		PtzCamera c;
		c.name = QString("PTZ %1").arg(Ptz::get().cams.size() + 1);
		if (editCamera(c)) {
			Ptz::get().cams << c;
			Ptz::get().save();
			refresh();
			camBox->setCurrentIndex(camBox->count() - 1);
		}
	});
	connect(edit, &QPushButton::clicked, this, [this] {
		Ptz &pz = Ptz::get();
		if (pz.current >= 0 && pz.current < pz.cams.size() && editCamera(pz.cams[pz.current])) {
			pz.save();
			const int keep = pz.current;
			refresh();
			camBox->setCurrentIndex(keep);
		}
	});
	connect(del, &QPushButton::clicked, this, [this] {
		Ptz &pz = Ptz::get();
		if (pz.current >= 0 && pz.current < pz.cams.size() &&
		    QMessageBox::question(this, "PTZ", QString("Remover \"%1\"?").arg(pz.cams[pz.current].name)) ==
			    QMessageBox::Yes) {
			pz.cams.removeAt(pz.current);
			pz.save();
			refresh();
		}
	});
	refresh();
}

void PtzPanel::refresh()
{
	const QSignalBlocker b(camBox);
	camBox->clear();
	for (const PtzCamera &c : Ptz::get().cams)
		camBox->addItem(QString("%1  (%2)").arg(c.name, c.host));
	if (camBox->count() == 0)
		camBox->addItem("nenhuma — clique em ＋ Câmera");
	Ptz::get().current = std::clamp(Ptz::get().current, 0, std::max(0, (int)Ptz::get().cams.size() - 1));
	camBox->setCurrentIndex(Ptz::get().current);
}

bool PtzPanel::editCamera(PtzCamera &c)
{
	QDialog dlg(this);
	dlg.setWindowTitle("Câmera PTZ");
	auto *f = new QFormLayout(&dlg);
	auto *name = new QLineEdit(c.name);
	auto *host = new QLineEdit(c.host);
	host->setPlaceholderText("192.168.0.100");
	auto *proto = new QComboBox;
	proto->addItem("VISCA sobre IP (Sony, UDP 52381)", 0);
	proto->addItem("VISCA TCP (PTZOptics e outras, 5678)", 1);
	proto->addItem("VISCA UDP sem cabeçalho (1259)", 2);
	proto->setCurrentIndex(std::clamp(c.protocol, 0, 2));
	auto *port = new QSpinBox;
	port->setRange(1, 65535);
	port->setValue(c.port);
	connect(proto, &QComboBox::currentIndexChanged, port, [port](int i) {
		port->setValue(i == 0 ? 52381 : i == 1 ? 5678 : 1259);
	});
	f->addRow("Nome", name);
	f->addRow("IP", host);
	f->addRow("Protocolo", proto);
	f->addRow("Porta", port);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	f->addRow(bb);
	if (dlg.exec() != QDialog::Accepted || host->text().trimmed().isEmpty())
		return false;
	c.name = name->text().trimmed().isEmpty() ? host->text().trimmed() : name->text().trimmed();
	c.host = host->text().trimmed();
	c.protocol = proto->currentData().toInt();
	c.port = port->value();
	return true;
}
