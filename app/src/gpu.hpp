/* Escolha da placa de vídeo (Windows). Em notebook com duas placas (Intel + NVIDIA/AMD),
 * o Windows entrega a Intel primeiro: o programa todo rodava nela e o codificador NVIDIA
 * "direto da placa" não funcionava (copiava os quadros para a memória — CPU e disco altos).
 * Aqui a dedicada é escolhida sozinha. */
#pragma once

#include <QString>
#include <QStringList>

namespace gpu {

struct Adapter {
	int index = 0;
	QString name;
	unsigned vendor = 0;
	unsigned long long dedicatedMB = 0;
	bool software = false;
};

QList<Adapter> adapters();
/* wanted: "" ou "auto" = automático; senão o nome da placa (o índice muda quando liga/desliga monitor) */
int pick(const QString &wanted, QString *chosenName = nullptr);
QString vendorName(unsigned vendor);

} // namespace gpu
