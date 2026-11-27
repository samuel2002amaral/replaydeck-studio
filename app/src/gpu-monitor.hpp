/* Leitura da placa NVIDIA pelo driver (nvml.dll): uso do desenho (3D), do codificador,
 * temperatura e se ela está reduzindo a velocidade por calor ou energia. Só Windows/NVIDIA;
 * nos outros casos ok = false e o rodapé não mostra nada. */
#pragma once

struct GpuStatus {
	bool ok = false;
	int gpu = 0;      /* % de uso do desenho */
	int encoder = 0;  /* % de uso do codificador (NVENC) */
	int tempC = 0;
	bool hot = false;   /* reduzindo por calor */
	bool power = false; /* reduzindo por limite de energia */
};

namespace gpumon {
GpuStatus read(); /* rápido (milissegundos); chamar 1x por segundo */
}
