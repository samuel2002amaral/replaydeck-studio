/* Transição Merge (como a do vMix) e entrada Programa (PGM) para usar como camada. */
#pragma once

#define MERGE_TRANSITION_ID "rds_merge_transition"
#define PROGRAM_INPUT_ID "rds_program"

namespace merge {
void registerTypes(); /* antes de abrir o projeto */
void shutdown();      /* antes de desligar o motor */
} // namespace merge
