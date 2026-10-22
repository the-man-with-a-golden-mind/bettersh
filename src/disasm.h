#ifndef BETTERSH_DISASM_H
#define BETTERSH_DISASM_H

#include "chunk.h"

void disasm_ins(const Chunk *chunk, uint32_t ip, FILE *out);
void disasm_chunk(const Chunk *chunk, FILE *out);

#endif
